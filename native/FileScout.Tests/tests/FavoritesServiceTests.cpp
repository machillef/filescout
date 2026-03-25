#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/FavoritesService.h"

using namespace FileScoutNative::Shell;

namespace
{
    class ScopedEnvironmentVariable
    {
    public:
        ScopedEnvironmentVariable(std::wstring name, std::wstring value)
            : m_name(std::move(name))
        {
            auto const required = GetEnvironmentVariableW(m_name.c_str(), nullptr, 0);
            if (required > 0)
            {
                std::wstring buffer(required, L'\0');
                auto const written =
                    GetEnvironmentVariableW(m_name.c_str(), buffer.data(), required);
                if (written > 0)
                {
                    buffer.resize(written);
                    m_original = std::move(buffer);
                }
            }

            SetEnvironmentVariableW(m_name.c_str(), value.c_str());
        }

        ScopedEnvironmentVariable(ScopedEnvironmentVariable const&) = delete;
        ScopedEnvironmentVariable& operator=(ScopedEnvironmentVariable const&) = delete;

        ~ScopedEnvironmentVariable()
        {
            SetEnvironmentVariableW(
                m_name.c_str(),
                m_original.has_value() ? m_original->c_str() : nullptr);
        }

    private:
        std::wstring m_name;
        std::optional<std::wstring> m_original;
    };

    std::filesystem::path MakeUniqueTempDirectory(std::wstring_view const suffix)
    {
        auto base = std::filesystem::temp_directory_path() /
            (L"FileScoutFavoritesTests-" +
             std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(GetTickCount64()) + L"-" +
             std::wstring{ suffix });
        std::filesystem::create_directories(base);
        return base;
    }

    std::filesystem::path NormalizeForComparison(std::filesystem::path const& path)
    {
        std::error_code absoluteError;
        auto const absolute = std::filesystem::absolute(path, absoluteError);
        return absoluteError ? path.lexically_normal() : absolute.lexically_normal();
    }
}

class FavoritesServiceFixture : public ::testing::Test
{
protected:
    std::filesystem::path tempRoot;
    std::unique_ptr<ScopedEnvironmentVariable> localAppDataOverride;
    FavoritesService service;

    void SetUp() override
    {
        tempRoot = MakeUniqueTempDirectory(L"root");
        localAppDataOverride = std::make_unique<ScopedEnvironmentVariable>(
            L"LOCALAPPDATA",
            tempRoot.wstring());
    }

    void TearDown() override
    {
        localAppDataOverride.reset();
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
    }
};

TEST_F(FavoritesServiceFixture, AddPinnedFavoritePersistsAndReturnsPinnedSidebarEntry)
{
    auto const targetPath = tempRoot / L"Projects";
    std::filesystem::create_directories(targetPath);

    EXPECT_FALSE(service.IsSidebarFavorite(targetPath));
    EXPECT_TRUE(service.AddPinnedFavorite(targetPath));
    EXPECT_TRUE(service.IsSidebarFavorite(targetPath));

    auto const favorites = service.GetSidebarFavorites();
    auto const match = std::find_if(
        favorites.begin(),
        favorites.end(),
        [&](auto const& entry)
        {
            return entry.is_pinned &&
                !entry.path.empty() &&
                NormalizeForComparison(entry.path) == NormalizeForComparison(targetPath);
        });

    ASSERT_NE(match, favorites.end());
    EXPECT_EQ(match->display_name, L"Projects");

    auto const storagePath = tempRoot / L"FileScoutNative" / L"pinned-places.txt";
    ASSERT_TRUE(std::filesystem::exists(storagePath));

    std::ifstream input(storagePath, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    EXPECT_FALSE(content.empty());
}

TEST_F(FavoritesServiceFixture, AddPinnedFavoriteRejectsDuplicatePinnedPath)
{
    auto const targetPath = tempRoot / L"Screenshots";
    std::filesystem::create_directories(targetPath);

    EXPECT_TRUE(service.AddPinnedFavorite(targetPath));
    EXPECT_FALSE(service.AddPinnedFavorite(targetPath));

    size_t pinnedMatchCount = 0;
    for (auto const& entry : service.GetSidebarFavorites())
    {
        if (entry.is_pinned &&
            !entry.path.empty() &&
            NormalizeForComparison(entry.path) == NormalizeForComparison(targetPath))
        {
            ++pinnedMatchCount;
        }
    }

    EXPECT_EQ(pinnedMatchCount, 1u);
}

TEST_F(FavoritesServiceFixture, AddPinnedFavoriteRejectsBuiltinFavoritePath)
{
    auto const builtinFavorites = service.GetBuiltinFavorites();
    auto const builtin = std::find_if(
        builtinFavorites.begin(),
        builtinFavorites.end(),
        [](auto const& entry) { return !entry.path.empty(); });

    ASSERT_NE(builtin, builtinFavorites.end());
    EXPECT_TRUE(service.IsSidebarFavorite(builtin->path));
    EXPECT_FALSE(service.AddPinnedFavorite(builtin->path));
}

TEST_F(FavoritesServiceFixture, RemovePinnedFavorite_RemovesExistingEntry)
{
    auto const targetPath = tempRoot / L"Workspace";
    std::filesystem::create_directories(targetPath);

    ASSERT_TRUE(service.AddPinnedFavorite(targetPath));
    ASSERT_TRUE(service.IsSidebarFavorite(targetPath));

    EXPECT_TRUE(service.RemovePinnedFavorite(targetPath));
    EXPECT_FALSE(service.IsSidebarFavorite(targetPath));
}

TEST_F(FavoritesServiceFixture, RemovePinnedFavorite_ReturnsFalseWhenNotPinned)
{
    auto const targetPath = tempRoot / L"NotPinned";
    std::filesystem::create_directories(targetPath);

    EXPECT_FALSE(service.RemovePinnedFavorite(targetPath));
}

TEST_F(FavoritesServiceFixture, RemovePinnedFavorite_PersistsDeletion)
{
    auto const targetPath = tempRoot / L"ToRemove";
    std::filesystem::create_directories(targetPath);

    ASSERT_TRUE(service.AddPinnedFavorite(targetPath));
    ASSERT_TRUE(service.RemovePinnedFavorite(targetPath));

    // Fresh service instance reads the same storage file.
    FavoritesService freshService;
    EXPECT_FALSE(freshService.IsSidebarFavorite(targetPath));
}

TEST_F(FavoritesServiceFixture, RemovePinnedFavorite_GetSidebarFavoritesNoLongerContainsEntry)
{
    auto const pathA = tempRoot / L"FolderA";
    auto const pathB = tempRoot / L"FolderB";
    std::filesystem::create_directories(pathA);
    std::filesystem::create_directories(pathB);

    ASSERT_TRUE(service.AddPinnedFavorite(pathA));
    ASSERT_TRUE(service.AddPinnedFavorite(pathB));
    ASSERT_TRUE(service.RemovePinnedFavorite(pathA));

    bool aFound = false;
    bool bFound = false;
    for (auto const& entry : service.GetSidebarFavorites())
    {
        if (entry.is_pinned && !entry.path.empty())
        {
            if (NormalizeForComparison(entry.path) == NormalizeForComparison(pathA))
                aFound = true;
            if (NormalizeForComparison(entry.path) == NormalizeForComparison(pathB))
                bFound = true;
        }
    }
    EXPECT_FALSE(aFound);
    EXPECT_TRUE(bFound);
}
