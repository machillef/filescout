#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/BatchRenameInput.h"
#include "Domain/BatchRenamePlan.h"

using namespace FileScoutNative::Domain;

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
    EXPECT_FALSE(ParseBatchRenamePositiveInteger(L"\x0661").has_value());
}

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

    EXPECT_EQ(
        detail::ApplyCaseTransform(L"my report", BatchRenameCaseTransformFromIndex(-1)),
        detail::ApplyCaseTransform(L"my report", static_cast<BatchRenameCaseTransform>(99)));
    EXPECT_EQ(L"My Report", detail::ApplyCaseTransform(L"my report", BatchRenameCaseTransformFromIndex(-1)));
}
