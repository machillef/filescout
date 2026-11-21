#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace FileScoutNative::Domain
{
    class PendingFilter
    {
    public:
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
