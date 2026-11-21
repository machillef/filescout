#include "pch.h"
#include <gtest/gtest.h>

#include "Domain/ArchiveCompression.h"
#include "Domain/UndoStack.h"
#include "Shell/ComApartment.h"
#include "Shell/ShellFileOperationService.h"

#include <cstdio>
#include <fstream>
#include <set>
#include <unordered_set>

using namespace FileScoutNative::Domain;
using namespace FileScoutNative::Shell;

namespace
{
    std::filesystem::path MakeUniqueTempDir(std::wstring_view const suffix)
    {
        auto dir = std::filesystem::temp_directory_path() /
            (L"FileScoutUndoArchiveTests-" +
             std::to_wstring(::GetCurrentProcessId()) + L"-" +
             std::to_wstring(::GetTickCount64()) + L"-" +
             std::wstring{ suffix });
        std::filesystem::create_directories(dir);
        return dir;
    }

    struct ScopedTempDir
    {
        std::filesystem::path path;
        explicit ScopedTempDir(std::wstring_view const suffix)
            : path(MakeUniqueTempDir(suffix))
        {
        }
        ~ScopedTempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };
}

TEST(UndoStackTests, PushPopUndoRedoAndClearRedoOnNewAction)
{
    UndoStack stack(10);
    EXPECT_FALSE(stack.CanUndo());
    EXPECT_FALSE(stack.CanRedo());

    // Empty items entry is ignored.
    stack.Push(UndoEntry{ UndoActionKind::Rename, L"Empty", {} });
    EXPECT_FALSE(stack.CanUndo());

    stack.Push(UndoEntry{ UndoActionKind::Rename, L"Rename 1", { { L"C:\\a.txt", L"C:\\b.txt" } } });
    stack.Push(UndoEntry{ UndoActionKind::CreateFolder, L"Create dir", { { {}, L"C:\\NewDir" } } });
    EXPECT_EQ(stack.UndoCount(), 2u);

    auto first = stack.PopUndo();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->kind, UndoActionKind::CreateFolder);
    stack.PushRedo(std::move(*first));
    EXPECT_EQ(stack.UndoCount(), 1u);
    EXPECT_EQ(stack.RedoCount(), 1u);

    // Redo pops from redo stack and preserves any remaining redo items when pushed back to undo.
    auto redone = stack.PopRedo();
    ASSERT_TRUE(redone.has_value());
    EXPECT_EQ(redone->description, L"Create dir");
    stack.PushUndoPreservingRedo(std::move(*redone));
    EXPECT_EQ(stack.UndoCount(), 2u);
    EXPECT_EQ(stack.RedoCount(), 0u);

    // Pop one to redo, then push a brand-new action -> redo stack clears.
    auto popped = stack.PopUndo();
    ASSERT_TRUE(popped.has_value());
    stack.PushRedo(std::move(*popped));
    ASSERT_TRUE(stack.CanRedo());

    stack.Push(UndoEntry{ UndoActionKind::CreateFile, L"New file", { { {}, L"C:\\note.txt" } } });
    EXPECT_FALSE(stack.CanRedo());
    EXPECT_EQ(stack.UndoCount(), 2u);
}

TEST(UndoStackTests, EnforcesMaxDepthByEvictingOldestEntries)
{
    UndoStack stack(2);
    stack.Push(UndoEntry{ UndoActionKind::Rename, L"1", { { L"C:\\1", L"C:\\1b" } } });
    stack.Push(UndoEntry{ UndoActionKind::Rename, L"2", { { L"C:\\2", L"C:\\2b" } } });
    stack.Push(UndoEntry{ UndoActionKind::Rename, L"3", { { L"C:\\3", L"C:\\3b" } } });

    EXPECT_EQ(stack.UndoCount(), 2u);
    auto top = stack.PopUndo();
    ASSERT_TRUE(top.has_value());
    EXPECT_EQ(top->description, L"3");
    auto next = stack.PopUndo();
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->description, L"2");
    EXPECT_FALSE(stack.CanUndo());
}

TEST(UndoStackTests, MultiOpenConfirmationThreshold)
{
    EXPECT_FALSE(ShouldConfirmMultiOpen(0));
    EXPECT_FALSE(ShouldConfirmMultiOpen(1));
    EXPECT_FALSE(ShouldConfirmMultiOpen(10));
    EXPECT_TRUE(ShouldConfirmMultiOpen(11));
    EXPECT_TRUE(ShouldConfirmMultiOpen(50));
}

TEST(ArchiveCompressionTests, QuotesWindowsCommandLineArgsCorrectly)
{
    EXPECT_EQ(QuoteWindowsCommandArg(L""), L"\"\"");
    EXPECT_EQ(QuoteWindowsCommandArg(L"plain.txt"), L"plain.txt");
    EXPECT_EQ(QuoteWindowsCommandArg(L"my file.txt"), L"\"my file.txt\"");
    EXPECT_EQ(QuoteWindowsCommandArg(L"say \"hi\""), L"\"say \\\"hi\\\"\"");
    EXPECT_EQ(QuoteWindowsCommandArg(L"C:\\My Folder\\"), L"\"C:\\My Folder\\\\\"");
}

