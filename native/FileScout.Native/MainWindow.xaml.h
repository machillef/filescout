#pragma once

#include "MainWindow.g.h"
#include "Domain\AddressNavigation.h"
#include "Domain\BatchRenamePlan.h"
#include "Domain\ContextMenuModel.h"
#include "Domain\PaneSelectionState.h"
#include "Domain\ShellItemRef.h"
#include "Domain\DriveCapacity.h"
#include "Domain\SidebarCache.h"
#include "Domain\TabState.h"
#include "Shell\ContextMenuService.h"
#include "Shell\FavoritesService.h"
#include "Shell\ShellClipboardService.h"
#include "Shell\ShellFileOperationService.h"
#include "Shell\ShellOverflowMenuService.h"
#include "Shell\CommandRegistry.h"
#include "Shell\DefaultFileManagerService.h"
#include "Shell\ShellStaWorker.h"
#include "Shell\ShellInteropService.h"
#include "Search\SearchService.h"
#include "Domain\FileRowViewModel.h"
#include <atomic>
#include <memory>

namespace winrt::FileScoutNative::implementation
{
    // Sort column applied to a file-list pane.
    enum class SortColumn { Name, Type, Size, DateModified };

    // Sort direction applied to a file-list pane.
    enum class SortDirection { Ascending, Descending };

    // Presentation mode for the file list — cards show rich cards; list shows compact rows.
    enum class ViewMode { Cards, List };

    // Search state for the address bar.
    enum class SearchMode { None, Filter, Recursive };

    // Per-pane UI and browse state. One instance per visible pane.
    struct PaneLoadTiming
    {
        uint64_t load_start_tick{ 0 };
        uint64_t enumeration_end_tick{ 0 };
        uint64_t first_batch_publish_tick{ 0 };
        uint64_t final_publish_tick{ 0 };
    };

    struct PaneContext
    {
        std::filesystem::path current_path;
        std::vector<::FileScoutNative::Domain::ShellItemRef> items;
        ::FileScoutNative::Domain::PaneSelectionState selection;
        std::vector<::FileScoutNative::Domain::TabState> tabs;
        std::vector<winrt::Windows::Foundation::Collections::IObservableVector<
            winrt::FileScoutNative::FileRowViewModel>> tab_rows;
        winrt::Microsoft::UI::Xaml::Controls::ListView active_list_view{ nullptr };
        int32_t active_tab_index{ 0 };
        bool tab_switching{ false };
        bool is_drives_view{ false };   // True when the pane shows the "This PC" drives list.
        bool is_archive_view{ false };  // True when the pane shows a virtual archive folder.
        bool is_recycle_bin_view{ false };  // True when the pane shows the Recycle Bin.
        // View toggles — per-pane to match Explorer behavior.
        bool show_hidden_files{ false };
        bool show_file_extensions{ false };
        // Sort state — shared across all tabs in this pane (same as Explorer behaviour).
        SortColumn sort_column{ SortColumn::Name };
        SortDirection sort_direction{ SortDirection::Ascending };
        // When true, rows render group headers derived from the active sort column.
        bool group_by_sort_column{ false };
        // View mode — cards (rich cards) or list (compact rows).
        ViewMode view_mode{ ViewMode::List };
        // Per-pane navigation history (back/forward stacks).
        std::vector<std::filesystem::path> back_stack;
        std::vector<std::filesystem::path> forward_stack;

        // Incremented each time LoadDirectory is called; lets background loads detect stale results.
        uint32_t load_generation{ 0 };
        PaneLoadTiming last_load_timing{};
        // Set to true while a staged final-batch publish is still pending. Cleared by
        // RebuildAllTabRowsForPane so any mid-load sort/group/view-mode change cancels the append.
        bool awaiting_final_batch{ false };
        // Set to true when the user interacts with the pane after the first batch appears.
        // Prevents the final-batch publish from overwriting live selection/focus state.
        bool user_interacted_after_first_batch{ false };

