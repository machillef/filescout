#include "pch.h"
// IRandomAccessStreamWithContentType return type for co_await file.OpenReadAsync().
// Included here rather than pch.h to avoid PCH bloat across all TUs.
#include <winrt/Windows.Storage.Streams.h>
#include <robuffer.h>   // IBufferByteAccess for WriteableBitmap pixel access
#include "MainWindow.xaml.h"
#include "TraceHelper.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

namespace
{
    winrt::hstring ToHString(std::wstring const& value)
    {
        return winrt::hstring{ value };
    }

    winrt::Microsoft::UI::Xaml::Media::Brush LookupBrush(std::wstring_view const key)
    {
        try
        {
            return winrt::Microsoft::UI::Xaml::Application::Current()
                .Resources()
                .Lookup(winrt::box_value(ToHString(std::wstring{ key })))
                .as<winrt::Microsoft::UI::Xaml::Media::Brush>();
        }
        catch (...)
        {
            return winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                winrt::Windows::UI::Color{ 0xFF, 0xF3, 0xF4, 0xF6 });
        }
    }

    // Synchronous: converts RGBA8-Straight icon pixels into a WriteableBitmap (BGRA8-Premultiplied).
    // Must be called from the UI thread. No async operations needed — avoids STA blocking issues.
    winrt::Microsoft::UI::Xaml::Media::ImageSource BuildIconSource(
        ::FileScoutNative::Domain::IconBitmap const& icon)
    {
        if (icon.rgba.empty() || icon.width == 0 || icon.height == 0)
        {
            return nullptr;
        }

        try
        {
            auto const wb = winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap{
                static_cast<int32_t>(icon.width),
                static_cast<int32_t>(icon.height)
            };

            // Write BGRA8-Premultiplied into the pixel buffer via IBufferByteAccess.
            // icon.rgba is RGBA8-Straight (R=byte0, G=byte1, B=byte2, A=byte3).
            auto const byteAccess = wb.PixelBuffer().as<::Windows::Storage::Streams::IBufferByteAccess>();
            uint8_t* dest = nullptr;
            winrt::check_hresult(byteAccess->Buffer(&dest));

            auto const& src = icon.rgba;
            for (std::size_t i = 0; i + 3 < src.size(); i += 4)
            {
                uint8_t const r = src[i];
                uint8_t const g = src[i + 1];
                uint8_t const b = src[i + 2];
                uint8_t const a = src[i + 3];
                dest[i]     = static_cast<uint8_t>(b * a / 255u);  // B premultiplied
                dest[i + 1] = static_cast<uint8_t>(g * a / 255u);  // G premultiplied
                dest[i + 2] = static_cast<uint8_t>(r * a / 255u);  // R premultiplied
                dest[i + 3] = a;
            }

            wb.Invalidate();
            return wb;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    std::wstring FormatSize(std::uintmax_t const sizeBytes)
    {
        static constexpr std::array<std::wstring_view, 5> suffixes{
            L"B",
            L"KB",
            L"MB",
            L"GB",
            L"TB",
        };

        double value = static_cast<double>(sizeBytes);
        std::size_t suffixIndex = 0;

        while (value >= 1024.0 && suffixIndex + 1 < suffixes.size())
        {
            value /= 1024.0;
            ++suffixIndex;
        }

        std::wstringstream stream;
        stream << std::fixed << std::setprecision(value >= 10.0 || suffixIndex == 0 ? 0 : 1) << value;
        return stream.str() + L" " + std::wstring(suffixes[suffixIndex]);
    }

    std::wstring BuildCommandStatus(
        std::wstring const& action,
        std::optional<::FileScoutNative::Domain::ShellItemRef> const& item)
    {
        if (!item.has_value())
        {
            return action;
        }

        return action + L": " + item->display_name;
    }

    std::wstring TrimFileName(std::wstring value)
    {
        auto const begin = value.find_first_not_of(L" \t\r\n");
        if (begin == std::wstring::npos)
        {
            return {};
        }

        auto const end = value.find_last_not_of(L" \t\r\n");
        return value.substr(begin, end - begin + 1);
    }

    // Creates a rich card row (Cards view mode).
    winrt::Microsoft::UI::Xaml::FrameworkElement CreateCardRow(
        ::FileScoutNative::Domain::ShellItemRef const& item)
    {
        // Build the detail chip: "Type  ·  Size"
        std::wstring details = item.kind_label;
        if (item.size_available)
        {
            details += L"  ·  " + FormatSize(item.size_bytes);
        }

        // Card container — clean modern card with subtle stroke.
        auto rowBorder = winrt::Microsoft::UI::Xaml::Controls::Border{};
        rowBorder.Padding({ 12, 10, 12, 10 });
        rowBorder.Margin({ 0, 0, 0, 8 });  // Gap between cards — embedded here, not in ItemContainerStyle.
        rowBorder.CornerRadius({ 8, 8, 8, 8 });
        rowBorder.BorderThickness({ 1, 1, 1, 1 });
        rowBorder.BorderBrush(LookupBrush(L"CardStrokeColorDefaultBrush"));
        rowBorder.Background(LookupBrush(L"CardBackgroundFillColorSecondaryBrush"));

        // Outer grid: [icon col] [content col]
        auto rowGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
        auto iconColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
        iconColumn.Width({ 44, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
        auto contentColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
        contentColumn.Width({ 1, winrt::Microsoft::UI::Xaml::GridUnitType::Star });
        rowGrid.ColumnDefinitions().Append(iconColumn);
        rowGrid.ColumnDefinitions().Append(contentColumn);
        rowGrid.ColumnSpacing(10);

        // Icon tile — folder gets a tinted accent background, files get a neutral pill.
        auto iconTile = winrt::Microsoft::UI::Xaml::Controls::Border{};
        iconTile.Width(36);
        iconTile.Height(36);
        iconTile.CornerRadius({ 8, 8, 8, 8 });
        iconTile.Background(item.is_folder
                ? LookupBrush(L"AccentFillColorSecondaryBrush")
                : LookupBrush(L"SubtleFillColorSecondaryBrush"));
        iconTile.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);

        auto iconImage = winrt::Microsoft::UI::Xaml::Controls::Image{};
        iconImage.Width(18);
        iconImage.Height(18);
        iconImage.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
        iconImage.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
        iconImage.Stretch(winrt::Microsoft::UI::Xaml::Media::Stretch::Uniform);
        iconImage.Source(BuildIconSource(item.icon));
        iconTile.Child(iconImage);

        // Content column: name + metadata row.
        auto contentStack = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
        contentStack.Spacing(2);
        contentStack.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);

        auto titleText = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        titleText.Text(ToHString(item.display_name));
        titleText.FontSize(14);
        titleText.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        titleText.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

        // Metadata row: type + size on left, date on right.
        auto metaGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
        auto metaLeftCol = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
        metaLeftCol.Width({ 1, winrt::Microsoft::UI::Xaml::GridUnitType::Star });
        auto metaRightCol = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
        metaRightCol.Width({ 1, winrt::Microsoft::UI::Xaml::GridUnitType::Auto });
        metaGrid.ColumnDefinitions().Append(metaLeftCol);
        metaGrid.ColumnDefinitions().Append(metaRightCol);

        auto detailText = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        detailText.Text(ToHString(details));
        detailText.Opacity(0.72);
        detailText.FontSize(12);
        detailText.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

        metaGrid.Children().Append(detailText);
        winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(detailText, 0);

        if (item.date_modified_available && !item.date_modified_str.empty())
        {
            auto dateText = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
            dateText.Text(ToHString(item.date_modified_str));
            dateText.Opacity(0.55);
            dateText.FontSize(11);
            dateText.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Right);
            metaGrid.Children().Append(dateText);
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(dateText, 1);
        }

        contentStack.Children().Append(titleText);
        contentStack.Children().Append(metaGrid);

        rowGrid.Children().Append(iconTile);
        rowGrid.Children().Append(contentStack);
        winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(contentStack, 1);

        rowBorder.Child(rowGrid);
        return rowBorder;
    }

    // Creates a compact list row (List view mode). Columns: icon | name | type | size | date.
    // Column proportions match BuildSortHeader's star ratios so headers align approximately.
    winrt::Microsoft::UI::Xaml::FrameworkElement CreateListRow(
        ::FileScoutNative::Domain::ShellItemRef const& item)
    {
        auto rowBorder = winrt::Microsoft::UI::Xaml::Controls::Border{};
        rowBorder.Padding({ 12, 5, 12, 5 });

        auto rowGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
        rowGrid.ColumnSpacing(8);

        // Columns: [20px icon] [3* name] [1.8* type] [1.2* size] [2.0* date]
        struct ColSpec { double width; winrt::Microsoft::UI::Xaml::GridUnitType unit; };
        static constexpr std::array<ColSpec, 5> cols{{
            { 20.0, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel },
            {  3.0, winrt::Microsoft::UI::Xaml::GridUnitType::Star  },
            {  1.8, winrt::Microsoft::UI::Xaml::GridUnitType::Star  },
            {  1.2, winrt::Microsoft::UI::Xaml::GridUnitType::Star  },
            {  2.0, winrt::Microsoft::UI::Xaml::GridUnitType::Star  },
        }};
        for (auto const& col : cols)
        {
            auto colDef = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            colDef.Width({ col.width, col.unit });
            rowGrid.ColumnDefinitions().Append(colDef);
        }

        // Icon — small, flat, no tile background.
        auto iconImage = winrt::Microsoft::UI::Xaml::Controls::Image{};
        iconImage.Width(16);
        iconImage.Height(16);
        iconImage.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
        iconImage.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
        iconImage.Stretch(winrt::Microsoft::UI::Xaml::Media::Stretch::Uniform);
        iconImage.Source(BuildIconSource(item.icon));
        winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(iconImage, 0);
        rowGrid.Children().Append(iconImage);

        auto addTextCell = [&](std::wstring const& text, int32_t col, double opacity, double fontSize)
        {
            auto tb = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
            tb.Text(ToHString(text));
            tb.FontSize(fontSize);
            tb.Opacity(opacity);
            tb.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
            tb.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(tb, col);
            rowGrid.Children().Append(tb);
        };

        addTextCell(item.display_name, 1, 1.0, 13.0);
        addTextCell(item.kind_label, 2, 0.68, 12.0);
        addTextCell(item.size_available ? FormatSize(item.size_bytes) : std::wstring{}, 3, 0.68, 12.0);
        addTextCell(
            (item.date_modified_available && !item.date_modified_str.empty())
                ? item.date_modified_str
                : std::wstring{},
            4, 0.68, 12.0);

        rowBorder.Child(rowGrid);
        return rowBorder;
    }

    winrt::Microsoft::UI::Xaml::FrameworkElement CreateRowView(
        ::FileScoutNative::Domain::ShellItemRef const& item,
        bool const listView)
    {
        return listView ? CreateListRow(item) : CreateCardRow(item);
    }
}

