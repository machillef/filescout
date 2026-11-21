#include "pch.h"
#include <gtest/gtest.h>

#include "Domain/NavigationHistory.h"
#include "Domain/RenamePolicy.h"
#include "Domain/SelectionSummary.h"

using namespace FileScoutNative::Domain;

namespace
{
    ShellItemRef MakeSizedFile(std::wstring path, std::uintmax_t sizeBytes, bool sizeAvailable = true)
    {
        ShellItemRef item;
        item.path = std::filesystem::path{ path };
        item.display_name = item.path.filename().wstring();
        item.is_folder = false;
        item.size_bytes = sizeBytes;
        item.size_available = sizeAvailable;
        return item;
    }

    ShellItemRef MakeFolderItem(std::wstring path)
    {
        ShellItemRef item;
        item.path = std::filesystem::path{ path };
        item.display_name = item.path.filename().wstring();
        item.is_folder = true;
        return item;
    }
}

TEST(SelectionSummaryTests, SummarizesFilesFoldersAndKnownBytes)
{
    std::vector<ShellItemRef> items{
        MakeSizedFile(LR"(C:\Data\a.bin)", 1024),
        MakeSizedFile(LR"(C:\Data\b.bin)", 2048),
        MakeSizedFile(LR"(C:\Data\unknown.bin)", 9999, /*sizeAvailable=*/false),
        MakeFolderItem(LR"(C:\Data\Sub)"),
    };

    auto const summary = SummarizeSelection(items);
    EXPECT_EQ(summary.total_count, 4u);
    EXPECT_EQ(summary.file_count, 3u);
    EXPECT_EQ(summary.folder_count, 1u);
    EXPECT_EQ(summary.known_size_bytes, 3072u);
    EXPECT_EQ(summary.files_with_size, 2u);

    auto const meta = FormatSelectionMetaText(summary);
    EXPECT_NE(meta.find(L"3 files"), std::wstring::npos);
    EXPECT_NE(meta.find(L"1 folder"), std::wstring::npos);
    EXPECT_NE(meta.find(L"3.0 KB"), std::wstring::npos);

    auto const status = FormatSelectionStatusText(summary);
    EXPECT_NE(status.find(L"4 items selected"), std::wstring::npos);
    EXPECT_NE(status.find(L"3.0 KB"), std::wstring::npos);
}

TEST(SelectionSummaryTests, FoldersOnlyOmitsSize)
{
    std::vector<ShellItemRef> items{
        MakeFolderItem(LR"(C:\Data\One)"),
        MakeFolderItem(LR"(C:\Data\Two)"),
    };

    auto const summary = SummarizeSelection(items);
    EXPECT_EQ(FormatSelectionMetaText(summary), L"2 folders");
    EXPECT_EQ(FormatSelectionStatusText(summary), L"2 items selected");
}

TEST(SelectionSummaryTests, FormatCopyPathsJoinsMultipleAndFallsBackToDirectory)
{
    std::vector<ShellItemRef> empty;
    EXPECT_EQ(FormatCopyPathsPayload(empty, LR"(C:\Users\Test)"), LR"(C:\Users\Test)");

    std::vector<ShellItemRef> items{
        MakeSizedFile(LR"(C:\Data\a.txt)", 10),
        MakeFolderItem(LR"(C:\Data\Docs)"),
    };
    EXPECT_EQ(
        FormatCopyPathsPayload(items, LR"(C:\Data)"),
        L"C:\\Data\\a.txt\r\nC:\\Data\\Docs");
}

TEST(RenamePolicyTests, ComputeRenameSelectionLengthSelectsStemForFilesOnly)
{
    EXPECT_EQ(ComputeRenameSelectionLength(L"photo.png", false), 5);
    EXPECT_EQ(ComputeRenameSelectionLength(L"archive.tar.gz", false), 11);
    EXPECT_EQ(ComputeRenameSelectionLength(L"README", false), 6);
    EXPECT_EQ(ComputeRenameSelectionLength(L".gitignore", false), 10);
    EXPECT_EQ(ComputeRenameSelectionLength(L"v1.2.3", true), 6);
    EXPECT_EQ(ComputeRenameSelectionLength(L"", false), 0);
}

TEST(NavigationHistoryTests, ResolveParentNavigationTargetHandlesSubfolderRootRecycleAndDrives)
{
    auto const sub = ResolveParentNavigationTarget(LR"(C:\Users\Docs\Reports)", false, false);
    EXPECT_EQ(sub.kind, ParentNavigationKind::Folder);
    EXPECT_EQ(sub.path, std::filesystem::path{ LR"(C:\Users\Docs)" });

    auto const root = ResolveParentNavigationTarget(LR"(C:\)", false, false);
    EXPECT_EQ(root.kind, ParentNavigationKind::DrivesView);

    auto const recycle = ResolveParentNavigationTarget(std::filesystem::path{ kRecycleBinLocation }, false, true);
    EXPECT_EQ(recycle.kind, ParentNavigationKind::DrivesView);

    auto const drives = ResolveParentNavigationTarget({}, true, false);
    EXPECT_EQ(drives.kind, ParentNavigationKind::None);
}

TEST(NavigationHistoryTests, DrivesViewSentinelPreservesBackForwardHistoryAndSkipsRecents)
{
    std::vector<std::filesystem::path> back;
    std::vector<std::filesystem::path> forward;
    std::filesystem::path current = LR"(C:\Data)";

    // Up from C:\Data -> C:\ -> This PC
    BeginNavigation(back, forward, current, LR"(C:\)", /*isHistoryStep=*/false);
    RecordNavigation(back, forward, current, std::filesystem::path{ kDrivesViewLocation });
    ASSERT_EQ(back.size(), 2u);
    EXPECT_EQ(back[0], std::filesystem::path{ LR"(C:\Data)" });
    EXPECT_EQ(back[1], std::filesystem::path{ LR"(C:\)" });

    EXPECT_TRUE(IsDrivesViewLocation(std::filesystem::path{ kDrivesViewLocation }));
    EXPECT_FALSE(IsDrivesViewLocation(std::filesystem::path{ LR"(C:\)" }));
    EXPECT_FALSE(FolderToRecordOnLeave(std::filesystem::path{ kDrivesViewLocation }, LR"(C:\)").has_value());
}
