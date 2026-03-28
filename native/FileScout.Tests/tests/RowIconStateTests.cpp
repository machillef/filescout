#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/RowIconState.h"

using namespace FileScoutNative::Domain;

namespace
{
    ShellItemRef MakeRowIconItem(std::filesystem::path path, bool const isFolder = false)
    {
        ShellItemRef item;
        item.path = std::move(path);
        item.display_name = item.path.filename().wstring();
        item.kind_label = isFolder ? L"Folder" : L"File";
        item.is_folder = isFolder;
        return item;
    }
}

TEST(RowIconState, CachedRowIconIndices_FindsRowsThatShouldBeRehydrated)
{
    std::unordered_map<std::wstring, winrt::Microsoft::UI::Xaml::Media::ImageSource> cache;
    cache.emplace(L".txt", nullptr);
    cache.emplace(L"__folder__", nullptr);

    std::vector<ShellItemRef> items{
        MakeRowIconItem(L"C:\\temp\\alpha.txt"),
        MakeRowIconItem(L"C:\\temp\\folder", true),
        MakeRowIconItem(L"C:\\temp\\README"),
    };

    auto const indices = CachedRowIconIndices(items, cache);

    ASSERT_EQ(indices.size(), 2u);
    EXPECT_EQ(indices[0], 0u);
    EXPECT_EQ(indices[1], 1u);
}

TEST(RowIconState, RowIconCacheKey_NormalizesExtensionsAndFolders)
{
    EXPECT_EQ(RowIconCacheKey(MakeRowIconItem(L"C:\\temp\\alpha.TXT")), L".txt");
    EXPECT_EQ(RowIconCacheKey(MakeRowIconItem(L"C:\\temp\\folder", true)), L"__folder__");
    EXPECT_EQ(RowIconCacheKey(MakeRowIconItem(L"C:\\temp\\README")), L"__file__");
}
