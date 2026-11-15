#include "pch.h"
#include "CommandRegistry.h"
#include "Search\FuzzyMatcher.h"
#include <algorithm>

namespace FileScoutNative::Shell
{
    CommandRegistry::CommandRegistry()
    {
        // Closed beta command table: keep gated-off features out of the registry so
        // the searchable command surface only advertises live functionality.
        // shortcut_text is display-only — matches accelerators in SetupKeyboardAccelerators.
        m_commands =
        {
            // Navigation
            { L"newTab",     L"New Tab",           L"Ctrl+T",      L"Navigation" },
            { L"closeTab",   L"Close Tab",         L"Ctrl+W",      L"Navigation" },
            { L"switchPane", L"Switch Pane",       L"Ctrl+Tab",    L"Navigation" },
            { L"refresh",    L"Refresh",           L"F5",          L"Navigation" },
            { L"back",       L"Go Back",           L"Alt+Left",    L"Navigation" },
            { L"forward",    L"Go Forward",        L"Alt+Right",   L"Navigation" },
            { L"goToPath",   L"Go to Path",        L"F4",          L"Navigation" },
            // File operations
            { L"open",       L"Open",              L"Enter",        L"File"       },
            { L"openWith",   L"Open With",         L"",             L"File"       },
            { L"copy",       L"Copy",              L"Ctrl+C",       L"File"       },
            { L"cut",        L"Cut",               L"Ctrl+X",       L"File"       },
            { L"paste",      L"Paste",             L"Ctrl+V",       L"File"       },
            { L"selectAll",  L"Select All",        L"Ctrl+A",       L"File"       },
            { L"delete",     L"Delete",            L"Del",          L"File"       },
            { L"rename",     L"Rename",            L"F2",           L"File"       },
            { L"newFolder",  L"New Folder",        L"Ctrl+Shift+N", L"File"       },
            { L"properties", L"Properties",        L"Alt+Enter",    L"File"       },
            // View / Sort
            { L"toggleView",         L"Toggle View Mode",         L"",  L"View" },
            { L"toggleGroup",        L"Toggle Grouping",          L"",  L"View" },
            { L"togglePreview",      L"Toggle Preview Panel",     L"",  L"View" },
            { L"toggleDualPane",     L"Toggle Dual Pane",         L"",  L"View" },
            { L"sortByName",         L"Sort by Name",             L"",  L"View" },
            { L"sortBySize",         L"Sort by Size",             L"",  L"View" },
            { L"sortByDateModified", L"Sort by Date Modified",    L"",  L"View" },
            // View toggles
            { L"toggleHiddenFiles",  L"Toggle Hidden Files",      L"",  L"View" },
            { L"toggleExtensions",   L"Toggle File Extensions",   L"",  L"View" },
            // System integration
            { L"setDefaultFileManager", L"Make FileScout the Default File Manager", L"", L"System" },
            { L"restoreExplorer",       L"Restore Explorer as Default File Manager", L"", L"System" },
        };
    }

    std::vector<::FileScoutNative::Domain::CommandEntry> RankCommandsByScore(
        std::vector<std::pair<int, ::FileScoutNative::Domain::CommandEntry>> scored)
    {
        std::stable_sort(scored.begin(), scored.end(),
            [](auto const& a, auto const& b) { return a.first > b.first; });

        std::vector<::FileScoutNative::Domain::CommandEntry> result;
        result.reserve(scored.size());
        for (auto& [score, cmd] : scored)
            result.push_back(std::move(cmd));

        return result;
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

        return RankCommandsByScore(std::move(scored));
    }

    std::vector<::FileScoutNative::Domain::CommandEntry> const& CommandRegistry::All() const
    {
        return m_commands;
    }
}
