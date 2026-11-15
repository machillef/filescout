#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/RenamePolicy.h"

using namespace FileScoutNative::Domain;

TEST(RenamePolicy, IdenticalName_IsSkipped)
{
    EXPECT_TRUE(IsRenameNoOp(L"foo.txt", L"foo.txt"));
}

TEST(RenamePolicy, CaseOnlyChange_IsNotSkipped)
{
    // UB-17: Explorer allows foo.txt -> Foo.txt; a case-insensitive compare skipped it.
    EXPECT_FALSE(IsRenameNoOp(L"Foo.txt", L"foo.txt"));
    EXPECT_FALSE(IsRenameNoOp(L"foo.TXT", L"foo.txt"));
}

TEST(RenamePolicy, CaseOnlyChange_NonLatin_IsNotSkipped)
{
    EXPECT_FALSE(IsRenameNoOp(L"Αρχείο.txt", L"αρχείο.txt"));
}

TEST(RenamePolicy, DifferentName_IsNotSkipped)
{
    EXPECT_FALSE(IsRenameNoOp(L"bar.txt", L"foo.txt"));
}
