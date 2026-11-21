#pragma once

#include <cwctype>
#include <filesystem>
#include <string>
#include "TextFold.h"

namespace FileScoutNative::Domain
{
    [[nodiscard]] inline std::wstring LowercaseCopy(std::wstring const& value)
    {
        return FoldCase(value);
    }

    [[nodiscard]] inline bool UsesPerItemIconIdentity(
        std::filesystem::path const& path,
        bool const isFolder)
    {
        if (isFolder)
        {
            return false;
        }

        return LowercaseCopy(path.extension().wstring()) == L".exe";
    }

    [[nodiscard]] inline bool UsesLiteralIconLookup(
        std::filesystem::path const& path,
        bool const isFolder)
    {
        return isFolder
            ? (path == path.root_path() && !path.root_name().empty())
            : UsesPerItemIconIdentity(path, false);
    }

    [[nodiscard]] inline std::wstring BuildFileIconCacheKey(
        std::filesystem::path const& path,
        bool const isFolder)
    {
        if (UsesLiteralIconLookup(path, isFolder))
        {
            return L"__item__:" + LowercaseCopy(path.lexically_normal().wstring());
        }

        if (isFolder)
        {
            return L"__folder__";
        }

        auto const extension = LowercaseCopy(path.extension().wstring());
        if (extension.empty())
        {
            return L"__file__";
        }

        return extension;
    }
}
