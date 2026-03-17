#include "pch.h"
#include "MainWindow.xaml.h"

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

    winrt::Microsoft::UI::Xaml::Media::ImageSource BuildIconSource(
        ::FileScoutNative::Domain::IconBitmap const& icon)
    {
        if (icon.rgba.empty() || icon.width == 0 || icon.height == 0)
        {
            return nullptr;
        }

        auto bitmap = winrt::Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(
            winrt::Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(icon.rgba),
            winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Rgba8,
            static_cast<int32_t>(icon.width),
            static_cast<int32_t>(icon.height),
            winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Straight);

        auto source = winrt::Microsoft::UI::Xaml::Media::Imaging::SoftwareBitmapSource{};
        source.SetBitmapAsync(bitmap).get();
        return source;
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

    winrt::Microsoft::UI::Xaml::FrameworkElement CreateRowView(::FileScoutNative::Domain::ShellItemRef const& item)
    {
        std::wstring details = item.kind_label;
        if (item.size_available)
        {
            details += L"  |  " + FormatSize(item.size_bytes);
        }

        auto rowBorder = winrt::Microsoft::UI::Xaml::Controls::Border{};
        rowBorder.Padding({ 14, 12, 14, 12 });
        rowBorder.CornerRadius({ 12, 12, 12, 12 });
        rowBorder.BorderThickness({ 1, 1, 1, 1 });
        rowBorder.BorderBrush(LookupBrush(L"CardStrokeColorDefaultBrush"));
        rowBorder.Background(LookupBrush(L"CardBackgroundFillColorSecondaryBrush"));

        auto rowGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
        auto iconColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
        iconColumn.Width({ 52, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
        auto contentColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
        contentColumn.Width({ 1, winrt::Microsoft::UI::Xaml::GridUnitType::Star });
        rowGrid.ColumnDefinitions().Append(iconColumn);
        rowGrid.ColumnDefinitions().Append(contentColumn);
        rowGrid.ColumnSpacing(12);

        auto iconTile = winrt::Microsoft::UI::Xaml::Controls::Border{};
        iconTile.Width(40);
        iconTile.Height(40);
        iconTile.CornerRadius({ 14, 14, 14, 14 });
        iconTile.Background(item.is_folder
                ? LookupBrush(L"AccentFillColorSecondaryBrush")
                : LookupBrush(L"SubtleFillColorSecondaryBrush"));

        auto iconImage = winrt::Microsoft::UI::Xaml::Controls::Image{};
        iconImage.Width(20);
        iconImage.Height(20);
        iconImage.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
        iconImage.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
        iconImage.Stretch(winrt::Microsoft::UI::Xaml::Media::Stretch::Uniform);
        iconImage.Source(BuildIconSource(item.icon));
        iconTile.Child(iconImage);

        auto contentStack = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
        contentStack.Spacing(4);

        auto titleText = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        titleText.Text(ToHString(item.display_name));
        titleText.FontSize(15);
        titleText.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        titleText.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

        auto detailText = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        detailText.Text(ToHString(details));
        detailText.Opacity(0.8);
        detailText.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

        auto pathText = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        pathText.Text(ToHString(item.path.wstring()));
        pathText.Opacity(0.58);
        pathText.FontSize(12);
        pathText.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

        contentStack.Children().Append(titleText);
        contentStack.Children().Append(detailText);
        contentStack.Children().Append(pathText);

        rowGrid.Children().Append(iconTile);
        rowGrid.Children().Append(contentStack);
        winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(contentStack, 1);

        rowBorder.Child(rowGrid);
        return rowBorder;
    }
}

namespace winrt::FileScoutNative::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        ConfigureWindow();
        FileRowsListView().ItemsSource(m_rows);
        LoadDirectory(std::filesystem::current_path());
    }

    void MainWindow::RefreshButton_Click(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        LoadDirectory(m_currentPath);
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
        SelectItemByIndex(FileRowsListView().SelectedIndex());
    }

    void MainWindow::FileRowsListView_DoubleTapped(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args)
    {
        args.Handled(true);

        const auto index = FileRowsListView().SelectedIndex();
        if (index < 0 || index >= static_cast<int32_t>(m_items.size()))
        {
            return;
        }

        auto const& item = m_items[static_cast<std::size_t>(index)];
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
        if (itemIndex.has_value())
        {
            FileRowsListView().SelectedIndex(*itemIndex);
            SelectItemByIndex(*itemIndex);
        }
        else
        {
            FileRowsListView().SelectedIndex(-1);
            SelectItemByIndex(-1);
        }

        ::FileScoutNative::Shell::ContextMenuScenario scenario;
        if (itemIndex.has_value())
        {
            scenario.focused_item = m_items[static_cast<std::size_t>(*itemIndex)];
        }

        ShowContextMenu(
            m_contextMenuService.BuildMenu(scenario),
            itemIndex,
            FileRowsListView());
        args.Handled(true);
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

        const auto hwnd = GetWindowHandle();
        const UINT dpi = GetDpiForWindow(hwnd);
        const float scale = static_cast<float>(dpi) / 96.0f;
        const int width = static_cast<int>(1520 * scale);
        const int height = static_cast<int>(920 * scale);

        SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
    }

    void MainWindow::LoadDirectory(std::filesystem::path const& path)
    {
        try
        {
            const auto absolute = std::filesystem::absolute(path);
            auto items = m_shellInterop.EnumerateDirectory(absolute);

            m_currentPath = absolute;
            m_items = items;
            m_selection.Clear();
            AddressTextBox().Text(ToHString(absolute.wstring()));
            UpdateRows(items);
            FileRowsListView().SelectedIndex(-1);
            ResetSelectionDetails();

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
        m_rows.Clear();

        for (auto const& item : items)
        {
            m_rows.Append(CreateRowView(item));
        }
    }

    void MainWindow::SelectItemByIndex(int32_t const index)
    {
        if (index < 0 || index >= static_cast<int32_t>(m_items.size()))
        {
            m_selection.Clear();
            ResetSelectionDetails();
            return;
        }

        auto const& item = m_items[static_cast<std::size_t>(index)];
        m_selection.ReplaceWith(item.parsing_name);
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
        SelectionIconImage().Source(nullptr);
        SelectionTitleTextBlock().Text(L"No item selected");
        SelectionMetaTextBlock().Text(L"Pick a folder or file to inspect its shell-backed details.");
        SelectionPathTextBlock().Text(L"Selection path will appear here.");
    }

    void MainWindow::UpdateSelectionDetails(::FileScoutNative::Domain::ShellItemRef const& item)
    {
        SelectionIconImage().Source(BuildIconSource(item.icon));
        SelectionTitleTextBlock().Text(ToHString(item.display_name));

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
        SelectionPathTextBlock().Text(ToHString(item.path.wstring()));
    }

    std::optional<int32_t> MainWindow::ResolveItemIndexFromSource(
        winrt::Windows::Foundation::IInspectable const& source)
    {
        auto dependencyObject = source.try_as<winrt::Microsoft::UI::Xaml::DependencyObject>();
        while (dependencyObject)
        {
            auto const itemContainer = dependencyObject.try_as<winrt::Microsoft::UI::Xaml::Controls::ListViewItem>();
            if (itemContainer)
            {
                const auto index = FileRowsListView().IndexFromContainer(itemContainer);
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
        if (itemIndex.has_value() && *itemIndex >= 0 && *itemIndex < static_cast<int32_t>(m_items.size()))
        {
            item = m_items[static_cast<std::size_t>(*itemIndex)];
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
            LoadDirectory(m_currentPath);
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
            StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Rename is queued for R4", item)));
            return;
        }

        if (commandId == L"delete")
        {
            StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Delete is queued for R4", item)));
            return;
        }

        if (commandId == L"copy")
        {
            StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Copy is queued for R4", item)));
            return;
        }

        if (commandId == L"cut")
        {
            StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Cut is queued for R4", item)));
            return;
        }

        if (commandId == L"paste")
        {
            StatusTextBlock().Text(
                ToHString(L"Paste is queued for R4 into " + m_currentPath.wstring()));
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
                    LoadDirectory(m_currentPath);
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
            StatusTextBlock().Text(
                ToHString(L"New folder is queued for R4 in " + m_currentPath.wstring()));
            return;
        }
    }
}
