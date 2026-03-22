# FileScout Chrome UX Redesign Plan (R18)

## Context

Manual smoke-testing of R17 fuzzy search revealed five UX problems:

1. **Search bar confusion** — Filter vs Recursive mode is opaque; users don't know what state
   they're in. Root cause: search hijacks the address bar, so there's no visual separation
   between "navigate to a path" and "search".
2. **No dedicated search box** — Windows Explorer separates navigation (left) from search
   (right). FileScout uses a single TextBox for both, which is confusing.
3. **Drive icons show a folder icon** — Bug: `GetIconForItem` uses the generic folder icon
   for drive roots because `SHGFI_USEFILEATTRIBUTES` ignores the actual path.
4. **No breadcrumb navigation** — Clicking the address bar does nothing interactive; in
   Windows Explorer you can click any segment to navigate up the tree.
5. **No keyboard type-ahead** — Pressing a letter in the file list should jump to the first
   item whose name starts with that letter.

## Target Chrome (after R18)

```
┌──────────────────────────────────────────────────────────────────────────┐
│ FileScout  ALPHA     ← → ↑   > Documents > Projects > filescout   [Search filescout… 🔍] │
└──────────────────────────────────────────────────────────────────────────┘
```

- **Left of toolbar**: Back, Forward, Refresh buttons (unchanged)
- **Centre**: Breadcrumb bar — `> segment > segment > …` as clickable buttons.
  Clicking a segment navigates to that path. Pressing F4 or Alt+D swaps it for a
  TextBox for direct path entry (same Enter/Escape semantics as today).
- **Right**: Dedicated `SearchTextBox` — always visible, placeholder `Search <folder>…`.
  Ctrl+F focuses it. Typing ≥ 2 chars triggers Filter mode. Enter escalates to Recursive
  mode. Escape clears and restores. This replaces the current Ctrl+F-hijacks-address-bar
  approach entirely.

## Slices

### R18a — Drive icon bug fix *(quick, no spec needed)*

