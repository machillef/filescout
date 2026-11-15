#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/BatchRenameInput.h"

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
