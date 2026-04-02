#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/FileNameValidation.h"
#include "Domain/BatchRenamePlan.h"
#include "Shell/PropertiesLaunchSpec.h"
#include "Shell/ShellFileOperationService.h"

using namespace FileScoutNative::Domain;
using namespace FileScoutNative::Shell;

// ─── Fixture ─────────────────────────────────────────────────────────────────

class ShellFileOperationFixture : public ::testing::Test
{
protected:
    std::filesystem::path m_root;
    ShellFileOperationService m_svc;

    void SetUp() override
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        wchar_t tempPath[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tempPath);
        m_root = std::filesystem::path{ tempPath } / L"FileScoutTests_SFO";

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        CoUninitialize();
    }

    // Write a small file at path and return its path.
    static std::filesystem::path WriteFile(
        std::filesystem::path const& path,
        std::string_view content)
    {
        std::ofstream f{ path, std::ios::binary };
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        return path;
    }
};

namespace
{
    ShellItemRef MakeBatchRenameItem(
        std::filesystem::path const& path,
        bool const isFolder = false)
    {
        ShellItemRef item;
        item.path = path;
        item.display_name = path.filename().wstring();
        item.parsing_name = item.path.wstring();
        item.is_folder = isFolder;
        return item;
    }

    std::string CaptureRuntimeError(std::function<void()> const& action)
    {
        try
        {
            action();
        }
        catch (std::runtime_error const& error)
        {
            return error.what();
        }

        ADD_FAILURE() << "Expected std::runtime_error";
        return {};
    }

    void ExpectContains(std::string const& text, std::string_view const needle)
    {
        EXPECT_NE(text.find(needle), std::string::npos)
            << "Expected '" << text << "' to contain '" << needle << "'";
    }
}

TEST(FileNameValidationTests, RejectsTrailingSpacesPeriodsAndDotSegments)
{
    EXPECT_EQ(
        ValidateWindowsFileName(L"report "),
        std::optional<std::wstring>{ L"File name cannot end with a space or period." });
    EXPECT_EQ(
        ValidateWindowsFileName(L"report."),
        std::optional<std::wstring>{ L"File name cannot end with a space or period." });
    EXPECT_EQ(
        ValidateWindowsFileName(L"."),
        std::optional<std::wstring>{ L"File name cannot be '.' or '..'." });
    EXPECT_EQ(
        ValidateWindowsFileName(L".."),
        std::optional<std::wstring>{ L"File name cannot be '.' or '..'." });
}

TEST(FileNameValidationTests, RejectsReservedDeviceNamesEvenWithExtensions)
{
    EXPECT_EQ(
        ValidateWindowsFileName(L"con.txt"),
        std::optional<std::wstring>{ L"That name is reserved on Windows." });
    EXPECT_EQ(
        ValidateWindowsFileName(L"Lpt1.log"),
        std::optional<std::wstring>{ L"That name is reserved on Windows." });
}

// ─── BuildSequentialBatchRenamePlan ──────────────────────────────────────────

TEST(BatchRenamePlanTests, BuildSequentialPlanPreservesExtensionsAndFolderNames)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\alpha.txt)"),
        MakeBatchRenameItem(LR"(C:\Temp\photos)", true),
        MakeBatchRenameItem(LR"(C:\Temp\gamma.png)"),
    };

    auto const plan = BuildSequentialBatchRenamePlan(selection, selection, L"Trip");

    ASSERT_FALSE(plan.error.has_value());
    ASSERT_EQ(plan.entries.size(), 3u);
    EXPECT_EQ(plan.entries[0].target_name, L"Trip 01.txt");
    EXPECT_EQ(plan.entries[1].target_name, L"Trip 02");
    EXPECT_EQ(plan.entries[2].target_name, L"Trip 03.png");
}

