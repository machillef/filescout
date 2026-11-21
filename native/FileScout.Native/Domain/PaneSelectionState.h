#pragma once

#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace FileScoutNative::Domain
{
    class PaneSelectionState
    {
    public:
        void Focus(std::wstring id)
        {
            m_focus = std::move(id);
            if (!m_anchor.has_value())
            {
                m_anchor = m_focus;
            }
        }

        void Clear()
        {
            m_selected.clear();
            m_order.clear();
            m_focus.reset();
            m_anchor.reset();
        }

        void ReplaceWith(std::wstring id)
        {
            Clear();
            if (id.empty())
            {
                return;
            }

            Add(id);
            Focus(std::move(id));
        }

        void ReplaceWith(
            std::span<std::wstring const> ids,
            std::optional<std::wstring> focus = std::nullopt)
        {
            m_selected.clear();
            m_order.clear();
            for (auto const& id : ids)
            {
                if (!id.empty())
                {
                    Add(id);
                }
            }

            if (m_order.empty())
            {
                m_focus.reset();
                m_anchor.reset();
                return;
            }

            if (focus.has_value() && IsSelected(*focus))
            {
                m_focus = std::move(focus);
            }
            else
            {
                m_focus = m_order.front();
            }

            if (!m_anchor.has_value() || !IsSelected(*m_anchor))
            {
                m_anchor = m_focus;
            }
        }

        [[nodiscard]] bool IsSelected(std::wstring const& id) const
        {
            return m_selected.find(id) != m_selected.end();
        }

        [[nodiscard]] std::vector<std::wstring> SelectedIds() const
        {
            return m_order;
        }

        [[nodiscard]] std::optional<std::wstring> FocusedId() const
        {
            return m_focus;
        }

    private:
        void Add(std::wstring const& id)
        {
            if (m_selected.insert(id).second)
            {
                m_order.push_back(id);
            }
        }

        std::unordered_set<std::wstring> m_selected;
        std::vector<std::wstring> m_order;
        std::optional<std::wstring> m_focus;
        std::optional<std::wstring> m_anchor;
    };
}
