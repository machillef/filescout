#pragma once

#include <condition_variable>
#include <coroutine>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <windows.h>

namespace FileScoutNative::Shell
{
    class ShellStaWorker
    {
    public:
        ShellStaWorker();
        ~ShellStaWorker();
        ShellStaWorker(ShellStaWorker const&) = delete;
        ShellStaWorker& operator=(ShellStaWorker const&) = delete;

        void Post(std::function<void()> task);

    private:
        void Run();

        std::mutex m_mutex;
        std::deque<std::function<void()>> m_tasks;
        HANDLE m_wake = nullptr;
        bool m_stopping = false;
        std::thread m_thread;
    };

    template <typename Resume, typename Fn>
    auto RunOnShellThread(ShellStaWorker& worker, Resume resume, Fn fn)
    {
        using Result = std::invoke_result_t<Fn&>;
        struct Awaiter
        {
            ShellStaWorker& worker;
            Resume resume;
            Fn fn;
            std::conditional_t<std::is_void_v<Result>, std::monostate, std::optional<Result>> result{};
            std::exception_ptr error;

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle)
            {
                worker.Post([this, handle]
                {
                    try
                    {
                        if constexpr (std::is_void_v<Result>) { fn(); }
                        else { result.emplace(fn()); }
                    }
                    catch (...)
                    {
                        error = std::current_exception();
                    }
                    resume([handle] { handle.resume(); });
                });
            }

            Result await_resume()
            {
                if (error) { std::rethrow_exception(error); }
                if constexpr (!std::is_void_v<Result>) { return std::move(*result); }
            }
        };
        return Awaiter{ worker, std::move(resume), std::move(fn) };
    }
}
