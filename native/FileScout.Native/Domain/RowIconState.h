#pragma once

#include "IconCacheKey.h"
#include "FileRowViewModel.h"

namespace FileScoutNative::Domain
{
    [[nodiscard]] inline std::wstring RowIconCacheKey(ShellItemRef const& item)
    {
        return BuildFileIconCacheKey(item.path, item.is_folder);
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
