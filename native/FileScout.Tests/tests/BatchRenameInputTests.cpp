#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/BatchRenameInput.h"
#include "Domain/BatchRenamePlan.h"

using namespace FileScoutNative::Domain;

// D-20: the start-number and padding boxes accept digits only (surrounding blanks trimmed).

TEST(BatchRenameInput, ParsePositiveInteger_AcceptsPlainDigits)
{
    EXPECT_EQ(std::optional<uint32_t>{ 5u }, ParseBatchRenamePositiveInteger(L"5"));
    EXPECT_EQ(std::optional<uint32_t>{ 4294967295u }, ParseBatchRenamePositiveInteger(L"4294967295"));
}

TEST(BatchRenameInput, ParsePositiveInteger_TrimsSurroundingBlanks)
{
    EXPECT_EQ(std::optional<uint32_t>{ 12u }, ParseBatchRenamePositiveInteger(L" 12 "));
}

TEST(BatchRenameInput, ParsePositiveInteger_RejectsSigns)
{
    EXPECT_FALSE(ParseBatchRenamePositiveInteger(L"+5").has_value());
    EXPECT_FALSE(ParseBatchRenamePositiveInteger(L"-1").has_value());
}

TEST(BatchRenameInput, ParsePositiveInteger_RejectsOverflow)
{
    EXPECT_FALSE(ParseBatchRenamePositiveInteger(L"4294967296").has_value());
    EXPECT_FALSE(ParseBatchRenamePositiveInteger(L"99999999999999999999").has_value());
}

TEST(BatchRenameInput, ParsePositiveInteger_RejectsZeroEmptyAndJunk)
{
    EXPECT_FALSE(ParseBatchRenamePositiveInteger(L"0").has_value());
    EXPECT_FALSE(ParseBatchRenamePositiveInteger(L"").has_value());
    EXPECT_FALSE(ParseBatchRenamePositiveInteger(L"   ").has_value());
    EXPECT_FALSE(ParseBatchRenamePositiveInteger(L"1 2").has_value());
    EXPECT_FALSE(ParseBatchRenamePositiveInteger(L"0x10").has_value());
    EXPECT_FALSE(ParseBatchRenamePositiveInteger(L"\x0661").has_value()); // Arabic-Indic digit one
}

// D-21: one mapping from the dialog's case combo index to the transform. The dialog opens on
// Title Case (index 2), and an unknown index (-1 = nothing selected, or out of range) falls back
// to the same Title Case the domain applies for an unknown transform.

TEST(BatchRenameInput, CaseTransformFromIndex_MapsKnownIndices)
{
    EXPECT_EQ(BatchRenameCaseTransform::Lowercase, BatchRenameCaseTransformFromIndex(0));
    EXPECT_EQ(BatchRenameCaseTransform::Uppercase, BatchRenameCaseTransformFromIndex(1));
    EXPECT_EQ(BatchRenameCaseTransform::TitleCase, BatchRenameCaseTransformFromIndex(2));
}

TEST(BatchRenameInput, CaseTransformFromIndex_UnknownIndexMatchesDomainDefault)
{
    EXPECT_EQ(BatchRenameCaseTransform::TitleCase, BatchRenameCaseTransformFromIndex(-1));
    EXPECT_EQ(BatchRenameCaseTransform::TitleCase, BatchRenameCaseTransformFromIndex(3));

    // The plan builder treats an out-of-range transform the same way.
    EXPECT_EQ(
        detail::ApplyCaseTransform(L"my report", BatchRenameCaseTransformFromIndex(-1)),
        detail::ApplyCaseTransform(L"my report", static_cast<BatchRenameCaseTransform>(99)));
    EXPECT_EQ(L"My Report", detail::ApplyCaseTransform(L"my report", BatchRenameCaseTransformFromIndex(-1)));
}
