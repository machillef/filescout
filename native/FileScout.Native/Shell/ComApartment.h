#pragma once

namespace FileScoutNative::Shell
{
    // Joins the multithreaded apartment for the guard's lifetime. Put one at every background
    // entry point that makes COM or shell calls, so no call relies on the implicit MTA (which
    // exists only while some other thread keeps the MTA alive). Nesting is cheap and safe. On a
    // thread that is already STA, CoInitializeEx returns RPC_E_CHANGED_MODE: the thread keeps its
    // apartment, the guard leaves it alone, and COM calls still work.
    class ComApartment
    {
    public:
        ComApartment() noexcept
            : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
        {
        }

        ~ComApartment()
        {
            if (SUCCEEDED(m_result))
            {
                CoUninitialize();
            }
        }

        ComApartment(ComApartment const&) = delete;
        ComApartment& operator=(ComApartment const&) = delete;

        [[nodiscard]] bool IsUsable() const noexcept
        {
            return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
        }

    private:
        HRESULT m_result;
    };
}
