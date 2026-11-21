#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <windows.h>

namespace FileScoutNative::Shell
{
    // Watches a single filesystem directory (non-recursively) using overlapped ReadDirectoryChangesW
    // on a dedicated background thread. Opening the directory handle and waiting on cancelled I/O
    // happen off the caller's thread so slow or disconnected network shares never stall the UI.
    class DirectoryWatcher
    {
    public:
        DirectoryWatcher() = default;
        ~DirectoryWatcher();
        DirectoryWatcher(DirectoryWatcher const&) = delete;
        DirectoryWatcher& operator=(DirectoryWatcher const&) = delete;

        // Starts watching `directory` asynchronously. Stops any previous watch first.
        // `onChanged` receives the GetTickCount64() timestamp of the most recent filesystem event
        // in the coalesced batch. `onReady` (optional) is invoked on the worker thread with `true`
        // right after the first ReadDirectoryChangesW request is armed, or `false` on failure.
        // Callers must ensure any state captured by callbacks remains valid (e.g. via shared/weak
        // ownership), because Stop() uses a bounded shutdown wait and does not drain in-flight callbacks.
        bool Start(
            std::filesystem::path directory,
            std::function<void(uint64_t)> onChanged,
            uint32_t debounceMs = 300,
            uint32_t maxWaitMs = 1500,
            std::function<void(bool)> onReady = nullptr);

        // Requests shutdown without blocking indefinitely on unresponsive network I/O.
        // Local watches join cleanly within a short grace period; hung remote I/O finishes on the
        // detached worker while keeping its OVERLAPPED and buffer alive until completion.
        // Does not guarantee draining an already-dispatched callback if the grace period expires.
        void Stop();

        // Waits up to `timeoutMs` for the worker to submit its first ReadDirectoryChangesW call.
        // Returns true if armed, false on startup failure, stop, or timeout.
        [[nodiscard]] bool WaitUntilArmed(uint32_t timeoutMs) const;

        [[nodiscard]] bool IsWatching() const noexcept;
        [[nodiscard]] std::filesystem::path WatchedPath() const;

    private:
        struct Session;
        static void RunSession(
            std::shared_ptr<Session> session,
            uint32_t debounceMs,
            uint32_t maxWaitMs);

        mutable std::mutex m_stateMutex;
        std::filesystem::path m_watchedPath;
        std::shared_ptr<Session> m_session;
        std::thread m_thread;
        std::atomic<bool> m_watching{ false };
    };
}
