#pragma once

#include "MainWindow.g.h"
#include "Domain\ContextMenuModel.h"
#include "Domain\PaneSelectionState.h"
#include "Domain\ShellItemRef.h"
#include "Shell\ContextMenuService.h"
#include "Shell\ShellFileOperationService.h"
#include "Shell\ShellOverflowMenuService.h"
#include "Shell\ShellInteropService.h"

namespace winrt::FileScoutNative::implementation
{
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

    private:
        HWND m_hwnd{ nullptr };
        std::filesystem::path m_currentPath;
        std::vector<::FileScoutNative::Domain::ShellItemRef> m_items;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_rows{
            winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>() };
        ::FileScoutNative::Domain::PaneSelectionState m_selection;
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout m_contextMenu{ nullptr };
        ::FileScoutNative::Shell::ContextMenuService m_contextMenuService;
        ::FileScoutNative::Shell::ShellFileOperationService m_shellFileOperationService;
        ::FileScoutNative::Shell::ShellOverflowMenuService m_shellOverflowMenuService;
        ::FileScoutNative::Shell::ShellInteropService m_shellInterop;
        POINT m_lastContextMenuPoint{};
        bool m_hasLastContextMenuPoint{ false };

        HWND GetWindowHandle();
        void ConfigureWindow();
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
    };
}

namespace winrt::FileScoutNative::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
