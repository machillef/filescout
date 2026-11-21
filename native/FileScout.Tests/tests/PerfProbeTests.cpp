#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/ShellItemSort.h"
#include "Shell/LocaleDateTimeFormatter.h"
#include "Shell/ShellInteropService.h"

#include <chrono>
#include <cstdio>

TEST(PerfProbe, DISABLED_LoadStages100k)
{
    using clock = std::chrono::steady_clock;
    auto ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
    FileScoutNative::Shell::ShellInteropService shell;
    for (int round = 0; round < 3; ++round)
    {
        auto t0 = clock::now();
        auto items = shell.EnumerateDirectory(L"C:\\fsperf\\100k", false);
        auto t1 = clock::now();
        auto copy = items;
        auto t2 = clock::now();
        FileScoutNative::Domain::SortShellItems(copy, FileScoutNative::Domain::ShellItemSortColumn::Name,
            FileScoutNative::Domain::ShellItemSortDirection::Ascending);
        auto t3 = clock::now();

        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileExW(L"C:\\fsperf\\100k\\*", FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        ASSERT_NE(h, INVALID_HANDLE_VALUE) << "C:\\fsperf\\100k is not readable";
        std::vector<FILETIME> times;
        do { times.push_back(fd.ftLastWriteTime); times.push_back(fd.ftCreationTime); } while (FindNextFileW(h, &fd));
        auto const error = GetLastError();
        FindClose(h);
        ASSERT_EQ(error, static_cast<DWORD>(ERROR_NO_MORE_FILES)) << "enumeration stopped early";
        auto t4 = clock::now();
        FileScoutNative::Shell::LocaleDateTimeFormatter formatter;
        std::size_t total = 0;
        for (auto const& t : times) total += formatter.FormatFileTime(t).size();
        auto t5 = clock::now();
        std::printf("round %d: enumerate %.0f ms (%zu items), copy %.0f, sort %.0f, dates(%zu, 1 thread) %.0f ms [%zu]\n",
            round, ms(t1 - t0), items.size(), ms(t2 - t1), ms(t3 - t2), times.size(), ms(t5 - t4), total);
    }
}
