#include "pch.h"
// IRandomAccessStreamWithContentType return type for co_await file.OpenReadAsync().
// Included here rather than pch.h to avoid PCH bloat across all TUs.
#include <cmath>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Storage.Streams.h>
#include <robuffer.h>   // IBufferByteAccess for WriteableBitmap pixel access
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "Domain/AddressNavigation.h"
#include "Domain/DragDropPlanning.h"
#include "Domain/DriveCapacity.h"
#include "Domain/FileNameValidation.h"
#include "Domain/NavigationInput.h"
#include "Domain/RowIconState.h"
#include "Domain/ShellItemSort.h"
#include "Domain/SidebarRecents.h"
#include "Domain/ThemeService.h"
#include "Domain/Wave1FeatureGate.h"
#include "Search/SearchFilter.h"
#include "Search/SearchPresentation.h"
#include "TraceHelper.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

namespace
{
    using SortColumn = winrt::FileScoutNative::implementation::SortColumn;

    std::wstring BuildGroupLabel(
        ::FileScoutNative::Domain::ShellItemRef const& item,
        SortColumn const sortColumn);

    winrt::hstring ToHString(std::wstring const& value)
    {
        return winrt::hstring{ value };
    }

    winrt::Windows::UI::Color ColorFromArgb(std::uint32_t const argb)
    {
        return winrt::Windows::UI::Color{
            static_cast<std::uint8_t>((argb >> 24) & 0xFF),
            static_cast<std::uint8_t>((argb >> 16) & 0xFF),
            static_cast<std::uint8_t>((argb >> 8) & 0xFF),
            static_cast<std::uint8_t>(argb & 0xFF),
        };
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

    winrt::Microsoft::UI::Xaml::Controls::FontIcon BuildThemeSwatchIcon(
        std::uint32_t const argb,
        double const fontSize = 12.0)
    {
        auto icon = winrt::Microsoft::UI::Xaml::Controls::FontIcon{};
        icon.Glyph(L"\u25CF");
        icon.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe UI Symbol"));
        icon.FontSize(fontSize);
        icon.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(ColorFromArgb(argb)));
        icon.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
        icon.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
        return icon;
    }

    winrt::Microsoft::UI::Xaml::Controls::StackPanel BuildThemeButtonContent(
        ::FileScoutNative::Domain::ThemeDefinition const& theme)
    {
        auto panel = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
        panel.Orientation(winrt::Microsoft::UI::Xaml::Controls::Orientation::Horizontal);
        panel.Spacing(8);
        panel.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);

        auto icon = BuildThemeSwatchIcon(theme.accent, 12.0);

        auto label = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        label.Text(ToHString(theme.display_name));
        label.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        label.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
        label.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

