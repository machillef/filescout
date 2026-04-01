#pragma once

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace FileScoutNative::Domain
{
    enum class AddressNavigationTargetKind
    {
        DrivesView,
        Directory,
        File,
        ShellNamespace,
        Invalid,
    };

    struct AddressNavigationTarget
    {
        AddressNavigationTargetKind kind{ AddressNavigationTargetKind::Invalid };
        std::filesystem::path path;
    };

    inline std::wstring TrimAddressNavigationInput(std::wstring_view const rawInput)
    {
        auto const begin = std::find_if_not(
            rawInput.begin(),
            rawInput.end(),
            [](wchar_t const ch) { return std::iswspace(ch) != 0; });
        if (begin == rawInput.end())
        {
            return {};
        }

        auto const end = std::find_if_not(
            rawInput.rbegin(),
            rawInput.rend(),
            [](wchar_t const ch) { return std::iswspace(ch) != 0; }).base();
        return std::wstring{ begin, end };
    }

    inline bool IsThisPcAddressNavigationTarget(std::wstring_view value)
    {
        static constexpr std::wstring_view expected = L"This PC";
        if (value.size() != expected.size())
        {
            return false;
        }

        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            if (std::towupper(value[index]) != std::towupper(expected[index]))
            {
                return false;
            }
        }

        return true;
    }

    inline bool IsDriveLetterOnlyAddressNavigationTarget(std::wstring_view value)
    {
        return value.size() == 2 &&
            std::iswalpha(value[0]) != 0 &&
            value[1] == L':';
    }

    inline bool IsShellNamespaceAddressNavigationTarget(std::wstring_view value)
    {
        return value.size() >= 2 &&
            value[0] == L'\\' &&
            value[1] == L'\\';
    }

    template <typename ExistsFn, typename DirectoryFn, typename ShellNamespaceFn>
    AddressNavigationTarget ResolveAddressNavigationTarget(
        std::wstring_view const rawInput,
        ExistsFn&& existsFn,
        DirectoryFn&& directoryFn,
        ShellNamespaceFn&& shellNamespaceFn)
    {
        auto const trimmed = TrimAddressNavigationInput(rawInput);
        if (trimmed.empty())
        {
            return {};
        }

        if (IsThisPcAddressNavigationTarget(trimmed))
        {
            return AddressNavigationTarget{
                AddressNavigationTargetKind::DrivesView,
                {},
            };
        }

        if (IsDriveLetterOnlyAddressNavigationTarget(trimmed))
        {
            return AddressNavigationTarget{
                AddressNavigationTargetKind::Directory,
                std::filesystem::path{ std::wstring{ trimmed } + L"\\" },
            };
        }

        std::filesystem::path const targetPath{ trimmed };
        if (!targetPath.is_absolute() && !IsShellNamespaceAddressNavigationTarget(trimmed))
        {
            return {};
        }

        if (existsFn(targetPath))
        {
            return AddressNavigationTarget{
                directoryFn(targetPath)
                    ? AddressNavigationTargetKind::Directory
                    : AddressNavigationTargetKind::File,
                targetPath,
            };
        }

        if (shellNamespaceFn(targetPath))
        {
            return AddressNavigationTarget{
                AddressNavigationTargetKind::ShellNamespace,
                targetPath,
            };
        }

        return {};
    }
}
