#include "pch.h"
#include "Shell/ShellStaWorker.h"

#include <objbase.h>

namespace FileScoutNative::Shell
{
    ShellStaWorker::ShellStaWorker()
        : m_wake(::CreateEventW(nullptr, FALSE, FALSE, nullptr))
    {
        m_thread = std::thread([this] { Run(); });
    }

    ShellStaWorker::~ShellStaWorker()
    {
        {
            std::lock_guard lock{ m_mutex };
            m_stopping = true;
        }
        ::SetEvent(m_wake);
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        ::CloseHandle(m_wake);
    }

    void ShellStaWorker::Post(std::function<void()> task)
    {
        {
            std::lock_guard lock{ m_mutex };
            m_tasks.push_back(std::move(task));
        }
        ::SetEvent(m_wake);
    }

    void ShellStaWorker::Run()
    {
        HRESULT const init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        for (;;)
        {
            // Drain everything queued, then wait for more while pumping messages (an STA must).
            for (;;)
            {
                std::function<void()> task;
                {
                    std::lock_guard lock{ m_mutex };
                    if (m_tasks.empty())
                    {
                        break;
                    }
                    task = std::move(m_tasks.front());
                    m_tasks.pop_front();
                }
                try
                {
                    task();
                }
                catch (...)
                {
                    // A failed operation reports through its own continuation; the worker lives on.
                }
            }
            {
                std::lock_guard lock{ m_mutex };
                if (m_stopping && m_tasks.empty())
                {
                    break;
                }
            }
            DWORD const wait = ::MsgWaitForMultipleObjectsEx(1, &m_wake, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (wait == WAIT_OBJECT_0 + 1)
            {
                MSG message{};
                while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    ::TranslateMessage(&message);
                    ::DispatchMessageW(&message);
                }
            }
        }
        if (SUCCEEDED(init))
        {
            ::CoUninitialize();
        }
    }
}
