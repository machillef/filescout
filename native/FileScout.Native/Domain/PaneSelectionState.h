#pragma once

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
            m_focus.reset();
            m_anchor.reset();
        }

        void ReplaceWith(std::wstring id)
        {
            m_selected.clear();
            m_selected.insert(id);
            Focus(*m_selected.begin());
        }

        [[nodiscard]] bool IsSelected(std::wstring const& id) const
        {
            return m_selected.find(id) != m_selected.end();
        }

        [[nodiscard]] std::optional<std::wstring> const& Focused() const
        {
            return m_focus;
        }

    private:
        std::unordered_set<std::wstring> m_selected;
        std::optional<std::wstring> m_focus;
        std::optional<std::wstring> m_anchor;
    };
}
