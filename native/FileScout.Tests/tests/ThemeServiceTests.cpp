#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/ThemeService.h"

using namespace FileScoutNative::Domain;

namespace
{
    class ScopedEnvironmentVariable
    {
    public:
        ScopedEnvironmentVariable(std::wstring name, std::optional<std::wstring> value)
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

            SetEnvironmentVariableW(
                m_name.c_str(),
                value.has_value() ? value->c_str() : nullptr);
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

    class ScopedCurrentPath
    {
    public:
        explicit ScopedCurrentPath(std::filesystem::path path)
            : m_original(std::filesystem::current_path())
        {
            std::filesystem::current_path(std::move(path));
        }

        ScopedCurrentPath(ScopedCurrentPath const&) = delete;
        ScopedCurrentPath& operator=(ScopedCurrentPath const&) = delete;

        ~ScopedCurrentPath()
        {
            std::error_code ec;
            std::filesystem::current_path(m_original, ec);
        }

    private:
        std::filesystem::path m_original;
    };

    std::filesystem::path MakeUniqueTempDirectory(std::wstring_view const suffix)
    {
        auto base = std::filesystem::temp_directory_path() /
            (L"FileScoutThemeTests-" +
             std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(GetTickCount64()) + L"-" +
             std::wstring{ suffix });
        std::filesystem::create_directories(base);
        return base;
    }

    std::array<std::uint32_t, 10> ThemeTokens(ThemeDefinition const& theme)
    {
        return {
            theme.base,
            theme.surface,
            theme.surface_alt,
            theme.accent,
            theme.accent_secondary,
            theme.text_primary,
            theme.text_secondary,
            theme.border,
            theme.border_subtle,
            theme.hover,
        };
    }
}

class ThemeServiceFixture : public ::testing::Test
{
protected:
    std::filesystem::path tempRoot;
    std::unique_ptr<ScopedEnvironmentVariable> localAppDataOverride;
    std::unique_ptr<ScopedCurrentPath> currentPathOverride;

    void SetUp() override
    {
        tempRoot = MakeUniqueTempDirectory(L"root");
        localAppDataOverride = std::make_unique<ScopedEnvironmentVariable>(
            L"LOCALAPPDATA",
            tempRoot.wstring());
        currentPathOverride = std::make_unique<ScopedCurrentPath>(tempRoot);
    }

