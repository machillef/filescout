#include "pch.h"
#include "UserSettings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>

namespace FileScoutNative::Domain
{
    namespace
    {
        constexpr int32_t kMinRestoredWidth = 400;
        constexpr int32_t kMinRestoredHeight = 300;
        constexpr int32_t kMinClampedWidth = 480;
        constexpr int32_t kMinClampedHeight = 360;
        constexpr int32_t kMaxWindowCoord = 100000;
        constexpr int32_t kMaxWindowDimension = 32768;

        std::optional<std::wstring> ReadEnvironmentVariable(wchar_t const* name)
        {
            std::wstring buffer(MAX_PATH, L'\0');
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                auto const length = ::GetEnvironmentVariableW(
                    name, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0)
                {
                    return std::nullopt;
                }

                if (length < buffer.size())
                {
                    buffer.resize(length);
                    return buffer;
                }

                buffer.resize(length);
            }

            return std::nullopt;
        }

        std::filesystem::path WithLongPathPrefix(std::filesystem::path path)
        {
            auto const& native = path.native();
            if (native.size() < 248 || native.starts_with(L"\\\\?\\"))
            {
                return path;
            }

            if (native.starts_with(L"\\\\"))
            {
                return std::filesystem::path{ L"\\\\?\\UNC\\" + native.substr(2) };
            }

            return std::filesystem::path{ L"\\\\?\\" + native };
        }

        bool EnsureDirectory(std::filesystem::path const& directory)
        {
            std::error_code ec;
            if (std::filesystem::is_directory(directory, ec))
            {
                return true;
            }

            auto const parent = directory.parent_path();
            if (parent.empty() || parent == directory || !EnsureDirectory(parent))
            {
                return false;
            }

            if (::CreateDirectoryW(directory.c_str(), nullptr) != FALSE)
            {
                return true;
            }

            return ::GetLastError() == ERROR_ALREADY_EXISTS &&
                std::filesystem::is_directory(directory, ec);
        }

        char const* ViewModeToString(SavedViewMode const mode) noexcept
        {
            return mode == SavedViewMode::Cards ? "cards" : "list";
        }

        char const* SortColumnToString(SavedSortColumn const col) noexcept
        {
            switch (col)
            {
            case SavedSortColumn::Name: return "name";
            case SavedSortColumn::Type: return "type";
            case SavedSortColumn::Size: return "size";
            case SavedSortColumn::DateModified: return "date";
            }
            return "name";
        }

        char const* SortDirectionToString(SavedSortDirection const dir) noexcept
        {
            return dir == SavedSortDirection::Descending ? "desc" : "asc";
        }

        void SkipWhitespace(std::string_view const text, std::size_t& pos) noexcept
        {
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
            {
                ++pos;
            }
        }

        std::optional<std::string> ParseJsonString(std::string_view const text, std::size_t& pos)
        {
            SkipWhitespace(text, pos);
            if (pos >= text.size() || text[pos] != '"')
            {
                return std::nullopt;
            }
            ++pos;
            std::string result;
            while (pos < text.size())
            {
                char const ch = text[pos++];
                if (ch == '"')
                {
                    return result;
                }
                if (ch == '\\')
                {
                    if (pos >= text.size())
                    {
                        return std::nullopt;
                    }
                    char const esc = text[pos++];
                    switch (esc)
                    {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    default: result.push_back(esc); break;
                    }
                }
                else
                {
                    result.push_back(ch);
                }
            }
            return std::nullopt;
        }

        std::optional<bool> ParseJsonBool(std::string_view const text, std::size_t& pos) noexcept
        {
            SkipWhitespace(text, pos);
            if (text.substr(pos, 4) == "true")
            {
                pos += 4;
                return true;
            }
            if (text.substr(pos, 5) == "false")
            {
                pos += 5;
                return false;
            }
            return std::nullopt;
        }

