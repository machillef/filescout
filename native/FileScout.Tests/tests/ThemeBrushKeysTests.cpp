#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/ThemeBrushKeys.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

using namespace FileScoutNative::Domain;

namespace
{
    std::filesystem::path NativeDir()
    {
        return std::filesystem::path{ __FILE__ }.parent_path().parent_path().parent_path();
    }

    std::string ReadFile(std::filesystem::path const& path)
    {
        std::ifstream stream{ path, std::ios::binary };
        EXPECT_TRUE(stream.is_open()) << "Failed to open " << path.string();
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

    std::string Narrow(std::wstring_view const value)
    {
        // Brush keys are ASCII, so each wchar_t fits in a char.
        std::string narrow;
        narrow.reserve(value.size());
        for (auto const ch : value)
        {
            narrow.push_back(static_cast<char>(ch));
        }
        return narrow;
    }

    // The WinUI generic.xaml of the package version the app builds against (packages.config).
    std::string ReadWinUiGenericXaml()
    {
        auto const config = ReadFile(NativeDir() / "FileScout.Native" / "packages.config");
        std::smatch match;
        std::regex const versionPattern{ R"re(id="Microsoft\.WindowsAppSDK\.WinUI" version="([^"]+)")re" };
        if (!std::regex_search(config, match, versionPattern))
        {
            ADD_FAILURE() << "Microsoft.WindowsAppSDK.WinUI not found in packages.config";
            return {};
        }
        return ReadFile(NativeDir() / "packages" / ("Microsoft.WindowsAppSDK.WinUI." + match[1].str()) /
            "lib" / "native" / "Microsoft.UI" / "Themes" / "generic.xaml");
    }

    // Text of the top-level theme dictionary `name` (Default = dark, Light, HighContrast).
    std::string ThemeDictionary(std::string const& xaml, std::string const& name)
    {
        auto const begin = xaml.find("<ResourceDictionary x:Key=\"" + name + "\">");
        if (begin == std::string::npos)
        {
            return {};
        }
        auto const end = xaml.find("</ResourceDictionary>", begin);
        return xaml.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    }
}

// UA-07: a key missing from the theme dictionaries silently became light grey (wrong in dark theme).
TEST(ThemeBrushKeys, EveryBrushKeyInMainWindowIsAKnownThemeResource)
{
    auto const source = ReadFile(NativeDir() / "FileScout.Native" / "MainWindow.xaml.cpp");
    std::regex const keyPattern{ R"re(L"([A-Za-z]+Brush)")re" };
    std::set<std::string> keys;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), keyPattern); it != std::sregex_iterator(); ++it)
    {
        keys.insert((*it)[1].str());
    }

    EXPECT_FALSE(keys.empty());
    for (auto const& key : keys)
    {
        std::wstring const wide(key.begin(), key.end());
        EXPECT_TRUE(IsKnownThemeBrushKey(wide)) << key << " is passed to LookupBrush but is not in kThemeBrushKeys";
    }
}

TEST(ThemeBrushKeys, KnownKeysExistInDarkLightAndHighContrast)
{
    auto const xaml = ReadWinUiGenericXaml();
    ASSERT_FALSE(xaml.empty());
    for (auto const* theme : { "Default", "Light", "HighContrast" })
    {
        auto const dictionary = ThemeDictionary(xaml, theme);
        ASSERT_FALSE(dictionary.empty()) << theme;
        for (auto const key : kThemeBrushKeys)
        {
            EXPECT_NE(dictionary.find("x:Key=\"" + Narrow(key) + "\""), std::string::npos)
                << Narrow(key) << " missing from the " << theme << " theme dictionary";
        }
    }
}
