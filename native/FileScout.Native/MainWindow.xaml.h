#pragma once

#include "MainWindow.g.h"
#include "Domain\ContextMenuModel.h"
#include "Domain\PaneSelectionState.h"
#include "Domain\ShellItemRef.h"
#include "Domain\TabState.h"
#include "Shell\ContextMenuService.h"
#include "Shell\FavoritesService.h"
#include "Shell\ShellClipboardService.h"
#include "Shell\ShellFileOperationService.h"
#include "Shell\ShellOverflowMenuService.h"
#include "Shell\ShellInteropService.h"

namespace winrt::FileScoutNative::implementation
{
    // Per-pane UI and browse state. One instance per visible pane.
    struct PaneContext
    {
        std::filesystem::path current_path;
        std::vector<::FileScoutNative::Domain::ShellItemRef> items;
        ::FileScoutNative::Domain::PaneSelectionState selection;
        std::vector<::FileScoutNative::Domain::TabState> tabs;
        std::vector<winrt::Windows::Foundation::Collections::IObservableVector<
            winrt::Windows::Foundation::IInspectable>> tab_rows;
        winrt::Microsoft::UI::Xaml::Controls::ListView active_list_view{ nullptr };
        int32_t active_tab_index{ 0 };
        bool tab_switching{ false };
        bool is_drives_view{ false };   // True when the pane shows the "This PC" drives list.
    };

    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void RefreshButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void AddressTextBox_KeyDown(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void FileRowsListView_SelectionChanged(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void FileRowsListView_DoubleTapped(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&);
        void FileRowsListView_RightTapped(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const&);
        void FileRowsListView_DragItemsStarting(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const&);
        void FileRowsListView_DragOver(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::DragEventArgs const&);
        winrt::fire_and_forget FileRowsListView_Drop(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::DragEventArgs const&);

        void PaneTabView_AddTabButtonClick(
            winrt::Microsoft::UI::Xaml::Controls::TabView const&,
            winrt::Windows::Foundation::IInspectable const&);
        void PaneTabView_TabCloseRequested(
            winrt::Microsoft::UI::Xaml::Controls::TabView const&,
            winrt::Microsoft::UI::Xaml::Controls::TabViewTabCloseRequestedEventArgs const&);
        void PaneTabView_SelectionChanged(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);

    private:
        HWND m_hwnd{ nullptr };

        // Dual-pane state: index 0 = left, index 1 = right.
        std::array<PaneContext, 2> m_panes{};
        int32_t m_activePaneIndex{ 0 };

        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout m_contextMenu{ nullptr };
        ::FileScoutNative::Shell::ContextMenuService m_contextMenuService;
        ::FileScoutNative::Shell::FavoritesService m_favoritesService;
        ::FileScoutNative::Shell::ShellClipboardService m_shellClipboardService;
        ::FileScoutNative::Shell::ShellFileOperationService m_shellFileOperationService;
        ::FileScoutNative::Shell::ShellOverflowMenuService m_shellOverflowMenuService;
        ::FileScoutNative::Shell::ShellInteropService m_shellInterop;
        POINT m_lastContextMenuPoint{};
        bool m_hasLastContextMenuPoint{ false };

        // Active-pane accessors.
        [[nodiscard]] PaneContext& ActivePane() noexcept { return m_panes[m_activePaneIndex]; }
        [[nodiscard]] PaneContext const& ActivePane() const noexcept { return m_panes[m_activePaneIndex]; }
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::TabView ActiveTabView();
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::TabView TabViewForPane(int32_t paneIndex);
        [[nodiscard]] int32_t PaneIndexForTabView(
            winrt::Microsoft::UI::Xaml::Controls::TabView const& tv);
        void SetActivePaneIndex(int32_t paneIndex);
        void UpdateActivePaneIndicator();

        HWND GetWindowHandle();
        void ConfigureWindow();
        void SetupKeyboardAccelerators();
        void LoadDirectory(std::filesystem::path const& path);
        void UpdateRows(std::vector<::FileScoutNative::Domain::ShellItemRef> const& items);
        void SelectItemByIndex(int32_t index);
        void ResetSelectionDetails();
        void UpdateSelectionDetails(::FileScoutNative::Domain::ShellItemRef const& item);
        [[nodiscard]] std::optional<int32_t> ResolveItemIndexFromSource(
            winrt::Windows::Foundation::IInspectable const& source);
        void ShowContextMenu(
            ::FileScoutNative::Domain::ContextMenuModel const& model,
            std::optional<int32_t> itemIndex,
            winrt::Microsoft::UI::Xaml::FrameworkElement const& target);
        void AppendContextCommands(
            winrt::Microsoft::UI::Xaml::Controls::MenuFlyout const& flyout,
            std::vector<::FileScoutNative::Domain::ExplorerCommand> const& commands,
            std::optional<int32_t> itemIndex);
        void ExecuteContextCommand(std::wstring const& commandId, std::optional<int32_t> itemIndex);
        winrt::fire_and_forget ShowNewFolderDialogAsync();
        winrt::fire_and_forget ShowRenameDialogAsync(::FileScoutNative::Domain::ShellItemRef item);
        winrt::fire_and_forget ShowDeleteDialogAsync(::FileScoutNative::Domain::ShellItemRef item);
        void SelectItemByPath(std::filesystem::path const& path);
        [[nodiscard]] std::optional<int32_t> FindItemIndexByPath(std::filesystem::path const& path) const;
        [[nodiscard]] std::wstring SuggestNewFolderName() const;
        [[nodiscard]] static std::optional<std::wstring> ValidateFileName(std::wstring const& name);

        void BuildSidebarFavorites();
        void LoadDrivesView();

        winrt::Microsoft::UI::Xaml::Controls::ListView CreateTabListView(
            winrt::Windows::Foundation::Collections::IObservableVector<
                winrt::Windows::Foundation::IInspectable> const& source,
            int32_t paneIndex);
        void AddTab(std::filesystem::path const& startPath);
        void CloseTab(int32_t index);
        void RestoreActiveTab();
        void UpdateTabHeader(int32_t index);
        void UpdateTabCloseability();
        void InitPane(int32_t paneIndex, std::filesystem::path const& startPath);
    };
}

namespace winrt::FileScoutNative::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