        std::optional<double> ParseJsonNumber(std::string_view const text, std::size_t& pos)
        {
            SkipWhitespace(text, pos);
            std::size_t const start = pos;
            while (pos < text.size())
            {
                char const ch = text[pos];
                if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E')
                {
                    ++pos;
                }
                else
                {
                    break;
                }
            }
            if (pos == start)
            {
                return std::nullopt;
            }
            std::string token{ text.substr(start, pos - start) };
            char* endPtr = nullptr;
            double const value = std::strtod(token.c_str(), &endPtr);
            if (endPtr != token.c_str() + token.size() || !std::isfinite(value))
            {
                return std::nullopt;
            }
            return value;
        }

        std::optional<int32_t> ParseJsonInt32(std::string_view const text, std::size_t& pos)
        {
            auto const number = ParseJsonNumber(text, pos);
            if (!number.has_value())
            {
                return std::nullopt;
            }
            double const rounded = std::round(*number);
            if (rounded < static_cast<double>((std::numeric_limits<int32_t>::min)()) ||
                rounded > static_cast<double>((std::numeric_limits<int32_t>::max)()))
            {
                return std::nullopt;
            }
            return static_cast<int32_t>(rounded);
        }

        bool SkipJsonValue(std::string_view const text, std::size_t& pos)
        {
            SkipWhitespace(text, pos);
            if (pos >= text.size())
            {
                return false;
            }
            char const ch = text[pos];
            if (ch == '"')
            {
                return ParseJsonString(text, pos).has_value();
            }
            if (ch == '{' || ch == '[')
            {
                char const openCh = ch;
                char const closeCh = (openCh == '{') ? '}' : ']';
                int depth = 0;
                while (pos < text.size())
                {
                    char const c = text[pos];
                    if (c == '"')
                    {
                        if (!ParseJsonString(text, pos).has_value())
                        {
                            return false;
                        }
                        continue;
                    }
                    if (c == openCh) { ++depth; }
                    else if (c == closeCh)
                    {
                        --depth;
                        ++pos;
                        if (depth == 0) { return true; }
                        continue;
                    }
                    ++pos;
                }
                return false;
            }
            if (text.substr(pos, 4) == "true") { pos += 4; return true; }
            if (text.substr(pos, 5) == "false") { pos += 5; return true; }
            if (text.substr(pos, 4) == "null") { pos += 4; return true; }
            return ParseJsonNumber(text, pos).has_value();
        }

        int64_t IntersectionArea(WindowBounds const& bounds, ScreenWorkArea const& work) noexcept
        {
            int64_t const bLeft = bounds.x;
            int64_t const bTop = bounds.y;
            int64_t const bRight = static_cast<int64_t>(bounds.x) + bounds.width;
            int64_t const bBottom = static_cast<int64_t>(bounds.y) + bounds.height;

            int64_t const left = (std::max)(bLeft, static_cast<int64_t>(work.left));
            int64_t const top = (std::max)(bTop, static_cast<int64_t>(work.top));
            int64_t const right = (std::min)(bRight, static_cast<int64_t>(work.right));
            int64_t const bottom = (std::min)(bBottom, static_cast<int64_t>(work.bottom));

            if (right <= left || bottom <= top)
            {
                return 0;
            }
            return (right - left) * (bottom - top);
        }

