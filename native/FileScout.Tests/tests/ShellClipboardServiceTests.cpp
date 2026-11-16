#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/ShellClipboardService.h"
#include "Shell/ShellStaWorker.h"

#include <future>

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
        if (OpenClipboard(nullptr))
        {
            EmptyClipboard();
            CloseClipboard();
        }
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

// S-03: a paste runs later on the shell worker, behind earlier operations. It must act on the
// clipboard as it was when the user pressed Paste, not on what the user copied in the meantime.
TEST_F(ShellClipboardServiceFixture, CapturedPasteUsesTheClipboardAtCaptureTime)
{
    auto const sourceDirectory = m_root / L"Source";
    auto const destination = m_root / L"Dest";
    std::filesystem::create_directories(sourceDirectory);
    std::filesystem::create_directories(destination);
    auto const alpha = MakeShellItemRef(WriteFile(sourceDirectory / L"alpha.txt", "a"));
    auto const beta = MakeShellItemRef(WriteFile(sourceDirectory / L"beta.txt", "b"));

    m_service.CopyShellItems(std::span<ShellItemRef const>{ &alpha, 1 });
    auto const snapshot = m_service.CaptureClipboard();
    m_service.CopyShellItems(std::span<ShellItemRef const>{ &beta, 1 });

    auto const result = m_service.PasteCaptured(nullptr, destination, snapshot);

    EXPECT_TRUE(result.succeeded);
    EXPECT_EQ(result.item_count, 1u);
    EXPECT_TRUE(std::filesystem::exists(destination / L"alpha.txt"));
    EXPECT_FALSE(std::filesystem::exists(destination / L"beta.txt"));
}

// A cut paste clears the clipboard afterwards, but only the cut it pasted: a newer copy stays.
TEST_F(ShellClipboardServiceFixture, CapturedCutPasteKeepsANewerClipboard)
{
    auto const sourceDirectory = m_root / L"Source";
    auto const destination = m_root / L"Dest";
    std::filesystem::create_directories(sourceDirectory);
    std::filesystem::create_directories(destination);
    auto const alphaPath = WriteFile(sourceDirectory / L"alpha.txt", "a");
    auto const alpha = MakeShellItemRef(alphaPath);
    auto const beta = MakeShellItemRef(WriteFile(sourceDirectory / L"beta.txt", "b"));

    m_service.CutShellItems(std::span<ShellItemRef const>{ &alpha, 1 });
    auto const snapshot = m_service.CaptureClipboard();
    m_service.CopyShellItems(std::span<ShellItemRef const>{ &beta, 1 });

    auto const result = m_service.PasteCaptured(nullptr, destination, snapshot);

    EXPECT_TRUE(result.succeeded);
    EXPECT_TRUE(result.was_move);
    EXPECT_FALSE(std::filesystem::exists(alphaPath));
    EXPECT_TRUE(std::filesystem::exists(destination / L"alpha.txt"));
    EXPECT_TRUE(m_service.CanPasteInto(destination));
}

// The snapshot is taken on the UI thread and pasted on the shell worker's STA.
TEST_F(ShellClipboardServiceFixture, CapturedPasteRunsOnTheShellWorker)
{
    auto const sourceDirectory = m_root / L"Source";
    auto const destination = m_root / L"Dest";
    std::filesystem::create_directories(sourceDirectory);
    std::filesystem::create_directories(destination);
    auto const alpha = MakeShellItemRef(WriteFile(sourceDirectory / L"alpha.txt", "a"));

    m_service.CopyShellItems(std::span<ShellItemRef const>{ &alpha, 1 });
    auto const snapshot = m_service.CaptureClipboard();

    std::promise<ClipboardPasteResult> done;
    {
        ShellStaWorker worker;
        worker.Post([&] { done.set_value(m_service.PasteCaptured(nullptr, destination, snapshot)); });
    }
    auto const result = done.get_future().get();

    EXPECT_TRUE(result.succeeded);
    EXPECT_TRUE(std::filesystem::exists(destination / L"alpha.txt"));
}