TEST(BatchRenamePlanTests, BuildSequentialPlanRejectsSiblingConflicts)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\alpha.txt)"),
        MakeBatchRenameItem(LR"(C:\Temp\beta.txt)"),
    };
    auto sibling = MakeBatchRenameItem(LR"(C:\Temp\Series 01.txt)");
    std::vector<ShellItemRef> siblings = selection;
    siblings.push_back(sibling);

    auto const plan = BuildSequentialBatchRenamePlan(selection, siblings, L"Series");

    ASSERT_TRUE(plan.error.has_value());
    EXPECT_EQ(*plan.error, L"Batch rename would overwrite an existing sibling item.");
    EXPECT_TRUE(plan.entries.empty());
}

TEST(BatchRenamePlanTests, BuildSequentialPlanSupportsCustomStartNumberAndPadding)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\alpha.txt)"),
        MakeBatchRenameItem(LR"(C:\Temp\beta.txt)"),
    };

    auto const plan = BuildSequentialBatchRenamePlan(
        selection,
        selection,
        BatchRenameOptions{
            .base_name = L"Series",
            .start_number = 7,
            .padding_width = 4,
        });

    ASSERT_FALSE(plan.error.has_value());
    ASSERT_EQ(plan.entries.size(), 2u);
    EXPECT_EQ(plan.entries[0].target_name, L"Series 0007.txt");
    EXPECT_EQ(plan.entries[1].target_name, L"Series 0008.txt");
}

TEST(BatchRenamePlanTests, BuildSequentialPlanRejectsZeroStartNumber)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\alpha.txt)"),
    };

    auto const plan = BuildSequentialBatchRenamePlan(
        selection,
        selection,
        BatchRenameOptions{
            .base_name = L"Series",
            .start_number = 0,
        });

    ASSERT_TRUE(plan.error.has_value());
    EXPECT_EQ(*plan.error, L"Batch rename start number must be at least 1.");
}

TEST(BatchRenamePlanTests, BuildPrefixPlanPreservesExtensionsAndFolderNames)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\alpha.txt)"),
        MakeBatchRenameItem(LR"(C:\Temp\photos)", true),
        MakeBatchRenameItem(LR"(C:\Temp\gamma.tar.gz)"),
    };

    auto const plan = BuildPrefixBatchRenamePlan(selection, selection, L"Project - ");

    ASSERT_FALSE(plan.error.has_value());
    ASSERT_EQ(plan.entries.size(), 3u);
    EXPECT_EQ(plan.entries[0].target_name, L"Project - alpha.txt");
    EXPECT_EQ(plan.entries[1].target_name, L"Project - photos");
    EXPECT_EQ(plan.entries[2].target_name, L"Project - gamma.tar.gz");
}

TEST(BatchRenamePlanTests, BuildSuffixPlanAddsSuffixBeforeExtensionAndRejectsConflicts)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\alpha.txt)"),
        MakeBatchRenameItem(LR"(C:\Temp\photos)", true),
    };

    auto const previewPlan = BuildSuffixBatchRenamePlan(selection, selection, L" (edited)");

    ASSERT_FALSE(previewPlan.error.has_value());
    ASSERT_EQ(previewPlan.entries.size(), 2u);
    EXPECT_EQ(previewPlan.entries[0].target_name, L"alpha (edited).txt");
    EXPECT_EQ(previewPlan.entries[1].target_name, L"photos (edited)");

    std::vector<ShellItemRef> siblings = selection;
    siblings.push_back(MakeBatchRenameItem(LR"(C:\Temp\alpha (edited).txt)"));

    auto const conflictPlan = BuildSuffixBatchRenamePlan(selection, siblings, L" (edited)");

    ASSERT_TRUE(conflictPlan.error.has_value());
    EXPECT_EQ(*conflictPlan.error, L"Batch rename would overwrite an existing sibling item.");
    EXPECT_TRUE(conflictPlan.entries.empty());
}

