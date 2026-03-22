#include "pch.h"
#include "CommandRegistry.h"
#include "Search\FuzzyMatcher.h"
#include <algorithm>

namespace FileScoutNative::Shell
{
    CommandRegistry::CommandRegistry()
    {
        // R13a: scaffold entries covering navigation and common file operations.
        // Categories and shortcut_text match the accelerators wired in SetupKeyboardAccelerators.
        // R13b–R13d will wire these ids to real ExecuteContextCommand dispatch.
        m_commands =
        {
            // Navigation
            { L"newTab",     L"New Tab",          L"Ctrl+T",   L"Navigation" },
            { L"closeTab",   L"Close Tab",         L"Ctrl+W",   L"Navigation" },
            { L"switchPane", L"Switch Pane",       L"Ctrl+Tab", L"Navigation" },
            { L"refresh",    L"Refresh",           L"F5",       L"Navigation" },
            { L"back",       L"Go Back",           L"",         L"Navigation" },
            { L"forward",    L"Go Forward",        L"",         L"Navigation" },
            // File operations
            { L"copy",       L"Copy",              L"Ctrl+C",   L"File"       },
            { L"cut",        L"Cut",               L"Ctrl+X",   L"File"       },
            { L"paste",      L"Paste",             L"Ctrl+V",   L"File"       },
            { L"delete",     L"Delete",            L"Del",      L"File"       },
            { L"rename",     L"Rename",            L"F2",       L"File"       },
            { L"newFolder",  L"New Folder",        L"",         L"File"       },
            // View
            { L"toggleView", L"Toggle View Mode",  L"",         L"View"       },
            { L"toggleGroup",L"Toggle Grouping",   L"",         L"View"       },
        };
    }

    std::vector<::FileScoutNative::Domain::CommandEntry>
    CommandRegistry::Filter(std::wstring_view query) const
    {
        if (query.empty())
            return m_commands;

        using namespace FileScoutNative::Search;

        std::vector<std::pair<int, ::FileScoutNative::Domain::CommandEntry>> scored;
        scored.reserve(m_commands.size());

        for (auto const& cmd : m_commands)
        {
            int score = FuzzyScore(query, cmd.display_name);
            if (score >= 0)
                scored.emplace_back(score, cmd);
        }

        std::sort(scored.begin(), scored.end(),
            [](auto const& a, auto const& b) { return a.first > b.first; });

        std::vector<::FileScoutNative::Domain::CommandEntry> result;
        result.reserve(scored.size());
        for (auto& [score, cmd] : scored)
            result.push_back(std::move(cmd));

        return result;
    }

    std::vector<::FileScoutNative::Domain::CommandEntry> const& CommandRegistry::All() const
    {
        return m_commands;
    }
}
