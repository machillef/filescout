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
        std::filesystem::path NormalizeForCreateFile(std::filesystem::path path)
        {
            auto const& raw = path.native();
            if (raw.starts_with(L"\\\\?\\"))
            {
                return path;
            }

            std::error_code ec;
            auto normalized = std::filesystem::absolute(path, ec);
            if (ec)
            {
                normalized = std::move(path);
            }
            normalized = normalized.lexically_normal();
            normalized.make_preferred();

            auto const& native = normalized.native();
            if (native.size() < 248)
            {
                return normalized;
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
        constexpr DWORD kStopJoinGraceMs = 250u;
    }

    struct DirectoryWatcher::Session
    {
        std::filesystem::path directory;
        std::function<void(uint64_t)> onChanged;
        std::function<void(bool)> onReady;
        HANDLE stopEvent{ nullptr };
        HANDLE readyEvent{ nullptr };
        HANDLE doneEvent{ nullptr };
        std::atomic<bool> stopped{ false };
        std::atomic<bool> armed{ false };
        std::atomic<bool> active{ true };

        ~Session()
        {
            if (doneEvent != nullptr) { ::CloseHandle(doneEvent); }
            if (readyEvent != nullptr) { ::CloseHandle(readyEvent); }
            if (stopEvent != nullptr) { ::CloseHandle(stopEvent); }
        }
    };

    DirectoryWatcher::~DirectoryWatcher()
    {
        Stop();
    }

    bool DirectoryWatcher::Start(
        std::filesystem::path directory,
        std::function<void(uint64_t)> onChanged,
        uint32_t const debounceMs,
        uint32_t const maxWaitMs,
        std::function<void(bool)> onReady)
    {
        Stop();

        if (directory.empty() || !onChanged)
        {
            return false;
        }

        auto session = std::make_shared<Session>();
        session->directory = directory;
        session->onChanged = std::move(onChanged);
        session->onReady = std::move(onReady);
        session->stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        session->readyEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        session->doneEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (session->stopEvent == nullptr || session->readyEvent == nullptr || session->doneEvent == nullptr)
        {
            return false;
        }

        {
            std::lock_guard lock{ m_stateMutex };
            m_watchedPath = std::move(directory);
            m_session = session;
        }
        m_watching.store(true, std::memory_order_release);

        try
        {
            m_thread = std::thread(
                &DirectoryWatcher::RunSession,
                session,
                debounceMs,
                maxWaitMs);
        }
        catch (...)
        {
            m_watching.store(false, std::memory_order_release);
            {
                std::lock_guard lock{ m_stateMutex };
                m_watchedPath.clear();
                m_session.reset();
            }
            return false;
        }

        return true;
    }

    void DirectoryWatcher::Stop()
    {
        std::shared_ptr<Session> session;
        std::thread threadToFinish;
        {
            std::lock_guard lock{ m_stateMutex };
            session = std::move(m_session);
            m_watchedPath.clear();
            threadToFinish = std::move(m_thread);
        }

        m_watching.store(false, std::memory_order_release);

        if (session)
        {
            session->stopped.store(true, std::memory_order_release);
            ::SetEvent(session->stopEvent);
        }

        if (threadToFinish.joinable())
        {
            bool finishedQuickly = false;
            if (session && session->doneEvent != nullptr)
            {
                finishedQuickly =
                    (::WaitForSingleObject(session->doneEvent, kStopJoinGraceMs) == WAIT_OBJECT_0);
            }

            if (finishedQuickly)
            {
                threadToFinish.join();
            }
            else
            {
                // Keep the worker thread (and its stack OVERLAPPED / buffer / handles) alive
                // until the OS completes any unresponsive remote I/O cancellation.
                threadToFinish.detach();
            }
        }
    }

    bool DirectoryWatcher::WaitUntilArmed(uint32_t const timeoutMs) const
    {
        std::shared_ptr<Session> session;
        {
            std::lock_guard lock{ m_stateMutex };
            session = m_session;
        }
        if (!session || session->readyEvent == nullptr)
        {
            return false;
        }

        if (::WaitForSingleObject(session->readyEvent, timeoutMs) != WAIT_OBJECT_0)
        {
            return false;
        }
        return session->armed.load(std::memory_order_acquire) &&
            !session->stopped.load(std::memory_order_acquire);
    }

    bool DirectoryWatcher::IsWatching() const noexcept
    {
        if (!m_watching.load(std::memory_order_acquire))
        {
            return false;
        }
        std::lock_guard lock{ m_stateMutex };
        return m_session && m_session->active.load(std::memory_order_acquire);
    }

    std::filesystem::path DirectoryWatcher::WatchedPath() const
    {
        std::lock_guard lock{ m_stateMutex };
        return m_watchedPath;
    }

    void DirectoryWatcher::RunSession(
        std::shared_ptr<Session> session,
        uint32_t const debounceMs,
        uint32_t const maxWaitMs)
    {
        struct DoneGuard
        {
            std::shared_ptr<Session>& session;
            ~DoneGuard()
            {
                session->active.store(false, std::memory_order_release);
                if (session->readyEvent != nullptr)
                {
                    ::SetEvent(session->readyEvent);
                }
                if (session->doneEvent != nullptr)
                {
                    ::SetEvent(session->doneEvent);
                }
            }
        } doneGuard{ session };

        auto notifyReady = [&session](bool const ok)
        {
            session->armed.store(ok, std::memory_order_release);
            if (!ok)
            {
                session->active.store(false, std::memory_order_release);
            }
            if (!session->stopped.load(std::memory_order_acquire) && session->onReady)
            {
                try
                {
                    session->onReady(ok);
                }
                catch (...)
                {
                }
            }
            ::SetEvent(session->readyEvent);
        };

        auto emitChange = [&session](uint64_t const eventTickMs)
        {
            if (session->stopped.load(std::memory_order_acquire))
            {
                return;
            }
            try
            {
                session->onChanged(eventTickMs);
            }
            catch (...)
            {
            }
        };

        if (session->stopped.load(std::memory_order_acquire))
        {
            notifyReady(false);
            return;
        }

        auto const openPath = NormalizeForCreateFile(session->directory);
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
            notifyReady(false);
            return;
        }

        HANDLE const ioEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ioEvent == nullptr)
        {
            ::CloseHandle(directoryHandle);
            notifyReady(false);
            return;
        }

        std::vector<std::byte> buffer(kNotifyBufferBytes);
        OVERLAPPED overlapped{};
        bool readPending = false;
        bool signaledReady = false;
        ::FileScoutNative::Domain::DirectoryWatchCoordinator coordinator{};

        for (;;)
        {
            if (session->stopped.load(std::memory_order_acquire) ||
                ::WaitForSingleObject(session->stopEvent, 0) == WAIT_OBJECT_0)
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
                    if (!signaledReady)
                    {
                        signaledReady = true;
                        notifyReady(false);
                    }
                    else if (coordinator.HasPending())
                    {
                        uint64_t const eventTick = coordinator.LastEventTickMs();
                        coordinator.MarkFlushed();
                        emitChange(eventTick);
                    }
                    break;
                }
                readPending = true;
                if (!signaledReady)
                {
                    signaledReady = true;
                    notifyReady(true);
                }
            }

            DWORD waitTimeoutMs = INFINITE;
            if (coordinator.HasPending())
            {
                auto const now = ::GetTickCount64();
                if (coordinator.ShouldFlush(now, debounceMs, maxWaitMs))
                {
                    uint64_t const eventTick = coordinator.LastEventTickMs();
                    coordinator.MarkFlushed();
                    emitChange(eventTick);
                    continue;
                }
                uint64_t const remaining = coordinator.RemainingWaitMs(now, debounceMs, maxWaitMs);
                waitTimeoutMs = remaining == 0 ? 1u : static_cast<DWORD>((std::min<uint64_t>)(remaining, 60000u));
            }

            HANDLE handles[2] = { session->stopEvent, ioEvent };
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
                        if (!session->stopped.load(std::memory_order_acquire) &&
                            ::WaitForSingleObject(session->stopEvent, 0) != WAIT_OBJECT_0)
                        {
                            coordinator.MarkFlushed();
                            emitChange(::GetTickCount64());
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
                    uint64_t const eventTick = coordinator.LastEventTickMs();
                    coordinator.MarkFlushed();
                    emitChange(eventTick);
                }
            }
            else
            {
                break;
            }
        }

        if (!signaledReady)
        {
            notifyReady(false);
        }

        if (readPending)
        {
            ::CancelIoEx(directoryHandle, &overlapped);
            DWORD ignored = 0;
            ::GetOverlappedResult(directoryHandle, &overlapped, &ignored, TRUE);
        }

        ::CloseHandle(ioEvent);
        ::CloseHandle(directoryHandle);
    }
}