        panel.Children().Append(icon);
        panel.Children().Append(label);
        return panel;
    }

    bool IsLocalDrivePath(std::filesystem::path const& path)
    {
        if (path.empty())
        {
            return false;
        }

        auto const native = path.native();
        if (native.rfind(L"\\\\", 0) == 0 || native.rfind(L"//", 0) == 0)
        {
            return false;
        }

        auto const root = path.root_path();
        if (root.empty())
        {
            return false;
        }

        switch (GetDriveTypeW(root.c_str()))
        {
        case DRIVE_FIXED:
        case DRIVE_REMOVABLE:
        case DRIVE_CDROM:
        case DRIVE_RAMDISK:
            return true;
        default:
            return false;
        }
    }

    bool IsTextEditingCommand(std::wstring_view const commandId)
    {
        return commandId == L"selectAll" ||
            commandId == L"copy" ||
            commandId == L"cut" ||
            commandId == L"paste" ||
            commandId == L"delete";
    }

    bool HasSystemModifierChord()
    {
        return GetKeyState(VK_CONTROL) < 0 ||
            GetKeyState(VK_MENU) < 0 ||
            GetKeyState(VK_LWIN) < 0 ||
            GetKeyState(VK_RWIN) < 0;
    }

    winrt::hstring DragOriginalPathsFormat()
    {
        static const winrt::hstring format{ L"FileScoutOriginalPaths" };
        return format;
    }

    constexpr wchar_t DragOriginalPathsSeparator = 0x1F;

    std::wstring CreateDragStagingToken()
    {
        GUID guid{};
        winrt::check_hresult(CoCreateGuid(&guid));

        wchar_t buffer[64]{};
        auto const length = StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
        if (length <= 1)
        {
            return L"drag-item";
        }

        std::wstring token{ buffer, buffer + length - 1 };
        token.erase(std::remove(token.begin(), token.end(), L'{'), token.end());
        token.erase(std::remove(token.begin(), token.end(), L'}'), token.end());
        return token;
    }

    std::wstring EncodeDragOriginalPaths(
        std::span<::FileScoutNative::Domain::ShellItemRef const> const items)
    {
        std::wstring payload;
        bool first = true;
        for (auto const& item : items)
        {
            if (!first)
            {
                payload.push_back(DragOriginalPathsSeparator);
            }

            payload += item.path.wstring();
            first = false;
        }

        return payload;
    }

    std::vector<std::filesystem::path> DecodeDragOriginalPaths(std::wstring_view const payload)
    {
        std::vector<std::filesystem::path> paths;
        std::size_t start = 0;
        while (start <= payload.size())
        {
            auto const separator = payload.find(DragOriginalPathsSeparator, start);
            auto const segmentEnd =
                separator == std::wstring_view::npos ? payload.size() : separator;
            if (segmentEnd > start)
            {
                paths.emplace_back(payload.substr(start, segmentEnd - start));
            }

            if (separator == std::wstring_view::npos)
            {
                break;
            }

            start = separator + 1;
        }

        return paths;
    }

    std::filesystem::path StageDragFileForExternalTargets(std::filesystem::path const& sourcePath)
    {
        auto stageRoot = std::filesystem::temp_directory_path() /
            L"FileScout" /
            L"DragStage" /
            CreateDragStagingToken();
        std::filesystem::create_directories(stageRoot);

        auto stagedPath = stageRoot / sourcePath.filename();
        if (std::filesystem::exists(stagedPath))
        {
            stagedPath = stageRoot /
                (sourcePath.stem().wstring() + L"-" + CreateDragStagingToken() + sourcePath.extension().wstring());
        }

        std::filesystem::copy_file(
            sourcePath,
            stagedPath,
            std::filesystem::copy_options::overwrite_existing);
        return stagedPath;
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Storage::IStorageItem> CreateDragStorageItemAsync(
        ::FileScoutNative::Domain::ShellItemRef const& item)
    {
        if (::FileScoutNative::Domain::ShouldStageExternalDragFile(
                item.path,
                item.is_folder,
                IsLocalDrivePath))
        {
            co_await winrt::resume_background();
            auto const stagedPath = StageDragFileForExternalTargets(item.path);
            co_return co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(
                winrt::hstring{ stagedPath.wstring() });
        }

        auto const itemPath = winrt::hstring{ item.path.wstring() };
        try
        {
            if (item.is_folder)
            {
                co_return co_await winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(itemPath);
            }

            co_return co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(itemPath);
        }
        catch (...)
        {
        }

        if (item.is_folder)
        {
            throw;
        }

        co_await winrt::resume_background();
        auto const stagedPath = StageDragFileForExternalTargets(item.path);
        co_return co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(
            winrt::hstring{ stagedPath.wstring() });
    }

    winrt::fire_and_forget ProvideDragStorageItemsAsync(
        std::vector<::FileScoutNative::Domain::ShellItemRef> items,
        winrt::Windows::ApplicationModel::DataTransfer::DataProviderRequest request)
    {
        auto const uiThread = winrt::apartment_context();
        auto const deferral = request.GetDeferral();
        auto storageItems = winrt::single_threaded_vector<winrt::Windows::Storage::IStorageItem>();
        bool failed = false;

        try
        {
            for (auto const& item : items)
            {
                storageItems.Append(co_await CreateDragStorageItemAsync(item));
            }
        }
        catch (...)
        {
            failed = true;
        }

        co_await uiThread;
        if (failed)
        {
            auto emptyItems = winrt::single_threaded_vector<winrt::Windows::Storage::IStorageItem>();
            request.SetData(emptyItems.GetView().as<winrt::Windows::Foundation::IInspectable>());
        }
        else
        {
            request.SetData(storageItems.GetView().as<winrt::Windows::Foundation::IInspectable>());
        }

        deferral.Complete();
    }

    bool FocusedElementLivesInsideEditableControl(
        winrt::Windows::Foundation::IInspectable const& focusedElement)
    {
        if (!focusedElement)
        {
            return false;
        }

        if (focusedElement.try_as<winrt::Microsoft::UI::Xaml::Controls::TextBox>() != nullptr ||
            focusedElement.try_as<winrt::Microsoft::UI::Xaml::Controls::RichEditBox>() != nullptr)
        {
            return true;
        }

        auto dependencyObject = focusedElement.try_as<winrt::Microsoft::UI::Xaml::DependencyObject>();
        while (dependencyObject)
        {
            if (dependencyObject.try_as<winrt::Microsoft::UI::Xaml::Controls::TextBox>() != nullptr ||
                dependencyObject.try_as<winrt::Microsoft::UI::Xaml::Controls::RichEditBox>() != nullptr)
            {
                return true;
            }

            dependencyObject =
                winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(dependencyObject);
        }

        return false;
    }

    winrt::FileScoutNative::implementation::App* CurrentAppImplementation()
    {
        return winrt::FileScoutNative::implementation::App::Current();
    }

    std::optional<winrt::Windows::Graphics::RectInt32> TryGetElementRect(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
        double const scale)
    {
        if (!element || element.Visibility() != winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            return std::nullopt;
        }

        auto const width = element.ActualWidth();
        auto const height = element.ActualHeight();
        if (width <= 0.0 || height <= 0.0)
        {
            return std::nullopt;
        }

        auto const transform = element.TransformToVisual(nullptr);
        auto const bounds = transform.TransformBounds(
            winrt::Windows::Foundation::Rect{
                0.0f,
                0.0f,
                static_cast<float>(width),
                static_cast<float>(height),
            });

        return winrt::Windows::Graphics::RectInt32{
            static_cast<int32_t>(std::lround(bounds.X * scale)),
            static_cast<int32_t>(std::lround(bounds.Y * scale)),
            static_cast<int32_t>(std::lround(bounds.Width * scale)),
            static_cast<int32_t>(std::lround(bounds.Height * scale)),
        };
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

    std::wstring FormatLoadTimingTrace(
        std::filesystem::path const& path,
        std::size_t const itemCount,
        winrt::FileScoutNative::implementation::PaneLoadTiming const& timing)
    {
        auto const enumerationMs = timing.enumeration_end_tick >= timing.load_start_tick
            ? timing.enumeration_end_tick - timing.load_start_tick
            : 0;
        auto const firstBatchMs = timing.first_batch_publish_tick >= timing.load_start_tick
            ? timing.first_batch_publish_tick - timing.load_start_tick
            : 0;
        auto const fullCompletionMs = timing.final_publish_tick >= timing.load_start_tick
            ? timing.final_publish_tick - timing.load_start_tick
            : 0;

        return L"PerfLoad path=\"" + path.wstring() +
            L"\" items=" + std::to_wstring(itemCount) +
            L" enumerate_ms=" + std::to_wstring(enumerationMs) +
            L" first_batch_ms=" + std::to_wstring(firstBatchMs) +
            L" full_completion_ms=" + std::to_wstring(fullCompletionMs);
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

    std::wstring BuildWave1FeatureStatus(::FileScoutNative::Domain::Wave1Feature const feature)
    {
        return std::wstring{ ::FileScoutNative::Domain::Wave1FeatureUnavailableStatus(feature) };
    }

    std::wstring DescribePathForStatus(std::filesystem::path const& path)
    {
        auto label = path.filename().wstring();
        if (label.empty())
        {
            label = path.wstring();
        }

        return label;
    }

    std::wstring BuildPasteSuccessStatus(::FileScoutNative::Shell::ClipboardPasteResult const& outcome)
    {
        std::wstring status = outcome.was_move ? L"Moved " : L"Pasted ";
        status += std::to_wstring(outcome.item_count);
        status += outcome.item_count == 1 ? L" item into " : L" items into ";
        status += outcome.destination.wstring();
        status += L".";
        return status;
    }

    std::wstring BuildPasteFailureStatus(::FileScoutNative::Shell::ClipboardPasteResult const& outcome)
    {
        std::wstring status = outcome.was_move ? L"Couldn't move " : L"Couldn't paste ";
        if (outcome.item_count == 0)
        {
            status += L"clipboard items";
        }
        else
        {
            status += std::to_wstring(outcome.item_count);
            status += outcome.item_count == 1 ? L" item" : L" items";
        }

        if (!outcome.destination.empty())
        {
            status += L" into ";
            status += outcome.destination.wstring();
        }

        status += L".";
        if (!outcome.failure_detail.empty())
        {
            status += L" ";
            status += outcome.failure_detail;
        }

        return status;
    }

    bool IsCanceledFileOperation(::FileScoutNative::Shell::FileOperationException const& error)
    {
        return error.Error().detail == L"Windows canceled the file operation.";
    }

    std::wstring BuildRenameFailureStatus(
        ::FileScoutNative::Domain::ShellItemRef const& item,
        ::FileScoutNative::Shell::FileOperationException const& error)
    {
        if (IsCanceledFileOperation(error))
        {
            return L"Rename canceled for " + item.display_name + L".";
        }

        std::wstring status = L"Couldn't rename " + item.display_name + L".";
        if (!error.Error().detail.empty())
        {
            status += L" ";
            status += error.Error().detail;
        }

        return status;
    }

    std::wstring BuildBatchRenameFailureStatus(
        std::size_t const itemCount,
        ::FileScoutNative::Shell::FileOperationException const& error)
    {
        std::wstring status = IsCanceledFileOperation(error)
            ? L"Batch rename canceled for "
            : L"Couldn't batch rename ";
        status += std::to_wstring(itemCount);
        status += itemCount == 1 ? L" item." : L" items.";

        if (!IsCanceledFileOperation(error) && !error.Error().detail.empty())
        {
            status += L" ";
            status += error.Error().detail;
        }

        return status;
    }

    std::wstring BuildDeleteCanceledStatus(
        std::wstring const& displayName,
        std::size_t const itemCount)
    {
        return itemCount == 1
            ? L"Delete canceled for " + displayName + L"."
            : L"Delete canceled for " + std::to_wstring(itemCount) + L" items.";
    }

    std::wstring BuildDeleteFailureStatus(
        std::wstring const& displayName,
        std::size_t const itemCount,
        std::wstring_view const detail)
    {
        std::wstring status = itemCount == 1
            ? L"Couldn't delete " + displayName + L"."
            : L"Couldn't delete " + std::to_wstring(itemCount) + L" items.";

        if (!detail.empty())
        {
            status += L" ";
            status += detail;
        }

        return status;
    }

    std::wstring BuildDropFailureStatus(
        bool const isMove,
        std::size_t const itemCount,
        std::filesystem::path const& destinationPath,
        std::wstring_view const detail,
        bool const canceled)
    {
        std::wstring status = canceled
            ? (isMove ? L"Move canceled for " : L"Copy canceled for ")
            : (isMove ? L"Couldn't move " : L"Couldn't copy ");
        status += std::to_wstring(itemCount);
        status += itemCount == 1 ? L" item into " : L" items into ";
        status += destinationPath.wstring();
        status += L".";

        if (!canceled && !detail.empty())
        {
            status += L" ";
            status += detail;
        }

        return status;
    }

    std::wstring BuildWave1ArchiveStatus(
        std::filesystem::path const& path,
        bool const openSucceeded)
    {
        auto status = BuildWave1FeatureStatus(::FileScoutNative::Domain::Wave1Feature::ArchiveBrowsing);
        if (openSucceeded)
        {
            status += L" Opened " + DescribePathForStatus(path) + L" in Windows.";
        }
        else
        {
            status += L" Open failed for " + DescribePathForStatus(path) + L".";
        }

        return status;
    }

    bool IsSupportedArchiveItem(::FileScoutNative::Domain::ShellItemRef const& item)
    {
        return !item.is_folder && item.is_supported_archive;
    }

    bool IsUnsupportedArchiveItem(::FileScoutNative::Domain::ShellItemRef const& item)
    {
        return !item.is_folder && item.is_archive_candidate && !item.is_supported_archive;
    }

    std::wstring AppendArchiveBrowseHint(
        std::wstring metadata,
        ::FileScoutNative::Domain::ShellItemRef const& item)
    {
        if (!::FileScoutNative::Domain::IsWave1FeatureEnabled(
                ::FileScoutNative::Domain::Wave1Feature::ArchiveBrowsing))
        {
            return metadata;
        }

        if (IsSupportedArchiveItem(item))
        {
            metadata += L"  |  Double-click to browse";
        }
        else if (IsUnsupportedArchiveItem(item))
        {
            metadata += L"  |  Browse unavailable in FileScout on this machine";
        }

        return metadata;
    }

    std::wstring BuildUnsupportedArchiveOpenStatus(
        ::FileScoutNative::Domain::ShellItemRef const& item,
        bool const openSucceeded)
    {
        return openSucceeded
            ? L"Archive browsing unavailable in FileScout on this machine. Opened " +
                item.display_name + L" with Windows."
            : L"Archive browsing unavailable in FileScout on this machine. Open failed for " +
                item.display_name + L".";
    }

    std::wstring SuggestBatchRenameBaseName(std::filesystem::path const& currentPath)
    {
        auto const folderName = currentPath.filename().wstring();
        return folderName.empty() ? std::wstring{ L"Renamed" } : folderName;
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

    std::optional<uint32_t> TryParsePositiveInteger(std::wstring const& value)
    {
        auto const trimmed = TrimFileName(value);
        if (trimmed.empty())
        {
            return std::nullopt;
        }

        wchar_t* end = nullptr;
        auto const parsed = wcstoul(trimmed.c_str(), &end, 10);
        if (end == nullptr || *end != L'\0' || parsed == 0)
        {
            return std::nullopt;
        }

        return static_cast<uint32_t>(parsed);
    }

    enum class BatchRenamePreset
    {
        Sequential = 0,
        AddPrefix = 1,
        AddSuffix = 2,
        FindReplace = 3,
        ChangeCase = 4,
    };

    ::FileScoutNative::Domain::BatchRenameCaseTransform BatchRenameCaseTransformFromSelectedIndex(
        int32_t const selectedIndex)
    {
        switch (selectedIndex)
        {
        case 1:
            return ::FileScoutNative::Domain::BatchRenameCaseTransform::Uppercase;
        case 2:
            return ::FileScoutNative::Domain::BatchRenameCaseTransform::TitleCase;
        case 0:
        default:
            return ::FileScoutNative::Domain::BatchRenameCaseTransform::Lowercase;
        }
    }

    BatchRenamePreset BatchRenamePresetFromSelectedIndex(int32_t const selectedIndex)
    {
        switch (selectedIndex)
        {
        case 1:
            return BatchRenamePreset::AddPrefix;
        case 2:
            return BatchRenamePreset::AddSuffix;
        case 3:
            return BatchRenamePreset::FindReplace;
        case 4:
            return BatchRenamePreset::ChangeCase;
        default:
            return BatchRenamePreset::Sequential;
        }
    }

    std::wstring BatchRenamePresetNote(BatchRenamePreset const preset)
    {
        switch (preset)
        {
        case BatchRenamePreset::AddPrefix:
            return L"Adds text before each current item name. Files keep their existing extensions.";
        case BatchRenamePreset::AddSuffix:
            return L"Adds text before each file extension and at the end of folder names.";
        case BatchRenamePreset::FindReplace:
            return L"Replaces all literal, case-sensitive matches in each selected item name. Files keep their existing extensions.";
        case BatchRenamePreset::ChangeCase:
            return L"Changes letter casing for each selected item name. Files keep their existing extensions.";
        case BatchRenamePreset::Sequential:
        default:
            return L"Selected items are renamed in the current list order. Files keep their existing extensions.";
        }
    }

    std::wstring BatchRenamePresetLabel(BatchRenamePreset const preset)
    {
        switch (preset)
        {
        case BatchRenamePreset::AddPrefix:
            return L"Add prefix";
        case BatchRenamePreset::AddSuffix:
            return L"Add suffix";
        case BatchRenamePreset::FindReplace:
            return L"Find and replace";
        case BatchRenamePreset::ChangeCase:
            return L"Change case";
        case BatchRenamePreset::Sequential:
        default:
            return L"Sequential numbers";
        }
    }

    std::wstring BuildBatchRenameSelectionSummary(
        std::span<::FileScoutNative::Domain::ShellItemRef const> const items)
    {
        auto const folderCount = std::count_if(
            items.begin(),
            items.end(),
            [](auto const& item) { return item.is_folder; });
        auto const fileCount = items.size() - static_cast<std::size_t>(folderCount);

        std::wstring summary = std::to_wstring(items.size()) +
            (items.size() == 1 ? L" item" : L" items");
        summary += L"  |  " + std::to_wstring(folderCount) +
            (folderCount == 1 ? L" folder" : L" folders");
        summary += L"  |  " + std::to_wstring(fileCount) +
            (fileCount == 1 ? L" file" : L" files");
        return summary;
    }

    std::wstring BuildBatchRenameSelectionExamples(
        std::span<::FileScoutNative::Domain::ShellItemRef const> const items)
    {
        if (items.empty())
        {
            return L"No items selected.";
        }

        std::wstring text = L"Includes ";
        auto const sampleCount = (std::min)(items.size(), static_cast<std::size_t>(3));
        for (std::size_t index = 0; index < sampleCount; ++index)
        {
            if (index > 0)
            {
                text += L", ";
            }

            text += items[index].display_name;
        }

        if (items.size() > sampleCount)
        {
            text += L", +" + std::to_wstring(items.size() - sampleCount) + L" more";
        }

        return text + L".";
    }

    struct SidebarDriveUsage
    {
        std::filesystem::path path;
        std::wstring display_name;
        std::uint32_t drive_type{ 0 };
        bool has_usage{ false };
        std::uintmax_t free_bytes{ 0 };
        std::uintmax_t total_bytes{ 0 };
        double used_ratio{ 0.0 };
        ::FileScoutNative::Domain::IconBitmap icon;
    };

    std::vector<SidebarDriveUsage> BuildSidebarDriveUsage(
        ::FileScoutNative::Shell::ShellInteropService& shellInterop)
    {
        std::vector<SidebarDriveUsage> usage;
        for (auto const& drive : shellInterop.EnumerateDrives())
        {
            SidebarDriveUsage sidebarDrive{
                .path = drive.path,
                .display_name = drive.display_name,
                .drive_type = drive.drive_type,
                .icon = drive.icon,
            };

            ULARGE_INTEGER freeBytesAvailable{};
            ULARGE_INTEGER totalBytes{};
            ULARGE_INTEGER totalFreeBytes{};
            if (GetDiskFreeSpaceExW(
                    drive.path.c_str(),
                    &freeBytesAvailable,
                    &totalBytes,
                    &totalFreeBytes) &&
                totalBytes.QuadPart > 0)
            {
                auto const total = static_cast<std::uintmax_t>(totalBytes.QuadPart);
                auto const free = static_cast<std::uintmax_t>(totalFreeBytes.QuadPart);

                sidebarDrive.has_usage = true;
                sidebarDrive.free_bytes = free;
                sidebarDrive.total_bytes = total;
                sidebarDrive.used_ratio = ::FileScoutNative::Domain::DriveUsedRatio(total, free);
            }

            usage.push_back(std::move(sidebarDrive));
        }

        return usage;
    }

    std::wstring SidebarFavoriteFallbackLabel(
        ::FileScoutNative::Domain::FavoriteEntry const& entry)
    {
        if (entry.is_drives_view)
        {
            return L"PC";
        }

        if (entry.is_recycle_bin)
        {
            return L"RB";
        }

        return entry.is_pinned ? L"P" : L"F";
    }

    winrt::Microsoft::UI::Xaml::Controls::FontIcon BuildSidebarFontIcon(
        std::wstring_view const glyph,
        double const fontSize = 15.0,
        std::wstring_view const foregroundBrushKey = L"TextFillColorPrimaryBrush")
    {
        auto icon = winrt::Microsoft::UI::Xaml::Controls::FontIcon{};
        icon.Glyph(winrt::hstring{ glyph });
        icon.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe Fluent Icons"));
        icon.FontSize(fontSize);
        icon.Foreground(LookupBrush(foregroundBrushKey));
        icon.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
        icon.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
        return icon;
    }

    // Maps domain icon_key strings to Segoe Fluent Icons Unicode glyphs.
    std::wstring_view GlyphForMenuIconKey(std::wstring_view const iconKey)
    {
        using namespace std::literals;
        if (iconKey == L"open"sv)                   return L"\uE838";
        if (iconKey == L"openWith"sv)               return L"\uE7AC";
        if (iconKey == L"rename"sv)                 return L"\uE8AC";
        if (iconKey == L"delete"sv)                 return L"\uE74D";
        if (iconKey == L"copy"sv)                   return L"\uE8C8";
        if (iconKey == L"cut"sv)                    return L"\uE8C6";
        if (iconKey == L"paste"sv)                  return L"\uE77F";
        if (iconKey == L"newFolder"sv)              return L"\uE8F4";
        if (iconKey == L"refresh"sv)                return L"\uE72C";
        if (iconKey == L"pin"sv)                    return L"\uE840";
        if (iconKey == L"properties"sv)             return L"\uE946";
        if (iconKey == L"overflow"sv)               return L"\uE712";
        if (iconKey == L"extractToArchiveFolder"sv) return L"\uE7B8";
        if (iconKey == L"extractReveal"sv)          return L"\uED25";
        return {};
    }

    void ApplyCachedRowIconsToRows(
        std::vector<::FileScoutNative::Domain::ShellItemRef> const& items,
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::FileScoutNative::FileRowViewModel> const& rows,
        std::unordered_map<std::wstring, winrt::Microsoft::UI::Xaml::Media::ImageSource> const& iconCache)
    {
        auto const total = static_cast<uint32_t>(
            (std::min)(items.size(), static_cast<std::size_t>(rows.Size())));

        for (auto const index : ::FileScoutNative::Domain::CachedRowIconIndices(items, iconCache))
        {
            if (index >= total)
            {
                continue;
            }

            rows.GetAt(index).IconSource(
                iconCache.at(::FileScoutNative::Domain::RowIconCacheKey(items[index])));
        }
    }

    void ApplyPaneStateToRows(
        winrt::FileScoutNative::implementation::PaneContext const& pane,
        std::vector<::FileScoutNative::Domain::ShellItemRef> const& items,
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::FileScoutNative::FileRowViewModel> const& rows,
        std::unordered_map<std::wstring, winrt::Microsoft::UI::Xaml::Media::ImageSource> const& iconCache)
    {
        auto const total = static_cast<uint32_t>(
            (std::min)(items.size(), static_cast<std::size_t>(rows.Size())));
        std::wstring previousGroupLabel;

        for (uint32_t i = 0; i < total; ++i)
        {
            auto const row = rows.GetAt(i);
            auto rowImpl = winrt::get_self<winrt::FileScoutNative::implementation::FileRowViewModel>(row);

            if (pane.group_by_sort_column)
            {
                auto const groupLabel = BuildGroupLabel(items[i], pane.sort_column);
                auto const showGroupHeader = previousGroupLabel != groupLabel;
                rowImpl->SetGroupHeader(groupLabel, showGroupHeader);
                previousGroupLabel = groupLabel;
            }
            else
            {
                rowImpl->SetGroupHeader(L"", false);
            }
        }

        ApplyCachedRowIconsToRows(items, rows, iconCache);
    }

    winrt::Microsoft::UI::Xaml::FrameworkElement BuildSidebarGlyph(
        std::optional<::FileScoutNative::Domain::IconBitmap> const& icon,
        std::wstring const& fallbackText)
    {
        if (icon.has_value())
        {
            auto image = winrt::Microsoft::UI::Xaml::Controls::Image{};
            image.Width(16);
            image.Height(16);
            image.Stretch(winrt::Microsoft::UI::Xaml::Media::Stretch::Uniform);
            image.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
            image.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
            if (auto const source = BuildIconSource(*icon))
            {
                image.Source(source);
                return image;
            }
        }

        auto fallback = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        fallback.Text(ToHString(fallbackText));
        fallback.FontSize(10);
        fallback.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        fallback.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
        fallback.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
        fallback.Opacity(0.82);
        return fallback;
    }

    winrt::Microsoft::UI::Xaml::Media::Brush SolidBrush(
        uint8_t const alpha,
        uint8_t const red,
        uint8_t const green,
        uint8_t const blue)
    {
        return winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
            winrt::Windows::UI::Color{ alpha, red, green, blue });
    }

    winrt::Microsoft::UI::Xaml::Media::Brush SolidBrush(winrt::Windows::UI::Color const& color)
    {
        return winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(color);
    }

    template<typename T>
    T FindDescendantByType(winrt::Microsoft::UI::Xaml::DependencyObject const& root)
    {
        if (!root)
        {
            return nullptr;
        }

        auto const childCount =
            winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(root);
        for (int32_t index = 0; index < childCount; ++index)
        {
            auto const child =
                winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(root, index);
            if (auto const typed = child.try_as<T>())
            {
                return typed;
            }

            if (auto const descendant = FindDescendantByType<T>(child))
            {
                return descendant;
            }
        }

        return nullptr;
    }

    template<typename T>
    bool HasAncestorOfType(winrt::Windows::Foundation::IInspectable const& source)
    {
        auto dependencyObject = source.try_as<winrt::Microsoft::UI::Xaml::DependencyObject>();
        while (dependencyObject)
        {
            if (dependencyObject.try_as<T>())
            {
                return true;
            }

            dependencyObject = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(dependencyObject);
        }

        return false;
    }

    winrt::Microsoft::UI::Xaml::Controls::Grid CreatePaneSurfaceHost(
        winrt::Microsoft::UI::Xaml::Controls::ListView const& listView)
    {
        auto host = winrt::Microsoft::UI::Xaml::Controls::Grid{};
        host.Background(SolidBrush(0x00, 0x00, 0x00, 0x00));
        host.IsRightTapEnabled(true);
        host.Children().Append(listView);
        return host;
    }

    enum class RowPresentation
    {
        Cards,
        List,
        Group,
    };

    RowPresentation ResolveRowPresentation(
        winrt::FileScoutNative::implementation::PaneContext const& pane)
    {
        if (pane.group_by_sort_column)
        {
            return RowPresentation::Group;
        }

        return pane.view_mode == winrt::FileScoutNative::implementation::ViewMode::List
            ? RowPresentation::List
            : RowPresentation::Cards;
    }

    winrt::hstring RowItemStyleKeyForPresentation(RowPresentation const presentation)
    {
        switch (presentation)
        {
        case RowPresentation::List:
            return winrt::hstring{ L"ListRowItemStyle" };
        case RowPresentation::Group:
            return winrt::hstring{ L"GroupRowItemStyle" };
        case RowPresentation::Cards:
        default:
            return winrt::hstring{ L"CardRowItemStyle" };
        }
    }

    winrt::hstring RowTemplateKeyForPresentation(RowPresentation const presentation)
    {
        switch (presentation)
        {
        case RowPresentation::List:
            return winrt::hstring{ L"ListRowTemplate" };
        case RowPresentation::Group:
            return winrt::hstring{ L"GroupRowTemplate" };
        case RowPresentation::Cards:
        default:
            return winrt::hstring{ L"CardRowTemplate" };
        }
    }

    std::wstring RowPresentationStatusLabel(
        winrt::FileScoutNative::implementation::PaneContext const& pane)
    {
        switch (ResolveRowPresentation(pane))
        {
        case RowPresentation::Group:
            return L"group view";
        case RowPresentation::List:
            return L"list view";
        case RowPresentation::Cards:
        default:
            return L"cards view";
        }
    }

    void ApplyRowPresentation(
        winrt::Microsoft::UI::Xaml::Controls::ListView const& listView,
        winrt::Microsoft::UI::Xaml::Controls::Grid const& rootGrid,
        winrt::FileScoutNative::implementation::PaneContext const& pane)
    {
        auto const presentation = ResolveRowPresentation(pane);
        auto const style = rootGrid.Resources()
            .Lookup(winrt::box_value(RowItemStyleKeyForPresentation(presentation)))
            .as<winrt::Microsoft::UI::Xaml::Style>();
        listView.ItemContainerStyle(style);

        auto const tmpl = rootGrid.Resources()
            .Lookup(winrt::box_value(RowTemplateKeyForPresentation(presentation)))
            .as<winrt::Microsoft::UI::Xaml::DataTemplate>();
        listView.ItemTemplate(tmpl);
    }

    winrt::Microsoft::UI::Xaml::Controls::ListView ResolveTabContentListView(
        winrt::Windows::Foundation::IInspectable const& content)
    {
        if (auto const listView = content.try_as<winrt::Microsoft::UI::Xaml::Controls::ListView>())
        {
            return listView;
        }

        if (auto const element = content.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
        {
            return FindDescendantByType<winrt::Microsoft::UI::Xaml::Controls::ListView>(element);
        }

        return nullptr;
    }

    winrt::Microsoft::UI::Xaml::Controls::Border BuildSidebarTile(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& child,
        double const size)
    {
        auto tile = winrt::Microsoft::UI::Xaml::Controls::Border{};
        tile.Width(size);
        tile.Height(size);
        tile.CornerRadius({ size >= 28.0 ? 10.0f : 8.0f, size >= 28.0 ? 10.0f : 8.0f,
                            size >= 28.0 ? 10.0f : 8.0f, size >= 28.0 ? 10.0f : 8.0f });
        tile.Background(SolidBrush(0x00, 0x00, 0x00, 0x00));
        tile.Child(child);
        return tile;
    }

    void StyleSidebarButtonBase(winrt::Microsoft::UI::Xaml::Controls::Button const& button)
    {
        button.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
        button.Padding({ 0, 0, 0, 0 });
        button.BorderThickness({ 0, 0, 0, 0 });
        button.Background(SolidBrush(0x00, 0x00, 0x00, 0x00));
    }

    winrt::Microsoft::UI::Xaml::Controls::Border BuildSidebarRowSurface(bool const emphasized)
    {
        auto surface = winrt::Microsoft::UI::Xaml::Controls::Border{};
        surface.Padding({ 8, 6, 8, 6 });
        surface.CornerRadius({ 8, 8, 8, 8 });
        surface.BorderThickness({ 0, 0, 0, 0 });
        surface.Background(emphasized
            ? LookupBrush(L"SubtleFillColorSecondaryBrush")
            : SolidBrush(0x00, 0x00, 0x00, 0x00));
        return surface;
    }

    winrt::Microsoft::UI::Xaml::Controls::TextBlock BuildSidebarSectionLabel(std::wstring const& label)
    {
        auto text = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        text.Text(ToHString(label));
        text.FontSize(11);
        text.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        text.Foreground(LookupBrush(L"TextFillColorSecondaryBrush"));
        text.Opacity(0.56);
        text.CharacterSpacing(120);
        return text;
    }

    struct DriveColors
    {
        winrt::Windows::UI::Color body;
        winrt::Windows::UI::Color cap;
        winrt::Windows::UI::Color led;
    };

    DriveColors DriveTypeColors(uint32_t const driveType)
    {
        switch (driveType)
        {
        case DRIVE_REMOVABLE:
            return { {0xff,0x14,0x53,0x2d}, {0xff,0x16,0xa3,0x4a}, {0xff,0x86,0xef,0xac} };
        case DRIVE_REMOTE:
            return { {0xff,0x31,0x2e,0x81}, {0xff,0x7c,0x3a,0xed}, {0xff,0xc4,0xb5,0xfd} };
        default: // DRIVE_FIXED, DRIVE_RAMDISK, DRIVE_CDROM, DRIVE_UNKNOWN
            return { {0xff,0x1e,0x3a,0x5f}, {0xff,0x25,0x63,0xeb}, {0xff,0x93,0xc5,0xfd} };
        }
    }

    winrt::Microsoft::UI::Xaml::Controls::Border MakeDriveBorder(
        double const width,
        double const height,
        winrt::Microsoft::UI::Xaml::CornerRadius const& cornerRadius,
        winrt::Windows::UI::Color const& color,
        winrt::Microsoft::UI::Xaml::VerticalAlignment const vAlign,
        winrt::Microsoft::UI::Xaml::HorizontalAlignment const hAlign,
        winrt::Microsoft::UI::Xaml::Thickness const& margin)
    {
        auto border = winrt::Microsoft::UI::Xaml::Controls::Border{};
        border.Width(width);
        border.Height(height);
        border.CornerRadius(cornerRadius);
        border.Background(SolidBrush(color));
        border.VerticalAlignment(vAlign);
        border.HorizontalAlignment(hAlign);
        border.Margin(margin);
        return border;
    }

    winrt::Microsoft::UI::Xaml::FrameworkElement BuildDriveTypeIcon(uint32_t const driveType)
    {
        namespace VA = winrt::Microsoft::UI::Xaml;
        auto const colors = DriveTypeColors(driveType);

        auto canvas = VA::Controls::Grid{};
        canvas.Width(18);
        canvas.Height(18);

        // Body — darker lower portion
        canvas.Children().Append(MakeDriveBorder(
            15, 10, { 2.5f, 2.5f, 2.5f, 2.5f }, colors.body,
            VA::VerticalAlignment::Bottom, VA::HorizontalAlignment::Center, { 0, 0, 0, 1 }));

        // Cap — brighter upper portion
        canvas.Children().Append(MakeDriveBorder(
            15, 6, { 2.5f, 2.5f, 0, 0 }, colors.cap,
            VA::VerticalAlignment::Top, VA::HorizontalAlignment::Center, { 0, 2, 0, 0 }));

        // LED dot — top-right accent circle
        canvas.Children().Append(MakeDriveBorder(
            3, 3, { 1.5f, 1.5f, 1.5f, 1.5f }, colors.led,
            VA::VerticalAlignment::Top, VA::HorizontalAlignment::Right, { 0, 3, 2, 0 }));

        // USB plug nub — removable only: small block above the cap
        if (driveType == DRIVE_REMOVABLE)
        {
            canvas.Children().Append(MakeDriveBorder(
                4, 3, { 0.8f, 0.8f, 0, 0 }, colors.cap,
                VA::VerticalAlignment::Top, VA::HorizontalAlignment::Center, { 0, 0, 0, 0 }));
        }

        return canvas;
    }

    std::wstring BuildDriveUsageText(SidebarDriveUsage const& drive)
    {
        if (!drive.has_usage)
        {
            return L"Capacity unavailable";
        }

        return FormatSize(drive.free_bytes) + L" free of " + FormatSize(drive.total_bytes);
    }

    std::wstring NormalizeSidebarPath(std::filesystem::path const& path)
    {
        auto normalized = path.lexically_normal().wstring();
        while (normalized.size() > 1 &&
               (normalized.back() == L'\\' || normalized.back() == L'/'))
        {
            if (normalized.size() >= 2 && normalized[normalized.size() - 2] == L':')
            {
                break;
            }

            normalized.pop_back();
        }

        return normalized;
    }

    bool SidebarPathsEqual(
        std::filesystem::path const& candidate,
        std::filesystem::path const& current)
    {
        auto const normalizedCandidate = NormalizeSidebarPath(candidate);
        auto const normalizedCurrent = NormalizeSidebarPath(current);
        return _wcsicmp(normalizedCandidate.c_str(), normalizedCurrent.c_str()) == 0;
    }

    std::size_t SidebarPathMatchScore(
        std::filesystem::path const& candidate,
        std::filesystem::path const& current)
    {
        auto const normalizedCandidate = NormalizeSidebarPath(candidate);
        auto const normalizedCurrent = NormalizeSidebarPath(current);
        if (normalizedCandidate.empty() || normalizedCurrent.empty())
        {
            return 0;
        }

        if (_wcsicmp(normalizedCandidate.c_str(), normalizedCurrent.c_str()) == 0)
        {
            return normalizedCandidate.size();
        }

        if (normalizedCurrent.size() <= normalizedCandidate.size())
        {
            return 0;
        }

        if (_wcsnicmp(
                normalizedCandidate.c_str(),
                normalizedCurrent.c_str(),
                normalizedCandidate.size()) != 0)
        {
            return 0;
        }

        auto const boundary = normalizedCurrent[normalizedCandidate.size()];
        if (boundary != L'\\' && boundary != L'/')
        {
            return 0;
        }

        return normalizedCandidate.size();
    }

    std::wstring SanitizePathComponent(std::wstring value)
    {
        if (value.empty())
        {
            return L"archive";
        }

        for (auto& ch : value)
        {
            switch (ch)
            {
            case L'<':
            case L'>':
            case L':':
            case L'"':
            case L'/':
            case L'\\':
            case L'|':
            case L'?':
            case L'*':
                ch = L'_';
                break;
            default:
                break;
            }
        }

        return value;
    }

    std::filesystem::path BuildArchiveOpenSessionRoot(std::filesystem::path const& archiveFilePath)
    {
        auto const archiveName = archiveFilePath.filename().wstring();
        auto const sessionId =
            std::to_wstring(GetTickCount64()) + L"-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetCurrentThreadId());

        return std::filesystem::temp_directory_path() /
               L"FileScoutNative" /
               L"ArchiveOpen" /
               SanitizePathComponent(archiveName.empty() ? L"archive.zip" : archiveName) /
               sessionId;
    }

    bool HasParentTraversal(std::filesystem::path const& relativePath)
    {
        for (auto const& part : relativePath)
        {
            if (part == std::filesystem::path{ L".." })
            {
                return true;
            }
        }

        return false;
    }

    std::optional<std::filesystem::path> TryGetArchiveRelativePath(
        ::FileScoutNative::Shell::ResolvedArchivePath const& resolved)
    {
        auto archiveRoot = resolved.archive_file_path.wstring();
        if (!archiveRoot.empty() && archiveRoot.back() != L'\\')
        {
            archiveRoot += L'\\';
        }

        auto const virtualPath = resolved.virtual_path.wstring();
        if (virtualPath.size() <= archiveRoot.size() ||
            _wcsnicmp(virtualPath.c_str(), archiveRoot.c_str(), archiveRoot.size()) != 0)
        {
            return std::nullopt;
        }

        auto const relativePath =
            std::filesystem::path{ virtualPath.substr(archiveRoot.size()) }.lexically_normal();
        if (relativePath.empty() || relativePath.is_absolute() || HasParentTraversal(relativePath))
        {
            return std::nullopt;
        }

        return relativePath;
    }

    std::filesystem::path BuildUniqueDestinationPath(
        std::filesystem::path const& desiredPath,
        bool const isFolder)
    {
        std::error_code existsError;
        if (!std::filesystem::exists(desiredPath, existsError))
        {
            return desiredPath;
        }

        auto const parentPath = desiredPath.parent_path();
        auto const filename = desiredPath.filename().wstring();
        auto const stem = isFolder ? filename : desiredPath.stem().wstring();
        auto const extension = isFolder ? std::wstring{} : desiredPath.extension().wstring();

        for (uint32_t suffix = 2;; ++suffix)
        {
            auto const candidateName =
                stem + L" (" + std::to_wstring(suffix) + L")" + extension;
            auto const candidatePath = parentPath / candidateName;
            existsError.clear();
            if (!std::filesystem::exists(candidatePath, existsError))
            {
                return candidatePath;
            }
        }
    }

    std::wstring SortColumnLabel(SortColumn const column)
    {
        switch (column)
        {
        case SortColumn::Name:
            return L"Name";
        case SortColumn::Type:
            return L"Type";
        case SortColumn::Size:
            return L"Size";
        case SortColumn::DateModified:
            return L"Date Modified";
        default:
            return L"Name";
        }
    }

    std::wstring BuildNameGroupLabel(::FileScoutNative::Domain::ShellItemRef const& item)
    {
        std::wstring prefix = item.is_folder ? L"Folders - " : L"Files - ";
        if (item.display_name.empty())
        {
            return prefix + L"#";
        }

        wchar_t ch = static_cast<wchar_t>(::towupper(item.display_name.front()));
        if (::iswalnum(ch))
        {
            return prefix + std::wstring{ 1, ch };
        }

        return prefix + L"#";
    }

    std::wstring BuildSizeGroupLabel(::FileScoutNative::Domain::ShellItemRef const& item)
    {
        if (item.is_folder)
        {
            return L"Folders";
        }

        if (!item.size_available)
        {
            return L"No Size";
        }

        auto const size = item.size_bytes;
        if (size == 0)
        {
            return L"Empty";
        }
        if (size < 100ull * 1024ull)
        {
            return L"Tiny (< 100 KB)";
        }
        if (size < 1024ull * 1024ull)
        {
            return L"Small (< 1 MB)";
        }
        if (size < 16ull * 1024ull * 1024ull)
        {
            return L"Medium (< 16 MB)";
        }
        if (size < 128ull * 1024ull * 1024ull)
        {
            return L"Large (< 128 MB)";
        }

        return L"Huge (>= 128 MB)";
    }

    std::wstring BuildDateModifiedGroupLabel(::FileScoutNative::Domain::ShellItemRef const& item)
    {
        std::wstring prefix = item.is_folder ? L"Folders - " : L"Files - ";
        if (!item.date_modified_available)
        {
            return prefix + L"No Date";
        }

        auto const ticks = static_cast<ULONGLONG>(item.date_modified.time_since_epoch().count());
        FILETIME ft{};
        ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFULL);
        ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);

        SYSTEMTIME utc{};
        SYSTEMTIME local{};
        if (!FileTimeToSystemTime(&ft, &utc) ||
            !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        {
            return prefix + L"No Date";
        }

        SYSTEMTIME now{};
        GetLocalTime(&now);

        if (local.wYear == now.wYear && local.wMonth == now.wMonth && local.wDay == now.wDay)
        {
            return prefix + L"Today";
        }

        FILETIME itemLocalFt{};
        FILETIME nowLocalFt{};
        if (!SystemTimeToFileTime(&local, &itemLocalFt) || !SystemTimeToFileTime(&now, &nowLocalFt))
        {
            return prefix + L"Older";
        }

        ULARGE_INTEGER itemTicks{};
        itemTicks.LowPart = itemLocalFt.dwLowDateTime;
        itemTicks.HighPart = itemLocalFt.dwHighDateTime;

        ULARGE_INTEGER nowTicks{};
        nowTicks.LowPart = nowLocalFt.dwLowDateTime;
        nowTicks.HighPart = nowLocalFt.dwHighDateTime;

        constexpr ULONGLONG dayTicks = 24ull * 60ull * 60ull * 10000000ull;
        if (nowTicks.QuadPart >= itemTicks.QuadPart)
        {
            auto const diffDays = (nowTicks.QuadPart - itemTicks.QuadPart) / dayTicks;
            if (diffDays <= 1)
            {
                return prefix + L"Yesterday";
            }
        }

        if (local.wYear == now.wYear && local.wMonth == now.wMonth)
        {
            return prefix + L"This Month";
        }

        if (local.wYear == now.wYear)
        {
            return prefix + L"Earlier This Year";
        }

        return prefix + L"Older";
    }

    std::wstring BuildGroupLabel(
        ::FileScoutNative::Domain::ShellItemRef const& item,
        SortColumn const sortColumn)
    {
        switch (sortColumn)
        {
        case SortColumn::Name:
            return BuildNameGroupLabel(item);
        case SortColumn::Type:
            return item.is_folder ? std::wstring{ L"Folders" }
                                  : (item.kind_label.empty() ? std::wstring{ L"Unknown Type" } : item.kind_label);
        case SortColumn::Size:
            return BuildSizeGroupLabel(item);
        case SortColumn::DateModified:
            return BuildDateModifiedGroupLabel(item);
        default:
            return BuildNameGroupLabel(item);
        }
    }

}

namespace winrt::FileScoutNative::implementation
{
    MainWindow::MainWindow()
    {
        TraceLog(L"MainWindow ctor: InitializeComponent...");
        InitializeComponent();
        m_dispatcherQueue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        TraceLog(L"MainWindow ctor: InitializeComponent done. ConfigureWindow...");
        ConfigureWindow();
        TraceLog(L"MainWindow ctor: ConfigureWindow done. SetupKeyboardAccelerators...");
        SetupKeyboardAccelerators();
        TraceLog(L"MainWindow ctor: SetupKeyboardAccelerators done. InitializeThemeDropDown...");
        m_currentThemeId = ::FileScoutNative::Domain::ThemeService::LoadSavedThemeId();
        m_dualPaneVisible = !::FileScoutNative::Domain::Wave1UsesSinglePaneFirstLaunch();
        InitializeThemeDropDown();
        TraceLog(L"MainWindow ctor: InitializeThemeDropDown done.");

        LeftPaneBorder().ContextRequested([this](auto const& sender, auto const& args)
            {
                SetActivePaneIndex(0);
                PaneSurface_ContextRequested(sender, args);
            });
        RightPaneBorder().ContextRequested([this](auto const& sender, auto const& args)
            {
                SetActivePaneIndex(1);
                PaneSurface_ContextRequested(sender, args);
            });

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
        // m_initComplete stays false until TitleBarDragHost_Loaded fires (after first render),
        // which prevents deferred WinUI TabView SelectionChanged events from switching to pane 1.
        m_activePaneIndex = 0;
        UpdateActivePaneIndicator();
        ApplyPaneLayoutVisibility();
        RefreshDualPaneButton();

        // S4: letter-jump border flash timer — fires once after 300 ms to restore the normal
        // pane indicator. Stop + Start on each keypress resets the 300 ms window.
        m_letterJumpTimer = winrt::Microsoft::UI::Xaml::DispatcherTimer{};
        m_letterJumpTimer.Interval(std::chrono::milliseconds{ 300 });
        m_letterJumpTimer.Tick([this, lifetime = get_strong()](auto const&, auto const&)
            {
                UpdateActivePaneIndicator();
                m_letterJumpTimer.Stop();
            });

        TraceLog(L"MainWindow ctor: complete.");
    }

    void MainWindow::TitleBarDragHost_Loaded(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        UpdateTitleBarInteractiveRegions();

        // Enqueue at Low priority so all pending Normal-priority deferred XAML events
        // (including the TabView SelectionChanged fired by InitPane) drain first.
        // TitleBarDragHost_Loaded fires BEFORE those deferred events, so a synchronous
        // re-assertion here would be overridden by them.
        m_dispatcherQueue.TryEnqueue(
            winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [this, lifetime = get_strong()]()
            {
                m_activePaneIndex = 0;
                UpdateActivePaneIndicator();
                m_initComplete = true;
            });
    }

    void MainWindow::TitleBarDragHost_SizeChanged(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::SizeChangedEventArgs const&)
    {
        UpdateTitleBarInteractiveRegions();
    }

    void MainWindow::InitializeThemeDropDown()
    {
        RefreshThemeDropDown();
    }

    void MainWindow::RefreshThemeDropDown()
    {
        auto const currentTheme =
            ::FileScoutNative::Domain::ThemeService::GetTheme(m_currentThemeId);
        m_currentThemeId = currentTheme.id;

        ThemeDropDownButton().Content(BuildThemeButtonContent(currentTheme));

        auto flyout = winrt::Microsoft::UI::Xaml::Controls::MenuFlyout{};
        for (auto const& theme : ::FileScoutNative::Domain::ThemeService::GetAllThemes())
        {
            auto item = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
            item.Icon(BuildThemeSwatchIcon(theme.accent, 11.0));
            item.Text(ToHString(
                theme.id == m_currentThemeId
                    ? theme.display_name + L" (Current)"
                    : theme.display_name));
            item.FontWeight(
                theme.id == m_currentThemeId
                    ? winrt::Microsoft::UI::Text::FontWeights::SemiBold()
                    : winrt::Microsoft::UI::Text::FontWeights::Normal());

            auto const themeId = theme.id;
            item.Click([this, themeId](auto const&, auto const&)
            {
                ApplyThemeSelection(themeId);
            });

            flyout.Items().Append(item);
        }

        ThemeDropDownButton().Flyout(flyout);
        UpdateTitleBarInteractiveRegions();
    }

    void MainWindow::ApplyThemeSelection(std::wstring_view const themeId)
    {
        auto const theme = ::FileScoutNative::Domain::ThemeService::GetTheme(themeId);

        try
        {
            CurrentAppImplementation()->ApplyTheme(theme.id);
            ::FileScoutNative::Domain::ThemeService::SaveThemeId(theme.id);
            m_currentThemeId = theme.id;

            RefreshThemeDropDown();
            RefreshThemeSensitiveChrome();
            StatusTextBlock().Text(ToHString(L"Theme changed to " + theme.display_name + L"."));
        }
        catch (...)
        {
            StatusTextBlock().Text(L"Theme change failed.");
        }
    }

    void MainWindow::RefreshThemeSensitiveChrome()
    {
        BuildSidebarFavorites();
        BuildSortHeader(0);
        BuildSortHeader(1);
        UpdateActivePaneIndicator();
        RefreshDualPaneButton();

        if (::FileScoutNative::Domain::IsWave1FeatureEnabled(
                ::FileScoutNative::Domain::Wave1Feature::CommandPalette) &&
            CommandPaletteOverlay().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            auto const query = CommandPaletteTextBox().Text();
            RefreshCommandPalette(query.c_str());
        }
    }

    void MainWindow::RefreshButton_Click(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (ActivePane().is_drives_view || ActivePane().current_path.empty())
        {
            LoadDrivesView();
            return;
        }

        LoadDirectory(ActivePane().current_path);
    }

    void MainWindow::AddressTextBox_KeyDown(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == winrt::Windows::System::VirtualKey::Escape)
        {
            args.Handled(true);
            HideAddressTextBox();
            return;
        }

        if (args.Key() != winrt::Windows::System::VirtualKey::Enter)
        {
            return;
        }

        args.Handled(true);
        (void)TryNavigateToAddressInput(AddressTextBox().Text().c_str());
    }

    void MainWindow::AddressTextBox_LostFocus(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        HideAddressTextBox();
    }

    void MainWindow::BreadcrumbBarBorder_Tapped(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const&)
    {
        ShowAddressTextBox();
    }

    void MainWindow::SearchAccelerator_Invoked(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        // Just focus the dedicated search box — typing will enter Filter mode automatically.
        SearchTextBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    bool MainWindow::TryNavigateToAddressInput(std::wstring_view const rawInput)
    {
        auto const trimmed = ::FileScoutNative::Domain::TrimAddressNavigationInput(rawInput);
        auto const target = ::FileScoutNative::Domain::ResolveAddressNavigationTarget(
            trimmed,
            [](std::filesystem::path const& path)
            {
                std::error_code existsError;
                return std::filesystem::exists(path, existsError) && !existsError;
            },
            [](std::filesystem::path const& path)
            {
                std::error_code directoryError;
                return std::filesystem::is_directory(path, directoryError) && !directoryError;
            },
            [this](std::filesystem::path const& path)
            {
                return m_shellInterop.CanEnumerateShellNamespace(path);
            });

        switch (target.kind)
        {
        case ::FileScoutNative::Domain::AddressNavigationTargetKind::DrivesView:
            HideAddressTextBox();
            LoadDrivesView();
            return true;

        case ::FileScoutNative::Domain::AddressNavigationTargetKind::Directory:
        case ::FileScoutNative::Domain::AddressNavigationTargetKind::ShellNamespace:
            HideAddressTextBox();
            LoadDirectory(target.path);
            return true;

        case ::FileScoutNative::Domain::AddressNavigationTargetKind::File:
        {
            std::filesystem::path filePath = target.path;
            std::error_code absoluteError;
            auto const absolutePath = std::filesystem::absolute(filePath, absoluteError);
            if (!absoluteError)
            {
                filePath = absolutePath.lexically_normal();
            }

            auto parentPath = filePath.parent_path();
            if (parentPath.empty())
            {
                StatusTextBlock().Text(ToHString(
                    L"Cannot navigate to file without a parent folder: " + trimmed));
                AddressTextBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
                return false;
            }

            HideAddressTextBox();
            LoadDirectory(parentPath, filePath);
            return true;
        }

        case ::FileScoutNative::Domain::AddressNavigationTargetKind::Invalid:
        default:
            StatusTextBlock().Text(ToHString(L"Path not found or unavailable: " + trimmed));
            AddressTextBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
            return false;
        }
    }

    void MainWindow::ResetSearchState(int32_t const paneIndex, bool const restoreSnapshot)
    {
        auto& pane = m_panes[paneIndex];
        pane.search_service.Cancel();
        AdvanceSearchViewGeneration(pane);
        SearchProgressForPane(paneIndex).Visibility(
            winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

        if (restoreSnapshot)
        {
            auto& rows = pane.tab_rows[pane.active_tab_index];
            rows.Clear();
            for (auto const& item : pane.search_snapshot)
            {
                rows.Append(MakeFileRowViewModel(item));
            }

            ApplyPaneStateToRows(pane, pane.search_snapshot, rows, m_iconSourceCache);
        }

        pane.search_mode = SearchMode::None;
        pane.search_query.clear();
        pane.filtered_items.clear();
        pane.search_snapshot.clear();

        if (paneIndex == m_activePaneIndex)
        {
            SearchTextBox().IsReadOnly(false);
            SearchTextBox().Text(L"");
        }
    }

    void MainWindow::SearchTextBox_KeyDown(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        auto& pane = ActivePane();

        if (args.Key() == winrt::Windows::System::VirtualKey::Escape)
        {
            if (pane.search_mode == SearchMode::Filter || pane.search_mode == SearchMode::Recursive)
            {
                args.Handled(true);
                ResetSearchState(m_activePaneIndex, true);
                StatusTextBlock().Text(ToHString(
                    std::to_wstring(pane.items.size()) + L" items  |  " +
                    RowPresentationStatusLabel(pane)));
            }
            return;
        }

        if (args.Key() == winrt::Windows::System::VirtualKey::Enter &&
            pane.search_mode == SearchMode::Filter)
        {
            args.Handled(true);

            if (pane.search_query.size() < 2) return;
            if (!IsLocalDrivePath(pane.current_path))
            {
                StatusTextBlock().Text(L"Recursive search is limited to local drives.");
                return;
            }

            pane.search_mode = SearchMode::Recursive;
            pane.filtered_items.clear();
            SearchTextBox().IsReadOnly(true);

            // Show per-pane progress bar.
            SearchProgressForPane(m_activePaneIndex).Visibility(
                winrt::Microsoft::UI::Xaml::Visibility::Visible);

            // Update status bar.
            StatusTextBlock().Text(ToHString(
                ::FileScoutNative::Search::BuildSearchProgressStatus(0)));

            auto const paneIndex = m_activePaneIndex;
            auto post = [dq = m_dispatcherQueue](std::function<void()> fn)
            {
                dq.TryEnqueue(
                    winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Normal,
                    std::move(fn));
            };

            auto resultCount = std::make_shared<std::atomic<std::size_t>>(0);

            pane.search_service.Start(
                pane.current_path.wstring(),
                pane.search_query,
                [this, paneIndex, resultCount](
                    std::vector<::FileScoutNative::Search::SearchResult> results)
                {
                    auto& p = m_panes[paneIndex];
                    if (p.search_mode != SearchMode::Recursive) return;

                    *resultCount = results.size();
                    auto const preservedSelection = p.selection;
                    p.filtered_items = ::FileScoutNative::Search::BuildSearchResultItems(
                        results,
                        p.current_path);
                    auto const searchViewGeneration = AdvanceSearchViewGeneration(p);
                    auto searchIconCancel = p.search_icon_backfill_cancel;

                    auto& rows = p.tab_rows[p.active_tab_index];
                    rows.Clear();
                    for (auto const& item : p.filtered_items)
                    {
                        rows.Append(MakeFileRowViewModel(item));
                    }

                    ApplyPaneStateToRows(p, p.filtered_items, rows, m_iconSourceCache);
                    RestoreSelectionFromState(p, preservedSelection, -1);
                    BackfillSearchResultIconsAsync(
                        paneIndex,
                        p.active_tab_index,
                        searchViewGeneration,
                        std::move(searchIconCancel),
                        p.filtered_items);
                    if (m_activePaneIndex == paneIndex)
                    {
                        auto const selectedIndices = GetSelectedItemIndices();
                        if (selectedIndices.size() == 1)
                        {
                            SelectItemByIndex(selectedIndices.front());
                        }
                        else if (selectedIndices.size() > 1)
                        {
                            UpdateMultiSelectionDetails(selectedIndices);
                        }
                        else
                        {
                            ResetSelectionDetails();
                        }

                        StatusTextBlock().Text(ToHString(
                            ::FileScoutNative::Search::BuildSearchProgressStatus(
                                resultCount->load())));
                    }
                },
                [this, paneIndex, resultCount](bool cap_reached, bool failed)
                {
                    auto& p = m_panes[paneIndex];
                    if (p.search_mode != SearchMode::Recursive) return;

                    if (failed)
                    {
                        ResetSearchState(paneIndex, true);
                        if (m_activePaneIndex == paneIndex)
                        {
                            ResetSelectionDetails();
                            StatusTextBlock().Text(L"Search failed.");
                        }
                        return;
                    }

                    SearchProgressForPane(paneIndex).Visibility(
                        winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
                    if (m_activePaneIndex == paneIndex)
                    {
                        StatusTextBlock().Text(ToHString(
                            ::FileScoutNative::Search::BuildSearchCompleteStatus(
                                resultCount->load(),
                                p.current_path,
                                cap_reached)));
                    }
                },
                post);
        }
    }

    void MainWindow::SearchTextBox_TextChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&)
    {
        auto& pane = ActivePane();

        // Recursive mode is driven by SearchService — don't interfere with it.
        if (pane.search_mode == SearchMode::Recursive) return;

        std::wstring query{ SearchTextBox().Text() };

        if (query.empty())
        {
            // Leaving search — restore the list if we were filtering.
            if (pane.search_mode == SearchMode::Filter)
            {
                ResetSearchState(m_activePaneIndex, true);
                StatusTextBlock().Text(ToHString(
                    std::to_wstring(pane.items.size()) + L" items  |  " +
                    RowPresentationStatusLabel(pane)));
            }
            return;
        }

        // Transition into Filter mode on first character.
        if (pane.search_mode == SearchMode::None)
        {
            if (pane.awaiting_final_batch) return; // Snapshot would be incomplete.
            pane.search_mode = SearchMode::Filter;
            auto const tabIndex = pane.active_tab_index;
            pane.search_snapshot = pane.tabs[tabIndex].items;
        }

        pane.search_query = query;

        auto& rows = pane.tab_rows[pane.active_tab_index];
        rows.Clear();

        auto const filteredResults =
            ::FileScoutNative::Search::BuildFilterResults(query, pane.search_snapshot);

        std::vector<::FileScoutNative::Domain::ShellItemRef> filteredItems;
        filteredItems.reserve(filteredResults.size());
        for (auto const* itemPtr : filteredResults)
        {
            rows.Append(MakeFileRowViewModel(*itemPtr));
            filteredItems.push_back(*itemPtr);
        }

        pane.filtered_items = filteredItems;
        ApplyPaneStateToRows(pane, filteredItems, rows, m_iconSourceCache);
    }

    void MainWindow::FileRowsListView_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&)
    {
        if (ActivePane().tab_switching || !ActivePane().active_list_view)
        {
            return;
        }

        // Record live user interaction so the final-batch publish does not clobber selection state.
        ActivePane().user_interacted_after_first_batch = true;
        SyncSelectionFromListView();
        auto const selectedIndices = GetSelectedItemIndices();
        if (selectedIndices.empty())
        {
            ResetSelectionDetails();
            StatusTextBlock().Text(L"No items selected.");
            return;
        }

        if (selectedIndices.size() == 1)
        {
            SelectItemByIndex(selectedIndices.front());
            return;
        }

        UpdateMultiSelectionDetails(selectedIndices);
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

        (void)ActivateDisplayedItemByIndex(ResolveItemIndexFromSource(args.OriginalSource()));
    }

    void MainWindow::FileRowsListView_RightTapped(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& args)
    {
        if (ShouldSkipPaneContextMenu(args.OriginalSource()))
        {
            return;
        }

        args.Handled(true);

        if (POINT point{}; GetCursorPos(&point))
        {
            m_lastContextMenuPoint = point;
            m_hasLastContextMenuPoint = true;
        }

        auto const itemIndex = ResolveItemIndexFromSource(args.OriginalSource());
        if (itemIndex.has_value() && ActivePane().active_list_view)
        {
            auto const* item = TryGetDisplayedItemByIndex(*itemIndex);
            if (item == nullptr)
            {
                return;
            }

            if (!ActivePane().selection.IsSelected(item->parsing_name))
            {
                ActivePane().active_list_view.SelectedIndex(*itemIndex);
                SelectItemByIndex(*itemIndex);
            }
            else
            {
                SyncSelectionFromListView();
                auto const selectedIndices = GetSelectedItemIndices();
                if (selectedIndices.size() > 1)
                {
                    UpdateMultiSelectionDetails(selectedIndices);
                }
                else
                {
                    SelectItemByIndex(*itemIndex);
                }
            }
        }
        else if (ActivePane().active_list_view)
        {
            ActivePane().active_list_view.SelectedIndex(-1);
            SelectItemByIndex(-1);
        }

        ::FileScoutNative::Shell::ContextMenuScenario scenario;
        scenario.can_paste =
            !ActivePane().is_archive_view &&
            m_shellClipboardService.CanPasteInto(ActivePane().current_path);
        scenario.is_read_only_archive_view = ActivePane().is_archive_view;
        scenario.selection_count = static_cast<uint32_t>(GetSelectedItemIndices().size());
        if (itemIndex.has_value())
        {
            if (auto const* item = TryGetDisplayedItemByIndex(*itemIndex))
            {
                scenario.focused_item = *item;
            }
        }
        if (scenario.selection_count <= 1)
        {
            if (scenario.focused_item.has_value() &&
                scenario.focused_item->is_folder &&
                !scenario.focused_item->path.empty() &&
                !m_favoritesService.IsSidebarFavorite(scenario.focused_item->path))
            {
                scenario.can_pin_to_places = true;
            }
            else if (!scenario.focused_item.has_value() &&
                     !ActivePane().current_path.empty() &&
                     !ActivePane().is_archive_view &&
                     !m_favoritesService.IsSidebarFavorite(ActivePane().current_path))
            {
                scenario.can_pin_to_places = true;
            }
        }

        if (auto const target = [&]() -> winrt::Microsoft::UI::Xaml::FrameworkElement
            {
                if (auto const senderTarget = args.OriginalSource().try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
                {
                    auto dependencyObject = senderTarget.try_as<winrt::Microsoft::UI::Xaml::DependencyObject>();
                    while (dependencyObject)
                    {
                        if (auto const listView = dependencyObject.try_as<winrt::Microsoft::UI::Xaml::Controls::ListView>())
                        {
                            return listView;
                        }

                        if (auto const border = dependencyObject.try_as<winrt::Microsoft::UI::Xaml::Controls::Border>())
                        {
                            if (border == LeftPaneBorder() || border == RightPaneBorder())
                            {
                                return border;
                            }
                        }

                        dependencyObject = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(dependencyObject);
                    }
                }

                if (ActivePane().active_list_view)
                {
                    return ActivePane().active_list_view;
                }

                return nullptr;
            }(); target)
        {
            auto const tapPoint = args.GetPosition(target);
            ShowContextMenu(
                m_contextMenuService.BuildMenu(scenario),
                itemIndex,
                target,
                tapPoint);
        }

        args.Handled(true);
    }

    void MainWindow::PaneSurface_ContextRequested(
        winrt::Microsoft::UI::Xaml::UIElement const& sender,
        winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args)
    {
        if (ShouldSkipPaneContextMenu(args.OriginalSource()))
        {
            return;
        }

        auto const itemIndex = ResolveItemIndexFromSource(args.OriginalSource());
        if (itemIndex.has_value())
        {
            return;
        }

        args.Handled(true);

        if (POINT point{}; GetCursorPos(&point))
        {
            m_lastContextMenuPoint = point;
            m_hasLastContextMenuPoint = true;
        }

        if (ActivePane().active_list_view)
        {
            ActivePane().active_list_view.SelectedIndex(-1);
            SelectItemByIndex(-1);
        }

        ::FileScoutNative::Shell::ContextMenuScenario scenario;
        scenario.can_paste =
            !ActivePane().is_archive_view &&
            m_shellClipboardService.CanPasteInto(ActivePane().current_path);
        scenario.is_read_only_archive_view = ActivePane().is_archive_view;
        scenario.selection_count = static_cast<uint32_t>(GetSelectedItemIndices().size());
        if (itemIndex.has_value())
        {
            if (auto const* item = TryGetDisplayedItemByIndex(*itemIndex))
            {
                scenario.focused_item = *item;
            }
        }
        if (scenario.selection_count <= 1)
        {
            if (scenario.focused_item.has_value() &&
                scenario.focused_item->is_folder &&
                !scenario.focused_item->path.empty() &&
                !m_favoritesService.IsSidebarFavorite(scenario.focused_item->path))
            {
                scenario.can_pin_to_places = true;
            }
            else if (!scenario.focused_item.has_value() &&
                     !ActivePane().current_path.empty() &&
                     !ActivePane().is_archive_view &&
                     !m_favoritesService.IsSidebarFavorite(ActivePane().current_path))
            {
                scenario.can_pin_to_places = true;
            }
        }

        if (auto const target = [&]() -> winrt::Microsoft::UI::Xaml::FrameworkElement
            {
                if (auto const senderTarget = args.OriginalSource().try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
                {
                    auto dependencyObject = senderTarget.try_as<winrt::Microsoft::UI::Xaml::DependencyObject>();
                    while (dependencyObject)
                    {
                        if (auto const listView = dependencyObject.try_as<winrt::Microsoft::UI::Xaml::Controls::ListView>())
                        {
                            return listView;
                        }

                        if (auto const scrollViewer =
                                dependencyObject.try_as<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>())
                        {
                            return scrollViewer;
                        }

                        if (auto const tabView =
                                dependencyObject.try_as<winrt::Microsoft::UI::Xaml::Controls::TabView>())
                        {
                            return tabView;
                        }

                        dependencyObject = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(dependencyObject);
                    }
                }

                if (auto const frameworkElement = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
                {
                    return frameworkElement;
                }

                if (ActivePane().active_list_view)
                {
                    return ActivePane().active_list_view;
                }

                return nullptr;
            }(); target)
        {
            winrt::Windows::Foundation::Point tapPoint{};
            auto const hasPoint = args.TryGetPosition(target.as<winrt::Microsoft::UI::Xaml::UIElement>(), tapPoint);
            if (!hasPoint)
            {
                tapPoint = { 24.0f, 24.0f };
            }

            ShowContextMenu(
                m_contextMenuService.BuildMenu(scenario),
                itemIndex,
                target,
                tapPoint);
        }
    }

    void MainWindow::FileRowsListView_DragItemsStarting(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const& e)
    {
        if (ActivePane().is_archive_view)
        {
            e.Cancel(true);
            return;
        }

        if (!ActivePane().active_list_view)
        {
            e.Cancel(true);
            return;
        }

        auto const selectedItems = GetSelectedItems();
        if (selectedItems.empty())
        {
            e.Cancel(true);
            return;
        }

        try
        {
            e.Data().SetData(
                DragOriginalPathsFormat(),
                winrt::box_value(ToHString(EncodeDragOriginalPaths(selectedItems))));

            if (::FileScoutNative::Domain::ResolveExternalDragStorageResolutionMode() ==
                ::FileScoutNative::Domain::DragStorageResolutionMode::DeferredProvider)
            {
                e.Data().SetDataProvider(
                    winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems(),
                    [selectedItems](winrt::Windows::ApplicationModel::DataTransfer::DataProviderRequest const& request)
                    {
                        ProvideDragStorageItemsAsync(selectedItems, request);
                    });
            }
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
        if (ActivePane().is_archive_view)
        {
            args.AcceptedOperation(
                winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::None);
            return;
        }

        if (!args.DataView().Contains(DragOriginalPathsFormat()) &&
            !args.DataView().Contains(
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

        if (ActivePane().is_archive_view)
        {
            StatusTextBlock().Text(L"Archive mode is read-only in this slice.");
            deferral.Complete();
            co_return;
        }

        // Capture state before the co_await suspension point.
        auto const destinationPath = ActivePane().current_path;
        bool const isMove =
            (args.Modifiers() &
             winrt::Windows::ApplicationModel::DataTransfer::DragDrop::DragDropModifiers::Shift) !=
            winrt::Windows::ApplicationModel::DataTransfer::DragDrop::DragDropModifiers::None;
        std::vector<std::filesystem::path> sourcePaths;

        try
        {
            if (args.DataView().Contains(DragOriginalPathsFormat()))
            {
                auto const payload = co_await args.DataView().GetDataAsync(DragOriginalPathsFormat());
                if (auto const pathText = payload.try_as<winrt::hstring>(); pathText.has_value())
                {
                    sourcePaths = DecodeDragOriginalPaths(pathText->c_str());
                }
            }

            if (sourcePaths.empty())
            {
                auto const storageItems = co_await args.DataView().GetStorageItemsAsync();
                sourcePaths.reserve(storageItems.Size());
                for (auto const& storageItem : storageItems)
                {
                    sourcePaths.emplace_back(storageItem.Path().c_str());
                }
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
        catch (::FileScoutNative::Shell::FileOperationException const& error)
        {
            StatusTextBlock().Text(ToHString(BuildDropFailureStatus(
                isMove,
                sourcePaths.size(),
                destinationPath,
                error.Error().detail,
                IsCanceledFileOperation(error))));
        }
        catch (std::exception const& error)
        {
            StatusTextBlock().Text(ToHString(
                L"Couldn't complete the drop into " + destinationPath.wstring() + L". " +
                winrt::to_hstring(error.what()).c_str()));
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

        if (pane.search_mode != SearchMode::None)
        {
            ResetSearchState(paneIndex, true);
        }

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
            pane.tabs[pane.active_tab_index].is_drives_view = pane.is_drives_view;
            pane.tabs[pane.active_tab_index].is_archive_view = pane.is_archive_view;
        }

        pane.active_tab_index = newIndex;

        // Switching to this pane (if not already active) and restore its tab.
        // Guard against deferred WinUI events firing before init is complete.
        if (m_initComplete)
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

    void MainWindow::ConfigureTitleBar()
    {
        ExtendsContentIntoTitleBar(true);

        m_appWindow = this->AppWindow();
        auto const titleBar = m_appWindow.TitleBar();
        titleBar.ExtendsContentIntoTitleBar(true);
        titleBar.PreferredHeightOption(winrt::Microsoft::UI::Windowing::TitleBarHeightOption::Tall);

        SetTitleBar(TitleBarDragHost());

        m_titleBarPointerSource =
            winrt::Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(m_appWindow.Id());

        UpdateTitleBarInteractiveRegions();
    }

    void MainWindow::UpdateTitleBarInteractiveRegions()
    {
        auto const host = TitleBarDragHost();
        if (!ExtendsContentIntoTitleBar() || !host || !host.XamlRoot())
        {
            return;
        }

        if (!m_appWindow)
        {
            m_appWindow = this->AppWindow();
        }

        if (!m_titleBarPointerSource)
        {
            m_titleBarPointerSource =
                winrt::Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(m_appWindow.Id());
        }

        auto const scale = host.XamlRoot().RasterizationScale();
        auto const titleBar = m_appWindow.TitleBar();
        TitleBarLeftInsetColumn().Width(
            winrt::Microsoft::UI::Xaml::GridLength{ titleBar.LeftInset() / scale });
        TitleBarRightInsetColumn().Width(
            winrt::Microsoft::UI::Xaml::GridLength{ titleBar.RightInset() / scale });

        std::vector<winrt::Windows::Graphics::RectInt32> passthroughRects;
        auto const appendRect =
            [&](winrt::Microsoft::UI::Xaml::FrameworkElement const& element)
            {
                if (auto const rect = TryGetElementRect(element, scale))
                {
                    passthroughRects.push_back(*rect);
                }
            };

        appendRect(BackButton());
        appendRect(ForwardButton());
        appendRect(RefreshButton());
        appendRect(BreadcrumbBarBorder());
        appendRect(AddressTextBox());
        appendRect(SearchTextBox());
        appendRect(DualPaneButton());
        appendRect(ThemeDropDownButton());

        if (passthroughRects.empty())
        {
            m_titleBarPointerSource.ClearRegionRects(
                winrt::Microsoft::UI::Input::NonClientRegionKind::Passthrough);
            return;
        }

        m_titleBarPointerSource.SetRegionRects(
            winrt::Microsoft::UI::Input::NonClientRegionKind::Passthrough,
            passthroughRects);
    }

    void MainWindow::ConfigureWindow()
    {
        Title(L"FileScout Native");

        SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop{});
        ConfigureTitleBar();

        const auto hwnd = GetWindowHandle();
        const UINT dpi = GetDpiForWindow(hwnd);
        const float scale = static_cast<float>(dpi) / 96.0f;
        const int width = static_cast<int>(1520 * scale);
        const int height = static_cast<int>(920 * scale);

        SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);

        static constexpr UINT_PTR navigationWindowSubclassId = 0x46534E56;
        SetWindowSubclass(
            hwnd,
            &MainWindow::NavigationWindowSubclassProc,
            navigationWindowSubclassId,
            reinterpret_cast<DWORD_PTR>(this));
    }

    LRESULT CALLBACK MainWindow::NavigationWindowSubclassProc(
        HWND const hwnd,
        UINT const message,
        WPARAM const wParam,
        LPARAM const lParam,
        UINT_PTR const,
        DWORD_PTR const refData)
    {
        auto const command = ::FileScoutNative::Domain::ResolveNavigationInputCommand(
            message,
            wParam,
            lParam);
        if (command == ::FileScoutNative::Domain::NavigationInputCommand::None)
        {
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

        auto* const self = reinterpret_cast<MainWindow*>(refData);
        if (self == nullptr)
        {
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

        if (command == ::FileScoutNative::Domain::NavigationInputCommand::Back)
        {
            self->NavigateBack();
        }
        else
        {
            self->NavigateForward();
        }

        return TRUE;
    }

    void MainWindow::SortItems(
        std::vector<::FileScoutNative::Domain::ShellItemRef>& items,
        SortColumn const column,
        SortDirection const direction)
    {
        auto const mappedColumn = [&]() -> ::FileScoutNative::Domain::ShellItemSortColumn
        {
            switch (column)
            {
            case SortColumn::Name:
                return ::FileScoutNative::Domain::ShellItemSortColumn::Name;
            case SortColumn::Type:
                return ::FileScoutNative::Domain::ShellItemSortColumn::Type;
            case SortColumn::Size:
                return ::FileScoutNative::Domain::ShellItemSortColumn::Size;
            case SortColumn::DateModified:
                return ::FileScoutNative::Domain::ShellItemSortColumn::DateModified;
            }

            return ::FileScoutNative::Domain::ShellItemSortColumn::Name;
        }();

        auto const mappedDirection =
            direction == SortDirection::Ascending
                ? ::FileScoutNative::Domain::ShellItemSortDirection::Ascending
                : ::FileScoutNative::Domain::ShellItemSortDirection::Descending;

        ::FileScoutNative::Domain::SortShellItems(items, mappedColumn, mappedDirection);
    }

    void MainWindow::LoadDirectory(
        std::filesystem::path const& path,
        std::optional<std::filesystem::path> selectAfter,
        bool skipHistoryRecord)
    {
        std::filesystem::path absolutePath;
        try
        {
            absolutePath = std::filesystem::absolute(path).lexically_normal();
        }
        catch (...)
        {
            absolutePath = path;
        }

        if (!::FileScoutNative::Domain::IsWave1FeatureEnabled(
                ::FileScoutNative::Domain::Wave1Feature::ArchiveBrowsing) &&
            m_shellInterop.IsArchivePath(absolutePath))
        {
            try
            {
                m_shellFileOperationService.OpenPath(GetWindowHandle(), absolutePath);
                StatusTextBlock().Text(ToHString(BuildWave1ArchiveStatus(absolutePath, true)));
            }
            catch (std::exception const&)
            {
                StatusTextBlock().Text(ToHString(BuildWave1ArchiveStatus(absolutePath, false)));
            }

            return;
        }

        // Exit search mode before navigating.
        auto& pane = ActivePane();

        // Record current path in back-stack unless this is a history navigation.
        if (!skipHistoryRecord && !pane.current_path.empty())
        {
            pane.back_stack.push_back(pane.current_path);
            pane.forward_stack.clear();
            BuildSidebarFavorites();  // refresh recent paths section
        }
        UpdateNavButtons();
        if (pane.search_mode != SearchMode::None)
        {
            ResetSearchState(m_activePaneIndex, true);
        }

        // Capture pane/tab index synchronously so async work targets the right pane
        // even if the user switches panes before the enumeration finishes.
        LoadDirectoryAsync(absolutePath, m_activePaneIndex, std::move(selectAfter));
    }

    winrt::fire_and_forget MainWindow::LoadDirectoryAsync(
        std::filesystem::path path,
        int32_t const paneIndex,
        std::optional<std::filesystem::path> selectAfter)
    {
        auto lifetime = get_strong();
        auto& pane = m_panes[paneIndex];
        uint32_t const generation = ++pane.load_generation;
        int32_t const tabIndex = pane.active_tab_index;
        PaneLoadTiming timing{};
        timing.load_start_tick = ::GetTickCount64();
        pane.last_load_timing = timing;

        // Immediately clear stale content on the UI thread so the list goes blank
        // rather than showing outdated rows during enumeration.
        pane.items.clear();
        pane.selection.Clear();
        pane.tab_rows[tabIndex].Clear();
        if (pane.active_list_view)
        {
            pane.tab_switching = true;
            pane.active_list_view.SelectedIndex(-1);
            pane.tab_switching = false;
        }
        // Normalize path to absolute on the UI thread so the initial breadcrumb shows the full
        // path immediately (before co_await returns the background thread).
        std::filesystem::path absolute;
        try { absolute = std::filesystem::absolute(path).lexically_normal(); }
        catch (...) { absolute = path; }

        if (m_activePaneIndex == paneIndex)
        {
            UpdateBreadcrumbs(absolute);
            UpdateSearchPlaceholder(absolute);
            HideAddressTextBox();
            ResetSelectionDetails();
        }

        // Prevent search Filter snapshot from capturing a partial item set during streaming.
        pane.awaiting_final_batch = true;

        // Capture sort parameters while still on the UI thread so the background section
        // can sort without accessing pane state across threads.
        SortColumn const sort_column = pane.sort_column;
        SortDirection const sort_direction = pane.sort_direction;

        // Switch to a thread-pool thread for I/O and shell COM calls.
        co_await winrt::resume_background();

        try
        {
            if (m_shellInterop.IsArchivePath(absolute))
            {
                // Archive: enumerate all at once, then publish on the UI thread.
                auto archiveItems = m_shellInterop.EnumerateArchiveDirectory(absolute);
                timing.enumeration_end_tick = ::GetTickCount64();
                m_dispatcherQueue.TryEnqueue(
                    [this, lifetime, paneIndex, tabIndex, generation,
                     absolute, selectAfter, timing, items = std::move(archiveItems)]() mutable
                    {
                        auto& pane = m_panes[paneIndex];
                        if (pane.load_generation != generation) return;
                        if (pane.active_tab_index != tabIndex) return;
                        pane.awaiting_final_batch = false;

                        SortItems(items, pane.sort_column, pane.sort_direction);
                        std::size_t const totalItems = items.size();
                        pane.current_path = absolute;
                        pane.is_drives_view = false;
                        pane.is_archive_view = true;
                        pane.items = items;
                        pane.selection.Clear();

                        pane.tabs[tabIndex].path = absolute;
                        pane.tabs[tabIndex].items = items;
                        pane.tabs[tabIndex].selection = {};
                        pane.tabs[tabIndex].selected_item_index = -1;
                        pane.tabs[tabIndex].is_drives_view = false;
                        pane.tabs[tabIndex].is_archive_view = true;

                        auto tabView = TabViewForPane(paneIndex);
                        if (tabIndex >= 0 && tabIndex < static_cast<int32_t>(pane.tabs.size()))
                        {
                            auto tabItem = tabView.TabItems()
                                .GetAt(static_cast<uint32_t>(tabIndex))
                                .as<winrt::Microsoft::UI::Xaml::Controls::TabViewItem>();
                            tabItem.Header(winrt::box_value(ToHString(pane.tabs[tabIndex].display_name())));
                        }

                        if (m_activePaneIndex == paneIndex)
                        {
                            UpdateBreadcrumbs(absolute);
                            UpdateSearchPlaceholder(absolute);
                        }

                        auto& rows = pane.tab_rows[tabIndex];
                        pane.tab_switching = true;
                        rows.Clear();
                        pane.tab_switching = false;
                        for (auto const& item : pane.items)
                            rows.Append(MakeFileRowViewModel(item));
                        ApplyPaneStateToRows(pane, pane.items, rows, m_iconSourceCache);

                        if (pane.active_list_view)
                        {
                            pane.tab_switching = true;
                            pane.active_list_view.SelectedIndex(-1);
                            pane.tab_switching = false;
                        }

                        if (m_activePaneIndex == paneIndex)
                        {
                            ResetSelectionDetails();
                            StatusTextBlock().Text(ToHString(
                                std::to_wstring(totalItems) +
                                (totalItems == 1 ? L" archive entry in " : L" archive entries in ") +
                                absolute.wstring()));
                        }

                        timing.first_batch_publish_tick = ::GetTickCount64();
                        timing.final_publish_tick = timing.first_batch_publish_tick;
                        pane.last_load_timing = timing;
                        TraceLog(FormatLoadTimingTrace(absolute, totalItems, timing));
                        if (selectAfter.has_value())
                            SelectItemByPath(*selectAfter);
                    });
            }
            else
            {
                // Regular directory: two-batch approach.
                // Enumerate all items on the background thread, then post to the UI thread in
                // two dispatches so the first visible rows appear quickly without a rows.Clear()
                // (CollectionChange::Reset) on the finalize path.
                auto items = m_shellInterop.EnumerateDirectory(absolute);
                timing.enumeration_end_tick = ::GetTickCount64();

                // L1: Sort and pre-build all ViewModels on the background thread.
                // MakeFileRowViewModel sets only hstring fields + bool — no WinUI/STA constraints.
                // The prebuilt objects are handed to the UI thread via lambda capture; after
                // handoff they are never accessed from the background thread again.
                SortItems(items, sort_column, sort_direction);
                std::vector<winrt::FileScoutNative::FileRowViewModel> prebuilt;
                prebuilt.reserve(items.size());
                for (auto const& item : items)
                    prebuilt.push_back(MakeFileRowViewModel(item));

                m_dispatcherQueue.TryEnqueue(
                    [this, lifetime, paneIndex, tabIndex, generation,
                     absolute, selectAfter, timing,
                     items = std::move(items),
                     prebuilt = std::move(prebuilt)]() mutable
                    {
                        auto& pane = m_panes[paneIndex];
                        if (pane.load_generation != generation) return;
                        if (pane.active_tab_index != tabIndex) return;

                        std::size_t const totalItems = prebuilt.size();
                        pane.current_path = absolute;
                        pane.is_drives_view = false;
                        pane.is_archive_view = false;
                        pane.items = items;
                        pane.selection.Clear();

                        pane.tabs[tabIndex].path = absolute;
                        pane.tabs[tabIndex].items = items;
                        pane.tabs[tabIndex].selection = {};
                        pane.tabs[tabIndex].selected_item_index = -1;
                        pane.tabs[tabIndex].is_drives_view = false;
                        pane.tabs[tabIndex].is_archive_view = false;

                        auto tabView = TabViewForPane(paneIndex);
                        if (tabIndex >= 0 && tabIndex < static_cast<int32_t>(pane.tabs.size()))
                        {
                            auto tabItem = tabView.TabItems()
                                .GetAt(static_cast<uint32_t>(tabIndex))
                                .as<winrt::Microsoft::UI::Xaml::Controls::TabViewItem>();
                            tabItem.Header(winrt::box_value(ToHString(pane.tabs[tabIndex].display_name())));
                        }

                        if (m_activePaneIndex == paneIndex)
                        {
                            UpdateBreadcrumbs(absolute);
                            UpdateSearchPlaceholder(absolute);
                        }

                        auto& rows = pane.tab_rows[tabIndex];
                        pane.tab_switching = true;
                        rows.Clear();
                        pane.tab_switching = false;

                        constexpr std::size_t k_first_batch = 150;
                        std::size_t const firstBatchEnd = (std::min)(k_first_batch, totalItems);

                        for (std::size_t i = 0; i < firstBatchEnd; ++i)
                            rows.Append(prebuilt[i]);
                        ApplyPaneStateToRows(pane, pane.items, rows, m_iconSourceCache);

                        timing.first_batch_publish_tick = ::GetTickCount64();
                        pane.user_interacted_after_first_batch = false;

                        if (pane.active_list_view)
                        {
                            pane.tab_switching = true;
                            pane.active_list_view.SelectedIndex(-1);
                            pane.tab_switching = false;
                        }

                        if (m_activePaneIndex == paneIndex)
                        {
                            ResetSelectionDetails();
                            StatusTextBlock().Text(ToHString(
                                std::to_wstring(totalItems) + L" items in " + absolute.wstring()));
                        }

                        if (totalItems <= k_first_batch)
                        {
                            pane.awaiting_final_batch = false;
                            timing.final_publish_tick = timing.first_batch_publish_tick;
                            pane.last_load_timing = timing;
                            TraceLog(FormatLoadTimingTrace(absolute, totalItems, timing));
                            if (selectAfter.has_value())
                                SelectItemByPath(*selectAfter);
                            // P4: backfill real per-extension icons after rows appear.
                            {
                                auto backfillItems = pane.items;
                                for (auto& bi : backfillItems) bi.icon = {};
                                BackfillIconsAsync(paneIndex, tabIndex, generation,
                                    std::move(backfillItems));
                            }
                            return;
                        }

                        // Large folder — stage the remaining rows in a second dispatch so
                        // the UI thread can process the first-batch render before blocking.
                        pane.last_load_timing = timing;
                        m_dispatcherQueue.TryEnqueue(
                            [this, lifetime, paneIndex, tabIndex, generation, absolute, selectAfter,
                             items = std::move(items),
                             prebuilt = std::move(prebuilt),
                             timing]() mutable
                            {
                                auto& pane = m_panes[paneIndex];
                                if (pane.load_generation != generation) return;
                                if (pane.active_tab_index != tabIndex) return;
                                if (!pane.awaiting_final_batch) return;
                                pane.awaiting_final_batch = false;

                                auto& rows = pane.tab_rows[tabIndex];
                                std::size_t const totalItems = prebuilt.size();
                                // L2: single VectorChanged::Reset instead of N ItemInserted events.
                                // ReplaceAll replaces the first-batch rows + all remaining rows in
                                // one call; ListView re-virtualises from scratch, instantiating
                                // DataTemplates only for the visible rows.
                                rows.as<winrt::Windows::Foundation::Collections::IVector<
                                    winrt::FileScoutNative::FileRowViewModel>>()
                                    .ReplaceAll(prebuilt);
                                ApplyPaneStateToRows(pane, items, rows, m_iconSourceCache);

                                timing.final_publish_tick = ::GetTickCount64();
                                pane.last_load_timing = timing;
                                TraceLog(FormatLoadTimingTrace(absolute, totalItems, timing));
                                if (!pane.user_interacted_after_first_batch && selectAfter.has_value())
                                    SelectItemByPath(*selectAfter);
                                // P4: backfill real per-extension icons after all rows appear.
                                {
                                    auto backfillItems = items;
                                    for (auto& bi : backfillItems) bi.icon = {};
                                    BackfillIconsAsync(paneIndex, tabIndex, generation,
                                        std::move(backfillItems));
                                }
                            });
                    });
            }
        }
        catch (...)
        {
            m_dispatcherQueue.TryEnqueue([this, lifetime, paneIndex, generation]()
            {
                if (m_panes[paneIndex].load_generation != generation) return;
                m_panes[paneIndex].awaiting_final_batch = false;
                if (m_activePaneIndex == paneIndex)
                    StatusTextBlock().Text(L"Directory load failed.");
            });
            co_return;
        }
    }

    // S7 / L3: Backfill real per-extension icons for fast-enumerated rows.
    // GetItemIcon runs on the background thread (all items).
    // UI dispatch is chunked at k_iconChunkSize items to avoid one large UI-thread lambda
    // that would block for thousands of WriteableBitmap assignments.
    // m_iconSourceCache (UI-thread-only) ensures one WriteableBitmap per unique extension
    // per session — cache hits skip the BGRA conversion and allocation entirely.
    winrt::fire_and_forget MainWindow::BackfillIconsAsync(
        int32_t const paneIndex,
        int32_t const tabIndex,
        uint32_t const generation,
        std::vector<::FileScoutNative::Domain::ShellItemRef> items)
    {
        auto lifetime = get_strong();

        co_await winrt::resume_background();

        for (auto& item : items)
            item.icon = m_shellInterop.GetItemIcon(item.path, item.is_folder);

        // L3: Chunk UI dispatches at 500 items so the UI thread drains incrementally.
        constexpr uint32_t k_iconChunkSize = 500;
        auto const total = static_cast<uint32_t>(items.size());
        auto shared_items =
            std::make_shared<std::vector<::FileScoutNative::Domain::ShellItemRef>>(
                std::move(items));

        for (uint32_t chunkStart = 0; chunkStart < total; chunkStart += k_iconChunkSize)
        {
            uint32_t const chunkEnd = (std::min)(chunkStart + k_iconChunkSize, total);
            bool const isLastChunk = (chunkEnd == total);

            m_dispatcherQueue.TryEnqueue(
                [this, lifetime, paneIndex, tabIndex, generation,
                 shared_items, chunkStart, chunkEnd, total, isLastChunk]()
                {
                    auto& pane = m_panes[paneIndex];
                    if (pane.load_generation != generation) return;
                    if (pane.active_tab_index != tabIndex) return;

                    auto& rows = pane.tab_rows[tabIndex];
                    if (rows.Size() != total) return;

                    auto const& items_ref = *shared_items;
                    for (uint32_t i = chunkStart; i < chunkEnd; ++i)
                    {
                        auto const& item = items_ref[i];
                        auto const key = ::FileScoutNative::Domain::RowIconCacheKey(item);
                        auto it = m_iconSourceCache.find(key);
                        if (it != m_iconSourceCache.end())
                        {
                            rows.GetAt(i).IconSource(it->second);
                        }
                        else if (auto source = BuildIconSource(item.icon))
                        {
                            m_iconSourceCache.emplace(key, source);
                            rows.GetAt(i).IconSource(source);
                        }
                    }

                    if (isLastChunk)
                        TraceLog(L"BackfillIcons: " + std::to_wstring(total) +
                                 L" rows for pane " + std::to_wstring(paneIndex));
                });
        }
    }

    winrt::fire_and_forget MainWindow::BackfillSearchResultIconsAsync(
        int32_t const paneIndex,
        int32_t const tabIndex,
        uint32_t const searchViewGeneration,
        std::shared_ptr<std::atomic<bool>> cancelToken,
        std::vector<::FileScoutNative::Domain::ShellItemRef> items)
    {
        auto lifetime = get_strong();

        co_await winrt::resume_background();

        for (auto& item : items)
        {
            if (cancelToken && cancelToken->load(std::memory_order_relaxed))
            {
                co_return;
            }

            item.icon = m_shellInterop.GetItemIcon(item.path, item.is_folder);
        }

        // Keep the UI thread responsive during recursive-search row hydration too.
        constexpr uint32_t k_iconChunkSize = 500;
        auto const total = static_cast<uint32_t>(items.size());
        auto shared_items =
            std::make_shared<std::vector<::FileScoutNative::Domain::ShellItemRef>>(
                std::move(items));

        for (uint32_t chunkStart = 0; chunkStart < total; chunkStart += k_iconChunkSize)
        {
            if (cancelToken && cancelToken->load(std::memory_order_relaxed))
            {
                co_return;
            }

            uint32_t const chunkEnd = (std::min)(chunkStart + k_iconChunkSize, total);

            m_dispatcherQueue.TryEnqueue(
                [this, lifetime, paneIndex, tabIndex, searchViewGeneration,
                 cancelToken, shared_items, chunkStart, chunkEnd, total]()
                {
                    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) return;

                    auto& pane = m_panes[paneIndex];
                    if (pane.search_mode != SearchMode::Recursive) return;
                    if (pane.search_view_generation != searchViewGeneration) return;
                    if (pane.active_tab_index != tabIndex) return;

                    auto& rows = pane.tab_rows[tabIndex];
                    if (rows.Size() != total) return;

                    auto const& items_ref = *shared_items;
                    for (uint32_t i = chunkStart; i < chunkEnd; ++i)
                    {
                        auto const& item = items_ref[i];
                        auto const key = ::FileScoutNative::Domain::RowIconCacheKey(item);
                        auto it = m_iconSourceCache.find(key);
                        if (it != m_iconSourceCache.end())
                        {
                            rows.GetAt(i).IconSource(it->second);
                        }
                        else if (auto source = BuildIconSource(item.icon))
                        {
                            m_iconSourceCache.emplace(key, source);
                            rows.GetAt(i).IconSource(source);
                        }
                    }
                });
        }
    }

    uint32_t MainWindow::AdvanceSearchViewGeneration(PaneContext& pane)
    {
        if (pane.search_icon_backfill_cancel)
        {
            pane.search_icon_backfill_cancel->store(true, std::memory_order_relaxed);
        }

        pane.search_icon_backfill_cancel = std::make_shared<std::atomic<bool>>(false);
        return ++pane.search_view_generation;
    }

    void MainWindow::UpdateRows(int32_t const paneIndex,
        std::vector<::FileScoutNative::Domain::ShellItemRef> const& items)
    {
        auto& pane = m_panes[paneIndex];
        auto& rows = pane.tab_rows[pane.active_tab_index];
        rows.Clear();
        for (auto const& item : items)
            rows.Append(MakeFileRowViewModel(item));
    }

    std::vector<int32_t> MainWindow::GetSelectedItemIndices() const
    {
        std::vector<int32_t> indices;
        if (!ActivePane().active_list_view)
        {
            return indices;
        }

        auto const& items = DisplayedItems();

        for (auto const& range : ActivePane().active_list_view.SelectedRanges())
        {
            for (uint32_t offset = 0; offset < range.Length(); ++offset)
            {
                auto const index = range.FirstIndex() + static_cast<int32_t>(offset);
                if (index >= 0 && index < static_cast<int32_t>(items.size()))
                {
                    indices.push_back(index);
                }
            }
        }

        if (indices.empty())
        {
            auto const index = ActivePane().active_list_view.SelectedIndex();
            if (index >= 0 && index < static_cast<int32_t>(items.size()))
            {
                indices.push_back(index);
            }
        }

        std::ranges::sort(indices);
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        return indices;
    }

    std::vector<::FileScoutNative::Domain::ShellItemRef> MainWindow::GetSelectedItems() const
    {
        std::vector<::FileScoutNative::Domain::ShellItemRef> items;
        auto const selectedIndices = GetSelectedItemIndices();
        items.reserve(selectedIndices.size());
        auto const& displayedItems = DisplayedItems();

        for (auto const index : selectedIndices)
        {
            items.push_back(displayedItems[static_cast<std::size_t>(index)]);
        }

        return items;
    }

    void MainWindow::SyncSelectionFromListView()
    {
        auto selectedIndices = GetSelectedItemIndices();
        if (selectedIndices.empty())
        {
            ActivePane().selection.Clear();
            if (ActivePane().active_tab_index >= 0 &&
                ActivePane().active_tab_index < static_cast<int32_t>(ActivePane().tabs.size()))
            {
                ActivePane().tabs[ActivePane().active_tab_index].selection = {};
                ActivePane().tabs[ActivePane().active_tab_index].selected_item_index = -1;
            }
            UpdateRowSelectionVisuals(ActivePane());
            return;
        }

        std::vector<std::wstring> selectedIds;
        selectedIds.reserve(selectedIndices.size());
        auto const& displayedItems = DisplayedItems();
        for (auto const index : selectedIndices)
        {
            selectedIds.push_back(displayedItems[static_cast<std::size_t>(index)].parsing_name);
        }

        std::optional<std::wstring> focusedId;
        auto const focusedIndex = ActivePane().active_list_view.SelectedIndex();
        if (focusedIndex >= 0 && focusedIndex < static_cast<int32_t>(displayedItems.size()))
        {
            focusedId = displayedItems[static_cast<std::size_t>(focusedIndex)].parsing_name;
        }
        else if (!selectedIds.empty())
        {
            focusedId = selectedIds.front();
        }

        ActivePane().selection.ReplaceWith(selectedIds, std::move(focusedId));
        if (ActivePane().active_tab_index >= 0 &&
            ActivePane().active_tab_index < static_cast<int32_t>(ActivePane().tabs.size()))
        {
            ActivePane().tabs[ActivePane().active_tab_index].selection = ActivePane().selection;
            ActivePane().tabs[ActivePane().active_tab_index].selected_item_index =
                focusedIndex >= 0 ? focusedIndex : selectedIndices.front();
        }

        UpdateRowSelectionVisuals(ActivePane());
    }

    void MainWindow::UpdateRowSelectionVisuals(PaneContext& pane)
    {
        if (pane.active_tab_index < 0 ||
            pane.active_tab_index >= static_cast<int32_t>(pane.tab_rows.size()))
        {
            return;
        }

        auto const& rows = pane.tab_rows[static_cast<std::size_t>(pane.active_tab_index)];
        auto const rowCount = rows.Size();
        auto const& displayedItems = DisplayedItems(pane);
        auto const selectedCount = static_cast<uint32_t>(
            (std::min)(displayedItems.size(), static_cast<std::size_t>(rowCount)));

        for (uint32_t index = 0; index < selectedCount; ++index)
        {
            auto const isSelected =
                pane.selection.IsSelected(displayedItems[static_cast<std::size_t>(index)].parsing_name);
            rows.GetAt(index).IsSelected(isSelected);
        }

        for (uint32_t index = selectedCount; index < rowCount; ++index)
        {
            rows.GetAt(index).IsSelected(false);
        }
    }

    void MainWindow::SelectItemByIndex(int32_t const index)
    {
        auto const* item = TryGetDisplayedItemByIndex(index);
        if (item == nullptr)
        {
            ActivePane().selection.Clear();
            UpdateRowSelectionVisuals(ActivePane());
            ResetSelectionDetails();
            return;
        }

        ActivePane().selection.ReplaceWith(item->parsing_name);
        UpdateRowSelectionVisuals(ActivePane());
        UpdateSelectionDetails(*item);

        auto summary = item->kind_label;
        if (item->size_available)
        {
            summary += L"  |  " + FormatSize(item->size_bytes);
        }
        summary = AppendArchiveBrowseHint(std::move(summary), *item);

        StatusTextBlock().Text(ToHString(L"Selected " + item->display_name + L"  |  " + summary));
    }

    void MainWindow::SelectAllDisplayedItems()
    {
        auto& pane = ActivePane();
        auto const& displayedItems = DisplayedItems();
        if (!pane.active_list_view)
        {
            StatusTextBlock().Text(L"Select All needs an active file list.");
            return;
        }

        if (displayedItems.empty())
        {
            pane.selection.Clear();
            UpdateRowSelectionVisuals(pane);
            ResetSelectionDetails();
            StatusTextBlock().Text(L"No items to select.");
            return;
        }

        pane.tab_switching = true;
        pane.active_list_view.SelectedIndex(-1);
        pane.active_list_view.SelectRange(winrt::Microsoft::UI::Xaml::Data::ItemIndexRange(
            0,
            static_cast<uint32_t>(displayedItems.size())));
        pane.tab_switching = false;

        SyncSelectionFromListView();
        auto const selectedIndices = GetSelectedItemIndices();
        if (selectedIndices.size() == 1)
        {
            SelectItemByIndex(selectedIndices.front());
        }
        else
        {
            UpdateMultiSelectionDetails(selectedIndices);
        }

        pane.active_list_view.Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::UpdateMultiSelectionDetails(std::vector<int32_t> const& indices)
    {
        auto const itemCount = indices.size();
        auto const& displayedItems = DisplayedItems();
        auto const folderCount = static_cast<std::size_t>(std::count_if(
            indices.begin(),
            indices.end(),
            [&displayedItems](int32_t const index)
            {
                return displayedItems[static_cast<std::size_t>(index)].is_folder;
            }));
        auto const fileCount = itemCount - folderCount;

        ++m_previewGeneration;
        PreviewBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        PreviewImage().Source(nullptr);
        SelectionIconBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        SelectionIconImage().Source(nullptr);
        SelectionTitleTextBlock().Text(ToHString(
            std::to_wstring(itemCount) + (itemCount == 1 ? L" item selected" : L" items selected")));

        std::wstring metadata;
        if (fileCount > 0)
        {
            metadata += std::to_wstring(fileCount) + (fileCount == 1 ? L" file" : L" files");
        }
        if (folderCount > 0)
        {
            if (!metadata.empty())
            {
                metadata += L"  |  ";
            }
            metadata += std::to_wstring(folderCount) + (folderCount == 1 ? L" folder" : L" folders");
        }
        metadata += L"  |  Rename works one item at a time in the closed beta";
        SelectionMetaTextBlock().Text(ToHString(metadata));

        SelectionCreatedTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        SelectionAttributesTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        SelectionPathTextBlock().Text(ToHString(ActivePane().current_path.wstring()));
        StatusTextBlock().Text(ToHString(
            std::to_wstring(itemCount) + (itemCount == 1 ? L" item selected." : L" items selected.")));
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
        SelectionMetaTextBlock().Text(L"Choose a folder or file to inspect its details, dates, and path.");
        TraceLog(L"ResetSelectionDetails: SelectionCreatedTextBlock...");
        SelectionCreatedTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        TraceLog(L"ResetSelectionDetails: SelectionAttributesTextBlock...");
        SelectionAttributesTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        TraceLog(L"ResetSelectionDetails: SelectionPathTextBlock...");
        SelectionPathTextBlock().Text(L"Selection details follow the active pane.");
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
        metadata = AppendArchiveBrowseHint(std::move(metadata), item);
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
                if (!attrStr.empty()) { attrStr += L"  |  "; }
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

    std::vector<::FileScoutNative::Domain::ShellItemRef> const& MainWindow::DisplayedItems(
        PaneContext const& pane) const
    {
        if (pane.search_mode == SearchMode::Filter ||
            pane.search_mode == SearchMode::Recursive)
        {
            return pane.filtered_items;
        }

        return pane.items;
    }

    std::vector<::FileScoutNative::Domain::ShellItemRef> const& MainWindow::DisplayedItems() const
    {
        return DisplayedItems(m_panes[static_cast<std::size_t>(m_activePaneIndex)]);
    }

    ::FileScoutNative::Domain::ShellItemRef const* MainWindow::TryGetDisplayedItemByIndex(
        int32_t const index) const
    {
        auto const& items = DisplayedItems();
        if (index < 0 || index >= static_cast<int32_t>(items.size()))
        {
            return nullptr;
        }

        return &items[static_cast<std::size_t>(index)];
    }

    bool MainWindow::ShouldSkipPaneContextMenu(
        winrt::Windows::Foundation::IInspectable const& source) const
    {
        return HasAncestorOfType<winrt::Microsoft::UI::Xaml::Controls::Button>(source);
    }

    void MainWindow::ShowContextMenu(
        ::FileScoutNative::Domain::ContextMenuModel const& model,
        std::optional<int32_t> const itemIndex,
        winrt::Microsoft::UI::Xaml::FrameworkElement const& target,
        winrt::Windows::Foundation::Point const tapPoint)
    {
        if (m_contextMenu)
        {
            m_contextMenu.Hide();
            m_contextMenu = nullptr;
        }

        auto flyout = winrt::Microsoft::UI::Xaml::Controls::MenuFlyout{};
        bool hasAppendedItems = AppendContextCommands(flyout, model.primary_commands, itemIndex);
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

            auto const appendedGroupItems = AppendContextCommands(flyout, group.commands, itemIndex);
            if (appendedGroupItems)
            {
                hasAppendedItems = true;
            }
            else if (hasAppendedItems)
            {
                flyout.Items().RemoveAtEnd();
            }
        }

        if (!hasAppendedItems)
        {
            return;
        }

        m_contextMenu = flyout;
        flyout.Closed([this](auto const&, auto const&)
        {
            m_contextMenu = nullptr;
        });
        winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowOptions options;
        options.Position(tapPoint);
        options.ShowMode(winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowMode::Transient);
        m_contextMenu.ShowAt(target, options);
    }

    bool MainWindow::AppendContextCommands(
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout const& flyout,
        std::vector<::FileScoutNative::Domain::ExplorerCommand> const& commands,
        std::optional<int32_t> const itemIndex)
    {
        bool appendedAny = false;
        auto const selectionCount = GetSelectedItems().size();

        for (auto const& command : commands)
        {
            if (!::FileScoutNative::Domain::ShouldShowWave1ContextCommand(
                    command.id,
                    selectionCount,
                    ActivePane().is_archive_view))
            {
                continue;
            }

            auto flyoutItem = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
            flyoutItem.Text(ToHString(command.label));
            flyoutItem.IsEnabled(command.available);

            auto const glyph = GlyphForMenuIconKey(command.icon_key);
            if (!glyph.empty())
            {
                auto icon = winrt::Microsoft::UI::Xaml::Controls::FontIcon{};
                icon.Glyph(winrt::hstring{ glyph });
                icon.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe Fluent Icons"));
                icon.FontSize(14.0);
                flyoutItem.Icon(icon);
            }

            if (command.is_destructive)
            {
                flyoutItem.Foreground(LookupBrush(L"SystemFillColorCriticalBrush"));
            }

            auto const commandId = command.id;
            flyoutItem.Click([this, commandId, itemIndex](
                                 winrt::Windows::Foundation::IInspectable const&,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
                {
                    ExecuteContextCommand(commandId, itemIndex);
                });

            flyout.Items().Append(flyoutItem);
            appendedAny = true;
        }

        return appendedAny;
    }

    void MainWindow::ExecuteContextCommand(
        std::wstring const& commandId,
        std::optional<int32_t> const itemIndex)
    {
        auto const selectedItems = GetSelectedItems();
        std::optional<::FileScoutNative::Domain::ShellItemRef> item;
        if (itemIndex.has_value())
        {
            if (auto const* displayedItem = TryGetDisplayedItemByIndex(*itemIndex))
            {
                item = *displayedItem;
            }
        }
        else if (selectedItems.size() == 1)
        {
            item = selectedItems.front();
        }

        auto const archiveReadOnlyCommand =
            commandId == L"openWith" ||
            commandId == L"rename" ||
            commandId == L"delete" ||
            commandId == L"copy" ||
            commandId == L"cut" ||
            commandId == L"paste" ||
            commandId == L"properties" ||
            commandId == L"moreWindowsActions" ||
            commandId == L"newFolder";

        if (ActivePane().is_archive_view && archiveReadOnlyCommand)
        {
            StatusTextBlock().Text(L"Archive mode is read-only in this slice.");
            return;
        }

        if (commandId == L"open")
        {
            if (selectedItems.size() > 1)
            {
                StatusTextBlock().Text(L"Open needs a single selected item.");
                return;
            }

            if (!(itemIndex.has_value()
                    ? ActivateDisplayedItemByIndex(itemIndex)
                    : ActivateCurrentSelection()))
            {
                StatusTextBlock().Text(L"Open needs a selected item.");
            }
            return;
        }

        if (commandId == L"pinToPlaces")
        {
            std::filesystem::path targetPath;
            if (item.has_value() && item->is_folder && !item->path.empty())
            {
                targetPath = item->path;
            }
            else if (!ActivePane().current_path.empty())
            {
                targetPath = ActivePane().current_path;
            }

            if (targetPath.empty())
            {
                StatusTextBlock().Text(L"Pin to Bookmarks needs a folder.");
                return;
            }

            if (m_favoritesService.IsSidebarFavorite(targetPath))
            {
                StatusTextBlock().Text(ToHString(
                    targetPath.filename().wstring().empty()
                        ? L"That folder is already in Bookmarks or Quick Access."
                        : targetPath.filename().wstring() + L" is already in Bookmarks."));
                return;
            }

            if (m_favoritesService.AddPinnedFavorite(targetPath))
            {
                BuildSidebarFavorites();
                auto const label = targetPath.filename().wstring().empty()
                    ? targetPath.wstring()
                    : targetPath.filename().wstring();
                StatusTextBlock().Text(ToHString(L"Pinned " + label + L" to Bookmarks."));
            }
            else
            {
                StatusTextBlock().Text(L"Pin to Bookmarks failed.");
            }
            return;
        }

        if (commandId == L"refresh")
        {
            LoadDirectory(ActivePane().current_path);
            return;
        }

        if (commandId == L"selectAll")
        {
            SelectAllDisplayedItems();
            return;
        }

        if (commandId == L"extractToArchiveFolder" || commandId == L"extractReveal")
        {
            if (!::FileScoutNative::Domain::IsWave1FeatureEnabled(
                    ::FileScoutNative::Domain::Wave1Feature::ArchiveBrowsing))
            {
                StatusTextBlock().Text(ToHString(
                    BuildWave1FeatureStatus(::FileScoutNative::Domain::Wave1Feature::ArchiveBrowsing)));
                return;
            }

            if (!item.has_value())
            {
                StatusTextBlock().Text(L"Extract needs a selected archive item.");
                return;
            }

            if (!ActivePane().is_archive_view)
            {
                StatusTextBlock().Text(L"Extract is only available inside archive views.");
                return;
            }

            ExtractArchiveItemAsync(*item, commandId == L"extractReveal");
            return;
        }

        if (commandId == L"openWith")
        {
            if (!item.has_value() || selectedItems.size() > 1)
            {
                StatusTextBlock().Text(L"Open With needs a single selected item.");
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
            if (selectedItems.empty())
            {
                StatusTextBlock().Text(L"Rename needs a selected item.");
                return;
            }

            if (selectedItems.size() > 1 &&
                !::FileScoutNative::Domain::IsWave1FeatureEnabled(
                    ::FileScoutNative::Domain::Wave1Feature::BatchRename))
            {
                StatusTextBlock().Text(ToHString(
                    BuildWave1FeatureStatus(::FileScoutNative::Domain::Wave1Feature::BatchRename)));
                return;
            }

            if (selectedItems.size() == 1)
            {
                ShowRenameDialogAsync(selectedItems.front());
            }
            else
            {
                ShowBatchRenameDialogAsync(selectedItems);
            }
            return;
        }

        if (commandId == L"delete")
        {
            if (selectedItems.empty())
            {
                StatusTextBlock().Text(L"Delete needs a selected item.");
                return;
            }

            if (selectedItems.size() == 1)
            {
                ShowDeleteDialogAsync(selectedItems.front());
            }
            else
            {
                ShowDeleteDialogAsync(selectedItems);
            }
            return;
        }

        if (commandId == L"copy")
        {
            if (selectedItems.empty())
            {
                StatusTextBlock().Text(L"Copy needs a selected item.");
                return;
            }

            try
            {
                m_shellClipboardService.CopyShellItems(selectedItems);
                StatusTextBlock().Text(ToHString(
                    L"Copied " + std::to_wstring(selectedItems.size()) +
                    (selectedItems.size() == 1 ? L" item." : L" items.")));
            }
            catch (std::exception const&)
            {
                StatusTextBlock().Text(ToHString(
                    L"Copy failed for " + std::to_wstring(selectedItems.size()) +
                    (selectedItems.size() == 1 ? L" item." : L" items.")));
            }
            return;
        }

        if (commandId == L"cut")
        {
            if (selectedItems.empty())
            {
                StatusTextBlock().Text(L"Cut needs a selected item.");
                return;
            }

            try
            {
                m_shellClipboardService.CutShellItems(selectedItems);
                StatusTextBlock().Text(ToHString(
                    L"Cut " + std::to_wstring(selectedItems.size()) +
                    (selectedItems.size() == 1 ? L" item." : L" items.")));
            }
            catch (std::exception const&)
            {
                StatusTextBlock().Text(ToHString(
                    L"Cut failed for " + std::to_wstring(selectedItems.size()) +
                    (selectedItems.size() == 1 ? L" item." : L" items.")));
            }
            return;
        }

        if (commandId == L"paste")
        {
            try
            {
                auto const outcome = m_shellClipboardService.PasteInto(
                    GetWindowHandle(), ActivePane().current_path);
                if (outcome.succeeded)
                {
                    LoadDirectory(ActivePane().current_path);
                    StatusTextBlock().Text(ToHString(BuildPasteSuccessStatus(outcome)));
                }
                else
                {
                    StatusTextBlock().Text(ToHString(BuildPasteFailureStatus(outcome)));
                }
            }
            catch (std::exception const& error)
            {
                StatusTextBlock().Text(ToHString(
                    L"Couldn't paste into " + ActivePane().current_path.wstring() + L". " +
                    winrt::to_hstring(error.what()).c_str()));
            }
            return;
        }

        if (commandId == L"properties")
        {
            if (!item.has_value() || selectedItems.size() > 1)
            {
                StatusTextBlock().Text(L"Properties needs a single selected item.");
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
            if (!item.has_value() || selectedItems.size() > 1)
            {
                StatusTextBlock().Text(L"Windows overflow menu needs a single selected item.");
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

        if (commandId == L"back")
        {
            NavigateBack();
            return;
        }

        if (commandId == L"forward")
        {
            NavigateForward();
            return;
        }

        if (commandId == L"goToPath")
        {
            ShowAddressTextBox();
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
            if (!m_dualPaneVisible)
            {
                StatusTextBlock().Text(L"Turn on Dual pane before switching panes.");
                return;
            }

            SetActivePaneIndex(1 - m_activePaneIndex);
            return;
        }

        if (commandId == L"toggleView")
        {
            auto const newMode = (ActivePane().view_mode == ViewMode::List)
                ? ViewMode::Cards
                : ViewMode::List;
            SetViewMode(m_activePaneIndex, newMode);
            return;
        }

        if (commandId == L"toggleGroup")
        {
            SetGroupingEnabled(m_activePaneIndex, !ActivePane().group_by_sort_column);
            return;
        }

        if (commandId == L"sortByName")
        {
            SetSortColumn(m_activePaneIndex, SortColumn::Name);
            return;
        }

        if (commandId == L"sortBySize")
        {
            SetSortColumn(m_activePaneIndex, SortColumn::Size);
            return;
        }

        if (commandId == L"sortByDateModified")
        {
            SetSortColumn(m_activePaneIndex, SortColumn::DateModified);
            return;
        }

        if (commandId == L"togglePreview")
        {
            TogglePreviewPanel();
            return;
        }

        if (commandId == L"toggleDualPane")
        {
            ToggleDualPane();
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
        if (auto const error = ::FileScoutNative::Domain::ValidateWindowsFileName(folderName); error.has_value())
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
            LoadDirectory(ActivePane().current_path, createdPath);
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
        if (auto const error = ::FileScoutNative::Domain::ValidateWindowsFileName(newName); error.has_value())
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
            LoadDirectory(ActivePane().current_path, renamedPath);
            StatusTextBlock().Text(ToHString(L"Renamed to " + renamedPath.filename().wstring()));
        }
        catch (::FileScoutNative::Shell::FileOperationException const& error)
        {
            StatusTextBlock().Text(ToHString(BuildRenameFailureStatus(item, error)));
        }
        catch (std::exception const& error)
        {
            StatusTextBlock().Text(ToHString(
                L"Couldn't rename " + item.display_name + L". " +
                winrt::to_hstring(error.what()).c_str()));
        }
    }

    winrt::fire_and_forget MainWindow::ShowBatchRenameDialogAsync(
        std::vector<::FileScoutNative::Domain::ShellItemRef> items)
    {
        auto lifetime = get_strong();
        auto const siblingItems = ActivePane().items;
        auto const selectionSummary = BuildBatchRenameSelectionSummary(items);
        auto const selectionExamples = BuildBatchRenameSelectionExamples(items);
        auto const currentFolder = ActivePane().current_path.empty()
            ? std::wstring{ L"This PC" }
            : ActivePane().current_path.wstring();

        auto presetBox = winrt::Microsoft::UI::Xaml::Controls::ComboBox{};
        presetBox.Header(winrt::box_value(L"Preset"));
        presetBox.Items().Append(winrt::box_value(L"Sequential numbers"));
        presetBox.Items().Append(winrt::box_value(L"Add prefix"));
        presetBox.Items().Append(winrt::box_value(L"Add suffix"));
        presetBox.Items().Append(winrt::box_value(L"Find and replace"));
        presetBox.Items().Append(winrt::box_value(L"Change case"));
        presetBox.SelectedIndex(0);

        auto baseNameBox = winrt::Microsoft::UI::Xaml::Controls::TextBox{};
        baseNameBox.Header(winrt::box_value(L"Base name"));
        baseNameBox.Text(ToHString(SuggestBatchRenameBaseName(ActivePane().current_path)));
        baseNameBox.SelectAll();

        auto prefixBox = winrt::Microsoft::UI::Xaml::Controls::TextBox{};
        prefixBox.Header(winrt::box_value(L"Prefix"));
        prefixBox.PlaceholderText(L"Example: Project - ");
        prefixBox.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

        auto suffixBox = winrt::Microsoft::UI::Xaml::Controls::TextBox{};
        suffixBox.Header(winrt::box_value(L"Suffix"));
        suffixBox.PlaceholderText(L"Example:  (edited)");
        suffixBox.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

        auto findBox = winrt::Microsoft::UI::Xaml::Controls::TextBox{};
        findBox.Header(winrt::box_value(L"Find"));
        findBox.PlaceholderText(L"Literal text");
        findBox.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

        auto replaceBox = winrt::Microsoft::UI::Xaml::Controls::TextBox{};
        replaceBox.Header(winrt::box_value(L"Replace with"));
        replaceBox.PlaceholderText(L"Leave blank to remove the text");
        replaceBox.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

        auto caseModeBox = winrt::Microsoft::UI::Xaml::Controls::ComboBox{};
        caseModeBox.Header(winrt::box_value(L"Case"));
        caseModeBox.Items().Append(winrt::box_value(L"lowercase"));
        caseModeBox.Items().Append(winrt::box_value(L"UPPERCASE"));
        caseModeBox.Items().Append(winrt::box_value(L"Title Case"));
        caseModeBox.SelectedIndex(2);
        caseModeBox.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

        auto startBox = winrt::Microsoft::UI::Xaml::Controls::TextBox{};
        startBox.Header(winrt::box_value(L"Start number"));
        startBox.Text(L"1");

        auto paddingBox = winrt::Microsoft::UI::Xaml::Controls::TextBox{};
        paddingBox.Header(winrt::box_value(L"Zero padding"));
        paddingBox.PlaceholderText(L"Auto");

        auto note = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        note.Text(ToHString(BatchRenamePresetNote(BatchRenamePreset::Sequential)));
        note.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::WrapWholeWords);
        note.Opacity(0.72);

        auto summaryLabel = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        summaryLabel.Text(ToHString(selectionSummary));
        summaryLabel.FontSize(18);
        summaryLabel.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());

        auto locationLabel = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        locationLabel.Text(ToHString(std::wstring{ L"Folder: " } + currentFolder));
        locationLabel.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::WrapWholeWords);
        locationLabel.Opacity(0.72);

        auto examplesLabel = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        examplesLabel.Text(ToHString(selectionExamples));
        examplesLabel.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::WrapWholeWords);
        examplesLabel.Opacity(0.72);

        auto summaryStack = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
        summaryStack.Spacing(6);
        summaryStack.Children().Append(summaryLabel);
        summaryStack.Children().Append(locationLabel);
        summaryStack.Children().Append(examplesLabel);

        auto summaryBorder = winrt::Microsoft::UI::Xaml::Controls::Border{};
        summaryBorder.Padding({ 18, 16, 18, 16 });
        summaryBorder.CornerRadius({ 16, 16, 16, 16 });
        summaryBorder.BorderThickness({ 1, 1, 1, 1 });
        summaryBorder.BorderBrush(LookupBrush(L"CardStrokeColorDefaultBrush"));
        summaryBorder.Background(LookupBrush(L"LayerFillColorAltBrush"));
        summaryBorder.Child(summaryStack);

        auto controlsHeading = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        controlsHeading.Text(L"Rename setup");
        controlsHeading.FontSize(16);
        controlsHeading.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());

        auto controlsSubheading = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        controlsSubheading.Text(L"Pick one preset and adjust only the fields that apply to that mode.");
        controlsSubheading.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::WrapWholeWords);
        controlsSubheading.Opacity(0.68);

        auto noteBorder = winrt::Microsoft::UI::Xaml::Controls::Border{};
        noteBorder.Padding({ 12, 10, 12, 10 });
        noteBorder.CornerRadius({ 12, 12, 12, 12 });
        noteBorder.Background(LookupBrush(L"SubtleFillColorSecondaryBrush"));
        noteBorder.Child(note);

        auto controlsStack = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
        controlsStack.Spacing(12);
        controlsStack.Children().Append(controlsHeading);
        controlsStack.Children().Append(controlsSubheading);
        controlsStack.Children().Append(presetBox);
        controlsStack.Children().Append(baseNameBox);
        controlsStack.Children().Append(prefixBox);
        controlsStack.Children().Append(suffixBox);
        controlsStack.Children().Append(findBox);
        controlsStack.Children().Append(replaceBox);
        controlsStack.Children().Append(caseModeBox);
        controlsStack.Children().Append(startBox);
        controlsStack.Children().Append(paddingBox);
        controlsStack.Children().Append(noteBorder);

        auto controlsBorder = winrt::Microsoft::UI::Xaml::Controls::Border{};
        controlsBorder.Padding({ 18, 18, 18, 18 });
        controlsBorder.CornerRadius({ 16, 16, 16, 16 });
        controlsBorder.BorderThickness({ 1, 1, 1, 1 });
        controlsBorder.BorderBrush(LookupBrush(L"CardStrokeColorDefaultBrush"));
        controlsBorder.Background(LookupBrush(L"CardBackgroundFillColorSecondaryBrush"));
        controlsBorder.Child(controlsStack);

        auto previewHeading = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        previewHeading.Text(L"Live preview");
        previewHeading.FontSize(16);
        previewHeading.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());

        auto previewSubheading = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        previewSubheading.Text(L"Review the first few generated names before committing the shell rename.");
        previewSubheading.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::WrapWholeWords);
        previewSubheading.Opacity(0.68);

        auto previewSummaryText = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        previewSummaryText.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::WrapWholeWords);
        previewSummaryText.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());

        auto previewErrorText = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        previewErrorText.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::WrapWholeWords);
        previewErrorText.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
            winrt::Windows::UI::Color{ 0xFF, 0xC4, 0x2B, 0x1C }));
        previewErrorText.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

        auto previewRows = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
        previewRows.Spacing(8);

        auto previewScroll = winrt::Microsoft::UI::Xaml::Controls::ScrollViewer{};
        previewScroll.VerticalScrollBarVisibility(
            winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Auto);
        previewScroll.HorizontalScrollBarVisibility(
            winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Disabled);
        previewScroll.MaxHeight(280);
        previewScroll.Content(previewRows);

        auto previewStack = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
        previewStack.Spacing(12);
        previewStack.Children().Append(previewHeading);
        previewStack.Children().Append(previewSubheading);
        previewStack.Children().Append(previewSummaryText);
        previewStack.Children().Append(previewErrorText);
        previewStack.Children().Append(previewScroll);

        auto previewBorder = winrt::Microsoft::UI::Xaml::Controls::Border{};
        previewBorder.Padding({ 18, 18, 18, 18 });
        previewBorder.CornerRadius({ 16, 16, 16, 16 });
        previewBorder.BorderThickness({ 1, 1, 1, 1 });
        previewBorder.BorderBrush(LookupBrush(L"CardStrokeColorDefaultBrush"));
        previewBorder.Background(LookupBrush(L"CardBackgroundFillColorDefaultBrush"));
        previewBorder.Child(previewStack);

        auto bodyGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
        bodyGrid.ColumnSpacing(16);
        auto bodyLeft = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
        bodyLeft.Width({ 1, winrt::Microsoft::UI::Xaml::GridUnitType::Star });
        auto bodyRight = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
        bodyRight.Width({ 1, winrt::Microsoft::UI::Xaml::GridUnitType::Star });
        bodyGrid.ColumnDefinitions().Append(bodyLeft);
        bodyGrid.ColumnDefinitions().Append(bodyRight);
        winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(controlsBorder, 0);
        winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(previewBorder, 1);
        bodyGrid.Children().Append(controlsBorder);
        bodyGrid.Children().Append(previewBorder);

        auto contentRoot = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
        contentRoot.Spacing(16);
        contentRoot.Width(760);
        contentRoot.Children().Append(summaryBorder);
        contentRoot.Children().Append(bodyGrid);

        auto contentScroll = winrt::Microsoft::UI::Xaml::Controls::ScrollViewer{};
        contentScroll.VerticalScrollBarVisibility(
            winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Auto);
        contentScroll.HorizontalScrollBarVisibility(
            winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Disabled);
        contentScroll.MaxHeight(640);
        contentScroll.Content(contentRoot);

        auto dialog = winrt::Microsoft::UI::Xaml::Controls::ContentDialog{};
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(L"Batch rename"));
        dialog.PrimaryButtonText(ToHString(
            std::wstring{ L"Rename " } + std::to_wstring(items.size()) +
            (items.size() == 1 ? L" item" : L" items")));
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
        dialog.Content(contentScroll);

        auto const buildPlan =
            [this, items, siblingItems, presetBox, baseNameBox, prefixBox, suffixBox, findBox, replaceBox, caseModeBox, startBox, paddingBox]()
            -> ::FileScoutNative::Domain::BatchRenamePlan
        {
            auto const selectedPreset =
                BatchRenamePresetFromSelectedIndex(presetBox.SelectedIndex());
            switch (selectedPreset)
            {
            case BatchRenamePreset::AddPrefix:
                return ::FileScoutNative::Domain::BuildPrefixBatchRenamePlan(
                    std::span<::FileScoutNative::Domain::ShellItemRef const>{ items.data(), items.size() },
                    std::span<::FileScoutNative::Domain::ShellItemRef const>{
                        siblingItems.data(),
                        siblingItems.size(),
                    },
                    std::wstring{ prefixBox.Text().c_str() });
            case BatchRenamePreset::AddSuffix:
                return ::FileScoutNative::Domain::BuildSuffixBatchRenamePlan(
                    std::span<::FileScoutNative::Domain::ShellItemRef const>{ items.data(), items.size() },
                    std::span<::FileScoutNative::Domain::ShellItemRef const>{
                        siblingItems.data(),
                        siblingItems.size(),
                    },
                    std::wstring{ suffixBox.Text().c_str() });
            case BatchRenamePreset::FindReplace:
                return ::FileScoutNative::Domain::BuildFindReplaceBatchRenamePlan(
                    std::span<::FileScoutNative::Domain::ShellItemRef const>{ items.data(), items.size() },
                    std::span<::FileScoutNative::Domain::ShellItemRef const>{
                        siblingItems.data(),
                        siblingItems.size(),
                    },
                    std::wstring{ findBox.Text().c_str() },
                    std::wstring{ replaceBox.Text().c_str() });
            case BatchRenamePreset::ChangeCase:
                return ::FileScoutNative::Domain::BuildCaseTransformBatchRenamePlan(
                    std::span<::FileScoutNative::Domain::ShellItemRef const>{ items.data(), items.size() },
                    std::span<::FileScoutNative::Domain::ShellItemRef const>{
                        siblingItems.data(),
                        siblingItems.size(),
                    },
                    BatchRenameCaseTransformFromSelectedIndex(caseModeBox.SelectedIndex()));
            case BatchRenamePreset::Sequential:
            default:
                break;
            }

            auto const startNumber = TryParsePositiveInteger(startBox.Text().c_str());
            if (!startNumber.has_value())
            {
                return ::FileScoutNative::Domain::BatchRenamePlan{
                    .error = std::wstring{ L"Start number must be a positive integer." },
                };
            }

            uint32_t paddingWidth = 0;
            if (!TrimFileName(paddingBox.Text().c_str()).empty())
            {
                auto const parsedPadding = TryParsePositiveInteger(paddingBox.Text().c_str());
                if (!parsedPadding.has_value())
                {
                    return ::FileScoutNative::Domain::BatchRenamePlan{
                        .error = std::wstring{ L"Zero padding must be blank or a positive integer." },
                    };
                }

                paddingWidth = *parsedPadding;
            }

            return ::FileScoutNative::Domain::BuildSequentialBatchRenamePlan(
                std::span<::FileScoutNative::Domain::ShellItemRef const>{ items.data(), items.size() },
                std::span<::FileScoutNative::Domain::ShellItemRef const>{
                    siblingItems.data(),
                    siblingItems.size(),
                },
                ::FileScoutNative::Domain::BatchRenameOptions{
                    .base_name = TrimFileName(baseNameBox.Text().c_str()),
                    .start_number = *startNumber,
                    .padding_width = paddingWidth,
                });
        };

        auto const refreshPreview =
            [dialog, presetBox, baseNameBox, prefixBox, suffixBox, findBox, replaceBox, caseModeBox, startBox, paddingBox, note, previewSummaryText, previewErrorText, previewRows, buildPlan]()
        {
            auto const selectedPreset =
                BatchRenamePresetFromSelectedIndex(presetBox.SelectedIndex());
            auto const sequentialVisibility =
                selectedPreset == BatchRenamePreset::Sequential
                    ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                    : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
            auto const prefixVisibility =
                selectedPreset == BatchRenamePreset::AddPrefix
                    ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                    : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
            auto const suffixVisibility =
                selectedPreset == BatchRenamePreset::AddSuffix
                    ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                    : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
            auto const findReplaceVisibility =
                selectedPreset == BatchRenamePreset::FindReplace
                    ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                    : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
            auto const caseTransformVisibility =
                selectedPreset == BatchRenamePreset::ChangeCase
                    ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                    : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

            baseNameBox.Visibility(sequentialVisibility);
            startBox.Visibility(sequentialVisibility);
            paddingBox.Visibility(sequentialVisibility);
            prefixBox.Visibility(prefixVisibility);
            suffixBox.Visibility(suffixVisibility);
            findBox.Visibility(findReplaceVisibility);
            replaceBox.Visibility(findReplaceVisibility);
            caseModeBox.Visibility(caseTransformVisibility);
            note.Text(ToHString(BatchRenamePresetNote(selectedPreset)));
            previewRows.Children().Clear();

            auto const plan = buildPlan();
            if (plan.error.has_value())
            {
                dialog.IsPrimaryButtonEnabled(false);
                previewSummaryText.Text(ToHString(
                    BatchRenamePresetLabel(selectedPreset) + L" is not ready yet."));
                previewErrorText.Text(ToHString(*plan.error));
                previewErrorText.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
                return;
            }

            dialog.IsPrimaryButtonEnabled(true);
            previewErrorText.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            auto const changedCount = std::count_if(
                plan.entries.begin(),
                plan.entries.end(),
                [](auto const& entry)
                {
                    return entry.target_name != entry.source_item.display_name;
                });
            auto const unchangedCount = plan.entries.size() - static_cast<std::size_t>(changedCount);
            std::wstring summary = BatchRenamePresetLabel(selectedPreset) + L"  |  " +
                std::to_wstring(changedCount) + (changedCount == 1 ? L" change ready" : L" changes ready");
            if (unchangedCount > 0)
            {
                summary += L"  |  " + std::to_wstring(unchangedCount) +
                    (unchangedCount == 1 ? L" item unchanged" : L" items unchanged");
            }
            previewSummaryText.Text(ToHString(summary));

            auto const previewCount = std::min<std::size_t>(plan.entries.size(), 5);
            for (std::size_t index = 0; index < previewCount; ++index)
            {
                auto const& entry = plan.entries[index];
                auto rowStack = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
                rowStack.Spacing(4);

                auto sourceText = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
                sourceText.Text(ToHString(entry.source_item.display_name));
                sourceText.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::WrapWholeWords);
                sourceText.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());

                auto targetText = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
                auto const changed = entry.target_name != entry.source_item.display_name;
                targetText.Text(ToHString(
                    changed ? std::wstring{ L"Rename to " } + entry.target_name
                            : std::wstring{ L"No change for this item" }));
                targetText.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::WrapWholeWords);
                targetText.Opacity(changed ? 0.82 : 0.6);

                rowStack.Children().Append(sourceText);
                rowStack.Children().Append(targetText);

                auto rowBorder = winrt::Microsoft::UI::Xaml::Controls::Border{};
                rowBorder.Padding({ 12, 10, 12, 10 });
                rowBorder.CornerRadius({ 12, 12, 12, 12 });
                rowBorder.Background(LookupBrush(
                    changed ? L"SubtleFillColorSecondaryBrush" : L"CardBackgroundFillColorSecondaryBrush"));
                rowBorder.Child(rowStack);
                previewRows.Children().Append(rowBorder);
            }

            if (plan.entries.size() > previewCount)
            {
                auto moreText = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
                moreText.Text(ToHString(
                    std::wstring{ L"+" } + std::to_wstring(plan.entries.size() - previewCount) +
                    L" more items follow the same preset logic."));
                moreText.Opacity(0.68);
                moreText.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::WrapWholeWords);
                previewRows.Children().Append(moreText);
            }
        };

        presetBox.SelectionChanged([refreshPreview](auto const&, auto const&) { refreshPreview(); });
        baseNameBox.TextChanged([refreshPreview](auto const&, auto const&) { refreshPreview(); });
        prefixBox.TextChanged([refreshPreview](auto const&, auto const&) { refreshPreview(); });
        suffixBox.TextChanged([refreshPreview](auto const&, auto const&) { refreshPreview(); });
        findBox.TextChanged([refreshPreview](auto const&, auto const&) { refreshPreview(); });
        replaceBox.TextChanged([refreshPreview](auto const&, auto const&) { refreshPreview(); });
        caseModeBox.SelectionChanged([refreshPreview](auto const&, auto const&) { refreshPreview(); });
        startBox.TextChanged([refreshPreview](auto const&, auto const&) { refreshPreview(); });
        paddingBox.TextChanged([refreshPreview](auto const&, auto const&) { refreshPreview(); });
        refreshPreview();

        if (co_await dialog.ShowAsync() != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto const plan = buildPlan();
        if (plan.error.has_value())
        {
            StatusTextBlock().Text(ToHString(*plan.error));
            co_return;
        }

        std::vector<::FileScoutNative::Shell::RenameRequest> requests;
        requests.reserve(plan.entries.size());
        for (auto const& entry : plan.entries)
        {
            if (entry.target_name == entry.source_item.display_name)
            {
                continue;
            }

            requests.push_back(::FileScoutNative::Shell::RenameRequest{
                .path = entry.source_item.path,
                .new_name = entry.target_name,
            });
        }

        if (requests.empty())
        {
            StatusTextBlock().Text(L"Batch rename skipped: no names changed.");
            co_return;
        }

        try
        {
            auto const renamedPaths = m_shellFileOperationService.RenameItems(
                GetWindowHandle(),
                std::span<::FileScoutNative::Shell::RenameRequest const>{ requests.data(), requests.size() });
            std::optional<std::filesystem::path> selectAfter;
            if (!renamedPaths.empty())
            {
                selectAfter = renamedPaths.front();
            }

            LoadDirectory(ActivePane().current_path, selectAfter);
            StatusTextBlock().Text(ToHString(
                L"Batch renamed " + std::to_wstring(requests.size()) +
                (requests.size() == 1 ? L" item." : L" items.")));
        }
        catch (::FileScoutNative::Shell::FileOperationException const& error)
        {
            StatusTextBlock().Text(ToHString(
                BuildBatchRenameFailureStatus(requests.size(), error)));
        }
        catch (std::exception const& error)
        {
            StatusTextBlock().Text(ToHString(
                L"Couldn't batch rename " + std::to_wstring(requests.size()) +
                (requests.size() == 1 ? L" item." : L" items.") + L" " +
                winrt::to_hstring(error.what()).c_str()));
        }
    }

    winrt::fire_and_forget MainWindow::ShowDeleteDialogAsync(::FileScoutNative::Domain::ShellItemRef item)
    {
        ShowDeleteDialogAsync(std::vector<::FileScoutNative::Domain::ShellItemRef>{ std::move(item) });
        co_return;
    }

    winrt::fire_and_forget MainWindow::ShowDeleteDialogAsync(
        std::vector<::FileScoutNative::Domain::ShellItemRef> items)
    {
        auto lifetime = get_strong();

        auto dialog = winrt::Microsoft::UI::Xaml::Controls::ContentDialog{};
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(L"Delete To Recycle Bin"));
        dialog.PrimaryButtonText(L"Delete");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        dialog.Content(winrt::box_value(
            items.size() == 1
                ? L"Move " + items.front().display_name + L" to the Recycle Bin?"
                : L"Move " + std::to_wstring(items.size()) + L" selected items to the Recycle Bin?"));

        if (co_await dialog.ShowAsync() != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            StatusTextBlock().Text(ToHString(BuildDeleteCanceledStatus(
                items.size() == 1 ? items.front().display_name : std::wstring{},
                items.size())));
            co_return;
        }

        // Capture everything we need before leaving the UI thread.
        HWND const ownerHwnd = GetWindowHandle();
        auto const currentPath = ActivePane().current_path;
        std::wstring const displayName = items.size() == 1 ? items.front().display_name : L"";
        std::size_t const itemCount = items.size();

        std::vector<std::filesystem::path> paths;
        paths.reserve(itemCount);
        for (auto const& item : items)
            paths.push_back(item.path);

        // Capture active pane before leaving the UI thread (m_activePaneIndex is UI-thread-only).
        int32_t const activePaneIndex = m_activePaneIndex;

        // Show progress before blocking operation.
        StatusTextBlock().Text(ToHString(
            itemCount == 1
                ? L"Deleting " + displayName + L"..."
                : L"Deleting " + std::to_wstring(itemCount) + L" items..."));
        // S5: show per-pane ProgressBar (reuses search bar, indeterminate) so long deletes
        // are not silent. Hidden unconditionally at the top of the TryEnqueue callback.
        SearchProgressForPane(activePaneIndex).Visibility(
            winrt::Microsoft::UI::Xaml::Visibility::Visible);

        // Run IFileOperation::PerformOperations on a background thread so the UI stays
        // responsive. IFileOperation runs as CLSCTX_LOCAL_SERVER (out-of-process via
        // Explorer), so cross-apartment calls are safe. Post UI updates back via
        // TryEnqueue since Microsoft::UI::Dispatching::DispatcherQueue does not have
        // a winrt::resume_foreground overload.
        co_await winrt::resume_background();

        bool success = true;
        bool canceled = false;
        std::wstring failureDetail;
        try
        {
            auto const t0 = std::chrono::steady_clock::now();
            if (paths.size() == 1)
            {
                m_shellFileOperationService.DeleteItem(ownerHwnd, paths.front());
            }
            else
            {
                m_shellFileOperationService.DeleteItems(
                    ownerHwnd,
                    std::span<std::filesystem::path const>{ paths.data(), paths.size() });
            }
            auto const deleteMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            TraceLog(L"Delete: " + std::to_wstring(itemCount) + L" item(s) in "
                + std::to_wstring(deleteMs) + L"ms");
        }
        catch (::FileScoutNative::Shell::FileOperationException const& error)
        {
            success = false;
            canceled = IsCanceledFileOperation(error);
            failureDetail = error.Error().detail;
        }
        catch (std::exception const& error)
        {
            success = false;
            failureDetail = winrt::to_hstring(error.what()).c_str();
        }

        m_dispatcherQueue.TryEnqueue(
            [this, lifetime, success, canceled, currentPath, displayName, itemCount, activePaneIndex, failureDetail]()
            {
                // S5: hide ProgressBar unconditionally — failure path has no LoadDirectory
                // call that would otherwise hide it.
                SearchProgressForPane(activePaneIndex).Visibility(
                    winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
                if (success)
                {
                    LoadDirectory(currentPath);
                    StatusTextBlock().Text(ToHString(
                        itemCount == 1
                            ? L"Deleted " + displayName + L" to Recycle Bin"
                            : L"Deleted " + std::to_wstring(itemCount) + L" items to Recycle Bin"));
                }
                else
                {
                    StatusTextBlock().Text(ToHString(
                        canceled
                            ? BuildDeleteCanceledStatus(displayName, itemCount)
                            : BuildDeleteFailureStatus(displayName, itemCount, failureDetail)));
                }
            });
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
        auto const& displayedItems = DisplayedItems();
        for (std::size_t index = 0; index < displayedItems.size(); ++index)
        {
            if (displayedItems[index].path == path)
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
        rootElement.KeyboardAcceleratorPlacementMode(
            winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorPlacementMode::Hidden);
        auto const& accelerators = rootElement.KeyboardAccelerators();
        auto const CombineModifiers = [](VKM const left, VKM const right)
        {
            using U = std::underlying_type_t<VKM>;
            return static_cast<VKM>(static_cast<U>(left) | static_cast<U>(right));
        };

        auto const AddAccelerator = [&](VK const key, VKM const modifiers, std::wstring commandId)
        {
            auto accel = KA{};
            accel.Key(key);
            accel.Modifiers(modifiers);
            accel.Invoked([this, commandId](winrt::Windows::Foundation::IInspectable const&, KAArgs const& e)
            {
                if (ShouldTextInputOwnFileShortcut(commandId))
                {
                    return;
                }

                auto const index =
                    ActivePane().active_list_view ? ActivePane().active_list_view.SelectedIndex() : -1;
                std::optional<int32_t> const itemIndex =
                    index >= 0 ? std::optional<int32_t>{ index } : std::nullopt;
                ExecuteContextCommand(commandId, itemIndex);
                e.Handled(true);
            });
            accelerators.Append(accel);
        };

        AddAccelerator(VK::A, VKM::Control, L"selectAll");
        AddAccelerator(VK::C, VKM::Control, L"copy");
        AddAccelerator(VK::X, VKM::Control, L"cut");
        AddAccelerator(VK::V, VKM::Control, L"paste");
        AddAccelerator(VK::Delete, VKM::None, L"delete");
        AddAccelerator(VK::F2, VKM::None, L"rename");
        AddAccelerator(VK::F5, VKM::None, L"refresh");
        AddAccelerator(VK::Enter, VKM::Menu, L"properties");
        AddAccelerator(VK::Left, VKM::Menu, L"back");
        AddAccelerator(VK::Right, VKM::Menu, L"forward");
        AddAccelerator(VK::N, CombineModifiers(VKM::Control, VKM::Shift), L"newFolder");
        AddAccelerator(VK::T, VKM::Control, L"newTab");
        AddAccelerator(VK::W, VKM::Control, L"closeTab");
        // Ctrl+Tab / Ctrl+Shift+Tab cycle through panes.
        AddAccelerator(VK::Tab, VKM::Control, L"switchPane");

        // Ctrl+F — focus the dedicated search box.
        {
            auto accel = KA{};
            accel.Key(VK::F);
            accel.Modifiers(VKM::Control);
            accel.Invoked([this](winrt::Windows::Foundation::IInspectable const& sender, KAArgs const& e)
            {
                SearchAccelerator_Invoked(sender, e);
            });
            accelerators.Append(accel);
        }

        // F4 / Ctrl+L — reveal address TextBox for direct path entry.
        for (auto const& [key, mod] : std::initializer_list<std::pair<VK, VKM>>{
                { VK::F4, VKM::None }, { VK::L, VKM::Control }})
        {
            auto accel = KA{};
            accel.Key(key);
            accel.Modifiers(mod);
            accel.Invoked([this](winrt::Windows::Foundation::IInspectable const&, KAArgs const& e)
            {
                e.Handled(true);
                ShowAddressTextBox();
            });
            accelerators.Append(accel);
        }

        if (::FileScoutNative::Domain::IsWave1FeatureEnabled(
                ::FileScoutNative::Domain::Wave1Feature::CommandPalette))
        {
            auto accel = KA{};
            accel.Key(VK::P);
            accel.Modifiers(VKM::Control);
            accel.Invoked([this](winrt::Windows::Foundation::IInspectable const&, KAArgs const& e)
            {
                e.Handled(true);
                ShowCommandPalette();
            });
            accelerators.Append(accel);
        }
    }

    bool MainWindow::ShouldTextInputOwnFileShortcut(std::wstring_view const commandId)
    {
        if (!IsTextEditingCommand(commandId))
        {
            return false;
        }

        if (AddressTextBox().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            return true;
        }

        auto const focused = winrt::Microsoft::UI::Xaml::Input::FocusManager::GetFocusedElement();
        if (FocusedElementLivesInsideEditableControl(focused))
        {
            return true;
        }

        auto const textBoxHasFocus =
            [](winrt::Microsoft::UI::Xaml::Controls::TextBox const& textBox)
            {
                return textBox &&
                    textBox.Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible &&
                    textBox.FocusState() != winrt::Microsoft::UI::Xaml::FocusState::Unfocused;
            };

        return textBoxHasFocus(AddressTextBox()) ||
            textBoxHasFocus(SearchTextBox()) ||
            (CommandPaletteOverlay().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible &&
             textBoxHasFocus(CommandPaletteTextBox()));
    }

    // Toolbar back/forward button click handlers.
    void MainWindow::BackButton_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        NavigateBack();
    }

    void MainWindow::ForwardButton_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        NavigateForward();
    }

    // R13b: Navigation history ────────────────────────────────────────────

    void MainWindow::NavigateBack()
    {
        auto& pane = ActivePane();
        if (pane.back_stack.empty())
        {
            StatusTextBlock().Text(L"No back history.");
            UpdateNavButtons();
            return;
        }
        auto const prev = pane.back_stack.back();
        pane.back_stack.pop_back();
        pane.forward_stack.push_back(pane.current_path);
        UpdateNavButtons();
        LoadDirectory(prev, std::nullopt, /*skipHistoryRecord=*/true);
    }

    void MainWindow::NavigateForward()
    {
        auto& pane = ActivePane();
        if (pane.forward_stack.empty())
        {
            StatusTextBlock().Text(L"No forward history.");
            UpdateNavButtons();
            return;
        }
        auto const next = pane.forward_stack.back();
        pane.forward_stack.pop_back();
        pane.back_stack.push_back(pane.current_path);
        UpdateNavButtons();
        LoadDirectory(next, std::nullopt, /*skipHistoryRecord=*/true);
    }

    void MainWindow::UpdateNavButtons()
    {
        BackButton().IsEnabled(!ActivePane().back_stack.empty());
        ForwardButton().IsEnabled(!ActivePane().forward_stack.empty());
        UpdateTitleBarInteractiveRegions();
    }

    // ────────────────────────────────────────────────────────────────────────

    // R13a: Command palette ────────────────────────────────────────────────

    void MainWindow::ShowCommandPalette()
    {
        if (!::FileScoutNative::Domain::IsWave1FeatureEnabled(
                ::FileScoutNative::Domain::Wave1Feature::CommandPalette))
        {
            StatusTextBlock().Text(ToHString(
                BuildWave1FeatureStatus(::FileScoutNative::Domain::Wave1Feature::CommandPalette)));
            return;
        }

        CommandPaletteTextBox().Text(L"");
        CommandPaletteOverlay().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        RefreshCommandPalette(L"");
        CommandPaletteTextBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::HideCommandPalette()
    {
        CommandPaletteOverlay().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    void MainWindow::RefreshCommandPalette(std::wstring_view query)
    {
        auto const& lv = CommandPaletteListView();
        lv.Items().Clear();
        for (auto const& cmd : m_commandRegistry.Filter(query))
            lv.Items().Append(CreateCommandRowView(cmd));
    }

    winrt::Windows::Foundation::IInspectable MainWindow::CreateCommandRowView(
        ::FileScoutNative::Domain::CommandEntry const& cmd)
    {
        namespace MUI = winrt::Microsoft::UI::Xaml::Controls;
        namespace MUX = winrt::Microsoft::UI::Xaml;

        MUI::Grid row;
        row.Padding({ 12, 8, 12, 8 });
        row.Tag(winrt::box_value(winrt::hstring(cmd.id)));

        {
            MUI::ColumnDefinition c1;
            c1.Width({ 1, MUX::GridUnitType::Star });
            row.ColumnDefinitions().Append(c1);
            MUI::ColumnDefinition c2;
            c2.Width({ 0, MUX::GridUnitType::Auto });
            row.ColumnDefinitions().Append(c2);
        }

        MUI::StackPanel namePanel;
        MUI::Grid::SetColumn(namePanel, 0);

        MUI::TextBlock nameText;
        nameText.Text(winrt::hstring(cmd.display_name));
        nameText.FontSize(14);
        namePanel.Children().Append(nameText);

        if (!cmd.category.empty())
        {
            MUI::TextBlock catText;
            catText.Text(winrt::hstring(cmd.category));
            catText.FontSize(11);
            catText.Opacity(0.55);
            namePanel.Children().Append(catText);
        }
        row.Children().Append(namePanel);

        if (!cmd.shortcut_text.empty())
        {
            MUI::TextBlock shortcutHint;
            shortcutHint.Text(winrt::hstring(cmd.shortcut_text));
            shortcutHint.FontSize(12);
            shortcutHint.Opacity(0.5);
            shortcutHint.VerticalAlignment(MUX::VerticalAlignment::Center);
            MUI::Grid::SetColumn(shortcutHint, 1);
            row.Children().Append(shortcutHint);
        }

        return row.as<winrt::Windows::Foundation::IInspectable>();
    }

    void MainWindow::CommandPalette_TextChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&)
    {
        RefreshCommandPalette(CommandPaletteTextBox().Text().c_str());
    }

    void MainWindow::CommandPalette_ItemClick(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        auto const elem = args.ClickedItem().try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
        if (!elem) return;
        auto const tag = elem.Tag();
        if (!tag) return;
        auto const commandId = winrt::unbox_value<winrt::hstring>(tag);
        HideCommandPalette();
        ExecuteContextCommand(commandId.c_str(), std::nullopt);
    }

    void MainWindow::CommandPalette_TextBox_KeyDown(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        using VK = winrt::Windows::System::VirtualKey;
        auto const& lv = CommandPaletteListView();

        if (args.Key() == VK::Escape)
        {
            args.Handled(true);
            HideCommandPalette();
        }
        else if (args.Key() == VK::Down)
        {
            args.Handled(true);
            auto const count = static_cast<int32_t>(lv.Items().Size());
            if (count > 0)
            {
                auto const next = (std::min)(lv.SelectedIndex() + 1, count - 1);
                lv.SelectedIndex(next);
                lv.ScrollIntoView(lv.SelectedItem());
            }
        }
        else if (args.Key() == VK::Up)
        {
            args.Handled(true);
            if (lv.SelectedIndex() > 0)
            {
                lv.SelectedIndex(lv.SelectedIndex() - 1);
                lv.ScrollIntoView(lv.SelectedItem());
            }
        }
        else if (args.Key() == VK::Enter)
        {
            args.Handled(true);
            auto const sel = lv.SelectedItem();
            if (!sel) return;
            auto const elem = sel.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
            if (!elem) return;
            auto const tag = elem.Tag();
            if (!tag) return;
            auto const commandId = winrt::unbox_value<winrt::hstring>(tag);
            HideCommandPalette();
            ExecuteContextCommand(commandId.c_str(), std::nullopt);
        }
    }

    void MainWindow::DualPaneButton_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ToggleDualPane();
    }

    // ────────────────────────────────────────────────────────────────────────

    winrt::fire_and_forget MainWindow::OpenArchiveItemAsync(::FileScoutNative::Domain::ShellItemRef item)
    {
        auto lifetime = get_strong();
        auto const ownerWindow = GetWindowHandle();

        StatusTextBlock().Text(ToHString(L"Extracting " + item.display_name + L"..."));

        co_await winrt::resume_background();

        std::filesystem::path extractedPath;
        std::wstring errorText;

        HRESULT const initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool const shouldUninitialize = SUCCEEDED(initHr);
        if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE)
        {
            errorText = L"Archive extraction failed to initialize COM.";
        }

        try
        {
            if (errorText.empty())
            {
                auto const resolved = m_shellInterop.TryResolveArchivePath(item.path);
                if (!resolved.has_value())
                {
                    errorText = L"Archive entry could not be resolved.";
                }
                else
                {
                    auto const relativePath = TryGetArchiveRelativePath(*resolved);
                    if (!relativePath.has_value())
                    {
                        errorText = L"Archive entry path is invalid.";
                    }
                    else
                    {
                        auto const sessionRoot = BuildArchiveOpenSessionRoot(resolved->archive_file_path);
                        extractedPath = sessionRoot / *relativePath;
                        std::filesystem::create_directories(extractedPath.parent_path());

                        std::array<std::filesystem::path, 1> const sourcePaths{ item.path };
                        m_shellFileOperationService.CopyItemsFromPaths(
                            ownerWindow,
                            sourcePaths,
                            extractedPath.parent_path(),
                            false);

                        if (!std::filesystem::exists(extractedPath))
                        {
                            errorText = L"Archive extraction completed without a visible output file.";
                        }
                    }
                }
            }
        }
        catch (...)
        {
            errorText = L"Archive extraction failed.";
        }

        if (shouldUninitialize)
        {
            CoUninitialize();
        }

        m_dispatcherQueue.TryEnqueue([this, lifetime, item = std::move(item),
                                      extractedPath = std::move(extractedPath),
                                      errorText = std::move(errorText)]() mutable
        {
            if (!errorText.empty())
            {
                StatusTextBlock().Text(ToHString(errorText));
                return;
            }

            try
            {
                m_shellFileOperationService.OpenPath(GetWindowHandle(), extractedPath);
                StatusTextBlock().Text(ToHString(
                    L"Extracted and opened " + item.display_name + L" from archive."));
            }
            catch (...)
            {
                StatusTextBlock().Text(ToHString(
                    L"Extracted " + item.display_name + L" but opening it failed."));
            }
        });
    }

    winrt::fire_and_forget MainWindow::ExtractArchiveItemAsync(
        ::FileScoutNative::Domain::ShellItemRef item,
        bool const revealAfterExtract)
    {
        auto lifetime = get_strong();
        auto const ownerWindow = GetWindowHandle();

        StatusTextBlock().Text(ToHString(L"Extracting " + item.display_name + L"..."));

        co_await winrt::resume_background();

        std::filesystem::path extractedPath;
        std::filesystem::path requestedPath;
        std::wstring errorText;

        HRESULT const initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool const shouldUninitialize = SUCCEEDED(initHr);
        if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE)
        {
            errorText = L"Archive extraction failed to initialize COM.";
        }

        try
        {
            if (errorText.empty())
            {
                auto const resolved = m_shellInterop.TryResolveArchivePath(item.path);
                if (!resolved.has_value())
                {
                    errorText = L"Archive entry could not be resolved.";
                }
                else if (auto const relativePath = TryGetArchiveRelativePath(*resolved);
                         !relativePath.has_value())
                {
                    errorText = L"Archive entry path is invalid.";
                }
                else
                {
                    requestedPath = resolved->archive_file_path.parent_path() / *relativePath;
                    std::filesystem::create_directories(requestedPath.parent_path());

                    extractedPath = BuildUniqueDestinationPath(requestedPath, item.is_folder);
                    m_shellFileOperationService.CopyItemFromPath(
                        ownerWindow,
                        item.path,
                        extractedPath.parent_path(),
                        extractedPath.filename().wstring());

                    if (!std::filesystem::exists(extractedPath))
                    {
                        errorText = L"Archive extraction completed without a visible output item.";
                    }
                }
            }
        }
        catch (...)
        {
            errorText = L"Archive extraction failed.";
        }

        if (shouldUninitialize)
        {
            CoUninitialize();
        }

        m_dispatcherQueue.TryEnqueue([this, lifetime, item = std::move(item),
                                      extractedPath = std::move(extractedPath),
                                      requestedPath = std::move(requestedPath),
                                      errorText = std::move(errorText),
                                      revealAfterExtract]() mutable
        {
            if (!errorText.empty())
            {
                StatusTextBlock().Text(ToHString(errorText));
                return;
            }

            auto status = std::wstring{ L"Extracted " } + item.display_name;
            if (!requestedPath.empty() &&
                extractedPath.filename().wstring() != requestedPath.filename().wstring())
            {
                status += L" as " + extractedPath.filename().wstring();
            }

            if (revealAfterExtract)
            {
                LoadDirectory(extractedPath.parent_path(), extractedPath);
                status += L" and revealed it.";
            }
            else
            {
                status += L" to " + extractedPath.parent_path().wstring() + L".";
            }

            StatusTextBlock().Text(ToHString(status));
        });
    }

    winrt::Microsoft::UI::Xaml::Controls::ListView MainWindow::CreateTabListView(
        winrt::Windows::Foundation::Collections::IObservableVector<
            winrt::FileScoutNative::FileRowViewModel> const& source,
        int32_t const paneIndex)
    {
        auto listView = winrt::Microsoft::UI::Xaml::Controls::ListView{};
        listView.Margin({ 12, 0, 12, 12 });
        listView.Background(SolidBrush(0x00, 0x00, 0x00, 0x00));
        listView.IsRightTapEnabled(true);
        listView.AllowDrop(true);
        listView.CanDrag(true);
        listView.CanDragItems(true);
        listView.SelectionMode(winrt::Microsoft::UI::Xaml::Controls::ListViewSelectionMode::Extended);
        listView.ItemsSource(source);

        // Apply item container style and DataTemplate from Grid resources.
        try
        {
            auto const rootGrid = Content().as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
            ApplyRowPresentation(listView, rootGrid, m_panes[paneIndex]);
        }
        catch (...) {}

        // Wire events with pane-index capture so that interacting with a ListView
        // in the inactive pane activates that pane first.
        listView.SelectionChanged([this, paneIndex](auto const& s, auto const& a)
            {
                if (m_panes[paneIndex].tab_switching) return;
                // Guard against deferred WinUI events firing before init is complete.
                if (m_initComplete)
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

        listView.ContextRequested([this, paneIndex](auto const& s, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                PaneSurface_ContextRequested(s, a);
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

        listView.PreviewKeyDown([this, paneIndex](auto const&, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                FileRowsListView_KeyDown(a);
            });

        listView.Loaded([this, paneIndex](auto const& sender, auto const&)
            {
                auto const list = sender.as<winrt::Microsoft::UI::Xaml::Controls::ListView>();
                if (auto const scrollViewer =
                        FindDescendantByType<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>(list))
                {
                    scrollViewer.Background(SolidBrush(0x00, 0x00, 0x00, 0x00));
                    scrollViewer.IsRightTapEnabled(true);
                    scrollViewer.VerticalScrollBarVisibility(
                        winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Visible);

                    auto const existingTag = scrollViewer.Tag();
                    if (auto const tagValue = existingTag.try_as<winrt::hstring>();
                        tagValue == winrt::hstring{ L"PaneContextWired" })
                    {
                        return;
                    }

                    scrollViewer.Tag(winrt::box_value(winrt::hstring{ L"PaneContextWired" }));
                    scrollViewer.RightTapped([this, paneIndex](auto const& s, auto const& a)
                        {
                            SetActivePaneIndex(paneIndex);
                            FileRowsListView_RightTapped(s, a);
                        });
                    scrollViewer.ContextRequested([this, paneIndex](auto const& s, auto const& a)
                        {
                            SetActivePaneIndex(paneIndex);
                            PaneSurface_ContextRequested(s, a);
                        });
                }
            });

        return listView;
    }

    void MainWindow::FileRowsListView_KeyDown(
        winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == winrt::Windows::System::VirtualKey::A && GetKeyState(VK_CONTROL) < 0)
        {
            SelectAllDisplayedItems();
            args.Handled(true);
            return;
        }

        if (HasSystemModifierChord())
        {
            return;
        }

        if (args.Key() == winrt::Windows::System::VirtualKey::Enter)
        {
            if (ActivateCurrentSelection())
            {
                args.Handled(true);
            }
            return;
        }

        auto const vk = static_cast<int32_t>(args.Key());
        // VK codes 65–90 are A–Z.
        if (vk < 65 || vk > 90) return;

        auto& pane = ActivePane();
        if (pane.search_mode != SearchMode::None) return;
        if (pane.items.empty()) return;

        wchar_t const ch = static_cast<wchar_t>(std::towlower(static_cast<wchar_t>(vk)));

        // Start from the item after the current selection, wrapping around.
        int32_t const selected = pane.active_list_view ? pane.active_list_view.SelectedIndex() : -1;
        int32_t const start = (selected >= 0)
            ? (selected + 1) % static_cast<int32_t>(pane.items.size())
            : 0;

        for (std::size_t i = 0; i < pane.items.size(); ++i)
        {
            auto const idx = static_cast<int32_t>((start + i) % static_cast<int32_t>(pane.items.size()));
            auto const& name = pane.items[static_cast<std::size_t>(idx)].display_name;
            if (!name.empty() && std::towlower(name[0]) == ch)
            {
                SelectItemByIndex(idx);
                if (pane.active_list_view)
                {
                    // Fix bug #2: set ListView SelectedIndex so the next keypress reads the
                    // correct position from SelectedIndex(). SelectItemByIndex only updates
                    // the domain model — without this, SelectedIndex() stays stale and every
                    // press restarts from the same item.
                    pane.active_list_view.SelectedIndex(idx);
                    pane.active_list_view.ScrollIntoView(
                        pane.active_list_view.Items().GetAt(static_cast<uint32_t>(idx)));
                    pane.active_list_view.UpdateLayout();

                    if (auto const container =
                            pane.active_list_view.ContainerFromIndex(idx)
                                .try_as<winrt::Microsoft::UI::Xaml::Controls::ListViewItem>())
                    {
                        container.Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
                    }
                    else
                    {
                        pane.active_list_view.Focus(
                            winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
                    }
                }
                StatusTextBlock().Text(ToHString(L"\u2192 " + pane.items[static_cast<std::size_t>(idx)].display_name));
                args.Handled(true);
                return;
            }
        }
    }

    bool MainWindow::ActivateDisplayedItemByIndex(std::optional<int32_t> const itemIndex)
    {
        if (!itemIndex.has_value())
        {
            return false;
        }

        auto const* item = TryGetDisplayedItemByIndex(*itemIndex);
        if (item == nullptr)
        {
            return false;
        }

        auto& pane = ActivePane();
        if (pane.search_mode == SearchMode::Recursive)
        {
            auto const target = ::FileScoutNative::Search::TryBuildSearchOpenTarget(item->path);
            if (!target.has_value())
            {
                return false;
            }

            ResetSearchState(m_activePaneIndex, true);
            ResetSelectionDetails();
            LoadDirectory(target->folder_path, target->select_path);
            return true;
        }

        if (!pane.is_archive_view && m_shellInterop.IsArchivePath(item->path))
        {
            LoadDirectory(item->path);
            return true;
        }

        if (item->is_folder)
        {
            LoadDirectory(item->path);
            return true;
        }

        if (pane.is_archive_view)
        {
            if (!::FileScoutNative::Domain::IsWave1FeatureEnabled(
                    ::FileScoutNative::Domain::Wave1Feature::ArchiveBrowsing))
            {
                StatusTextBlock().Text(ToHString(
                    BuildWave1FeatureStatus(::FileScoutNative::Domain::Wave1Feature::ArchiveBrowsing)));
                return true;
            }

            OpenArchiveItemAsync(*item);
            return true;
        }

        try
        {
            m_shellFileOperationService.OpenPath(GetWindowHandle(), item->path);
            if (IsUnsupportedArchiveItem(*item))
            {
                StatusTextBlock().Text(ToHString(BuildUnsupportedArchiveOpenStatus(*item, true)));
            }
            else
            {
                StatusTextBlock().Text(ToHString(L"Opened " + item->display_name));
            }
        }
        catch (std::exception const&)
        {
            if (IsUnsupportedArchiveItem(*item))
            {
                StatusTextBlock().Text(ToHString(BuildUnsupportedArchiveOpenStatus(*item, false)));
            }
            else
            {
                StatusTextBlock().Text(ToHString(L"Open failed: " + item->display_name));
            }
        }

        return true;
    }

    bool MainWindow::ActivateCurrentSelection()
    {
        auto index = ActivePane().active_list_view
            ? ActivePane().active_list_view.SelectedIndex()
            : -1;
        if (index < 0)
        {
            auto const& displayedItems = DisplayedItems();
            for (std::size_t itemIndex = 0; itemIndex < displayedItems.size(); ++itemIndex)
            {
                if (ActivePane().selection.IsSelected(displayedItems[itemIndex].parsing_name))
                {
                    index = static_cast<int32_t>(itemIndex);
                    break;
                }
            }
        }

        if (index < 0)
        {
            return false;
        }

        return ActivateDisplayedItemByIndex(index);
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
            pane.tabs[pane.active_tab_index].is_drives_view = pane.is_drives_view;
            pane.tabs[pane.active_tab_index].is_archive_view = pane.is_archive_view;
        }

        auto newRows = winrt::single_threaded_observable_vector<winrt::FileScoutNative::FileRowViewModel>();
        pane.tab_rows.push_back(newRows);
        pane.tabs.push_back({});

        auto const newIndex = static_cast<int32_t>(pane.tabs.size()) - 1;
        auto const paneIndex = m_activePaneIndex;
        auto newListView = CreateTabListView(newRows, paneIndex);
        auto paneSurfaceHost = CreatePaneSurfaceHost(newListView);
        paneSurfaceHost.RightTapped([this, paneIndex](auto const& s, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                FileRowsListView_RightTapped(s, a);
            });
        paneSurfaceHost.ContextRequested([this, paneIndex](auto const& s, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                PaneSurface_ContextRequested(s, a);
            });

        auto tabItem = winrt::Microsoft::UI::Xaml::Controls::TabViewItem{};
        tabItem.Header(winrt::box_value(winrt::hstring{ L"New Tab" }));
        tabItem.Content(paneSurfaceHost);
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
        pane.active_list_view = ResolveTabContentListView(tabItem.Content());

        auto const& tab = pane.tabs[pane.active_tab_index];
        pane.current_path = tab.path;
        pane.is_drives_view = tab.is_drives_view;
        pane.is_archive_view = tab.is_archive_view;
        pane.items = tab.items;
        pane.selection = tab.selection;

        if (tab.is_drives_view)
            BreadcrumbPanel().Children().Clear();
        else
        {
            UpdateBreadcrumbs(pane.current_path);
            UpdateSearchPlaceholder(pane.current_path);
        }

        if (pane.items.empty() && !tab.path.empty() && !tab.is_drives_view)
        {
            // Tab hasn't loaded yet — navigate to its path now.
            LoadDirectory(tab.path);
            return;
        }

        // Restore the cached rows and selection without re-scanning the disk.
        RestoreSelectionFromState(pane, tab.selection, tab.selected_item_index);

        auto const selectedIndices = GetSelectedItemIndices();
        if (selectedIndices.size() == 1)
        {
            SelectItemByIndex(selectedIndices.front());
        }
        else if (selectedIndices.size() > 1)
        {
            UpdateMultiSelectionDetails(selectedIndices);
        }
        else
        {
            ResetSelectionDetails();
        }

        auto const statusSuffix = tab.is_drives_view
            ? std::wstring{ L" drive(s) available." }
            : (tab.is_archive_view
                ? L" archive entries loaded from " + pane.current_path.wstring()
                : L" shell-backed entries loaded from " + pane.current_path.wstring());
        StatusTextBlock().Text(ToHString(std::to_wstring(pane.items.size()) + statusSuffix));
    }

    void MainWindow::RestoreSelectionFromState(
        PaneContext& pane,
        ::FileScoutNative::Domain::PaneSelectionState const& selection,
        int32_t const fallbackSelectedIndex)
    {
        if (!pane.active_list_view)
        {
            return;
        }

        pane.tab_switching = true;
        pane.active_list_view.SelectedIndex(-1);

        auto const& displayedItems = DisplayedItems(pane);
        std::vector<int32_t> selectedIndices;
        std::vector<std::wstring> survivingIds;
        selectedIndices.reserve(displayedItems.size());
        survivingIds.reserve(displayedItems.size());

        auto const focusedId = selection.FocusedId();
        std::optional<int32_t> focusedIndex;
        for (std::size_t index = 0; index < displayedItems.size(); ++index)
        {
            auto const& item = displayedItems[index];
            if (!selection.IsSelected(item.parsing_name))
            {
                continue;
            }

            auto const selectedIndex = static_cast<int32_t>(index);
            selectedIndices.push_back(selectedIndex);
            survivingIds.push_back(item.parsing_name);

            if (focusedId.has_value() && item.parsing_name == *focusedId)
            {
                focusedIndex = selectedIndex;
            }
        }

        if (!selectedIndices.empty())
        {
            auto const primaryIndex = focusedIndex.value_or(selectedIndices.front());
            pane.active_list_view.SelectedIndex(primaryIndex);

            for (auto const selectedIndex : selectedIndices)
            {
                if (selectedIndex == primaryIndex)
                {
                    continue;
                }

                pane.active_list_view.SelectRange(
                    winrt::Microsoft::UI::Xaml::Data::ItemIndexRange(selectedIndex, 1));
            }
        }
        else if (fallbackSelectedIndex >= 0 &&
            fallbackSelectedIndex < static_cast<int32_t>(displayedItems.size()))
        {
            pane.active_list_view.SelectedIndex(fallbackSelectedIndex);
        }

        pane.tab_switching = false;

        if (!selectedIndices.empty())
        {
            auto const primaryIndex = focusedIndex.value_or(selectedIndices.front());
            pane.selection.ReplaceWith(
                survivingIds,
                displayedItems[static_cast<std::size_t>(primaryIndex)].parsing_name);
        }
        else if (fallbackSelectedIndex >= 0 &&
            fallbackSelectedIndex < static_cast<int32_t>(displayedItems.size()))
        {
            pane.selection.ReplaceWith(
                displayedItems[static_cast<std::size_t>(fallbackSelectedIndex)].parsing_name);
        }
        else
        {
            pane.selection.Clear();
        }

        if (pane.active_tab_index >= 0 &&
            pane.active_tab_index < static_cast<int32_t>(pane.tabs.size()))
        {
            pane.tabs[static_cast<std::size_t>(pane.active_tab_index)].selection = pane.selection;
            pane.tabs[static_cast<std::size_t>(pane.active_tab_index)].selected_item_index =
                pane.active_list_view.SelectedIndex();
        }

        UpdateRowSelectionVisuals(pane);
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

        auto rows0 = winrt::single_threaded_observable_vector<winrt::FileScoutNative::FileRowViewModel>();
        pane.tab_rows.push_back(rows0);
        pane.tabs.push_back({});

        auto listView0 = CreateTabListView(rows0, paneIndex);
        auto paneSurfaceHost = CreatePaneSurfaceHost(listView0);
        paneSurfaceHost.RightTapped([this, paneIndex](auto const& s, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                FileRowsListView_RightTapped(s, a);
            });
        paneSurfaceHost.ContextRequested([this, paneIndex](auto const& s, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                PaneSurface_ContextRequested(s, a);
            });
        pane.active_list_view = listView0;

        auto tabItem0 = winrt::Microsoft::UI::Xaml::Controls::TabViewItem{};
        tabItem0.Header(winrt::box_value(winrt::hstring{ L"New Tab" }));
        tabItem0.IsClosable(false);
        tabItem0.Content(paneSurfaceHost);
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

        // Exit search mode on the outgoing pane before switching.
        auto& outgoing = m_panes[m_activePaneIndex];
        if (outgoing.search_mode != SearchMode::None)
        {
            ResetSearchState(m_activePaneIndex, true);
        }

        m_activePaneIndex = paneIndex;
        UpdateActivePaneIndicator();
        if (!ActivePane().is_drives_view && !ActivePane().current_path.empty())
        {
            UpdateBreadcrumbs(ActivePane().current_path);
            UpdateSearchPlaceholder(ActivePane().current_path);
        }
        else
        {
            BreadcrumbPanel().Children().Clear();
        }
        // Sync search box with incoming pane's search state.
        SearchTextBox().IsReadOnly(ActivePane().search_mode == SearchMode::Recursive);
        SearchTextBox().Text(winrt::hstring(ActivePane().search_query));
        UpdateNavButtons();
    }

    void MainWindow::UpdateActivePaneIndicator()
    {
        auto const activeBrush = LookupBrush(L"AccentFillColorDefaultBrush");
        auto const inactiveBrush = LookupBrush(L"CardStrokeColorDefaultBrush");

        if (m_activePaneIndex == 0)
        {
            LeftPaneBorder().BorderBrush(activeBrush);
            LeftPaneBorder().BorderThickness({ 2, 2, 2, 2 });
            RightPaneBorder().BorderBrush(inactiveBrush);
            RightPaneBorder().BorderThickness({ 1, 1, 1, 1 });
        }
        else
        {
            LeftPaneBorder().BorderBrush(inactiveBrush);
            LeftPaneBorder().BorderThickness({ 1, 1, 1, 1 });
            RightPaneBorder().BorderBrush(activeBrush);
            RightPaneBorder().BorderThickness({ 2, 2, 2, 2 });
        }
    }

    winrt::Microsoft::UI::Xaml::Controls::Border MainWindow::SortHeaderForPane(int32_t const paneIndex)
    {
        return paneIndex == 0 ? LeftPaneSortHeader() : RightPaneSortHeader();
    }

    winrt::Microsoft::UI::Xaml::Controls::ProgressBar MainWindow::SearchProgressForPane(int32_t const paneIndex)
    {
        return paneIndex == 0 ? LeftPaneSearchProgress() : RightPaneSearchProgress();
    }

    void MainWindow::UpdateBreadcrumbs(std::filesystem::path const& path)
    {
        auto const panel = BreadcrumbPanel();
        panel.Children().Clear();

        if (path.empty()) return;

        // Collect ancestors from root down to current path.
        std::vector<std::filesystem::path> segments;
        for (auto p = path; !p.empty() && p != p.parent_path(); p = p.parent_path())
            segments.insert(segments.begin(), p);
        if (segments.empty())
            segments.push_back(path);

        // The loop stops before the filesystem root (C:\) because root.parent_path()==root.
        // Prepend it so paths like C:\Windows\System32 show all three segments.
        if (!segments.empty())
        {
            auto const root = segments.front().root_path();
            if (segments.front() != root)
                segments.insert(segments.begin(), root);
        }

        TraceLog(L"UpdateBreadcrumbs: " + path.wstring() + L" (" + std::to_wstring(segments.size()) + L" segments)");

        namespace MUI = winrt::Microsoft::UI::Xaml::Controls;

        for (std::size_t i = 0; i < segments.size(); ++i)
        {
            auto const& seg = segments[i];
            std::wstring label = (i == 0)
                ? seg.root_name().wstring()    // "C:"
                : seg.filename().wstring();    // "Documents"
            if (label.empty()) label = seg.wstring();

            // Separator "›" (U+203A) between segments.
            if (i > 0)
            {
                MUI::TextBlock sep;
                sep.Text(L" \u203A ");
                sep.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
                sep.Opacity(0.4);
                sep.FontSize(18);
                panel.Children().Append(sep);
            }

            MUI::HyperlinkButton btn;
            btn.Padding({ 10, 3, 10, 3 });
            MUI::TextBlock lbl;
            lbl.Text(winrt::hstring(label));
            lbl.FontSize(18);
            lbl.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
            btn.Content(lbl);

            auto const targetPath = seg;
            btn.Click([this, targetPath](winrt::Windows::Foundation::IInspectable const&,
                                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
            {
                LoadDirectory(targetPath);
            });

            panel.Children().Append(btn);
        }

        UpdateNavButtons();
    }

    void MainWindow::ShowAddressTextBox()
    {
        BreadcrumbScrollViewer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        AddressTextBox().Text(ToHString(ActivePane().current_path.wstring()));
        AddressTextBox().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        AddressTextBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
        AddressTextBox().SelectAll();

        auto const weakThis = get_weak();
        m_dispatcherQueue.TryEnqueue([weakThis]()
        {
            if (auto self = weakThis.get())
            {
                if (self->AddressTextBox().Visibility() !=
                    winrt::Microsoft::UI::Xaml::Visibility::Visible)
                {
                    return;
                }

                self->AddressTextBox().Focus(
                    winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
                self->AddressTextBox().SelectAll();
            }
        });
    }

    void MainWindow::HideAddressTextBox()
    {
        AddressTextBox().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        BreadcrumbScrollViewer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
    }

    void MainWindow::UpdateSearchPlaceholder(std::filesystem::path const& path)
    {
        auto const folderName = path.filename().empty()
            ? path.wstring()
            : path.filename().wstring();
        SearchTextBox().PlaceholderText(L"Search " + folderName + L"\u2026");
    }

    void MainWindow::BuildSortHeader(int32_t const paneIndex)
    {
        struct ColumnDef { std::wstring_view label; SortColumn column; double widthStar; };
        static constexpr std::array<ColumnDef, 4> columns{{
            { L"Name",          SortColumn::Name,         3.4 },
            { L"Type",          SortColumn::Type,         1.4 },
            { L"Size",          SortColumn::Size,         1.0 },
            { L"Date Modified", SortColumn::DateModified, 2.4 },
        }};

        auto headerBorder = SortHeaderForPane(paneIndex);

        auto headerGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
        auto const& pane = m_panes[paneIndex];
        auto const presentation = ResolveRowPresentation(pane);

        // In list mode, prepend a fixed-width column matching the row icon gutter.
        // This keeps sort button labels approximately aligned with the list row data columns.
        int32_t colOffset = 0;
        if (presentation == RowPresentation::List)
        {
            auto iconSpacerCol = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            iconSpacerCol.Width({ 30.0, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
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

        auto addHeaderButton = [&](std::wstring_view label, bool isActive, auto&& onClick)
        {
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
            btn.Click(std::forward<decltype(onClick)>(onClick));
            toggleStack.Children().Append(btn);
        };

        addHeaderButton(L"Group", pane.group_by_sort_column, [this, paneIndex](auto const&, auto const&)
            {
                SetActivePaneIndex(paneIndex);
                SetGroupingEnabled(paneIndex, !m_panes[paneIndex].group_by_sort_column);
            });

        auto addToggleBtn = [&](std::wstring_view label, ViewMode targetMode)
        {
            bool const isActive = !pane.group_by_sort_column && pane.view_mode == targetMode;
            auto const capturedMode = targetMode;
            addHeaderButton(label, isActive, [this, paneIndex, capturedMode](auto const&, auto const&)
                {
                    SetActivePaneIndex(paneIndex);
                    auto& pane = m_panes[paneIndex];
                    if (pane.group_by_sort_column)
                    {
                        pane.view_mode = capturedMode;
                        SetGroupingEnabled(paneIndex, false);
                        return;
                    }

                    SetViewMode(paneIndex, capturedMode);
                });
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

        for (auto& tab : pane.tabs)
        {
            SortItems(tab.items, pane.sort_column, pane.sort_direction);
        }

        if (pane.active_tab_index >= 0 && pane.active_tab_index < static_cast<int32_t>(pane.tabs.size()))
        {
            pane.items = pane.tabs[pane.active_tab_index].items;
        }

        RebuildAllTabRowsForPane(paneIndex);

        StatusTextBlock().Text(ToHString(
            std::to_wstring(pane.items.size()) + L" items  |  sorted by "
            + SortColumnLabel(column)
            + (pane.sort_direction == SortDirection::Ascending ? L"  \u2191" : L"  \u2193")
            + (pane.group_by_sort_column ? L"  |  grouped" : L"")));
    }

    void MainWindow::SetGroupingEnabled(int32_t const paneIndex, bool const enabled)
    {
        auto& pane = m_panes[paneIndex];
        if (pane.group_by_sort_column == enabled)
        {
            return;
        }

        pane.group_by_sort_column = enabled;
        BuildSortHeader(paneIndex);
        RebuildAllTabRowsForPane(paneIndex);

        if (pane.active_list_view)
        {
            try
            {
                auto const rootGrid = Content().as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
                ApplyRowPresentation(pane.active_list_view, rootGrid, pane);
            }
            catch (...) {}
        }

        StatusTextBlock().Text(ToHString(
            std::to_wstring(pane.items.size()) + L" items  |  " +
            (enabled
                ? L"group view  |  grouped by " + SortColumnLabel(pane.sort_column)
                : RowPresentationStatusLabel(pane))));
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

        // Update ItemTemplate on the active ListView to match the new view mode.
        if (pane.active_list_view)
        {
            try
            {
                auto const rootGrid = Content().as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
                ApplyRowPresentation(pane.active_list_view, rootGrid, pane);
            }
            catch (...) {}
        }

        StatusTextBlock().Text(ToHString(
            std::to_wstring(pane.items.size()) + L" items  |  " +
            RowPresentationStatusLabel(pane)));
    }

    void MainWindow::RebuildAllTabRowsForPane(int32_t const paneIndex)
    {
        auto& pane = m_panes[paneIndex];
        // Cancel any pending staged-publish dispatch — this rebuild replaces it entirely.
        pane.awaiting_final_batch = false;

        // Use (std::min)() to prevent expansion of the Windows.h min() macro.
        auto const tabCount = (std::min)(pane.tabs.size(), pane.tab_rows.size());
        for (std::size_t i = 0; i < tabCount; ++i)
        {
            auto const& source = (static_cast<int32_t>(i) == pane.active_tab_index)
                ? pane.items
                : pane.tabs[i].items;

            auto& rows = pane.tab_rows[i];
            rows.Clear();
            std::wstring previousGroupLabel;
            for (auto const& item : source)
            {
                auto row = MakeFileRowViewModel(item);
                auto rowImpl = winrt::get_self<winrt::FileScoutNative::implementation::FileRowViewModel>(row);

                if (pane.group_by_sort_column)
                {
                    auto const groupLabel = BuildGroupLabel(item, pane.sort_column);
                    auto const showGroupHeader = previousGroupLabel != groupLabel;
                    rowImpl->SetGroupHeader(groupLabel, showGroupHeader);
                    previousGroupLabel = groupLabel;
                }
                else
                {
                    rowImpl->SetGroupHeader(L"", false);
                }

                rows.Append(row);
            }

            ApplyCachedRowIconsToRows(source, rows, m_iconSourceCache);
            if (static_cast<int32_t>(i) == pane.active_tab_index)
            {
                UpdateRowSelectionVisuals(pane);
            }
        }
    }

    void MainWindow::BuildSidebarFavorites()
    {
        auto const panel = SidebarPlacesPanel();
        panel.Children().Clear();
        auto const& activePane = ActivePane();
        // Tracks paths already shown so recent paths can be deduplicated against them.
        std::unordered_set<std::wstring> shownPathKeys;
        auto const favorites = m_favoritesService.GetSidebarFavorites();
        std::filesystem::path activeFavoritePath;
        std::size_t activeFavoriteScore = 0;

        if (!activePane.is_drives_view && !activePane.current_path.empty())
        {
            for (auto const& favorite : favorites)
            {
                if (favorite.path.empty() || favorite.is_drives_view)
                {
                    continue;
                }

                auto const score = SidebarPathMatchScore(
                    favorite.path,
                    activePane.current_path);
                if (score > activeFavoriteScore)
                {
                    activeFavoriteScore = score;
                    activeFavoritePath = favorite.path;
                }
            }
        }

        for (auto const& entry : favorites)
        {
            if (entry.is_drives_view) continue;  // This PC has its own Expander section (S1/S3)

            if (entry.is_pinned)
            {
                // Pinned entries render in BookmarksPanel — exclude from recents dedup.
                if (!entry.path.empty())
                    shownPathKeys.insert(::FileScoutNative::Domain::SidebarPathKey(entry.path));
                continue;
            }

            auto btn = winrt::Microsoft::UI::Xaml::Controls::Button{};
            StyleSidebarButtonBase(btn);
            btn.Margin({ 0, 0, 0, 2 });

            std::optional<::FileScoutNative::Domain::IconBitmap> entryIcon;
            if (!entry.path.empty())
            {
                winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                    btn,
                    winrt::box_value(ToHString(entry.path.wstring())));
                entryIcon = m_shellInterop.GetItemIcon(entry.path, true);
            }

            auto rowGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
            auto iconColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            iconColumn.Width({ 20, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
            auto labelColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            labelColumn.Width({ 1, winrt::Microsoft::UI::Xaml::GridUnitType::Star });
            auto badgeColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            badgeColumn.Width({ 0, winrt::Microsoft::UI::Xaml::GridUnitType::Auto });
            rowGrid.ColumnDefinitions().Append(iconColumn);
            rowGrid.ColumnDefinitions().Append(labelColumn);
            rowGrid.ColumnDefinitions().Append(badgeColumn);
            rowGrid.ColumnSpacing(8);

            auto iconTile = BuildSidebarTile(
                BuildSidebarGlyph(entryIcon, SidebarFavoriteFallbackLabel(entry)),
                20.0);

            auto label = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
            label.Text(ToHString(entry.display_name));
            label.FontWeight(entry.is_pinned
                ? winrt::Microsoft::UI::Text::FontWeights::SemiBold()
                : winrt::Microsoft::UI::Text::FontWeights::Normal());
            label.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
            label.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
            label.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);

            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(iconTile, 0);
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(label, 1);
            rowGrid.Children().Append(iconTile);
            rowGrid.Children().Append(label);

            auto const isSelected =
                (entry.is_drives_view && activePane.is_drives_view) ||
                (!entry.path.empty() && activeFavoriteScore > 0 &&
                    SidebarPathsEqual(entry.path, activeFavoritePath));

            auto surface = BuildSidebarRowSurface(isSelected);
            surface.Child(rowGrid);
            btn.Content(surface);

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

            if (!entry.path.empty())
                shownPathKeys.insert(::FileScoutNative::Domain::SidebarPathKey(entry.path));
            panel.Children().Append(btn);
        }

        // Recent paths: last 5 navigated folders not already shown above.
        auto const recents = ::FileScoutNative::Domain::SelectRecentPaths(
            m_panes[0].back_stack, shownPathKeys, 5);

        if (!recents.empty())
        {
            auto recentSep = winrt::Microsoft::UI::Xaml::Controls::Border{};
            recentSep.Height(1);
            recentSep.Margin({ 0, 6, 0, 6 });
            recentSep.Background(LookupBrush(L"DividerStrokeColorDefaultBrush"));
            panel.Children().Append(recentSep);

            for (auto const& recentPath : recents)
            {
                auto displayName = recentPath.filename().wstring();
                if (displayName.empty())
                    displayName = recentPath.root_name().wstring();
                if (displayName.empty())
                    displayName = recentPath.wstring();

                auto btn = winrt::Microsoft::UI::Xaml::Controls::Button{};
                StyleSidebarButtonBase(btn);
                btn.Margin({ 0, 0, 0, 2 });
                winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                    btn, winrt::box_value(ToHString(recentPath.wstring())));

                auto rowGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
                auto iconColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
                iconColumn.Width({ 20, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
                auto labelColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
                labelColumn.Width({ 1, winrt::Microsoft::UI::Xaml::GridUnitType::Star });
                rowGrid.ColumnDefinitions().Append(iconColumn);
                rowGrid.ColumnDefinitions().Append(labelColumn);
                rowGrid.ColumnSpacing(8);

                auto const entryIcon = m_shellInterop.GetItemIcon(recentPath, true);
                auto iconTile = BuildSidebarTile(
                    BuildSidebarGlyph(entryIcon, L"folder"),
                    20.0);

                auto label = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
                label.Text(ToHString(displayName));
                label.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Normal());
                label.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
                label.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
                label.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);

                winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(iconTile, 0);
                winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(label, 1);
                rowGrid.Children().Append(iconTile);
                rowGrid.Children().Append(label);

                auto const isSelected = activeFavoriteScore > 0 &&
                    SidebarPathsEqual(recentPath, activeFavoritePath);
                auto surface = BuildSidebarRowSurface(isSelected);
                surface.Child(rowGrid);
                btn.Content(surface);

                auto const targetPath = recentPath;
                btn.Click([this, targetPath](auto const&, auto const&)
                {
                    LoadDirectory(targetPath);
                });
                panel.Children().Append(btn);
            }
        }

        // Refresh the Bookmarks expander with user-pinned folders.
        UpdateBookmarksSection();
        // Refresh the This PC expander to reflect the current active pane's drive.
        UpdateThisPCSection();
    }

    void MainWindow::UpdateBookmarksSection()
    {
        auto const panel = BookmarksPanel();
        panel.Children().Clear();

        auto const& activePane = ActivePane();
        auto const bookmarks = m_favoritesService.GetPinnedFavorites();

        for (auto const& entry : bookmarks)
        {
            auto const isSelected =
                !entry.path.empty() &&
                !activePane.is_drives_view &&
                !activePane.current_path.empty() &&
                SidebarPathsEqual(entry.path, activePane.current_path);

            auto btn = winrt::Microsoft::UI::Xaml::Controls::Button{};
            StyleSidebarButtonBase(btn);
            btn.Margin({ 0, 0, 0, 2 });
            winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                btn, winrt::box_value(ToHString(entry.path.wstring())));

            auto rowGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
            auto iconColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            iconColumn.Width({ 20, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
            auto labelColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            labelColumn.Width({ 1, winrt::Microsoft::UI::Xaml::GridUnitType::Star });
            auto unpinColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            unpinColumn.Width({ 0, winrt::Microsoft::UI::Xaml::GridUnitType::Auto });
            rowGrid.ColumnDefinitions().Append(iconColumn);
            rowGrid.ColumnDefinitions().Append(labelColumn);
            rowGrid.ColumnDefinitions().Append(unpinColumn);
            rowGrid.ColumnSpacing(8);

            auto const entryIcon = m_shellInterop.GetItemIcon(entry.path, true);
            auto iconTile = BuildSidebarTile(
                BuildSidebarGlyph(entryIcon, L"folder"),
                20.0);

            auto label = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
            label.Text(ToHString(entry.display_name));
            label.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            label.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
            label.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
            label.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);

            // Unpin × button — transparent, minimal, right-aligned.
            auto unpinBtn = winrt::Microsoft::UI::Xaml::Controls::Button{};
            unpinBtn.Padding({ 4, 2, 4, 2 });
            unpinBtn.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
                winrt::Microsoft::UI::Colors::Transparent() });
            unpinBtn.BorderThickness({ 0, 0, 0, 0 });
            auto unpinIcon = BuildSidebarFontIcon(
                L"\uE711",
                10.0,
                L"TextFillColorSecondaryBrush");
            unpinIcon.Opacity(0.65);
            unpinBtn.Content(unpinIcon);
            winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                unpinBtn, winrt::box_value(winrt::hstring{ L"Unpin" }));

            auto const pinnedPath = entry.path;
            unpinBtn.Click([this, pinnedPath](auto const&, auto const&)
            {
                (void)m_favoritesService.RemovePinnedFavorite(pinnedPath);
                BuildSidebarFavorites();
            });

            btn.RightTapped([this, pinnedPath, btn](auto const&, auto const& args)
            {
                auto flyout = winrt::Microsoft::UI::Xaml::Controls::MenuFlyout{};
                auto unpinItem = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
                unpinItem.Text(winrt::hstring{ L"Unpin" });
                unpinItem.Click([this, pinnedPath](auto const&, auto const&)
                {
                    (void)m_favoritesService.RemovePinnedFavorite(pinnedPath);
                    BuildSidebarFavorites();
                });
                flyout.Items().Append(unpinItem);

                winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowOptions options;
                options.ShowMode(
                    winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowMode::Transient);
                options.Position(args.GetPosition(btn));
                flyout.ShowAt(btn, options);
                args.Handled(true);
            });

            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(iconTile, 0);
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(label, 1);
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(unpinBtn, 2);
            rowGrid.Children().Append(iconTile);
            rowGrid.Children().Append(label);
            rowGrid.Children().Append(unpinBtn);

            auto surface = BuildSidebarRowSurface(isSelected);
            surface.Child(rowGrid);
            btn.Content(surface);

            btn.Click([this, pinnedPath](auto const&, auto const&)
            {
                LoadDirectory(pinnedPath);
            });
            panel.Children().Append(btn);
        }
    }

    void MainWindow::UpdateThisPCSection()
    {
        auto const panel = ThisPCPanel();
        panel.Children().Clear();

        auto const& activePane = ActivePane();
        auto const drives = BuildSidebarDriveUsage(m_shellInterop);
        for (auto const& drive : drives)
        {
            auto button = winrt::Microsoft::UI::Xaml::Controls::Button{};
            StyleSidebarButtonBase(button);
            button.Margin({ 0, 0, 0, 2 });
            winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                button,
                winrt::box_value(ToHString(drive.path.wstring())));

            auto const driveSelected =
                !activePane.is_drives_view &&
                !activePane.current_path.empty() &&
                _wcsnicmp(
                    activePane.current_path.root_path().wstring().c_str(),
                    drive.path.wstring().c_str(),
                    drive.path.wstring().size()) == 0;

            auto card = BuildSidebarRowSurface(driveSelected);
            card.Padding({ 8, 6, 8, 6 });

            auto stack = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
            stack.Spacing(6);

            auto headerGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
            auto driveIconColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            driveIconColumn.Width({ 22, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
            auto driveLabelColumn = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            driveLabelColumn.Width({ 1, winrt::Microsoft::UI::Xaml::GridUnitType::Star });
            headerGrid.ColumnDefinitions().Append(driveIconColumn);
            headerGrid.ColumnDefinitions().Append(driveLabelColumn);
            headerGrid.ColumnSpacing(8);

            auto iconTile = BuildSidebarTile(BuildDriveTypeIcon(drive.drive_type), 18.0);

            auto labelStack = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
            labelStack.Spacing(2);

            auto title = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
            title.Text(ToHString(drive.display_name));
            title.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            title.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

            auto details = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
            details.Text(ToHString(BuildDriveUsageText(drive)));
            details.FontSize(11);
            details.Opacity(0.62);
            details.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

            labelStack.Children().Append(title);
            labelStack.Children().Append(details);

            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(iconTile, 0);
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(labelStack, 1);
            headerGrid.Children().Append(iconTile);
            headerGrid.Children().Append(labelStack);

            stack.Children().Append(headerGrid);
            if (drive.has_usage)
            {
                bool const isCritical = drive.used_ratio > 0.85;
                if (isCritical)
                {
                    details.Opacity(1.0);
                    details.Foreground(LookupBrush(L"SystemFillColorCriticalBrush"));
                }
                auto bar = winrt::Microsoft::UI::Xaml::Controls::ProgressBar{};
                bar.Minimum(0);
                bar.Maximum(100);
                bar.Value(drive.used_ratio * 100.0);
                bar.Height(4);
                bar.Background(LookupBrush(L"SubtleFillColorSecondaryBrush"));
                if (isCritical)
                {
                    bar.Foreground(LookupBrush(L"SystemFillColorCriticalBrush"));
                }
                stack.Children().Append(bar);
            }
            card.Child(stack);

            auto const drivePath = drive.path;
            button.Click([this, drivePath](auto const&, auto const&)
            {
                LoadDirectory(drivePath);
            });
            button.Content(card);
            panel.Children().Append(button);
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
            pane.is_archive_view = false;
            pane.items = items;
            pane.selection.Clear();

            pane.tabs[pane.active_tab_index].path = std::filesystem::path{};
            pane.tabs[pane.active_tab_index].items = items;
            pane.tabs[pane.active_tab_index].selection = {};
            pane.tabs[pane.active_tab_index].selected_item_index = -1;
            pane.tabs[pane.active_tab_index].is_drives_view = true;
            pane.tabs[pane.active_tab_index].is_archive_view = false;
            UpdateTabHeader(pane.active_tab_index);

            BreadcrumbPanel().Children().Clear();
            SearchTextBox().PlaceholderText(L"Search This PC\u2026");
            UpdateRows(m_activePaneIndex, items);

            if (pane.active_list_view)
            {
                pane.tab_switching = true;
                pane.active_list_view.SelectedIndex(-1);
                pane.tab_switching = false;
            }

            ResetSelectionDetails();
            UpdateNavButtons();

            StatusTextBlock().Text(
                ToHString(std::to_wstring(items.size()) + L" drive(s) available."));
        }
        catch (...)
        {
            StatusTextBlock().Text(L"Drive enumeration failed.");
        }
    }

    void MainWindow::RefreshDualPaneButton()
    {
        auto const tooltip = m_dualPaneVisible
            ? winrt::hstring{ L"Hide the secondary pane" }
            : winrt::hstring{ L"Show the secondary pane" };
        winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
            DualPaneButton(),
            winrt::box_value(tooltip));
        DualPaneButton().Opacity(m_dualPaneVisible ? 1.0 : 0.9);
    }

    void MainWindow::ApplyPaneLayoutVisibility()
    {
        using winrt::Microsoft::UI::Xaml::GridLength;
        using winrt::Microsoft::UI::Xaml::GridUnitType;
        using winrt::Microsoft::UI::Xaml::Visibility;

        if (!m_dualPaneVisible && m_activePaneIndex == 1)
        {
            SetActivePaneIndex(0);
        }

        if (m_dualPaneVisible)
        {
            RightPaneColumn().Width(GridLength{ 1.0, GridUnitType::Star });
            RightPaneBorder().Visibility(Visibility::Visible);
        }
        else
        {
            RightPaneColumn().Width(GridLength{ 0.0, GridUnitType::Pixel });
            RightPaneBorder().Visibility(Visibility::Collapsed);
        }

        RefreshDualPaneButton();
    }

    // R13e: toggle the sidebar / preview panel on or off.
    void MainWindow::TogglePreviewPanel()
    {
        using winrt::Microsoft::UI::Xaml::GridLength;
        using winrt::Microsoft::UI::Xaml::GridUnitType;
        using winrt::Microsoft::UI::Xaml::Visibility;

        m_previewPanelVisible = !m_previewPanelVisible;
        if (m_previewPanelVisible)
        {
            SidebarColumn().Width(GridLength{ 260.0, GridUnitType::Pixel });
            SidebarBorder().Visibility(Visibility::Visible);
        }
        else
        {
            SidebarColumn().Width(GridLength{ 0.0, GridUnitType::Pixel });
            SidebarBorder().Visibility(Visibility::Collapsed);
        }
    }

    // R13e: toggle the right pane on or off; switches to left pane if right was active.
    void MainWindow::ToggleDualPane()
    {
        m_dualPaneVisible = !m_dualPaneVisible;
        ApplyPaneLayoutVisibility();
        StatusTextBlock().Text(m_dualPaneVisible
            ? L"Dual pane enabled."
            : L"Single-pane beta layout restored.");
    }
}
