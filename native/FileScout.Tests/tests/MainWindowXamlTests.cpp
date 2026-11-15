#include "pch.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <regex>
#include <vector>

namespace
{
    std::string ReadMainWindowXaml()
    {
        auto path = std::filesystem::path{ __FILE__ };
        path = path.parent_path().parent_path() / ".." / "FileScout.Native" / "MainWindow.xaml";

        std::ifstream stream{ path, std::ios::binary };
        EXPECT_TRUE(stream.is_open()) << "Failed to open " << path.string();

        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }
}

TEST(MainWindowXaml, PreservesListAndGroupRowTemplates)
{
    auto const xaml = ReadMainWindowXaml();

    EXPECT_NE(xaml.find("<DataTemplate x:Key=\"ListRowTemplate\""), std::string::npos);
    EXPECT_NE(xaml.find("<DataTemplate x:Key=\"GroupRowTemplate\""), std::string::npos);
    EXPECT_NE(xaml.find("Text=\"{x:Bind TypeLabel}\""), std::string::npos);
    EXPECT_NE(xaml.find("Text=\"{x:Bind SizeText}\""), std::string::npos);
    EXPECT_NE(xaml.find("Text=\"GROUP\""), std::string::npos);
}

// spec-ui UA-01: nothing ever wrote the "LOCATION" placeholder, so it showed as dead text in the chrome.
TEST(MainWindowXaml, HasNoDeadLocationPlaceholder)
{
    auto const xaml = ReadMainWindowXaml();

    EXPECT_EQ(xaml.find("Text=\"LOCATION\""), std::string::npos);
    EXPECT_EQ(xaml.find("ActiveLocationTextBlock"), std::string::npos);
}

// spec-ui UA-02: StatusIconTextBlock never received a glyph and only reserved empty space.
TEST(MainWindowXaml, HasNoEmptyStatusIconSlot)
{
    auto const xaml = ReadMainWindowXaml();

    EXPECT_EQ(xaml.find("StatusIconTextBlock"), std::string::npos);
}

// New look: chrome brushes come from theme resources; no literal colours (named or #hex) in the window
// XAML (spec-ui UA-08 / UB-13 "no colour islands"). The command-palette scrim is the one exception.
TEST(MainWindowXaml, UsesThemeBrushesInsteadOfHardCodedColours)
{
    auto const xaml = ReadMainWindowXaml();
    // Every start tag, then every attribute in it (either quote style, optional spaces around '=').
    std::regex const startTag{ R"re(<([A-Za-z][\w:.]*)\b([^>]*)>)re" };
    std::regex const attribute{ R"re(([\w:.]+)\s*=\s*(["'])(.*?)\2)re" };
    std::regex const colourName{ R"re(^(Foreground|Background|BorderBrush|Fill|Stroke|Color)$)re" };

    struct Literal { std::string element; std::string name; std::string attribute; std::string value; };
    std::vector<Literal> literals;
    for (auto tag = std::sregex_iterator(xaml.begin(), xaml.end(), startTag); tag != std::sregex_iterator{}; ++tag)
    {
        auto const body = (*tag)[2].str();
        std::string elementName;
        std::vector<std::pair<std::string, std::string>> colours;
        for (auto attr = std::sregex_iterator(body.begin(), body.end(), attribute); attr != std::sregex_iterator{}; ++attr)
        {
            auto const key = (*attr)[1].str();
            auto const value = (*attr)[3].str();
            if (key == "x:Name")
            {
                elementName = value;
            }
            else if (std::regex_match(key, colourName) && !value.empty() && value.front() != '{' && value != "Transparent")
            {
                colours.emplace_back(key, value);
            }
        }
        for (auto const& [key, value] : colours)
        {
            literals.push_back({ (*tag)[1].str(), elementName, key, value });
        }
    }

    ASSERT_EQ(literals.size(), 1u) << "only the palette scrim may use a literal colour";
    EXPECT_EQ(literals.front().element, "Grid");
    EXPECT_EQ(literals.front().name, "CommandPaletteOverlay");
    EXPECT_EQ(literals.front().attribute, "Background");
    EXPECT_EQ(literals.front().value, "#80000000");
}
