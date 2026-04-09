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