        // --- Search state (R17) ---
        SearchMode search_mode{ SearchMode::None };
        std::wstring search_query;
        // Snapshot of current tab's items taken at Filter entry.
        // Used for both scoring and Escape-restore; avoids live pane.items divergence.
        std::vector<::FileScoutNative::Domain::ShellItemRef> search_snapshot;
        // Visible search-mode items, indexed in lockstep with the current ListView rows.
        std::vector<::FileScoutNative::Domain::ShellItemRef> filtered_items;
        // Incremented whenever recursive search publishes or tears down visible rows so pending
        // icon backfills for older search snapshots cannot mutate the current list.
        uint32_t search_view_generation{ 0 };
        // Replaced whenever recursive rows are rebuilt or search mode tears down so stale
        // background icon extraction can abandon work before touching the shell repeatedly.
        std::shared_ptr<std::atomic<bool>> search_icon_backfill_cancel{
            std::make_shared<std::atomic<bool>>(false) };
        ::FileScoutNative::Search::SearchService search_service;
    };

    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void BackButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ForwardButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RefreshButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void AddressTextBox_KeyDown(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void AddressTextBox_LostFocus(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void BreadcrumbBarBorder_Tapped(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const&);
        void SearchAccelerator_Invoked(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void SearchTextBox_KeyDown(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void SearchTextBox_TextChanged(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        void FileRowsListView_SelectionChanged(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void FileRowsListView_DoubleTapped(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&);
        void FileRowsListView_RightTapped(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const&);
        void PaneSurface_ContextRequested(
            winrt::Microsoft::UI::Xaml::UIElement const&,
            winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const&);
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
        void TitleBarDragHost_Loaded(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void TitleBarDragHost_SizeChanged(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void DualPaneButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

        // R13a: command palette event handlers (must be public for XAML Connect).
        void CommandPalette_TextChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void CommandPalette_ItemClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        void CommandPalette_TextBox_KeyDown(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);

    private:
        HWND m_hwnd{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcherQueue{ nullptr };
        winrt::Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
        winrt::Microsoft::UI::Input::InputNonClientPointerSource m_titleBarPointerSource{ nullptr };

        // Dual-pane state: index 0 = left, index 1 = right.
        std::array<PaneContext, 2> m_panes{};
        // Folders the user left in either pane, oldest first (UB-11); feeds the sidebar recents.
        std::vector<std::filesystem::path> m_recentFolders;
        static constexpr std::size_t kRecentFolderCapacity = 32;
        int32_t m_activePaneIndex{ 0 };
        // Set to true in TitleBarDragHost_Loaded (first render complete).
        // Guards against deferred WinUI TabView SelectionChanged events during init
        // overriding the intended default active pane (0 = left).
        bool m_initComplete{ false };
        // Panel visibility state — toggled by togglePreview / toggleDualPane commands.
        bool m_previewPanelVisible{ true };
        bool m_dualPaneVisible{ true };
        std::optional<std::filesystem::path> m_deferredRightPanePath;
        // S4: letter-jump border flash timer (single-shot, 300 ms).
        winrt::Microsoft::UI::Xaml::DispatcherTimer m_letterJumpTimer{ nullptr };
        std::wstring m_currentThemeId{ L"default-dark" };

        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout m_contextMenu{ nullptr };
        ::FileScoutNative::Shell::ContextMenuService m_contextMenuService;
        ::FileScoutNative::Shell::FavoritesService m_favoritesService;
        ::FileScoutNative::Shell::DefaultFileManagerService m_defaultFileManager;
        ::FileScoutNative::Shell::ShellClipboardService m_shellClipboardService;
        ::FileScoutNative::Shell::ShellFileOperationService m_shellFileOperationService;
        // S-03: IFileOperation work runs here, never on the UI thread. Declared after the services its
        // tasks use, so it is destroyed (and joins) first.
        ::FileScoutNative::Shell::ShellStaWorker m_shellWorker;

        // co_await OnShellThread(fn): fn runs on the shell STA, the coroutine resumes on the UI thread.
        template <typename Fn>
        auto OnShellThread(Fn fn)
        {
            auto resume = [queue = m_dispatcherQueue](std::function<void()> continuation)
            {
                queue.TryEnqueue([continuation = std::move(continuation)] { continuation(); });
            };
            return ::FileScoutNative::Shell::RunOnShellThread(m_shellWorker, std::move(resume), std::move(fn));
        }
        winrt::fire_and_forget PasteIntoAsync(std::filesystem::path destination);
        ::FileScoutNative::Shell::ShellOverflowMenuService m_shellOverflowMenuService;
        ::FileScoutNative::Shell::ShellInteropService m_shellInterop;
        // UB-10: the sidebar is rebuilt on navigation; the drives (with free space) are read once
        // and reload on a volume change or Refresh. Sidebar icons are cached by ShellInteropService.
        ::FileScoutNative::Domain::CachedValue<std::vector<::FileScoutNative::Domain::SidebarDriveUsage>> m_sidebarDrives;
        ::FileScoutNative::Shell::CommandRegistry m_commandRegistry;
        POINT m_lastContextMenuPoint{};
        bool m_hasLastContextMenuPoint{ false };

        // L3: Session-persistent icon cache. Key = lowercase extension (e.g. L".exe"),
        // L"__folder__" for directories, or L"__file__" for files with no extension.
        // Avoids one WriteableBitmap allocation per item — one per unique extension per session.
        std::unordered_map<std::wstring, winrt::Microsoft::UI::Xaml::Media::ImageSource> m_iconSourceCache;

        // Paths of items currently in cut state (for faded icon feedback).
        std::unordered_set<std::wstring> m_cutItemPaths;

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
        static LRESULT CALLBACK NavigationWindowSubclassProc(
            HWND hwnd,
            UINT message,
            WPARAM wParam,
            LPARAM lParam,
            UINT_PTR subclassId,
            DWORD_PTR refData);
        void ConfigureTitleBar();
        void UpdateTitleBarInteractiveRegions();
        void InitializeThemeDropDown();
        void RefreshThemeDropDown();
        void ApplyThemeSelection(std::wstring_view themeId);
        void BenchOnFullList(int32_t paneIndex, std::size_t itemCount);
        void SetDefaultFileManager(bool makeDefault);
        void RefreshThemeSensitiveChrome();
        void SetupKeyboardAccelerators();
        void LoadDirectory(std::filesystem::path const& path,
            std::optional<std::filesystem::path> selectAfter = std::nullopt,
            bool skipHistoryRecord = false);
        [[nodiscard]] bool TryNavigateToAddressInput(std::wstring_view rawInput);
        winrt::fire_and_forget LoadDirectoryAsync(std::filesystem::path path, int32_t paneIndex,
            std::optional<std::filesystem::path> selectAfter);
        // Reloads every pane whose current folder is `folder` (a file operation changed it).
        void RefreshPanesShowing(std::filesystem::path const& folder,
            std::optional<std::filesystem::path> selectAfter = std::nullopt);
        // S7: backfill real per-extension icons asynchronously after fast rows appear.
        winrt::fire_and_forget BackfillIconsAsync(
            int32_t paneIndex, int32_t tabIndex, uint32_t generation,
            std::vector<::FileScoutNative::Domain::ShellItemRef> items);
        winrt::fire_and_forget BackfillSearchResultIconsAsync(
            int32_t paneIndex, int32_t tabIndex, uint32_t searchViewGeneration,
            std::shared_ptr<std::atomic<bool>> cancelToken,
            std::vector<::FileScoutNative::Domain::ShellItemRef> items);
        void UpdateRows(int32_t paneIndex, std::vector<::FileScoutNative::Domain::ShellItemRef> const& items);
        uint32_t AdvanceSearchViewGeneration(PaneContext& pane);
        [[nodiscard]] std::vector<int32_t> GetSelectedItemIndices() const;
        [[nodiscard]] std::vector<::FileScoutNative::Domain::ShellItemRef> GetSelectedItems() const;
        void SyncSelectionFromListView();
        void SelectAllDisplayedItems();
        void SelectItemByIndex(int32_t index);
        void UpdateRowSelectionVisuals(PaneContext& pane);
        void UpdateMultiSelectionDetails(std::vector<int32_t> const& indices);
        void ResetSelectionDetails();
        void UpdateSelectionDetails(::FileScoutNative::Domain::ShellItemRef const& item);
        [[nodiscard]] std::optional<int32_t> ResolveItemIndexFromSource(
            winrt::Windows::Foundation::IInspectable const& source);
        [[nodiscard]] std::vector<::FileScoutNative::Domain::ShellItemRef> const& DisplayedItems(
            PaneContext const& pane) const;
        [[nodiscard]] std::vector<::FileScoutNative::Domain::ShellItemRef> const& DisplayedItems() const;
        [[nodiscard]] ::FileScoutNative::Domain::ShellItemRef const* TryGetDisplayedItemByIndex(
            int32_t index) const;
        void ShowContextMenu(
            ::FileScoutNative::Domain::ContextMenuModel const& model,
            std::optional<int32_t> itemIndex,
            winrt::Microsoft::UI::Xaml::FrameworkElement const& target,
            winrt::Windows::Foundation::Point tapPoint);
        [[nodiscard]] bool ShouldSkipPaneContextMenu(
            winrt::Windows::Foundation::IInspectable const& source) const;
        bool AppendContextCommands(
            winrt::Microsoft::UI::Xaml::Controls::MenuFlyout const& flyout,
            std::vector<::FileScoutNative::Domain::ExplorerCommand> const& commands,
            std::optional<int32_t> itemIndex);
        void ExecuteContextCommand(std::wstring const& commandId, std::optional<int32_t> itemIndex);
        [[nodiscard]] bool ActivateDisplayedItemByIndex(std::optional<int32_t> itemIndex);
        [[nodiscard]] bool ActivateCurrentSelection();
        [[nodiscard]] bool ShouldTextInputOwnFileShortcut(std::wstring_view commandId);
        [[nodiscard]] bool FocusIsInEditableControl();
        winrt::fire_and_forget ShowNewFolderDialogAsync();
        winrt::fire_and_forget ShowRenameDialogAsync(::FileScoutNative::Domain::ShellItemRef item);
        winrt::fire_and_forget ShowBatchRenameDialogAsync(
            std::vector<::FileScoutNative::Domain::ShellItemRef> items);
        winrt::fire_and_forget ShowDeleteDialogAsync(::FileScoutNative::Domain::ShellItemRef item);
        winrt::fire_and_forget ShowDeleteDialogAsync(
            std::vector<::FileScoutNative::Domain::ShellItemRef> items);
        void SelectItemByPath(std::filesystem::path const& path);
        [[nodiscard]] std::optional<int32_t> FindItemIndexByPath(std::filesystem::path const& path) const;
        [[nodiscard]] std::wstring SuggestNewFolderName() const;
        winrt::fire_and_forget OpenArchiveItemAsync(::FileScoutNative::Domain::ShellItemRef item);
        winrt::fire_and_forget ExtractArchiveItemAsync(
            ::FileScoutNative::Domain::ShellItemRef item,
            bool revealAfterExtract);

        void BuildSidebarFavorites();
        void UpdateThisPCSection();
        void RefreshSidebarCaches();
        void UpdateBookmarksSection();
        void LoadDrivesView();

        // R18b: breadcrumb navigation bar + address overlay.
        void UpdateBreadcrumbs(std::filesystem::path const& path);
        void ShowAddressTextBox();
        void HideAddressTextBox();
        void UpdateSearchPlaceholder(std::filesystem::path const& path);
        // R18c: keyboard type-ahead in file list.
        void FileRowsListView_KeyDown(
            winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);

        // R13b: navigation history.
        void NavigateBack();
        void NavigateForward();
        void UpdateNavButtons();

        // R13a: command palette overlay.
        void ShowCommandPalette();
        void HideCommandPalette();
        void RefreshCommandPalette(std::wstring_view query);
        void RefreshDualPaneButton();
        void ApplyPaneLayoutVisibility();
        [[nodiscard]] winrt::Windows::Foundation::IInspectable CreateCommandRowView(
            ::FileScoutNative::Domain::CommandEntry const& cmd);
        // R13e: panel toggle commands.
        void TogglePreviewPanel();
        void ToggleDualPane();
        void ResetSearchState(int32_t paneIndex, bool restoreSnapshot);

        // R17d: per-pane search ProgressBar accessor.
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::ProgressBar SearchProgressForPane(int32_t paneIndex);

        // R10: async image preview with a generation counter to cancel stale loads.
        uint32_t m_previewGeneration{ 0 };
        winrt::fire_and_forget LoadImagePreviewAsync(
            std::filesystem::path path, uint32_t generation);

        // Sort header helpers.
        void BuildSortHeader(int32_t paneIndex);
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Border SortHeaderForPane(int32_t paneIndex);
        void SetSortColumn(int32_t paneIndex, SortColumn column);
        static void SortItems(std::vector<::FileScoutNative::Domain::ShellItemRef>& items,
                              SortColumn column, SortDirection direction);
        void SetGroupingEnabled(int32_t paneIndex, bool enabled);

        // View toggles
        void ToggleHiddenFiles(int32_t paneIndex);
        void ToggleExtensions(int32_t paneIndex);

        // View mode helpers (R9b).
        void SetViewMode(int32_t paneIndex, ViewMode mode);
        void RebuildAllTabRowsForPane(int32_t paneIndex);

        winrt::Microsoft::UI::Xaml::Controls::ListView CreateTabListView(
            winrt::Windows::Foundation::Collections::IObservableVector<
                winrt::FileScoutNative::FileRowViewModel> const& source,
            int32_t paneIndex);
        void AddTab(std::filesystem::path const& startPath);
        void CloseTab(int32_t index);
        void RestoreActiveTab();
        void RestoreSelectionFromState(
            PaneContext& pane,
            ::FileScoutNative::Domain::PaneSelectionState const& selection,
            int32_t fallbackSelectedIndex);
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