TEST(ArchiveCompressionTests, SuggestsCollisionSafeZipNamesForFilesFoldersAndMultiSelect)
{
    std::unordered_set<std::wstring> existing{
        L"C:\\Work\\report.zip",
        L"C:\\Work\\report (2).zip",
    };
    auto existsFn = [&](std::filesystem::path const& p) { return existing.contains(p.wstring()); };

    // Single file uses stem + collision suffix.
    std::vector<std::filesystem::path> singleFile{ L"C:\\Work\\report.pdf" };
    EXPECT_EQ(
        SuggestZipArchivePath(L"C:\\Work", singleFile, existsFn, /*singleItemIsFolder=*/false),
        std::optional<std::filesystem::path>{ std::filesystem::path{ L"C:\\Work\\report (3).zip" } });

    // Single folder containing a dot keeps full folder name instead of stripping ".2".
    std::vector<std::filesystem::path> dottedFolder{ L"C:\\Work\\release.2" };
    EXPECT_EQ(
        SuggestZipArchivePath(L"C:\\Work", dottedFolder, existsFn, /*singleItemIsFolder=*/true),
        std::optional<std::filesystem::path>{ std::filesystem::path{ L"C:\\Work\\release.2.zip" } });

    // Multi-select uses parent folder name; drive root falls back to Archive.zip.
    std::vector<std::filesystem::path> multi{ L"C:\\Work\\a.txt", L"C:\\Work\\b.txt" };
    EXPECT_EQ(
        SuggestZipArchivePath(L"C:\\Work", multi, existsFn, false),
        std::optional<std::filesystem::path>{ std::filesystem::path{ L"C:\\Work\\Work.zip" } });
    EXPECT_EQ(
        SuggestZipArchivePath(L"C:\\", multi, existsFn, false),
        std::optional<std::filesystem::path>{ std::filesystem::path{ L"C:\\Archive.zip" } });

    // Exhausted candidate range returns std::nullopt instead of an occupied path.
    auto alwaysExists = [](std::filesystem::path const&) { return true; };
    EXPECT_FALSE(SuggestZipArchivePath(L"C:\\Work", singleFile, alwaysExists, false).has_value());

    // Common parent directory helper accepts same-folder items and rejects mixed-parent selections.
    std::vector<std::filesystem::path> sameParent{ L"C:\\Work\\Sub\\a.txt", L"c:\\work\\sub\\b.txt" };
    auto const common = TryGetCommonParentDirectory(sameParent);
    ASSERT_TRUE(common.has_value());
    EXPECT_EQ(common->wstring(), L"C:\\Work\\Sub");

    std::vector<std::filesystem::path> mixedParent{ L"C:\\Work\\a.txt", L"C:\\Work\\Sub\\b.txt" };
    EXPECT_FALSE(TryGetCommonParentDirectory(mixedParent).has_value());
}

TEST(ArchiveCompressionTests, BuildsTarCommandWithOptionSeparatorAndQuoting)
{
    auto const cmd = BuildTarZipCommandLine(
        L"C:\\Windows\\System32\\tar.exe",
        L"C:\\My Docs\\Archive.zip",
        L"C:\\My Docs\\.filescout-list.tmp");
    EXPECT_EQ(
        cmd,
        L"C:\\Windows\\System32\\tar.exe -a -c -f \"C:\\My Docs\\Archive.zip\" --null -T \"C:\\My Docs\\.filescout-list.tmp\"");
}

TEST(ArchiveCompressionTests, SelectsNewestRecycleBinMatchPerOriginalPathOnce)
{
    auto const origA = BuildRecycleBinOriginalPath(L"C:\\Docs", L"note.txt");
    EXPECT_EQ(origA, std::filesystem::path{ L"C:\\Docs\\note.txt" });

    std::vector<RecycleBinCandidate> candidates{
        { L"C:\\$Recycle.Bin\\R_OLD.txt", L"C:\\Docs\\note.txt", 100 },
        { L"C:\\$Recycle.Bin\\R_NEW.txt", L"c:\\docs\\NOTE.txt", 500 },
        { L"C:\\$Recycle.Bin\\R_OTHER.txt", L"C:\\Docs\\other.txt", 300 },
    };
    std::vector<std::filesystem::path> targets{
        L"C:\\Docs\\note.txt",
        L"C:\\Docs\\note.txt",
    };

    auto const matched = SelectNewestRecycleBinMatches(candidates, targets);
    ASSERT_EQ(matched.size(), 2u);
    EXPECT_EQ(matched[0], L"C:\\$Recycle.Bin\\R_NEW.txt");
    EXPECT_EQ(matched[1], L"C:\\$Recycle.Bin\\R_OLD.txt");
}

