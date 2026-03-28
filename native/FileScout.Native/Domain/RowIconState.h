#pragma once

#include "FileRowViewModel.h"

namespace FileScoutNative::Domain
{
    [[nodiscard]] inline std::wstring RowIconCacheKey(ShellItemRef const& item)
    {
        if (item.is_folder)
        {
            return L"__folder__";
        }

        auto ext = item.path.extension().wstring();
        if (ext.empty())
        {
            return L"__file__";
        }

        for (wchar_t& ch : ext)
        {
            ch = static_cast<wchar_t>(::towlower(ch));
        }

        return ext;
    }

    [[nodiscard]] inline std::vector<uint32_t> CachedRowIconIndices(
        std::vector<ShellItemRef> const& items,
        std::unordered_map<std::wstring, winrt::Microsoft::UI::Xaml::Media::ImageSource> const& iconCache)
    {
        std::vector<uint32_t> indices;
        indices.reserve(items.size());

        for (uint32_t index = 0; index < items.size(); ++index)
        {
            if (iconCache.contains(RowIconCacheKey(items[index])))
            {
                indices.push_back(index);
            }
        }

        return indices;
    }

}
