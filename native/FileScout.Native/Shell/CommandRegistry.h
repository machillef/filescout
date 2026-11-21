#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "Domain\CommandEntry.h"

namespace FileScoutNative::Shell
{
    std::vector<::FileScoutNative::Domain::CommandEntry> RankCommandsByScore(
        std::vector<std::pair<int, ::FileScoutNative::Domain::CommandEntry>> scored);

    class CommandRegistry
    {
    public:
        CommandRegistry();

        std::vector<::FileScoutNative::Domain::CommandEntry> Filter(std::wstring_view query) const;

        std::vector<::FileScoutNative::Domain::CommandEntry> const& All() const;

    private:
        std::vector<::FileScoutNative::Domain::CommandEntry> m_commands;
    };
}