TEST(ShellFileOperationZipTests, CompressesFilesIncludingDashAtAndSpacesAndAvoidsCollision)
{
    ComApartment apartment;
    ScopedTempDir temp{ L"zip" };
    {
        std::ofstream f1(temp.path / L"-leading-dash.txt");
        f1 << "dash content";
        std::ofstream f2(temp.path / L"my file.txt");
        f2 << "spaced content";
        std::ofstream f3(temp.path / L"@notes.txt");
        f3 << "at-prefix content";
        std::ofstream f4(temp.path / L"-Cdir.txt");
        f4 << "dash-C content";
    }

    ShellFileOperationService service;
    std::vector<std::filesystem::path> sources{
        temp.path / L"-leading-dash.txt",
        temp.path / L"my file.txt",
        temp.path / L"@notes.txt",
        temp.path / L"-Cdir.txt",
    };

    auto const zip1 = service.CompressItemsToZip(nullptr, temp.path, sources);
    ASSERT_TRUE(std::filesystem::exists(zip1));

    // The ZIP must hold exactly the four names (no `./` prefix) with their original contents.
    wchar_t systemDir[MAX_PATH]{};
    ASSERT_GT(::GetSystemDirectoryW(systemDir, MAX_PATH), 0u);
    auto const tarExe = std::filesystem::path{ systemDir } / L"tar.exe";
    std::set<std::string> entryNames;
    {
        FILE* const listing = ::_wpopen(
            (QuoteWindowsCommandArg(tarExe.wstring()) + L" -t -f " + QuoteWindowsCommandArg(zip1.wstring())).c_str(), L"rt");
        ASSERT_NE(listing, nullptr);
        char line[512];
        while (std::fgets(line, sizeof(line), listing) != nullptr)
        {
            std::string name{ line };
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
            {
                name.pop_back();
            }
            entryNames.insert(name);
        }
        EXPECT_EQ(::_pclose(listing), 0);
    }
    EXPECT_EQ(entryNames, (std::set<std::string>{ "-leading-dash.txt", "my file.txt", "@notes.txt", "-Cdir.txt" }));

    ScopedTempDir extracted{ L"zip-out" };
    ASSERT_EQ(::_wsystem((QuoteWindowsCommandArg(tarExe.wstring()) + L" -x -f " + QuoteWindowsCommandArg(zip1.wstring()) +
                          L" -C " + QuoteWindowsCommandArg(extracted.path.wstring())).c_str()), 0);
    auto const readAll = [](std::filesystem::path const& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string{ std::istreambuf_iterator<char>{ in }, std::istreambuf_iterator<char>{} };
    };
    for (auto const& source : sources)
    {
        EXPECT_EQ(readAll(extracted.path / source.filename()), readAll(source)) << source.filename().string();
    }

    auto const zip2 = service.CompressItemsToZip(nullptr, temp.path, sources);
    EXPECT_TRUE(std::filesystem::exists(zip2));
    EXPECT_NE(zip1, zip2);
    EXPECT_GT(std::filesystem::file_size(zip2), 0u);
}

TEST(ShellFileOperationIdentityTests, DetectsReplacementAtSamePath)
{
    ScopedTempDir temp{ L"identity" };
    auto const target = temp.path / L"draft.txt";
    {
        std::ofstream out(target);
        out << "original";
    }
    auto const id1 = TryCaptureFileIdentity(target);
    ASSERT_TRUE(id1.has_value());
    EXPECT_EQ(TryCaptureFileIdentity(target), id1);

    // Move original away and create a new file at the same path; identity must differ.
    std::filesystem::rename(target, temp.path / L"moved.txt");
    {
        std::ofstream out(target);
        out << "replacement";
    }
    auto const id2 = TryCaptureFileIdentity(target);
    ASSERT_TRUE(id2.has_value());
    EXPECT_NE(*id1, *id2);
}

TEST(ShellFileOperationTransferTests, CopyAndMoveRecordTransferredPairsForUndo)
{
    ComApartment apartment;
    ScopedTempDir srcDir{ L"transfer-src" };
    ScopedTempDir dstDir{ L"transfer-dst" };
    auto const fileA = srcDir.path / L"alpha.txt";
    {
        std::ofstream out(fileA);
        out << "undo payload";
    }

    ShellFileOperationService service;
    std::vector<std::filesystem::path> sources{ fileA };
    std::vector<TransferredItemPair> copiedPairs;
    service.CopyItemsFromPaths(nullptr, sources, dstDir.path, /*move=*/false, &copiedPairs);
    ASSERT_EQ(copiedPairs.size(), 1u);
    EXPECT_TRUE(std::filesystem::equivalent(copiedPairs[0].source, fileA));
    EXPECT_TRUE(std::filesystem::equivalent(copiedPairs[0].destination, dstDir.path / L"alpha.txt"));
    EXPECT_TRUE(std::filesystem::exists(copiedPairs[0].destination));

    // Move copied item back under a new name via MoveItemFromPath.
    service.MoveItemFromPath(nullptr, copiedPairs[0].destination, srcDir.path, L"alpha_moved.txt");
    EXPECT_FALSE(std::filesystem::exists(copiedPairs[0].destination));
    EXPECT_TRUE(std::filesystem::exists(srcDir.path / L"alpha_moved.txt"));
}
