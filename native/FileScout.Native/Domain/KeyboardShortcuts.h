#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace FileScoutNative::Domain
{
    enum class ShortcutModifiers : std::uint32_t
    {
        None = 0,
        Control = 1,
        Menu = 2,
        Shift = 4,
    };

    [[nodiscard]] constexpr ShortcutModifiers operator|(ShortcutModifiers const left, ShortcutModifiers const right)
    {
        return static_cast<ShortcutModifiers>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    namespace Key
    {
        inline constexpr std::uint32_t Back = 0x08;
        inline constexpr std::uint32_t Tab = 0x09;
        inline constexpr std::uint32_t Enter = 0x0D;
        inline constexpr std::uint32_t PageUp = 0x21;
        inline constexpr std::uint32_t PageDown = 0x22;
        inline constexpr std::uint32_t Left = 0x25;
        inline constexpr std::uint32_t Up = 0x26;
        inline constexpr std::uint32_t Right = 0x27;
        inline constexpr std::uint32_t Delete = 0x2E;
        inline constexpr std::uint32_t F2 = 0x71;
        inline constexpr std::uint32_t F5 = 0x74;
    }

    struct FileShortcut
    {
        std::uint32_t key;
        ShortcutModifiers modifiers;
        std::wstring_view command_id;
    };

    inline constexpr std::array<FileShortcut, 31> kFileShortcuts{ {
        { 'A', ShortcutModifiers::Control, L"selectAll" },
        { 'C', ShortcutModifiers::Control, L"copy" },
        { 'C', ShortcutModifiers::Control | ShortcutModifiers::Shift, L"copyPath" },
        { 'X', ShortcutModifiers::Control, L"cut" },
        { 'V', ShortcutModifiers::Control, L"paste" },
        { Key::Delete, ShortcutModifiers::None, L"delete" },
        { Key::Delete, ShortcutModifiers::Shift, L"permanentDelete" },
        { Key::F2, ShortcutModifiers::None, L"rename" },
        { Key::F5, ShortcutModifiers::None, L"refresh" },
        { Key::Enter, ShortcutModifiers::Menu, L"properties" },
        { Key::Left, ShortcutModifiers::Menu, L"back" },
        { Key::Right, ShortcutModifiers::Menu, L"forward" },
        { Key::Up, ShortcutModifiers::Menu, L"navigateUp" },
        { Key::Back, ShortcutModifiers::None, L"navigateUpBackspace" },
        { 'N', ShortcutModifiers::Control | ShortcutModifiers::Shift, L"newFolder" },
        { 'H', ShortcutModifiers::Control, L"toggleHiddenFiles" },
        { 'T', ShortcutModifiers::Control, L"newTab" },
        { 'T', ShortcutModifiers::Control | ShortcutModifiers::Shift, L"reopenClosedTab" },
        { 'W', ShortcutModifiers::Control, L"closeTab" },
        { Key::PageUp, ShortcutModifiers::Control, L"prevTab" },
        { Key::PageDown, ShortcutModifiers::Control, L"nextTab" },
        { '1', ShortcutModifiers::Control, L"selectTab1" },
        { '2', ShortcutModifiers::Control, L"selectTab2" },
        { '3', ShortcutModifiers::Control, L"selectTab3" },
        { '4', ShortcutModifiers::Control, L"selectTab4" },
        { '5', ShortcutModifiers::Control, L"selectTab5" },
        { '6', ShortcutModifiers::Control, L"selectTab6" },
        { '7', ShortcutModifiers::Control, L"selectTab7" },
        { '8', ShortcutModifiers::Control, L"selectTab8" },
        { '9', ShortcutModifiers::Control, L"selectTab9" },
        { Key::Tab, ShortcutModifiers::Control, L"switchPane" },
    } };

    [[nodiscard]] constexpr FileShortcut const* FindFileShortcut(std::uint32_t const key, ShortcutModifiers const modifiers)
    {
        for (auto const& shortcut : kFileShortcuts)
        {
            if (shortcut.key == key && shortcut.modifiers == modifiers)
            {
                return &shortcut;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr bool IsFileItemCommand(std::wstring_view const commandId)
    {
        return commandId == L"selectAll" ||
            commandId == L"copy" ||
            commandId == L"copyPath" ||
            commandId == L"cut" ||
            commandId == L"paste" ||
            commandId == L"delete" ||
            commandId == L"permanentDelete" ||
            commandId == L"navigateUpBackspace" ||
            commandId == L"rename" ||
            commandId == L"properties";
    }

    [[nodiscard]] constexpr bool ShouldTextInputOwnShortcut(
        std::wstring_view const commandId,
        bool const focusInEditableControl)
    {
        return focusInEditableControl && IsFileItemCommand(commandId);
    }

    enum class ListKeyAction
    {
        None,
        ActivateSelection,
        TypeAhead,
    };

    [[nodiscard]] constexpr ListKeyAction ResolveListKeyAction(std::uint32_t const key, bool const hasModifierChord)
    {
        if (hasModifierChord)
        {
            return ListKeyAction::None;
        }
        return key == Key::Enter ? ListKeyAction::ActivateSelection : ListKeyAction::TypeAhead;
    }
}
