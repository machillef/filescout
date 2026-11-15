#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/PendingFilter.h"

using namespace FileScoutNative::Domain;

// UA-05: typing in the filter while a large folder was still loading was silently ignored.
TEST(PendingFilter, TypingDuringALoad_IsAppliedOnTheFinalPublish)
{
    PendingFilter pending;
    EXPECT_FALSE(pending.OnQueryChanged(L"rep", /*loadInProgress=*/true));
    EXPECT_FALSE(pending.OnQueryChanged(L"report", /*loadInProgress=*/true));
    EXPECT_TRUE(pending.HasPending());
    auto const query = pending.TakeOnFinalPublish();
    ASSERT_TRUE(query.has_value());
    EXPECT_EQ(*query, L"report");
    EXPECT_FALSE(pending.HasPending());
    EXPECT_FALSE(pending.TakeOnFinalPublish().has_value());
}

TEST(PendingFilter, TypingWhenLoaded_AppliesImmediately)
{
    PendingFilter pending;
    EXPECT_TRUE(pending.OnQueryChanged(L"report", /*loadInProgress=*/false));
    EXPECT_FALSE(pending.HasPending());
    EXPECT_FALSE(pending.TakeOnFinalPublish().has_value());
}

TEST(PendingFilter, ClearingTheBoxDuringALoad_DropsThePendingQuery)
{
    PendingFilter pending;
    EXPECT_FALSE(pending.OnQueryChanged(L"rep", /*loadInProgress=*/true));
    pending.Clear();
    EXPECT_FALSE(pending.TakeOnFinalPublish().has_value());
}

TEST(PendingFilter, EmptyQueryDuringALoad_IsNotPending)
{
    PendingFilter pending;
    EXPECT_FALSE(pending.OnQueryChanged(L"", /*loadInProgress=*/true));
    EXPECT_FALSE(pending.HasPending());
}

TEST(PendingFilter, StatusText_TellsTheUserTheFilterIsWaiting)
{
    EXPECT_EQ(BuildPendingFilterStatus(L"report"), L"Loading\u2026 the filter \u201Creport\u201D applies when the folder has loaded.");
}
