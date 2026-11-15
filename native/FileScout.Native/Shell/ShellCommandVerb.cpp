#include "pch.h"
#include "Shell/ShellCommandVerb.h"

namespace FileScoutNative::Shell
{
    std::wstring ReadShellCommandVerb(IContextMenu* const contextMenu, UINT const commandOffset)
    {
        constexpr std::size_t maxVerbLength = 32 * 1024;
        for (std::size_t capacity = 256;; capacity = (std::min)(capacity * 4, maxVerbLength))
        {
            std::wstring buffer(capacity, L'\0');
            auto const result = contextMenu->GetCommandString(
                commandOffset,
                GCS_VERBW,
                nullptr,
                reinterpret_cast<LPSTR>(buffer.data()),
                static_cast<UINT>(buffer.size()));
            bool const atLimit = capacity == maxVerbLength;
            if (result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
            {
                if (atLimit) return {};
                continue;
            }

            if (FAILED(result))
            {
                return {};
            }

            auto const length = wcsnlen(buffer.c_str(), buffer.size());
            // A handler may truncate to the buffer and still return S_OK: a full buffer means "try
            // bigger"; at the 32K limit a full buffer may still be truncated, so no verb is returned.
            if (length + 1 >= buffer.size())
            {
                if (atLimit) return {};
                continue;
            }
            buffer.resize(length);
            return buffer;
        }

        return {};
    }

    bool IsBlockedShellVerb(std::wstring_view const verb) noexcept
    {
        constexpr std::wstring_view blocked[]{ L"copy", L"cut", L"paste" };
        for (auto const candidate : blocked)
        {
            if (CompareStringOrdinal(verb.data(), static_cast<int>(verb.size()),
                    candidate.data(), static_cast<int>(candidate.size()), TRUE) == CSTR_EQUAL)
            {
                return true;
            }
        }

        return false;
    }
}
