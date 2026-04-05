#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/ShellClipboardService.h"

using namespace FileScoutNative::Domain;
using namespace FileScoutNative::Shell;

namespace
{
    ShellItemRef MakeShellItemRef(std::filesystem::path const& path, bool const isFolder = false)
    {
        ShellItemRef item;
        item.path = path;
        item.display_name = path.filename().wstring();
        item.parsing_name = item.path.wstring();
        item.is_folder = isFolder;
        return item;
    }

    std::filesystem::path WriteFile(
        std::filesystem::path const& path,
        std::string_view const content)
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        return path;
    }
}

class ShellClipboardServiceFixture : public ::testing::Test
{
protected:
    std::filesystem::path m_root;
    ShellClipboardService m_service;

    void SetUp() override
    {
        ASSERT_EQ(OleInitialize(nullptr), S_OK);

        wchar_t tempPath[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tempPath);
        m_root = std::filesystem::path{ tempPath } / L"FileScoutTests_ShellClipboard";

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root);
        ClearClipboard();
    }

    void TearDown() override
    {
        ClearClipboard();

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        OleUninitialize();
    }

    static void ClearClipboard()
    {
        (void)OleSetClipboard(nullptr);
    }
};

TEST_F(ShellClipboardServiceFixture, PasteIntoEmptyClipboardReturnsFailureResult)
{
    auto const destination = m_root / L"Dest";
    std::filesystem::create_directories(destination);

    auto const result = m_service.PasteInto(nullptr, destination);

    EXPECT_FALSE(result.succeeded);
    EXPECT_FALSE(result.was_move);
    EXPECT_EQ(result.item_count, 0u);
    EXPECT_EQ(result.destination, destination);
    EXPECT_EQ(result.attempted_action, L"Paste copy");
    EXPECT_EQ(result.failure_detail, L"Clipboard does not contain shell items.");
}

TEST_F(ShellClipboardServiceFixture, PasteIntoCopyReturnsStructuredSuccessOutcome)
{
    auto const sourceDirectory = m_root / L"Source";
    auto const destination = m_root / L"Dest";
    std::filesystem::create_directories(sourceDirectory);
    std::filesystem::create_directories(destination);

    auto const sourcePath = WriteFile(sourceDirectory / L"alpha.txt", "payload");
    auto const item = MakeShellItemRef(sourcePath);

    m_service.CopyShellItems(std::span<ShellItemRef const>{ &item, 1 });

    auto const result = m_service.PasteInto(nullptr, destination);

    EXPECT_TRUE(result.succeeded);
    EXPECT_FALSE(result.was_move);
    EXPECT_EQ(result.item_count, 1u);
    EXPECT_EQ(result.destination, destination);
    EXPECT_EQ(result.attempted_action, L"Paste copy");
    EXPECT_TRUE(result.failure_detail.empty());
    EXPECT_TRUE(std::filesystem::exists(sourcePath));
    EXPECT_TRUE(std::filesystem::exists(destination / sourcePath.filename()));
}

TEST_F(ShellClipboardServiceFixture, PasteIntoCutReturnsMoveOutcomeAndClearsClipboard)
{
    auto const sourceDirectory = m_root / L"Source";
    auto const destination = m_root / L"Dest";
    std::filesystem::create_directories(sourceDirectory);
    std::filesystem::create_directories(destination);

    auto const sourcePath = WriteFile(sourceDirectory / L"alpha.txt", "payload");
    auto const item = MakeShellItemRef(sourcePath);

    m_service.CutShellItems(std::span<ShellItemRef const>{ &item, 1 });

    auto const result = m_service.PasteInto(nullptr, destination);

    EXPECT_TRUE(result.succeeded);
    EXPECT_TRUE(result.was_move);
    EXPECT_EQ(result.item_count, 1u);
    EXPECT_EQ(result.destination, destination);
    EXPECT_EQ(result.attempted_action, L"Paste move");
    EXPECT_TRUE(result.failure_detail.empty());
    EXPECT_FALSE(std::filesystem::exists(sourcePath));
    EXPECT_TRUE(std::filesystem::exists(destination / sourcePath.filename()));
    EXPECT_FALSE(m_service.CanPasteInto(destination));
}

TEST_F(ShellClipboardServiceFixture, PasteIntoMissingDestinationReturnsStructuredFailure)
{
    auto const sourceDirectory = m_root / L"Source";
    auto const destination = m_root / L"MissingDest";
    std::filesystem::create_directories(sourceDirectory);

    auto const sourcePath = WriteFile(sourceDirectory / L"alpha.txt", "payload");
    auto const item = MakeShellItemRef(sourcePath);

    m_service.CopyShellItems(std::span<ShellItemRef const>{ &item, 1 });

    auto const result = m_service.PasteInto(nullptr, destination);

    EXPECT_FALSE(result.succeeded);
    EXPECT_FALSE(result.was_move);
    EXPECT_EQ(result.item_count, 1u);
    EXPECT_EQ(result.destination, destination);
    EXPECT_EQ(result.attempted_action, L"Paste copy");
    EXPECT_FALSE(result.failure_detail.empty());
    EXPECT_TRUE(std::filesystem::exists(sourcePath));
}
