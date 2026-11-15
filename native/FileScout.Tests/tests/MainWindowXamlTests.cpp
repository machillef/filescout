#include "pch.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <regex>
#include <cctype>
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
namespace
{
    struct ColourLiteral { std::string element; std::string name; std::string attribute; std::string value; };

    // Quote-aware scan of XAML start tags: comments are skipped, and a '>' inside a quoted attribute
    // value does not end the tag. Returns every colour attribute whose value is a literal (not a
    // {markup extension} and not Transparent).
    std::vector<ColourLiteral> FindColourLiterals(std::string const& xaml)
    {
        static std::regex const attribute{ R"re(([\w:.]+)\s*=\s*(["'])(.*?)\2)re" };
        static std::regex const colourName{ R"re(^(Foreground|Background|BorderBrush|Fill|Stroke|Color)$)re" };
        std::vector<ColourLiteral> literals;
        std::size_t i = 0;
        while ((i = xaml.find('<', i)) != std::string::npos)
        {
            if (xaml.compare(i, 4, "<!--") == 0)
            {
                auto const end = xaml.find("-->", i + 4);
                if (end == std::string::npos) break;
                i = end + 3;
                continue;
            }
            if (i + 1 >= xaml.size() || !std::isalpha(static_cast<unsigned char>(xaml[i + 1])))
            {
                ++i;
                continue;
            }
            std::size_t j = i + 1;
            char quote = 0;
            for (; j < xaml.size(); ++j)
            {
                char const c = xaml[j];
                if (quote) { if (c == quote) quote = 0; }
                else if (c == '"' || c == '\'') quote = c;
                else if (c == '>') break;
            }
            std::string const tag = xaml.substr(i + 1, j - i - 1);
            auto const nameEnd = tag.find_first_of(" \t\r\n/");
            std::string const element = tag.substr(0, nameEnd);
            std::string const body = nameEnd == std::string::npos ? std::string{} : tag.substr(nameEnd);
            std::string elementName;
            std::vector<std::pair<std::string, std::string>> colours;
            for (auto attr = std::sregex_iterator(body.begin(), body.end(), attribute); attr != std::sregex_iterator{}; ++attr)
            {
                auto const key = (*attr)[1].str();
                auto const value = (*attr)[3].str();
                if (key == "x:Name") elementName = value;
                else if (std::regex_match(key, colourName) && !value.empty() && value.front() != '{' && value != "Transparent")
                    colours.emplace_back(key, value);
            }
            for (auto const& [key, value] : colours) literals.push_back({ element, elementName, key, value });
            i = j;
        }
        return literals;
    }
}

TEST(MainWindowXaml, ColourScannerHandlesQuotesSpacesAngleBracketsAndComments)
{
    auto const found = FindColourLiterals(
        "<!-- <Border Background=\"Red\" /> -->"
        "<TextBlock Text=\"a > b\" Foreground = 'Red' />"
        "<Grid x:Name=\"G\" Background=\"{ThemeResource X}\" BorderBrush=\"Transparent\" />");

    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found.front().element, "TextBlock");
    EXPECT_EQ(found.front().attribute, "Foreground");
    EXPECT_EQ(found.front().value, "Red");
}

TEST(MainWindowXaml, UsesThemeBrushesInsteadOfHardCodedColours)
{
    auto const literals = FindColourLiterals(ReadMainWindowXaml());

    ASSERT_EQ(literals.size(), 1u) << "only the palette scrim may use a literal colour";
    EXPECT_EQ(literals.front().element, "Grid");
    EXPECT_EQ(literals.front().name, "CommandPaletteOverlay");
    EXPECT_EQ(literals.front().attribute, "Background");
    EXPECT_EQ(literals.front().value, "#80000000");
}
