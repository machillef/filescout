#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace FileScoutNative::Domain
{
    // UA-05: a filter typed while the folder is still loading is kept, shown as waiting, and applied
    // when the load publishes its final rows, instead of being dropped.
    class PendingFilter
    {
    public:
        // Returns true when the filter can run now; during a load the query is kept for the final
        // publish (an empty query clears it).
        bool OnQueryChanged(std::wstring query, bool const loadInProgress)
        {
            if (!loadInProgress)
            {
                m_query.reset();
                return true;
            }

            if (query.empty())
            {
                m_query.reset();
            }
            else
            {
                m_query = std::move(query);
            }
            return false;
        }

        // The query to apply once the final rows are published, if one is waiting.
        [[nodiscard]] std::optional<std::wstring> TakeOnFinalPublish()
        {
            auto query = std::move(m_query);
            m_query.reset();
            return query;
        }

        void Clear() noexcept { m_query.reset(); }
        [[nodiscard]] bool HasPending() const noexcept { return m_query.has_value(); }

    private:
        std::optional<std::wstring> m_query;
    };

    [[nodiscard]] inline std::wstring BuildPendingFilterStatus(std::wstring_view const query)
    {
        return L"Loading\u2026 the filter \u201C" + std::wstring{ query } + L"\u201D applies when the folder has loaded.";
    }
}