TEST(BatchRenamePlanTests, TryAppendPlanEntryRejectsNamesEndingWithPeriod)
{
    BatchRenamePlan plan;
    std::unordered_set<std::wstring> generatedNames;
    std::unordered_set<std::wstring> existingNames;
    auto const item = MakeBatchRenameItem(LR"(C:\Temp\photos)", true);

    EXPECT_FALSE(FileScoutNative::Domain::detail::TryAppendPlanEntry(
        plan,
        item,
        L"photos.",
        generatedNames,
        existingNames));
    ASSERT_TRUE(plan.error.has_value());
    EXPECT_EQ(*plan.error, L"File name cannot end with a space or period.");
    EXPECT_TRUE(plan.entries.empty());
}

TEST(BatchRenamePlanTests, BuildFindReplacePlanPreservesExtensionsAndRenamesFolders)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\draft-report.txt)"),
        MakeBatchRenameItem(LR"(C:\Temp\draft-notes)", true),
        MakeBatchRenameItem(LR"(C:\Temp\draft-draft.tar.gz)"),
    };

    auto const plan = BuildFindReplaceBatchRenamePlan(selection, selection, L"draft", L"final");

    ASSERT_FALSE(plan.error.has_value());
    ASSERT_EQ(plan.entries.size(), 3u);
    EXPECT_EQ(plan.entries[0].target_name, L"final-report.txt");
    EXPECT_EQ(plan.entries[1].target_name, L"final-notes");
    EXPECT_EQ(plan.entries[2].target_name, L"final-final.tar.gz");
}

TEST(BatchRenamePlanTests, BuildFindReplacePlanRejectsNoOpSelections)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\alpha.txt)"),
        MakeBatchRenameItem(LR"(C:\Temp\photos)", true),
    };

    auto const plan = BuildFindReplaceBatchRenamePlan(selection, selection, L"draft", L"final");

    ASSERT_TRUE(plan.error.has_value());
    EXPECT_EQ(*plan.error, L"Find and replace did not change any selected item names.");
    EXPECT_TRUE(plan.entries.empty());
}

TEST(BatchRenamePlanTests, BuildFindReplacePlanRejectsDuplicatesAgainstUnchangedSelections)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\alpha.txt)"),
        MakeBatchRenameItem(LR"(C:\Temp\beta.txt)"),
    };

    auto const plan = BuildFindReplaceBatchRenamePlan(selection, selection, L"alpha", L"beta");

    ASSERT_TRUE(plan.error.has_value());
    EXPECT_EQ(*plan.error, L"Batch rename generated duplicate names.");
    EXPECT_TRUE(plan.entries.empty());
}

TEST(BatchRenamePlanTests, BuildCaseTransformPlanPreservesExtensionsAndRenamesFolders)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\alpha beta.txt)"),
        MakeBatchRenameItem(LR"(C:\Temp\mixed-case folder)", true),
        MakeBatchRenameItem(LR"(C:\Temp\report.final.tar.gz)"),
    };

    auto const plan = BuildCaseTransformBatchRenamePlan(
        selection,
        selection,
        BatchRenameCaseTransform::Uppercase);

    ASSERT_FALSE(plan.error.has_value());
    ASSERT_EQ(plan.entries.size(), 3u);
    EXPECT_EQ(plan.entries[0].target_name, L"ALPHA BETA.txt");
    EXPECT_EQ(plan.entries[1].target_name, L"MIXED-CASE FOLDER");
    EXPECT_EQ(plan.entries[2].target_name, L"REPORT.FINAL.TAR.gz");
}

TEST(BatchRenamePlanTests, BuildCaseTransformPlanSupportsTitleCase)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\my_report draft.txt)"),
        MakeBatchRenameItem(LR"(C:\Temp\q1-summary (final))", true),
    };

    auto const plan = BuildCaseTransformBatchRenamePlan(
        selection,
        selection,
        BatchRenameCaseTransform::TitleCase);

    ASSERT_FALSE(plan.error.has_value());
    ASSERT_EQ(plan.entries.size(), 2u);
    EXPECT_EQ(plan.entries[0].target_name, L"My_Report Draft.txt");
    EXPECT_EQ(plan.entries[1].target_name, L"Q1-Summary (Final)");
}

