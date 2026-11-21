#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace FileScoutNative::Domain
{
    inline constexpr std::size_t kDefaultMaxUndoDepth = 50;
    inline constexpr std::size_t kMultiOpenConfirmationThreshold = 10;

    [[nodiscard]] constexpr bool ShouldConfirmMultiOpen(std::size_t const itemCount) noexcept
    {
        return itemCount > kMultiOpenConfirmationThreshold;
    }

    enum class UndoActionKind
    {
        Rename,
        BatchRename,
        CreateFolder,
        CreateFile,
        Move,
        Copy,
        DeleteToRecycleBin,
    };

    struct UndoPathPair
    {
        std::filesystem::path source;
        std::filesystem::path target;
    };

    struct UndoEntry
    {
        UndoActionKind kind{ UndoActionKind::Rename };
        std::wstring description;
        std::vector<UndoPathPair> items;
    };

    // Bounded LIFO undo/redo history for reversible filesystem mutations.
    class UndoStack
    {
    public:
        explicit UndoStack(std::size_t const maxDepth = kDefaultMaxUndoDepth) noexcept
            : m_maxDepth(maxDepth == 0 ? 1 : maxDepth)
        {
        }

        void Push(UndoEntry entry)
        {
            if (entry.items.empty())
            {
                return;
            }
            m_redo.clear();
            PushUndoInternal(std::move(entry));
        }

        void PushRedo(UndoEntry entry)
        {
            if (entry.items.empty())
            {
                return;
            }
            if (m_redo.size() >= m_maxDepth)
            {
                m_redo.erase(m_redo.begin());
            }
            m_redo.push_back(std::move(entry));
        }

        // Used after a Redo succeeds so the remaining redo stack is preserved.
        void PushUndoPreservingRedo(UndoEntry entry)
        {
            if (entry.items.empty())
            {
                return;
            }
            PushUndoInternal(std::move(entry));
        }

        [[nodiscard]] std::optional<UndoEntry> PopUndo()
        {
            if (m_undo.empty())
            {
                return std::nullopt;
            }
            auto entry = std::move(m_undo.back());
            m_undo.pop_back();
            return entry;
        }

        [[nodiscard]] std::optional<UndoEntry> PopRedo()
        {
            if (m_redo.empty())
            {
                return std::nullopt;
            }
            auto entry = std::move(m_redo.back());
            m_redo.pop_back();
            return entry;
        }

        [[nodiscard]] bool CanUndo() const noexcept { return !m_undo.empty(); }
        [[nodiscard]] bool CanRedo() const noexcept { return !m_redo.empty(); }
        [[nodiscard]] std::size_t UndoCount() const noexcept { return m_undo.size(); }
        [[nodiscard]] std::size_t RedoCount() const noexcept { return m_redo.size(); }

        [[nodiscard]] UndoEntry const* PeekUndo() const noexcept
        {
            return m_undo.empty() ? nullptr : &m_undo.back();
        }

        [[nodiscard]] UndoEntry const* PeekRedo() const noexcept
        {
            return m_redo.empty() ? nullptr : &m_redo.back();
        }

        void Clear() noexcept
        {
            m_undo.clear();
            m_redo.clear();
        }

    private:
        void PushUndoInternal(UndoEntry entry)
        {
            if (m_undo.size() >= m_maxDepth)
            {
                m_undo.erase(m_undo.begin());
            }
            m_undo.push_back(std::move(entry));
        }

        std::size_t m_maxDepth{ kDefaultMaxUndoDepth };
        std::vector<UndoEntry> m_undo;
        std::vector<UndoEntry> m_redo;
    };
}
