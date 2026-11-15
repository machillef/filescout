#include "pch.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

// New look: chrome brushes come from theme resources; no hard-coded colours in the window XAML
// (spec-ui UA-08 / UB-13 "no colour islands"). The command-palette scrim is the one exception.
TEST(MainWindowXaml, UsesThemeBrushesInsteadOfHardCodedColours)
{
    auto const xaml = ReadMainWindowXaml();

    std::size_t position = 0;
    std::size_t hardCoded = 0;
    while ((position = xaml.find("=\"#", position)) != std::string::npos)
    {
        ++hardCoded;
        ++position;
    }
    EXPECT_LE(hardCoded, 1u) << "only the palette scrim may use a literal colour";
}
