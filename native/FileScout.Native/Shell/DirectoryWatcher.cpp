#include "pch.h"
#include "Shell/DirectoryWatcher.h"
#include "Domain/WatchDebounce.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace FileScoutNative::Shell
{
    namespace
    {
        std::filesystem::path WithLongPathPrefix(std::filesystem::path path)
        {
            auto const& native = path.native();
            if (native.size() < 248 || native.starts_with(L"\\\\?\\"))
            {
                return path;
            }

            if (native.starts_with(L"\\\\"))
            {
                return std::filesystem::path{ L"\\\\?\\UNC\\" + native.substr(2) };
            }

            return std::filesystem::path{ L"\\\\?\\" + native };
        }

        constexpr DWORD kDirectoryNotifyFilter =
            FILE_NOTIFY_CHANGE_FILE_NAME |
            FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_ATTRIBUTES |
            FILE_NOTIFY_CHANGE_SIZE |
            FILE_NOTIFY_CHANGE_LAST_WRITE;

        constexpr DWORD kNotifyBufferBytes = 16u * 1024u;
    }

    DirectoryWatcher::~DirectoryWatcher()
    {
        Stop();
    }

    bool DirectoryWatcher::Start(
        std::filesystem::path directory,
        std::function<void()> onChanged,
        uint32_t const debounceMs,
        uint32_t const maxWaitMs)
    {
        Stop();

        if (directory.empty() || !onChanged)
        {
            return false;
        }

        auto const openPath = WithLongPathPrefix(directory);
        HANDLE const directoryHandle = ::CreateFileW(
            openPath.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);
        if (directoryHandle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        HANDLE const stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stopEvent == nullptr)
        {
            ::CloseHandle(directoryHandle);
            return false;
        }

        {
            std::lock_guard lock{ m_stateMutex };
            m_watchedPath = std::move(directory);
            m_stopEvent = stopEvent;
        }
        m_watching.store(true, std::memory_order_release);

        try
        {
            m_thread = std::thread(
                &DirectoryWatcher::WatchLoop,
                this,
                directoryHandle,
                stopEvent,
                std::move(onChanged),
                debounceMs,
                maxWaitMs);
        }
        catch (...)
        {
            m_watching.store(false, std::memory_order_release);
            {
                std::lock_guard lock{ m_stateMutex };
                m_watchedPath.clear();
                m_stopEvent = nullptr;
            }
            ::CloseHandle(stopEvent);
            ::CloseHandle(directoryHandle);
            return false;
        }

        return true;
    }

    void DirectoryWatcher::Stop()
    {
        HANDLE stopEvent = nullptr;
        std::thread threadToJoin;
        {
            std::lock_guard lock{ m_stateMutex };
            stopEvent = m_stopEvent;
            m_stopEvent = nullptr;
            m_watchedPath.clear();
            threadToJoin = std::move(m_thread);
        }

        if (stopEvent != nullptr)
        {
            ::SetEvent(stopEvent);
        }

        if (threadToJoin.joinable())
        {
            threadToJoin.join();
        }

        if (stopEvent != nullptr)
        {
            ::CloseHandle(stopEvent);
        }

        m_watching.store(false, std::memory_order_release);
    }

    bool DirectoryWatcher::IsWatching() const noexcept
    {
        return m_watching.load(std::memory_order_acquire);
    }

    std::filesystem::path DirectoryWatcher::WatchedPath() const
    {
        std::lock_guard lock{ m_stateMutex };
        return m_watchedPath;
    }

    void DirectoryWatcher::WatchLoop(
        HANDLE const directoryHandle,
        HANDLE const stopEvent,
        std::function<void()> onChanged,
        uint32_t const debounceMs,
        uint32_t const maxWaitMs)
    {
        HANDLE const ioEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ioEvent == nullptr)
        {
            ::CloseHandle(directoryHandle);
            m_watching.store(false, std::memory_order_release);
            return;
        }

        std::vector<std::byte> buffer(kNotifyBufferBytes);
        OVERLAPPED overlapped{};
        bool readPending = false;
        ::FileScoutNative::Domain::DirectoryWatchCoordinator coordinator{};

        auto emitChange = [&onChanged]()
        {
            try
            {
                onChanged();
            }
            catch (...)
            {
            }
        };

        for (;;)
        {
            if (::WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
            {
                break;
            }

            if (!readPending)
            {
                ::ResetEvent(ioEvent);
                overlapped = {};
                overlapped.hEvent = ioEvent;
                DWORD unusedBytes = 0;
                BOOL const issued = ::ReadDirectoryChangesW(
                    directoryHandle,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    FALSE,
                    kDirectoryNotifyFilter,
                    &unusedBytes,
                    &overlapped,
                    nullptr);
                if (!issued)
                {
                    if (coordinator.HasPending())
                    {
                        coordinator.MarkFlushed();
                        emitChange();
                    }
                    break;
                }
                readPending = true;
            }

            DWORD waitTimeoutMs = INFINITE;
            if (coordinator.HasPending())
            {
                auto const now = ::GetTickCount64();
                if (coordinator.ShouldFlush(now, debounceMs, maxWaitMs))
                {
                    coordinator.MarkFlushed();
                    emitChange();
                    continue;
                }
                uint64_t const remaining = coordinator.RemainingWaitMs(now, debounceMs, maxWaitMs);
                waitTimeoutMs = remaining == 0 ? 1u : static_cast<DWORD>((std::min<uint64_t>)(remaining, 60000u));
            }

            HANDLE handles[2] = { stopEvent, ioEvent };
            DWORD const waitResult = ::WaitForMultipleObjects(2, handles, FALSE, waitTimeoutMs);
            if (waitResult == WAIT_OBJECT_0)
            {
                break;
            }

            if (waitResult == WAIT_OBJECT_0 + 1)
            {
                readPending = false;
                DWORD bytesTransferred = 0;
                if (::GetOverlappedResult(directoryHandle, &overlapped, &bytesTransferred, FALSE))
                {
                    coordinator.NoteChange(::GetTickCount64());
                }
                else
                {
                    DWORD const error = ::GetLastError();
                    if (error == ERROR_NOTIFY_ENUM_DIR)
                    {
                        coordinator.NoteChange(::GetTickCount64());
                    }
                    else
                    {
                        // Watched directory vanished, unmounted, or lost access.
                        // Emit once if not stopping so the UI can react, then exit.
                        if (::WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0)
                        {
                            coordinator.MarkFlushed();
                            emitChange();
                        }
                        break;
                    }
                }
            }
            else if (waitResult == WAIT_TIMEOUT)
            {
                if (coordinator.HasPending() &&
                    coordinator.ShouldFlush(::GetTickCount64(), debounceMs, maxWaitMs))
                {
                    coordinator.MarkFlushed();
                    emitChange();
                }
            }
            else
            {
                break;
            }
        }

        if (readPending)
        {
            ::CancelIoEx(directoryHandle, &overlapped);
            DWORD ignored = 0;
            ::GetOverlappedResult(directoryHandle, &overlapped, &ignored, TRUE);
        }

        ::CloseHandle(ioEvent);
        ::CloseHandle(directoryHandle);
        m_watching.store(false, std::memory_order_release);
    }
}