        bool IsValidWorkArea(ScreenWorkArea const& work) noexcept
        {
            return work.right > work.left && work.bottom > work.top;
        }
    }

    std::string SerializeUserSettings(UserSettings const& settings)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::fixed << std::setprecision(1);
        out << "{\n";
        out << "  \"version\": 1,\n";
        out << "  \"has_window_bounds\": " << (settings.window.has_bounds ? "true" : "false") << ",\n";
        out << "  \"window_x\": " << settings.window.x << ",\n";
        out << "  \"window_y\": " << settings.window.y << ",\n";
        out << "  \"window_width\": " << settings.window.width << ",\n";
        out << "  \"window_height\": " << settings.window.height << ",\n";
        out << "  \"window_maximized\": " << (settings.window.maximized ? "true" : "false") << ",\n";
        out << "  \"col_name\": " << settings.list_column_widths.name << ",\n";
        out << "  \"col_type\": " << settings.list_column_widths.type << ",\n";
        out << "  \"col_size\": " << settings.list_column_widths.size << ",\n";
        out << "  \"col_date\": " << settings.list_column_widths.date << ",\n";
        out << "  \"view_mode\": \"" << ViewModeToString(settings.view_mode) << "\",\n";
        out << "  \"show_hidden_files\": " << (settings.show_hidden_files ? "true" : "false") << ",\n";
        out << "  \"show_file_extensions\": " << (settings.show_file_extensions ? "true" : "false") << ",\n";
        out << "  \"group_by_sort_column\": " << (settings.group_by_sort_column ? "true" : "false") << ",\n";
        if (settings.has_dual_pane_setting)
        {
            out << "  \"dual_pane_visible\": " << (settings.dual_pane_visible ? "true" : "false") << ",\n";
        }
        out << "  \"sort_column\": \"" << SortColumnToString(settings.sort_column) << "\",\n";
        out << "  \"sort_direction\": \"" << SortDirectionToString(settings.sort_direction) << "\"\n";
        out << "}\n";
        return out.str();
    }

    UserSettings DeserializeUserSettings(std::string_view const json)
    {
        UserSettings result{};
        std::size_t pos = 0;
        SkipWhitespace(json, pos);
        if (pos >= json.size() || json[pos] != '{')
        {
            return result;
        }
        ++pos;

        bool parsedHasBounds = false;
        bool parsedX = false;
        bool parsedY = false;
        bool parsedW = false;
        bool parsedH = false;

        while (pos < json.size())
        {
            SkipWhitespace(json, pos);
            if (pos >= json.size() || json[pos] == '}')
            {
                break;
            }

            auto const key = ParseJsonString(json, pos);
            if (!key.has_value())
            {
                break;
            }

            SkipWhitespace(json, pos);
            if (pos >= json.size() || json[pos] != ':')
            {
                break;
            }
            ++pos;

            if (*key == "has_window_bounds")
            {
                if (auto const v = ParseJsonBool(json, pos)) { parsedHasBounds = *v; }
                else { break; }
            }
            else if (*key == "window_x")
            {
                if (auto const v = ParseJsonInt32(json, pos))
                {
                    if (*v >= -kMaxWindowCoord && *v <= kMaxWindowCoord)
                    {
                        result.window.x = *v;
                        parsedX = true;
                    }
                }
                else { break; }
            }
            else if (*key == "window_y")
            {
                if (auto const v = ParseJsonInt32(json, pos))
                {
                    if (*v >= -kMaxWindowCoord && *v <= kMaxWindowCoord)
                    {
                        result.window.y = *v;
                        parsedY = true;
                    }
                }
                else { break; }
            }
            else if (*key == "window_width")
            {
                if (auto const v = ParseJsonInt32(json, pos))
                {
                    if (*v >= kMinRestoredWidth && *v <= kMaxWindowDimension)
                    {
                        result.window.width = *v;
                        parsedW = true;
                    }
                }
                else { break; }
            }
            else if (*key == "window_height")
            {
                if (auto const v = ParseJsonInt32(json, pos))
                {
                    if (*v >= kMinRestoredHeight && *v <= kMaxWindowDimension)
                    {
                        result.window.height = *v;
                        parsedH = true;
                    }
                }
                else { break; }
            }
            else if (*key == "window_maximized")
            {
                if (auto const v = ParseJsonBool(json, pos)) { result.window.maximized = *v; }
                else { break; }
            }
            else if (*key == "col_name")
            {
                if (auto const v = ParseJsonNumber(json, pos))
                {
                    result.list_column_widths = SetListColumnWidth(result.list_column_widths, ListColumnIndex::Name, *v);
                }
                else { break; }
            }
            else if (*key == "col_type")
            {
                if (auto const v = ParseJsonNumber(json, pos))
                {
                    result.list_column_widths = SetListColumnWidth(result.list_column_widths, ListColumnIndex::Type, *v);
                }
                else { break; }
            }
            else if (*key == "col_size")
            {
                if (auto const v = ParseJsonNumber(json, pos))
                {
                    result.list_column_widths = SetListColumnWidth(result.list_column_widths, ListColumnIndex::Size, *v);
                }
                else { break; }
            }
            else if (*key == "col_date")
            {
                if (auto const v = ParseJsonNumber(json, pos))
                {
                    result.list_column_widths = SetListColumnWidth(result.list_column_widths, ListColumnIndex::DateModified, *v);
                }
                else { break; }
            }
            else if (*key == "view_mode")
            {
                if (auto const v = ParseJsonString(json, pos))
                {
                    if (*v == "cards") { result.view_mode = SavedViewMode::Cards; }
                    else if (*v == "list") { result.view_mode = SavedViewMode::List; }
                }
                else { break; }
            }
            else if (*key == "show_hidden_files")
            {
                if (auto const v = ParseJsonBool(json, pos)) { result.show_hidden_files = *v; }
                else { break; }
            }
            else if (*key == "show_file_extensions")
            {
                if (auto const v = ParseJsonBool(json, pos)) { result.show_file_extensions = *v; }
                else { break; }
            }
            else if (*key == "group_by_sort_column")
            {
                if (auto const v = ParseJsonBool(json, pos)) { result.group_by_sort_column = *v; }
                else { break; }
            }
            else if (*key == "dual_pane_visible")
            {
                if (auto const v = ParseJsonBool(json, pos))
                {
                    result.dual_pane_visible = *v;
                    result.has_dual_pane_setting = true;
                }
                else { break; }
            }
            else if (*key == "sort_column")
            {
                if (auto const v = ParseJsonString(json, pos))
                {
                    if (*v == "name") { result.sort_column = SavedSortColumn::Name; }
                    else if (*v == "type") { result.sort_column = SavedSortColumn::Type; }
                    else if (*v == "size") { result.sort_column = SavedSortColumn::Size; }
                    else if (*v == "date") { result.sort_column = SavedSortColumn::DateModified; }
                }
                else { break; }
            }
            else if (*key == "sort_direction")
            {
                if (auto const v = ParseJsonString(json, pos))
                {
                    if (*v == "desc") { result.sort_direction = SavedSortDirection::Descending; }
                    else if (*v == "asc") { result.sort_direction = SavedSortDirection::Ascending; }
                }
                else { break; }
            }
            else
            {
                if (!SkipJsonValue(json, pos))
                {
                    break;
                }
            }

            SkipWhitespace(json, pos);
            if (pos < json.size() && json[pos] == ',')
            {
                ++pos;
            }
            else if (pos < json.size() && json[pos] == '}')
            {
                break;
            }
            else
            {
                break;
            }
        }

        result.window.has_bounds = parsedHasBounds && parsedX && parsedY && parsedW && parsedH;
        if (!result.window.has_bounds)
        {
            result.window = WindowBounds{ .maximized = result.window.maximized };
        }

        return result;
    }

    WindowBounds ResolveRestoredWindowBounds(
        WindowBounds const& saved,
        std::vector<ScreenWorkArea> const& monitors,
        ScreenWorkArea const& primaryWorkArea,
        int32_t const defaultWidth,
        int32_t const defaultHeight) noexcept
    {
        ScreenWorkArea target = primaryWorkArea;
        if (!IsValidWorkArea(target))
        {
            for (auto const& mon : monitors)
            {
                if (IsValidWorkArea(mon))
                {
                    target = mon;
                    break;
                }
            }
        }

        if (!IsValidWorkArea(target))
        {
            return WindowBounds{
                .x = 0,
                .y = 0,
                .width = (std::max)(defaultWidth, kMinClampedWidth),
                .height = (std::max)(defaultHeight, kMinClampedHeight),
                .maximized = saved.maximized,
                .has_bounds = true,
            };
        }

        int32_t const primaryW = target.right - target.left;
        int32_t const primaryH = target.bottom - target.top;

        if (!saved.has_bounds || saved.width < kMinRestoredWidth || saved.height < kMinRestoredHeight)
        {
            int32_t const clampedW = (std::min)(defaultWidth, static_cast<int32_t>(primaryW * 0.94));
            int32_t const clampedH = (std::min)(defaultHeight, static_cast<int32_t>(primaryH * 0.94));
            int32_t const finalW = (std::max)(clampedW, (std::min)(kMinClampedWidth, primaryW));
            int32_t const finalH = (std::max)(clampedH, (std::min)(kMinClampedHeight, primaryH));
            return WindowBounds{
                .x = target.left + (primaryW - finalW) / 2,
                .y = target.top + (primaryH - finalH) / 2,
                .width = finalW,
                .height = finalH,
                .maximized = saved.maximized,
                .has_bounds = true,
            };
        }

        // Pick the monitor work area with the largest overlap with the saved rectangle.
        int64_t bestOverlap = 0;
        if (monitors.empty())
        {
            bestOverlap = IntersectionArea(saved, target);
        }
        else
        {
            for (auto const& mon : monitors)
            {
                if (!IsValidWorkArea(mon))
                {
                    continue;
                }
                int64_t const overlap = IntersectionArea(saved, mon);
                if (overlap > bestOverlap)
                {
                    bestOverlap = overlap;
                    target = mon;
                }
            }
        }

        int32_t const workW = target.right - target.left;
        int32_t const workH = target.bottom - target.top;
        int32_t const minW = (std::min)(kMinClampedWidth, workW);
        int32_t const minH = (std::min)(kMinClampedHeight, workH);
        int32_t const width = std::clamp(saved.width, minW, workW);
        int32_t const height = std::clamp(saved.height, minH, workH);

        int32_t const maxX = target.right - width;
        int32_t const maxY = target.bottom - height;
        int32_t const x = (bestOverlap > 0)
            ? std::clamp(saved.x, target.left, maxX)
            : target.left + (workW - width) / 2;
        int32_t const y = (bestOverlap > 0)
            ? std::clamp(saved.y, target.top, maxY)
            : target.top + (workH - height) / 2;

        return WindowBounds{
            .x = x,
            .y = y,
            .width = width,
            .height = height,
            .maximized = saved.maximized,
            .has_bounds = true,
        };
    }

    std::filesystem::path UserSettingsService::GetStoragePath()
    {
        if (auto const localAppData = ReadEnvironmentVariable(L"LOCALAPPDATA");
            localAppData.has_value())
        {
            std::filesystem::path const base{ *localAppData };
            if (base.is_absolute())
            {
                return WithLongPathPrefix(
                    (base / L"FileScoutNative" / L"settings.json").lexically_normal());
            }
        }

        std::error_code ec;
        return WithLongPathPrefix(std::filesystem::current_path(ec) / L"settings.json");
    }

    UserSettings UserSettingsService::Load()
    {
        auto const path = GetStoragePath();
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            return {};
        }

        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return {};
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        return DeserializeUserSettings(buffer.str());
    }

    bool UserSettingsService::Save(UserSettings const& settings)
    {
        auto const path = GetStoragePath();
        if (!EnsureDirectory(path.parent_path()))
        {
            return false;
        }

        auto const payload = SerializeUserSettings(settings);
        auto tempPath = path;
        tempPath += L"." + std::to_wstring(::GetCurrentProcessId()) +
            L"." + std::to_wstring(::GetCurrentThreadId()) + L".tmp";

        {
            std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                return false;
            }
            output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            output.flush();
            if (!output.good())
            {
                output.close();
                ::DeleteFileW(tempPath.c_str());
                return false;
            }
        }

        if (::MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE)
        {
            return true;
        }

        ::DeleteFileW(tempPath.c_str());
        return false;
    }
}