TEST(BatchRenamePlanTests, BuildCaseTransformPlanRejectsNoOpSelections)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\ALPHA.txt)"),
        MakeBatchRenameItem(LR"(C:\Temp\READY)", true),
    };

    auto const plan = BuildCaseTransformBatchRenamePlan(
        selection,
        selection,
        BatchRenameCaseTransform::Uppercase);

    ASSERT_TRUE(plan.error.has_value());
    EXPECT_EQ(*plan.error, L"Case transform did not change any selected item names.");
    EXPECT_TRUE(plan.entries.empty());
}

TEST(BatchRenamePlanTests, BuildCaseTransformPlanRejectsDuplicateNamesAgainstUnchangedSelections)
{
    std::vector<ShellItemRef> selection{
        MakeBatchRenameItem(LR"(C:\Temp\Alpha.txt)"),
        MakeBatchRenameItem(LR"(C:\Temp\ALPHA.txt)"),
    };

    auto const plan = BuildCaseTransformBatchRenamePlan(
        selection,
        selection,
        BatchRenameCaseTransform::Uppercase);

    ASSERT_TRUE(plan.error.has_value());
    EXPECT_EQ(*plan.error, L"Batch rename generated duplicate names.");
    EXPECT_TRUE(plan.entries.empty());
}

TEST(ShellFileOperationDetail, BuildPropertiesLaunchSpec_UsesShopFilePathAndNoPage)
{
    auto const spec = FileScoutNative::Shell::detail::BuildPropertiesLaunchSpec(
        LR"(C:\Temp\sample.txt)");

    EXPECT_EQ(spec.object_type, static_cast<DWORD>(SHOP_FILEPATH));
    EXPECT_EQ(spec.object_name, LR"(C:\Temp\sample.txt)");
    EXPECT_TRUE(spec.property_page.empty());
}

// ─── CreateFolder ────────────────────────────────────────────────────────────

TEST_F(ShellFileOperationFixture, CreateFolderCreatesDirectory)
{
    auto result = m_svc.CreateFolder(nullptr, m_root, L"NewDir");
    EXPECT_TRUE(std::filesystem::is_directory(result));
}

TEST_F(ShellFileOperationFixture, CreateFolderReturnsCorrectPath)
{
    auto result = m_svc.CreateFolder(nullptr, m_root, L"SomeFolder");
    EXPECT_EQ(result, m_root / L"SomeFolder");
}

// ─── RenameItem ──────────────────────────────────────────────────────────────

TEST_F(ShellFileOperationFixture, RenameItemRenamesFile)
{
    auto src = WriteFile(m_root / L"old.txt", "hello");
    auto newPath = m_svc.RenameItem(nullptr, src, L"new.txt");
    EXPECT_FALSE(std::filesystem::exists(src));
    EXPECT_TRUE(std::filesystem::exists(newPath));
}

TEST_F(ShellFileOperationFixture, RenameItemReturnsNewPath)
{
    auto src = WriteFile(m_root / L"alpha.txt", "data");
    auto result = m_svc.RenameItem(nullptr, src, L"beta.txt");
    EXPECT_EQ(result, m_root / L"beta.txt");
}

TEST_F(ShellFileOperationFixture, RenameItemsRenamesMultipleFiles)
{
    auto const first = WriteFile(m_root / L"alpha.txt", "data");
    auto const second = WriteFile(m_root / L"beta.txt", "more");

    std::vector<RenameRequest> requests{
        { first, L"Series 01.txt" },
        { second, L"Series 02.txt" },
    };

    auto const results = m_svc.RenameItems(nullptr, requests);

    ASSERT_EQ(results.size(), 2u);
    EXPECT_FALSE(std::filesystem::exists(first));
    EXPECT_FALSE(std::filesystem::exists(second));
    EXPECT_TRUE(std::filesystem::exists(results[0]));
    EXPECT_TRUE(std::filesystem::exists(results[1]));
}

