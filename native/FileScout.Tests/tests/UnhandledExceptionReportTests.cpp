#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/UnhandledExceptionReport.h"

using namespace FileScoutNative::Domain;

TEST(UnhandledExceptionReport, MessageNamesTheErrorAndSaysTheAppCloses)
{
    auto const report = BuildUnhandledExceptionReport(L"Catastrophic failure", static_cast<int32_t>(0x8000FFFF));
    EXPECT_EQ(report.title, L"FileScout has to close");
    EXPECT_EQ(report.message,
        L"Catastrophic failure\n"
        L"HRESULT: 0x8000FFFF\n\n"
        L"FileScout will close now so it does not keep working on your files in an unknown state.");
    EXPECT_EQ(report.log_line, L"Unhandled exception, terminating: Catastrophic failure (HRESULT 0x8000FFFF)");
}

TEST(UnhandledExceptionReport, EmptyMessage_GetsAFallbackText)
{
    auto const report = BuildUnhandledExceptionReport(L"", static_cast<int32_t>(0x80004005));
    EXPECT_EQ(report.message.rfind(L"XAML raised an unhandled exception without a message.\nHRESULT: 0x80004005", 0), 0u);
}

TEST(UnhandledExceptionReport, TrailingLineBreaksOfTheMessageAreTrimmed)
{
    auto const report = BuildUnhandledExceptionReport(L"Access is denied.\r\n", static_cast<int32_t>(0x80070005));
    EXPECT_EQ(report.log_line, L"Unhandled exception, terminating: Access is denied. (HRESULT 0x80070005)");
}

TEST(UnhandledExceptionReport, TheAppIsNeverLeftRunning)
{
    EXPECT_FALSE(ShouldMarkUnhandledExceptionHandled());
}
