#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace FileScoutNative::Domain
{
    // Same bit values as winrt::Windows::System::VirtualKeyModifiers, so the window can cast.
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

    struct FileShortcut
    {
        std::uint32_t key;  // Win32 virtual-key code (equal to winrt VirtualKey)
        ShortcutModifiers modifiers;
        std::wstring_view command_id;
    };

    // The window-wide accelerators that run a context command. Each chord appears once: the file list
    // does not bind any of them again in its own key handler (UB-05).
    inline constexpr std::array<FileShortcut, 31> kFileShortcuts{ {
        { 'A', ShortcutModifiers::Control, L"selectAll" },
        { 'C', ShortcutModifiers::Control, L"copy" },
        { 'C', ShortcutModifiers::Control | ShortcutModifiers::Shift, L"copyPath" },
        { 'X', ShortcutModifiers::Control, L"cut" },
        { 'V', ShortcutModifiers::Control, L"paste" },
        { 0x2E /*VK_DELETE*/, ShortcutModifiers::None, L"delete" },
        { 0x2E /*VK_DELETE*/, ShortcutModifiers::Shift, L"permanentDelete" },
        { 0x71 /*VK_F2*/, ShortcutModifiers::None, L"rename" },
        { 0x74 /*VK_F5*/, ShortcutModifiers::None, L"refresh" },
        { 0x0D /*VK_RETURN*/, ShortcutModifiers::Menu, L"properties" },
        { 0x25 /*VK_LEFT*/, ShortcutModifiers::Menu, L"back" },
        { 0x27 /*VK_RIGHT*/, ShortcutModifiers::Menu, L"forward" },
        { 0x26 /*VK_UP*/, ShortcutModifiers::Menu, L"navigateUp" },
        { 0x08 /*VK_BACK*/, ShortcutModifiers::None, L"navigateUpBackspace" },
        { 'N', ShortcutModifiers::Control | ShortcutModifiers::Shift, L"newFolder" },
        { 'H', ShortcutModifiers::Control, L"toggleHiddenFiles" },
        { 'T', ShortcutModifiers::Control, L"newTab" },
        { 'T', ShortcutModifiers::Control | ShortcutModifiers::Shift, L"reopenClosedTab" },
        { 'W', ShortcutModifiers::Control, L"closeTab" },
        { 0x21 /*VK_PRIOR (PageUp)*/, ShortcutModifiers::Control, L"prevTab" },
        { 0x22 /*VK_NEXT (PageDown)*/, ShortcutModifiers::Control, L"nextTab" },
        { '1', ShortcutModifiers::Control, L"selectTab1" },
        { '2', ShortcutModifiers::Control, L"selectTab2" },
        { '3', ShortcutModifiers::Control, L"selectTab3" },
        { '4', ShortcutModifiers::Control, L"selectTab4" },
        { '5', ShortcutModifiers::Control, L"selectTab5" },
        { '6', ShortcutModifiers::Control, L"selectTab6" },
        { '7', ShortcutModifiers::Control, L"selectTab7" },
        { '8', ShortcutModifiers::Control, L"selectTab8" },
        { '9', ShortcutModifiers::Control, L"selectTab9" },
        { 0x09 /*VK_TAB*/, ShortcutModifiers::Control, L"switchPane" },
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

    // Commands that act on the selected files, or on text when a text field has focus.
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

    // U-236 / UB-04: while an editable control (search box, address box, palette, a dialog's text
    // box) has focus, the file commands belong to it: F2 must not rename the selected file and
    // Ctrl+A/C/X/V and Delete edit the text. Window commands (F5, Alt+Left, Ctrl+T, ...) still run.
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

    // Key routing of the file list's PreviewKeyDown. Ctrl/Alt/Win chords (Ctrl+A among them) are left
    // to the window accelerators, so each chord has a single binding.
    [[nodiscard]] constexpr ListKeyAction ResolveListKeyAction(std::uint32_t const key, bool const hasModifierChord)
    {
        if (hasModifierChord)
        {
            return ListKeyAction::None;
        }
        return key == 0x0D /*VK_RETURN*/ ? ListKeyAction::ActivateSelection : ListKeyAction::TypeAhead;
    }
}
