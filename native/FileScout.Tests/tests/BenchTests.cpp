#include "pch.h"
#include <gtest/gtest.h>
#include "Bench.h"

using FileScoutNative::Bench::Percentile;
using FileScoutNative::Bench::Report;

TEST(Bench, PercentileUsesNearestRank)
{
    std::vector<double> const values{ 5, 1, 4, 2, 3, 10, 6, 7, 9, 8 };
    EXPECT_DOUBLE_EQ(*Percentile(values, 50), 5.0);
    EXPECT_DOUBLE_EQ(*Percentile(values, 95), 10.0);
    EXPECT_DOUBLE_EQ(*Percentile(values, 0), 1.0);
    EXPECT_DOUBLE_EQ(*Percentile(values, 100), 10.0);
}

TEST(Bench, PercentileOfEmptySampleIsEmpty)
{
    EXPECT_FALSE(Percentile({}, 50).has_value());
}

TEST(Bench, ReportJsonHasMarksFramesAndSlowFrameCount)
{
    Report report;
    report.marks = { { L"first_rows", 812.5 }, { L"full_list", 2400.25 } };
    report.frameIntervalsMs = { 16.0, 17.0, 40.0 };
    report.itemCount = 100000;

    auto const json = report.ToJson();

    EXPECT_NE(json.find("\"first_rows\": 812.500"), std::string::npos) << json;
    EXPECT_NE(json.find("\"full_list\": 2400.250"), std::string::npos) << json;
    EXPECT_NE(json.find("\"items\": 100000"), std::string::npos) << json;
    EXPECT_NE(json.find("\"frame_p50_ms\": 17.000"), std::string::npos) << json;
    EXPECT_NE(json.find("\"frames_over_33ms\": 1"), std::string::npos) << json;
}