namespace winrt::FileScoutNative::implementation
{
    MainWindow::MainWindow()
    {
        TraceLog(L"MainWindow ctor: InitializeComponent...");
        InitializeComponent();
        TraceLog(L"MainWindow ctor: InitializeComponent done. ConfigureWindow...");
        ConfigureWindow();
        TraceLog(L"MainWindow ctor: ConfigureWindow done. SetupKeyboardAccelerators...");
        SetupKeyboardAccelerators();
        TraceLog(L"MainWindow ctor: SetupKeyboardAccelerators done.");

        // Start in the user's home folder rather than the process working directory.
        auto const startPath = m_favoritesService.GetUserHomePath();
        TraceLog(L"MainWindow ctor: startPath=" + startPath.wstring());

        // Initialize both panes. Right pane starts at the same path but is loaded
        // lazily — it gets LoadDirectory called when InitPane runs for index 1.
        TraceLog(L"MainWindow ctor: InitPane(0)...");
        InitPane(0, startPath);
        TraceLog(L"MainWindow ctor: InitPane(0) done. InitPane(1)...");
        InitPane(1, startPath);
        TraceLog(L"MainWindow ctor: InitPane(1) done.");

        // Build the sidebar navigation buttons after panes are initialized.
        TraceLog(L"MainWindow ctor: BuildSidebarFavorites...");
        BuildSidebarFavorites();
        TraceLog(L"MainWindow ctor: BuildSidebarFavorites done.");

        // Build sort column headers for both panes.
        TraceLog(L"MainWindow ctor: BuildSortHeader(0)...");
        BuildSortHeader(0);
        TraceLog(L"MainWindow ctor: BuildSortHeader(0) done. BuildSortHeader(1)...");
        BuildSortHeader(1);
        TraceLog(L"MainWindow ctor: BuildSortHeader(1) done.");

        // Pane 0 (left) starts as active with its indicator already lit.
        m_activePaneIndex = 0;
        UpdateActivePaneIndicator();
        TraceLog(L"MainWindow ctor: complete.");
    }

    void MainWindow::RefreshButton_Click(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        LoadDirectory(ActivePane().current_path);
    }

    void MainWindow::AddressTextBox_KeyDown(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() != winrt::Windows::System::VirtualKey::Enter)
        {
            return;
        }