**Root cause:** `GetIconForItem` sets `lookupName = L"folder"` for all `isFolder = true`
items, then calls `SHGetFileInfoW` with `SHGFI_USEFILEATTRIBUTES`. With that flag, the
shell ignores the actual path and returns the generic folder icon for anything marked as a
directory. Drive roots (`C:\`) therefore get the same folder icon as `C:\Users\`.

**Fix:** In `GetIconForItem`, detect drive roots (`path == path.root_path()` and
`!path.root_name().empty()`). For these, call `SHGetFileInfoW` without
`SHGFI_USEFILEATTRIBUTES` so the shell looks up the actual drive icon.
Implement via a second overload of `ExtractShellIconBitmap` (or an extra `bool` param)
that omits the flag.

**Files:** `Shell/ShellInteropService.cpp` only.

---

### R18b — Dedicated search box + breadcrumb navigation

**Files:**
- `MainWindow.xaml` — add `SearchTextBox`, add `BreadcrumbPanel`, remove `AddressTextBox`
  search wiring (keep `AddressTextBox` hidden for F4 path entry)
- `MainWindow.xaml.h` — add `UpdateBreadcrumbs()` declaration; remove `AddressTextBox_TextChanged`
  from search duty; add `SearchTextBox_TextChanged` / `SearchTextBox_KeyDown`
- `MainWindow.xaml.cpp` — implement breadcrumb builder, wire search to `SearchTextBox`,
  move R17 search logic out of `AddressTextBox_*` into `SearchTextBox_*`

#### Step B.1 — XAML: add SearchTextBox and BreadcrumbPanel

Replace the toolbar row (inner Grid Row 1) from:
```xml
<Grid Grid.Row="1" ColumnSpacing="10">
    <Grid.ColumnDefinitions>
        <ColumnDefinition Width="Auto" />
        <ColumnDefinition Width="*" />
    </Grid.ColumnDefinitions>
    <StackPanel [buttons]>
    <TextBox x:Name="AddressTextBox" ... TextChanged="..." KeyDown="..." />
</Grid>
```

With:
```xml
<Grid Grid.Row="1" ColumnSpacing="10">
    <Grid.ColumnDefinitions>
        <ColumnDefinition Width="Auto" />   <!-- nav buttons -->
        <ColumnDefinition Width="*" />      <!-- breadcrumb + address overlay -->
        <ColumnDefinition Width="220" />    <!-- search box -->
    </Grid.ColumnDefinitions>

    <StackPanel Orientation="Horizontal" Spacing="6" VerticalAlignment="Center">
        [Back, Forward, Refresh — unchanged]
    </StackPanel>

    <!-- Address overlay: breadcrumb bar (default) with hidden TextBox for F4 path entry -->
    <Grid Grid.Column="1">
        <!-- Breadcrumb bar — shown by default -->
        <Border x:Name="BreadcrumbBarBorder"
                Style="{StaticResource ChromeAddressTextBoxStyle}"
                Padding="8,0,8,0"
                Tapped="BreadcrumbBarBorder_Tapped">
            <ScrollViewer HorizontalScrollBarVisibility="Hidden"
                          VerticalScrollBarVisibility="Disabled"
                          VerticalAlignment="Center">
                <StackPanel x:Name="BreadcrumbPanel"
                            Orientation="Horizontal"
                            Spacing="0"
                            VerticalAlignment="Center" />
            </ScrollViewer>
        </Border>
        <!-- Address TextBox — hidden; shown on F4/Alt+D or breadcrumb tap -->
        <TextBox x:Name="AddressTextBox"
                 Style="{StaticResource ChromeAddressTextBoxStyle}"
                 PlaceholderText="Enter a filesystem path"
                 Visibility="Collapsed"
                 KeyDown="AddressTextBox_KeyDown" />
    </Grid>

    <!-- Dedicated search box -->
    <TextBox x:Name="SearchTextBox"
             Grid.Column="2"
             Style="{StaticResource ChromeAddressTextBoxStyle}"
             PlaceholderText="Search…"
             KeyDown="SearchTextBox_KeyDown"
             TextChanged="SearchTextBox_TextChanged" />
</Grid>
```

Remove `TextChanged="AddressTextBox_TextChanged"` from `AddressTextBox` (it now only
handles path entry; search is driven by `SearchTextBox`).

#### Step B.2 — Header: new handlers + helpers

Add to `MainWindow.xaml.h` public section:
```cpp
void BreadcrumbBarBorder_Tapped(
    winrt::Windows::Foundation::IInspectable const&,
    winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const&);
void SearchTextBox_KeyDown(
    winrt::Windows::Foundation::IInspectable const&,
    winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
void SearchTextBox_TextChanged(
    winrt::Windows::Foundation::IInspectable const&,
    winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
```

Add to private section:
```cpp
void UpdateBreadcrumbs(std::filesystem::path const& path);
void ShowAddressTextBox();
void HideAddressTextBox();
```

Remove `AddressTextBox_TextChanged` from the public handler list (delete declaration).

#### Step B.3 — Source: implement breadcrumb builder

```cpp
void MainWindow::UpdateBreadcrumbs(std::filesystem::path const& path)
{
    auto const panel = BreadcrumbPanel();
    panel.Children().Clear();

    // Build list of ancestors root → current.
    std::vector<std::filesystem::path> segments;
    for (auto p = path; !p.empty() && p != p.parent_path(); p = p.parent_path())
        segments.insert(segments.begin(), p);
    if (segments.empty()) segments.push_back(path);

    for (std::size_t i = 0; i < segments.size(); ++i)
    {
        auto const& seg = segments[i];
        std::wstring label = (i == 0)
            ? seg.root_name().wstring()       // "C:"
            : seg.filename().wstring();        // "Documents"
        if (label.empty()) label = seg.wstring();

        // Separator " › " between segments.
        if (i > 0)
        {
            winrt::Microsoft::UI::Xaml::Controls::TextBlock sep;
            sep.Text(L" › ");
            sep.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
            sep.Opacity(0.45);
            sep.FontSize(12);
            panel.Children().Append(sep);
        }

        winrt::Microsoft::UI::Xaml::Controls::HyperlinkButton btn;
        winrt::Microsoft::UI::Xaml::Controls::TextBlock lbl;
        lbl.Text(winrt::hstring(label));
        lbl.FontSize(12);
        lbl.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
        btn.Content(lbl);
        btn.Padding({ 4, 0, 4, 0 });

        auto const targetPath = seg; // capture for lambda
        btn.Click([this, targetPath](auto const&, auto const&)
        {
            LoadDirectory(targetPath);
        });

        panel.Children().Append(btn);
    }
}
```

Call `UpdateBreadcrumbs(path)` at the end of `LoadDirectory` (after starting the async load)
and in `LoadDrivesView` (pass the root path or a sentinel "This PC" label).

#### Step B.4 — Source: show/hide address TextBox

```cpp
void MainWindow::ShowAddressTextBox()
{
    BreadcrumbBarBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
    AddressTextBox().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
    AddressTextBox().Text(ToHString(ActivePane().current_path.wstring()));
    AddressTextBox().SelectAll();
    AddressTextBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
}

void MainWindow::HideAddressTextBox()
{
    AddressTextBox().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
    BreadcrumbBarBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
}
```

Wire `BreadcrumbBarBorder_Tapped` → `ShowAddressTextBox()`.

In `AddressTextBox_KeyDown`: on Enter (navigate) → call `HideAddressTextBox()` after
`LoadDirectory`. On Escape → call `HideAddressTextBox()` without navigating.

Add F4 accelerator in `SetupKeyboardAccelerators` → `ShowAddressTextBox()`.

#### Step B.5 — Source: move search logic to SearchTextBox

Move search handler logic:
- `AddressTextBox_TextChanged` body → new `SearchTextBox_TextChanged` (same logic,
  references `SearchTextBox()` instead of `AddressTextBox()`)
- R17 Enter→Recursive block from `AddressTextBox_KeyDown` → new `SearchTextBox_KeyDown`
- R17 Escape block from `AddressTextBox_KeyDown` → `SearchTextBox_KeyDown`

Delete `AddressTextBox_TextChanged` handler (declaration + definition).

Update `SearchAccelerator_Invoked` (Ctrl+F) to focus `SearchTextBox()` instead of
`AddressTextBox()`.

Update all references to `AddressTextBox()` that set placeholder/IsReadOnly for search
mode → `SearchTextBox()` instead.

Update `SearchTextBox.PlaceholderText` whenever path changes:
```cpp
auto const folderName = path.filename().empty()
    ? path.wstring() : path.filename().wstring();
SearchTextBox().PlaceholderText(L"Search " + folderName + L"\u2026");
```

#### Step B.6 — Build + test

Build and run 71-test suite. Verify no regressions.

#### Step B.7 — Commit

```
feat(chrome): add breadcrumb nav bar + dedicated search box (R18b)
```

---

### R18c — Keyboard type-ahead in file list

**Files:** `MainWindow.xaml.cpp` only (add KeyDown handler on ListView).

When the file list has keyboard focus and the user presses a printable character, jump to
the first visible item whose `display_name` starts with that character (case-insensitive).

#### Step C.1 — Wire ListView KeyDown

In `CreateTabListView`, add a `KeyDown` handler:
```cpp
lv.KeyDown([this](auto const&, auto const& args)
{
    FileRowsListView_KeyDown(args);
});
```

#### Step C.2 — Implement type-ahead

```cpp
void MainWindow::FileRowsListView_KeyDown(
    winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
{
    auto const vk = static_cast<int32_t>(args.Key());
    // Printable ASCII A-Z (VK codes 65-90).
    if (vk < 65 || vk > 90) return;

    wchar_t const ch = static_cast<wchar_t>(std::tolower(vk));
    auto& pane = ActivePane();

    // In search mode, let the search box handle keys.
    if (pane.search_mode != SearchMode::None) return;

    auto const& items = pane.items;
    if (items.empty()) return;

    // Find next item starting with ch, wrapping from current selection.
    int32_t const start = (pane.selection.primary_index >= 0)
        ? (pane.selection.primary_index + 1) % static_cast<int32_t>(items.size())
        : 0;

    for (std::size_t i = 0; i < items.size(); ++i)
    {
        auto const idx = static_cast<int32_t>((start + i) % items.size());
        if (!items[static_cast<std::size_t>(idx)].display_name.empty() &&
            std::towlower(items[static_cast<std::size_t>(idx)].display_name[0]) == ch)
        {
            SelectItemByIndex(idx);
            if (pane.active_list_view)
                pane.active_list_view.ScrollIntoView(
                    pane.active_list_view.Items().GetAt(static_cast<uint32_t>(idx)));
            args.Handled(true);
            return;
        }
    }
}
```

Add `FileRowsListView_KeyDown` as a private method in `MainWindow.xaml.h`.

#### Step C.3 — Build + test + commit

```
feat(list): keyboard type-ahead jumps to first item by letter (R18c)
```

---

## Deferred

- Back/Forward navigation history (breadcrumb arrows remain disabled for now)
- "Up" button in toolbar
- Performance: async icon loading (separate perf initiative)