    void TearDown() override
    {
        currentPathOverride.reset();
        localAppDataOverride.reset();
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
    }
};

TEST(ThemeDefinition, DefaultDarkHasTenNonZeroTokens)
{
    for (auto const token : ThemeTokens(ThemeDefinition::DefaultDark()))
    {
        EXPECT_NE(token, 0u);
    }
}

TEST(ThemeDefinition, TokyoNightHasTenNonZeroTokens)
{
    for (auto const token : ThemeTokens(ThemeDefinition::TokyoNight()))
    {
        EXPECT_NE(token, 0u);
    }
}

TEST(ThemeDefinition, CatppuccinMochaHasTenNonZeroTokens)
{
    for (auto const token : ThemeTokens(ThemeDefinition::CatppuccinMocha()))
    {
        EXPECT_NE(token, 0u);
    }
}

TEST(ThemeService, BuildThemeResourceEntries_ReturnsExtendedMappedBrushKeys)
{
    auto const entries = BuildThemeResourceEntries(ThemeDefinition::TokyoNight());
    auto const colorFor = [&entries](std::wstring_view const key) -> std::optional<std::uint32_t>
    {
        auto const it = std::find_if(
            entries.begin(),
            entries.end(),
            [key](ThemeResourceEntry const& entry) { return entry.resource_key == key; });
        if (it == entries.end())
        {
            return std::nullopt;
        }

        return it->color;
    };

    EXPECT_EQ(entries.size(), 19u);
    EXPECT_EQ(colorFor(L"ApplicationPageBackgroundThemeBrush"), 0xFF1A1B26u);
    EXPECT_EQ(colorFor(L"CardBackgroundFillColorSecondaryBrush"), 0xFF1F2335u);
    EXPECT_EQ(colorFor(L"LayerFillColorAltBrush"), 0xFF1F2335u);
    EXPECT_EQ(colorFor(L"ControlFillColorDefaultBrush"), 0xFF24283Bu);
    EXPECT_EQ(colorFor(L"ControlStrokeColorDefaultBrush"), 0xFF29293Fu);
    EXPECT_EQ(colorFor(L"ExpanderHeaderBackground"), 0xFF1F2335u);
    EXPECT_EQ(colorFor(L"ExpanderHeaderForeground"), 0xFFC0CAF5u);
    EXPECT_EQ(colorFor(L"ExpanderContentBackground"), 0xFF1F2335u);
    EXPECT_EQ(colorFor(L"AccentFillColorSecondaryBrush"), 0xFFBB9AF7u);
    EXPECT_EQ(colorFor(L"SubtleFillColorSecondaryBrush"), 0x14C0CAF5u);
}

TEST(ThemeService, BuildCaptionButtonPalette_HoverAndPressedAreLighterThanBaseForAllThemes)
{
    auto const luma = [](std::uint32_t const argb)
    {
        auto const r = (argb >> 16) & 0xFFu;
        auto const g = (argb >> 8) & 0xFFu;
        auto const b = argb & 0xFFu;
        return r + g + b;
    };

    for (auto const& theme : ThemeService::GetAllThemes())
    {
        auto const palette = BuildCaptionButtonPalette(theme);
        EXPECT_EQ(palette.foreground, theme.text_primary);
        EXPECT_EQ(palette.inactive_foreground, theme.text_secondary);
        EXPECT_EQ(palette.hover_background & 0xFF000000u, 0xFF000000u);
        EXPECT_EQ(palette.pressed_background & 0xFF000000u, 0xFF000000u);
        EXPECT_GT(luma(palette.hover_background), luma(palette.pressed_background));
        EXPECT_GT(luma(palette.pressed_background), luma(theme.base));
    }
}

TEST(ThemeService, GetAllThemes_ReturnsThreeThemesInDropdownOrder)
{
    auto const themes = ThemeService::GetAllThemes();

    ASSERT_EQ(themes.size(), 3u);
    EXPECT_EQ(themes[0].id, L"default-dark");
    EXPECT_EQ(themes[0].display_name, L"Default Dark");
    EXPECT_EQ(themes[1].id, L"tokyo-night");
    EXPECT_EQ(themes[1].display_name, L"Tokyo Night");
    EXPECT_EQ(themes[2].id, L"catppuccin-mocha");
    EXPECT_EQ(themes[2].display_name, L"Catppuccin Mocha");
}

TEST_F(ThemeServiceFixture, SaveThemeIdAndLoadSavedThemeId_RoundTrip)
{
    ThemeService::SaveThemeId(L"tokyo-night");
    EXPECT_EQ(ThemeService::LoadSavedThemeId(), L"tokyo-night");
}

TEST_F(ThemeServiceFixture, LoadSavedThemeId_MissingFileReturnsDefaultDark)
{
    EXPECT_EQ(ThemeService::LoadSavedThemeId(), L"default-dark");
}

TEST_F(ThemeServiceFixture, LoadSavedThemeId_UnknownIdReturnsDefaultDark)
{
    auto const storagePath = tempRoot / L"FileScoutNative" / L"theme.txt";
    std::filesystem::create_directories(storagePath.parent_path());

    std::ofstream output(storagePath, std::ios::binary | std::ios::trunc);
    output << "mystery-theme\r\n";
    output.close();

    EXPECT_EQ(ThemeService::LoadSavedThemeId(), L"default-dark");
}

TEST_F(ThemeServiceFixture, SaveThemeId_CreatesFirstRunDirectory)
{
    auto const storageDir = tempRoot / L"FileScoutNative";
    ASSERT_FALSE(std::filesystem::exists(storageDir));

    ThemeService::SaveThemeId(L"catppuccin-mocha");

    EXPECT_TRUE(std::filesystem::exists(storageDir));
    EXPECT_TRUE(std::filesystem::exists(storageDir / L"theme.txt"));
}

TEST_F(ThemeServiceFixture, SaveThemeId_WithoutLocalAppDataFallsBackToCurrentDirectory)
{
    localAppDataOverride.reset();
    localAppDataOverride = std::make_unique<ScopedEnvironmentVariable>(
        L"LOCALAPPDATA",
        std::nullopt);

    ThemeService::SaveThemeId(L"tokyo-night");

    auto const storagePath = tempRoot / L"theme.txt";
    ASSERT_TRUE(std::filesystem::exists(storagePath));
    EXPECT_EQ(ThemeService::LoadSavedThemeId(), L"tokyo-night");
}

// D-10: LOCALAPPDATA of 260+ characters must still be honoured (GetEnvironmentVariableW with a
// MAX_PATH buffer returns the needed size and writes nothing), never a cwd-relative theme.txt.
TEST_F(ThemeServiceFixture, SaveThemeId_LongLocalAppDataIsNotReplacedByARelativePath)
{
    auto longRoot = tempRoot;
    while (longRoot.wstring().size() < 300)
    {
        longRoot /= std::wstring(40, L'x');
    }
    ScopedEnvironmentVariable longLocalAppData{ L"LOCALAPPDATA", longRoot.wstring() };

    EXPECT_NO_THROW(ThemeService::SaveThemeId(L"tokyo-night"));

    // The fixture's current directory is tempRoot: a relative or current-directory fallback would
    // land here, so neither location may exist, and the file must be under the long LOCALAPPDATA.
    EXPECT_FALSE(std::filesystem::exists(tempRoot / L"FileScoutNative" / L"theme.txt"));
    EXPECT_FALSE(std::filesystem::exists(tempRoot / L"theme.txt"));
    EXPECT_TRUE(std::filesystem::exists(
        std::filesystem::path{ L"\\\\?\\" + (longRoot / L"FileScoutNative" / L"theme.txt").wstring() }));
    EXPECT_EQ(ThemeService::LoadSavedThemeId(), L"tokyo-night");

    std::error_code ec;
    std::filesystem::remove_all(
        std::filesystem::path{ L"\\\\?\\" + (tempRoot / std::wstring(40, L'x')).wstring() }, ec);
}

// D-10: a folder that cannot be created must not throw out of SaveThemeId.
TEST_F(ThemeServiceFixture, SaveThemeId_DoesNotThrowWhenTheFolderCannotBeCreated)
{
    auto const blocker = tempRoot / L"blocker";
    std::ofstream{ blocker } << "a file where a folder should be";
    ScopedEnvironmentVariable blockedLocalAppData{ L"LOCALAPPDATA", blocker.wstring() };

    EXPECT_NO_THROW(ThemeService::SaveThemeId(L"tokyo-night"));
    EXPECT_EQ(ThemeService::LoadSavedThemeId(), L"default-dark");
}
