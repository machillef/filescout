#include "pch.h"
#include <cmath>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Storage.Streams.h>
#include <robuffer.h>
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "Domain/WorkerCount.h"
#include "Domain/AddressNavigation.h"
#include "Domain/TextFold.h"
#include "Domain/BatchRenameInput.h"
#include "Domain/ContextMenuAnchor.h"
#include "Domain/DragDropPlanning.h"
#include "Domain/DriveCapacity.h"
#include "Domain/FileNameValidation.h"
#include "Domain/NavigationHistory.h"
#include "Domain/KeyboardShortcuts.h"
#include "Domain/NavigationInput.h"
#include "Domain/PaneLoadTarget.h"
#include "Domain/RenamePolicy.h"
#include "Domain/RowIconState.h"
#include "Domain/ShellItemSort.h"
#include "Domain/SidebarRecents.h"
#include "Domain/ThemeBrushKeys.h"
#include "Domain/ThemeService.h"
#include "Domain/TypeAhead.h"
#include "Domain/Wave1FeatureGate.h"
#include "Search/SearchFilter.h"
#include "Search/SearchPresentation.h"
#include "Shell/ComApartment.h"
#include "Shell/TempStagingCleanup.h"
#include "TraceHelper.h"

#include <cassert>
#include "Bench.h"
#include "NavDiag.h"
#include "Domain/ParallelFor.h"

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
        assert(::FileScoutNative::Domain::IsKnownThemeBrushKey(key) && "add the key to Domain/ThemeBrushKeys.h");
        try
        {
            return winrt::Microsoft::UI::Xaml::Application::Current()
                .Resources()
                .Lookup(winrt::box_value(ToHString(std::wstring{ key })))
                .as<winrt::Microsoft::UI::Xaml::Media::Brush>();
        }
        catch (...)
        {
            TraceLog(L"LookupBrush: missing theme resource " + std::wstring{ key });
            assert(!"LookupBrush: missing theme resource (see TraceLog)");
            return nullptr;
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
        auto stageRoot = ::FileScoutNative::Shell::DragStagingRoot(std::filesystem::temp_directory_path()) /
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

        auto const isEditable = [](winrt::Windows::Foundation::IInspectable const& element)
        {
            return element.try_as<winrt::Microsoft::UI::Xaml::Controls::TextBox>() != nullptr ||
                element.try_as<winrt::Microsoft::UI::Xaml::Controls::RichEditBox>() != nullptr ||
                element.try_as<winrt::Microsoft::UI::Xaml::Controls::PasswordBox>() != nullptr;
        };

        if (isEditable(focusedElement))
        {
            return true;
        }

        auto dependencyObject = focusedElement.try_as<winrt::Microsoft::UI::Xaml::DependencyObject>();
        while (dependencyObject)
        {
            if (isEditable(dependencyObject))
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

    winrt::Microsoft::UI::Xaml::Media::ImageSource BuildIconSource(
        ::FileScoutNative::Domain::IconBitmap const& icon)
    {
        if (icon.IsEmpty())
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

            auto const& src = *icon.rgba;
            for (std::size_t i = 0; i + 3 < src.size(); i += 4)
            {
                uint8_t const r = src[i];
                uint8_t const g = src[i + 1];
                uint8_t const b = src[i + 2];
                uint8_t const a = src[i + 3];
                dest[i]     = static_cast<uint8_t>(b * a / 255u);
                dest[i + 1] = static_cast<uint8_t>(g * a / 255u);
                dest[i + 2] = static_cast<uint8_t>(r * a / 255u);
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
        return error.IsCanceled();
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
        return ::FileScoutNative::Domain::ParseBatchRenamePositiveInteger(value);
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
        return ::FileScoutNative::Domain::BatchRenameCaseTransformFromIndex(selectedIndex);
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

    using ::FileScoutNative::Domain::SidebarDriveUsage;

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

    std::wstring BuildFileOperationErrorStatus(
        std::wstring_view const prefix,
        std::wstring_view const detail)
    {
        std::wstring status{ prefix };
        status += L".";
        if (!detail.empty())
        {
            status += L" ";
            status += detail;
        }
        return status;
    }

    std::wstring_view GlyphForMenuIconKey(std::wstring_view const iconKey)
    {
        using namespace std::literals;
        if (iconKey == L"open"sv)                   return L"\uE838";
        if (iconKey == L"openInNewTab"sv)           return L"\uE8A7";
        if (iconKey == L"openWith"sv)               return L"\uE7AC";
        if (iconKey == L"rename"sv)                 return L"\uE8AC";
        if (iconKey == L"delete"sv)                 return L"\uE74D";
        if (iconKey == L"copy"sv)                   return L"\uE8C8";
        if (iconKey == L"copyPath"sv)               return L"\uE71B";
        if (iconKey == L"cut"sv)                    return L"\uE8C6";
        if (iconKey == L"paste"sv)                  return L"\uE77F";
        if (iconKey == L"terminal"sv)               return L"\uE756";
        if (iconKey == L"newFolder"sv)              return L"\uE8F4";
        if (iconKey == L"newFile"sv)                return L"\uE8A5";
        if (iconKey == L"refresh"sv)                return L"\uE72C";
        if (iconKey == L"pin"sv)                    return L"\uE840";
        if (iconKey == L"properties"sv)             return L"\uE946";
        if (iconKey == L"overflow"sv)               return L"\uE712";
        if (iconKey == L"undo"sv)                   return L"\uE7A7";
        if (iconKey == L"archive"sv)                return L"\uF012";
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

    winrt::Microsoft::UI::Xaml::FrameworkElement BuildCachedSidebarGlyph(
        std::filesystem::path const& path,
        ::FileScoutNative::Shell::ShellInteropService const& shellInterop,
        std::unordered_map<std::wstring, winrt::Microsoft::UI::Xaml::Media::ImageSource>& iconSourceCache,
        std::wstring const& fallbackText,
        long long* iconDurationUs = nullptr)
    {
        if (!path.empty())
        {
            auto const cacheKey = L"__sidebar_img__:" + ::FileScoutNative::Domain::SidebarPathKey(path);
            auto it = iconSourceCache.find(cacheKey);
            winrt::Microsoft::UI::Xaml::Media::ImageSource source{ nullptr };
            if (it != iconSourceCache.end())
            {
                source = it->second;
            }
            else
            {
                ScopedTimer iconTimer{ L"GetItemIcon: " + path.wstring() };
                auto const iconBegin = iconDurationUs ? ::FileScoutNative::NavDiag::Now() : ::FileScoutNative::NavDiag::Tick{};
                auto const bitmap = shellInterop.GetItemIcon(path, true);
                if (iconDurationUs)
                {
                    *iconDurationUs += ::FileScoutNative::NavDiag::SpanUs(iconBegin, ::FileScoutNative::NavDiag::Now());
                }
                if (auto built = BuildIconSource(bitmap))
                {
                    iconSourceCache.emplace(cacheKey, built);
                    source = std::move(built);
                }
            }

            if (source)
            {
                auto image = winrt::Microsoft::UI::Xaml::Controls::Image{};
                image.Width(16);
                image.Height(16);
                image.Stretch(winrt::Microsoft::UI::Xaml::Media::Stretch::Uniform);
                image.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
                image.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
                image.Source(source);
                return image;
            }
        }

        return BuildSidebarGlyph(std::nullopt, fallbackText);
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
        bool const isListMode = (presentation == RowPresentation::List);
        auto const style = rootGrid.Resources()
            .Lookup(winrt::box_value(RowItemStyleKeyForPresentation(presentation)))
            .as<winrt::Microsoft::UI::Xaml::Style>();
        listView.ItemContainerStyle(style);

        auto const tmpl = rootGrid.Resources()
            .Lookup(winrt::box_value(RowTemplateKeyForPresentation(presentation)))
            .as<winrt::Microsoft::UI::Xaml::DataTemplate>();
        listView.ItemTemplate(tmpl);

        auto const hMode = isListMode
            ? winrt::Microsoft::UI::Xaml::Controls::ScrollMode::Auto
            : winrt::Microsoft::UI::Xaml::Controls::ScrollMode::Disabled;
        auto const hVis = isListMode
            ? winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Auto
            : winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Disabled;
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer::SetHorizontalScrollMode(listView, hMode);
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer::SetHorizontalScrollBarVisibility(listView, hVis);

        if (auto const scrollViewer =
                FindDescendantByType<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>(listView))
        {
            scrollViewer.HorizontalScrollMode(hMode);
            scrollViewer.HorizontalScrollBarVisibility(hVis);
            if (!isListMode && scrollViewer.HorizontalOffset() > 0.5)
            {
                scrollViewer.ChangeView(
                    winrt::box_value(0.0).as<winrt::Windows::Foundation::IReference<double>>(),
                    nullptr,
                    nullptr,
                    true);
            }
        }
    }

    void ApplyListColumnWidthsToContainer(
        winrt::Microsoft::UI::Xaml::DependencyObject const& container,
        ::FileScoutNative::Domain::ListColumnWidths const& widths,
        double const trailingMinWidth)
    {
        auto const selectorItem = container.try_as<winrt::Microsoft::UI::Xaml::Controls::Primitives::SelectorItem>();
        if (!selectorItem)
        {
            return;
        }
        auto const root = selectorItem.ContentTemplateRoot().try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
        if (!root)
        {
            return;
        }
        auto const rowGrid = root.FindName(L"ListRowGrid").try_as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
        if (!rowGrid)
        {
            return;
        }
        auto const cols = rowGrid.ColumnDefinitions();
        if (cols.Size() < 5)
        {
            return;
        }
        cols.GetAt(1).Width({ widths.name, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
        cols.GetAt(2).Width({ widths.type, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
        cols.GetAt(3).Width({ widths.size, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
        cols.GetAt(4).Width({ widths.date, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
        if (cols.Size() >= 6)
        {
            cols.GetAt(5).MinWidth(trailingMinWidth > 0.0 ? trailingMinWidth : 0.0);
        }
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
        default:
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

        canvas.Children().Append(MakeDriveBorder(
            15, 10, { 2.5f, 2.5f, 2.5f, 2.5f }, colors.body,
            VA::VerticalAlignment::Bottom, VA::HorizontalAlignment::Center, { 0, 0, 0, 1 }));

        canvas.Children().Append(MakeDriveBorder(
            15, 6, { 2.5f, 2.5f, 0, 0 }, colors.cap,
            VA::VerticalAlignment::Top, VA::HorizontalAlignment::Center, { 0, 2, 0, 0 }));

        canvas.Children().Append(MakeDriveBorder(
            3, 3, { 1.5f, 1.5f, 1.5f, 1.5f }, colors.led,
            VA::VerticalAlignment::Top, VA::HorizontalAlignment::Right, { 0, 3, 2, 0 }));

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

        return ::FileScoutNative::Shell::ArchiveOpenRoot(std::filesystem::temp_directory_path()) /
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

        // std::wstring(count, ch): braces would pick the initializer-list constructor (two characters).
        return prefix + std::wstring(1, ::FileScoutNative::Domain::NameGroupInitial(item.display_name));
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

    // Runs `action` on the UI thread once the frame after now has been presented: Rendering fires
    // before a frame's layout and render, so the second callback means the first one's frame is
    // on screen. A minimised window renders no frames, so a 250 ms timer runs it anyway.
    template <typename Action>
    void RunAfterFramePresented(Action&& action)
    {
        struct State
        {
            std::decay_t<Action> action;
            int frames = 0;
            bool done = false;
            winrt::event_token token{};
            winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer{ nullptr };
        };
        auto state = std::make_shared<State>(State{ std::forward<Action>(action) });
        auto run = [state](::FileScoutNative::NavDiag::StagedTrigger const trigger)
        {
            if (state->done)
            {
                return;
            }
            state->done = true;
            winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(state->token);
            // Clearing the timer breaks the state -> timer -> Tick handler -> state cycle.
            if (state->timer)
            {
                state->timer.Stop();
                state->timer = nullptr;
            }
            auto action = std::move(state->action);
            if constexpr (std::is_invocable_v<decltype(action)&, ::FileScoutNative::NavDiag::StagedTrigger>)
            {
                action(trigger);
            }
            else
            {
                action();
            }
        };
        state->token = winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(
            [state, run](auto&&, auto&&)
            {
                if (++state->frames >= 2)
                {
                    run(::FileScoutNative::NavDiag::StagedTrigger::Rendering2);
                }
            });
        state->timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        state->timer.Interval(std::chrono::milliseconds(250));
        state->timer.IsRepeating(false);
        state->timer.Tick([run](auto&&, auto&&) { run(::FileScoutNative::NavDiag::StagedTrigger::FallbackTimer); });
        state->timer.Start();
    }

    struct NavDiagSidebarClick
    {
        NavDiagSidebarClick() noexcept { ::FileScoutNative::NavDiag::Recorder::Instance().BeginSidebarClick(); }
        ~NavDiagSidebarClick()
        {
            try { ::FileScoutNative::NavDiag::Recorder::Instance().EndSidebarClick(); }
            catch (...) {}
        }
        NavDiagSidebarClick(NavDiagSidebarClick const&) = delete;
        NavDiagSidebarClick& operator=(NavDiagSidebarClick const&) = delete;
    };

    void NavDiagEmit(::FileScoutNative::NavDiag::Trace const& trace)
    {
        try { ::FileScoutNative::NavDiag::Recorder::Instance().Emit(trace); }
        catch (...) {}
    }

    void NavDiagEmitAfterFrames(
        ::FileScoutNative::NavDiag::Trace trace,
        std::function<bool()> stillCurrent)
    {
        if (!trace.active)
        {
            return;
        }
        struct State
        {
            ::FileScoutNative::NavDiag::Trace trace;
            std::function<bool()> stillCurrent;
            int frames = 0;
            bool done = false;
            winrt::event_token token{};
            winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer{ nullptr };
        };
        auto state = std::make_shared<State>();
        state->trace = trace;
        state->stillCurrent = std::move(stillCurrent);
        auto finish = [state](bool const observed)
        {
            if (state->done)
            {
                return;
            }
            state->done = true;
            winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(state->token);
            if (state->timer)
            {
                state->timer.Stop();
                state->timer = nullptr;
            }
            if (observed)
            {
                state->trace.frame_after_final = ::FileScoutNative::NavDiag::Now();
                state->trace.frame_observed = true;
            }
            NavDiagEmit(state->trace);
        };
        state->token = winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(
            [state, finish](auto&&, auto&&)
            {
                if (state->stillCurrent && !state->stillCurrent())
                {
                    finish(false);
                    return;
                }
                if (++state->frames >= 2)
                {
                    finish(true);
                }
            });
        state->timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        state->timer.Interval(std::chrono::milliseconds(1000));
        state->timer.IsRepeating(false);
        state->timer.Tick([finish](auto&&, auto&&) { finish(false); });
        state->timer.Start();
    }

    void NavDiagSuperseded(::FileScoutNative::NavDiag::Trace trace)
    {
        trace.outcome = ::FileScoutNative::NavDiag::Outcome::Superseded;
        NavDiagEmit(trace);
    }

    void BenchMarkAfterFrames(std::wstring name, int frames, std::function<void()> then = {})
    {
        auto& recorder = ::FileScoutNative::Bench::Recorder::Instance();
        if (!recorder.Enabled() || recorder.HasMark(name))
        {
            return;
        }
        auto remaining = std::make_shared<int>(frames);
        auto token = std::make_shared<winrt::event_token>();
        *token = winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(
            [remaining, token, name = std::move(name), then = std::move(then)](auto&&, auto&&)
            {
                if (--*remaining > 0)
                {
                    return;
                }
                winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(*token);
                ::FileScoutNative::Bench::Recorder::Instance().Mark(name);
                if (then)
                {
                    then();
                }
            });
    }

    void BenchRunScroll(winrt::Microsoft::UI::Xaml::Controls::ScrollViewer const& viewer)
    {
        auto& recorder = ::FileScoutNative::Bench::Recorder::Instance();
        if (!viewer)
        {
            recorder.Mark(L"scroll_skipped");
            recorder.Write();
            return;
        }
        struct State
        {
            int frame = 0;
            double lastMs = -1.0;
            winrt::event_token token{};
        };
        auto state = std::make_shared<State>();
        recorder.Mark(L"scroll_start");
        state->token = winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(
            [state, viewer](auto&&, auto&&)
            {
                constexpr int k_frames = 300;
                constexpr double k_stepPx = 96.0;
                auto& rec = ::FileScoutNative::Bench::Recorder::Instance();
                double const now = rec.NowMs();
                if (state->lastMs >= 0.0)
                {
                    rec.AddFrameInterval(now - state->lastMs);
                }
                state->lastMs = now;
                if (++state->frame > k_frames)
                {
                    winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(state->token);
                    rec.Mark(L"scroll_done");
                    rec.Write();
                    return;
                }
                viewer.ChangeView(nullptr, winrt::box_value(viewer.VerticalOffset() + k_stepPx).as<winrt::Windows::Foundation::IReference<double>>(), nullptr, true);
            });
    }

    bool BenchTargets(std::filesystem::path const& path)
    {
        auto const& target = ::FileScoutNative::Bench::Recorder::Instance().TargetPath();
        if (target.empty())
        {
            return false;
        }
        std::error_code ec;
        return std::filesystem::equivalent(path, std::filesystem::path{ target }, ec);
    }
}

namespace winrt::FileScoutNative::implementation
{
    MainWindow::MainWindow()
    {
        {
            ScopedTimer timer{ L"InitializeComponent" };
            InitializeComponent();
            m_dispatcherQueue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        }
        m_userSettings = ::FileScoutNative::Domain::UserSettingsService::Load();
        {
            ScopedTimer timer{ L"ConfigureWindow" };
            ConfigureWindow();
        }
        {
            ScopedTimer timer{ L"SetupKeyboardAccelerators" };
            SetupKeyboardAccelerators();
            SetupMouseNavigationHandlers();
        }
        {
            ScopedTimer timer{ L"InitializeThemeDropDown" };
            m_currentThemeId = ::FileScoutNative::Domain::ThemeService::LoadSavedThemeId();
            m_dualPaneVisible = m_userSettings.has_dual_pane_setting
                ? m_userSettings.dual_pane_visible
                : !::FileScoutNative::Domain::Wave1UsesSinglePaneFirstLaunch();
            ApplyLoadedUserSettingsToPanes();
            InitializeThemeDropDown();
            InitializeViewOptionsDropDown();
        }

        this->Closed([this](auto const&, auto const&)
            {
                m_windowClosing = true;
                SaveWindowBoundsSettings();
                StopPaneDirectoryWatcher(0);
                StopPaneDirectoryWatcher(1);
            });

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

        auto const launchPath = winrt::FileScoutNative::implementation::App::LaunchPath();
        auto const startPath = launchPath.value_or(m_favoritesService.GetUserHomePath());
        TraceLog(L"MainWindow ctor: startPath=" + startPath.wstring());

        {
            ScopedTimer timer{ L"InitPane(0)" };
            InitPane(0, startPath);
        }
        {
            ScopedTimer timer{ L"InitPane(1)" };
            InitPane(1, startPath);
        }

        BuildSidebarFavorites();

        {
            ScopedTimer timer{ L"BuildSortHeaders" };
            BuildSortHeader(0);
            BuildSortHeader(1);
        }

        m_activePaneIndex = 0;
        UpdateActivePaneIndicator();
        ApplyPaneLayoutVisibility();
        RefreshDualPaneButton();

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
                BenchMarkAfterFrames(L"window_first_frame", 1);
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
        ApplyTitleBarCaptionColors();
    }

    void MainWindow::InitializeViewOptionsDropDown()
    {
        RefreshViewOptionsDropDown();
    }

    void MainWindow::RefreshViewOptionsDropDown()
    {
        auto const& pane = ActivePane();
        auto flyout = winrt::Microsoft::UI::Xaml::Controls::MenuFlyout{};

        auto hiddenItem = winrt::Microsoft::UI::Xaml::Controls::ToggleMenuFlyoutItem{};
        hiddenItem.Text(L"Show hidden files (Ctrl+H)");
        hiddenItem.IsChecked(pane.show_hidden_files);
        hiddenItem.Click([this](auto const&, auto const&)
            {
                ToggleHiddenFiles(m_activePaneIndex);
            });
        flyout.Items().Append(hiddenItem);

        auto extItem = winrt::Microsoft::UI::Xaml::Controls::ToggleMenuFlyoutItem{};
        extItem.Text(L"Show file extensions");
        extItem.IsChecked(pane.show_file_extensions);
        extItem.Click([this](auto const&, auto const&)
            {
                ToggleExtensions(m_activePaneIndex);
            });
        flyout.Items().Append(extItem);

        ViewOptionsDropDownButton().Flyout(flyout);
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

        flyout.Items().Append(winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutSeparator{});
        {
            auto item = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
            winrt::Microsoft::UI::Xaml::Controls::FontIcon icon;
            item.Icon(icon);
            winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(item, L"DefaultFileManagerMenuItem");
            auto const shownMakeDefault = std::make_shared<bool>(true);
            auto const refresh = [this, shownMakeDefault, weakItem = winrt::make_weak(item), weakIcon = winrt::make_weak(icon)]()
            {
                auto const item = weakItem.get();
                auto const icon = weakIcon.get();
                if (!item || !icon)
                {
                    return;
                }
                bool const isDefault = m_defaultFileManager.GetStatus() ==
                    ::FileScoutNative::Shell::DefaultFileManagerStatus::FullyDefault;
                *shownMakeDefault = !isDefault;
                item.Text(isDefault ? L"Restore Explorer as default file manager"
                                    : L"Make FileScout the default file manager");
                icon.Glyph(isDefault ? L"\uE7A7" : L"\uE8B7");
            };
            refresh();
            flyout.Opening([refresh](auto const&, auto const&) { refresh(); });
            item.Click([this, shownMakeDefault](auto const&, auto const&)
                {
                    SetDefaultFileManager(*shownMakeDefault);
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

    void MainWindow::SetDefaultFileManager(bool const makeDefault)
    {
        bool const ok = makeDefault ? m_defaultFileManager.SetAsDefault() : m_defaultFileManager.RestoreDefault();
        TraceLog(std::wstring{ L"[default-fm] " } + (makeDefault ? L"set" : L"restore") + (ok ? L" ok" : L" FAILED"));
        StatusTextBlock().Text(
            !ok ? (makeDefault ? L"Could not make FileScout the default file manager."
                               : L"Could not restore Explorer as the default file manager.")
                : (makeDefault ? L"FileScout now opens folders, drives and Win+E."
                               : L"Explorer is the default file manager again."));
        RefreshThemeDropDown();
    }

    void MainWindow::RefreshThemeSensitiveChrome()
    {
        ApplyTitleBarCaptionColors();
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
        RefreshSidebarCaches();
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
        pane.recursive_search_running = false;
        UpdatePaneProgressBar(paneIndex);

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

        if (restoreSnapshot)
        {
            FlushPendingWatchRefresh(paneIndex);
        }
    }

    void MainWindow::SearchTextBox_KeyDown(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        auto& pane = ActivePane();

        if (args.Key() == winrt::Windows::System::VirtualKey::Escape)
        {
            if (pane.search_mode == SearchMode::None && pane.pending_filter.HasPending())
            {
                args.Handled(true);
                pane.pending_filter.Clear();
                SearchTextBox().Text(L"");
                return;
            }
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

            pane.recursive_search_running = true;
            UpdatePaneProgressBar(m_activePaneIndex);

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

                    p.recursive_search_running = false;
                    UpdatePaneProgressBar(paneIndex);
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

        if (pane.search_mode == SearchMode::Recursive) return;

        std::wstring query{ SearchTextBox().Text() };

        if (query.empty())
        {
            pane.pending_filter.Clear();
            if (pane.search_mode == SearchMode::Filter)
            {
                ResetSearchState(m_activePaneIndex, true);
                StatusTextBlock().Text(ToHString(
                    std::to_wstring(pane.items.size()) + L" items  |  " +
                    RowPresentationStatusLabel(pane)));
            }
            return;
        }

        if (pane.search_mode == SearchMode::None)
        {
            if (!pane.pending_filter.OnQueryChanged(query, pane.awaiting_final_batch))
            {
                StatusTextBlock().Text(ToHString(::FileScoutNative::Domain::BuildPendingFilterStatus(query)));
                return;
            }
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

    void MainWindow::EndSearchOnNavigation(int32_t const paneIndex, bool const restoreSnapshot)
    {
        auto& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        if (pane.search_mode != SearchMode::None)
        {
            ResetSearchState(paneIndex, restoreSnapshot);
        }
        else if (pane.pending_filter.HasPending())
        {
            pane.pending_filter.Clear();
            if (paneIndex == m_activePaneIndex)
            {
                SearchTextBox().Text(L"");
            }
        }
    }

    void MainWindow::ApplyPendingFilter(int32_t const paneIndex)
    {
        auto& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        if (!pane.pending_filter.TakeOnFinalPublish().has_value() || paneIndex != m_activePaneIndex)
        {
            return;
        }

        SearchTextBox_TextChanged(nullptr, nullptr);
    }

    void MainWindow::FileRowsListView_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&)
    {
        if (ActivePane().tab_switching || !ActivePane().active_list_view)
        {
            return;
        }

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
        scenario.is_read_only_archive_view = ActivePane().is_archive_view;
        scenario.is_recycle_bin_view = ActivePane().is_recycle_bin_view;
        scenario.is_drives_view = ActivePane().is_drives_view;
        scenario.can_paste =
            !ActivePane().is_archive_view &&
            !ActivePane().is_recycle_bin_view &&
            m_shellClipboardService.CanPasteInto(ActivePane().current_path);
        auto const selectedIndices = GetSelectedItemIndices();
        scenario.selection_count = static_cast<uint32_t>(selectedIndices.size());
        {
            std::vector<std::filesystem::path> selectedPaths;
            selectedPaths.reserve(selectedIndices.size());
            for (int32_t const idx : selectedIndices)
            {
                if (auto const* selItem = TryGetDisplayedItemByIndex(idx))
                {
                    selectedPaths.push_back(selItem->path);
                }
            }
            scenario.can_compress_to_zip =
                !ActivePane().is_drives_view &&
                !ActivePane().is_recycle_bin_view &&
                !ActivePane().is_archive_view &&
                ::FileScoutNative::Domain::TryGetCommonParentDirectory(selectedPaths).has_value();
        }
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

        // Shift+F10 / the Menu key raise ContextRequested without a pointer position; the mouse
        // cursor is then unrelated to the menu, so record no click point.
        m_hasLastContextMenuPoint = false;
        if (winrt::Windows::Foundation::Point pointerPosition{};
            args.TryGetPosition(nullptr, pointerPosition))
        {
            if (POINT point{}; GetCursorPos(&point))
            {
                m_lastContextMenuPoint = point;
                m_hasLastContextMenuPoint = true;
            }
        }

        if (ActivePane().active_list_view)
        {
            ActivePane().active_list_view.SelectedIndex(-1);
            SelectItemByIndex(-1);
        }

        ::FileScoutNative::Shell::ContextMenuScenario scenario;
        scenario.is_read_only_archive_view = ActivePane().is_archive_view;
        scenario.is_recycle_bin_view = ActivePane().is_recycle_bin_view;
        scenario.is_drives_view = ActivePane().is_drives_view;
        scenario.can_paste =
            !ActivePane().is_archive_view &&
            !ActivePane().is_recycle_bin_view &&
            m_shellClipboardService.CanPasteInto(ActivePane().current_path);
        auto const selectedIndices = GetSelectedItemIndices();
        scenario.selection_count = static_cast<uint32_t>(selectedIndices.size());
        {
            std::vector<std::filesystem::path> selectedPaths;
            selectedPaths.reserve(selectedIndices.size());
            for (int32_t const idx : selectedIndices)
            {
                if (auto const* selItem = TryGetDisplayedItemByIndex(idx))
                {
                    selectedPaths.push_back(selItem->path);
                }
            }
            scenario.can_compress_to_zip =
                !ActivePane().is_drives_view &&
                !ActivePane().is_recycle_bin_view &&
                !ActivePane().is_archive_view &&
                ::FileScoutNative::Domain::TryGetCommonParentDirectory(selectedPaths).has_value();
        }
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

            struct ScopedPendingMutation
            {
                uint32_t& count;
                ScopedPendingMutation(uint32_t& c) : count(c) { ++count; }
                ~ScopedPendingMutation() { if (count > 0) { --count; } }
            };
            ScopedPendingMutation mutationGuard{ m_pendingMutationCount };

            auto const owner = GetWindowHandle();
            struct DropTransferOutcome
            {
                std::vector<::FileScoutNative::Domain::UndoPathPair> completed_items;
                std::optional<::FileScoutNative::Shell::FileOperationError> error;
                std::wstring std_error;
            };
            auto const dropOutcome = co_await OnShellThread([this, owner, sourcePaths, destinationPath, isMove]
                {
                    DropTransferOutcome out{};
                    std::vector<::FileScoutNative::Shell::TransferredItemPair> pairs;
                    try
                    {
                        m_shellFileOperationService.CopyItemsFromPaths(
                            owner, sourcePaths, destinationPath, isMove, &pairs);
                    }
                    catch (::FileScoutNative::Shell::FileOperationException const& err)
                    {
                        out.error = err.Error();
                    }
                    catch (std::exception const& err)
                    {
                        out.std_error = winrt::to_hstring(err.what()).c_str();
                    }

                    out.completed_items.reserve(pairs.size());
                    for (auto const& pair : pairs)
                    {
                        ::FileScoutNative::Domain::UndoPathPair undoPair{};
                        undoPair.source = pair.source;
                        undoPair.target = pair.destination;
                        undoPair.target_identity =
                            ::FileScoutNative::Shell::TryCaptureFileIdentity(pair.destination);
                        out.completed_items.push_back(std::move(undoPair));
                    }
                    return out;
                });

            if (!dropOutcome.completed_items.empty())
            {
                ::FileScoutNative::Domain::UndoEntry entry{};
                entry.kind = isMove
                    ? ::FileScoutNative::Domain::UndoActionKind::Move
                    : ::FileScoutNative::Domain::UndoActionKind::Copy;
                entry.description = (isMove ? L"Move " : L"Copy ") +
                    std::to_wstring(dropOutcome.completed_items.size()) +
                    (dropOutcome.completed_items.size() == 1 ? L" item" : L" items");
                entry.items = dropOutcome.completed_items;
                m_undoStack.Push(std::move(entry));

                std::vector<std::filesystem::path> affectedFolders{ destinationPath };
                if (isMove)
                {
                    for (auto const& pair : dropOutcome.completed_items)
                    {
                        if (pair.source.has_parent_path())
                        {
                            affectedFolders.push_back(pair.source.parent_path());
                        }
                    }
                }
                RefreshAffectedFolders(affectedFolders);
            }
            else if (!dropOutcome.error.has_value() && dropOutcome.std_error.empty())
            {
                RefreshAffectedFolders({ destinationPath });
            }

            if (dropOutcome.error.has_value())
            {
                throw ::FileScoutNative::Shell::FileOperationException(*dropOutcome.error);
            }
            if (!dropOutcome.std_error.empty())
            {
                StatusTextBlock().Text(ToHString(
                    BuildFileOperationErrorStatus(
                        isMove ? L"Couldn't move items" : L"Couldn't copy items",
                        dropOutcome.std_error)));
                deferral.Complete();
                co_return;
            }

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

        EndSearchOnNavigation(paneIndex);

        if (pane.tab_switching || pane.tabs.empty())
        {
            return;
        }

        auto const newIndex = tabView.SelectedIndex();
        if (newIndex < 0 || newIndex >= static_cast<int32_t>(pane.tabs.size()))
        {
            return;
        }

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
            pane.tabs[pane.active_tab_index].is_recycle_bin_view = pane.is_recycle_bin_view;
        }

        pane.active_tab_index = newIndex;

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
        titleBar.PreferredHeightOption(winrt::Microsoft::UI::Windowing::TitleBarHeightOption::Standard);
        ApplyTitleBarCaptionColors();

        SetTitleBar(TitleBarDragHost());

        m_titleBarPointerSource =
            winrt::Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(m_appWindow.Id());

        UpdateTitleBarInteractiveRegions();
    }

    void MainWindow::ApplyTitleBarCaptionColors()
    {
        if (!m_appWindow)
        {
            m_appWindow = this->AppWindow();
        }
        if (!m_appWindow)
        {
            return;
        }

        auto const titleBar = m_appWindow.TitleBar();
        HIGHCONTRASTW hc{ sizeof(hc) };
        bool const highContrast =
            ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) != FALSE &&
            (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
        if (highContrast)
        {
            titleBar.ButtonBackgroundColor(nullptr);
            titleBar.ButtonInactiveBackgroundColor(nullptr);
            titleBar.ButtonForegroundColor(nullptr);
            titleBar.ButtonInactiveForegroundColor(nullptr);
            titleBar.ButtonHoverForegroundColor(nullptr);
            titleBar.ButtonHoverBackgroundColor(nullptr);
            titleBar.ButtonPressedForegroundColor(nullptr);
            titleBar.ButtonPressedBackgroundColor(nullptr);
            return;
        }

        auto const theme = ::FileScoutNative::Domain::ThemeService::GetTheme(m_currentThemeId);
        auto const palette = ::FileScoutNative::Domain::BuildCaptionButtonPalette(theme);
        auto const toColor = [](std::uint32_t const argb)
        {
            return winrt::Windows::UI::Color{
                static_cast<std::uint8_t>((argb >> 24) & 0xFFu),
                static_cast<std::uint8_t>((argb >> 16) & 0xFFu),
                static_cast<std::uint8_t>((argb >> 8) & 0xFFu),
                static_cast<std::uint8_t>(argb & 0xFFu),
            };
        };

        titleBar.ButtonBackgroundColor(winrt::Microsoft::UI::Colors::Transparent());
        titleBar.ButtonInactiveBackgroundColor(winrt::Microsoft::UI::Colors::Transparent());
        titleBar.ButtonForegroundColor(toColor(palette.foreground));
        titleBar.ButtonInactiveForegroundColor(toColor(palette.inactive_foreground));
        titleBar.ButtonHoverForegroundColor(toColor(palette.foreground));
        titleBar.ButtonHoverBackgroundColor(toColor(palette.hover_background));
        titleBar.ButtonPressedForegroundColor(toColor(palette.foreground));
        titleBar.ButtonPressedBackgroundColor(toColor(palette.pressed_background));
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
        double const leftInset = scale > 0.0 ? (titleBar.LeftInset() / scale) : 0.0;
        double const rightInset = scale > 0.0 ? (titleBar.RightInset() / scale) : 0.0;
        TitleBarLeftInsetColumn().Width(winrt::Microsoft::UI::Xaml::GridLength{ leftInset });
        // Always reserve at least the standard caption-button width (138 DIPs) in case
        // RightInset reports 0 during early layout.
        TitleBarRightInsetColumn().Width(
            winrt::Microsoft::UI::Xaml::GridLength{ (std::max)(rightInset, 138.0) });

        m_titleBarPointerSource.ClearRegionRects(
            winrt::Microsoft::UI::Input::NonClientRegionKind::Passthrough);
    }

    void MainWindow::ConfigureWindow()
    {
        Title(L"FileScout Native");

        SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop{});
        ConfigureTitleBar();

        const auto hwnd = GetWindowHandle();
        const UINT dpi = GetDpiForWindow(hwnd);
        const float scale = static_cast<float>(dpi) / 96.0f;
        int32_t const defaultWidth = static_cast<int32_t>(1520 * scale);
        int32_t const defaultHeight = static_cast<int32_t>(920 * scale);

        std::vector<::FileScoutNative::Domain::ScreenWorkArea> monitors;
        ::EnumDisplayMonitors(
            nullptr,
            nullptr,
            [](HMONITOR const hmon, HDC, LPRECT, LPARAM const lParam) -> BOOL
            {
                auto* const list = reinterpret_cast<std::vector<::FileScoutNative::Domain::ScreenWorkArea>*>(lParam);
                MONITORINFO info{ sizeof(info) };
                if (::GetMonitorInfoW(hmon, &info))
                {
                    list->push_back({
                        info.rcWork.left,
                        info.rcWork.top,
                        info.rcWork.right,
                        info.rcWork.bottom,
                    });
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&monitors));

        ::FileScoutNative::Domain::ScreenWorkArea primaryWorkArea{};
        MONITORINFO monitor{ sizeof(monitor) };
        if (::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
        {
            primaryWorkArea = {
                monitor.rcWork.left,
                monitor.rcWork.top,
                monitor.rcWork.right,
                monitor.rcWork.bottom,
            };
        }

        auto const restored = ::FileScoutNative::Domain::ResolveRestoredWindowBounds(
            m_userSettings.window,
            monitors,
            primaryWorkArea,
            defaultWidth,
            defaultHeight);

        ::SetWindowPos(
            hwnd,
            nullptr,
            restored.x,
            restored.y,
            restored.width,
            restored.height,
            SWP_NOZORDER);

        m_lastNormalWindowBounds = restored;
        m_lastNormalWindowBounds.maximized = false;
        m_windowMaximized = restored.maximized;

        if (restored.maximized)
        {
            bool maximizedViaPresenter = false;
            try
            {
                if (m_appWindow)
                {
                    if (auto const presenter = m_appWindow.Presenter()
                        .try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>())
                    {
                        presenter.Maximize();
                        maximizedViaPresenter = true;
                    }
                }
            }
            catch (...)
            {
            }
            if (!maximizedViaPresenter)
            {
                ::ShowWindow(hwnd, SW_SHOWMAXIMIZED);
            }
        }

        static constexpr UINT_PTR navigationWindowSubclassId = 0x46534E56;
        SetWindowSubclass(
            hwnd,
            &MainWindow::NavigationWindowSubclassProc,
            navigationWindowSubclassId,
            reinterpret_cast<DWORD_PTR>(this));

        m_windowConfigured = true;
    }

    void MainWindow::ApplyLoadedUserSettingsToPanes()
    {
        auto const savedViewMode = m_userSettings.view_mode == ::FileScoutNative::Domain::SavedViewMode::Cards
            ? ViewMode::Cards
            : ViewMode::List;
        auto const savedSortColumn = [&]()
        {
            switch (m_userSettings.sort_column)
            {
            case ::FileScoutNative::Domain::SavedSortColumn::Type: return SortColumn::Type;
            case ::FileScoutNative::Domain::SavedSortColumn::Size: return SortColumn::Size;
            case ::FileScoutNative::Domain::SavedSortColumn::DateModified: return SortColumn::DateModified;
            case ::FileScoutNative::Domain::SavedSortColumn::Name:
            default:
                return SortColumn::Name;
            }
        }();
        auto const savedSortDirection =
            m_userSettings.sort_direction == ::FileScoutNative::Domain::SavedSortDirection::Descending
                ? SortDirection::Descending
                : SortDirection::Ascending;

        for (auto& pane : m_panes)
        {
            pane.list_column_widths = m_userSettings.list_column_widths;
            pane.view_mode = savedViewMode;
            pane.show_hidden_files = m_userSettings.show_hidden_files;
            pane.show_file_extensions = m_userSettings.show_file_extensions;
            pane.group_by_sort_column = m_userSettings.group_by_sort_column;
            pane.sort_column = savedSortColumn;
            pane.sort_direction = savedSortDirection;
        }
    }

    void MainWindow::CaptureLiveWindowBounds()
    {
        if (!m_windowConfigured || m_windowClosing)
        {
            return;
        }

        HWND const hwnd = GetWindowHandle();
        if (hwnd == nullptr || ::IsIconic(hwnd))
        {
            return;
        }

        bool const isMaximized = (::IsZoomed(hwnd) != FALSE);
        m_windowMaximized = isMaximized;
        if (!isMaximized)
        {
            RECT rc{};
            if (::GetWindowRect(hwnd, &rc))
            {
                int32_t const width = rc.right - rc.left;
                int32_t const height = rc.bottom - rc.top;
                if (width >= 400 && height >= 300)
                {
                    m_lastNormalWindowBounds = ::FileScoutNative::Domain::WindowBounds{
                        .x = rc.left,
                        .y = rc.top,
                        .width = width,
                        .height = height,
                        .maximized = false,
                        .has_bounds = true,
                    };
                }
            }
        }
    }

    void MainWindow::PersistUserSettingsCandidate(
        ::FileScoutNative::Domain::UserSettings const& candidate)
    {
        if (::FileScoutNative::Domain::UserSettingsService::Save(candidate))
        {
            m_userSettings = candidate;
            return;
        }

        TraceLog(L"UserSettingsService::Save failed for " +
            ::FileScoutNative::Domain::UserSettingsService::GetStoragePath().wstring());
        if (!m_windowClosing)
        {
            try
            {
                StatusTextBlock().Text(L"Couldn't save settings.");
            }
            catch (...)
            {
            }
        }
    }

    void MainWindow::SaveWindowBoundsSettings()
    {
        if (!m_windowConfigured)
        {
            return;
        }

        if (!m_windowClosing)
        {
            CaptureLiveWindowBounds();
        }

        auto candidate = m_userSettings;
        candidate.window = m_lastNormalWindowBounds;
        candidate.window.maximized = m_windowMaximized;
        PersistUserSettingsCandidate(candidate);
    }

    void MainWindow::SaveDualPaneSetting()
    {
        if (!m_windowConfigured)
        {
            return;
        }

        auto candidate = m_userSettings;
        candidate.dual_pane_visible = m_dualPaneVisible;
        candidate.has_dual_pane_setting = true;
        PersistUserSettingsCandidate(candidate);
    }

    void MainWindow::SavePaneColumnWidths(int32_t const paneIndex)
    {
        if (!m_windowConfigured ||
            paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }

        auto candidate = m_userSettings;
        candidate.list_column_widths = m_panes[static_cast<std::size_t>(paneIndex)].list_column_widths;
        PersistUserSettingsCandidate(candidate);
    }

    void MainWindow::SavePaneSortPreference(int32_t const paneIndex)
    {
        if (!m_windowConfigured ||
            paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }

        auto const& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        auto candidate = m_userSettings;
        candidate.sort_column = [&]()
        {
            switch (pane.sort_column)
            {
            case SortColumn::Type: return ::FileScoutNative::Domain::SavedSortColumn::Type;
            case SortColumn::Size: return ::FileScoutNative::Domain::SavedSortColumn::Size;
            case SortColumn::DateModified: return ::FileScoutNative::Domain::SavedSortColumn::DateModified;
            case SortColumn::Name:
            default:
                return ::FileScoutNative::Domain::SavedSortColumn::Name;
            }
        }();
        candidate.sort_direction = pane.sort_direction == SortDirection::Descending
            ? ::FileScoutNative::Domain::SavedSortDirection::Descending
            : ::FileScoutNative::Domain::SavedSortDirection::Ascending;
        PersistUserSettingsCandidate(candidate);
    }

    void MainWindow::SavePaneGroupingPreference(int32_t const paneIndex)
    {
        if (!m_windowConfigured ||
            paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }

        auto candidate = m_userSettings;
        candidate.group_by_sort_column = m_panes[static_cast<std::size_t>(paneIndex)].group_by_sort_column;
        PersistUserSettingsCandidate(candidate);
    }

    void MainWindow::SavePaneHiddenFilesPreference(int32_t const paneIndex)
    {
        if (!m_windowConfigured ||
            paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }

        auto candidate = m_userSettings;
        candidate.show_hidden_files = m_panes[static_cast<std::size_t>(paneIndex)].show_hidden_files;
        PersistUserSettingsCandidate(candidate);
    }

    void MainWindow::SavePaneExtensionsPreference(int32_t const paneIndex)
    {
        if (!m_windowConfigured ||
            paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }

        auto candidate = m_userSettings;
        candidate.show_file_extensions = m_panes[static_cast<std::size_t>(paneIndex)].show_file_extensions;
        PersistUserSettingsCandidate(candidate);
    }

    void MainWindow::SavePaneViewModePreference(int32_t const paneIndex)
    {
        if (!m_windowConfigured ||
            paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }

        auto const& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        auto candidate = m_userSettings;
        candidate.view_mode = pane.view_mode == ViewMode::Cards
            ? ::FileScoutNative::Domain::SavedViewMode::Cards
            : ::FileScoutNative::Domain::SavedViewMode::List;
        PersistUserSettingsCandidate(candidate);
    }

    void MainWindow::CancelWatchSuppressionTimer(PaneContext& pane)
    {
        if (pane.watch_suppress_timer)
        {
            try
            {
                pane.watch_suppress_timer.Stop();
                if (pane.watch_suppress_timer_token.value != 0)
                {
                    pane.watch_suppress_timer.Tick(pane.watch_suppress_timer_token);
                }
            }
            catch (...)
            {
            }
            pane.watch_suppress_timer = nullptr;
            pane.watch_suppress_timer_token = {};
        }
        pane.watch_suppress_timer_scheduled = false;
    }

    void MainWindow::StopPaneDirectoryWatcher(int32_t const paneIndex)
    {
        if (paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }
        auto& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        ++pane.watch_token;
        pane.watched_path_key.clear();
        pane.watch_armed = false;
        pane.watch_reconcile_on_arm = false;
        pane.watch_refresh_pending = false;
        CancelWatchSuppressionTimer(pane);
        if (pane.directory_watcher)
        {
            pane.directory_watcher->Stop();
        }
    }

    void MainWindow::UpdatePaneDirectoryWatcher(int32_t const paneIndex)
    {
        if (m_windowClosing || paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }

        if (paneIndex == 1 && !m_dualPaneVisible)
        {
            StopPaneDirectoryWatcher(paneIndex);
            return;
        }

        auto& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        if (!::FileScoutNative::Domain::ShouldWatchLocation(
                pane.current_path,
                pane.is_drives_view,
                pane.is_archive_view,
                pane.is_recycle_bin_view))
        {
            StopPaneDirectoryWatcher(paneIndex);
            return;
        }

        auto const targetKey = ::FileScoutNative::Domain::SidebarPathKey(pane.current_path);
        if (pane.directory_watcher &&
            pane.directory_watcher->IsWatching() &&
            pane.watched_path_key == targetKey)
        {
            return;
        }

        if (!pane.directory_watcher)
        {
            pane.directory_watcher = std::make_unique<::FileScoutNative::Shell::DirectoryWatcher>();
        }

        uint64_t const token = ++pane.watch_token;
        pane.watched_path_key = targetKey;
        pane.watch_armed = false;
        pane.watch_refresh_pending = false;
        CancelWatchSuppressionTimer(pane);
        std::filesystem::path const watchedPath = pane.current_path;
        auto weakThis = get_weak();
        auto queue = m_dispatcherQueue;

        bool const started = pane.directory_watcher->Start(
            watchedPath,
            [weakThis, queue, paneIndex, token, watchedPath](uint64_t const eventTickMs)
            {
                if (!queue)
                {
                    return;
                }
                queue.TryEnqueue([weakThis, paneIndex, token, watchedPath, eventTickMs]()
                {
                    if (auto strong = weakThis.get())
                    {
                        strong->HandleDirectoryWatchChanged(paneIndex, token, watchedPath, eventTickMs);
                    }
                });
            },
            static_cast<uint32_t>(::FileScoutNative::Domain::kDefaultWatchDebounceMs),
            static_cast<uint32_t>(::FileScoutNative::Domain::kDefaultWatchMaxWaitMs),
            [weakThis, queue, paneIndex, token, watchedPath](bool const armed)
            {
                if (!queue)
                {
                    return;
                }
                queue.TryEnqueue([weakThis, paneIndex, token, watchedPath, armed]()
                {
                    if (auto strong = weakThis.get())
                    {
                        strong->HandleDirectoryWatchReady(paneIndex, token, watchedPath, armed);
                    }
                });
            });

        if (!started)
        {
            pane.watched_path_key.clear();
            pane.watch_armed = false;
        }
    }

    void MainWindow::HandleDirectoryWatchReady(
        int32_t const paneIndex,
        uint64_t const watchToken,
        std::filesystem::path const& watchedPath,
        bool const armed)
    {
        if (m_windowClosing || paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }

        auto& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        if (pane.watch_token != watchToken)
        {
            return;
        }

        if (!::FileScoutNative::Domain::ShouldWatchLocation(
                pane.current_path,
                pane.is_drives_view,
                pane.is_archive_view,
                pane.is_recycle_bin_view) ||
            ::FileScoutNative::Domain::SidebarPathKey(pane.current_path) !=
                ::FileScoutNative::Domain::SidebarPathKey(watchedPath))
        {
            return;
        }

        pane.watch_armed = armed;
        if (!armed)
        {
            pane.watched_path_key.clear();
        }

        // Changes made before ReadDirectoryChangesW began buffering are not reported: reconcile once.
        if (pane.watch_reconcile_on_arm)
        {
            pane.watch_reconcile_on_arm = false;
            pane.watch_refresh_pending = true;
            FlushPendingWatchRefresh(paneIndex);
        }
    }

    bool MainWindow::CanApplyWatchRefreshNow(int32_t const paneIndex)
    {
        if (m_windowClosing || m_modalDialogOpen ||
            paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return false;
        }

        auto const& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        if (pane.awaiting_final_batch || pane.search_mode != SearchMode::None)
        {
            return false;
        }

        if (paneIndex == m_activePaneIndex &&
            AddressTextBox().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            return false;
        }

        if (::GetTickCount64() < pane.watch_suppress_until_tick)
        {
            return false;
        }

        return true;
    }

    void MainWindow::ScheduleWatchSuppressionRetry(int32_t const paneIndex, uint64_t const watchToken)
    {
        if (m_windowClosing || !m_dispatcherQueue ||
            paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }

        auto& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        if (pane.watch_suppress_timer_scheduled)
        {
            return;
        }

        uint64_t const now = ::GetTickCount64();
        uint64_t const delayMs = (pane.watch_suppress_until_tick > now)
            ? (pane.watch_suppress_until_tick - now) + 50u
            : 50u;

        CancelWatchSuppressionTimer(pane);
        pane.watch_suppress_timer_scheduled = true;
        auto timer = m_dispatcherQueue.CreateTimer();
        timer.IsRepeating(false);
        timer.Interval(std::chrono::milliseconds{ (std::min<uint64_t>)(delayMs, 5000u) });
        auto weakThis = get_weak();
        auto weakTimer = winrt::make_weak(timer);
        pane.watch_suppress_timer = timer;
        pane.watch_suppress_timer_token = timer.Tick(
            [weakThis, weakTimer, paneIndex, watchToken](auto const&, auto const&)
            {
                if (auto t = weakTimer.get())
                {
                    t.Stop();
                }
                if (auto strong = weakThis.get())
                {
                    if (paneIndex >= 0 && paneIndex < static_cast<int32_t>(strong->m_panes.size()))
                    {
                        auto& p = strong->m_panes[static_cast<std::size_t>(paneIndex)];
                        if (p.watch_token == watchToken)
                        {
                            strong->CancelWatchSuppressionTimer(p);
                            strong->FlushPendingWatchRefresh(paneIndex);
                        }
                    }
                }
            });
        timer.Start();
    }

    void MainWindow::RefreshPaneInPlacePreservingSelection(int32_t const paneIndex)
    {
        if (m_windowClosing || paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }

        auto& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        if (!::FileScoutNative::Domain::ShouldWatchLocation(
                pane.current_path,
                pane.is_drives_view,
                pane.is_archive_view,
                pane.is_recycle_bin_view))
        {
            return;
        }

        std::optional<std::filesystem::path> selectAfter;
        if (auto const focusedId = pane.selection.FocusedId(); focusedId.has_value())
        {
            for (auto const& item : pane.items)
            {
                if (item.parsing_name == *focusedId)
                {
                    selectAfter = item.path;
                    break;
                }
            }
        }

        LoadDirectoryAsync(pane.current_path, paneIndex, std::move(selectAfter), true);
    }

    void MainWindow::FlushPendingWatchRefresh(int32_t const paneIndex)
    {
        if (m_windowClosing || paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }

        auto& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        if (!pane.watch_refresh_pending)
        {
            return;
        }

        if (!::FileScoutNative::Domain::ShouldWatchLocation(
                pane.current_path,
                pane.is_drives_view,
                pane.is_archive_view,
                pane.is_recycle_bin_view))
        {
            pane.watch_refresh_pending = false;
            return;
        }

        if (::GetTickCount64() < pane.watch_suppress_until_tick)
        {
            ScheduleWatchSuppressionRetry(paneIndex, pane.watch_token);
            return;
        }

        if (!CanApplyWatchRefreshNow(paneIndex))
        {
            return;
        }

        pane.watch_refresh_pending = false;
        RefreshPaneInPlacePreservingSelection(paneIndex);
    }

    void MainWindow::FlushPendingWatchRefreshAllPanes()
    {
        FlushPendingWatchRefresh(0);
        if (m_dualPaneVisible)
        {
            FlushPendingWatchRefresh(1);
        }
    }

    void MainWindow::HandleDirectoryWatchChanged(
        int32_t const paneIndex,
        uint64_t const watchToken,
        std::filesystem::path const& watchedPath,
        uint64_t const eventTickMs)
    {
        if (m_windowClosing || paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }

        auto& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        if (pane.watch_token != watchToken)
        {
            return;
        }

        if (!::FileScoutNative::Domain::ShouldWatchLocation(
                pane.current_path,
                pane.is_drives_view,
                pane.is_archive_view,
                pane.is_recycle_bin_view))
        {
            return;
        }

        if (::FileScoutNative::Domain::SidebarPathKey(pane.current_path) !=
            ::FileScoutNative::Domain::SidebarPathKey(watchedPath))
        {
            return;
        }

        if (::FileScoutNative::Domain::IsWatchEventCoveredBySnapshot(
                eventTickMs, pane.last_enumeration_start_tick))
        {
            return;
        }

        pane.watch_refresh_pending = true;
        FlushPendingWatchRefresh(paneIndex);
    }

    void MainWindow::SelectItemByPathInPane(int32_t const paneIndex, std::filesystem::path const& path)
    {
        if (paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }
        if (paneIndex == m_activePaneIndex)
        {
            SelectItemByPath(path);
            return;
        }

        auto& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        if (!pane.active_list_view)
        {
            return;
        }

        auto const& displayedItems = DisplayedItems(pane);
        for (std::size_t index = 0; index < displayedItems.size(); ++index)
        {
            if (displayedItems[index].path == path)
            {
                auto const i32 = static_cast<int32_t>(index);
                pane.selection.ReplaceWith(displayedItems[index].parsing_name);
                if (pane.active_tab_index >= 0 &&
                    pane.active_tab_index < static_cast<int32_t>(pane.tabs.size()))
                {
                    pane.tabs[pane.active_tab_index].selection = pane.selection;
                    pane.tabs[pane.active_tab_index].selected_item_index = i32;
                }
                pane.tab_switching = true;
                pane.active_list_view.SelectedIndex(i32);
                pane.tab_switching = false;
                UpdateRowSelectionVisuals(pane);
                return;
            }
        }
    }

    LRESULT CALLBACK MainWindow::NavigationWindowSubclassProc(
        HWND const hwnd,
        UINT const message,
        WPARAM const wParam,
        LPARAM const lParam,
        UINT_PTR const subclassId,
        DWORD_PTR const refData)
    {
        if (message == WM_NCDESTROY)
        {
            ::RemoveWindowSubclass(hwnd, &MainWindow::NavigationWindowSubclassProc, subclassId);
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

        if (message == WM_SIZE || message == WM_MOVE)
        {
            if (auto* const self = reinterpret_cast<MainWindow*>(refData))
            {
                self->CaptureLiveWindowBounds();
            }
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

        if (message == WM_EXITSIZEMOVE)
        {
            if (auto* const self = reinterpret_cast<MainWindow*>(refData))
            {
                self->CaptureLiveWindowBounds();
                self->SaveWindowBoundsSettings();
            }
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }
        if (message == WM_THEMECHANGED ||
            (message == WM_SETTINGCHANGE && (wParam == SPI_SETHIGHCONTRAST || wParam == 0)))
        {
            if (auto* const self = reinterpret_cast<MainWindow*>(refData))
            {
                try { self->ApplyTitleBarCaptionColors(); }
                catch (...) {}
            }
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

        if (::FileScoutNative::Domain::IsDriveListChange(message, static_cast<std::uintptr_t>(wParam)))
        {
            if (auto* const self = reinterpret_cast<MainWindow*>(refData))
            {
                self->m_sidebarDrives.Invalidate();
                self->UpdateThisPCSection();
            }
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

        if (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE)
        {
            if (auto* const self = reinterpret_cast<MainWindow*>(refData))
            {
                self->m_mouseNavDeduplicator.ResetPressedState();
            }
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

        if (message == WM_NCXBUTTONDOWN)
        {
            if (auto* const self = reinterpret_cast<MainWindow*>(refData))
            {
                self->m_mouseNavDeduplicator.OnNonClientButtonDown(
                    ::FileScoutNative::Domain::ResolveNonClientXButtonCommand(wParam));
            }
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

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

        if (self->m_mouseNavDeduplicator.ShouldSuppressWin32Command(
                message, lParam, command, ::GetTickCount64()))
        {
            return TRUE;
        }

        if (self->m_modalDialogOpen ||
            self->CommandPaletteOverlay().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            return TRUE;
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

    void MainWindow::BenchOnFullList(int32_t const paneIndex, std::size_t const itemCount)
    {
        auto& recorder = ::FileScoutNative::Bench::Recorder::Instance();
        recorder.SetItemCount(itemCount);
        recorder.Mark(L"full_list_published");
        auto const list = m_panes[paneIndex].active_list_view;
        BenchMarkAfterFrames(L"full_list", 2, [list]()
            {
                auto timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
                timer.Interval(std::chrono::milliseconds(1500));
                timer.IsRepeating(false);
                timer.Tick([list, timer](auto&&, auto&&)
                    {
                        BenchRunScroll(list ? FindDescendantByType<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>(list) : nullptr);
                    });
                timer.Start();
            });
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
        LoadDirectoryInPane(m_activePaneIndex, path, std::move(selectAfter), skipHistoryRecord);
    }

    void MainWindow::LoadDirectoryInPane(
        int32_t const paneIndex,
        std::filesystem::path const& path,
        std::optional<std::filesystem::path> selectAfter,
        bool skipHistoryRecord)
    {
        auto const chrome = ::FileScoutNative::Domain::ChromeUpdatesForLoad(paneIndex, m_activePaneIndex);
        ScopedTimer loadTimer{ L"LoadDirectory: " + path.wstring() };
        m_cutItemPaths.clear();
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

        auto& pane = m_panes[static_cast<std::size_t>(paneIndex)];

        {
            if (pane.is_drives_view && pane.current_path.empty())
            {
                pane.current_path = std::filesystem::path{ ::FileScoutNative::Domain::kDrivesViewLocation };
            }
            auto& navDiagRec = ::FileScoutNative::NavDiag::Recorder::Instance();
            auto const recentsBegin = navDiagRec.Enabled() ? ::FileScoutNative::NavDiag::Now() : ::FileScoutNative::NavDiag::Tick{};
            bool const recentsChanged = ::FileScoutNative::Domain::NavigateAndRecordRecent(
                pane.back_stack, pane.forward_stack, pane.current_path, m_recentFolders,
                absolutePath, skipHistoryRecord, kRecentFolderCapacity);
            if (navDiagRec.Enabled())
            {
                navDiagRec.NoteRecentsUpdate(::FileScoutNative::NavDiag::SpanUs(recentsBegin, ::FileScoutNative::NavDiag::Now()));
            }
            if (chrome.toolbar)
            {
                UpdateSidebarSelectionHighlights();
            }
            if (recentsChanged)
            {
                m_sidebarRecentsDirty = true;
            }
        }
        pane.is_drives_view = false;
        pane.is_archive_view = m_shellInterop.IsArchivePath(absolutePath);
        pane.is_recycle_bin_view = ::FileScoutNative::Domain::IsRecycleBinLocation(absolutePath);
        UpdatePaneDirectoryWatcher(paneIndex);
        if (chrome.toolbar)
        {
            UpdateNavButtons();
        }
        EndSearchOnNavigation(paneIndex);

        LoadDirectoryAsync(absolutePath, paneIndex, std::move(selectAfter));
    }

    void MainWindow::RefreshPanesShowing(
        std::filesystem::path const& folder,
        std::optional<std::filesystem::path> selectAfter)
    {
        auto const folderKey = ::FileScoutNative::Domain::SidebarPathKey(folder);
        for (int32_t index = 0; index < static_cast<int32_t>(m_panes.size()); ++index)
        {
            auto& pane = m_panes[index];
            if (pane.is_drives_view || pane.is_recycle_bin_view || pane.current_path.empty())
            {
                continue;
            }

            auto const paneKey = ::FileScoutNative::Domain::SidebarPathKey(pane.current_path);
            auto const isShown = paneKey == folderKey ||
                (pane.search_mode != SearchMode::None &&
                 ::FileScoutNative::Domain::IsSameOrDescendantPathKey(folderKey, paneKey));
            if (!isShown)
            {
                continue;
            }

            pane.watch_suppress_until_tick =
                ::GetTickCount64() + ::FileScoutNative::Domain::kDefaultWatchSelfSuppressMs;

            if (pane.search_mode != SearchMode::None)
            {
                ResetSearchState(index, false);
            }
            LoadDirectoryAsync(pane.current_path, index, selectAfter);
        }
    }

    winrt::fire_and_forget MainWindow::LoadDirectoryAsync(
        std::filesystem::path path,
        int32_t const paneIndex,
        std::optional<std::filesystem::path> selectAfter,
        bool const preserveAddressEntry)
    {
        auto lifetime = get_strong();
        auto& pane = m_panes[paneIndex];
        uint32_t const generation = ++pane.load_generation;
        int32_t const tabIndex = pane.active_tab_index;
        PaneLoadTiming timing{};
        timing.load_start_tick = ::GetTickCount64();
        pane.last_load_timing = timing;
        auto navDiag = ::FileScoutNative::NavDiag::Recorder::Instance().StartLoad(paneIndex,
            pane.is_recycle_bin_view ? ::FileScoutNative::NavDiag::Kind::RecycleBin
                                     : ::FileScoutNative::NavDiag::Kind::Folder);

        pane.items.clear();
        pane.selection.Clear();
        if (navDiag.active) { navDiag.pre_reset_begin = ::FileScoutNative::NavDiag::Now(); }
        pane.tab_rows[tabIndex].Clear();
        if (navDiag.active) { navDiag.pre_reset_end = ::FileScoutNative::NavDiag::Now(); }
        ::FileScoutNative::Domain::ResetTypeAhead(pane.type_ahead);
        if (tabIndex >= 0 && tabIndex < static_cast<int32_t>(pane.tabs.size()))
        {
            pane.tabs[tabIndex].items.clear();
            pane.tabs[tabIndex].path = path;
            pane.tabs[tabIndex].is_drives_view = false;
            auto tabView = TabViewForPane(paneIndex);
            if (tabIndex < static_cast<int32_t>(tabView.TabItems().Size()))
            {
                auto tabItem = tabView.TabItems()
                    .GetAt(static_cast<uint32_t>(tabIndex))
                    .as<winrt::Microsoft::UI::Xaml::Controls::TabViewItem>();
                auto const initialLabel = pane.is_recycle_bin_view
                    ? std::wstring{ L"Recycle Bin" }
                    : pane.tabs[tabIndex].display_name();
                tabItem.Header(winrt::box_value(ToHString(initialLabel)));
            }
        }
        pane.is_drives_view = false;
        if (pane.active_list_view)
        {
            pane.tab_switching = true;
            pane.active_list_view.SelectedIndex(-1);
            pane.tab_switching = false;
        }
        std::filesystem::path absolute;
        try { absolute = std::filesystem::absolute(path).lexically_normal(); }
        catch (...) { absolute = path; }

        if (m_activePaneIndex == paneIndex)
        {
            UpdateBreadcrumbs(absolute);
            UpdateSearchPlaceholder(absolute);
            if (!preserveAddressEntry)
            {
                HideAddressTextBox();
            }
            ResetSelectionDetails();
        }

        pane.awaiting_final_batch = true;

        SortColumn const sort_column = pane.sort_column;
        SortDirection const sort_direction = pane.sort_direction;
        bool const show_hidden_files = pane.show_hidden_files;
        bool const show_file_extensions = pane.show_file_extensions;
        bool const is_recycle_bin_view = pane.is_recycle_bin_view;
        uint64_t const startWatchToken = pane.watch_token;
        bool const startedWhileWatchArmed = pane.watch_armed;

        if (navDiag.active) { navDiag.ui_pre_done = ::FileScoutNative::NavDiag::Now(); }
        co_await winrt::resume_background();

        try
        {
            if (is_recycle_bin_view)
            {
                if (navDiag.active) { navDiag.enumerate_begin = ::FileScoutNative::NavDiag::Now(); }
                auto recycleItems = m_shellInterop.EnumerateRecycleBin();
                timing.enumeration_end_tick = ::GetTickCount64();
                if (navDiag.active) { navDiag.enumerate_end = navDiag.enqueue = ::FileScoutNative::NavDiag::Now(); }
                m_dispatcherQueue.TryEnqueue(
                    [this, lifetime, paneIndex, tabIndex, generation,
                     absolute, selectAfter, timing, navDiag, items = std::move(recycleItems)]() mutable
                    {
                        if (navDiag.active) { navDiag.callback = ::FileScoutNative::NavDiag::Now(); }
                        auto& pane = m_panes[paneIndex];
                        if (pane.load_generation != generation) { NavDiagSuperseded(navDiag); return; }
                        if (pane.active_tab_index != tabIndex) { NavDiagSuperseded(navDiag); return; }
                        pane.awaiting_final_batch = false;

                        if (navDiag.active) { navDiag.sort_begin = ::FileScoutNative::NavDiag::Now(); }
                        SortItems(items, pane.sort_column, pane.sort_direction);
                        if (navDiag.active) { navDiag.sort_end = ::FileScoutNative::NavDiag::Now(); }
                        std::size_t const totalItems = items.size();
                        pane.current_path = absolute;
                        pane.is_drives_view = false;
                        pane.is_archive_view = false;
                        pane.is_recycle_bin_view = true;
                        pane.items = items;
                        pane.selection.Clear();

                        pane.tabs[tabIndex].path = absolute;
                        pane.tabs[tabIndex].items = items;
                        pane.tabs[tabIndex].selection = {};
                        pane.tabs[tabIndex].selected_item_index = -1;
                        pane.tabs[tabIndex].is_drives_view = false;
                        pane.tabs[tabIndex].is_archive_view = false;
                        pane.tabs[tabIndex].is_recycle_bin_view = true;

                        auto tabView = TabViewForPane(paneIndex);
                        if (tabIndex >= 0 && tabIndex < static_cast<int32_t>(pane.tabs.size()))
                        {
                            auto tabItem = tabView.TabItems()
                                .GetAt(static_cast<uint32_t>(tabIndex))
                                .as<winrt::Microsoft::UI::Xaml::Controls::TabViewItem>();
                            tabItem.Header(winrt::box_value(ToHString(L"Recycle Bin")));
                        }

                        auto& rows = pane.tab_rows[tabIndex];
                        if (navDiag.active) { navDiag.reset_begin = ::FileScoutNative::NavDiag::Now(); }
                        pane.tab_switching = true;
                        rows.Clear();
                        pane.tab_switching = false;
                        if (navDiag.active) { navDiag.reset_end = ::FileScoutNative::NavDiag::Now(); }
                        for (auto const& item : pane.items)
                            rows.Append(MakeFileRowViewModel(item));
                        if (navDiag.active) { navDiag.first_append_end = ::FileScoutNative::NavDiag::Now(); }

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
                                (totalItems == 1 ? L" item in " : L" items in ") +
                                L"Recycle Bin"));
                        }

                        timing.first_batch_publish_tick = ::GetTickCount64();
                        timing.final_publish_tick = timing.first_batch_publish_tick;
                        pane.last_load_timing = timing;
                        TraceLog(FormatLoadTimingTrace(absolute, totalItems, timing));
                        if (navDiag.active)
                        {
                            navDiag.items = navDiag.first_batch_rows = totalItems;
                            navDiag.first_publish = navDiag.final_publish = ::FileScoutNative::NavDiag::Now();
                            auto const weakThis = get_weak();
                            NavDiagEmitAfterFrames(navDiag, [weakThis, paneIndex, tabIndex, generation]()
                            {
                                auto self = weakThis.get();
                                return self &&
                                    self->m_panes[paneIndex].load_generation == generation &&
                                    self->m_panes[paneIndex].active_tab_index == tabIndex;
                            });
                        }
                        FlushDeferredSidebarRecents();
                        ApplyPendingFilter(paneIndex);
                        if (selectAfter.has_value() &&
                            ::FileScoutNative::Domain::ChromeUpdatesForLoad(paneIndex, m_activePaneIndex).select_after)
                            SelectItemByPath(*selectAfter);
                    });
            }
            else if (m_shellInterop.IsArchivePath(absolute))
            {
                navDiag.kind = ::FileScoutNative::NavDiag::Kind::Archive;
                if (navDiag.active) { navDiag.enumerate_begin = ::FileScoutNative::NavDiag::Now(); }
                auto archiveItems = m_shellInterop.EnumerateArchiveDirectory(absolute);
                timing.enumeration_end_tick = ::GetTickCount64();
                if (navDiag.active) { navDiag.enumerate_end = navDiag.enqueue = ::FileScoutNative::NavDiag::Now(); }
                m_dispatcherQueue.TryEnqueue(
                    [this, lifetime, paneIndex, tabIndex, generation,
                     absolute, selectAfter, timing, navDiag, items = std::move(archiveItems)]() mutable
                    {
                        if (navDiag.active) { navDiag.callback = ::FileScoutNative::NavDiag::Now(); }
                        auto& pane = m_panes[paneIndex];
                        if (pane.load_generation != generation) { NavDiagSuperseded(navDiag); return; }
                        if (pane.active_tab_index != tabIndex) { NavDiagSuperseded(navDiag); return; }
                        pane.awaiting_final_batch = false;

                        if (navDiag.active) { navDiag.sort_begin = ::FileScoutNative::NavDiag::Now(); }
                        SortItems(items, pane.sort_column, pane.sort_direction);
                        if (navDiag.active) { navDiag.sort_end = ::FileScoutNative::NavDiag::Now(); }
                        std::size_t const totalItems = items.size();
                        pane.current_path = absolute;
                        pane.is_drives_view = false;
                        pane.is_archive_view = true;
                        pane.is_recycle_bin_view = false;
                        StopPaneDirectoryWatcher(paneIndex);
                        pane.items = items;
                        pane.selection.Clear();

                        pane.tabs[tabIndex].path = absolute;
                        pane.tabs[tabIndex].items = items;
                        pane.tabs[tabIndex].selection = {};
                        pane.tabs[tabIndex].selected_item_index = -1;
                        pane.tabs[tabIndex].is_drives_view = false;
                        pane.tabs[tabIndex].is_archive_view = true;
                        pane.tabs[tabIndex].is_recycle_bin_view = false;

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
                        if (navDiag.active) { navDiag.reset_begin = ::FileScoutNative::NavDiag::Now(); }
                        pane.tab_switching = true;
                        rows.Clear();
                        pane.tab_switching = false;
                        if (navDiag.active) { navDiag.reset_end = ::FileScoutNative::NavDiag::Now(); }
                        for (auto const& item : pane.items)
                            rows.Append(MakeFileRowViewModel(item));
                        if (navDiag.active) { navDiag.first_append_end = ::FileScoutNative::NavDiag::Now(); }
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
                        if (navDiag.active)
                        {
                            navDiag.items = navDiag.first_batch_rows = totalItems;
                            navDiag.first_publish = navDiag.final_publish = ::FileScoutNative::NavDiag::Now();
                            auto const weakThis = get_weak();
                            NavDiagEmitAfterFrames(navDiag, [weakThis, paneIndex, tabIndex, generation]()
                            {
                                auto self = weakThis.get();
                                return self &&
                                    self->m_panes[paneIndex].load_generation == generation &&
                                    self->m_panes[paneIndex].active_tab_index == tabIndex;
                            });
                        }
                        FlushDeferredSidebarRecents();
                        ApplyPendingFilter(paneIndex);
                        if (selectAfter.has_value() &&
                            ::FileScoutNative::Domain::ChromeUpdatesForLoad(paneIndex, m_activePaneIndex).select_after)
                            SelectItemByPath(*selectAfter);
                    });
            }
            else
            {
                bool const benchLoad = paneIndex == 0 && BenchTargets(absolute);
                if (benchLoad) { ::FileScoutNative::Bench::Recorder::Instance().Mark(L"load_enumerate_start"); }
                if (navDiag.active) { navDiag.enumerate_begin = ::FileScoutNative::NavDiag::Now(); }
                uint64_t const enumStartTick = ::GetTickCount64();
                auto items = m_shellInterop.EnumerateDirectory(absolute, show_hidden_files);
                timing.enumeration_end_tick = ::GetTickCount64();
                if (navDiag.active) { navDiag.enumerate_end = ::FileScoutNative::NavDiag::Now(); }
                if (benchLoad) { ::FileScoutNative::Bench::Recorder::Instance().Mark(L"load_enumerated"); }

                if (navDiag.active) { navDiag.sort_begin = ::FileScoutNative::NavDiag::Now(); }
                SortItems(items, sort_column, sort_direction);
                if (navDiag.active) { navDiag.sort_end = navDiag.rows_begin = ::FileScoutNative::NavDiag::Now(); }
                if (benchLoad) { ::FileScoutNative::Bench::Recorder::Instance().Mark(L"load_sorted"); }
                // C++/WinRT view models are agile, so they can be built on several threads.
                std::vector<winrt::FileScoutNative::FileRowViewModel> prebuilt(items.size(), nullptr);
                ::FileScoutNative::Domain::ParallelForRanges(items.size(), [&](std::size_t begin, std::size_t end)
                    {
                        for (auto i = begin; i < end; ++i)
                        {
                            auto const& item = items[i];
                            auto vm = MakeFileRowViewModel(item);
                            if (!show_file_extensions && !item.is_folder)
                            {
                                auto impl = vm.as<winrt::FileScoutNative::implementation::FileRowViewModel>();
                                auto const dotPos = item.display_name.rfind(L'.');
                                if (dotPos != std::wstring_view::npos && dotPos > 0)
                                {
                                    impl->SetName(item.display_name.substr(0, dotPos));
                                }
                            }
                            prebuilt[i] = std::move(vm);
                        }
                    });
                if (navDiag.active) { navDiag.rows_end = ::FileScoutNative::NavDiag::Now(); }
                if (benchLoad) { ::FileScoutNative::Bench::Recorder::Instance().Mark(L"load_rows_built"); }

                if (navDiag.active) { navDiag.enqueue = ::FileScoutNative::NavDiag::Now(); }
                auto navDiagOnFailure = navDiag;
                bool const enqueued = m_dispatcherQueue.TryEnqueue(
                    [this, lifetime, paneIndex, tabIndex, generation,
                     absolute, selectAfter, preserveAddressEntry,
                     startWatchToken, startedWhileWatchArmed, enumStartTick,
                     timing, navDiag,
                     items = std::move(items),
                     prebuilt = std::move(prebuilt)]() mutable
                    {
                        if (navDiag.active) { navDiag.callback = ::FileScoutNative::NavDiag::Now(); }
                        auto& pane = m_panes[paneIndex];
                        if (pane.load_generation != generation) { NavDiagSuperseded(navDiag); return; }
                        if (pane.active_tab_index != tabIndex) { NavDiagSuperseded(navDiag); return; }

                        pane.last_enumeration_start_tick =
                            (std::max)(pane.last_enumeration_start_tick, enumStartTick);

                        std::size_t const totalItems = prebuilt.size();
                        pane.current_path = absolute;
                        pane.is_drives_view = false;
                        pane.is_archive_view = false;
                        pane.is_recycle_bin_view = false;
                        pane.items = items;
                        pane.selection.Clear();

                        pane.tabs[tabIndex].path = absolute;
                        pane.tabs[tabIndex].items = items;
                        pane.tabs[tabIndex].selection = {};
                        pane.tabs[tabIndex].selected_item_index = -1;
                        pane.tabs[tabIndex].is_drives_view = false;
                        pane.tabs[tabIndex].is_archive_view = false;
                        pane.tabs[tabIndex].is_recycle_bin_view = false;

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
                        bool const stageLoad = ::FileScoutNative::Domain::ShouldStageDirectoryLoad(totalItems);
                        std::size_t const firstBatchEnd = stageLoad
                            ? (std::min)(::FileScoutNative::Domain::kStagedLoadFirstBatch, totalItems)
                            : totalItems;

                        if (navDiag.active) { navDiag.reset_begin = ::FileScoutNative::NavDiag::Now(); }
                        pane.tab_switching = true;
                        if (firstBatchEnd == 0)
                        {
                            rows.Clear();
                            if (navDiag.active) { navDiag.reset_end = ::FileScoutNative::NavDiag::Now(); }
                        }
                        else
                        {
                            if (navDiag.active) { navDiag.reset_end = ::FileScoutNative::NavDiag::Now(); }
                            rows.as<winrt::Windows::Foundation::Collections::IVector<
                                winrt::FileScoutNative::FileRowViewModel>>()
                                .ReplaceAll(winrt::array_view<winrt::FileScoutNative::FileRowViewModel const>{
                                    prebuilt.data(),
                                    prebuilt.data() + firstBatchEnd });
                        }
                        pane.tab_switching = false;
                        if (navDiag.active)
                        {
                            navDiag.first_append_end = ::FileScoutNative::NavDiag::Now();
                            navDiag.items = totalItems;
                            navDiag.first_batch_rows = firstBatchEnd;
                        }
                        ApplyPaneStateToRows(pane, pane.items, rows, m_iconSourceCache);

                        timing.first_batch_publish_tick = ::GetTickCount64();
                        pane.user_interacted_after_first_batch = false;
                        bool const benchTarget = paneIndex == 0 && BenchTargets(absolute);
                        if (benchTarget)
                        {
                            ::FileScoutNative::Bench::Recorder::Instance().Mark(L"enumerated_and_first_publish");
                            BenchMarkAfterFrames(L"first_rows", 2);
                        }

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
                        if (navDiag.active) { navDiag.first_publish = ::FileScoutNative::NavDiag::Now(); }
                        FlushDeferredSidebarRecents();

                        if (!stageLoad)
                        {
                            if (benchTarget)
                            {
                                BenchOnFullList(paneIndex, totalItems);
                            }
                            pane.awaiting_final_batch = false;
                            timing.final_publish_tick = timing.first_batch_publish_tick;
                            pane.last_load_timing = timing;
                            TraceLog(FormatLoadTimingTrace(absolute, totalItems, timing));
                            if (navDiag.active)
                            {
                                navDiag.final_publish = navDiag.first_publish;
                                auto const weakThis = get_weak();
                                NavDiagEmitAfterFrames(navDiag, [weakThis, paneIndex, tabIndex, generation]()
                                {
                                    auto self = weakThis.get();
                                    return self &&
                                        self->m_panes[paneIndex].load_generation == generation &&
                                        self->m_panes[paneIndex].active_tab_index == tabIndex;
                                });
                            }
                            ApplyPendingFilter(paneIndex);
                            if (selectAfter.has_value() &&
                                ::FileScoutNative::Domain::ShouldRestoreSelectionInLoadedPane(
                                    paneIndex, m_activePaneIndex, preserveAddressEntry))
                            {
                                SelectItemByPathInPane(paneIndex, *selectAfter);
                            }
                            {
                                auto backfillItems = pane.items;
                                for (auto& bi : backfillItems) bi.icon = {};
                                BackfillIconsAsync(paneIndex, tabIndex, generation,
                                    std::move(backfillItems));
                            }
                            if (pane.watch_refresh_pending)
                            {
                                FlushPendingWatchRefresh(paneIndex);
                            }
                            else if (!startedWhileWatchArmed &&
                                pane.watch_token == startWatchToken &&
                                ::FileScoutNative::Domain::ShouldWatchLocation(
                                    pane.current_path, pane.is_drives_view,
                                    pane.is_archive_view, pane.is_recycle_bin_view))
                            {
                                if (pane.watch_armed)
                                {
                                    pane.watch_refresh_pending = true;
                                    FlushPendingWatchRefresh(paneIndex);
                                }
                                else if (pane.directory_watcher && pane.directory_watcher->IsWatching())
                                {
                                    pane.watch_reconcile_on_arm = true;
                                }
                            }
                            return;
                        }

                        pane.last_load_timing = timing;
                        navDiag.staged = true;
                        RunAfterFramePresented(
                            [this, lifetime, paneIndex, tabIndex, generation, absolute, selectAfter,
                             preserveAddressEntry, startWatchToken, startedWhileWatchArmed,
                             items = std::move(items),
                             prebuilt = std::move(prebuilt),
                             timing, navDiag](::FileScoutNative::NavDiag::StagedTrigger const trigger) mutable
                            {
                                if (navDiag.active)
                                {
                                    navDiag.final_callback = ::FileScoutNative::NavDiag::Now();
                                    navDiag.staged_trigger = trigger;
                                }
                                auto& pane = m_panes[paneIndex];
                                if (pane.load_generation != generation) { NavDiagSuperseded(navDiag); return; }
                                if (pane.active_tab_index != tabIndex) { NavDiagSuperseded(navDiag); return; }
                                if (!pane.awaiting_final_batch) { NavDiagSuperseded(navDiag); return; }
                                pane.awaiting_final_batch = false;

                                auto& rows = pane.tab_rows[tabIndex];
                                std::size_t const totalItems = prebuilt.size();
                                // ListView's Reset clears the selection through SelectionChanged;
                                // tab_switching keeps that from reaching pane.selection, which is restored below.
                                pane.tab_switching = true;
                                rows.as<winrt::Windows::Foundation::Collections::IVector<
                                    winrt::FileScoutNative::FileRowViewModel>>()
                                    .ReplaceAll(prebuilt);
                                if (pane.user_interacted_after_first_batch && pane.active_list_view)
                                {
                                    auto const focusedId = pane.selection.FocusedId();
                                    std::vector<int32_t> selectedIndices;
                                    std::optional<int32_t> focusedIndex;
                                    std::size_t const scanLimit = (std::min)(items.size(), totalItems);
                                    for (std::size_t idx = 0; idx < scanLimit; ++idx)
                                    {
                                        auto const& parsingName = items[idx].parsing_name;
                                        if (!pane.selection.IsSelected(parsingName))
                                        {
                                            continue;
                                        }
                                        auto const i32 = static_cast<int32_t>(idx);
                                        selectedIndices.push_back(i32);
                                        if (focusedId.has_value() && parsingName == *focusedId)
                                        {
                                            focusedIndex = i32;
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
                                }
                                pane.tab_switching = false;
                                if (navDiag.active) { navDiag.replace_end = ::FileScoutNative::NavDiag::Now(); }
                                ApplyPaneStateToRows(pane, items, rows, m_iconSourceCache);
                                if (pane.user_interacted_after_first_batch)
                                {
                                    UpdateRowSelectionVisuals(pane);
                                }

                                timing.final_publish_tick = ::GetTickCount64();
                                pane.last_load_timing = timing;
                                TraceLog(FormatLoadTimingTrace(absolute, totalItems, timing));
                                if (navDiag.active)
                                {
                                    navDiag.final_publish = ::FileScoutNative::NavDiag::Now();
                                    auto const weakThis = get_weak();
                                    NavDiagEmitAfterFrames(navDiag, [weakThis, paneIndex, tabIndex, generation]()
                                    {
                                        auto self = weakThis.get();
                                        return self &&
                                            self->m_panes[paneIndex].load_generation == generation &&
                                            self->m_panes[paneIndex].active_tab_index == tabIndex;
                                    });
                                }
                                ApplyPendingFilter(paneIndex);
                                if (paneIndex == 0 && BenchTargets(absolute))
                                {
                                    BenchOnFullList(paneIndex, totalItems);
                                }
                                if (!pane.user_interacted_after_first_batch && selectAfter.has_value() &&
                                    ::FileScoutNative::Domain::ShouldRestoreSelectionInLoadedPane(
                                        paneIndex, m_activePaneIndex, preserveAddressEntry))
                                {
                                    SelectItemByPathInPane(paneIndex, *selectAfter);
                                }
                                {
                                    auto backfillItems = items;
                                    for (auto& bi : backfillItems) bi.icon = {};
                                    BackfillIconsAsync(paneIndex, tabIndex, generation,
                                        std::move(backfillItems));
                                }
                                if (pane.watch_refresh_pending)
                                {
                                    FlushPendingWatchRefresh(paneIndex);
                                }
                                else if (!startedWhileWatchArmed &&
                                    pane.watch_token == startWatchToken &&
                                    ::FileScoutNative::Domain::ShouldWatchLocation(
                                        pane.current_path, pane.is_drives_view,
                                        pane.is_archive_view, pane.is_recycle_bin_view))
                                {
                                    if (pane.watch_armed)
                                    {
                                        pane.watch_refresh_pending = true;
                                        FlushPendingWatchRefresh(paneIndex);
                                    }
                                    else if (pane.directory_watcher && pane.directory_watcher->IsWatching())
                                    {
                                        pane.watch_reconcile_on_arm = true;
                                    }
                                }
                            });
                    });
                if (!enqueued && navDiagOnFailure.active)
                {
                    navDiagOnFailure.enqueue_ok = false;
                    navDiagOnFailure.outcome = ::FileScoutNative::NavDiag::Outcome::Failed;
                    NavDiagEmit(navDiagOnFailure);
                }
            }
        }
        catch (...)
        {
            m_dispatcherQueue.TryEnqueue([this, lifetime, paneIndex, generation, navDiag]() mutable
            {
                navDiag.outcome = ::FileScoutNative::NavDiag::Outcome::Failed;
                NavDiagEmit(navDiag);
                FlushDeferredSidebarRecents();
                if (m_panes[paneIndex].load_generation != generation) return;
                m_panes[paneIndex].awaiting_final_batch = false;
                if (m_activePaneIndex == paneIndex)
                    StatusTextBlock().Text(L"Directory load failed.");
            });
            co_return;
        }
    }

    winrt::fire_and_forget MainWindow::BackfillIconsAsync(
        int32_t const paneIndex,
        int32_t const tabIndex,
        uint32_t const generation,
        std::vector<::FileScoutNative::Domain::ShellItemRef> items)
    {
        auto lifetime = get_strong();

        co_await winrt::resume_background();

        auto const itemCount = items.size();
        if (itemCount > 0)
        {
            auto const workerCount = ::FileScoutNative::Domain::ComputeIconWorkerCount(
                std::thread::hardware_concurrency(), itemCount);
            auto const chunkSize = (itemCount + workerCount - 1) / workerCount;

            std::vector<std::future<void>> futures;
            futures.reserve(workerCount);

            for (size_t w = 0; w < workerCount; ++w)
            {
                auto const start = w * chunkSize;
                auto const end = (std::min)(start + chunkSize, itemCount);
                if (start >= itemCount) break;

                futures.push_back(std::async(std::launch::async,
                    [this, &items, start, end]()
                    {
                        ::FileScoutNative::Shell::ComApartment apartment;
                        for (size_t i = start; i < end; ++i)
                            items[i].icon = m_shellInterop.GetItemIcon(
                                items[i].path, items[i].is_folder);
                    }));
            }

            for (auto& f : futures)
            {
                try { f.get(); }
                catch (...) {}
            }
        }

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

        auto const itemCount = items.size();
        std::atomic<bool> anyWorkerCancelled{ false };

        if (itemCount > 0 && !(cancelToken && cancelToken->load(std::memory_order_relaxed)))
        {
            auto const workerCount = ::FileScoutNative::Domain::ComputeIconWorkerCount(
                std::thread::hardware_concurrency(), itemCount);
            auto const chunkSize = (itemCount + workerCount - 1) / workerCount;

            std::vector<std::future<void>> futures;
            futures.reserve(workerCount);

            for (size_t w = 0; w < workerCount; ++w)
            {
                auto const start = w * chunkSize;
                auto const end = (std::min)(start + chunkSize, itemCount);
                if (start >= itemCount) break;

                futures.push_back(std::async(std::launch::async,
                    [this, &items, start, end, cancelToken, &anyWorkerCancelled]()
                    {
                        ::FileScoutNative::Shell::ComApartment apartment;
                        for (size_t i = start; i < end; ++i)
                        {
                            if (cancelToken && cancelToken->load(std::memory_order_relaxed))
                            {
                                anyWorkerCancelled.store(true, std::memory_order_relaxed);
                                return;
                            }
                            items[i].icon = m_shellInterop.GetItemIcon(
                                items[i].path, items[i].is_folder);
                        }
                    }));
            }

            for (auto& f : futures)
            {
                try { f.get(); }
                catch (...) {}
            }
        }

        if (anyWorkerCancelled.load(std::memory_order_relaxed))
        {
            co_return;
        }

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

        for (uint32_t index = 0; index < rowCount; ++index)
        {
            auto row = rows.GetAt(index).as<winrt::FileScoutNative::implementation::FileRowViewModel>();
            auto const isCut = m_cutItemPaths.count(displayedItems[static_cast<std::size_t>(index)].parsing_name) > 0;
            row->IsCut(isCut);
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
        auto const& displayedItems = DisplayedItems();
        std::vector<::FileScoutNative::Domain::ShellItemRef> selected;
        selected.reserve(indices.size());
        for (int32_t const index : indices)
        {
            if (index >= 0 && index < static_cast<int32_t>(displayedItems.size()))
            {
                selected.push_back(displayedItems[static_cast<std::size_t>(index)]);
            }
        }
        auto const summary = ::FileScoutNative::Domain::SummarizeSelection(selected);
        auto const itemCount = summary.total_count;

        ++m_previewGeneration;
        PreviewBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        PreviewImage().Source(nullptr);
        SelectionIconBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        SelectionIconImage().Source(nullptr);
        SelectionTitleTextBlock().Text(ToHString(
            std::to_wstring(itemCount) + (itemCount == 1 ? L" item selected" : L" items selected")));

        SelectionMetaTextBlock().Text(ToHString(::FileScoutNative::Domain::FormatSelectionMetaText(summary)));

        SelectionCreatedTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        SelectionAttributesTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        auto const multiPath = ToHString(ActivePane().current_path.wstring());
        SelectionPathTextBlock().Text(multiPath);
        winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
            SelectionPathTextBlock(), winrt::box_value(multiPath));
        SelectionPathTextBlock().Visibility(
            multiPath.empty()
                ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
                : winrt::Microsoft::UI::Xaml::Visibility::Visible);
        StatusTextBlock().Text(ToHString(::FileScoutNative::Domain::FormatSelectionStatusText(summary)));
    }

    void MainWindow::ResetSelectionDetails()
    {
        TraceLog(L"ResetSelectionDetails: start");
        ++m_previewGeneration;

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
        SelectionMetaTextBlock().Text(L"Pick a file or folder to inspect.");
        TraceLog(L"ResetSelectionDetails: SelectionCreatedTextBlock...");
        SelectionCreatedTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        TraceLog(L"ResetSelectionDetails: SelectionAttributesTextBlock...");
        SelectionAttributesTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        TraceLog(L"ResetSelectionDetails: SelectionPathTextBlock...");
        SelectionPathTextBlock().Text(L"");
        SelectionPathTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        TraceLog(L"ResetSelectionDetails: done.");
    }

    void MainWindow::UpdateSelectionDetails(::FileScoutNative::Domain::ShellItemRef const& item)
    {
        SelectionTitleTextBlock().Text(ToHString(item.display_name));

        if (item.is_image)
        {
            auto const gen = ++m_previewGeneration;
            PreviewBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            SelectionIconBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            SelectionIconImage().Source(BuildIconSource(item.icon));
            LoadImagePreviewAsync(item.path, gen);
        }
        else
        {
            ++m_previewGeneration;
            PreviewBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            SelectionIconBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            SelectionIconImage().Source(BuildIconSource(item.icon));
        }

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

        if (item.date_created_available && !item.date_created_str.empty())
        {
            SelectionCreatedTextBlock().Text(ToHString(L"Created: " + item.date_created_str));
            SelectionCreatedTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }
        else
        {
            SelectionCreatedTextBlock().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }

        if (item.file_attributes_available)
        {
            std::wstring attrStr;
            auto const appendAttr = [&attrStr](std::wstring_view label)
            {
                if (!attrStr.empty()) { attrStr += L"  |  "; }
                attrStr += label;
            };

            // Surface meaningful flags only (skip FILE_ATTRIBUTE_ARCHIVE, which is set on most files).
            if (item.file_attributes & FILE_ATTRIBUTE_READONLY) { appendAttr(L"Read-only"); }
            if (item.file_attributes & FILE_ATTRIBUTE_HIDDEN)   { appendAttr(L"Hidden");    }
            if (item.file_attributes & FILE_ATTRIBUTE_SYSTEM)   { appendAttr(L"System");    }

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

        auto const itemPath = ToHString(item.path.wstring());
        SelectionPathTextBlock().Text(itemPath);
        winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
            SelectionPathTextBlock(), winrt::box_value(itemPath));
        SelectionPathTextBlock().Visibility(
            itemPath.empty()
                ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
                : winrt::Microsoft::UI::Xaml::Visibility::Visible);
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

    std::optional<::FileScoutNative::Domain::ScreenRect> MainWindow::TryGetDisplayedRowScreenRect(
        int32_t const displayedIndex)
    {
        auto const& listView = ActivePane().active_list_view;
        if (!listView || displayedIndex < 0)
        {
            return std::nullopt;
        }

        auto const container =
            listView.ContainerFromIndex(displayedIndex).try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
        if (!container || !Content() || !Content().XamlRoot())
        {
            return std::nullopt;
        }

        auto const rect = TryGetElementRect(container, Content().XamlRoot().RasterizationScale());
        POINT origin{ 0, 0 };
        if (!rect.has_value() || !::ClientToScreen(GetWindowHandle(), &origin))
        {
            return std::nullopt;
        }

        return ::FileScoutNative::Domain::ScreenRect{
            origin.x + rect->X,
            origin.y + rect->Y,
            origin.x + rect->X + rect->Width,
            origin.y + rect->Y + rect->Height,
        };
    }

    ::FileScoutNative::Domain::ScreenRect MainWindow::WindowClientScreenRect()
    {
        auto const hwnd = GetWindowHandle();
        RECT client{};
        POINT origin{ 0, 0 };
        if (!::GetClientRect(hwnd, &client) || !::ClientToScreen(hwnd, &origin))
        {
            return {};
        }
        return { origin.x, origin.y, origin.x + client.right, origin.y + client.bottom };
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

        auto const clickPoint = m_hasLastContextMenuPoint
            ? std::optional<POINT>{ m_lastContextMenuPoint }
            : std::nullopt;
        m_hasLastContextMenuPoint = false;

        auto flyout = winrt::Microsoft::UI::Xaml::Controls::MenuFlyout{};
        bool hasAppendedItems = AppendContextCommands(flyout, model.primary_commands, itemIndex, clickPoint);
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

            auto const appendedGroupItems = AppendContextCommands(flyout, group.commands, itemIndex, clickPoint);
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
        std::optional<int32_t> const itemIndex,
        std::optional<POINT> const clickPoint)
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
            flyoutItem.Click([this, commandId, itemIndex, clickPoint](
                                 winrt::Windows::Foundation::IInspectable const&,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
                {
                    ExecuteContextCommand(commandId, itemIndex, clickPoint);
                });

            flyout.Items().Append(flyoutItem);
            appendedAny = true;
        }

        return appendedAny;
    }

    void MainWindow::ExecuteContextCommand(
        std::wstring const& commandId,
        std::optional<int32_t> const itemIndex,
        std::optional<POINT> const clickPoint)
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
            commandId == L"permanentDelete" ||
            commandId == L"copy" ||
            commandId == L"cut" ||
            commandId == L"paste" ||
            commandId == L"properties" ||
            commandId == L"moreWindowsActions" ||
            commandId == L"newFolder" ||
            commandId == L"newFile" ||
            commandId == L"compressToZip" ||
            commandId == L"restoreFromRecycleBin";

        if (ActivePane().is_archive_view && archiveReadOnlyCommand)
        {
            StatusTextBlock().Text(L"Archive mode is read-only in this slice.");
            return;
        }

        if (commandId == L"undo")
        {
            UndoLastOperationAsync();
            return;
        }

        if (commandId == L"redo")
        {
            RedoLastOperationAsync();
            return;
        }

        if (commandId == L"open")
        {
            // Archive and Recycle Bin views cannot multi-open; they keep focused-item activation.
            if (selectedItems.size() > 1 && !ActivePane().is_archive_view && !ActivePane().is_recycle_bin_view)
            {
                OpenMultipleItemsAsync(selectedItems);
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
            RefreshSidebarCaches();
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

        if (commandId == L"delete" || commandId == L"permanentDelete")
        {
            if (ActivePane().is_drives_view)
            {
                StatusTextBlock().Text(L"Cannot delete drives from This PC.");
                return;
            }

            if (selectedItems.empty())
            {
                StatusTextBlock().Text(L"Delete needs a selected item.");
                return;
            }

            bool const isRecycleBin =
                ::FileScoutNative::Domain::IsRecycleBinLocation(ActivePane().current_path);
            bool const permanent = (commandId == L"permanentDelete") || isRecycleBin;
            if (selectedItems.size() == 1)
            {
                ShowDeleteDialogAsync(selectedItems.front(), permanent);
            }
            else
            {
                ShowDeleteDialogAsync(selectedItems, permanent);
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
                m_cutItemPaths.clear();
                for (auto const& item : selectedItems)
                {
                    m_cutItemPaths.insert(item.parsing_name);
                }
                UpdateRowSelectionVisuals(ActivePane());
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
            PasteIntoAsync(ActivePane().current_path);
            return;
        }

        if (commandId == L"properties")
        {
            if (selectedItems.size() > 1)
            {
                std::vector<std::filesystem::path> paths;
                paths.reserve(selectedItems.size());
                for (auto const& selected : selectedItems)
                {
                    if (!selected.path.empty())
                    {
                        paths.push_back(selected.path);
                    }
                }
                if (paths.empty())
                {
                    StatusTextBlock().Text(L"Properties needs at least one valid item.");
                    return;
                }
                try
                {
                    m_shellFileOperationService.ShowPropertiesForItems(GetWindowHandle(), paths);
                    StatusTextBlock().Text(ToHString(
                        L"Opened properties for " + std::to_wstring(paths.size()) + L" items."));
                }
                catch (std::exception const&)
                {
                    StatusTextBlock().Text(ToHString(
                        L"Properties failed for " + std::to_wstring(paths.size()) + L" items."));
                }
                return;
            }

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

        if (commandId == L"compressToZip")
        {
            auto const& pane = ActivePane();
            if (pane.is_drives_view || pane.is_recycle_bin_view || pane.is_archive_view || pane.current_path.empty())
            {
                StatusTextBlock().Text(L"Compress to ZIP is only available inside a filesystem folder.");
                return;
            }

            std::vector<::FileScoutNative::Domain::ShellItemRef> targets = selectedItems;
            if (targets.empty() && item.has_value())
            {
                targets.push_back(*item);
            }
            if (targets.empty())
            {
                StatusTextBlock().Text(L"Compress to ZIP needs at least one selected item.");
                return;
            }

            std::vector<std::filesystem::path> targetPaths;
            targetPaths.reserve(targets.size());
            for (auto const& target : targets)
            {
                targetPaths.push_back(target.path);
            }
            auto const commonParent = ::FileScoutNative::Domain::TryGetCommonParentDirectory(targetPaths);
            if (!commonParent.has_value())
            {
                StatusTextBlock().Text(L"All selected items must be in the same folder to compress to ZIP.");
                return;
            }

            CompressSelectionToZipAsync(*commonParent, std::move(targets));
            return;
        }

        if (commandId == L"restoreFromRecycleBin")
        {
            if (!ActivePane().is_recycle_bin_view)
            {
                StatusTextBlock().Text(L"Restore is only available inside the Recycle Bin.");
                return;
            }

            std::vector<::FileScoutNative::Domain::ShellItemRef> targets = selectedItems;
            if (targets.empty() && item.has_value())
            {
                targets.push_back(*item);
            }
            if (targets.empty())
            {
                StatusTextBlock().Text(L"Restore needs at least one selected Recycle Bin item.");
                return;
            }

            RestoreRecycleBinSelectionAsync(std::move(targets));
            return;
        }

        if (commandId == L"emptyRecycleBin")
        {
            ShowEmptyRecycleBinDialogAsync();
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

            auto const rowIndex = itemIndex.has_value()
                ? *itemIndex
                : (ActivePane().active_list_view ? ActivePane().active_list_view.SelectedIndex() : -1);
            auto const anchor = ::FileScoutNative::Domain::ChooseContextMenuAnchor(
                clickPoint.has_value()
                    ? std::optional<::FileScoutNative::Domain::ScreenPoint>{ { clickPoint->x, clickPoint->y } }
                    : std::nullopt,
                TryGetDisplayedRowScreenRect(rowIndex),
                WindowClientScreenRect());
            POINT const screenPoint{ anchor.x, anchor.y };

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

        if (commandId == L"newFile")
        {
            ShowNewFileDialogAsync();
            return;
        }

        if (commandId == L"openInNewTab")
        {
            if (ActivePane().is_archive_view || ActivePane().is_recycle_bin_view)
            {
                StatusTextBlock().Text(L"Open in new tab is not available in this view.");
                return;
            }
            if (item.has_value() && item->is_folder && !item->path.empty())
            {
                AddTab(item->path);
            }
            else if (selectedItems.size() == 1 && selectedItems.front().is_folder && !selectedItems.front().path.empty())
            {
                AddTab(selectedItems.front().path);
            }
            else
            {
                StatusTextBlock().Text(L"Open in new tab needs a selected folder.");
            }
            return;
        }

        if (commandId == L"copyPath")
        {
            CopySelectedPaths(selectedItems);
            return;
        }

        if (commandId == L"openInTerminal")
        {
            OpenInTerminalForContext(item);
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

        if (commandId == L"navigateUp" || commandId == L"navigateUpBackspace")
        {
            NavigateUp();
            return;
        }

        if (commandId == L"goToPath")
        {
            ShowAddressTextBox();
            return;
        }

        if (commandId == L"newTab")
        {
            if (ActivePane().is_drives_view)
            {
                AddTabForDrivesView();
            }
            else
            {
                AddTab(ActivePane().current_path);
            }
            return;
        }

        if (commandId == L"reopenClosedTab")
        {
            ReopenClosedTab();
            return;
        }

        if (commandId == L"closeTab")
        {
            CloseTab(ActivePane().active_tab_index);
            return;
        }

        if (commandId == L"nextTab")
        {
            SwitchToAdjacentTab(1);
            return;
        }

        if (commandId == L"prevTab")
        {
            SwitchToAdjacentTab(-1);
            return;
        }

        if (commandId.size() == 10 && commandId.rfind(L"selectTab", 0) == 0 &&
            commandId[9] >= L'1' && commandId[9] <= L'9')
        {
            SelectTabByNumber(static_cast<int32_t>(commandId[9] - L'0'));
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

        if (commandId == L"toggleHiddenFiles")
        {
            ToggleHiddenFiles(m_activePaneIndex);
            return;
        }

        if (commandId == L"toggleExtensions")
        {
            ToggleExtensions(m_activePaneIndex);
            return;
        }

        if (commandId == L"setDefaultFileManager" || commandId == L"restoreExplorer")
        {
            SetDefaultFileManager(commandId == L"setDefaultFileManager");
            return;
        }
    }

    winrt::fire_and_forget MainWindow::PasteIntoAsync(std::filesystem::path destination)
    {
        auto lifetime = get_strong();
        struct ScopedPendingMutation
        {
            uint32_t& count;
            ScopedPendingMutation(uint32_t& c) : count(c) { ++count; }
            ~ScopedPendingMutation() { if (count > 0) { --count; } }
        };
        ScopedPendingMutation mutationGuard{ m_pendingMutationCount };

        StatusTextBlock().Text(ToHString(L"Pasting into " + destination.wstring() + L"..."));
        try
        {
            auto const owner = GetWindowHandle();
            // Read now: the paste may queue behind other shell work and must not see a later copy or cut.
            auto snapshot = m_shellClipboardService.CaptureClipboard();
            struct PasteWithIdentities
            {
                ::FileScoutNative::Shell::ClipboardPasteResult outcome;
                std::vector<::FileScoutNative::Domain::UndoPathPair> completed_items;
            };
            auto const pasteResult = co_await OnShellThread([this, owner, destination, snapshot = std::move(snapshot)]
                {
                    PasteWithIdentities out{};
                    out.outcome = m_shellClipboardService.PasteCaptured(owner, destination, snapshot);
                    out.completed_items.reserve(out.outcome.transferred_items.size());
                    for (auto const& pair : out.outcome.transferred_items)
                    {
                        ::FileScoutNative::Domain::UndoPathPair undoPair{};
                        undoPair.source = pair.source;
                        undoPair.target = pair.destination;
                        undoPair.target_identity =
                            ::FileScoutNative::Shell::TryCaptureFileIdentity(pair.destination);
                        out.completed_items.push_back(std::move(undoPair));
                    }
                    return out;
                });

            auto const& outcome = pasteResult.outcome;
            if (!pasteResult.completed_items.empty())
            {
                std::vector<std::filesystem::path> affectedFolders{ destination };
                ::FileScoutNative::Domain::UndoEntry entry{};
                entry.kind = outcome.was_move
                    ? ::FileScoutNative::Domain::UndoActionKind::Move
                    : ::FileScoutNative::Domain::UndoActionKind::Copy;
                entry.description = (outcome.was_move ? L"Move " : L"Copy ") +
                    std::to_wstring(pasteResult.completed_items.size()) +
                    (pasteResult.completed_items.size() == 1 ? L" item" : L" items");
                entry.items = pasteResult.completed_items;
                if (outcome.was_move)
                {
                    for (auto const& pair : pasteResult.completed_items)
                    {
                        if (pair.source.has_parent_path())
                        {
                            affectedFolders.push_back(pair.source.parent_path());
                        }
                    }
                }
                m_undoStack.Push(std::move(entry));
                RefreshAffectedFolders(affectedFolders);
            }
            else if (outcome.succeeded)
            {
                RefreshAffectedFolders({ destination });
            }

            if (outcome.succeeded)
            {
                m_cutItemPaths.clear();
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
                L"Couldn't paste into " + destination.wstring() + L". " +
                winrt::to_hstring(error.what()).c_str()));
        }
    }

    winrt::fire_and_forget MainWindow::ShowNewFolderDialogAsync()
    {
        if (m_modalDialogOpen)
        {
            co_return;
        }

        auto lifetime = get_strong();
        m_modalDialogOpen = true;
        auto const parent = ActivePane().current_path;

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

        winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult dialogResult =
            winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::None;
        try
        {
            dialogResult = co_await dialog.ShowAsync();
        }
        catch (...)
        {
            m_modalDialogOpen = false;
            FlushPendingWatchRefreshAllPanes();
            co_return;
        }
        m_modalDialogOpen = false;
        FlushPendingWatchRefreshAllPanes();

        if (dialogResult != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
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
            struct ScopedPendingMutation
            {
                uint32_t& count;
                ScopedPendingMutation(uint32_t& c) : count(c) { ++count; }
                ~ScopedPendingMutation() { if (count > 0) { --count; } }
            };
            ScopedPendingMutation mutationGuard{ m_pendingMutationCount };

            auto const owner = GetWindowHandle();
            auto const created = co_await OnShellThread([this, owner, parent, folderName]
                {
                    auto path = m_shellFileOperationService.CreateFolder(owner, parent, folderName);
                    auto identity = ::FileScoutNative::Shell::TryCaptureFileIdentity(path);
                    return std::pair{ std::move(path), identity };
                });
            auto const& createdPath = created.first;
            ::FileScoutNative::Domain::UndoPathPair pair{};
            pair.target = createdPath;
            pair.target_identity = created.second;
            m_undoStack.Push(::FileScoutNative::Domain::UndoEntry{
                .kind = ::FileScoutNative::Domain::UndoActionKind::CreateFolder,
                .description = L"Create folder '" + createdPath.filename().wstring() + L"'",
                .items = { std::move(pair) },
            });
            RefreshPanesShowing(parent, createdPath);
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
        if (m_modalDialogOpen)
        {
            co_return;
        }

        auto lifetime = get_strong();
        m_modalDialogOpen = true;

        auto textBox = winrt::Microsoft::UI::Xaml::Controls::TextBox{};
        textBox.Text(ToHString(item.display_name));
        auto const selectionLength = ::FileScoutNative::Domain::ComputeRenameSelectionLength(
            item.display_name,
            item.is_folder);
        textBox.Select(0, selectionLength);

        auto dialog = winrt::Microsoft::UI::Xaml::Controls::ContentDialog{};
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(L"Rename"));
        dialog.PrimaryButtonText(L"Rename");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
        dialog.Content(textBox);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult dialogResult =
            winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::None;
        try
        {
            dialogResult = co_await dialog.ShowAsync();
        }
        catch (...)
        {
            m_modalDialogOpen = false;
            FlushPendingWatchRefreshAllPanes();
            co_return;
        }
        m_modalDialogOpen = false;
        FlushPendingWatchRefreshAllPanes();

        if (dialogResult != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto const newName = TrimFileName(textBox.Text().c_str());
        if (auto const error = ::FileScoutNative::Domain::ValidateWindowsFileName(newName); error.has_value())
        {
            StatusTextBlock().Text(ToHString(*error));
            co_return;
        }

        if (::FileScoutNative::Domain::IsRenameNoOp(newName, item.display_name))
        {
            StatusTextBlock().Text(ToHString(L"Rename skipped: " + item.display_name));
            co_return;
        }

        try
        {
            struct ScopedPendingMutation
            {
                uint32_t& count;
                ScopedPendingMutation(uint32_t& c) : count(c) { ++count; }
                ~ScopedPendingMutation() { if (count > 0) { --count; } }
            };
            ScopedPendingMutation mutationGuard{ m_pendingMutationCount };

            auto const owner = GetWindowHandle();
            auto const source = item.path;
            auto const renamed = co_await OnShellThread([this, owner, source, newName]
                {
                    auto path = m_shellFileOperationService.RenameItem(owner, source, newName);
                    auto identity = ::FileScoutNative::Shell::TryCaptureFileIdentity(path);
                    return std::pair{ std::move(path), identity };
                });
            auto const& renamedPath = renamed.first;
            ::FileScoutNative::Domain::UndoPathPair pair{};
            pair.source = source;
            pair.target = renamedPath;
            pair.target_identity = renamed.second;
            m_undoStack.Push(::FileScoutNative::Domain::UndoEntry{
                .kind = ::FileScoutNative::Domain::UndoActionKind::Rename,
                .description = L"Rename '" + source.filename().wstring() + L"' to '" + renamedPath.filename().wstring() + L"'",
                .items = { std::move(pair) },
            });
            RefreshPanesShowing(source.parent_path(), renamedPath);
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
        previewErrorText.Foreground(LookupBrush(L"SystemFillColorCriticalBrush"));
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

        if (m_modalDialogOpen)
        {
            co_return;
        }
        m_modalDialogOpen = true;
        winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult dialogResult =
            winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::None;
        try
        {
            dialogResult = co_await dialog.ShowAsync();
        }
        catch (...)
        {
            m_modalDialogOpen = false;
            FlushPendingWatchRefreshAllPanes();
            co_return;
        }
        m_modalDialogOpen = false;
        FlushPendingWatchRefreshAllPanes();

        if (dialogResult != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
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
            struct ScopedPendingMutation
            {
                uint32_t& count;
                ScopedPendingMutation(uint32_t& c) : count(c) { ++count; }
                ~ScopedPendingMutation() { if (count > 0) { --count; } }
            };
            ScopedPendingMutation mutationGuard{ m_pendingMutationCount };

            auto const owner = GetWindowHandle();
            struct BatchRenameOutcome
            {
                std::vector<::FileScoutNative::Domain::UndoPathPair> completed_items;
                std::optional<::FileScoutNative::Shell::FileOperationError> error;
            };
            // Record every rename the Shell completed, even when a later one fails, so Ctrl+Z can
            // undo the partial batch.
            auto const renameOutcome = co_await OnShellThread([this, owner, requests]
                {
                    BatchRenameOutcome out{};
                    std::vector<::FileScoutNative::Shell::TransferredItemPair> pairs;
                    try
                    {
                        (void)m_shellFileOperationService.RenameItems(
                            owner,
                            std::span<::FileScoutNative::Shell::RenameRequest const>{ requests.data(), requests.size() },
                            &pairs);
                    }
                    catch (::FileScoutNative::Shell::FileOperationException const& err)
                    {
                        out.error = err.Error();
                    }
                    out.completed_items.reserve(pairs.size());
                    for (auto const& pair : pairs)
                    {
                        ::FileScoutNative::Domain::UndoPathPair undoPair{};
                        undoPair.source = pair.source;
                        undoPair.target = pair.destination;
                        undoPair.target_identity = ::FileScoutNative::Shell::TryCaptureFileIdentity(pair.destination);
                        out.completed_items.push_back(std::move(undoPair));
                    }
                    return out;
                });
            auto const& completed = renameOutcome.completed_items;
            if (!completed.empty())
            {
                m_undoStack.Push(::FileScoutNative::Domain::UndoEntry{
                    .kind = ::FileScoutNative::Domain::UndoActionKind::BatchRename,
                    .description = L"Batch rename " + std::to_wstring(completed.size()) +
                        (completed.size() == 1 ? L" item" : L" items"),
                    .items = completed,
                });
                RefreshPanesShowing(requests.front().path.parent_path(), completed.front().target);
            }
            if (renameOutcome.error.has_value())
            {
                throw ::FileScoutNative::Shell::FileOperationException(*renameOutcome.error);
            }
            if (completed.empty())
            {
                RefreshPanesShowing(requests.front().path.parent_path(), std::nullopt);
            }
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

    winrt::fire_and_forget MainWindow::ShowDeleteDialogAsync(
        ::FileScoutNative::Domain::ShellItemRef item,
        bool const permanent)
    {
        ShowDeleteDialogAsync(
            std::vector<::FileScoutNative::Domain::ShellItemRef>{ std::move(item) },
            permanent);
        co_return;
    }

    winrt::fire_and_forget MainWindow::ShowDeleteDialogAsync(
        std::vector<::FileScoutNative::Domain::ShellItemRef> items,
        bool const permanent)
    {
        if (items.empty() || m_modalDialogOpen)
        {
            co_return;
        }

        auto lifetime = get_strong();
        m_modalDialogOpen = true;

        int32_t const activePaneIndex = m_activePaneIndex;
        auto const currentPath = ActivePane().current_path;
        bool const isRecycleBin = ::FileScoutNative::Domain::IsRecycleBinLocation(currentPath);

        auto dialog = winrt::Microsoft::UI::Xaml::Controls::ContentDialog{};
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(permanent ? L"Delete Permanently" : L"Delete To Recycle Bin"));
        dialog.PrimaryButtonText(permanent ? L"Delete Permanently" : L"Delete");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        if (permanent)
        {
            dialog.Content(winrt::box_value(
                items.size() == 1
                    ? L"Are you sure you want to permanently delete \"" + items.front().display_name + L"\"? This cannot be undone."
                    : L"Are you sure you want to permanently delete these " + std::to_wstring(items.size()) + L" items? This cannot be undone."));
        }
        else
        {
            dialog.Content(winrt::box_value(
                items.size() == 1
                    ? L"Move " + items.front().display_name + L" to the Recycle Bin?"
                    : L"Move " + std::to_wstring(items.size()) + L" selected items to the Recycle Bin?"));
        }

        winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult dialogResult =
            winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::None;
        try
        {
            dialogResult = co_await dialog.ShowAsync();
        }
        catch (...)
        {
            m_modalDialogOpen = false;
            FlushPendingWatchRefreshAllPanes();
            co_return;
        }
        m_modalDialogOpen = false;
        FlushPendingWatchRefreshAllPanes();

        if (dialogResult != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            StatusTextBlock().Text(ToHString(BuildDeleteCanceledStatus(
                items.size() == 1 ? items.front().display_name : std::wstring{},
                items.size())));
            co_return;
        }

        HWND const ownerHwnd = GetWindowHandle();
        std::wstring const displayName = items.size() == 1 ? items.front().display_name : L"";
        std::size_t const itemCount = items.size();

        std::vector<std::filesystem::path> paths;
        paths.reserve(itemCount);
        for (auto const& item : items)
            paths.push_back(item.path);

        StatusTextBlock().Text(ToHString(
            permanent
                ? (itemCount == 1
                    ? L"Permanently deleting " + displayName + L"..."
                    : L"Permanently deleting " + std::to_wstring(itemCount) + L" items...")
                : (itemCount == 1
                    ? L"Deleting " + displayName + L"..."
                    : L"Deleting " + std::to_wstring(itemCount) + L" items...")));
        ++m_panes[static_cast<std::size_t>(activePaneIndex)].background_op_progress_count;
        UpdatePaneProgressBar(activePaneIndex);
        ++m_pendingMutationCount;

        // IFileOperation is created CLSCTX_INPROC_SERVER, so it needs a real single-threaded
        // apartment, which thread-pool threads do not have.
        co_await ::FileScoutNative::Shell::RunOnShellThread(
            m_shellWorker, [](std::function<void()> continuation) { continuation(); }, [] {});

        bool success = true;
        bool canceled = false;
        std::wstring failureDetail;
        std::vector<::FileScoutNative::Shell::DeletedItemRecord> deletedRecords;
        try
        {
            auto const t0 = std::chrono::steady_clock::now();
            if (!permanent && !isRecycleBin)
            {
                m_shellFileOperationService.DeleteItems(
                    ownerHwnd,
                    std::span<std::filesystem::path const>{ paths.data(), paths.size() },
                    /*permanent=*/false,
                    &deletedRecords);
            }
            else if (paths.size() == 1)
            {
                m_shellFileOperationService.DeleteItem(ownerHwnd, paths.front(), permanent);
            }
            else
            {
                m_shellFileOperationService.DeleteItems(
                    ownerHwnd,
                    std::span<std::filesystem::path const>{ paths.data(), paths.size() },
                    permanent);
            }
            auto const deleteMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            TraceLog(std::wstring(permanent ? L"PermanentDelete: " : L"Delete: ")
                + std::to_wstring(itemCount) + L" item(s) in "
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
            [this, lifetime, success, canceled, permanent, isRecycleBin, currentPath, paths, displayName, itemCount, activePaneIndex, failureDetail, deletedRecords = std::move(deletedRecords)]()
            {
                if (m_pendingMutationCount > 0)
                {
                    --m_pendingMutationCount;
                }
                auto& activePaneRef = m_panes[static_cast<std::size_t>(activePaneIndex)];
                if (activePaneRef.background_op_progress_count > 0)
                {
                    --activePaneRef.background_op_progress_count;
                }
                UpdatePaneProgressBar(activePaneIndex);

                if (!permanent && !isRecycleBin && !deletedRecords.empty())
                {
                    ::FileScoutNative::Domain::UndoEntry entry{};
                    entry.kind = ::FileScoutNative::Domain::UndoActionKind::DeleteToRecycleBin;
                    entry.description = deletedRecords.size() == 1
                        ? L"Delete '" + deletedRecords.front().original_path.filename().wstring() + L"'"
                        : L"Delete " + std::to_wstring(deletedRecords.size()) + L" items";
                    entry.items.reserve(deletedRecords.size());
                    for (auto const& rec : deletedRecords)
                    {
                        ::FileScoutNative::Domain::UndoPathPair pair{};
                        pair.source = rec.original_path;
                        pair.recycle_bin_parsing_name = rec.recycle_bin_parsing_name;
                        entry.items.push_back(std::move(pair));
                    }
                    m_undoStack.Push(std::move(entry));
                    if (!success)
                    {
                        RefreshPanesShowing(currentPath);
                        RefreshRecycleBinPanes();
                    }
                }

                if (success)
                {
                    if (isRecycleBin)
                    {
                        RefreshRecycleBinPanes();
                    }
                    else
                    {
                        RefreshPanesShowing(currentPath);
                        RefreshRecycleBinPanes();
                    }
                    StatusTextBlock().Text(ToHString(
                        permanent
                            ? (itemCount == 1
                                ? L"Permanently deleted " + displayName
                                : L"Permanently deleted " + std::to_wstring(itemCount) + L" items")
                            : (itemCount == 1
                                ? L"Deleted " + displayName + L" to Recycle Bin"
                                : L"Deleted " + std::to_wstring(itemCount) + L" items to Recycle Bin")));
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
                        return item.is_folder && ::FileScoutNative::Domain::EqualsIgnoreCase(item.display_name, name);
                    });
            };

        while (folderNameExists(candidate))
        {
            candidate = L"New Folder (" + std::to_wstring(suffix++) + L")";
        }

        return candidate;
    }

    std::wstring MainWindow::SuggestNewFileName() const
    {
        std::wstring candidate = L"New Text Document.txt";
        int32_t suffix = 2;

        auto fileNameExists = [this](std::wstring const& name)
            {
                return std::ranges::any_of(ActivePane().items, [&name](auto const& item)
                    {
                        return ::FileScoutNative::Domain::EqualsIgnoreCase(item.display_name, name) ||
                            ::FileScoutNative::Domain::EqualsIgnoreCase(item.path.filename().wstring(), name);
                    });
            };

        while (fileNameExists(candidate))
        {
            candidate = L"New Text Document (" + std::to_wstring(suffix++) + L").txt";
        }

        return candidate;
    }

    winrt::fire_and_forget MainWindow::ShowNewFileDialogAsync()
    {
        if (m_modalDialogOpen)
        {
            co_return;
        }

        auto const& pane = ActivePane();
        if (pane.is_drives_view || pane.is_recycle_bin_view || pane.is_archive_view || pane.current_path.empty())
        {
            StatusTextBlock().Text(L"New file is only available inside a filesystem folder.");
            co_return;
        }

        auto lifetime = get_strong();
        m_modalDialogOpen = true;
        auto const parent = pane.current_path;
        auto const suggestedName = SuggestNewFileName();

        auto textBox = winrt::Microsoft::UI::Xaml::Controls::TextBox{};
        textBox.Text(ToHString(suggestedName));
        textBox.Select(0, ::FileScoutNative::Domain::ComputeRenameSelectionLength(suggestedName, false));

        auto dialog = winrt::Microsoft::UI::Xaml::Controls::ContentDialog{};
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(L"Create New File"));
        dialog.PrimaryButtonText(L"Create");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
        dialog.Content(textBox);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult dialogResult =
            winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::None;
        try
        {
            dialogResult = co_await dialog.ShowAsync();
        }
        catch (...)
        {
            m_modalDialogOpen = false;
            FlushPendingWatchRefreshAllPanes();
            co_return;
        }
        m_modalDialogOpen = false;
        FlushPendingWatchRefreshAllPanes();

        if (dialogResult != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto const fileName = TrimFileName(textBox.Text().c_str());
        if (auto const error = ::FileScoutNative::Domain::ValidateWindowsFileName(fileName); error.has_value())
        {
            StatusTextBlock().Text(ToHString(*error));
            co_return;
        }

        try
        {
            struct ScopedPendingMutation
            {
                uint32_t& count;
                ScopedPendingMutation(uint32_t& c) : count(c) { ++count; }
                ~ScopedPendingMutation() { if (count > 0) { --count; } }
            };
            ScopedPendingMutation mutationGuard{ m_pendingMutationCount };

            auto const owner = GetWindowHandle();
            auto const created = co_await OnShellThread([this, owner, parent, fileName]
                {
                    auto path = m_shellFileOperationService.CreateEmptyFile(owner, parent, fileName);
                    auto identity = ::FileScoutNative::Shell::TryCaptureFileIdentity(path);
                    return std::pair{ std::move(path), identity };
                });
            auto const& createdPath = created.first;
            ::FileScoutNative::Domain::UndoPathPair pair{};
            pair.target = createdPath;
            pair.target_identity = created.second;
            m_undoStack.Push(::FileScoutNative::Domain::UndoEntry{
                .kind = ::FileScoutNative::Domain::UndoActionKind::CreateFile,
                .description = L"Create file '" + createdPath.filename().wstring() + L"'",
                .items = { std::move(pair) },
            });
            RefreshPanesShowing(parent, createdPath);
            StatusTextBlock().Text(ToHString(L"Created file " + createdPath.filename().wstring() + L"."));
        }
        catch (::FileScoutNative::Shell::FileOperationException const& error)
        {
            std::wstring status = L"Couldn't create file " + fileName + L".";
            if (!error.Error().detail.empty())
            {
                status += L" ";
                status += error.Error().detail;
            }
            StatusTextBlock().Text(ToHString(status));
        }
        catch (std::exception const& error)
        {
            StatusTextBlock().Text(ToHString(
                L"Couldn't create file " + fileName + L". " + winrt::to_hstring(error.what()).c_str()));
        }
    }

    void MainWindow::RefreshRecycleBinPanes()
    {
        for (int32_t idx = 0; idx < static_cast<int32_t>(m_panes.size()); ++idx)
        {
            auto& pane = m_panes[static_cast<std::size_t>(idx)];
            for (int32_t tabIdx = 0; tabIdx < static_cast<int32_t>(pane.tabs.size()); ++tabIdx)
            {
                if (tabIdx == pane.active_tab_index)
                {
                    continue;
                }
                auto& tab = pane.tabs[static_cast<std::size_t>(tabIdx)];
                if (tab.is_recycle_bin_view ||
                    ::FileScoutNative::Domain::IsRecycleBinLocation(tab.path))
                {
                    tab.items.clear();
                    tab.selection.Clear();
                    tab.selected_item_index = -1;
                    if (tabIdx < static_cast<int32_t>(pane.tab_rows.size()))
                    {
                        pane.tab_rows[static_cast<std::size_t>(tabIdx)].Clear();
                    }
                }
            }
            if (pane.is_recycle_bin_view ||
                ::FileScoutNative::Domain::IsRecycleBinLocation(pane.current_path))
            {
                if (pane.search_mode != SearchMode::None)
                {
                    ResetSearchState(idx, false);
                }
                LoadDirectoryAsync(pane.current_path, idx, std::nullopt);
            }
        }
    }

    void MainWindow::RefreshAffectedFolders(
        std::vector<std::filesystem::path> const& folders,
        std::optional<std::filesystem::path> selectAfter)
    {
        std::unordered_set<std::wstring> refreshedKeys;
        bool usedSelectAfter = false;
        for (auto const& folder : folders)
        {
            if (folder.empty())
            {
                continue;
            }
            auto const key = ::FileScoutNative::Domain::SidebarPathKey(folder);
            if (!refreshedKeys.insert(key).second)
            {
                continue;
            }
            if (!usedSelectAfter && selectAfter.has_value() &&
                ::FileScoutNative::Domain::SidebarPathKey(selectAfter->parent_path()) == key)
            {
                RefreshPanesShowing(folder, selectAfter);
                usedSelectAfter = true;
            }
            else
            {
                RefreshPanesShowing(folder);
            }
        }
    }

    winrt::fire_and_forget MainWindow::UndoLastOperationAsync()
    {
        if (m_undoOperationInFlight || m_modalDialogOpen)
        {
            co_return;
        }
        if (m_pendingMutationCount > 0)
        {
            StatusTextBlock().Text(L"Wait for the current file operation to finish before undoing.");
            co_return;
        }

        auto entryOpt = m_undoStack.PopUndo();
        if (!entryOpt.has_value())
        {
            StatusTextBlock().Text(L"Nothing to undo.");
            co_return;
        }

        auto lifetime = get_strong();
        struct ScopedUndoFlag
        {
            bool& flag;
            ~ScopedUndoFlag() { flag = false; }
        };
        m_undoOperationInFlight = true;
        ScopedUndoFlag flagGuard{ m_undoOperationInFlight };

        auto entry = std::move(*entryOpt);
        auto const owner = GetWindowHandle();
        StatusTextBlock().Text(ToHString(L"Undoing " + entry.description + L"..."));

        struct UndoReplayOutcome
        {
            std::vector<::FileScoutNative::Domain::UndoPathPair> completed;
            std::vector<::FileScoutNative::Domain::UndoPathPair> remaining;
            bool failed{ false };
            bool canceled{ false };
            std::optional<std::size_t> gone_pair;
            std::wstring error_detail;
        };

        auto const outcome = co_await OnShellThread([this, owner, entry]() -> UndoReplayOutcome
            {
                using Kind = ::FileScoutNative::Domain::UndoActionKind;
                UndoReplayOutcome out{};
                auto const& items = entry.items;

                auto recordFailure = [&](std::size_t const failedReverseIdx, bool const isReverse, bool const isCanceled,
                    std::wstring detail, bool const gone = false)
                {
                    out.failed = true;
                    out.canceled = isCanceled;
                    out.error_detail = std::move(detail);
                    if (gone)
                    {
                        out.gone_pair = isReverse ? failedReverseIdx : 0;
                    }
                    if (isReverse)
                    {
                        for (std::size_t idx = 0; idx <= failedReverseIdx; ++idx)
                        {
                            out.remaining.push_back(items[idx]);
                        }
                    }
                    else
                    {
                        for (std::size_t idx = failedReverseIdx; idx < items.size(); ++idx)
                        {
                            out.remaining.push_back(items[idx]);
                        }
                    }
                };

                switch (entry.kind)
                {
                case Kind::Rename:
                case Kind::BatchRename:
                    for (std::size_t rev = items.size(); rev-- > 0;)
                    {
                        auto pair = items[rev];
                        auto const identity = ::FileScoutNative::Shell::CheckRecordedIdentity(pair.target, pair.target_identity);
                        if (identity != ::FileScoutNative::Shell::RecordedIdentityCheck::Matches)
                        {
                            recordFailure(rev, /*isReverse=*/true, false,
                                L"Item at '" + pair.target.filename().wstring() +
                                L"' changed since it was renamed; cannot safely undo.",
                                identity == ::FileScoutNative::Shell::RecordedIdentityCheck::Gone);
                            break;
                        }
                        try
                        {
                            (void)m_shellFileOperationService.RenameItem(
                                owner, pair.target, pair.source.filename().wstring());
                            pair.target_identity = ::FileScoutNative::Shell::TryCaptureFileIdentity(pair.source);
                            out.completed.push_back(std::move(pair));
                        }
                        catch (::FileScoutNative::Shell::FileOperationException const& err)
                        {
                            recordFailure(rev, /*isReverse=*/true, err.IsCanceled(), err.Error().detail);
                            break;
                        }
                        catch (std::exception const& err)
                        {
                            recordFailure(rev, /*isReverse=*/true, false, winrt::to_hstring(err.what()).c_str());
                            break;
                        }
                    }
                    // Restore original forward order for completed items so Redo replays in original order.
                    std::reverse(out.completed.begin(), out.completed.end());
                    break;

                case Kind::CreateFolder:
                case Kind::CreateFile:
                case Kind::Copy:
                    for (std::size_t i = 0; i < items.size(); ++i)
                    {
                        auto const& pair = items[i];
                        // Verify filesystem identity before recycling by path so Undo never
                        // deletes a different item that later replaced the original path.
                        auto const identity = ::FileScoutNative::Shell::CheckRecordedIdentity(pair.target, pair.target_identity);
                        if (identity != ::FileScoutNative::Shell::RecordedIdentityCheck::Matches)
                        {
                            recordFailure(i, /*isReverse=*/false, false,
                                L"Item at '" + pair.target.filename().wstring() +
                                L"' changed since it was created; cannot safely undo.",
                                identity == ::FileScoutNative::Shell::RecordedIdentityCheck::Gone);
                            break;
                        }

                        if (entry.kind == Kind::CreateFolder)
                        {
                            std::error_code ec;
                            bool const isDir = std::filesystem::is_directory(pair.target, ec);
                            bool const isEmpty = !ec && isDir && std::filesystem::is_empty(pair.target, ec);
                            if (ec || !isDir || !isEmpty)
                            {
                                recordFailure(i, /*isReverse=*/false, false,
                                    L"Folder '" + pair.target.filename().wstring() +
                                    L"' is no longer empty; cannot undo creation.");
                                break;
                            }
                        }
                        else if (entry.kind == Kind::CreateFile)
                        {
                            std::error_code ec;
                            bool const isRegular = std::filesystem::is_regular_file(pair.target, ec);
                            auto const fileSize = (!ec && isRegular) ? std::filesystem::file_size(pair.target, ec) : 1u;
                            if (ec || !isRegular || fileSize != 0)
                            {
                                recordFailure(i, /*isReverse=*/false, false,
                                    L"File '" + pair.target.filename().wstring() +
                                    L"' is no longer empty; cannot undo creation.");
                                break;
                            }
                        }

                        try
                        {
                            std::filesystem::path const singleTarget[1]{ pair.target };
                            m_shellFileOperationService.DeleteItems(owner, singleTarget, /*permanent=*/false);
                            out.completed.push_back(pair);
                        }
                        catch (::FileScoutNative::Shell::FileOperationException const& err)
                        {
                            recordFailure(i, /*isReverse=*/false, err.IsCanceled(), err.Error().detail);
                            break;
                        }
                        catch (std::exception const& err)
                        {
                            recordFailure(i, /*isReverse=*/false, false, winrt::to_hstring(err.what()).c_str());
                            break;
                        }
                    }
                    break;

                case Kind::Move:
                    for (std::size_t rev = items.size(); rev-- > 0;)
                    {
                        auto pair = items[rev];
                        auto const identity = ::FileScoutNative::Shell::CheckRecordedIdentity(pair.target, pair.target_identity);
                        if (identity != ::FileScoutNative::Shell::RecordedIdentityCheck::Matches)
                        {
                            recordFailure(rev, /*isReverse=*/true, false,
                                L"Item at '" + pair.target.filename().wstring() +
                                L"' changed since it was moved; cannot safely undo.",
                                identity == ::FileScoutNative::Shell::RecordedIdentityCheck::Gone);
                            break;
                        }
                        try
                        {
                            m_shellFileOperationService.MoveItemFromPath(
                                owner,
                                pair.target,
                                pair.source.parent_path(),
                                pair.source.filename().wstring());
                            pair.target_identity = ::FileScoutNative::Shell::TryCaptureFileIdentity(pair.source);
                            out.completed.push_back(std::move(pair));
                        }
                        catch (::FileScoutNative::Shell::FileOperationException const& err)
                        {
                            recordFailure(rev, /*isReverse=*/true, err.IsCanceled(), err.Error().detail);
                            break;
                        }
                        catch (std::exception const& err)
                        {
                            recordFailure(rev, /*isReverse=*/true, false, winrt::to_hstring(err.what()).c_str());
                            break;
                        }
                    }
                    std::reverse(out.completed.begin(), out.completed.end());
                    break;

                case Kind::DeleteToRecycleBin:
                {
                    std::vector<std::wstring> targetParsingNames;
                    targetParsingNames.reserve(items.size());
                    for (auto const& pair : items)
                    {
                        if (!pair.recycle_bin_parsing_name.empty())
                        {
                            targetParsingNames.push_back(pair.recycle_bin_parsing_name);
                        }
                    }
                    if (targetParsingNames.empty())
                    {
                        // Permanently unidentifiable entry: retire it rather than blocking older undo entries.
                        out.failed = true;
                        out.error_detail = L"Skipped deleted item(s) whose Recycle Bin identity was unavailable.";
                        break;
                    }

                    try
                    {
                        std::vector<std::wstring> restoredParsingNames;
                        (void)m_shellFileOperationService.RestoreRecycleBinItems(
                            owner, targetParsingNames, &restoredParsingNames);

                        auto wasRestored = [&](std::wstring const& name)
                        {
                            if (name.empty())
                            {
                                return false;
                            }
                            for (auto const& restored : restoredParsingNames)
                            {
                                if (::CompareStringOrdinal(
                                        name.c_str(), static_cast<int>(name.size()),
                                        restored.c_str(), static_cast<int>(restored.size()),
                                        TRUE) == CSTR_EQUAL)
                                {
                                    return true;
                                }
                            }
                            return false;
                        };

                        std::size_t missingCount = 0;
                        for (auto pair : items)
                        {
                            if (wasRestored(pair.recycle_bin_parsing_name))
                            {
                                // Capture the restored file's live identity so Redo verifies it
                                // before recycling the path again.
                                pair.target_identity = ::FileScoutNative::Shell::TryCaptureFileIdentity(pair.source);
                                out.completed.push_back(std::move(pair));
                            }
                            else
                            {
                                ++missingCount;
                            }
                        }

                        if (missingCount > 0)
                        {
                            // Confirmed absent from the Recycle Bin: retire the missing items so
                            // subsequent Ctrl+Z can reach earlier history entries.
                            out.failed = true;
                            out.error_detail = out.completed.empty()
                                ? L"The deleted item(s) are no longer in the Recycle Bin (skipped)."
                                : L"Some deleted items were no longer in the Recycle Bin (skipped).";
                        }
                    }
                    catch (::FileScoutNative::Shell::FileOperationException const& err)
                    {
                        out.failed = true;
                        out.canceled = err.IsCanceled();
                        out.error_detail = err.Error().detail;
                        // If RestoreRecycleBinItems confirmed none of the items exist in the bin,
                        // retire the entry; keep transient/canceled errors retryable.
                        bool const confirmedMissing =
                            !err.IsCanceled() &&
                            err.Error().detail.find(L"could not be found in the Recycle Bin") != std::wstring::npos;
                        if (!confirmedMissing)
                        {
                            out.remaining = items;
                        }
                    }
                    catch (std::exception const& err)
                    {
                        out.failed = true;
                        out.error_detail = winrt::to_hstring(err.what()).c_str();
                        out.remaining = items;
                    }
                    break;
                }
                }
                return out;
            });

        using Kind = ::FileScoutNative::Domain::UndoActionKind;
        bool const touchesRecycleBin =
            entry.kind == Kind::DeleteToRecycleBin ||
            entry.kind == Kind::CreateFolder ||
            entry.kind == Kind::CreateFile ||
            entry.kind == Kind::Copy;

        if (!outcome.completed.empty())
        {
            std::vector<std::filesystem::path> affectedFolders;
            std::optional<std::filesystem::path> selectAfter;
            for (auto const& pair : outcome.completed)
            {
                if (pair.source.has_parent_path()) { affectedFolders.push_back(pair.source.parent_path()); }
                if (pair.target.has_parent_path()) { affectedFolders.push_back(pair.target.parent_path()); }
            }
            if (entry.kind == Kind::Rename || entry.kind == Kind::BatchRename ||
                entry.kind == Kind::Move || entry.kind == Kind::DeleteToRecycleBin)
            {
                if (!outcome.completed.front().source.empty())
                {
                    selectAfter = outcome.completed.front().source;
                }
            }

            m_undoStack.PushRedo(::FileScoutNative::Domain::UndoEntry{
                .kind = entry.kind,
                .description = entry.description,
                .items = outcome.completed,
            });
            RefreshAffectedFolders(affectedFolders, selectAfter);
            if (touchesRecycleBin)
            {
                RefreshRecycleBinPanes();
            }
        }

        auto remaining = outcome.remaining;
        bool discarded = false;
        if (outcome.gone_pair.has_value() &&
            co_await ConfirmDiscardHistoryStepAsync(BuildFileOperationErrorStatus(
                L"Couldn't undo " + entry.description, outcome.error_detail)))
        {
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(*outcome.gone_pair));
            discarded = true;
        }
        if (!remaining.empty())
        {
            m_undoStack.PushUndoPreservingRedo(::FileScoutNative::Domain::UndoEntry{
                .kind = entry.kind,
                .description = entry.description,
                .items = std::move(remaining),
            });
        }

        if (!outcome.failed)
        {
            StatusTextBlock().Text(ToHString(L"Undid: " + entry.description));
        }
        else if (outcome.canceled)
        {
            StatusTextBlock().Text(outcome.completed.empty()
                ? L"Undo canceled."
                : ToHString(L"Undo partially completed (" + std::to_wstring(outcome.completed.size()) +
                            L" item(s)); remaining canceled."));
        }
        else
        {
            StatusTextBlock().Text(ToHString(
                BuildFileOperationErrorStatus(L"Couldn't undo " + entry.description,
                    discarded ? outcome.error_detail + L" Removed it from history." : outcome.error_detail)));
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> MainWindow::ConfirmDiscardHistoryStepAsync(
        std::wstring const detail)
    {
        auto lifetime = get_strong();
        m_modalDialogOpen = true;
        auto dialog = winrt::Microsoft::UI::Xaml::Controls::ContentDialog{};
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(L"Item Not Found"));
        dialog.Content(winrt::box_value(ToHString(
            detail + L"\n\nRemove this step from history? Keep it if the item can come back.")));
        dialog.PrimaryButtonText(L"Remove");
        dialog.CloseButtonText(L"Keep");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);

        auto result = winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::None;
        try
        {
            result = co_await dialog.ShowAsync();
        }
        catch (...)
        {
        }
        m_modalDialogOpen = false;
        FlushPendingWatchRefreshAllPanes();
        co_return result == winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary;
    }

    winrt::fire_and_forget MainWindow::RedoLastOperationAsync()
    {
        if (m_undoOperationInFlight || m_modalDialogOpen)
        {
            co_return;
        }
        if (m_pendingMutationCount > 0)
        {
            StatusTextBlock().Text(L"Wait for the current file operation to finish before redoing.");
            co_return;
        }

        auto entryOpt = m_undoStack.PopRedo();
        if (!entryOpt.has_value())
        {
            StatusTextBlock().Text(L"Nothing to redo.");
            co_return;
        }

        auto lifetime = get_strong();
        struct ScopedUndoFlag
        {
            bool& flag;
            ~ScopedUndoFlag() { flag = false; }
        };
        m_undoOperationInFlight = true;
        ScopedUndoFlag flagGuard{ m_undoOperationInFlight };

        auto entry = std::move(*entryOpt);
        auto const owner = GetWindowHandle();
        StatusTextBlock().Text(ToHString(L"Redoing " + entry.description + L"..."));

        struct RedoReplayOutcome
        {
            std::vector<::FileScoutNative::Domain::UndoPathPair> completed;
            std::vector<::FileScoutNative::Domain::UndoPathPair> remaining;
            bool failed{ false };
            bool canceled{ false };
            std::optional<std::size_t> gone_pair;
            std::wstring error_detail;
        };

        auto const outcome = co_await OnShellThread([this, owner, entry]() -> RedoReplayOutcome
            {
                using Kind = ::FileScoutNative::Domain::UndoActionKind;
                RedoReplayOutcome out{};
                auto const& items = entry.items;

                auto recordFailure = [&](std::size_t const failedIdx, bool const isCanceled, std::wstring detail,
                    bool const gone = false)
                {
                    out.failed = true;
                    out.canceled = isCanceled;
                    out.error_detail = std::move(detail);
                    if (gone)
                    {
                        out.gone_pair = 0;
                    }
                    for (std::size_t idx = failedIdx; idx < items.size(); ++idx)
                    {
                        out.remaining.push_back(items[idx]);
                    }
                };

                switch (entry.kind)
                {
                case Kind::Rename:
                case Kind::BatchRename:
                    for (std::size_t i = 0; i < items.size(); ++i)
                    {
                        auto pair = items[i];
                        auto const identity = ::FileScoutNative::Shell::CheckRecordedIdentity(pair.source, pair.target_identity);
                        if (identity != ::FileScoutNative::Shell::RecordedIdentityCheck::Matches)
                        {
                            recordFailure(i, false,
                                L"Item at '" + pair.source.filename().wstring() +
                                L"' changed since it was undone; cannot safely redo rename.",
                                identity == ::FileScoutNative::Shell::RecordedIdentityCheck::Gone);
                            break;
                        }
                        try
                        {
                            (void)m_shellFileOperationService.RenameItem(
                                owner, pair.source, pair.target.filename().wstring());
                            pair.target_identity = ::FileScoutNative::Shell::TryCaptureFileIdentity(pair.target);
                            out.completed.push_back(std::move(pair));
                        }
                        catch (::FileScoutNative::Shell::FileOperationException const& err)
                        {
                            recordFailure(i, err.IsCanceled(), err.Error().detail);
                            break;
                        }
                        catch (std::exception const& err)
                        {
                            recordFailure(i, false, winrt::to_hstring(err.what()).c_str());
                            break;
                        }
                    }
                    break;

                case Kind::CreateFolder:
                    for (std::size_t i = 0; i < items.size(); ++i)
                    {
                        auto pair = items[i];
                        try
                        {
                            auto const created = m_shellFileOperationService.CreateFolder(
                                owner, pair.target.parent_path(), pair.target.filename().wstring());
                            pair.target = created;
                            pair.target_identity = ::FileScoutNative::Shell::TryCaptureFileIdentity(created);
                            out.completed.push_back(std::move(pair));
                        }
                        catch (::FileScoutNative::Shell::FileOperationException const& err)
                        {
                            recordFailure(i, err.IsCanceled(), err.Error().detail);
                            break;
                        }
                        catch (std::exception const& err)
                        {
                            recordFailure(i, false, winrt::to_hstring(err.what()).c_str());
                            break;
                        }
                    }
                    break;

                case Kind::CreateFile:
                    for (std::size_t i = 0; i < items.size(); ++i)
                    {
                        auto pair = items[i];
                        try
                        {
                            auto const created = m_shellFileOperationService.CreateEmptyFile(
                                owner, pair.target.parent_path(), pair.target.filename().wstring());
                            pair.target = created;
                            pair.target_identity = ::FileScoutNative::Shell::TryCaptureFileIdentity(created);
                            out.completed.push_back(std::move(pair));
                        }
                        catch (::FileScoutNative::Shell::FileOperationException const& err)
                        {
                            recordFailure(i, err.IsCanceled(), err.Error().detail);
                            break;
                        }
                        catch (std::exception const& err)
                        {
                            recordFailure(i, false, winrt::to_hstring(err.what()).c_str());
                            break;
                        }
                    }
                    break;

                case Kind::Move:
                    for (std::size_t i = 0; i < items.size(); ++i)
                    {
                        auto pair = items[i];
                        auto const identity = ::FileScoutNative::Shell::CheckRecordedIdentity(pair.source, pair.target_identity);
                        if (identity != ::FileScoutNative::Shell::RecordedIdentityCheck::Matches)
                        {
                            recordFailure(i, false,
                                L"Item at '" + pair.source.filename().wstring() +
                                L"' changed since it was undone; cannot safely redo move.",
                                identity == ::FileScoutNative::Shell::RecordedIdentityCheck::Gone);
                            break;
                        }
                        try
                        {
                            m_shellFileOperationService.MoveItemFromPath(
                                owner,
                                pair.source,
                                pair.target.parent_path(),
                                pair.target.filename().wstring());
                            pair.target_identity = ::FileScoutNative::Shell::TryCaptureFileIdentity(pair.target);
                            out.completed.push_back(std::move(pair));
                        }
                        catch (::FileScoutNative::Shell::FileOperationException const& err)
                        {
                            recordFailure(i, err.IsCanceled(), err.Error().detail);
                            break;
                        }
                        catch (std::exception const& err)
                        {
                            recordFailure(i, false, winrt::to_hstring(err.what()).c_str());
                            break;
                        }
                    }
                    break;

                case Kind::Copy:
                    for (std::size_t i = 0; i < items.size(); ++i)
                    {
                        auto pair = items[i];
                        try
                        {
                            m_shellFileOperationService.CopyItemFromPath(
                                owner,
                                pair.source,
                                pair.target.parent_path(),
                                pair.target.filename().wstring());
                            pair.target_identity = ::FileScoutNative::Shell::TryCaptureFileIdentity(pair.target);
                            out.completed.push_back(std::move(pair));
                        }
                        catch (::FileScoutNative::Shell::FileOperationException const& err)
                        {
                            recordFailure(i, err.IsCanceled(), err.Error().detail);
                            break;
                        }
                        catch (std::exception const& err)
                        {
                            recordFailure(i, false, winrt::to_hstring(err.what()).c_str());
                            break;
                        }
                    }
                    break;

                case Kind::DeleteToRecycleBin:
                    for (std::size_t i = 0; i < items.size(); ++i)
                    {
                        auto pair = items[i];
                        auto const identity = ::FileScoutNative::Shell::CheckRecordedIdentity(pair.source, pair.target_identity);
                        if (identity != ::FileScoutNative::Shell::RecordedIdentityCheck::Matches)
                        {
                            recordFailure(i, false,
                                L"Item at '" + pair.source.filename().wstring() +
                                L"' changed since it was restored; cannot safely redo delete.",
                                identity == ::FileScoutNative::Shell::RecordedIdentityCheck::Gone);
                            break;
                        }

                        std::vector<::FileScoutNative::Shell::DeletedItemRecord> deletedRecords;
                        auto applyCompletedDeleteIfPresent = [&]() -> bool
                        {
                            if (deletedRecords.empty())
                            {
                                return false;
                            }
                            pair.recycle_bin_parsing_name = deletedRecords.front().recycle_bin_parsing_name;
                            out.completed.push_back(std::move(pair));
                            return true;
                        };
                        try
                        {
                            std::filesystem::path const singleTarget[1]{ pair.source };
                            m_shellFileOperationService.DeleteItems(
                                owner, singleTarget, /*permanent=*/false, &deletedRecords);
                            if (!applyCompletedDeleteIfPresent())
                            {
                                pair.recycle_bin_parsing_name.clear();
                                out.completed.push_back(std::move(pair));
                            }
                        }
                        catch (::FileScoutNative::Shell::FileOperationException const& err)
                        {
                            bool const completedCurrent = applyCompletedDeleteIfPresent();
                            recordFailure(completedCurrent ? (i + 1) : i, err.IsCanceled(), err.Error().detail);
                            break;
                        }
                        catch (std::exception const& err)
                        {
                            bool const completedCurrent = applyCompletedDeleteIfPresent();
                            recordFailure(completedCurrent ? (i + 1) : i, false, winrt::to_hstring(err.what()).c_str());
                            break;
                        }
                    }
                    break;
                }
                return out;
            });

        using Kind = ::FileScoutNative::Domain::UndoActionKind;
        bool const touchesRecycleBin = (entry.kind == Kind::DeleteToRecycleBin);

        if (!outcome.completed.empty())
        {
            std::vector<std::filesystem::path> affectedFolders;
            std::optional<std::filesystem::path> selectAfter;
            for (auto const& pair : outcome.completed)
            {
                if (pair.source.has_parent_path()) { affectedFolders.push_back(pair.source.parent_path()); }
                if (pair.target.has_parent_path()) { affectedFolders.push_back(pair.target.parent_path()); }
            }
            if (!outcome.completed.front().target.empty())
            {
                selectAfter = outcome.completed.front().target;
            }

            m_undoStack.PushUndoPreservingRedo(::FileScoutNative::Domain::UndoEntry{
                .kind = entry.kind,
                .description = entry.description,
                .items = outcome.completed,
            });
            RefreshAffectedFolders(affectedFolders, selectAfter);
            if (touchesRecycleBin)
            {
                RefreshRecycleBinPanes();
            }
        }

        auto remaining = outcome.remaining;
        bool discarded = false;
        if (outcome.gone_pair.has_value() &&
            co_await ConfirmDiscardHistoryStepAsync(BuildFileOperationErrorStatus(
                L"Couldn't redo " + entry.description, outcome.error_detail)))
        {
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(*outcome.gone_pair));
            discarded = true;
        }
        if (!remaining.empty())
        {
            m_undoStack.PushRedo(::FileScoutNative::Domain::UndoEntry{
                .kind = entry.kind,
                .description = entry.description,
                .items = std::move(remaining),
            });
        }

        if (!outcome.failed)
        {
            StatusTextBlock().Text(ToHString(L"Redid: " + entry.description));
        }
        else if (outcome.canceled)
        {
            StatusTextBlock().Text(outcome.completed.empty()
                ? L"Redo canceled."
                : ToHString(L"Redo partially completed (" + std::to_wstring(outcome.completed.size()) +
                            L" item(s)); remaining canceled."));
        }
        else
        {
            StatusTextBlock().Text(ToHString(
                BuildFileOperationErrorStatus(L"Couldn't redo " + entry.description,
                    discarded ? outcome.error_detail + L" Removed it from history." : outcome.error_detail)));
        }
    }

    winrt::fire_and_forget MainWindow::OpenMultipleItemsAsync(
        std::vector<::FileScoutNative::Domain::ShellItemRef> items)
    {
        if (items.empty())
        {
            co_return;
        }
        if (ActivePane().is_archive_view || ActivePane().is_recycle_bin_view)
        {
            StatusTextBlock().Text(L"Opening multiple items is not available in this view.");
            co_return;
        }

        auto lifetime = get_strong();
        if (::FileScoutNative::Domain::ShouldConfirmMultiOpen(items.size()))
        {
            if (m_modalDialogOpen)
            {
                co_return;
            }
            m_modalDialogOpen = true;
            auto dialog = winrt::Microsoft::UI::Xaml::Controls::ContentDialog{};
            dialog.XamlRoot(Content().XamlRoot());
            dialog.Title(winrt::box_value(L"Open Multiple Items"));
            dialog.PrimaryButtonText(L"Open");
            dialog.CloseButtonText(L"Cancel");
            dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
            dialog.Content(winrt::box_value(
                L"Are you sure you want to open " + std::to_wstring(items.size()) + L" items at once?"));

            winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult dialogResult =
                winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::None;
            try
            {
                dialogResult = co_await dialog.ShowAsync();
            }
            catch (...)
            {
                m_modalDialogOpen = false;
                FlushPendingWatchRefreshAllPanes();
                co_return;
            }
            m_modalDialogOpen = false;
            FlushPendingWatchRefreshAllPanes();

            if (dialogResult != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
            {
                StatusTextBlock().Text(L"Open canceled.");
                co_return;
            }
        }

        auto const owner = GetWindowHandle();
        std::size_t openedCount = 0;
        std::size_t failedCount = 0;
        for (auto const& item : items)
        {
            if (item.path.empty())
            {
                ++failedCount;
                continue;
            }
            if (item.is_folder)
            {
                AddTab(item.path);
                ++openedCount;
                continue;
            }
            try
            {
                m_shellFileOperationService.OpenPath(owner, item.path);
                ++openedCount;
            }
            catch (std::exception const&)
            {
                ++failedCount;
            }
        }

        if (failedCount == 0)
        {
            StatusTextBlock().Text(ToHString(
                L"Opened " + std::to_wstring(openedCount) +
                (openedCount == 1 ? L" item." : L" items.")));
        }
        else
        {
            StatusTextBlock().Text(ToHString(
                L"Opened " + std::to_wstring(openedCount) + L" item(s); " +
                std::to_wstring(failedCount) + L" failed."));
        }
    }

    winrt::fire_and_forget MainWindow::RestoreRecycleBinSelectionAsync(
        std::vector<::FileScoutNative::Domain::ShellItemRef> items)
    {
        if (items.empty())
        {
            co_return;
        }

        auto lifetime = get_strong();
        std::vector<std::wstring> parsingNames;
        parsingNames.reserve(items.size());
        for (auto const& item : items)
        {
            if (!item.parsing_name.empty())
            {
                parsingNames.push_back(item.parsing_name);
            }
        }
        if (parsingNames.empty())
        {
            StatusTextBlock().Text(L"Selected Recycle Bin items cannot be restored.");
            co_return;
        }

        auto const owner = GetWindowHandle();
        StatusTextBlock().Text(ToHString(
            L"Restoring " + std::to_wstring(parsingNames.size()) +
            (parsingNames.size() == 1 ? L" item from Recycle Bin..." : L" items from Recycle Bin...")));

        try
        {
            uint32_t const restoredCount = co_await OnShellThread([this, owner, parsingNames]()
                {
                    return m_shellFileOperationService.RestoreRecycleBinItems(owner, parsingNames);
                });
            RefreshRecycleBinPanes();
            StatusTextBlock().Text(ToHString(
                L"Restored " + std::to_wstring(restoredCount) +
                (restoredCount == 1 ? L" item from Recycle Bin." : L" items from Recycle Bin.")));
        }
        catch (::FileScoutNative::Shell::FileOperationException const& error)
        {
            StatusTextBlock().Text(ToHString(
                BuildFileOperationErrorStatus(L"Couldn't restore from Recycle Bin", error.Error().detail)));
        }
        catch (std::exception const& error)
        {
            StatusTextBlock().Text(ToHString(
                BuildFileOperationErrorStatus(L"Couldn't restore from Recycle Bin", winrt::to_hstring(error.what()).c_str())));
        }
    }

    winrt::fire_and_forget MainWindow::ShowEmptyRecycleBinDialogAsync()
    {
        if (m_modalDialogOpen)
        {
            co_return;
        }

        auto lifetime = get_strong();
        std::optional<uint64_t> itemCount = co_await OnShellThread([this]()
            {
                return m_shellFileOperationService.QueryRecycleBinItemCount();
            });
        if ((!itemCount.has_value() || *itemCount == 0) &&
            ActivePane().is_recycle_bin_view && !ActivePane().items.empty())
        {
            itemCount = ActivePane().items.size();
        }
        if (itemCount.has_value() && *itemCount == 0)
        {
            StatusTextBlock().Text(L"Recycle Bin is already empty.");
            co_return;
        }

        if (m_modalDialogOpen)
        {
            co_return;
        }
        m_modalDialogOpen = true;

        auto dialog = winrt::Microsoft::UI::Xaml::Controls::ContentDialog{};
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(L"Empty Recycle Bin"));
        dialog.PrimaryButtonText(L"Empty Recycle Bin");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        std::wstring const confirmMessage =
            L"Are you sure you want to permanently delete all items in the Recycle Bin? This cannot be undone.";
        dialog.Content(winrt::box_value(confirmMessage));

        winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult dialogResult =
            winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::None;
        try
        {
            dialogResult = co_await dialog.ShowAsync();
        }
        catch (...)
        {
            m_modalDialogOpen = false;
            FlushPendingWatchRefreshAllPanes();
            co_return;
        }
        m_modalDialogOpen = false;
        FlushPendingWatchRefreshAllPanes();

        if (dialogResult != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            StatusTextBlock().Text(L"Empty Recycle Bin canceled.");
            co_return;
        }

        auto const owner = GetWindowHandle();
        StatusTextBlock().Text(L"Emptying Recycle Bin...");
        try
        {
            bool const emptied = co_await OnShellThread([this, owner]()
                {
                    return m_shellFileOperationService.EmptyRecycleBin(owner);
                });
            RefreshRecycleBinPanes();
            StatusTextBlock().Text(emptied
                ? L"Recycle Bin emptied."
                : L"Recycle Bin is already empty.");
        }
        catch (::FileScoutNative::Shell::FileOperationException const& error)
        {
            StatusTextBlock().Text(ToHString(
                BuildFileOperationErrorStatus(L"Couldn't empty Recycle Bin", error.Error().detail)));
        }
        catch (std::exception const& error)
        {
            StatusTextBlock().Text(ToHString(
                BuildFileOperationErrorStatus(L"Couldn't empty Recycle Bin", winrt::to_hstring(error.what()).c_str())));
        }
    }

    winrt::fire_and_forget MainWindow::CompressSelectionToZipAsync(
        std::filesystem::path parentDirectory,
        std::vector<::FileScoutNative::Domain::ShellItemRef> items)
    {
        if (parentDirectory.empty() || items.empty())
        {
            co_return;
        }

        auto lifetime = get_strong();
        std::vector<std::filesystem::path> sourcePaths;
        sourcePaths.reserve(items.size());
        for (auto const& item : items)
        {
            if (!item.path.empty())
            {
                sourcePaths.push_back(item.path);
            }
        }
        if (sourcePaths.empty())
        {
            co_return;
        }

        auto const owner = GetWindowHandle();
        int32_t const activePaneIndex = m_activePaneIndex;
        StatusTextBlock().Text(ToHString(
            L"Compressing " + std::to_wstring(sourcePaths.size()) +
            (sourcePaths.size() == 1 ? L" item to ZIP..." : L" items to ZIP...")));
        ++m_panes[static_cast<std::size_t>(activePaneIndex)].background_op_progress_count;
        UpdatePaneProgressBar(activePaneIndex);

        // Run tar.exe on the thread pool rather than the shared Shell STA worker so a slow
        // archive does not block Paste, Undo, or other Shell operations.
        co_await winrt::resume_background();

        std::filesystem::path zipPath;
        std::wstring errorDetail;
        bool succeeded = false;
        try
        {
            zipPath = m_shellFileOperationService.CompressItemsToZip(
                owner, parentDirectory, sourcePaths);
            succeeded = true;
        }
        catch (::FileScoutNative::Shell::FileOperationException const& error)
        {
            errorDetail = error.Error().detail;
        }
        catch (std::exception const& error)
        {
            errorDetail = winrt::to_hstring(error.what()).c_str();
        }

        m_dispatcherQueue.TryEnqueue(
            [this, lifetime, activePaneIndex, parentDirectory, zipPath = std::move(zipPath), succeeded, errorDetail = std::move(errorDetail)]()
            {
                auto& paneRef = m_panes[static_cast<std::size_t>(activePaneIndex)];
                if (paneRef.background_op_progress_count > 0)
                {
                    --paneRef.background_op_progress_count;
                }
                UpdatePaneProgressBar(activePaneIndex);

                if (succeeded)
                {
                    RefreshPanesShowing(parentDirectory, zipPath);
                    StatusTextBlock().Text(ToHString(L"Created archive: " + zipPath.filename().wstring()));
                }
                else
                {
                    StatusTextBlock().Text(ToHString(
                        BuildFileOperationErrorStatus(L"Couldn't compress to ZIP", errorDetail)));
                }
            });
    }

    void MainWindow::CopySelectedPaths(std::vector<::FileScoutNative::Domain::ShellItemRef> const& selectedItems)
    {
        auto const& pane = ActivePane();
        auto const fallback = (pane.is_drives_view || pane.is_recycle_bin_view)
            ? std::filesystem::path{}
            : pane.current_path;
        auto const payload = ::FileScoutNative::Domain::FormatCopyPathsPayload(selectedItems, fallback);
        if (payload.empty())
        {
            StatusTextBlock().Text(L"No path available to copy.");
            return;
        }

        if (::FileScoutNative::Shell::ShellClipboardService::CopyTextToClipboard(GetWindowHandle(), payload))
        {
            if (selectedItems.empty())
            {
                StatusTextBlock().Text(L"Copied current folder path.");
            }
            else if (selectedItems.size() == 1)
            {
                StatusTextBlock().Text(ToHString(L"Copied path: " + selectedItems.front().path.wstring()));
            }
            else
            {
                StatusTextBlock().Text(ToHString(
                    L"Copied " + std::to_wstring(selectedItems.size()) + L" paths to clipboard."));
            }
        }
        else
        {
            StatusTextBlock().Text(L"Couldn't copy path to clipboard.");
        }
    }

    void MainWindow::OpenInTerminalForContext(std::optional<::FileScoutNative::Domain::ShellItemRef> const& item)
    {
        auto const& pane = ActivePane();
        std::filesystem::path targetDir;
        if (item.has_value() && item->is_folder && !item->path.empty() && !pane.is_archive_view && !pane.is_recycle_bin_view)
        {
            targetDir = item->path;
        }
        else if (!pane.is_drives_view && !pane.is_recycle_bin_view && !pane.is_archive_view && !pane.current_path.empty())
        {
            targetDir = pane.current_path;
        }
        else
        {
            StatusTextBlock().Text(L"Open in Terminal requires a filesystem folder.");
            return;
        }

        try
        {
            m_shellFileOperationService.OpenInTerminal(GetWindowHandle(), targetDir);
            StatusTextBlock().Text(ToHString(L"Opened terminal in " + targetDir.wstring()));
        }
        catch (::FileScoutNative::Shell::FileOperationException const& error)
        {
            std::wstring status = L"Couldn't open terminal in " + targetDir.wstring() + L".";
            if (!error.Error().detail.empty())
            {
                status += L" ";
                status += error.Error().detail;
            }
            StatusTextBlock().Text(ToHString(status));
        }
        catch (std::exception const& error)
        {
            StatusTextBlock().Text(ToHString(
                std::wstring{ L"Couldn't open terminal. " } + winrt::to_hstring(error.what()).c_str()));
        }
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

        {
            namespace D = ::FileScoutNative::Domain;
            static_assert(static_cast<std::uint32_t>(VKM::Control) == static_cast<std::uint32_t>(D::ShortcutModifiers::Control));
            static_assert(static_cast<std::uint32_t>(VKM::Menu) == static_cast<std::uint32_t>(D::ShortcutModifiers::Menu));
            static_assert(static_cast<std::uint32_t>(VKM::Shift) == static_cast<std::uint32_t>(D::ShortcutModifiers::Shift));
            static_assert(static_cast<std::uint32_t>(VK::Back) == D::Key::Back && static_cast<std::uint32_t>(VK::Tab) == D::Key::Tab &&
                static_cast<std::uint32_t>(VK::Enter) == D::Key::Enter && static_cast<std::uint32_t>(VK::PageUp) == D::Key::PageUp &&
                static_cast<std::uint32_t>(VK::PageDown) == D::Key::PageDown && static_cast<std::uint32_t>(VK::Left) == D::Key::Left &&
                static_cast<std::uint32_t>(VK::Up) == D::Key::Up && static_cast<std::uint32_t>(VK::Right) == D::Key::Right &&
                static_cast<std::uint32_t>(VK::Delete) == D::Key::Delete && static_cast<std::uint32_t>(VK::F2) == D::Key::F2 &&
                static_cast<std::uint32_t>(VK::F5) == D::Key::F5);
        }
        for (auto const& shortcut : ::FileScoutNative::Domain::kFileShortcuts)
        {
            AddAccelerator(
                static_cast<VK>(shortcut.key),
                static_cast<VKM>(shortcut.modifiers),
                std::wstring{ shortcut.command_id });
        }

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

    void MainWindow::SetupMouseNavigationHandlers()
    {
        auto const rootElement = Content().as<winrt::Microsoft::UI::Xaml::UIElement>();
        // WinUI 3 delivers client-area mouse XButton events to its internal InputSite child HWND
        // rather than the top-level HWND, so listen on the root Grid with handledEventsToo=true
        // to handle mouse Back/Forward buttons over any control in the window.
        rootElement.AddHandler(
            winrt::Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),
            winrt::box_value(winrt::Microsoft::UI::Xaml::Input::PointerEventHandler(
                [this](winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
                {
                    HandleRootPointerPressed(args);
                })),
            true);
        rootElement.AddHandler(
            winrt::Microsoft::UI::Xaml::UIElement::PointerReleasedEvent(),
            winrt::box_value(winrt::Microsoft::UI::Xaml::Input::PointerEventHandler(
                [this](winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
                {
                    HandleRootPointerReleased(args);
                })),
            true);
    }

    void MainWindow::HandleRootPointerPressed(
        winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        using PUK = winrt::Microsoft::UI::Input::PointerUpdateKind;
        auto const kind = args.GetCurrentPoint(nullptr).Properties().PointerUpdateKind();
        auto command = ::FileScoutNative::Domain::NavigationInputCommand::None;
        if (kind == PUK::XButton1Pressed)
        {
            command = ::FileScoutNative::Domain::NavigationInputCommand::Back;
        }
        else if (kind == PUK::XButton2Pressed)
        {
            command = ::FileScoutNative::Domain::NavigationInputCommand::Forward;
        }
        else
        {
            return;
        }

        m_mouseNavDeduplicator.OnPointerPressed(command);
        args.Handled(true);

        if (m_modalDialogOpen ||
            CommandPaletteOverlay().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            return;
        }

        if (command == ::FileScoutNative::Domain::NavigationInputCommand::Back)
        {
            NavigateBack();
        }
        else
        {
            NavigateForward();
        }
    }

    void MainWindow::HandleRootPointerReleased(
        winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        using PUK = winrt::Microsoft::UI::Input::PointerUpdateKind;
        auto const kind = args.GetCurrentPoint(nullptr).Properties().PointerUpdateKind();
        if (kind == PUK::XButton1Released)
        {
            m_mouseNavDeduplicator.OnPointerReleased(
                ::FileScoutNative::Domain::NavigationInputCommand::Back,
                ::GetTickCount64());
            args.Handled(true);
        }
        else if (kind == PUK::XButton2Released)
        {
            m_mouseNavDeduplicator.OnPointerReleased(
                ::FileScoutNative::Domain::NavigationInputCommand::Forward,
                ::GetTickCount64());
            args.Handled(true);
        }
    }

    bool MainWindow::ShouldTextInputOwnFileShortcut(std::wstring_view const commandId)
    {
        if (!::FileScoutNative::Domain::IsFileItemCommand(commandId))
        {
            return false;
        }

        return ::FileScoutNative::Domain::ShouldTextInputOwnShortcut(commandId, FocusIsInEditableControl());
    }

    bool MainWindow::FocusIsInEditableControl()
    {
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

    void MainWindow::UpButton_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        NavigateUp();
    }

    void MainWindow::NavigateUp()
    {
        auto const& pane = ActivePane();
        auto const target = ::FileScoutNative::Domain::ResolveParentNavigationTarget(
            pane.current_path,
            pane.is_drives_view,
            pane.is_recycle_bin_view);
        if (target.kind == ::FileScoutNative::Domain::ParentNavigationKind::Folder)
        {
            LoadDirectory(target.path);
        }
        else if (target.kind == ::FileScoutNative::Domain::ParentNavigationKind::DrivesView)
        {
            LoadDrivesView();
        }
        else
        {
            StatusTextBlock().Text(L"Already at the top level.");
            UpdateNavButtons();
        }
    }

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
        auto const currentHistoryEntry = pane.is_drives_view
            ? std::filesystem::path{ ::FileScoutNative::Domain::kDrivesViewLocation }
            : pane.current_path;
        if (!currentHistoryEntry.empty())
        {
            pane.forward_stack.push_back(currentHistoryEntry);
        }
        UpdateNavButtons();
        if (::FileScoutNative::Domain::IsDrivesViewLocation(prev))
        {
            LoadDrivesView(true);
        }
        else
        {
            LoadDirectory(prev, std::nullopt, true);
        }
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
        auto const currentHistoryEntry = pane.is_drives_view
            ? std::filesystem::path{ ::FileScoutNative::Domain::kDrivesViewLocation }
            : pane.current_path;
        if (!currentHistoryEntry.empty())
        {
            pane.back_stack.push_back(currentHistoryEntry);
        }
        UpdateNavButtons();
        if (::FileScoutNative::Domain::IsDrivesViewLocation(next))
        {
            LoadDrivesView(true);
        }
        else
        {
            LoadDirectory(next, std::nullopt, true);
        }
    }

    void MainWindow::UpdateNavButtons()
    {
        auto const& pane = ActivePane();
        BackButton().IsEnabled(!pane.back_stack.empty());
        ForwardButton().IsEnabled(!pane.forward_stack.empty());
        auto const parentTarget = ::FileScoutNative::Domain::ResolveParentNavigationTarget(
            pane.current_path,
            pane.is_drives_view,
            pane.is_recycle_bin_view);
        UpButton().IsEnabled(parentTarget.kind != ::FileScoutNative::Domain::ParentNavigationKind::None);
        UpdateTitleBarInteractiveRegions();
    }

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

    winrt::fire_and_forget MainWindow::OpenArchiveItemAsync(::FileScoutNative::Domain::ShellItemRef item)
    {
        auto lifetime = get_strong();
        auto const ownerWindow = GetWindowHandle();

        StatusTextBlock().Text(ToHString(L"Extracting " + item.display_name + L"..."));

        co_await winrt::resume_background();

        std::filesystem::path extractedPath;
        std::wstring errorText;

        std::optional<::FileScoutNative::Shell::ComApartment> apartment{ std::in_place };
        if (!apartment->IsUsable())
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

        apartment.reset();

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

        std::optional<::FileScoutNative::Shell::ComApartment> apartment{ std::in_place };
        if (!apartment->IsUsable())
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

        apartment.reset();

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

        try
        {
            auto const rootGrid = Content().as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
            ApplyRowPresentation(listView, rootGrid, m_panes[paneIndex]);
        }
        catch (...) {}

        listView.ContainerContentChanging([this, paneIndex](
            auto const& sender,
            winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args)
            {
                if (args.InRecycleQueue())
                {
                    return;
                }
                auto const& pane = m_panes[paneIndex];
                if (ResolveRowPresentation(pane) != RowPresentation::List)
                {
                    return;
                }
                ApplyListColumnWidthsToContainer(
                    args.ItemContainer(),
                    pane.list_column_widths,
                    pane.sort_header_toggle_width);
                if (pane.list_header_offset_unreconciled &&
                    pane.active_list_view &&
                    sender.try_as<winrt::Microsoft::UI::Xaml::Controls::ListView>() == pane.active_list_view &&
                    pane.active_list_view.Items().Size() > 0)
                {
                    SyncListHeaderHorizontalOffset(paneIndex);
                }
            });

        listView.SelectionChanged([this, paneIndex](auto const& s, auto const& a)
            {
                if (m_panes[paneIndex].tab_switching) return;
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

        listView.AddHandler(
            winrt::Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),
            winrt::box_value(winrt::Microsoft::UI::Xaml::Input::PointerEventHandler(
                [this, paneIndex](auto const&, auto const& a)
                {
                    TryHandleListViewMiddleClick(paneIndex, a);
                })),
            true);

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

        listView.CharacterReceived([this, paneIndex](auto const&, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                FileRowsListView_CharacterReceived(a);
            });

        listView.Loaded([this, paneIndex](auto const& sender, auto const&)
            {
                auto const list = sender.as<winrt::Microsoft::UI::Xaml::Controls::ListView>();
                if (auto const scrollViewer =
                        FindDescendantByType<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>(list))
                {
                    auto const& pane = m_panes[paneIndex];
                    bool const isListMode = (ResolveRowPresentation(pane) == RowPresentation::List);
                    scrollViewer.Background(SolidBrush(0x00, 0x00, 0x00, 0x00));
                    scrollViewer.IsRightTapEnabled(true);
                    scrollViewer.VerticalScrollBarVisibility(
                        winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Visible);
                    scrollViewer.HorizontalScrollMode(isListMode
                        ? winrt::Microsoft::UI::Xaml::Controls::ScrollMode::Auto
                        : winrt::Microsoft::UI::Xaml::Controls::ScrollMode::Disabled);
                    scrollViewer.HorizontalScrollBarVisibility(isListMode
                        ? winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Auto
                        : winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Disabled);

                    if (pane.active_list_view == list)
                    {
                        SyncListHeaderHorizontalOffset(paneIndex);
                    }

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
                    auto weakList = winrt::make_weak(list);
                    scrollViewer.ViewChanged([this, paneIndex, weakList](
                        auto const& s,
                        winrt::Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const&)
                        {
                            auto const ownerList = weakList.get();
                            if (!ownerList)
                            {
                                return;
                            }
                            auto& curPane = m_panes[paneIndex];
                            if (curPane.active_list_view != ownerList || !curPane.sort_header_scroll_viewer)
                            {
                                return;
                            }
                            if (ownerList.Items().Size() == 0)
                            {
                                curPane.list_header_offset_unreconciled = true;
                                return;
                            }
                            curPane.list_header_offset_unreconciled = false;
                            auto const sv = s.try_as<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>();
                            if (!sv)
                            {
                                return;
                            }
                            double const targetOffset = sv.HorizontalOffset();
                            if (std::abs(curPane.sort_header_scroll_viewer.HorizontalOffset() - targetOffset) > 0.5)
                            {
                                curPane.sort_header_scroll_viewer.ChangeView(
                                    winrt::box_value(targetOffset).as<winrt::Windows::Foundation::IReference<double>>(),
                                    nullptr,
                                    nullptr,
                                    true);
                            }
                        });
                }
            });

        return listView;
    }

    void MainWindow::FileRowsListView_KeyDown(
        winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        auto const action = ::FileScoutNative::Domain::ResolveListKeyAction(
            static_cast<std::uint32_t>(args.Key()), HasSystemModifierChord());
        if (action == ::FileScoutNative::Domain::ListKeyAction::None)
        {
            return;
        }

        if (action == ::FileScoutNative::Domain::ListKeyAction::ActivateSelection)
        {
            if (ActivateCurrentSelection())
            {
                args.Handled(true);
            }
            return;
        }

        // Type-ahead runs from CharacterReceived, which gets the typed character for any keyboard
        // layout instead of a virtual key. Text committed by an IME does not raise
        // CharacterReceived, so IME type-ahead is not supported.
    }

    void MainWindow::FileRowsListView_CharacterReceived(
        winrt::Microsoft::UI::Xaml::Input::CharacterReceivedRoutedEventArgs const& args)
    {
        auto& pane = ActivePane();
        auto const nowMs = ::GetTickCount64();
        auto const ch = args.Character();
        if (!::FileScoutNative::Domain::AcceptsTypeAheadCharacter(
                ch, ::FileScoutNative::Domain::IsTypeAheadIdle(pane.type_ahead, nowMs)))
        {
            return;
        }

        auto const& displayed = DisplayedItems();
        if (displayed.empty())
        {
            return;
        }

        auto const query = ::FileScoutNative::Domain::AppendTypeAheadCharacter(pane.type_ahead, ch, nowMs);
        // Search from the focused row (Ctrl+arrows move focus without selecting in Extended mode);
        // fall back to the selection.
        int32_t start = -1;
        if (pane.active_list_view && pane.active_list_view.XamlRoot())
        {
            auto const focusedElement = winrt::Microsoft::UI::Xaml::Input::FocusManager::GetFocusedElement(
                pane.active_list_view.XamlRoot());
            if (auto const container = focusedElement.try_as<winrt::Microsoft::UI::Xaml::Controls::ListViewItem>())
            {
                start = pane.active_list_view.IndexFromContainer(container);
            }
            if (start < 0)
            {
                start = pane.active_list_view.SelectedIndex();
            }
        }
        auto const focused = start >= 0
            ? std::optional<std::size_t>{ static_cast<std::size_t>(start) }
            : std::nullopt;
        std::size_t const published = pane.active_tab_index >= 0 &&
                pane.active_tab_index < static_cast<int32_t>(pane.tab_rows.size())
            ? (std::min)(displayed.size(), static_cast<std::size_t>(pane.tab_rows[pane.active_tab_index].Size()))
            : displayed.size();
        auto const match = ::FileScoutNative::Domain::FindTypeAheadMatch(
            published,
            [&](std::size_t const i) -> std::wstring_view { return displayed[i].display_name; },
            focused,
            query);
        args.Handled(true);
        if (!match.has_value())
        {
            StatusTextBlock().Text(ToHString(L"No item starts with \u201C" + pane.type_ahead.text + L"\u201D"));
            return;
        }

        auto const idx = static_cast<int32_t>(*match);
        auto const displayName = displayed[*match].display_name;
        SelectItemByIndex(idx);
        if (pane.active_list_view)
        {
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
                pane.active_list_view.Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
            }
        }
        StatusTextBlock().Text(ToHString(L"\u2192 " + displayName));
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
        auto const selectedItems = GetSelectedItems();
        if (selectedItems.size() > 1 && !ActivePane().is_archive_view && !ActivePane().is_recycle_bin_view)
        {
            OpenMultipleItemsAsync(selectedItems);
            return true;
        }

        auto const& listView = ActivePane().active_list_view;
        int32_t index = -1;
        if (selectedItems.size() > 1 && listView && listView.XamlRoot())
        {
            if (auto const container = winrt::Microsoft::UI::Xaml::Input::FocusManager::GetFocusedElement(
                    listView.XamlRoot()).try_as<winrt::Microsoft::UI::Xaml::Controls::ListViewItem>())
            {
                index = listView.IndexFromContainer(container);
            }
        }
        if (index < 0 && listView)
        {
            index = listView.SelectedIndex();
        }
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
            pane.tabs[pane.active_tab_index].is_recycle_bin_view = pane.is_recycle_bin_view;
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

    void MainWindow::AddTabForDrivesView()
    {
        auto& pane = ActivePane();
        auto tabView = ActiveTabView();

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
            pane.tabs[pane.active_tab_index].is_recycle_bin_view = pane.is_recycle_bin_view;
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
        tabItem.Header(winrt::box_value(winrt::hstring{ L"This PC" }));
        tabItem.Content(paneSurfaceHost);
        tabView.TabItems().Append(tabItem);

        pane.active_tab_index = newIndex;
        pane.active_list_view = newListView;

        pane.tab_switching = true;
        tabView.SelectedIndex(pane.active_tab_index);
        pane.tab_switching = false;

        UpdateTabCloseability();
        LoadDrivesView(true);
    }

    void MainWindow::CloseTab(int32_t const index)
    {
        auto& pane = ActivePane();
        auto tabView = ActiveTabView();

        if (pane.tabs.size() <= 1 || index < 0 || index >= static_cast<int32_t>(pane.tabs.size()))
        {
            return;
        }

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
            pane.tabs[pane.active_tab_index].is_recycle_bin_view = pane.is_recycle_bin_view;
        }

        auto const& closingTab = pane.tabs[static_cast<std::size_t>(index)];
        if (closingTab.is_drives_view || !closingTab.path.empty())
        {
            static constexpr std::size_t kMaxClosedTabs = 20;
            pane.closed_tabs.push_back(PaneContext::ClosedTabEntry{
                .path = closingTab.path,
                .is_drives_view = closingTab.is_drives_view,
            });
            if (pane.closed_tabs.size() > kMaxClosedTabs)
            {
                pane.closed_tabs.erase(pane.closed_tabs.begin());
            }
        }

        pane.tab_switching = true;
        pane.tabs.erase(pane.tabs.begin() + index);
        pane.tab_rows.erase(pane.tab_rows.begin() + index);
        tabView.TabItems().RemoveAt(static_cast<uint32_t>(index));
        pane.tab_switching = false;

        if (pane.active_tab_index >= static_cast<int32_t>(pane.tabs.size()))
        {
            pane.active_tab_index = static_cast<int32_t>(pane.tabs.size()) - 1;
        }

        tabView.SelectedIndex(pane.active_tab_index);

        UpdateTabCloseability();
    }

    void MainWindow::ReopenClosedTab()
    {
        auto& pane = ActivePane();
        if (pane.closed_tabs.empty())
        {
            StatusTextBlock().Text(L"No closed tabs to reopen.");
            return;
        }

        auto const entry = pane.closed_tabs.back();
        pane.closed_tabs.pop_back();
        if (entry.is_drives_view)
        {
            AddTabForDrivesView();
        }
        else
        {
            AddTab(entry.path);
        }
    }

    void MainWindow::SwitchToAdjacentTab(int32_t const delta)
    {
        auto const& pane = ActivePane();
        auto const count = static_cast<int32_t>(pane.tabs.size());
        if (count <= 1)
        {
            return;
        }

        int32_t const nextIndex = ((pane.active_tab_index + delta) % count + count) % count;
        ActiveTabView().SelectedIndex(nextIndex);
    }

    void MainWindow::SelectTabByNumber(int32_t const oneBasedIndex)
    {
        auto const& pane = ActivePane();
        auto const count = static_cast<int32_t>(pane.tabs.size());
        if (count <= 0 || oneBasedIndex < 1)
        {
            return;
        }

        int32_t const targetIndex = (oneBasedIndex == 9)
            ? (count - 1)
            : (oneBasedIndex - 1);
        if (targetIndex >= 0 && targetIndex < count && targetIndex != pane.active_tab_index)
        {
            ActiveTabView().SelectedIndex(targetIndex);
        }
    }

    void MainWindow::TryHandleListViewMiddleClick(
        int32_t const paneIndex,
        winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto const kind = args.GetCurrentPoint(nullptr).Properties().PointerUpdateKind();
        if (kind != winrt::Microsoft::UI::Input::PointerUpdateKind::MiddleButtonPressed)
        {
            return;
        }

        SetActivePaneIndex(paneIndex);
        auto const itemIndex = ResolveItemIndexFromSource(args.OriginalSource());
        if (!itemIndex.has_value())
        {
            return;
        }

        auto const* item = TryGetDisplayedItemByIndex(*itemIndex);
        if (item == nullptr || !item->is_folder || item->path.empty() ||
            ActivePane().is_archive_view || ActivePane().is_recycle_bin_view)
        {
            return;
        }

        args.Handled(true);
        AddTab(item->path);
    }

    void MainWindow::RestoreActiveTab()
    {
        auto& pane = ActivePane();
        auto tabView = ActiveTabView();
        ::FileScoutNative::Domain::ResetTypeAhead(pane.type_ahead);

        if (pane.active_tab_index < 0 ||
            pane.active_tab_index >= static_cast<int32_t>(pane.tabs.size()))
        {
            return;
        }

        auto const tabItemObj = tabView.TabItems().GetAt(static_cast<uint32_t>(pane.active_tab_index));
        auto const tabItem = tabItemObj.as<winrt::Microsoft::UI::Xaml::Controls::TabViewItem>();
        pane.active_list_view = ResolveTabContentListView(tabItem.Content());
        if (pane.active_list_view)
        {
            try
            {
                auto const rootGrid = Content().as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
                ApplyRowPresentation(pane.active_list_view, rootGrid, pane);
            }
            catch (...) {}
        }
        ApplyListColumnWidthsToPane(m_activePaneIndex);
        SyncListHeaderHorizontalOffset(m_activePaneIndex);

        ++pane.load_generation;
        pane.awaiting_final_batch = false;

        auto const& tab = pane.tabs[pane.active_tab_index];
        pane.current_path = tab.path;
        pane.is_drives_view = tab.is_drives_view;
        pane.is_archive_view = tab.is_archive_view;
        pane.is_recycle_bin_view =
            tab.is_recycle_bin_view || ::FileScoutNative::Domain::IsRecycleBinLocation(tab.path);
        pane.items = tab.items;
        pane.selection = tab.selection;
        UpdatePaneDirectoryWatcher(m_activePaneIndex);

        if (tab.is_drives_view)
        {
            BreadcrumbPanel().Children().Clear();
            UpdateNavButtons();
        }
        else
        {
            UpdateBreadcrumbs(pane.current_path);
            UpdateSearchPlaceholder(pane.current_path);
        }

        if (pane.items.empty() && !tab.path.empty() && !tab.is_drives_view)
        {
            LoadDirectory(tab.path);
            return;
        }

        SortItems(pane.items, pane.sort_column, pane.sort_direction);
        auto& rows = pane.tab_rows[pane.active_tab_index];
        rows.Clear();
        std::wstring previousGroupLabel;
        for (auto const& item : pane.items)
        {
            auto vm = MakeFileRowViewModel(item);
            auto impl = winrt::get_self<winrt::FileScoutNative::implementation::FileRowViewModel>(vm);
            if (!pane.show_file_extensions && !item.is_folder)
            {
                auto const dotPos = item.display_name.rfind(L'.');
                if (dotPos != std::wstring_view::npos && dotPos > 0)
                {
                    impl->SetName(item.display_name.substr(0, dotPos));
                }
            }
            if (pane.group_by_sort_column)
            {
                auto const groupLabel = BuildGroupLabel(item, pane.sort_column);
                bool const showGroupHeader = previousGroupLabel != groupLabel;
                impl->SetGroupHeader(groupLabel, showGroupHeader);
                previousGroupLabel = groupLabel;
            }
            rows.Append(vm);
        }

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

        if (::FileScoutNative::Domain::ShouldWatchLocation(
                pane.current_path, pane.is_drives_view, pane.is_archive_view, pane.is_recycle_bin_view))
        {
            if (pane.watch_armed)
            {
                pane.watch_refresh_pending = true;
                FlushPendingWatchRefresh(m_activePaneIndex);
            }
            else if (pane.directory_watcher && pane.directory_watcher->IsWatching())
            {
                pane.watch_reconcile_on_arm = true;
            }
            else
            {
                pane.watch_refresh_pending = true;
                FlushPendingWatchRefresh(m_activePaneIndex);
            }
        }
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

        if (paneIndex == 1 && !m_dualPaneVisible)
        {
            m_deferredRightPanePath = startPath;
            return;
        }

        LoadDirectoryInPane(paneIndex, startPath);
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

        EndSearchOnNavigation(m_activePaneIndex);

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
        SearchTextBox().IsReadOnly(ActivePane().search_mode == SearchMode::Recursive);
        SearchTextBox().Text(winrt::hstring(ActivePane().search_query));
        UpdateNavButtons();
        RefreshViewOptionsDropDown();
    }

    void MainWindow::UpdateActivePaneIndicator()
    {
        auto const activeBrush = LookupBrush(L"AccentFillColorDefaultBrush");
        auto const inactiveBrush = LookupBrush(L"CardStrokeColorDefaultBrush");

        if (!m_dualPaneVisible)
        {
            LeftPaneBorder().BorderBrush(inactiveBrush);
            LeftPaneBorder().BorderThickness({ 1, 1, 1, 1 });
            RightPaneBorder().BorderBrush(inactiveBrush);
            RightPaneBorder().BorderThickness({ 1, 1, 1, 1 });
            return;
        }

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

    void MainWindow::UpdatePaneProgressBar(int32_t const paneIndex)
    {
        if (paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }
        auto const& pane = m_panes[static_cast<std::size_t>(paneIndex)];
        bool const visible = pane.recursive_search_running || pane.background_op_progress_count > 0;
        SearchProgressForPane(paneIndex).Visibility(
            visible
                ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    void MainWindow::UpdateBreadcrumbs(std::filesystem::path const& path)
    {
        auto const panel = BreadcrumbPanel();
        if (!path.empty() && !m_lastBreadcrumbPath.empty() &&
            SidebarPathsEqual(path, m_lastBreadcrumbPath) &&
            panel.Children().Size() > 0)
        {
            UpdateNavButtons();
            return;
        }

        m_lastBreadcrumbPath = path;
        panel.Children().Clear();

        if (path.empty()) return;

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
                ? seg.root_name().wstring()
                : seg.filename().wstring();
            if (label.empty()) label = seg.wstring();

            if (i > 0)
            {
                MUI::TextBlock sep;
                sep.Text(L"\u203A");
                sep.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
                sep.Margin({ 2, 0, 2, 0 });
                sep.Foreground(LookupBrush(L"TextFillColorSecondaryBrush"));
                sep.Opacity(0.65);
                sep.FontSize(12);
                panel.Children().Append(sep);
            }

            MUI::Button btn;
            btn.Background(SolidBrush(0x00, 0x00, 0x00, 0x00));
            btn.BorderThickness({ 0, 0, 0, 0 });
            btn.Padding({ 6, 2, 6, 2 });
            btn.MinHeight(24);
            btn.MinWidth(0);
            btn.CornerRadius({ 4, 4, 4, 4 });
            btn.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
            MUI::TextBlock lbl;
            lbl.Text(winrt::hstring(label));
            lbl.FontSize(13);
            lbl.Foreground(LookupBrush(L"TextFillColorPrimaryBrush"));
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
        bool const wasVisible =
            AddressTextBox().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible;
        AddressTextBox().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        BreadcrumbScrollViewer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        if (wasVisible)
        {
            FlushPendingWatchRefresh(m_activePaneIndex);
        }
    }

    void MainWindow::UpdateSearchPlaceholder(std::filesystem::path const& path)
    {
        auto const folderName = path.filename().empty()
            ? path.wstring()
            : path.filename().wstring();
        SearchTextBox().PlaceholderText(L"Search " + folderName + L"\u2026");
    }

    void MainWindow::ApplyListColumnWidthsToPane(int32_t const paneIndex)
    {
        auto& pane = m_panes[paneIndex];
        auto const& widths = pane.list_column_widths;

        if (ResolveRowPresentation(pane) != RowPresentation::List)
        {
            return;
        }

        static constexpr std::array<::FileScoutNative::Domain::ListColumnIndex, 4> kCols{{
            ::FileScoutNative::Domain::ListColumnIndex::Name,
            ::FileScoutNative::Domain::ListColumnIndex::Type,
            ::FileScoutNative::Domain::ListColumnIndex::Size,
            ::FileScoutNative::Domain::ListColumnIndex::DateModified,
        }};
        for (std::size_t i = 0; i < kCols.size(); ++i)
        {
            if (pane.sort_header_columns[i])
            {
                pane.sort_header_columns[i].Width({
                    ::FileScoutNative::Domain::GetListColumnWidth(widths, kCols[i]),
                    winrt::Microsoft::UI::Xaml::GridUnitType::Pixel,
                });
            }
        }

        if (!pane.active_list_view)
        {
            return;
        }
        auto const itemCount = static_cast<int32_t>(pane.active_list_view.Items().Size());
        if (itemCount <= 0)
        {
            return;
        }
        int32_t firstIndex = 0;
        int32_t lastIndex = (std::min)(itemCount - 1, 63);
        if (auto const stackPanel = pane.active_list_view.ItemsPanelRoot()
                .try_as<winrt::Microsoft::UI::Xaml::Controls::ItemsStackPanel>())
        {
            auto const cacheFirst = stackPanel.FirstCacheIndex();
            auto const cacheLast = stackPanel.LastCacheIndex();
            if (cacheFirst >= 0 && cacheLast >= cacheFirst)
            {
                firstIndex = cacheFirst;
                lastIndex = (std::min)(cacheLast, itemCount - 1);
            }
        }
        for (int32_t i = firstIndex; i <= lastIndex; ++i)
        {
            if (auto const container = pane.active_list_view.ContainerFromIndex(i))
            {
                ApplyListColumnWidthsToContainer(container, widths, pane.sort_header_toggle_width);
            }
        }
    }

    void MainWindow::SyncListHeaderHorizontalOffset(int32_t const paneIndex)
    {
        if (paneIndex < 0 || paneIndex >= static_cast<int32_t>(m_panes.size()))
        {
            return;
        }
        auto& pane = m_panes[paneIndex];
        if (!pane.sort_header_scroll_viewer || !pane.active_list_view)
        {
            return;
        }
        if (pane.active_list_view.Items().Size() == 0)
        {
            pane.list_header_offset_unreconciled = true;
            return;
        }
        pane.list_header_offset_unreconciled = false;
        if (auto const listSv =
                FindDescendantByType<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>(pane.active_list_view))
        {
            double const offset = listSv.HorizontalOffset();
            if (std::abs(pane.sort_header_scroll_viewer.HorizontalOffset() - offset) > 0.5)
            {
                pane.sort_header_scroll_viewer.ChangeView(
                    winrt::box_value(offset).as<winrt::Windows::Foundation::IReference<double>>(),
                    nullptr,
                    nullptr,
                    true);
            }
        }
    }

    void MainWindow::BuildSortHeader(int32_t const paneIndex)
    {
        struct ColumnDef
        {
            std::wstring_view label;
            SortColumn column;
            ::FileScoutNative::Domain::ListColumnIndex listIndex;
            double widthStar;
            bool rightAlign;
        };
        static constexpr std::array<ColumnDef, 4> columns{{
            { L"Name",          SortColumn::Name,         ::FileScoutNative::Domain::ListColumnIndex::Name,         3.4, false },
            { L"Type",          SortColumn::Type,         ::FileScoutNative::Domain::ListColumnIndex::Type,         1.4, false },
            { L"Size",          SortColumn::Size,         ::FileScoutNative::Domain::ListColumnIndex::Size,         1.0, true  },
            { L"Date Modified", SortColumn::DateModified, ::FileScoutNative::Domain::ListColumnIndex::DateModified, 2.4, false },
        }};

        auto headerBorder = SortHeaderForPane(paneIndex);
        auto& pane = m_panes[paneIndex];
        auto const presentation = ResolveRowPresentation(pane);
        bool const isListMode = (presentation == RowPresentation::List);
        pane.sort_header_columns = { nullptr, nullptr, nullptr, nullptr };
        pane.sort_header_scroll_viewer = nullptr;

        auto outerGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
        auto colsHostDef = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
        colsHostDef.Width({ 1.0, winrt::Microsoft::UI::Xaml::GridUnitType::Star });
        auto toggleHostDef = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
        toggleHostDef.Width({ 0.0, winrt::Microsoft::UI::Xaml::GridUnitType::Auto });
        outerGrid.ColumnDefinitions().Append(colsHostDef);
        outerGrid.ColumnDefinitions().Append(toggleHostDef);

        auto columnsGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
        columnsGrid.Margin({ 8, 0, 0, 0 });
        int32_t colOffset = 0;
        if (isListMode)
        {
            columnsGrid.ColumnSpacing(8);
            auto iconSpacerCol = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            iconSpacerCol.Width({ 20.0, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
            columnsGrid.ColumnDefinitions().Append(iconSpacerCol);
            colOffset = 1;
        }

        for (std::size_t idx = 0; idx < columns.size(); ++idx)
        {
            auto colDef = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            if (isListMode)
            {
                colDef.Width({
                    ::FileScoutNative::Domain::GetListColumnWidth(pane.list_column_widths, columns[idx].listIndex),
                    winrt::Microsoft::UI::Xaml::GridUnitType::Pixel,
                });
                pane.sort_header_columns[idx] = colDef;
            }
            else
            {
                colDef.Width({ columns[idx].widthStar, winrt::Microsoft::UI::Xaml::GridUnitType::Star });
            }
            columnsGrid.ColumnDefinitions().Append(colDef);
        }

        if (isListMode)
        {
            auto trailingCol = winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition{};
            trailingCol.Width({ 42.0, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
            columnsGrid.ColumnDefinitions().Append(trailingCol);
        }

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
            btn.HorizontalContentAlignment((isListMode && colDef.rightAlign)
                ? winrt::Microsoft::UI::Xaml::HorizontalAlignment::Right
                : winrt::Microsoft::UI::Xaml::HorizontalAlignment::Left);
            btn.Padding(isListMode
                ? (colDef.rightAlign
                    ? winrt::Microsoft::UI::Xaml::Thickness{ 4, 5, 8, 5 }
                    : winrt::Microsoft::UI::Xaml::Thickness{ 0, 5, 8, 5 })
                : winrt::Microsoft::UI::Xaml::Thickness{ 6, 5, 6, 5 });
            btn.FontSize(12);
            btn.FontWeight(isActive
                ? winrt::Microsoft::UI::Text::FontWeights::SemiBold()
                : winrt::Microsoft::UI::Text::FontWeights::Normal());

            if (isActive)
            {
                btn.Foreground(LookupBrush(L"AccentTextFillColorPrimaryBrush"));
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

            if (isListMode)
            {
                auto cellGrid = winrt::Microsoft::UI::Xaml::Controls::Grid{};
                cellGrid.Children().Append(btn);

                auto grip = winrt::Microsoft::UI::Xaml::Controls::Grid{};
                grip.Width(8.0);
                grip.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Right);
                grip.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Stretch);
                grip.Margin({ 0, 0, -4, 0 });
                grip.Background(SolidBrush(0x00, 0x00, 0x00, 0x00));
                winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                    grip, winrt::box_value(winrt::hstring{ L"Drag to resize column (double-click to reset)" }));

                auto hairline = winrt::Microsoft::UI::Xaml::Controls::Border{};
                hairline.Width(1.0);
                hairline.Height(14.0);
                hairline.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
                hairline.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
                hairline.Background(LookupBrush(L"DividerStrokeColorDefaultBrush"));
                hairline.Opacity(0.75);
                grip.Children().Append(hairline);

                struct ResizeDragState
                {
                    bool dragging{ false };
                    POINT startScreenPt{};
                    ::FileScoutNative::Domain::ListColumnWidths startWidths{};
                };
                auto dragState = std::make_shared<ResizeDragState>();
                auto weakHairline = winrt::make_weak(hairline);
                auto const listColIdx = colDef.listIndex;

                // Avoid capturing grip or hairline strongly inside grip's own event delegates so
                // replaced sort headers are released cleanly without C++/WinRT ownership cycles.
                grip.PointerEntered([weakHairline](auto const&, auto const&)
                    {
                        if (auto const line = weakHairline.get())
                        {
                            line.Background(LookupBrush(L"TextFillColorSecondaryBrush"));
                            line.Opacity(1.0);
                        }
                        ::SetCursor(::LoadCursorW(nullptr, IDC_SIZEWE));
                    });
                grip.PointerExited([weakHairline, dragState](auto const&, auto const&)
                    {
                        if (!dragState->dragging)
                        {
                            if (auto const line = weakHairline.get())
                            {
                                line.Background(LookupBrush(L"DividerStrokeColorDefaultBrush"));
                                line.Opacity(0.75);
                            }
                        }
                    });
                grip.PointerPressed([this, paneIndex, listColIdx, weakHairline, dragState](
                    auto const& sender,
                    winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
                    {
                        auto const gripEl = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
                        if (!gripEl)
                        {
                            return;
                        }
                        auto const props = args.GetCurrentPoint(gripEl).Properties();
                        if (!props.IsLeftButtonPressed())
                        {
                            return;
                        }
                        SetActivePaneIndex(paneIndex);
                        if (::GetCursorPos(&dragState->startScreenPt) == FALSE)
                        {
                            return;
                        }
                        dragState->startWidths = m_panes[paneIndex].list_column_widths;
                        dragState->dragging = true;
                        (void)gripEl.CapturePointer(args.Pointer());
                        if (auto const line = weakHairline.get())
                        {
                            line.Background(LookupBrush(L"AccentTextFillColorPrimaryBrush"));
                            line.Opacity(1.0);
                        }
                        ::SetCursor(::LoadCursorW(nullptr, IDC_SIZEWE));
                        args.Handled(true);
                    });
                grip.PointerMoved([this, paneIndex, listColIdx, dragState](
                    auto const& sender,
                    winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
                    {
                        ::SetCursor(::LoadCursorW(nullptr, IDC_SIZEWE));
                        if (!dragState->dragging)
                        {
                            return;
                        }
                        auto const gripEl = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
                        if (!gripEl)
                        {
                            return;
                        }
                        POINT curPt{};
                        if (::GetCursorPos(&curPt) == FALSE)
                        {
                            return;
                        }
                        double scale = 1.0;
                        if (auto const xamlRoot = gripEl.XamlRoot())
                        {
                            auto const s = xamlRoot.RasterizationScale();
                            if (s > 0.0) { scale = s; }
                        }
                        double const deltaDip = static_cast<double>(curPt.x - dragState->startScreenPt.x) / scale;
                        m_panes[paneIndex].list_column_widths =
                            ::FileScoutNative::Domain::ResizeListColumn(dragState->startWidths, listColIdx, deltaDip);
                        ApplyListColumnWidthsToPane(paneIndex);
                        args.Handled(true);
                    });
                grip.PointerReleased([this, paneIndex, weakHairline, dragState](
                    auto const& sender,
                    winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
                    {
                        if (!dragState->dragging)
                        {
                            return;
                        }
                        dragState->dragging = false;
                        if (auto const gripEl = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Grid>())
                        {
                            gripEl.ReleasePointerCapture(args.Pointer());
                        }
                        if (auto const line = weakHairline.get())
                        {
                            line.Background(LookupBrush(L"DividerStrokeColorDefaultBrush"));
                            line.Opacity(0.75);
                        }
                        SavePaneColumnWidths(paneIndex);
                        args.Handled(true);
                    });
                grip.PointerCaptureLost([this, paneIndex, weakHairline, dragState](auto const&, auto const&)
                    {
                        bool const wasDragging = dragState->dragging;
                        dragState->dragging = false;
                        if (auto const line = weakHairline.get())
                        {
                            line.Background(LookupBrush(L"DividerStrokeColorDefaultBrush"));
                            line.Opacity(0.75);
                        }
                        if (wasDragging)
                        {
                            SavePaneColumnWidths(paneIndex);
                        }
                    });
                grip.DoubleTapped([this, paneIndex, listColIdx](auto const&, auto const& args)
                    {
                        SetActivePaneIndex(paneIndex);
                        m_panes[paneIndex].list_column_widths =
                            ::FileScoutNative::Domain::ResetListColumn(m_panes[paneIndex].list_column_widths, listColIdx);
                        ApplyListColumnWidthsToPane(paneIndex);
                        SavePaneColumnWidths(paneIndex);
                        args.Handled(true);
                    });

                cellGrid.Children().Append(grip);
                winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(cellGrid, colIdx + colOffset);
                columnsGrid.Children().Append(cellGrid);
            }
            else
            {
                winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(btn, colIdx + colOffset);
                columnsGrid.Children().Append(btn);
            }
        }

        auto toggleStack = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
        toggleStack.Orientation(winrt::Microsoft::UI::Xaml::Controls::Orientation::Horizontal);
        toggleStack.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
        toggleStack.Margin({ 8, 0, 0, 0 });
        toggleStack.SizeChanged([this, paneIndex](
            auto const&,
            winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
            {
                double const measured = static_cast<double>(args.NewSize().Width) + 8.0;
                if (measured > 0.0 && std::abs(m_panes[paneIndex].sort_header_toggle_width - measured) > 1.0)
                {
                    m_panes[paneIndex].sort_header_toggle_width = measured;
                    ApplyListColumnWidthsToPane(paneIndex);
                }
            });

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
                btn.Foreground(LookupBrush(L"AccentTextFillColorPrimaryBrush"));
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
                        SavePaneViewModePreference(paneIndex);
                        return;
                    }

                    SetViewMode(paneIndex, capturedMode);
                });
        };

        addToggleBtn(L"Cards", ViewMode::Cards);
        addToggleBtn(L"List",  ViewMode::List);

        if (isListMode)
        {
            auto headerScroll = winrt::Microsoft::UI::Xaml::Controls::ScrollViewer{};
            headerScroll.HorizontalScrollMode(winrt::Microsoft::UI::Xaml::Controls::ScrollMode::Enabled);
            headerScroll.HorizontalScrollBarVisibility(winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Hidden);
            headerScroll.VerticalScrollMode(winrt::Microsoft::UI::Xaml::Controls::ScrollMode::Disabled);
            headerScroll.VerticalScrollBarVisibility(winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Disabled);
            headerScroll.ZoomMode(winrt::Microsoft::UI::Xaml::Controls::ZoomMode::Disabled);
            headerScroll.Content(columnsGrid);

            headerScroll.Loaded([this, paneIndex](auto const&, auto const&)
                {
                    SyncListHeaderHorizontalOffset(paneIndex);
                });
            headerScroll.ViewChanged([this, paneIndex](
                auto const& sender,
                winrt::Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const&)
                {
                    auto const hdrSv = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>();
                    auto const& curPane = m_panes[paneIndex];
                    if (!hdrSv || curPane.sort_header_scroll_viewer != hdrSv || !curPane.active_list_view)
                    {
                        return;
                    }
                    if (curPane.active_list_view.Items().Size() == 0)
                    {
                        m_panes[paneIndex].list_header_offset_unreconciled = true;
                        return;
                    }
                    auto const listSv =
                        FindDescendantByType<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>(curPane.active_list_view);
                    if (!listSv)
                    {
                        return;
                    }
                    double const maxListOffset = listSv.ScrollableWidth();
                    double const clamped = (std::min)(hdrSv.HorizontalOffset(), maxListOffset);
                    if (hdrSv.HorizontalOffset() > maxListOffset + 0.5)
                    {
                        hdrSv.ChangeView(
                            winrt::box_value(clamped).as<winrt::Windows::Foundation::IReference<double>>(),
                            nullptr,
                            nullptr,
                            true);
                    }
                    if (std::abs(listSv.HorizontalOffset() - clamped) > 0.5)
                    {
                        listSv.ChangeView(
                            winrt::box_value(clamped).as<winrt::Windows::Foundation::IReference<double>>(),
                            nullptr,
                            nullptr,
                            true);
                    }
                });

            pane.sort_header_scroll_viewer = headerScroll;
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(headerScroll, 0);
            outerGrid.Children().Append(headerScroll);
        }
        else
        {
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(columnsGrid, 0);
            outerGrid.Children().Append(columnsGrid);
        }
        winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(toggleStack, 1);
        outerGrid.Children().Append(toggleStack);

        headerBorder.Child(outerGrid);
    }

    void MainWindow::SetSortColumn(int32_t const paneIndex, SortColumn const column)
    {
        auto& pane = m_panes[paneIndex];

        if (pane.sort_column == column)
        {
            pane.sort_direction = (pane.sort_direction == SortDirection::Ascending)
                ? SortDirection::Descending
                : SortDirection::Ascending;
        }
        else
        {
            pane.sort_column = column;
            pane.sort_direction = (column == SortColumn::DateModified || column == SortColumn::Size)
                ? SortDirection::Descending
                : SortDirection::Ascending;
        }

        BuildSortHeader(paneIndex);

        for (auto& tab : pane.tabs)
        {
            SortItems(tab.items, pane.sort_column, pane.sort_direction);
        }
        SortItems(pane.search_snapshot, pane.sort_column, pane.sort_direction);
        SortItems(pane.filtered_items, pane.sort_column, pane.sort_direction);

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
        SavePaneSortPreference(paneIndex);
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
        ApplyListColumnWidthsToPane(paneIndex);
        SyncListHeaderHorizontalOffset(paneIndex);

        StatusTextBlock().Text(ToHString(
            std::to_wstring(pane.items.size()) + L" items  |  " +
            (enabled
                ? L"group view  |  grouped by " + SortColumnLabel(pane.sort_column)
                : RowPresentationStatusLabel(pane))));
        SavePaneGroupingPreference(paneIndex);
    }

    void MainWindow::ToggleHiddenFiles(int32_t const paneIndex)
    {
        auto& pane = m_panes[paneIndex];
        pane.show_hidden_files = !pane.show_hidden_files;
        if (!pane.is_drives_view && !pane.current_path.empty())
        {
            LoadDirectoryInPane(paneIndex, pane.current_path);
        }
        RefreshViewOptionsDropDown();
        StatusTextBlock().Text(ToHString(
            pane.show_hidden_files ? L"Hidden files shown." : L"Hidden files hidden."));
        SavePaneHiddenFilesPreference(paneIndex);
    }

    void MainWindow::ToggleExtensions(int32_t const paneIndex)
    {
        auto& pane = m_panes[paneIndex];
        pane.show_file_extensions = !pane.show_file_extensions;
        RebuildAllTabRowsForPane(paneIndex);
        RefreshViewOptionsDropDown();
        StatusTextBlock().Text(ToHString(
            pane.show_file_extensions ? L"File extensions shown." : L"File extensions hidden."));
        SavePaneExtensionsPreference(paneIndex);
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

        if (pane.active_list_view)
        {
            try
            {
                auto const rootGrid = Content().as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
                ApplyRowPresentation(pane.active_list_view, rootGrid, pane);
            }
            catch (...) {}
        }
        ApplyListColumnWidthsToPane(paneIndex);
        SyncListHeaderHorizontalOffset(paneIndex);

        StatusTextBlock().Text(ToHString(
            std::to_wstring(pane.items.size()) + L" items  |  " +
            RowPresentationStatusLabel(pane)));
        SavePaneViewModePreference(paneIndex);
    }

    void MainWindow::RebuildAllTabRowsForPane(int32_t const paneIndex)
    {
        auto& pane = m_panes[paneIndex];
        bool const replacesFinalPublish = pane.awaiting_final_batch && !pane.items.empty();
        if (replacesFinalPublish)
        {
            pane.awaiting_final_batch = false;
        }

        // Use (std::min)() to prevent expansion of the Windows.h min() macro.
        auto const tabCount = (std::min)(pane.tabs.size(), pane.tab_rows.size());
        for (std::size_t i = 0; i < tabCount; ++i)
        {
            auto const& source = (static_cast<int32_t>(i) == pane.active_tab_index)
                ? DisplayedItems(pane)
                : pane.tabs[i].items;

            auto& rows = pane.tab_rows[i];
            rows.Clear();
            std::wstring previousGroupLabel;
            for (auto const& item : source)
            {
                auto row = MakeFileRowViewModel(item);
                auto rowImpl = winrt::get_self<winrt::FileScoutNative::implementation::FileRowViewModel>(row);

                if (!pane.show_file_extensions && !item.is_folder)
                {
                    auto const dotPos = item.display_name.rfind(L'.');
                    if (dotPos != std::wstring_view::npos && dotPos > 0)
                    {
                        rowImpl->SetName(item.display_name.substr(0, dotPos));
                    }
                }

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

        if (replacesFinalPublish)
        {
            ApplyPendingFilter(paneIndex);
        }
    }

    void MainWindow::BuildSidebarFavorites()
    {
        ScopedTimer totalTimer{ L"BuildSidebarFavorites" };
        auto& navDiagRec = ::FileScoutNative::NavDiag::Recorder::Instance();
        bool const navDiagOn = navDiagRec.Enabled();
        auto const sbBegin = navDiagOn ? ::FileScoutNative::NavDiag::Now() : ::FileScoutNative::NavDiag::Tick{};
        long long iconsUs = 0;

        auto const panel = SidebarPlacesPanel();
        panel.Children().Clear();
        m_sidebarPlaceRows.clear();
        m_sidebarRecentRows.clear();
        m_sidebarShownPathKeys.clear();
        m_renderedRecentKeys.clear();

        auto const& activePane = ActivePane();
        auto const favFetchBegin = navDiagOn ? ::FileScoutNative::NavDiag::Now() : ::FileScoutNative::NavDiag::Tick{};
        auto const favorites = m_favoritesService.GetSidebarFavorites();
        long long const favFetchUs = navDiagOn
            ? ::FileScoutNative::NavDiag::SpanUs(favFetchBegin, ::FileScoutNative::NavDiag::Now())
            : -1;
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
            if (entry.is_drives_view) continue;

            if (entry.is_pinned)
            {
                if (!entry.path.empty())
                    m_sidebarShownPathKeys.insert(::FileScoutNative::Domain::SidebarPathKey(entry.path));
                continue;
            }

            auto btn = winrt::Microsoft::UI::Xaml::Controls::Button{};
            StyleSidebarButtonBase(btn);
            btn.Margin({ 0, 0, 0, 2 });

            if (!entry.path.empty())
            {
                winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                    btn,
                    winrt::box_value(ToHString(entry.path.wstring())));
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
                BuildCachedSidebarGlyph(
                    entry.path,
                    m_shellInterop,
                    m_iconSourceCache,
                    SidebarFavoriteFallbackLabel(entry),
                    navDiagOn ? &iconsUs : nullptr),
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
                !entry.path.empty() && activeFavoriteScore > 0 &&
                SidebarPathsEqual(entry.path, activeFavoritePath);

            auto surface = BuildSidebarRowSurface(isSelected);
            surface.Child(rowGrid);
            btn.Content(surface);

            if (entry.is_recycle_bin)
            {
                auto const recycleTarget = std::filesystem::path{ ::FileScoutNative::Domain::kRecycleBinLocation };
                btn.Click([this, recycleTarget](auto const&, auto const&)
                {
                    NavDiagSidebarClick const navDiagClick;
                    LoadDirectory(recycleTarget);
                });
                btn.AddHandler(
                    winrt::Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),
                    winrt::box_value(winrt::Microsoft::UI::Xaml::Input::PointerEventHandler(
                        [this, recycleTarget](auto const&, auto const& args)
                        {
                            if (args.GetCurrentPoint(nullptr).Properties().PointerUpdateKind() ==
                                winrt::Microsoft::UI::Input::PointerUpdateKind::MiddleButtonPressed)
                            {
                                args.Handled(true);
                                AddTab(recycleTarget);
                            }
                        })),
                    true);
            }
            else if (!entry.path.empty())
            {
                auto const targetPath = entry.path;
                btn.Click([this, targetPath](auto const&, auto const&)
                {
                    NavDiagSidebarClick const navDiagClick;
                    LoadDirectory(targetPath);
                });
                btn.AddHandler(
                    winrt::Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),
                    winrt::box_value(winrt::Microsoft::UI::Xaml::Input::PointerEventHandler(
                        [this, targetPath](auto const&, auto const& args)
                        {
                            if (args.GetCurrentPoint(nullptr).Properties().PointerUpdateKind() ==
                                winrt::Microsoft::UI::Input::PointerUpdateKind::MiddleButtonPressed)
                            {
                                args.Handled(true);
                                AddTab(targetPath);
                            }
                        })),
                    true);
            }

            if (!entry.path.empty())
                m_sidebarShownPathKeys.insert(::FileScoutNative::Domain::SidebarPathKey(entry.path));
            m_sidebarPlaceRows.push_back(SidebarRowSurfaceEntry{
                .path = entry.path,
                .is_drives_view = entry.is_drives_view,
                .is_recycle_bin = entry.is_recycle_bin,
                .emphasized = isSelected,
                .surface = surface,
            });
            panel.Children().Append(btn);
        }

        m_sidebarPlacesStaticCount = panel.Children().Size();
        m_sidebarRecentsDirty = false;
        RefreshSidebarRecentsSection(true, false);

        auto const placesEnd = navDiagOn ? ::FileScoutNative::NavDiag::Now() : ::FileScoutNative::NavDiag::Tick{};
        UpdateBookmarksSection();
        auto const bookmarksEnd = navDiagOn ? ::FileScoutNative::NavDiag::Now() : ::FileScoutNative::NavDiag::Tick{};
        UpdateThisPCSection();
        UpdateSidebarSelectionHighlights();
        if (navDiagOn)
        {
            auto const sbEnd = ::FileScoutNative::NavDiag::Now();
            long long const placesTotalUs = ::FileScoutNative::NavDiag::SpanUs(sbBegin, placesEnd);
            long long const placesUiUs = (std::max)(0LL, placesTotalUs - (std::max)(0LL, favFetchUs) - iconsUs);
            ::FileScoutNative::NavDiag::SidebarRebuildTiming sbTiming{};
            sbTiming.total_us = ::FileScoutNative::NavDiag::SpanUs(sbBegin, sbEnd);
            sbTiming.favorites_fetch_us = favFetchUs;
            sbTiming.icons_us = iconsUs;
            sbTiming.places_ui_us = placesUiUs;
            sbTiming.bookmarks_us = ::FileScoutNative::NavDiag::SpanUs(placesEnd, bookmarksEnd);
            sbTiming.thispc_us = ::FileScoutNative::NavDiag::SpanUs(bookmarksEnd, sbEnd);
            navDiagRec.NoteSidebarRebuilt(sbTiming);
        }
    }

    void MainWindow::UpdateSidebarSelectionHighlights()
    {
        auto const& activePane = ActivePane();
        std::filesystem::path activeFavoritePath;
        std::size_t activeFavoriteScore = 0;

        if (!activePane.is_drives_view && !activePane.current_path.empty())
        {
            auto consider = [&](std::filesystem::path const& candidate)
            {
                if (candidate.empty())
                {
                    return;
                }
                auto const score = SidebarPathMatchScore(candidate, activePane.current_path);
                if (score > activeFavoriteScore)
                {
                    activeFavoriteScore = score;
                    activeFavoritePath = candidate;
                }
            };
            for (auto const& row : m_sidebarPlaceRows)
            {
                if (!row.is_drives_view)
                {
                    consider(row.path);
                }
            }
            for (auto const& row : m_sidebarBookmarkRows)
            {
                consider(row.path);
            }
        }

        auto applyEmphasis = [](SidebarRowSurfaceEntry& row, bool const emphasized)
        {
            if (row.emphasized == emphasized || !row.surface)
            {
                return;
            }
            row.emphasized = emphasized;
            row.surface.Background(emphasized
                ? LookupBrush(L"SubtleFillColorSecondaryBrush")
                : SolidBrush(0x00, 0x00, 0x00, 0x00));
        };

        for (auto& row : m_sidebarPlaceRows)
        {
            bool const isSelected =
                (row.is_drives_view && activePane.is_drives_view) ||
                (!row.path.empty() && activeFavoriteScore > 0 &&
                    SidebarPathsEqual(row.path, activeFavoritePath));
            applyEmphasis(row, isSelected);
        }

        for (auto& row : m_sidebarRecentRows)
        {
            bool const isSelected =
                !row.path.empty() && activeFavoriteScore > 0 &&
                SidebarPathsEqual(row.path, activeFavoritePath);
            applyEmphasis(row, isSelected);
        }

        for (auto& row : m_sidebarBookmarkRows)
        {
            bool const isSelected =
                !row.path.empty() &&
                !activePane.is_drives_view &&
                !activePane.current_path.empty() &&
                SidebarPathsEqual(row.path, activePane.current_path);
            applyEmphasis(row, isSelected);
        }

        for (auto& row : m_sidebarDriveRows)
        {
            bool const driveSelected =
                !activePane.is_drives_view &&
                !activePane.current_path.empty() &&
                !row.path.empty() &&
                _wcsnicmp(
                    activePane.current_path.root_path().wstring().c_str(),
                    row.path.wstring().c_str(),
                    row.path.wstring().size()) == 0;
            applyEmphasis(row, driveSelected);
        }
    }

    void MainWindow::RefreshSidebarRecentsSection(bool const forceRebuild, bool const updateHighlights)
    {
        auto const newKeys = ::FileScoutNative::Domain::VisibleRecentPathKeys(
            m_recentFolders, m_sidebarShownPathKeys, 5);
        if (!forceRebuild && newKeys == m_renderedRecentKeys)
        {
            return;
        }

        auto const panel = SidebarPlacesPanel();
        while (panel.Children().Size() > m_sidebarPlacesStaticCount)
        {
            panel.Children().RemoveAtEnd();
        }
        m_sidebarRecentRows.clear();
        m_renderedRecentKeys = newKeys;

        auto const recents = ::FileScoutNative::Domain::SelectRecentPaths(
            m_recentFolders, m_sidebarShownPathKeys, 5);
        if (recents.empty())
        {
            return;
        }

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

            auto iconTile = BuildSidebarTile(
                BuildCachedSidebarGlyph(
                    recentPath,
                    m_shellInterop,
                    m_iconSourceCache,
                    L"folder"),
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

            auto surface = BuildSidebarRowSurface(false);
            surface.Child(rowGrid);
            btn.Content(surface);

            auto const targetPath = recentPath;
            btn.Click([this, targetPath](auto const&, auto const&)
            {
                NavDiagSidebarClick const navDiagClick;
                LoadDirectory(targetPath);
            });
            btn.AddHandler(
                winrt::Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),
                winrt::box_value(winrt::Microsoft::UI::Xaml::Input::PointerEventHandler(
                    [this, targetPath](auto const&, auto const& args)
                    {
                        if (args.GetCurrentPoint(nullptr).Properties().PointerUpdateKind() ==
                            winrt::Microsoft::UI::Input::PointerUpdateKind::MiddleButtonPressed)
                        {
                            args.Handled(true);
                            AddTab(targetPath);
                        }
                    })),
                true);
            m_sidebarRecentRows.push_back(SidebarRowSurfaceEntry{
                .path = recentPath,
                .emphasized = false,
                .surface = surface,
            });
            panel.Children().Append(btn);
        }

        if (updateHighlights)
        {
            UpdateSidebarSelectionHighlights();
        }
    }

    void MainWindow::ScheduleSidebarRecentsRefresh()
    {
        auto const newKeys = ::FileScoutNative::Domain::VisibleRecentPathKeys(
            m_recentFolders, m_sidebarShownPathKeys, 5);
        if (newKeys == m_renderedRecentKeys)
        {
            return;
        }
        if (m_sidebarRecentsRefreshPending || !m_dispatcherQueue)
        {
            return;
        }
        m_sidebarRecentsRefreshPending = true;
        auto const weakThis = get_weak();
        m_dispatcherQueue.TryEnqueue(
            winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weakThis]()
            {
                if (auto self = weakThis.get())
                {
                    self->m_sidebarRecentsRefreshPending = false;
                    self->RefreshSidebarRecentsSection(false);
                }
            });
    }

    void MainWindow::FlushDeferredSidebarRecents()
    {
        if (!m_sidebarRecentsDirty)
        {
            return;
        }
        m_sidebarRecentsDirty = false;
        ScheduleSidebarRecentsRefresh();
    }

    void MainWindow::UpdateBookmarksSection()
    {
        ScopedTimer totalTimer{ L"UpdateBookmarksSection" };
        auto const panel = BookmarksPanel();
        panel.Children().Clear();
        m_sidebarBookmarkRows.clear();

        auto const& activePane = ActivePane();
        auto const bookmarks = m_favoritesService.GetPinnedFavorites();

        if (!bookmarks.empty())
            BookmarksExpander().IsExpanded(true);

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

            auto iconTile = BuildSidebarTile(
                BuildCachedSidebarGlyph(entry.path, m_shellInterop, m_iconSourceCache, L"folder"),
                20.0);

            auto label = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
            label.Text(ToHString(entry.display_name));
            label.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            label.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
            label.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
            label.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);

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
                NavDiagSidebarClick const navDiagClick;
                LoadDirectory(pinnedPath);
            });
            btn.AddHandler(
                winrt::Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),
                winrt::box_value(winrt::Microsoft::UI::Xaml::Input::PointerEventHandler(
                    [this, pinnedPath](auto const&, auto const& args)
                    {
                        if (args.GetCurrentPoint(nullptr).Properties().PointerUpdateKind() ==
                            winrt::Microsoft::UI::Input::PointerUpdateKind::MiddleButtonPressed)
                        {
                            args.Handled(true);
                            AddTab(pinnedPath);
                        }
                    })),
                true);
            m_sidebarBookmarkRows.push_back(SidebarRowSurfaceEntry{
                .path = entry.path,
                .emphasized = isSelected,
                .surface = surface,
            });
            panel.Children().Append(btn);
        }
    }

    void MainWindow::RefreshSidebarCaches()
    {
        m_sidebarDrives.Invalidate();
        BuildSidebarFavorites();
    }

    void MainWindow::UpdateThisPCSection()
    {
        ScopedTimer totalTimer{ L"UpdateThisPCSection" };
        auto const panel = ThisPCPanel();
        panel.Children().Clear();
        m_sidebarDriveRows.clear();

        auto const& activePane = ActivePane();
        auto const& drives = m_sidebarDrives.Get([this] { return BuildSidebarDriveUsage(m_shellInterop); });
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
                NavDiagSidebarClick const navDiagClick;
                LoadDirectory(drivePath);
            });
            button.AddHandler(
                winrt::Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),
                winrt::box_value(winrt::Microsoft::UI::Xaml::Input::PointerEventHandler(
                    [this, drivePath](auto const&, auto const& args)
                    {
                        if (args.GetCurrentPoint(nullptr).Properties().PointerUpdateKind() ==
                            winrt::Microsoft::UI::Input::PointerUpdateKind::MiddleButtonPressed)
                        {
                            args.Handled(true);
                            AddTab(drivePath);
                        }
                    })),
                true);
            button.Content(card);
            m_sidebarDriveRows.push_back(SidebarRowSurfaceEntry{
                .path = drive.path,
                .emphasized = driveSelected,
                .surface = card,
            });
            panel.Children().Append(button);
        }
    }

    void MainWindow::LoadDrivesView(bool const skipHistoryRecord)
    {
        try
        {
            auto items = m_shellInterop.EnumerateDrives();
            SortItems(items, ActivePane().sort_column, ActivePane().sort_direction);

            auto& pane = ActivePane();
            EndSearchOnNavigation(m_activePaneIndex, false);
            ++pane.load_generation;
            pane.awaiting_final_batch = false;
            if (!skipHistoryRecord && !pane.is_drives_view && !pane.current_path.empty())
            {
                ::FileScoutNative::Domain::RecordNavigation(
                    pane.back_stack,
                    pane.forward_stack,
                    pane.current_path,
                    std::filesystem::path{ ::FileScoutNative::Domain::kDrivesViewLocation });
            }
            bool recentsChanged = false;
            if (auto const left = ::FileScoutNative::Domain::FolderToRecordOnLeave(pane.current_path, std::filesystem::path{}))
            {
                ::FileScoutNative::Domain::RecordRecentFolder(m_recentFolders, *left, kRecentFolderCapacity);
                recentsChanged = true;
            }
            pane.current_path = std::filesystem::path{};
            pane.is_drives_view = true;
            pane.is_archive_view = false;
            pane.is_recycle_bin_view = false;
            StopPaneDirectoryWatcher(m_activePaneIndex);
            if (recentsChanged)
            {
                BuildSidebarFavorites();
            }
            pane.is_archive_view = false;
            pane.items = items;
            pane.selection.Clear();

            pane.tabs[pane.active_tab_index].path = std::filesystem::path{};
            pane.tabs[pane.active_tab_index].items = items;
            pane.tabs[pane.active_tab_index].selection = {};
            pane.tabs[pane.active_tab_index].selected_item_index = -1;
            pane.tabs[pane.active_tab_index].is_drives_view = true;
            pane.tabs[pane.active_tab_index].is_archive_view = false;
            pane.tabs[pane.active_tab_index].is_recycle_bin_view = false;
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

    void MainWindow::ToggleDualPane()
    {
        m_dualPaneVisible = !m_dualPaneVisible;
        ApplyPaneLayoutVisibility();
        UpdateActivePaneIndicator();
        if (m_dualPaneVisible && m_deferredRightPanePath.has_value())
        {
            auto const path = *std::exchange(m_deferredRightPanePath, std::nullopt);
            LoadDirectoryInPane(1, path);
        }
        else
        {
            UpdatePaneDirectoryWatcher(1);
            if (m_dualPaneVisible)
            {
                auto& rightPane = m_panes[1];
                if (::FileScoutNative::Domain::ShouldWatchLocation(
                        rightPane.current_path,
                        rightPane.is_drives_view,
                        rightPane.is_archive_view,
                        rightPane.is_recycle_bin_view))
                {
                    if (rightPane.watch_armed)
                    {
                        rightPane.watch_refresh_pending = true;
                        FlushPendingWatchRefresh(1);
                    }
                    else if (rightPane.directory_watcher && rightPane.directory_watcher->IsWatching())
                    {
                        rightPane.watch_reconcile_on_arm = true;
                    }
                    else
                    {
                        rightPane.watch_refresh_pending = true;
                        FlushPendingWatchRefresh(1);
                    }
                }
            }
        }
        SaveDualPaneSetting();
        StatusTextBlock().Text(m_dualPaneVisible
            ? L"Dual pane enabled."
            : L"Single-pane beta layout restored.");
    }
}
