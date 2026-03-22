#include "pch.h"
// IRandomAccessStreamWithContentType return type for co_await file.OpenReadAsync().
// Included here rather than pch.h to avoid PCH bloat across all TUs.
#include <cmath>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Storage.Streams.h>
#include <robuffer.h>   // IBufferByteAccess for WriteableBitmap pixel access
#include "MainWindow.xaml.h"
#include "Domain/FileNameValidation.h"
#include "Search/FuzzyMatcher.h"
#include "TraceHelper.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

namespace
{
    using SortColumn = winrt::FileScoutNative::implementation::SortColumn;

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
                auto const used = total > free ? total - free : 0;

                sidebarDrive.has_usage = true;
                sidebarDrive.free_bytes = free;
                sidebarDrive.total_bytes = total;
                sidebarDrive.used_ratio =
                    std::clamp(static_cast<double>(used) / static_cast<double>(total), 0.0, 1.0);
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
        double const fontSize = 15.0)
    {
        auto icon = winrt::Microsoft::UI::Xaml::Controls::FontIcon{};
        icon.Glyph(winrt::hstring{ glyph });
        icon.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe Fluent Icons"));
        icon.FontSize(fontSize);
        icon.Foreground(LookupBrush(L"TextFillColorSecondaryBrush"));
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

    std::wstring_view KnownFolderGlyph(std::wstring_view const displayName)
    {
        using namespace std::literals;
        auto const equalsIgnoreCase = [](std::wstring_view const left, std::wstring_view const right)
        {
            return left.size() == right.size() &&
                _wcsnicmp(left.data(), right.data(), left.size()) == 0;
        };

        if (equalsIgnoreCase(displayName, L"Home"sv) ||
            equalsIgnoreCase(displayName, L"Quick access"sv))
        {
            return L"\uE80F";
        }

        if (equalsIgnoreCase(displayName, L"Desktop"sv))
        {
            return L"\uEC64";
        }

        if (equalsIgnoreCase(displayName, L"Documents"sv))
        {
            return L"\uE8A5";
        }

        if (equalsIgnoreCase(displayName, L"Downloads"sv))
        {
            return L"\uE896";
        }

        if (equalsIgnoreCase(displayName, L"Pictures"sv))
        {
            return L"\uEB9F";
        }

        if (equalsIgnoreCase(displayName, L"Music"sv))
        {
            return L"\uEC4F";
        }

        if (equalsIgnoreCase(displayName, L"Videos"sv))
        {
            return L"\uE8B2";
        }

        if (equalsIgnoreCase(displayName, L"This PC"sv))
        {
            return L"\uEDA2";
        }

        if (equalsIgnoreCase(displayName, L"Recycle Bin"sv))
        {
            return L"\uE74D";
        }

        return {};
    }

    winrt::Microsoft::UI::Xaml::FrameworkElement BuildSidebarGlyph(
        std::wstring const& displayName,
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

        if (auto const glyph = KnownFolderGlyph(displayName); !glyph.empty())
        {
            return BuildSidebarFontIcon(glyph);
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

    winrt::Microsoft::UI::Xaml::FrameworkElement BuildSidebarDriveGlyph(
        SidebarDriveUsage const& drive)
    {
        if (!drive.icon.rgba.empty())
        {
            auto image = winrt::Microsoft::UI::Xaml::Controls::Image{};
            image.Width(16);
            image.Height(16);
            image.Stretch(winrt::Microsoft::UI::Xaml::Media::Stretch::Uniform);
            image.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
            image.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
            if (auto const source = BuildIconSource(drive.icon))
            {
                image.Source(source);
                return image;
            }
        }

        return BuildSidebarFontIcon(L"\uEDA2", 16.0);
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

    winrt::Microsoft::UI::Xaml::FrameworkElement WrapRowWithGroupHeader(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& row,
        std::optional<std::wstring> const& groupLabel,
        bool const listView)
    {
        if (!groupLabel.has_value() || groupLabel->empty())
        {
            return row;
        }

        auto host = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
        host.Spacing(listView ? 2 : 4);

        auto header = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
        header.Text(ToHString(*groupLabel));
        header.Margin({ 4.0, listView ? 10.0 : 12.0, 0.0, 0.0 });
        header.Opacity(0.72);
        header.FontSize(11);
        header.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        header.CharacterSpacing(20);

        host.Children().Append(header);
        host.Children().Append(row);
        return host;
    }

    // Creates a rich card row (Cards view mode).
    winrt::Microsoft::UI::Xaml::FrameworkElement CreateCardRow(
        ::FileScoutNative::Domain::ShellItemRef const& item)
    {
        // Build the detail chip: "Type  |  Size"
        std::wstring details = item.kind_label;
        if (item.size_available)
        {
            details += L"  |  " + FormatSize(item.size_bytes);
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
        bool const listView,
        std::optional<std::wstring> const& groupLabel = std::nullopt)
    {
        auto row = listView ? CreateListRow(item) : CreateCardRow(item);
        return WrapRowWithGroupHeader(row, groupLabel, listView);
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

    void MainWindow::TitleBarDragHost_Loaded(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        UpdateTitleBarInteractiveRegions();
    }

    void MainWindow::TitleBarDragHost_SizeChanged(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::SizeChangedEventArgs const&)
    {
        UpdateTitleBarInteractiveRegions();
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

        auto const pathText = TrimFileName(AddressTextBox().Text().c_str());
        args.Handled(true);
        HideAddressTextBox();

        if (_wcsicmp(pathText.c_str(), L"This PC") == 0)
        {
            LoadDrivesView();
            return;
        }

        LoadDirectory(pathText);
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
                pane.search_service.Cancel();
                SearchProgressForPane(m_activePaneIndex).Visibility(
                    winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
                pane.search_mode = SearchMode::None;
                pane.search_query.clear();
                SearchTextBox().IsReadOnly(false);
                SearchTextBox().Text(L"");

                // Restore ListView from snapshot.
                auto& rows = pane.tab_rows[pane.active_tab_index];
                rows.Clear();
                bool const listMode = (pane.view_mode == ViewMode::List);
                for (auto const& item : pane.search_snapshot)
                    rows.Append(CreateRowView(item, listMode));
            }
            return;
        }

        if (args.Key() == winrt::Windows::System::VirtualKey::Enter &&
            pane.search_mode == SearchMode::Filter)
        {
            args.Handled(true);

            if (pane.search_query.size() < 2) return;

            pane.search_mode = SearchMode::Recursive;
            SearchTextBox().IsReadOnly(true);

            // Show per-pane progress bar.
            SearchProgressForPane(m_activePaneIndex).Visibility(
                winrt::Microsoft::UI::Xaml::Visibility::Visible);

            // Update status bar.
            StatusTextBlock().Text(L"0 matches");

            auto const paneIndex = m_activePaneIndex;
            auto post = [dq = m_dispatcherQueue](std::function<void()> fn)
            {
                dq.TryEnqueue(
                    winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Normal,
                    std::move(fn));
            };

            auto resultCount = std::make_shared<std::atomic<int>>(0);

            pane.search_service.Start(
                pane.current_path.wstring(),
                pane.search_query,
                [this, paneIndex, resultCount](
                    std::vector<::FileScoutNative::Search::SearchResult> results)
                {
                    auto& p = m_panes[paneIndex];
                    if (p.search_mode != SearchMode::Recursive) return;

                    *resultCount = static_cast<int>(results.size());
                    StatusTextBlock().Text(std::to_wstring(*resultCount) + L" matches");

                    p.search_results = results; // keep for navigate-on-select

                    auto& rows = p.tab_rows[p.active_tab_index];
                    rows.Clear();
                    for (auto const& r : results)
                        rows.Append(CreateSearchRowView(r, p.current_path));
                },
                [this, paneIndex, resultCount](bool cap_reached)
                {
                    auto& p = m_panes[paneIndex];
                    if (p.search_mode != SearchMode::Recursive) return;

                    SearchProgressForPane(paneIndex).Visibility(
                        winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

                    if (cap_reached)
                        StatusTextBlock().Text(
                            L"Showing top 200 matches \u2014 refine your query to narrow results");
                    else
                        StatusTextBlock().Text(
                            std::to_wstring(resultCount->load()) + L" matches in " +
                            p.current_path.wstring());
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
                pane.search_mode = SearchMode::None;
                pane.search_query.clear();
                auto& rows = pane.tab_rows[pane.active_tab_index];
                rows.Clear();
                bool const listMode = (pane.view_mode == ViewMode::List);
                for (auto const& item : pane.search_snapshot)
                    rows.Append(CreateRowView(item, listMode));
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

        if (query.size() < 2)
        {
            // Show all snapshot items unfiltered.
            bool const listMode = (pane.view_mode == ViewMode::List);
            for (auto const& item : pane.search_snapshot)
                rows.Append(CreateRowView(item, listMode));
            return;
        }

        // Score all snapshot items and collect top 200.
        std::vector<std::pair<int, ::FileScoutNative::Domain::ShellItemRef const*>> scored;
        scored.reserve(pane.search_snapshot.size());
        for (auto const& item : pane.search_snapshot)
        {
            int s = ::FileScoutNative::Search::FuzzyScore(query, item.display_name);
            if (s >= 0)
                scored.emplace_back(s, &item);
        }
        std::sort(scored.begin(), scored.end(),
            [](auto const& a, auto const& b) { return a.first > b.first; });
        if (scored.size() > 200) scored.resize(200);

        bool const listMode = (pane.view_mode == ViewMode::List);
        for (auto const& [score, itemPtr] : scored)
            rows.Append(CreateRowView(*itemPtr, listMode));
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

        // In Recursive search mode, double-click navigates to the result's parent folder.
        auto& pane = ActivePane();
        if (pane.search_mode == SearchMode::Recursive)
        {
            auto const idx = ResolveItemIndexFromSource(args.OriginalSource());
            if (!idx.has_value()) return;
            if (*idx < 0 || static_cast<std::size_t>(*idx) >= pane.search_results.size()) return;

            auto const& result = pane.search_results[static_cast<std::size_t>(*idx)];
            auto const fullPath = std::filesystem::path(result.full_path);
            LoadDirectory(fullPath.parent_path(), fullPath);
            return;
        }

        auto const index = ResolveItemIndexFromSource(args.OriginalSource());
        if (!index.has_value() ||
            *index < 0 ||
            *index >= static_cast<int32_t>(ActivePane().items.size()))
        {
            return;
        }

        auto const& item = ActivePane().items[static_cast<std::size_t>(*index)];
        if (!ActivePane().is_archive_view && m_shellInterop.IsArchivePath(item.path))
        {
            LoadDirectory(item.path);
            return;
        }

        if (item.is_folder)
        {
            LoadDirectory(item.path);
            return;
        }

        if (ActivePane().is_archive_view)
        {
            OpenArchiveItemAsync(item);
            return;
        }

        try
        {
            m_shellFileOperationService.OpenPath(GetWindowHandle(), item.path);
            if (IsUnsupportedArchiveItem(item))
            {
                StatusTextBlock().Text(ToHString(BuildUnsupportedArchiveOpenStatus(item, true)));
            }
            else
            {
                StatusTextBlock().Text(ToHString(L"Opened " + item.display_name));
            }
        }
        catch (std::exception const&)
        {
            if (IsUnsupportedArchiveItem(item))
            {
                StatusTextBlock().Text(ToHString(BuildUnsupportedArchiveOpenStatus(item, false)));
            }
            else
            {
                StatusTextBlock().Text(ToHString(L"Open failed: " + item.display_name));
            }
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
            auto const& item = ActivePane().items[static_cast<std::size_t>(*itemIndex)];
            if (!ActivePane().selection.IsSelected(item.parsing_name))
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
            scenario.focused_item = ActivePane().items[static_cast<std::size_t>(*itemIndex)];
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
            std::vector<std::pair<winrt::hstring, bool>> dragItems;
            dragItems.reserve(selectedItems.size());
            for (auto const& item : selectedItems)
            {
                dragItems.emplace_back(winrt::hstring{ item.path.wstring() }, item.is_folder);
            }

            // SetDataProvider defers the async StorageItem lookup to a background thread
            // called by the drop target when it actually requests the data.
            // This avoids a blocking .get() call on the STA (UI) thread.
            e.Data().SetDataProvider(
                winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems(),
                [dragItems = std::move(dragItems)](
                    winrt::Windows::ApplicationModel::DataTransfer::DataProviderRequest const& request)
                {
                    auto deferral = request.GetDeferral();
                    [request, deferral, dragItems]() mutable -> winrt::fire_and_forget
                    {
                        co_await winrt::resume_background();
                        try
                        {
                            auto items = winrt::single_threaded_vector<winrt::Windows::Storage::IStorageItem>();
                            for (auto const& [pathStr, isFolder] : dragItems)
                            {
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
        if (ActivePane().is_archive_view)
        {
            args.AcceptedOperation(
                winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::None);
            return;
        }

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

        if (pane.search_mode != SearchMode::None)
        {
            pane.search_service.Cancel();
            SearchProgressForPane(paneIndex).Visibility(
                winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            pane.search_mode = SearchMode::None;
            pane.search_query.clear();
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
                if (auto const control = element.try_as<winrt::Microsoft::UI::Xaml::Controls::Control>())
                {
                    if (!control.IsEnabled())
                    {
                        return;
                    }
                }

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

    void MainWindow::LoadDirectory(
        std::filesystem::path const& path,
        std::optional<std::filesystem::path> selectAfter,
        bool skipHistoryRecord)
    {
        // Exit search mode before navigating.
        auto& pane = ActivePane();

        // Record current path in back-stack unless this is a history navigation.
        if (!skipHistoryRecord && !pane.current_path.empty())
        {
            pane.back_stack.push_back(pane.current_path);
            pane.forward_stack.clear();
        }
        if (pane.search_mode != SearchMode::None)
        {
            pane.search_service.Cancel();
            SearchProgressForPane(m_activePaneIndex).Visibility(
                winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            pane.search_mode = SearchMode::None;
            pane.search_query.clear();
            SearchTextBox().IsReadOnly(false);
            SearchTextBox().Text(L"");
        }

        // Capture pane/tab index synchronously so async work targets the right pane
        // even if the user switches panes before the enumeration finishes.
        LoadDirectoryAsync(path, m_activePaneIndex, std::move(selectAfter));
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
        if (m_activePaneIndex == paneIndex)
        {
            UpdateBreadcrumbs(path);
            UpdateSearchPlaceholder(path);
            HideAddressTextBox();
            ResetSelectionDetails();
        }

        // Switch to a thread-pool thread for I/O and shell COM calls.
        co_await winrt::resume_background();

        std::filesystem::path absolute;
        std::vector<::FileScoutNative::Domain::ShellItemRef> items;
        bool isArchiveView = false;
        try
        {
            absolute = std::filesystem::absolute(path).lexically_normal();
            if (m_shellInterop.IsArchivePath(absolute))
            {
                items = m_shellInterop.EnumerateArchiveDirectory(absolute);
                isArchiveView = true;
            }
            else
            {
                items = m_shellInterop.EnumerateDirectory(absolute);
            }
            timing.enumeration_end_tick = ::GetTickCount64();
        }
        catch (...)
        {
            m_dispatcherQueue.TryEnqueue([this, lifetime, paneIndex, generation]()
            {
                if (m_panes[paneIndex].load_generation != generation) return;
                if (m_activePaneIndex == paneIndex)
                {
                    StatusTextBlock().Text(L"Directory load failed.");
                }
            });
            co_return;
        }

        // Post results back to the UI thread.
        m_dispatcherQueue.TryEnqueue(
            [this, lifetime, paneIndex, tabIndex, generation,
             absolute, selectAfter, isArchiveView, timing, items = std::move(items)]() mutable
            {
                auto& pane = m_panes[paneIndex];
                if (pane.load_generation != generation) return;
                if (pane.active_tab_index != tabIndex) return;

                SortItems(items, pane.sort_column, pane.sort_direction);
                pane.current_path = absolute;
                pane.is_drives_view = false;
                pane.is_archive_view = isArchiveView;
                pane.items = items;
                pane.selection.Clear();

                pane.tabs[tabIndex].path = absolute;
                pane.tabs[tabIndex].items = items;
                pane.tabs[tabIndex].selection = {};
                pane.tabs[tabIndex].selected_item_index = -1;
                pane.tabs[tabIndex].is_drives_view = false;
                pane.tabs[tabIndex].is_archive_view = isArchiveView;

                // Update tab header via TabViewForPane so we target the right pane
                // regardless of which pane is currently active.
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

                // Build and publish the first visible batch of rows.
                bool const listMode = (pane.view_mode == ViewMode::List);
                auto& rows = pane.tab_rows[tabIndex];
                rows.Clear();

                constexpr std::size_t k_first_batch = 150;
                std::size_t const totalItems = items.size();
                std::size_t const firstBatchEnd = (std::min)(k_first_batch, totalItems);

                std::wstring previousGroupLabel;
                for (std::size_t i = 0; i < firstBatchEnd; ++i)
                {
                    auto const& item = items[i];
                    std::optional<std::wstring> groupLabel;
                    if (pane.group_by_sort_column)
                    {
                        auto currentGroupLabel = BuildGroupLabel(item, pane.sort_column);
                        if (rows.Size() == 0 || currentGroupLabel != previousGroupLabel)
                        {
                            groupLabel = currentGroupLabel;
                        }
                        previousGroupLabel = std::move(currentGroupLabel);
                    }
                    rows.Append(CreateRowView(item, listMode, groupLabel));
                }

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
                    if (isArchiveView)
                    {
                        StatusTextBlock().Text(ToHString(
                            std::to_wstring(totalItems) +
                            (totalItems == 1 ? L" archive entry in " : L" archive entries in ") +
                            absolute.wstring()));
                    }
                    else
                    {
                        StatusTextBlock().Text(ToHString(
                            std::to_wstring(totalItems) + L" items in " + absolute.wstring()));
                    }
                }

                if (totalItems <= k_first_batch)
                {
                    // Small folder — first batch covers everything; record final timing and done.
                    timing.final_publish_tick = timing.first_batch_publish_tick;
                    pane.last_load_timing = timing;
                    TraceLog(FormatLoadTimingTrace(absolute, totalItems, timing));
                    if (selectAfter.has_value())
                    {
                        SelectItemByPath(*selectAfter);
                    }
                    return;
                }

                // Large folder — stage the remaining rows in a second dispatch so the UI thread
                // can process the first-batch render before blocking on further row creation.
                pane.awaiting_final_batch = true;
                pane.last_load_timing = timing;
                m_dispatcherQueue.TryEnqueue(
                    [this, lifetime, paneIndex, tabIndex, generation, absolute, selectAfter,
                     items = std::move(items), timing,
                     previousGroupLabel = std::move(previousGroupLabel),
                     listMode, firstBatchEnd]() mutable
                    {
                        auto& pane = m_panes[paneIndex];
                        if (pane.load_generation != generation) return;
                        if (pane.active_tab_index != tabIndex) return;
                        if (!pane.awaiting_final_batch) return;
                        pane.awaiting_final_batch = false;

                        auto& rows = pane.tab_rows[tabIndex];
                        std::size_t const totalItems = items.size();
                        for (std::size_t i = firstBatchEnd; i < totalItems; ++i)
                        {
                            auto const& item = items[i];
                            std::optional<std::wstring> groupLabel;
                            if (pane.group_by_sort_column)
                            {
                                auto currentGroupLabel = BuildGroupLabel(item, pane.sort_column);
                                if (currentGroupLabel != previousGroupLabel)
                                {
                                    groupLabel = currentGroupLabel;
                                }
                                previousGroupLabel = std::move(currentGroupLabel);
                            }
                            rows.Append(CreateRowView(item, listMode, groupLabel));
                        }

                        timing.final_publish_tick = ::GetTickCount64();
                        pane.last_load_timing = timing;
                        TraceLog(FormatLoadTimingTrace(absolute, totalItems, timing));
                        if (!pane.user_interacted_after_first_batch && selectAfter.has_value())
                        {
                            SelectItemByPath(*selectAfter);
                        }
                    });
            });
    }

    void MainWindow::UpdateRows(int32_t const paneIndex,
        std::vector<::FileScoutNative::Domain::ShellItemRef> const& items)
    {
        auto& pane = m_panes[paneIndex];
        bool const listMode = (pane.view_mode == ViewMode::List);
        auto& rows = pane.tab_rows[pane.active_tab_index];
        rows.Clear();

        std::wstring previousGroupLabel;
        for (auto const& item : items)
        {
            std::optional<std::wstring> groupLabel;
            if (pane.group_by_sort_column)
            {
                auto currentGroupLabel = BuildGroupLabel(item, pane.sort_column);
                if (rows.Size() == 0 || currentGroupLabel != previousGroupLabel)
                {
                    groupLabel = currentGroupLabel;
                }
                previousGroupLabel = std::move(currentGroupLabel);
            }

            rows.Append(CreateRowView(item, listMode, groupLabel));
        }
    }

    std::vector<int32_t> MainWindow::GetSelectedItemIndices() const
    {
        std::vector<int32_t> indices;
        if (!ActivePane().active_list_view)
        {
            return indices;
        }

        for (auto const& range : ActivePane().active_list_view.SelectedRanges())
        {
            for (uint32_t offset = 0; offset < range.Length(); ++offset)
            {
                auto const index = range.FirstIndex() + static_cast<int32_t>(offset);
                if (index >= 0 && index < static_cast<int32_t>(ActivePane().items.size()))
                {
                    indices.push_back(index);
                }
            }
        }

        if (indices.empty())
        {
            auto const index = ActivePane().active_list_view.SelectedIndex();
            if (index >= 0 && index < static_cast<int32_t>(ActivePane().items.size()))
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

        for (auto const index : selectedIndices)
        {
            items.push_back(ActivePane().items[static_cast<std::size_t>(index)]);
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
            return;
        }

        std::vector<std::wstring> selectedIds;
        selectedIds.reserve(selectedIndices.size());
        for (auto const index : selectedIndices)
        {
            selectedIds.push_back(ActivePane().items[static_cast<std::size_t>(index)].parsing_name);
        }

        std::optional<std::wstring> focusedId;
        auto const focusedIndex = ActivePane().active_list_view.SelectedIndex();
        if (focusedIndex >= 0 && focusedIndex < static_cast<int32_t>(ActivePane().items.size()))
        {
            focusedId = ActivePane().items[static_cast<std::size_t>(focusedIndex)].parsing_name;
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
        summary = AppendArchiveBrowseHint(std::move(summary), item);

        StatusTextBlock().Text(ToHString(L"Selected " + item.display_name + L"  |  " + summary));
    }

    void MainWindow::UpdateMultiSelectionDetails(std::vector<int32_t> const& indices)
    {
        auto const itemCount = indices.size();
        auto const folderCount = static_cast<std::size_t>(std::count_if(
            indices.begin(),
            indices.end(),
            [this](int32_t const index)
            {
                return ActivePane().items[static_cast<std::size_t>(index)].is_folder;
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
        metadata += L"  |  F2 to batch rename";
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
        }
    }

    void MainWindow::ExecuteContextCommand(
        std::wstring const& commandId,
        std::optional<int32_t> const itemIndex)
    {
        auto const selectedItems = GetSelectedItems();
        std::optional<::FileScoutNative::Domain::ShellItemRef> item;
        if (itemIndex.has_value() && *itemIndex >= 0 &&
            *itemIndex < static_cast<int32_t>(ActivePane().items.size()))
        {
            item = ActivePane().items[static_cast<std::size_t>(*itemIndex)];
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

            if (item.has_value() &&
                (item->is_folder ||
                 (!ActivePane().is_archive_view && m_shellInterop.IsArchivePath(item->path))))
            {
                LoadDirectory(item->path);
                return;
            }

            if (!item.has_value())
            {
                StatusTextBlock().Text(L"Open needs a selected item.");
                return;
            }

            if (ActivePane().is_archive_view)
            {
                OpenArchiveItemAsync(*item);
                return;
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
                    StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Opened", item)));
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
                    StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Open failed for", item)));
                }
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
                StatusTextBlock().Text(L"Pin to Places needs a folder.");
                return;
            }

            if (m_favoritesService.IsSidebarFavorite(targetPath))
            {
                StatusTextBlock().Text(ToHString(
                    targetPath.filename().wstring().empty()
                        ? L"That place is already in the sidebar."
                        : targetPath.filename().wstring() + L" is already in Places."));
                return;
            }

            if (m_favoritesService.AddPinnedFavorite(targetPath))
            {
                BuildSidebarFavorites();
                auto const label = targetPath.filename().wstring().empty()
                    ? targetPath.wstring()
                    : targetPath.filename().wstring();
                StatusTextBlock().Text(ToHString(L"Pinned " + label + L" to Places."));
            }
            else
            {
                StatusTextBlock().Text(L"Pin to Places failed.");
            }
            return;
        }

        if (commandId == L"refresh")
        {
            LoadDirectory(ActivePane().current_path);
            return;
        }

        if (commandId == L"extractToArchiveFolder" || commandId == L"extractReveal")
        {
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
        catch (std::exception const&)
        {
            StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Rename failed for", item)));
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
        catch (std::exception const&)
        {
            StatusTextBlock().Text(ToHString(
                L"Batch rename failed for " + std::to_wstring(requests.size()) +
                (requests.size() == 1 ? L" item." : L" items.")));
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
            co_return;
        }

        try
        {
            if (items.size() == 1)
            {
                m_shellFileOperationService.DeleteItem(GetWindowHandle(), items.front().path);
            }
            else
            {
                std::vector<std::filesystem::path> paths;
                paths.reserve(items.size());
                for (auto const& item : items)
                {
                    paths.push_back(item.path);
                }

                m_shellFileOperationService.DeleteItems(
                    GetWindowHandle(),
                    std::span<std::filesystem::path const>{ paths.data(), paths.size() });
            }
            LoadDirectory(ActivePane().current_path);
            if (items.size() == 1)
            {
                StatusTextBlock().Text(ToHString(
                    L"Deleted " + items.front().display_name + L" to Recycle Bin"));
            }
            else
            {
                StatusTextBlock().Text(ToHString(
                    L"Deleted " + std::to_wstring(items.size()) + L" items to Recycle Bin"));
            }
        }
        catch (std::exception const&)
        {
            if (items.size() == 1)
            {
                StatusTextBlock().Text(ToHString(BuildCommandStatus(L"Delete failed for", items.front())));
            }
            else
            {
                StatusTextBlock().Text(ToHString(
                    L"Delete failed for " + std::to_wstring(items.size()) + L" items."));
            }
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

        // F4 — reveal address TextBox for direct path entry.
        {
            auto accel = KA{};
            accel.Key(VK::F4);
            accel.Modifiers(VKM::None);
            accel.Invoked([this](winrt::Windows::Foundation::IInspectable const&, KAArgs const& e)
            {
                e.Handled(true);
                ShowAddressTextBox();
            });
            accelerators.Append(accel);
        }

        // Ctrl+P — open command palette.
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

    // R13b: Navigation history ────────────────────────────────────────────

    void MainWindow::NavigateBack()
    {
        auto& pane = ActivePane();
        if (pane.back_stack.empty())
        {
            StatusTextBlock().Text(L"No back history.");
            return;
        }
        auto const prev = pane.back_stack.back();
        pane.back_stack.pop_back();
        pane.forward_stack.push_back(pane.current_path);
        LoadDirectory(prev, std::nullopt, /*skipHistoryRecord=*/true);
    }

    void MainWindow::NavigateForward()
    {
        auto& pane = ActivePane();
        if (pane.forward_stack.empty())
        {
            StatusTextBlock().Text(L"No forward history.");
            return;
        }
        auto const next = pane.forward_stack.back();
        pane.forward_stack.pop_back();
        pane.back_stack.push_back(pane.current_path);
        LoadDirectory(next, std::nullopt, /*skipHistoryRecord=*/true);
    }

    // ────────────────────────────────────────────────────────────────────────

    // R13a: Command palette ────────────────────────────────────────────────

    void MainWindow::ShowCommandPalette()
    {
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
            winrt::Windows::Foundation::IInspectable> const& source,
        int32_t const paneIndex)
    {
        auto listView = winrt::Microsoft::UI::Xaml::Controls::ListView{};
        listView.Margin({ 12, 0, 12, 12 });
        listView.AllowDrop(true);
        listView.CanDrag(true);
        listView.SelectionMode(winrt::Microsoft::UI::Xaml::Controls::ListViewSelectionMode::Extended);
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

        listView.KeyDown([this, paneIndex](auto const&, auto const& a)
            {
                SetActivePaneIndex(paneIndex);
                FileRowsListView_KeyDown(a);
            });

        return listView;
    }

    void MainWindow::FileRowsListView_KeyDown(
        winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
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
                    pane.active_list_view.ScrollIntoView(
                        pane.active_list_view.Items().GetAt(static_cast<uint32_t>(idx)));
                args.Handled(true);
                return;
            }
        }
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

        auto const selectedIds = selection.SelectedIds();
        if (!selectedIds.empty())
        {
            for (std::size_t index = 0; index < pane.items.size(); ++index)
            {
                if (!selection.IsSelected(pane.items[index].parsing_name))
                {
                    continue;
                }

                pane.active_list_view.SelectRange(
                    winrt::Microsoft::UI::Xaml::Data::ItemIndexRange(
                        static_cast<int32_t>(index),
                        1));
            }
        }
        else
        {
            pane.active_list_view.SelectedIndex(fallbackSelectedIndex);
        }

        pane.tab_switching = false;
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

        // Exit search mode on the outgoing pane before switching.
        auto& outgoing = m_panes[m_activePaneIndex];
        if (outgoing.search_mode != SearchMode::None)
        {
            outgoing.search_service.Cancel();
            SearchProgressForPane(m_activePaneIndex).Visibility(
                winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            outgoing.search_mode = SearchMode::None;
            outgoing.search_query.clear();
            // (ListView restore happens implicitly when the pane regains focus later.)
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

        namespace MUI = winrt::Microsoft::UI::Xaml::Controls;

        for (std::size_t i = 0; i < segments.size(); ++i)
        {
            auto const& seg = segments[i];
            std::wstring label = (i == 0)
                ? seg.root_name().wstring()    // "C:"
                : seg.filename().wstring();    // "Documents"
            if (label.empty()) label = seg.wstring();

            // Separator " › " between segments.
            if (i > 0)
            {
                MUI::TextBlock sep;
                sep.Text(L" › ");
                sep.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
                sep.Opacity(0.4);
                sep.FontSize(12);
                panel.Children().Append(sep);
            }

            MUI::HyperlinkButton btn;
            btn.Padding({ 4, 0, 4, 0 });
            MUI::TextBlock lbl;
            lbl.Text(winrt::hstring(label));
            lbl.FontSize(12);
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
    }

    void MainWindow::ShowAddressTextBox()
    {
        BreadcrumbBarBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        AddressTextBox().Text(ToHString(ActivePane().current_path.wstring()));
        AddressTextBox().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        AddressTextBox().SelectAll();
        AddressTextBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::HideAddressTextBox()
    {
        AddressTextBox().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        BreadcrumbBarBorder().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
    }

    void MainWindow::UpdateSearchPlaceholder(std::filesystem::path const& path)
    {
        auto const folderName = path.filename().empty()
            ? path.wstring()
            : path.filename().wstring();
        SearchTextBox().PlaceholderText(L"Search " + folderName + L"\u2026");
    }

    winrt::Windows::Foundation::IInspectable MainWindow::CreateSearchRowView(
        ::FileScoutNative::Search::SearchResult const& result,
        std::filesystem::path const& searchRoot)
    {
        // Build relative path subtitle (e.g. "src\components\").
        std::wstring subtitle;
        std::error_code ec;
        auto const rel = std::filesystem::relative(
            std::filesystem::path(result.full_path).parent_path(), searchRoot, ec);
        if (!ec && !rel.empty() && rel.wstring() != L".")
            subtitle = rel.wstring() + L"\\";

        namespace MUI = winrt::Microsoft::UI::Xaml::Controls;
        MUI::StackPanel panel;
        panel.Margin({ 12, 4, 12, 4 });

        MUI::TextBlock nameText;
        nameText.Text(winrt::hstring(result.filename));
        nameText.FontSize(13);
        panel.Children().Append(nameText);

        if (!subtitle.empty())
        {
            MUI::TextBlock subText;
            subText.Text(winrt::hstring(subtitle));
            subText.FontSize(11);
            subText.Opacity(0.6);
            panel.Children().Append(subText);
        }

        return panel.as<winrt::Windows::Foundation::IInspectable>();
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
            bool const isActive = (pane.view_mode == targetMode);
            auto const capturedMode = targetMode;
            addHeaderButton(label, isActive, [this, paneIndex, capturedMode](auto const&, auto const&)
                {
                    SetActivePaneIndex(paneIndex);
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

        StatusTextBlock().Text(ToHString(
            std::to_wstring(pane.items.size()) +
            (enabled
                ? L" items  |  grouped by " + SortColumnLabel(pane.sort_column)
                : L" items  |  grouping off")));
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
            (mode == ViewMode::Cards ? L" items  |  cards view" : L" items  |  list view")));
    }

    void MainWindow::RebuildAllTabRowsForPane(int32_t const paneIndex)
    {
        auto& pane = m_panes[paneIndex];
        // Cancel any pending staged-publish dispatch — this rebuild replaces it entirely.
        pane.awaiting_final_batch = false;
        bool const listMode = (pane.view_mode == ViewMode::List);

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
                std::optional<std::wstring> groupLabel;
                if (pane.group_by_sort_column)
                {
                    auto currentGroupLabel = BuildGroupLabel(item, pane.sort_column);
                    if (rows.Size() == 0 || currentGroupLabel != previousGroupLabel)
                    {
                        groupLabel = currentGroupLabel;
                    }
                    previousGroupLabel = std::move(currentGroupLabel);
                }

                rows.Append(CreateRowView(item, listMode, groupLabel));
            }
        }
    }

    void MainWindow::BuildSidebarFavorites()
    {
        auto const panel = SidebarPlacesPanel();
        panel.Children().Clear();
        bool addedPinnedHeader = false;
        bool addedPrimaryHeader = false;
        auto const& activePane = ActivePane();
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
            if (!entry.is_pinned && !addedPrimaryHeader)
            {
                auto quickAccessHeader = BuildSidebarSectionLabel(L"QUICK ACCESS");
                quickAccessHeader.Margin({ 2, 0, 0, 2 });
                panel.Children().Append(quickAccessHeader);
                addedPrimaryHeader = true;
            }

            if (entry.is_pinned && !addedPinnedHeader)
            {
                auto separator = winrt::Microsoft::UI::Xaml::Controls::Border{};
                separator.Height(1);
                separator.Margin({ 0, 8, 0, 8 });
                separator.Background(LookupBrush(L"DividerStrokeColorDefaultBrush"));
                panel.Children().Append(separator);

                auto pinnedHeader = BuildSidebarSectionLabel(L"PINNED");
                pinnedHeader.Margin({ 2, 0, 0, 2 });
                panel.Children().Append(pinnedHeader);
                addedPinnedHeader = true;
            }

            auto btn = winrt::Microsoft::UI::Xaml::Controls::Button{};
            StyleSidebarButtonBase(btn);
            btn.Margin({ 0, 0, 0, 2 });

            auto const knownFolderDisplayName = entry.is_pinned ? std::wstring{} : entry.display_name;
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
                BuildSidebarGlyph(knownFolderDisplayName, entryIcon, SidebarFavoriteFallbackLabel(entry)),
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

            if (entry.is_pinned)
            {
                auto badge = BuildSidebarFontIcon(L"\uE718", 11.0);
                badge.Opacity(0.58);
                winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(badge, 2);
                rowGrid.Children().Append(badge);
            }

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

            panel.Children().Append(btn);
        }

        auto const drives = BuildSidebarDriveUsage(m_shellInterop);
        if (!drives.empty())
        {
            auto separator = winrt::Microsoft::UI::Xaml::Controls::Border{};
            separator.Height(1);
            separator.Margin({ 0, 10, 0, 8 });
            separator.Background(LookupBrush(L"DividerStrokeColorDefaultBrush"));
            panel.Children().Append(separator);

            auto storageLabel = BuildSidebarSectionLabel(L"STORAGE");
            storageLabel.Margin({ 2, 0, 0, 2 });
            panel.Children().Append(storageLabel);

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

                auto iconTile = BuildSidebarTile(
                    BuildSidebarDriveGlyph(drive),
                    18.0);

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
                    auto bar = winrt::Microsoft::UI::Xaml::Controls::ProgressBar{};
                    bar.Minimum(0);
                    bar.Maximum(100);
                    bar.Value(drive.used_ratio * 100.0);
                    bar.Height(4);
                    bar.Background(LookupBrush(L"SubtleFillColorSecondaryBrush"));
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

            StatusTextBlock().Text(
                ToHString(std::to_wstring(items.size()) + L" drive(s) available."));
        }
        catch (...)
        {
            StatusTextBlock().Text(L"Drive enumeration failed.");
        }
    }
}
