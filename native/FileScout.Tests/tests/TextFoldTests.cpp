#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/AddressNavigation.h"
#include "Domain/BatchRenamePlan.h"
#include "Domain/ShellItemSort.h"
#include "Domain/TextFold.h"
#include "Search/FuzzyMatcher.h"
#include "Search/SearchPresentation.h"

using namespace FileScoutNative::Domain;

// D-22: case-insensitive behaviour must not depend on the C locale (the app never calls
// setlocale, so towlower folds ASCII only). The user's file names are Greek.

TEST(TextFold, FoldsGreekAndLatin)
{
    EXPECT_EQ(L"\u03B1\u03C1\u03C7\u03B5\u03AF\u03BF.txt", FoldCase(L"\u0391\u03A1\u03A7\u0395\u038A\u039F.TXT"));
    EXPECT_EQ(L"\u0391\u03A1\u03A7\u0395\u038A\u039F.TXT", UpperCase(L"\u03B1\u03C1\u03C7\u03B5\u03AF\u03BF.txt"));
    EXPECT_EQ(L"\u00E9cole", FoldCase(L"\u00C9COLE"));
    EXPECT_EQ(0, CompareIgnoreCase(L"\u0391\u03C1\u03C7\u03B5\u03AF\u03BF", L"\u03B1\u03A1\u03A7\u0395\u038A\u039F"));
    EXPECT_TRUE(EqualsIgnoreCase(L"\u03A6\u0391\u039A\u0395\u039B\u039F\u03A3", L"\u03C6\u03B1\u03BA\u03B5\u03BB\u03BF\u03C2"));
    EXPECT_LT(CompareIgnoreCase(L"\u03B1\u03B2\u03B3", L"\u03C9\u03BC\u03AD\u03B3\u03B1"), 0);
    // Final sigma: folded for comparisons, kept by the lowercase rename.
    EXPECT_EQ(FoldCase(L"\u03C2"), FoldCase(L"\u03A3"));
    EXPECT_EQ(L"\u03C2", LowerCase(L"\u03C2"));
}

TEST(TextFold, FuzzyQueryMatchesGreekNameInEitherCase)
{
    EXPECT_GE(FileScoutNative::Search::FuzzyScore(L"\u03B1\u03C1\u03C7", L"\u0391\u03C1\u03C7\u03B5\u03AF\u03BF.txt"), 0);
    EXPECT_GE(FileScoutNative::Search::FuzzyScore(L"\u0391\u03A1\u03A7", L"\u03B1\u03C1\u03C7\u03B5\u03AF\u03BF.txt"), 0);
    EXPECT_EQ(
        FileScoutNative::Search::FuzzyScore(L"\u03B1\u03C1\u03C7", L"\u03B1\u03C1\u03C7\u03B5\u03AF\u03BF.txt"),
        FileScoutNative::Search::FuzzyScore(L"\u03B1\u03C1\u03C7", L"\u0391\u03C1\u03C7\u03B5\u03AF\u03BF.txt"))
        << "case must not change the score of a prefix match";
}

TEST(TextFold, HighlightPositionsIgnoreGreekCase)
{
    auto const positions = FileScoutNative::Search::FindSearchHighlightPositions(L"\u03B1\u03C1", L"\u0391\u03C1\u03C7\u03B5\u03AF\u03BF.txt");
    EXPECT_EQ((std::vector<std::size_t>{ 0, 1 }), positions);
}

