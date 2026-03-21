#pragma once

#include <array>
#include <cwctype>
#include <optional>
#include <string>
#include <algorithm>

namespace FileScoutNative::Domain
{
    namespace detail
    {
        inline std::wstring ToUpperInvariantForFileNameValidation(std::wstring value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](wchar_t const character)
                {
                    return static_cast<wchar_t>(std::towupper(character));
                });
            return value;
        }
    }

    inline std::optional<std::wstring> ValidateWindowsFileName(std::wstring const& name)
    {
        if (name.empty())
        {
            return std::wstring{ L"File name cannot be empty." };
        }

        if (name == L"." || name == L"..")
        {
            return std::wstring{ L"File name cannot be '.' or '..'." };
        }

        static constexpr std::array<wchar_t, 9> invalidChars{
            L'/', L'\\', L':', L'*', L'?', L'"', L'<', L'>', L'|' };
        if (auto const invalidChar = std::ranges::find_if(name, [](wchar_t const character)
                {
                    return character < 32 ||
                        std::ranges::find(invalidChars, character) != invalidChars.end();
                });
            invalidChar != name.end())
        {
            return std::wstring{ L"File name contains an invalid character." };
        }

        if (name.back() == L' ' || name.back() == L'.')
        {
            return std::wstring{ L"File name cannot end with a space or period." };
        }

        auto stem = detail::ToUpperInvariantForFileNameValidation(name);
        if (auto const extensionSeparator = stem.find(L'.'); extensionSeparator != std::wstring::npos)
        {
            stem = stem.substr(0, extensionSeparator);
        }

        while (!stem.empty() && (stem.back() == L' ' || stem.back() == L'.'))
        {
            stem.pop_back();
        }

        static constexpr std::array reservedNames{
            L"CON",
            L"PRN",
            L"AUX",
            L"NUL",
            L"COM1",
            L"COM2",
            L"COM3",
            L"COM4",
            L"COM5",
            L"COM6",
            L"COM7",
            L"COM8",
            L"COM9",
            L"LPT1",
            L"LPT2",
            L"LPT3",
            L"LPT4",
            L"LPT5",
            L"LPT6",
            L"LPT7",
            L"LPT8",
            L"LPT9",
        };
        if (std::ranges::find(reservedNames, stem) != reservedNames.end())
        {
            return std::wstring{ L"That name is reserved on Windows." };
        }

        return std::nullopt;
    }
}