        LoadDirectory(AddressTextBox().Text().c_str());
        args.Handled(true);
    }

    void MainWindow::FileRowsListView_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&)
    {
        if (ActivePane().tab_switching || !ActivePane().active_list_view)
        {
            return;
        }

        SelectItemByIndex(ActivePane().active_list_view.SelectedIndex());
    }

    void MainWindow::FileRowsListView_DoubleTapped(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args)
    {
        args.Handled(true);

        if (!ActivePane().active_list_view)
        {
            return;
        }

        const auto index = ActivePane().active_list_view.SelectedIndex();
        if (index < 0 || index >= static_cast<int32_t>(ActivePane().items.size()))
        {
            return;
        }

        auto const& item = ActivePane().items[static_cast<std::size_t>(index)];
        if (item.is_folder)
        {
            LoadDirectory(item.path);
            return;
        }

        try
        {
            m_shellFileOperationService.OpenPath(GetWindowHandle(), item.path);
            StatusTextBlock().Text(ToHString(L"Opened " + item.display_name));
        }
        catch (std::exception const&)
        {
            StatusTextBlock().Text(ToHString(L"Open failed: " + item.display_name));
        }
    }

    void MainWindow::FileRowsListView_RightTapped(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& args)
    {
        if (POINT point{}; GetCursorPos(&point))
        {
            m_lastContextMenuPoint = point;
            m_hasLastContextMenuPoint = true;
        }

        auto const itemIndex = ResolveItemIndexFromSource(args.OriginalSource());
        if (itemIndex.has_value() && ActivePane().active_list_view)
        {
            ActivePane().active_list_view.SelectedIndex(*itemIndex);
            SelectItemByIndex(*itemIndex);
        }
        else if (ActivePane().active_list_view)
        {
            ActivePane().active_list_view.SelectedIndex(-1);
            SelectItemByIndex(-1);
        }

        ::FileScoutNative::Shell::ContextMenuScenario scenario;
        scenario.can_paste = m_shellClipboardService.CanPasteInto(ActivePane().current_path);
        if (itemIndex.has_value())
        {
            scenario.focused_item = ActivePane().items[static_cast<std::size_t>(*itemIndex)];
        }

        if (ActivePane().active_list_view)
        {
            ShowContextMenu(
                m_contextMenuService.BuildMenu(scenario),
                itemIndex,
                ActivePane().active_list_view);
        }

        args.Handled(true);
    }

    void MainWindow::FileRowsListView_DragItemsStarting(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const& e)
    {
        if (!ActivePane().active_list_view)
        {
            e.Cancel(true);
            return;
        }

        auto const selectedIndex = ActivePane().active_list_view.SelectedIndex();
        if (selectedIndex < 0 || selectedIndex >= static_cast<int32_t>(ActivePane().items.size()))
        {
            e.Cancel(true);
            return;
        }

        auto const& item = ActivePane().items[static_cast<std::size_t>(selectedIndex)];

        try
        {
            auto const pathStr = winrt::hstring{ item.path.wstring() };
            bool const isFolder = item.is_folder;

            // SetDataProvider defers the async StorageItem lookup to a background thread
            // called by the drop target when it actually requests the data.
            // This avoids a blocking .get() call on the STA (UI) thread.
            e.Data().SetDataProvider(
                winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems(),
                [pathStr, isFolder](winrt::Windows::ApplicationModel::DataTransfer::DataProviderRequest const& request)
                {
                    auto deferral = request.GetDeferral();
                    [request, deferral, pathStr, isFolder]() mutable -> winrt::fire_and_forget
                    {
                        co_await winrt::resume_background();
                        try
                        {
                            auto items = winrt::single_threaded_vector<winrt::Windows::Storage::IStorageItem>();
                            if (isFolder)
                            {
                                auto folder = co_await winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(pathStr);
                                items.Append(folder);
                            }
                            else
                            {
                                auto file = co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(pathStr);
                                items.Append(file);
                            }
                            request.SetData(items.GetView());
                        }
                        catch (...) {}
                        deferral.Complete();
                    }();
                });
            e.Data().RequestedOperation(
                winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy |
                winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Move);
        }
        catch (...)
        {
            e.Cancel(true);
        }
    }

    void MainWindow::FileRowsListView_DragOver(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::DragEventArgs const& args)
    {
        if (!args.DataView().Contains(
            winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
        {
            return;
        }

        bool const isMove =
            (args.Modifiers() &
             winrt::Windows::ApplicationModel::DataTransfer::DragDrop::DragDropModifiers::Shift) !=
            winrt::Windows::ApplicationModel::DataTransfer::DragDrop::DragDropModifiers::None;

        args.AcceptedOperation(
            isMove
                ? winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Move
                : winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);

        auto const& uiOverride = args.DragUIOverride();
        uiOverride.Caption(isMove ? L"Move here" : L"Copy here");
        uiOverride.IsGlyphVisible(true);
        uiOverride.IsContentVisible(true);
        uiOverride.IsCaptionVisible(true);
    }

    winrt::fire_and_forget MainWindow::FileRowsListView_Drop(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::DragEventArgs const& args)
    {
        auto lifetime = get_strong();
        auto const deferral = args.GetDeferral();

        // Capture state before the co_await suspension point.
        auto const destinationPath = ActivePane().current_path;
        bool const isMove =
            (args.Modifiers() &
             winrt::Windows::ApplicationModel::DataTransfer::DragDrop::DragDropModifiers::Shift) !=
            winrt::Windows::ApplicationModel::DataTransfer::DragDrop::DragDropModifiers::None;

        try
        {
            auto const storageItems = co_await args.DataView().GetStorageItemsAsync();

            std::vector<std::filesystem::path> sourcePaths;
            sourcePaths.reserve(storageItems.Size());
            for (auto const& storageItem : storageItems)
            {
                sourcePaths.emplace_back(storageItem.Path().c_str());
            }

            if (sourcePaths.empty())
            {
                deferral.Complete();
                co_return;
            }

            m_shellFileOperationService.CopyItemsFromPaths(
                GetWindowHandle(), sourcePaths, destinationPath, isMove);
            LoadDirectory(destinationPath);

            StatusTextBlock().Text(ToHString(
                (isMove ? L"Moved " : L"Copied ") +
                std::to_wstring(sourcePaths.size()) +
                (sourcePaths.size() == 1 ? L" item into " : L" items into ") +
                destinationPath.wstring()));
        }
        catch (...)
        {
            StatusTextBlock().Text(L"Drop failed.");
        }

        deferral.Complete();
    }

    void MainWindow::PaneTabView_AddTabButtonClick(
        winrt::Microsoft::UI::Xaml::Controls::TabView const& sender,
        winrt::Windows::Foundation::IInspectable const&)
    {
        auto const paneIndex = PaneIndexForTabView(sender);
        SetActivePaneIndex(paneIndex);
        AddTab(ActivePane().current_path);
    }

    void MainWindow::PaneTabView_TabCloseRequested(
        winrt::Microsoft::UI::Xaml::Controls::TabView const& sender,
        winrt::Microsoft::UI::Xaml::Controls::TabViewTabCloseRequestedEventArgs const& args)
    {
        auto const paneIndex = PaneIndexForTabView(sender);
        SetActivePaneIndex(paneIndex);

        uint32_t foundAt{};
        if (!sender.TabItems().IndexOf(args.Tab(), foundAt))
        {
            return;
        }
        CloseTab(static_cast<int32_t>(foundAt));
    }

    void MainWindow::PaneTabView_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&)
    {
        auto const tabView = sender.as<winrt::Microsoft::UI::Xaml::Controls::TabView>();
        auto const paneIndex = PaneIndexForTabView(tabView);
        auto& pane = m_panes[paneIndex];

        if (pane.tab_switching || pane.tabs.empty())
        {
            return;
        }

        auto const newIndex = tabView.SelectedIndex();
        if (newIndex < 0 || newIndex >= static_cast<int32_t>(pane.tabs.size()))
        {
            return;
        }

        // Persist current UI state to the outgoing tab.
        if (pane.active_list_view && pane.active_tab_index >= 0 &&
            pane.active_tab_index < static_cast<int32_t>(pane.tabs.size()))
        {
            pane.tabs[pane.active_tab_index].path = pane.current_path;
            pane.tabs[pane.active_tab_index].items = pane.items;
            pane.tabs[pane.active_tab_index].selection = pane.selection;
            pane.tabs[pane.active_tab_index].selected_item_index =
                pane.active_list_view.SelectedIndex();
        }

        pane.active_tab_index = newIndex;

        // Switching to this pane (if not already active) and restore its tab.
        SetActivePaneIndex(paneIndex);
        RestoreActiveTab();
    }

    HWND MainWindow::GetWindowHandle()
    {
        if (m_hwnd == nullptr)
        {
            auto native = this->m_inner.as<::IWindowNative>();
            native->get_WindowHandle(&m_hwnd);
        }

        return m_hwnd;
    }

    void MainWindow::ConfigureWindow()
    {
        Title(L"FileScout Native");

        SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop{});

        const auto hwnd = GetWindowHandle();
        const UINT dpi = GetDpiForWindow(hwnd);
        const float scale = static_cast<float>(dpi) / 96.0f;
        const int width = static_cast<int>(1520 * scale);
        const int height = static_cast<int>(920 * scale);

        SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
    }

    void MainWindow::SortItems(
        std::vector<::FileScoutNative::Domain::ShellItemRef>& items,
        SortColumn const column,
        SortDirection const direction) const
    {
        std::sort(items.begin(), items.end(),
            [column, direction](
                ::FileScoutNative::Domain::ShellItemRef const& a,
                ::FileScoutNative::Domain::ShellItemRef const& b) -> bool
            {
                // Folders always sort before files regardless of column or direction —
                // consistent with Windows Explorer behaviour on all columns.
                if (a.is_folder != b.is_folder)
                {
                    return a.is_folder > b.is_folder;
                }

                bool ascending{};
                switch (column)
                {
                case SortColumn::Name:
                    ascending = _wcsicmp(a.display_name.c_str(), b.display_name.c_str()) < 0;
                    break;
                case SortColumn::Type:
                    ascending = _wcsicmp(a.kind_label.c_str(), b.kind_label.c_str()) < 0;
                    break;
                case SortColumn::Size:
                    // Items without size (folders) are already separated above; among files,
                    // sort by size then fall back to name for equal sizes.
                    if (a.size_available != b.size_available)
                    {
                        return a.size_available > b.size_available;
                    }
                    if (a.size_bytes != b.size_bytes)
                    {
                        ascending = a.size_bytes < b.size_bytes;
                        break;
                    }
                    ascending = _wcsicmp(a.display_name.c_str(), b.display_name.c_str()) < 0;
                    break;
                case SortColumn::DateModified:
                    if (a.date_modified_available != b.date_modified_available)
                    {
                        return a.date_modified_available > b.date_modified_available;
                    }
                    if (a.date_modified != b.date_modified)
                    {
                        ascending = a.date_modified < b.date_modified;
                        break;
                    }
                    ascending = _wcsicmp(a.display_name.c_str(), b.display_name.c_str()) < 0;
                    break;
                default:
                    ascending = _wcsicmp(a.display_name.c_str(), b.display_name.c_str()) < 0;
                }

                return direction == SortDirection::Ascending ? ascending : !ascending;
            });
    }

    void MainWindow::LoadDirectory(std::filesystem::path const& path)
    {
        TraceLog(L"LoadDirectory: " + path.wstring());
        try
        {
            const auto absolute = std::filesystem::absolute(path);
            TraceLog(L"LoadDirectory: EnumerateDirectory...");
            auto items = m_shellInterop.EnumerateDirectory(absolute);
            TraceLog(L"LoadDirectory: EnumerateDirectory done, count=" + std::to_wstring(items.size()));
            SortItems(items, ActivePane().sort_column, ActivePane().sort_direction);

            auto& pane = ActivePane();
            pane.current_path = absolute;
            pane.is_drives_view = false;
            pane.items = items;
            pane.selection.Clear();

            // Persist the newly loaded state to the active tab.
            pane.tabs[pane.active_tab_index].path = absolute;
            pane.tabs[pane.active_tab_index].items = items;
            pane.tabs[pane.active_tab_index].selection = {};
            pane.tabs[pane.active_tab_index].selected_item_index = -1;
            pane.tabs[pane.active_tab_index].is_drives_view = false;
            UpdateTabHeader(pane.active_tab_index);

            AddressTextBox().Text(ToHString(absolute.wstring()));
            TraceLog(L"LoadDirectory: UpdateRows...");
            UpdateRows(items);
            TraceLog(L"LoadDirectory: UpdateRows done.");

            if (pane.active_list_view)
            {
                pane.tab_switching = true;
                pane.active_list_view.SelectedIndex(-1);
                pane.tab_switching = false;
            }

            TraceLog(L"LoadDirectory: ResetSelectionDetails...");
            ResetSelectionDetails();
            TraceLog(L"LoadDirectory: ResetSelectionDetails done.");

            StatusTextBlock().Text(
                ToHString(std::to_wstring(items.size()) + L" shell-backed entries loaded from " + absolute.wstring()));
        }
        catch (std::exception const&)
        {
            StatusTextBlock().Text(L"Directory load failed.");
        }
    }

    void MainWindow::UpdateRows(std::vector<::FileScoutNative::Domain::ShellItemRef> const& items)
    {
        auto& pane = ActivePane();
        bool const listView = (pane.view_mode == ViewMode::List);
        pane.tab_rows[pane.active_tab_index].Clear();

        for (auto const& item : items)
        {
            pane.tab_rows[pane.active_tab_index].Append(CreateRowView(item, listView));
        }
    }

    void MainWindow::SelectItemByIndex(int32_t const index)
    {
        if (index < 0 || index >= static_cast<int32_t>(ActivePane().items.size()))
        {
            ActivePane().selection.Clear();
            ResetSelectionDetails();
            return;
        }

        auto const& item = ActivePane().items[static_cast<std::size_t>(index)];
        ActivePane().selection.ReplaceWith(item.parsing_name);
        UpdateSelectionDetails(item);

        auto summary = item.kind_label;
        if (item.size_available)
        {
            summary += L"  |  " + FormatSize(item.size_bytes);
        }

        StatusTextBlock().Text(ToHString(L"Selected " + item.display_name + L"  |  " + summary));
    }

    void MainWindow::ResetSelectionDetails()
    {
        TraceLog(L"ResetSelectionDetails: start");
        // Cancel any in-flight image preview load.
        ++m_previewGeneration;

        // Hide image preview and restore the icon tile.
        TraceLog(L"ResetSelectionDetails: PreviewBorder...");
        PreviewBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        TraceLog(L"ResetSelectionDetails: PreviewImage...");
        PreviewImage().Source(nullptr);
        TraceLog(L"ResetSelectionDetails: SelectionIconBorder...");
        SelectionIconBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        TraceLog(L"ResetSelectionDetails: SelectionIconImage...");
        SelectionIconImage().Source(nullptr);
        TraceLog(L"ResetSelectionDetails: SelectionTitleTextBlock...");
        SelectionTitleTextBlock().Text(L"No item selected");
        TraceLog(L"ResetSelectionDetails: SelectionMetaTextBlock...");
        SelectionMetaTextBlock().Text(L"Pick a folder or file to inspect its shell-backed details.");
        TraceLog(L"ResetSelectionDetails: SelectionCreatedTextBlock...");
        SelectionCreatedTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        TraceLog(L"ResetSelectionDetails: SelectionAttributesTextBlock...");
        SelectionAttributesTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        TraceLog(L"ResetSelectionDetails: SelectionPathTextBlock...");
        SelectionPathTextBlock().Text(L"Selection path will appear here.");
        TraceLog(L"ResetSelectionDetails: done.");
    }

    void MainWindow::UpdateSelectionDetails(::FileScoutNative::Domain::ShellItemRef const& item)
    {
        SelectionTitleTextBlock().Text(ToHString(item.display_name));

        // R10: image preview — show thumbnail for recognised image files via async StorageFile
        // load. This avoids URI percent-encoding issues for paths with spaces or Unicode.
        if (item.is_image)
        {
            // Show the icon tile immediately while the async load is in-flight.
            // Cancel any previous load by incrementing the generation counter.
            auto const gen = ++m_previewGeneration;
            PreviewBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            SelectionIconBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            SelectionIconImage().Source(BuildIconSource(item.icon));
            LoadImagePreviewAsync(item.path, gen);
        }
        else
        {
            ++m_previewGeneration;  // Cancel any in-flight load from a previous image selection.
            PreviewBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            SelectionIconBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            SelectionIconImage().Source(BuildIconSource(item.icon));
        }

        // Primary metadata line: type + size (or folder hint).
        auto metadata = item.kind_label;
        if (item.size_available)
        {
            metadata += L"  |  " + FormatSize(item.size_bytes);
        }
        else if (item.is_folder)
        {
            metadata += L"  |  Double-click to enter";
        }
        SelectionMetaTextBlock().Text(ToHString(metadata));

        // R10: Created date.
        if (item.date_created_available && !item.date_created_str.empty())
        {
            SelectionCreatedTextBlock().Text(ToHString(L"Created: " + item.date_created_str));
            SelectionCreatedTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }
        else
        {
            SelectionCreatedTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }

        // R10: File attributes (read-only, hidden, system, archive).
        if (item.file_attributes_available)
        {
            std::wstring attrStr;
            auto const appendAttr = [&attrStr](std::wstring_view label)
            {
                if (!attrStr.empty()) { attrStr += L"  ·  "; }
                attrStr += label;
            };

            if (item.file_attributes & FILE_ATTRIBUTE_READONLY) { appendAttr(L"Read-only"); }
            if (item.file_attributes & FILE_ATTRIBUTE_HIDDEN)   { appendAttr(L"Hidden");    }
            if (item.file_attributes & FILE_ATTRIBUTE_SYSTEM)   { appendAttr(L"System");    }
            if (item.file_attributes & FILE_ATTRIBUTE_ARCHIVE)  { appendAttr(L"Archive");   }

            if (!attrStr.empty())
            {
                SelectionAttributesTextBlock().Text(ToHString(attrStr));
                SelectionAttributesTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            else
            {
                SelectionAttributesTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
        }
        else
        {
            SelectionAttributesTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }

        SelectionPathTextBlock().Text(ToHString(item.path.wstring()));
    }

    std::optional<int32_t> MainWindow::ResolveItemIndexFromSource(
        winrt::Windows::Foundation::IInspectable const& source)
    {
        if (!ActivePane().active_list_view)
        {
            return std::nullopt;
        }

        auto dependencyObject = source.try_as<winrt::Microsoft::UI::Xaml::DependencyObject>();
        while (dependencyObject)
        {
            auto const itemContainer = dependencyObject.try_as<winrt::Microsoft::UI::Xaml::Controls::ListViewItem>();
            if (itemContainer)
            {
                const auto index = ActivePane().active_list_view.IndexFromContainer(itemContainer);
                if (index >= 0)
                {
                    return index;
                }
            }

            dependencyObject = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(dependencyObject);
        }

        return std::nullopt;
    }

    void MainWindow::ShowContextMenu(
        ::FileScoutNative::Domain::ContextMenuModel const& model,
        std::optional<int32_t> const itemIndex,
        winrt::Microsoft::UI::Xaml::FrameworkElement const& target)
    {
        auto flyout = winrt::Microsoft::UI::Xaml::Controls::MenuFlyout{};
        AppendContextCommands(flyout, model.primary_commands, itemIndex);

        bool hasAppendedItems = !model.primary_commands.empty();
        for (auto const& group : model.groups)
        {
            if (group.commands.empty())
            {
                continue;
            }

            if (hasAppendedItems)
            {
                flyout.Items().Append(winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutSeparator{});
            }

            AppendContextCommands(flyout, group.commands, itemIndex);
            hasAppendedItems = true;
        }

        m_contextMenu = flyout;
        m_contextMenu.ShowAt(target);
    }

    void MainWindow::AppendContextCommands(
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout const& flyout,
        std::vector<::FileScoutNative::Domain::ExplorerCommand> const& commands,
        std::optional<int32_t> const itemIndex)
    {
        for (auto const& command : commands)
        {
            auto flyoutItem = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
            flyoutItem.Text(ToHString(command.label));
            if (!command.shortcut_text.empty())
            {
                flyoutItem.KeyboardAcceleratorTextOverride(ToHString(command.shortcut_text));
            }

            flyoutItem.IsEnabled(command.available);

            auto const commandId = command.id;
            flyoutItem.Click([this, commandId, itemIndex](
                                 winrt::Windows::Foundation::IInspectable const&,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
                {
                    ExecuteContextCommand(commandId, itemIndex);
                });

            flyout.Items().Append(flyoutItem);
        }
    }

    void MainWindow::ExecuteContextCommand(
        std::wstring const& commandId,
        std::optional<int32_t> const itemIndex)
    {
        std::optional<::FileScoutNative::Domain::ShellItemRef> item;
        if (itemIndex.has_value() && *itemIndex >= 0 &&
            *itemIndex < static_cast<int32_t>(ActivePane().items.size()))
        {
            item = ActivePane().items[static_cast<std::size_t>(*itemIndex)];
        }

        if (commandId == L"open")
        {
            if (item.has_value() && item->is_folder)
            {
                LoadDirectory(item->path);
                return;
            }

            if (!item.has_value())
            {
                StatusTextBlock().Text(L"Open needs a selected item.");
                return;
            }

            try
            {
                m_shellFileOperationService.OpenPath(GetWindowHandle(), item->path);
                StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Opened", item)));
            }
            catch (std::exception const&)
            {
                StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Open failed for", item)));
            }
            return;
        }

        if (commandId == L"refresh")
        {
            LoadDirectory(ActivePane().current_path);
            return;
        }

        if (commandId == L"openWith")
        {
            if (!item.has_value())
            {
                StatusTextBlock().Text(L"Open With needs a selected item.");
                return;
            }

            try
            {
                m_shellFileOperationService.OpenWith(GetWindowHandle(), item->path);
                StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Opened app picker for", item)));
            }
            catch (std::exception const&)
            {
                StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Open With failed for", item)));
            }
            return;
        }

        if (commandId == L"rename")
        {
            if (!item.has_value())
            {
                StatusTextBlock().Text(L"Rename needs a selected item.");
                return;
            }

            ShowRenameDialogAsync(*item);
            return;
        }

        if (commandId == L"delete")
        {
            if (!item.has_value())
            {
                StatusTextBlock().Text(L"Delete needs a selected item.");
                return;
            }

            ShowDeleteDialogAsync(*item);
            return;
        }

        if (commandId == L"copy")
        {
            if (!item.has_value())
            {
                StatusTextBlock().Text(L"Copy needs a selected item.");
                return;
            }

            try
            {
                std::array selection{ *item };
                m_shellClipboardService.CopyShellItems(selection);
                StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Copied", item)));
            }
            catch (std::exception const&)
            {
                StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Copy failed for", item)));
            }
            return;
        }

        if (commandId == L"cut")
        {
            if (!item.has_value())
            {
                StatusTextBlock().Text(L"Cut needs a selected item.");
                return;
            }

            try
            {
                std::array selection{ *item };
                m_shellClipboardService.CutShellItems(selection);
                StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Cut", item)));
            }
            catch (std::exception const&)
            {
                StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Cut failed for", item)));
            }
            return;
        }

        if (commandId == L"paste")
        {
            if (!m_shellClipboardService.CanPasteInto(ActivePane().current_path))
            {
                StatusTextBlock().Text(L"Paste needs shell items on the clipboard.");
                return;
            }

            try
            {
                auto const outcome = m_shellClipboardService.PasteInto(
                    GetWindowHandle(), ActivePane().current_path);
                LoadDirectory(ActivePane().current_path);

                std::wstring action = outcome.was_move ? L"Moved " : L"Pasted ";
                action += std::to_wstring(outcome.item_count);
                action += outcome.item_count == 1 ? L" item into " : L" items into ";
                action += ActivePane().current_path.wstring();
                StatusTextBlock().Text(ToHString(action));
            }
            catch (std::exception const&)
            {
                StatusTextBlock().Text(
                    ToHString(L"Paste failed into " + ActivePane().current_path.wstring()));
            }
            return;
        }

        if (commandId == L"properties")
        {
            if (!item.has_value())
            {
                StatusTextBlock().Text(L"Properties needs a selected item.");
                return;
            }

            try
            {
                m_shellFileOperationService.ShowProperties(GetWindowHandle(), item->path);
                StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Opened properties for", item)));
            }
            catch (std::exception const&)
            {
                StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Properties failed for", item)));
            }
            return;
        }

        if (commandId == L"moreWindowsActions")
        {
            if (!item.has_value())
            {
                StatusTextBlock().Text(L"Windows overflow menu needs a selected item.");
                return;
            }

            if (m_contextMenu)
            {
                m_contextMenu.Hide();
            }

            auto const screenPoint = m_hasLastContextMenuPoint ? m_lastContextMenuPoint : POINT{ 160, 160 };

            try
            {
                auto const outcome = m_shellOverflowMenuService.ShowForPath(
                    GetWindowHandle(),
                    item->path,
                    screenPoint);

                if (outcome == ::FileScoutNative::Shell::ShellOverflowMenuOutcome::Invoked)
                {
                    LoadDirectory(ActivePane().current_path);
                    StatusTextBlock().Text(
                        ToHString(BuildCommandStatus(L"Opened Windows overflow menu for", item)));
                }
                else
                {
                    StatusTextBlock().Text(
                        ToHString(BuildCommandStatus(L"Dismissed Windows overflow menu for", item)));
                }
            }
            catch (std::exception const&)
            {
                StatusTextBlock().Text(
                    ToHString(BuildCommandStatus(L"Windows overflow menu failed for", item)));
            }

            return;
        }

        if (commandId == L"newFolder")
        {
            ShowNewFolderDialogAsync();
            return;
        }

        if (commandId == L"newTab")
        {
            AddTab(ActivePane().current_path);
            return;
        }

        if (commandId == L"closeTab")
        {
            CloseTab(ActivePane().active_tab_index);
            return;
        }

        if (commandId == L"switchPane")
        {
            SetActivePaneIndex(1 - m_activePaneIndex);
            AddressTextBox().Text(ToHString(ActivePane().current_path.wstring()));
            return;
        }
    }

    winrt::fire_and_forget MainWindow::ShowNewFolderDialogAsync()
    {
        auto lifetime = get_strong();

        auto textBox = winrt::Microsoft::UI::Xaml::Controls::TextBox{};
        textBox.Text(ToHString(SuggestNewFolderName()));
        textBox.SelectAll();

        auto dialog = winrt::Microsoft::UI::Xaml::Controls::ContentDialog{};
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(L"Create New Folder"));
        dialog.PrimaryButtonText(L"Create");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
        dialog.Content(textBox);

        if (co_await dialog.ShowAsync() != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto const folderName = TrimFileName(textBox.Text().c_str());
        if (auto const error = ValidateFileName(folderName); error.has_value())
        {
            StatusTextBlock().Text(ToHString(*error));
            co_return;
        }

        try
        {
            auto const createdPath = m_shellFileOperationService.CreateFolder(
                GetWindowHandle(),
                ActivePane().current_path,
                folderName);
            LoadDirectory(ActivePane().current_path);
            SelectItemByPath(createdPath);
            StatusTextBlock().Text(ToHString(L"Created folder: " + createdPath.filename().wstring()));
        }
        catch (std::exception const&)
        {
            StatusTextBlock().Text(
                ToHString(L"New folder failed in " + ActivePane().current_path.wstring()));
        }
    }

    winrt::fire_and_forget MainWindow::ShowRenameDialogAsync(::FileScoutNative::Domain::ShellItemRef item)
    {
        auto lifetime = get_strong();

        auto textBox = winrt::Microsoft::UI::Xaml::Controls::TextBox{};
        textBox.Text(ToHString(item.display_name));
        textBox.SelectAll();

        auto dialog = winrt::Microsoft::UI::Xaml::Controls::ContentDialog{};
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(L"Rename"));
        dialog.PrimaryButtonText(L"Rename");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
        dialog.Content(textBox);

        if (co_await dialog.ShowAsync() != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto const newName = TrimFileName(textBox.Text().c_str());
        if (auto const error = ValidateFileName(newName); error.has_value())
        {
            StatusTextBlock().Text(ToHString(*error));
            co_return;
        }

        if (_wcsicmp(newName.c_str(), item.display_name.c_str()) == 0)
        {
            StatusTextBlock().Text(ToHString(L"Rename skipped: " + item.display_name));
            co_return;
        }

        try
        {
            auto const renamedPath = m_shellFileOperationService.RenameItem(
                GetWindowHandle(),
                item.path,
                newName);
            LoadDirectory(ActivePane().current_path);
            SelectItemByPath(renamedPath);
            StatusTextBlock().Text(ToHString(L"Renamed to " + renamedPath.filename().wstring()));
        }
        catch (std::exception const&)
        {
            StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Rename failed for", item)));
        }
    }

    winrt::fire_and_forget MainWindow::ShowDeleteDialogAsync(::FileScoutNative::Domain::ShellItemRef item)
    {
        auto lifetime = get_strong();

        auto dialog = winrt::Microsoft::UI::Xaml::Controls::ContentDialog{};
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(L"Delete To Recycle Bin"));
        dialog.PrimaryButtonText(L"Delete");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        dialog.Content(winrt::box_value(
            L"Move " + item.display_name + L" to the Recycle Bin?"));

        if (co_await dialog.ShowAsync() != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        try
        {
            m_shellFileOperationService.DeleteItem(GetWindowHandle(), item.path);
            LoadDirectory(ActivePane().current_path);
            StatusTextBlock().Text(ToHString(L"Deleted " + item.display_name + L" to Recycle Bin"));
        }
        catch (std::exception const&)
        {
            StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Delete failed for", item)));
        }
    }

    void MainWindow::SelectItemByPath(std::filesystem::path const& path)
    {
        auto const itemIndex = FindItemIndexByPath(path);
        if (!itemIndex.has_value() || !ActivePane().active_list_view)
        {
            return;
        }

        ActivePane().active_list_view.SelectedIndex(*itemIndex);
        SelectItemByIndex(*itemIndex);
    }

    std::optional<int32_t> MainWindow::FindItemIndexByPath(std::filesystem::path const& path) const
    {
        for (std::size_t index = 0; index < ActivePane().items.size(); ++index)
        {
            if (ActivePane().items[index].path == path)
            {
                return static_cast<int32_t>(index);
            }
        }

        return std::nullopt;
    }

    std::wstring MainWindow::SuggestNewFolderName() const
    {
        std::wstring candidate = L"New Folder";
        int32_t suffix = 2;

        auto folderNameExists = [this](std::wstring const& name)
            {
                return std::ranges::any_of(ActivePane().items, [&name](auto const& item)
                    {
                        return item.is_folder && _wcsicmp(item.display_name.c_str(), name.c_str()) == 0;
                    });
            };

        while (folderNameExists(candidate))
        {
            candidate = L"New Folder (" + std::to_wstring(suffix++) + L")";
        }

        return candidate;
    }

    void MainWindow::SetupKeyboardAccelerators()
    {
        using VK = winrt::Windows::System::VirtualKey;
        using VKM = winrt::Windows::System::VirtualKeyModifiers;
        using KA = winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator;
        using KAArgs = winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs;

        auto const rootElement = Content().as<winrt::Microsoft::UI::Xaml::UIElement>();
        auto const& accelerators = rootElement.KeyboardAccelerators();

        auto const AddAccelerator = [&](VK const key, VKM const modifiers, std::wstring commandId)
        {
            auto accel = KA{};
            accel.Key(key);
            accel.Modifiers(modifiers);
            accel.Invoked([this, commandId](winrt::Windows::Foundation::IInspectable const&, KAArgs const& e)
            {
                auto const index =
                    ActivePane().active_list_view ? ActivePane().active_list_view.SelectedIndex() : -1;
                std::optional<int32_t> const itemIndex =
                    index >= 0 ? std::optional<int32_t>{ index } : std::nullopt;
                ExecuteContextCommand(commandId, itemIndex);
                e.Handled(true);
            });
            accelerators.Append(accel);
        };

        AddAccelerator(VK::C, VKM::Control, L"copy");
        AddAccelerator(VK::X, VKM::Control, L"cut");
        AddAccelerator(VK::V, VKM::Control, L"paste");
        AddAccelerator(VK::Delete, VKM::None, L"delete");
        AddAccelerator(VK::F2, VKM::None, L"rename");
        AddAccelerator(VK::F5, VKM::None, L"refresh");
        AddAccelerator(VK::T, VKM::Control, L"newTab");
        AddAccelerator(VK::W, VKM::Control, L"closeTab");
        // Ctrl+Tab / Ctrl+Shift+Tab cycle through panes.
        AddAccelerator(VK::Tab, VKM::Control, L"switchPane");
    }

    std::optional<std::wstring> MainWindow::ValidateFileName(std::wstring const& name)
    {
        if (name.empty())
        {
            return std::wstring{ L"File name cannot be empty." };
        }

        static constexpr std::array<wchar_t, 9> invalidChars{ L'/', L'\\', L':', L'*', L'?', L'"', L'<', L'>', L'|' };
        if (auto const invalidChar = std::ranges::find_if(name, [](wchar_t const character)
                {
                    return std::ranges::find(invalidChars, character) != invalidChars.end();
                });
            invalidChar != name.end())
        {
            return std::wstring{ L"File name contains an invalid character." };
        }

        auto upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t const character)
            {
                return static_cast<wchar_t>(towupper(character));
            });

        auto const extensionSeparator = upper.find(L'.');
        auto const stem = upper.substr(0, extensionSeparator);
        static constexpr std::array reservedNames{
            L"CON",
            L"PRN",
            L"AUX",
            L"NUL",
            L"COM1",
            L"COM2",
            L"COM3",
            L"COM4",
            L"COM5",
            L"COM6",
            L"COM7",
            L"COM8",
            L"COM9",
            L"LPT1",
            L"LPT2",
            L"LPT3",
            L"LPT4",
            L"LPT5",
            L"LPT6",
            L"LPT7",
            L"LPT8",
            L"LPT9",
        };

        if (std::ranges::find(reservedNames, stem) != reservedNames.end())
        {
            return std::wstring{ L"That name is reserved on Windows." };
        }

        return std::nullopt;
    }

    winrt::Microsoft::UI::Xaml::Controls::ListView MainWindow::CreateTabListView(
        winrt::Windows::Foundation::Collections::IObservableVector<
            winrt::Windows::Foundation::IInspectable> const& source,
        int32_t const paneIndex)
    {
        auto listView = winrt::Microsoft::UI::Xaml::Controls::ListView{};
        listView.Margin({ 12, 0, 12, 12 });
        listView.AllowDrop(true);
        listView.CanDrag(true);
        listView.SelectionMode(winrt::Microsoft::UI::Xaml::Controls::ListViewSelectionMode::Single);
        listView.ItemsSource(source);

        // Apply item container style from the root Grid's resources.
        try
        {
            auto const rootGrid = Content().as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
            auto const style = rootGrid.Resources()
                .Lookup(winrt::box_value(winrt::hstring{ L"FileRowItemStyle" }))
                .as<winrt::Microsoft::UI::Xaml::Style>();
            listView.ItemContainerStyle(style);
        }
        catch (...) {}

        // Wire events with pane-index capture so that interacting with a ListView
        // in the inactive pane activates that pane first.
        listView.SelectionChanged([this, paneIndex](auto const& s, auto const& a)
            {
                if (m_panes[paneIndex].tab_switching) return;
                SetActivePaneIndex(paneIndex);
                FileRowsListView_SelectionChanged(s, a);
            });

        listView.DoubleTapped([this, paneIndex](auto const& s, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                FileRowsListView_DoubleTapped(s, a);
            });

        listView.RightTapped([this, paneIndex](auto const& s, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                FileRowsListView_RightTapped(s, a);
            });

        listView.DragItemsStarting([this, paneIndex](auto const& s, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                FileRowsListView_DragItemsStarting(s, a);
            });

        listView.DragOver([this, paneIndex](auto const& s, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                FileRowsListView_DragOver(s, a);
            });

        listView.Drop([this, paneIndex](auto const& s, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                FileRowsListView_Drop(s, a);
            });

        return listView;
    }

    void MainWindow::AddTab(std::filesystem::path const& startPath)
    {
        auto& pane = ActivePane();
        auto tabView = ActiveTabView();

        // Persist current state before adding the new tab.
        if (pane.active_list_view && pane.active_tab_index >= 0 &&
            pane.active_tab_index < static_cast<int32_t>(pane.tabs.size()))
        {
            pane.tabs[pane.active_tab_index].path = pane.current_path;
            pane.tabs[pane.active_tab_index].items = pane.items;
            pane.tabs[pane.active_tab_index].selection = pane.selection;
            pane.tabs[pane.active_tab_index].selected_item_index =
                pane.active_list_view.SelectedIndex();
        }

        auto newRows = winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>();
        pane.tab_rows.push_back(newRows);
        pane.tabs.push_back({});

        auto const newIndex = static_cast<int32_t>(pane.tabs.size()) - 1;
        auto newListView = CreateTabListView(newRows, m_activePaneIndex);

        auto tabItem = winrt::Microsoft::UI::Xaml::Controls::TabViewItem{};
        tabItem.Header(winrt::box_value(winrt::hstring{ L"New Tab" }));
        tabItem.Content(newListView);
        tabView.TabItems().Append(tabItem);

        pane.active_tab_index = newIndex;
        pane.active_list_view = newListView;

        pane.tab_switching = true;
        tabView.SelectedIndex(pane.active_tab_index);
        pane.tab_switching = false;

        UpdateTabCloseability();
        LoadDirectory(startPath);
    }

    void MainWindow::CloseTab(int32_t const index)
    {
        auto& pane = ActivePane();
        auto tabView = ActiveTabView();

        if (pane.tabs.size() <= 1 || index < 0 || index >= static_cast<int32_t>(pane.tabs.size()))
        {
            return;
        }

        pane.tab_switching = true;
        pane.tabs.erase(pane.tabs.begin() + index);
        pane.tab_rows.erase(pane.tab_rows.begin() + index);
        tabView.TabItems().RemoveAt(static_cast<uint32_t>(index));
        pane.tab_switching = false;

        // Clamp active index after removal.
        if (pane.active_tab_index >= static_cast<int32_t>(pane.tabs.size()))
        {
            pane.active_tab_index = static_cast<int32_t>(pane.tabs.size()) - 1;
        }

        tabView.SelectedIndex(pane.active_tab_index);
        // SelectionChanged fires here with tab_switching = false → RestoreActiveTab.

        UpdateTabCloseability();
    }

    void MainWindow::RestoreActiveTab()
    {
        auto& pane = ActivePane();
        auto tabView = ActiveTabView();

        if (pane.active_tab_index < 0 ||
            pane.active_tab_index >= static_cast<int32_t>(pane.tabs.size()))
        {
            return;
        }

        // Get the ListView for the newly active tab.
        auto const tabItemObj = tabView.TabItems().GetAt(static_cast<uint32_t>(pane.active_tab_index));
        auto const tabItem = tabItemObj.as<winrt::Microsoft::UI::Xaml::Controls::TabViewItem>();
        pane.active_list_view = tabItem.Content().as<winrt::Microsoft::UI::Xaml::Controls::ListView>();

        auto const& tab = pane.tabs[pane.active_tab_index];
        pane.current_path = tab.path;
        pane.is_drives_view = tab.is_drives_view;
        pane.items = tab.items;
        pane.selection = tab.selection;

        AddressTextBox().Text(tab.is_drives_view ? winrt::hstring{ L"This PC" }
                                                 : ToHString(pane.current_path.wstring()));

        if (pane.items.empty() && !tab.path.empty() && !tab.is_drives_view)
        {
            // Tab hasn't loaded yet — navigate to its path now.
            LoadDirectory(tab.path);
            return;
        }

        // Restore the cached rows and selection without re-scanning the disk.
        pane.tab_switching = true;
        pane.active_list_view.SelectedIndex(tab.selected_item_index);
        pane.tab_switching = false;

        if (tab.selected_item_index >= 0 &&
            tab.selected_item_index < static_cast<int32_t>(pane.items.size()))
        {
            SelectItemByIndex(tab.selected_item_index);
        }
        else
        {
            ResetSelectionDetails();
        }

        auto const statusSuffix = tab.is_drives_view
            ? std::wstring{ L" drive(s) available." }
            : L" shell-backed entries loaded from " + pane.current_path.wstring();
        StatusTextBlock().Text(ToHString(std::to_wstring(pane.items.size()) + statusSuffix));
    }

    void MainWindow::UpdateTabHeader(int32_t const index)
    {
        auto& pane = ActivePane();
        auto tabView = ActiveTabView();

        if (index < 0 || index >= static_cast<int32_t>(pane.tabs.size()))
        {
            return;
        }

        auto const tabItemObj = tabView.TabItems().GetAt(static_cast<uint32_t>(index));
        auto const tabItem = tabItemObj.as<winrt::Microsoft::UI::Xaml::Controls::TabViewItem>();
        tabItem.Header(winrt::box_value(ToHString(pane.tabs[index].display_name())));
    }

    void MainWindow::UpdateTabCloseability()
    {
        auto& pane = ActivePane();
        auto tabView = ActiveTabView();

        bool const canClose = pane.tabs.size() > 1;
        for (uint32_t i = 0; i < tabView.TabItems().Size(); ++i)
        {
            tabView.TabItems().GetAt(i)
                .as<winrt::Microsoft::UI::Xaml::Controls::TabViewItem>()
                .IsClosable(canClose);
        }
    }

    void MainWindow::InitPane(int32_t const paneIndex, std::filesystem::path const& startPath)
    {
        auto& pane = m_panes[paneIndex];
        auto tabView = TabViewForPane(paneIndex);

        // Guard against SelectionChanged firing during initial tab setup.
        pane.tab_switching = true;

        auto rows0 = winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>();
        pane.tab_rows.push_back(rows0);
        pane.tabs.push_back({});

        auto listView0 = CreateTabListView(rows0, paneIndex);
        pane.active_list_view = listView0;

        auto tabItem0 = winrt::Microsoft::UI::Xaml::Controls::TabViewItem{};
        tabItem0.Header(winrt::box_value(winrt::hstring{ L"New Tab" }));
        tabItem0.IsClosable(false);
        tabItem0.Content(listView0);
        tabView.TabItems().Append(tabItem0);
        tabView.SelectedIndex(0);

        pane.tab_switching = false;

        // Temporarily make this pane active to run LoadDirectory through it.
        auto const savedActivePane = m_activePaneIndex;
        m_activePaneIndex = paneIndex;
        LoadDirectory(startPath);
        m_activePaneIndex = savedActivePane;
    }

    winrt::Microsoft::UI::Xaml::Controls::TabView MainWindow::ActiveTabView()
    {
        return TabViewForPane(m_activePaneIndex);
    }

    winrt::Microsoft::UI::Xaml::Controls::TabView MainWindow::TabViewForPane(int32_t const paneIndex)
    {
        return paneIndex == 0 ? LeftPaneTabView() : RightPaneTabView();
    }

    int32_t MainWindow::PaneIndexForTabView(
        winrt::Microsoft::UI::Xaml::Controls::TabView const& tv)
    {
        return tv == LeftPaneTabView() ? 0 : 1;
    }

    void MainWindow::SetActivePaneIndex(int32_t const paneIndex)
    {
        if (m_activePaneIndex == paneIndex)
        {
            return;
        }

        m_activePaneIndex = paneIndex;
        UpdateActivePaneIndicator();
        AddressTextBox().Text(ActivePane().is_drives_view
            ? winrt::hstring{ L"This PC" }
            : ToHString(ActivePane().current_path.wstring()));
    }

    void MainWindow::UpdateActivePaneIndicator()
    {
        // Left pane: highlight when index 0 is active.
        auto const accentBrush = winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
            winrt::Windows::UI::Color{ 0xFF, 0x00, 0x78, 0xD4 });
        auto const transparentBrush = winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
            winrt::Windows::UI::Color{ 0x00, 0x00, 0x00, 0x00 });

        LeftPaneBorder().BorderBrush(m_activePaneIndex == 0 ? accentBrush : transparentBrush);
        RightPaneBorder().BorderBrush(m_activePaneIndex == 1 ? accentBrush : transparentBrush);
    }

    winrt::Microsoft::UI::Xaml::Controls::Border MainWindow::SortHeaderForPane(int32_t const paneIndex)
    {
        return paneIndex == 0 ? LeftPaneSortHeader() : RightPaneSortHeader();
    }

    void MainWindow::BuildSortHeader(int32_t const paneIndex)
    {
        struct ColumnDef { std::wstring_view label; SortColumn column; double widthStar; };
        static constexpr std::array<ColumnDef, 4> columns{{
            { L"Name",          SortColumn::Name,         3.0 },
            { L"Type",          SortColumn::Type,         1.8 },
            { L"Size",          SortColumn::Size,         1.2 },
            { L"Date Modified", SortColumn::DateModified, 2.0 },
        }};

        auto headerBorder = SortHeaderForPane(paneIndex);

        auto headerGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
        auto const& pane = m_panes[paneIndex];

        // In list mode, prepend a fixed-width column matching the row icon (20px + 8px spacing).
        // This keeps sort button labels approximately aligned with the list row data columns.
        int32_t colOffset = 0;
        if (pane.view_mode == ViewMode::List)
        {
            auto iconSpacerCol = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            iconSpacerCol.Width({ 28.0, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
            headerGrid.ColumnDefinitions().Append(iconSpacerCol);
            colOffset = 1;
        }

        // Four sort columns (star-proportioned).
        for (auto const& col : columns)
        {
            auto colDef = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            colDef.Width({ col.widthStar, winrt::Microsoft::UI::Xaml::GridUnitType::Star });
            headerGrid.ColumnDefinitions().Append(colDef);
        }

        // Trailing Auto column for the view-mode toggle buttons.
        auto toggleColDef = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
        toggleColDef.Width({ 0, winrt::Microsoft::UI::Xaml::GridUnitType::Auto });
        headerGrid.ColumnDefinitions().Append(toggleColDef);

        // Sort column buttons.
        for (int32_t colIdx = 0; colIdx < static_cast<int32_t>(columns.size()); ++colIdx)
        {
            auto const& colDef = columns[static_cast<std::size_t>(colIdx)];
            bool const isActive = (pane.sort_column == colDef.column);

            std::wstring labelText{ colDef.label };
            if (isActive)
            {
                labelText += (pane.sort_direction == SortDirection::Ascending) ? L"  \u2191" : L"  \u2193";
            }

            auto btn = winrt::Microsoft::UI::Xaml::Controls::Button{};
            btn.Content(winrt::box_value(winrt::hstring{ labelText }));
            btn.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
            btn.HorizontalContentAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Left);
            btn.Padding({ 6, 5, 6, 5 });
            btn.FontSize(12);
            btn.FontWeight(isActive
                ? winrt::Microsoft::UI::Text::FontWeights::SemiBold()
                : winrt::Microsoft::UI::Text::FontWeights::Normal());

            if (isActive)
            {
                btn.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                    winrt::Windows::UI::Color{ 0xFF, 0x00, 0x78, 0xD4 }));
            }

            try
            {
                auto const style = winrt::Microsoft::UI::Xaml::Application::Current()
                    .Resources()
                    .Lookup(winrt::box_value(winrt::hstring{ L"SubtleButtonStyle" }))
                    .as<winrt::Microsoft::UI::Xaml::Style>();
                btn.Style(style);
            }
            catch (...) {}

            auto const targetCol = colDef.column;
            btn.Click([this, paneIndex, targetCol](auto const&, auto const&)
                {
                    SetActivePaneIndex(paneIndex);
                    SetSortColumn(paneIndex, targetCol);
                });

            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(btn, colIdx + colOffset);
            headerGrid.Children().Append(btn);
        }

        // View-mode toggle buttons: "Cards" and "List".
        auto toggleStack = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
        toggleStack.Orientation(winrt::Microsoft::UI::Xaml::Controls::Orientation::Horizontal);
        toggleStack.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);

        auto addToggleBtn = [&](std::wstring_view label, ViewMode targetMode)
        {
            bool const isActive = (pane.view_mode == targetMode);
            auto btn = winrt::Microsoft::UI::Xaml::Controls::Button{};
            btn.Content(winrt::box_value(winrt::hstring{ label }));
            btn.Padding({ 8, 4, 8, 4 });
            btn.FontSize(12);
            btn.FontWeight(isActive
                ? winrt::Microsoft::UI::Text::FontWeights::SemiBold()
                : winrt::Microsoft::UI::Text::FontWeights::Normal());
            if (isActive)
            {
                btn.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                    winrt::Windows::UI::Color{ 0xFF, 0x00, 0x78, 0xD4 }));
            }
            try
            {
                auto const style = winrt::Microsoft::UI::Xaml::Application::Current()
                    .Resources()
                    .Lookup(winrt::box_value(winrt::hstring{ L"SubtleButtonStyle" }))
                    .as<winrt::Microsoft::UI::Xaml::Style>();
                btn.Style(style);
            }
            catch (...) {}
            auto const capturedMode = targetMode;
            btn.Click([this, paneIndex, capturedMode](auto const&, auto const&)
                {
                    SetActivePaneIndex(paneIndex);
                    SetViewMode(paneIndex, capturedMode);
                });
            toggleStack.Children().Append(btn);
        };

        addToggleBtn(L"Cards", ViewMode::Cards);
        addToggleBtn(L"List",  ViewMode::List);

        // Place the toggle stack in the trailing column.
        auto const toggleColIdx = static_cast<int32_t>(columns.size()) + colOffset;
        winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(toggleStack, toggleColIdx);
        headerGrid.Children().Append(toggleStack);

        headerBorder.Child(headerGrid);
    }

    void MainWindow::SetSortColumn(int32_t const paneIndex, SortColumn const column)
    {
        auto& pane = m_panes[paneIndex];

        if (pane.sort_column == column)
        {
            // Same column — flip direction.
            pane.sort_direction = (pane.sort_direction == SortDirection::Ascending)
                ? SortDirection::Descending
                : SortDirection::Ascending;
        }
        else
        {
            pane.sort_column = column;
            // Date and Size default to Descending when first selected (most-recent / largest first).
            pane.sort_direction = (column == SortColumn::DateModified || column == SortColumn::Size)
                ? SortDirection::Descending
                : SortDirection::Ascending;
        }

        BuildSortHeader(paneIndex);

        // Re-sort the current items and refresh the rows without re-enumerating disk.
        SortItems(pane.items, pane.sort_column, pane.sort_direction);

        // Also sync the active tab's cached items so RestoreActiveTab stays consistent.
        if (pane.active_tab_index >= 0 && pane.active_tab_index < static_cast<int32_t>(pane.tabs.size()))
        {
            pane.tabs[pane.active_tab_index].items = pane.items;
        }

        // Switch to this pane temporarily to call UpdateRows (which uses ActivePane()).
        auto const savedActive = m_activePaneIndex;
        m_activePaneIndex = paneIndex;
        UpdateRows(pane.items);
        m_activePaneIndex = savedActive;

        StatusTextBlock().Text(ToHString(
            std::to_wstring(pane.items.size()) + L" items  ·  sorted by "
            + [column]() -> std::wstring
              {
                  switch (column)
                  {
                  case SortColumn::Name:         return L"Name";
                  case SortColumn::Type:         return L"Type";
                  case SortColumn::Size:         return L"Size";
                  case SortColumn::DateModified: return L"Date Modified";
                  default:                       return L"Name";
                  }
              }()
            + (pane.sort_direction == SortDirection::Ascending ? L"  \u2191" : L"  \u2193")));
    }

    winrt::fire_and_forget MainWindow::LoadImagePreviewAsync(
        std::filesystem::path path,
        uint32_t const generation)
    {
        auto lifetime = get_strong();

        try
        {
            auto file = co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(
                winrt::hstring{ path.wstring() });

            // Abort if a newer selection has arrived while we were opening the file.
            if (m_previewGeneration != generation) { co_return; }

            auto stream = co_await file.OpenReadAsync();
            if (m_previewGeneration != generation) { co_return; }

            auto bmpImage = winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage{};
            co_await bmpImage.SetSourceAsync(stream);
            if (m_previewGeneration != generation) { co_return; }

            PreviewImage().Source(bmpImage);
            PreviewBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            SelectionIconBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }
        catch (...)
        {
            // If the load fails (access denied, corrupt image, etc.) leave the icon tile visible.
            if (m_previewGeneration == generation)
            {
                PreviewBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
                SelectionIconBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
        }
    }

    void MainWindow::SetViewMode(int32_t const paneIndex, ViewMode const mode)
    {
        auto& pane = m_panes[paneIndex];
        if (pane.view_mode == mode)
        {
            return;
        }

        pane.view_mode = mode;
        BuildSortHeader(paneIndex);
        RebuildAllTabRowsForPane(paneIndex);

        StatusTextBlock().Text(ToHString(
            std::to_wstring(pane.items.size()) +
            (mode == ViewMode::Cards ? L" items  ·  cards view" : L" items  ·  list view")));
    }

    void MainWindow::RebuildAllTabRowsForPane(int32_t const paneIndex)
    {
        auto& pane = m_panes[paneIndex];
        bool const listView = (pane.view_mode == ViewMode::List);

        // pane.tabs and pane.tab_rows are maintained in parallel and must be the same size.
        // Guard against divergence introduced by future refactoring.
        // Use (std::min)() to prevent expansion of the Windows.h min() macro.
        auto const tabCount = (std::min)(pane.tabs.size(), pane.tab_rows.size());
        for (std::size_t i = 0; i < tabCount; ++i)
        {
            // pane.items is the live, up-to-date vector for the active tab.
            // Background tabs use their cached copy.
            auto const& source = (static_cast<int32_t>(i) == pane.active_tab_index)
                ? pane.items
                : pane.tabs[i].items;

            auto& rows = pane.tab_rows[i];
            rows.Clear();
            for (auto const& item : source)
            {
                rows.Append(CreateRowView(item, listView));
            }
        }
    }

    void MainWindow::BuildSidebarFavorites()
    {
        auto const panel = SidebarPlacesPanel();
        panel.Children().Clear();

        for (auto const& entry : m_favoritesService.GetBuiltinFavorites())
        {
            auto btn = winrt::Microsoft::UI::Xaml::Controls::Button{};
            btn.Content(winrt::box_value(ToHString(entry.display_name)));
            btn.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
            btn.HorizontalContentAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Left);
            btn.Padding({ 8, 6, 8, 6 });


            if (entry.is_drives_view)
            {
                btn.Click([this](auto const&, auto const&) { LoadDrivesView(); });
            }
            else if (entry.is_recycle_bin)
            {
                btn.Click([this](auto const&, auto const&)
                {
                    ShellExecuteW(GetWindowHandle(), L"open",
                        L"shell:RecycleBinFolder", nullptr, nullptr, SW_SHOW);
                    StatusTextBlock().Text(L"Opened Recycle Bin in Explorer.");
                });
            }
            else if (!entry.path.empty())
            {
                auto const targetPath = entry.path;
                btn.Click([this, targetPath](auto const&, auto const&)
                {
                    LoadDirectory(targetPath);
                });
            }

            panel.Children().Append(btn);
        }
    }

    void MainWindow::LoadDrivesView()
    {
        try
        {
            auto items = m_shellInterop.EnumerateDrives();
            // Drives lack size/date, so sort degenerates to name order for those columns —
            // but apply the active sort to stay consistent with the current pane preference.
            SortItems(items, ActivePane().sort_column, ActivePane().sort_direction);

            auto& pane = ActivePane();
            pane.current_path = std::filesystem::path{};
            pane.is_drives_view = true;
            pane.items = items;
            pane.selection.Clear();

            pane.tabs[pane.active_tab_index].path = std::filesystem::path{};
            pane.tabs[pane.active_tab_index].items = items;
            pane.tabs[pane.active_tab_index].selection = {};
            pane.tabs[pane.active_tab_index].selected_item_index = -1;
            pane.tabs[pane.active_tab_index].is_drives_view = true;
            UpdateTabHeader(pane.active_tab_index);

            AddressTextBox().Text(L"This PC");
            UpdateRows(items);

            if (pane.active_list_view)
            {
                pane.tab_switching = true;
                pane.active_list_view.SelectedIndex(-1);
                pane.tab_switching = false;
            }

            ResetSelectionDetails();

            StatusTextBlock().Text(
                ToHString(std::to_wstring(items.size()) + L" drive(s) available."));
        }
        catch (...)
        {
            StatusTextBlock().Text(L"Drive enumeration failed.");
        }
    }
}