TEST(TextFold, NameSortKeepsGreekCaseVariantsTogether)
{
    std::vector<ShellItemRef> items;
    for (auto const* name : { L"\u03B1\u03B2\u03B3.txt", L"\u03A9mega.txt", L"\u0391\u0392\u0393.txt", L"abc.txt" })
    {
        ShellItemRef item;
        item.display_name = name;
        item.path = std::filesystem::path{ L"C:\\x" } / name;
        items.push_back(std::move(item));
    }

    SortShellItems(items, ShellItemSortColumn::Name, ShellItemSortDirection::Ascending);

    ASSERT_EQ(items.size(), 4u);
    EXPECT_EQ(items[0].display_name, L"abc.txt");
    EXPECT_EQ(FoldCase(items[1].display_name), L"\u03B1\u03B2\u03B3.txt");
    EXPECT_EQ(FoldCase(items[2].display_name), L"\u03B1\u03B2\u03B3.txt");
    EXPECT_EQ(items[3].display_name, L"\u03A9mega.txt");
}

TEST(TextFold, DriveLetterParsingStaysAscii)
{
    EXPECT_TRUE(IsDriveLetterOnlyAddressNavigationTarget(L"C:"));
    EXPECT_TRUE(IsDriveLetterOnlyAddressNavigationTarget(L"z:"));
    EXPECT_FALSE(IsDriveLetterOnlyAddressNavigationTarget(L"\u0391:")); // Greek capital alpha
    EXPECT_FALSE(IsDriveLetterOnlyAddressNavigationTarget(L"\u00C9:"));
    EXPECT_FALSE(IsDriveLetterOnlyAddressNavigationTarget(L"1:"));
}

TEST(TextFold, BatchRenameCaseTransformsHandleGreek)
{
    EXPECT_EQ(L"\u03B1\u03C1\u03C7\u03B5\u03AF\u03BF", detail::ApplyCaseTransform(L"\u0391\u03A1\u03A7\u0395\u038A\u039F", BatchRenameCaseTransform::Lowercase));
    EXPECT_EQ(L"\u0391\u03A1\u03A7\u0395\u038A\u039F", detail::ApplyCaseTransform(L"\u03B1\u03C1\u03C7\u03B5\u03AF\u03BF", BatchRenameCaseTransform::Uppercase));
    // Greek "archeio keimenou" -> "Archeio Keimenou"
    EXPECT_EQ(
        L"\u0391\u03C1\u03C7\u03B5\u03AF\u03BF \u039A\u03B5\u03B9\u03BC\u03AD\u03BD\u03BF\u03C5",
        detail::ApplyCaseTransform(
            L"\u03B1\u03C1\u03C7\u03B5\u03AF\u03BF \u03BA\u03B5\u03B9\u03BC\u03AD\u03BD\u03BF\u03C5",
            BatchRenameCaseTransform::TitleCase));
}

// Unicode simple case folding entries whose target is not the lowercase mapping.
TEST(TextFold, SymbolVariantsFoldToTheLetterUsersType)
{
    using FileScoutNative::Domain::FoldChar;
    EXPECT_EQ(FoldChar(L'\u03D0'), L'\u03B2');  // beta symbol
    EXPECT_EQ(FoldChar(L'\u03F1'), L'\u03C1');  // rho symbol
    EXPECT_EQ(FoldChar(L'\u00B5'), L'\u03BC');  // micro sign
    EXPECT_EQ(FoldChar(L'\u212A'), L'k');        // Kelvin sign
    EXPECT_EQ(FoldChar(L'\u017F'), L's');        // long s
}

// Group headers use the same folding as the name sort, so Greek case variants share one header.
TEST(TextFold, NameGroupInitialMatchesTheSortFolding)
{
    using FileScoutNative::Domain::NameGroupInitial;
    EXPECT_EQ(NameGroupInitial(L"\u0391a"), L'\u0391');  // Αa
    EXPECT_EQ(NameGroupInitial(L"\u03B1b"), L'\u0391');  // αb
    EXPECT_EQ(NameGroupInitial(L"\u03C2x"), L'\u03A3');  // ςx groups under Σ
    EXPECT_EQ(NameGroupInitial(L"7z"), L'7');
    EXPECT_EQ(NameGroupInitial(L"_tmp"), L'#');
    EXPECT_EQ(NameGroupInitial(L""), L'#');
}