// ─── CopyItemsFromPaths ───────────────────────────────────────────────────────

TEST_F(ShellFileOperationFixture, CopyItemsFromPathsEmptySpanIsNoOp)
{
    std::vector<std::filesystem::path> empty;
    // Should return without error when no source paths are provided.
    EXPECT_NO_THROW(
        m_svc.CopyItemsFromPaths(
            nullptr,
            std::span<std::filesystem::path const>{ empty },
            m_root,
            false));
}

// ─── CopyItemFromPath ────────────────────────────────────────────────────────

TEST_F(ShellFileOperationFixture, CopyItemFromPathCopiesFile)
{
    auto src = WriteFile(m_root / L"source.txt", "payload");
    auto destDir = m_root / L"Dest";
    std::filesystem::create_directories(destDir);

    m_svc.CopyItemFromPath(nullptr, src, destDir, L"copy.txt");

    EXPECT_TRUE(std::filesystem::exists(src));              // original still present
    EXPECT_TRUE(std::filesystem::exists(destDir / L"copy.txt")); // copy created
}

TEST_F(ShellFileOperationFixture, CopyItemsFromPathsIntoSameFolderCreatesDuplicateInsteadOfFailing)
{
    auto const src = WriteFile(m_root / L"source.txt", "payload");

    EXPECT_NO_THROW(m_svc.CopyItemsFromPaths(
        nullptr,
        std::span<std::filesystem::path const>{ &src, 1 },
        m_root,
        false));

    std::vector<std::filesystem::path> payloadCopies;
    for (auto const& entry : std::filesystem::directory_iterator(m_root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        std::ifstream stream(entry.path(), std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        if (content == "payload")
        {
            payloadCopies.push_back(entry.path());
        }
    }

    EXPECT_EQ(payloadCopies.size(), 2u);
}

// ─── DeleteItem ───────────────────────────────────────────────────────────────

TEST_F(ShellFileOperationFixture, DeleteItemRemovesFileFromOriginalPath)
{
    auto target = WriteFile(m_root / L"to_delete.txt", "ephemeral");
    ASSERT_TRUE(std::filesystem::exists(target));

    m_svc.DeleteItem(nullptr, target);

    // File moved to Recycle Bin — should no longer be at original path.
    EXPECT_FALSE(std::filesystem::exists(target));
}

TEST_F(ShellFileOperationFixture, DeleteItemsRemovesFilesFromOriginalPaths)
{
    auto const first = WriteFile(m_root / L"first.txt", "one");
    auto const second = WriteFile(m_root / L"second.txt", "two");
    std::vector<std::filesystem::path> paths{ first, second };

    m_svc.DeleteItems(nullptr, paths);

    EXPECT_FALSE(std::filesystem::exists(first));
    EXPECT_FALSE(std::filesystem::exists(second));
}

TEST_F(ShellFileOperationFixture, ShowPropertiesThrowsForMissingPath)
{
    auto const missing = m_root / L"missing.txt";
    EXPECT_THROW(m_svc.ShowProperties(nullptr, missing), std::runtime_error);
}

TEST_F(ShellFileOperationFixture, ShowPropertiesFailureIncludesActionAndPath)
{
    auto const missing = m_root / L"missing.txt";

    auto const message = CaptureRuntimeError([&]
    {
        m_svc.ShowProperties(nullptr, missing);
    });

    ExpectContains(message, "Show properties");
    ExpectContains(message, winrt::to_string(winrt::hstring{ missing.wstring() }));
}

TEST_F(ShellFileOperationFixture, RenameFailureIncludesActionAndSourcePath)
{
    auto const original = m_root / L"alpha.txt";

    auto const message = CaptureRuntimeError([&]
    {
        [[maybe_unused]] auto const renamedPath = m_svc.RenameItem(nullptr, original, L"beta.txt");
    });

    ExpectContains(message, "Rename");
    ExpectContains(message, winrt::to_string(winrt::hstring{ original.wstring() }));
}
