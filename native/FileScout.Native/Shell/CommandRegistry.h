#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "Domain\CommandEntry.h"

namespace FileScoutNative::Shell
{
    // Registry of all palette-dispatchable commands.
    // Constructed once at window startup; stateless after construction.
    // Orders scored commands by descending score; equal scores keep their input
    // (registration) order.
    std::vector<::FileScoutNative::Domain::CommandEntry> RankCommandsByScore(
        std::vector<std::pair<int, ::FileScoutNative::Domain::CommandEntry>> scored);

    class CommandRegistry
    {
    public:
        CommandRegistry();

        // Returns a scored+sorted subset of commands whose display_name matches
        // the query as a fuzzy subsequence. Empty query returns all commands.
        std::vector<::FileScoutNative::Domain::CommandEntry> Filter(std::wstring_view query) const;

        // The full unfiltered command table.
        std::vector<::FileScoutNative::Domain::CommandEntry> const& All() const;

    private:
        std::vector<::FileScoutNative::Domain::CommandEntry> m_commands;
    };
}
