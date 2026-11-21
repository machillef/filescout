#include "pch.h"
#include <gtest/gtest.h>
#include "NavDiag.h"

using namespace FileScoutNative::NavDiag;

namespace
{
    Trace StagedSidebarTrace(Tick const base)
    {
        auto at = [base](int const ms) { return base + std::chrono::milliseconds(ms); };
        Trace t;
        t.active = true;
        t.nav_id = 7;
        t.source = Source::Sidebar;
        t.staged = true;
        t.staged_trigger = StagedTrigger::Rendering2;
        t.items = 100200;
        t.first_batch_rows = 150;
        t.recents_update_us = 12;
        t.sidebar_timing = { 18000, 200, 1500, 6000, 7500, 2800 };
        t.click = at(0);
        t.load_start = at(2);
        t.pre_reset_begin = at(3);
        t.pre_reset_end = at(4);
        t.ui_pre_done = at(5);
        t.enumerate_begin = at(6);
        t.enumerate_end = at(406);
        t.sort_begin = at(406);
        t.sort_end = at(463);
        t.rows_begin = at(463);
        t.rows_end = at(563);
        t.enqueue = at(563);
        t.callback = at(570);
        t.reset_begin = at(571);
        t.reset_end = at(572);
        t.first_append_end = at(580);
        t.first_publish = at(590);
        t.final_callback = at(640);
        t.replace_end = at(700);
        t.final_publish = at(720);
        t.frame_after_final = at(760);
        t.frame_observed = true;
        return t;
    }

    long long SpanOf(Trace const& t, std::string const& name)
    {
        for (auto const& [n, us] : ModelSpans(t))
        {
            if (name == n) { return us; }
        }
        ADD_FAILURE() << "no span " << name;
        return -2;
    }
}

TEST(NavDiag, SpansSeparateSortRowsDispatchResetAndPublication)
{
    auto const t = StagedSidebarTrace(Now());
    EXPECT_EQ(SpanOf(t, "click_to_load_start"), 2000);
    EXPECT_EQ(SpanOf(t, "sort"), 57000);
    EXPECT_EQ(SpanOf(t, "row_build"), 100000);
    EXPECT_EQ(SpanOf(t, "dispatch_wait"), 7000);
    EXPECT_EQ(SpanOf(t, "list_reset"), 1000);
    EXPECT_EQ(SpanOf(t, "first_append"), 8000);
    EXPECT_EQ(SpanOf(t, "staged_frame_wait"), 50000);
    EXPECT_EQ(SpanOf(t, "final_replace_all"), 60000);
    EXPECT_EQ(SpanOf(t, "origin_to_final_publish"), 720000);
}

TEST(NavDiag, UnreachedStagesAreMinusOne)
{
    Trace t;
    t.active = true;
    t.load_start = Now();
    EXPECT_EQ(SpanOf(t, "click_to_load_start"), -1);
    EXPECT_EQ(SpanOf(t, "dispatch_wait"), -1);
    EXPECT_EQ(SpanUs(t.load_start, t.load_start - std::chrono::milliseconds(1)), -1);
}

TEST(NavDiag, JsonLineIsAnonymousAndKeepsModelAndFrameApart)
{
    auto const json = ToJsonLine(StagedSidebarTrace(Now()), 3);
    EXPECT_NE(json.find("\"source\":\"sidebar\""), std::string::npos);
    EXPECT_NE(json.find("\"staged_trigger\":\"rendering_2\""), std::string::npos);
    EXPECT_NE(json.find("\"items\":100200"), std::string::npos);
    EXPECT_NE(json.find("\"model_us\":{\"click_to_load_start\":2000"), std::string::npos);
    EXPECT_NE(json.find("\"sidebar_rebuild\":18000"), std::string::npos);
    EXPECT_NE(json.find("\"frame_us\":{\"final_publish_to_frame\":40000"), std::string::npos);
    EXPECT_NE(json.find("\"onscreen_verified\":false}"), std::string::npos);
    EXPECT_EQ(json.find('\\'), std::string::npos);
    EXPECT_EQ(json.find('/'), std::string::npos);
    EXPECT_EQ(json.find('\n'), std::string::npos);
}

TEST(NavDiag, RecorderIsOffWithoutTheBuildFlagOrTheEnvironmentVariable)
{
    auto& recorder = Recorder::Instance();
    if (!kCompiledIn)
    {
        EXPECT_FALSE(recorder.Enabled());
    }
    if (!recorder.Enabled())
    {
        recorder.BeginSidebarClick();
        auto const t = recorder.StartLoad(0, Kind::Folder);
        EXPECT_FALSE(t.active);
        EXPECT_EQ(t.source, Source::Other);
    }
}
