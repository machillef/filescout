#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/ShellStaWorker.h"

#include <atomic>
#include <coroutine>
#include <future>
#include <stdexcept>
#include <vector>

using FileScoutNative::Shell::ShellStaWorker;

TEST(ShellStaWorker, RunsTasksInOrderOnOneStaThreadThatIsNotTheCaller)
{
    ShellStaWorker worker;
    std::vector<int> order;
    std::vector<DWORD> threads;
    std::vector<APTTYPE> apartments;
    std::promise<void> done;
    for (int i = 0; i < 3; ++i)
    {
        worker.Post([&, i]
        {
            order.push_back(i);
            threads.push_back(::GetCurrentThreadId());
            APTTYPE type{};
            APTTYPEQUALIFIER qualifier{};
            ::CoGetApartmentType(&type, &qualifier);
            apartments.push_back(type);
            if (i == 2) done.set_value();
        });
    }
    done.get_future().wait();

    EXPECT_EQ(order, (std::vector<int>{ 0, 1, 2 }));
    EXPECT_EQ(threads[0], threads[1]);
    EXPECT_EQ(threads[1], threads[2]);
    EXPECT_NE(threads[0], ::GetCurrentThreadId());
    for (auto const type : apartments)
    {
        EXPECT_TRUE(type == APTTYPE_STA || type == APTTYPE_MAINSTA) << static_cast<int>(type);
    }
}

TEST(ShellStaWorker, AThrowingTaskDoesNotStopTheWorker)
{
    ShellStaWorker worker;
    std::promise<int> value;
    worker.Post([] { throw std::runtime_error("boom"); });
    worker.Post([&] { value.set_value(42); });

    EXPECT_EQ(value.get_future().get(), 42);
}

TEST(ShellStaWorker, DestructorRunsQueuedTasksAndJoins)
{
    std::atomic<int> ran{ 0 };
    {
        ShellStaWorker worker;
        for (int i = 0; i < 5; ++i)
        {
            worker.Post([&] { ::Sleep(5); ran.fetch_add(1); });
        }
    }
    EXPECT_EQ(ran.load(), 5);
}

namespace
{
    struct Task
    {
        struct promise_type
        {
            Task get_return_object() noexcept { return {}; }
            std::suspend_never initial_suspend() noexcept { return {}; }
            std::suspend_never final_suspend() noexcept { return {}; }
            void return_void() noexcept {}
            void unhandled_exception() { std::terminate(); }
        };
    };

    Task RunAndCollect(ShellStaWorker& worker, std::promise<std::pair<int, DWORD>>& out, std::promise<std::string>& error)
    {
        auto const resume = [](std::function<void()> continuation) { continuation(); };
        auto const value = co_await FileScoutNative::Shell::RunOnShellThread(worker, resume, [] { return 7; });
        out.set_value({ value, ::GetCurrentThreadId() });
        try
        {
            co_await FileScoutNative::Shell::RunOnShellThread(worker, resume, []() -> int { throw std::runtime_error("op failed"); });
        }
        catch (std::runtime_error const& e)
        {
            error.set_value(e.what());
        }
    }
}

TEST(ShellStaWorker, AwaiterReturnsTheResultAndRethrowsOnTheResumer)
{
    ShellStaWorker worker;
    std::promise<std::pair<int, DWORD>> out;
    std::promise<std::string> error;
    RunAndCollect(worker, out, error);

    auto const [value, thread] = out.get_future().get();
    EXPECT_EQ(value, 7);
    EXPECT_NE(thread, ::GetCurrentThreadId());
    EXPECT_EQ(error.get_future().get(), "op failed");
}
