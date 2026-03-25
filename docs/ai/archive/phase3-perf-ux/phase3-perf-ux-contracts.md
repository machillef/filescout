# Contracts: phase3-perf-ux

## FileRowViewModel (new contract — P1)

```idl
runtimeclass FileRowViewModel : Microsoft.UI.Xaml.Data.INotifyPropertyChanged
{
    FileRowViewModel();
    String Name { get; };           // Display name (no path)
    String TypeLabel { get; };      // e.g. "Folder", "PDF Document"
    String DisplaySize { get; };    // e.g. "1.2 MB", "—" for folders
    String DisplayDate { get; };    // e.g. "3/24/2026 2:15 PM"
    Boolean IsFolder { get; };
    Microsoft.UI.Xaml.Media.Imaging.BitmapSource IconSource { get; set; };
    // Setting IconSource fires PropertyChanged("IconSource")
}
```

Consumers: DataTemplates (P2), `UpdateRows` (P3), `BackfillIconsAsync` (P4).

## tab_rows type contract (changed in P3)

Before: `IObservableVector<IInspectable>` — items are pre-built XAML `Border` objects.
After: `IObservableVector<IInspectable>` — items are `FileRowViewModel` instances (or group separators).

ListView `ItemsSource` binds to `tab_rows`; DataTemplate interprets items as `FileRowViewModel`.
Group separators (if any) require special handling — see risks.md R3.

## ShellInteropService::GetItemIcon (unchanged)

```cpp
IconBitmap GetItemIcon(std::filesystem::path const& path, bool isFolder);
```

Thread-safe (protected by `m_cacheMutex`). Called from `BackfillIconsAsync` background section.
Returns `IconBitmap { width, height, std::vector<uint8_t> rgba }`.

Consumers: `BackfillIconsAsync` (P4) — converts `IconBitmap` → `BitmapSource` on UI thread.

## FavoritesService (extended in S4)

Existing: pin/unpin favorites, persist to file.
Added in S4:
```cpp
void Pin(std::filesystem::path const& path);
void Unpin(std::filesystem::path const& path);
std::vector<std::filesystem::path> GetAll() const;
```

## Breadcrumb visual contract (U1)

Before: `FontSize=12`, `MinHeight=38`, `Padding="{4,0,4,0}"`
After:  `FontSize=14`, `MinHeight=44`, `Padding="{6,0,6,0}"`

Breadcrumb segments remain `HyperlinkButton` controls in the same `BreadcrumbBar`-style layout.
No structural change — visual only.
