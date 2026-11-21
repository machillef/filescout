#include "pch.h"
#include "FavoritesService.h"
#include "Domain/TextFold.h"
#include <cwctype>
#include <fstream>
#include <unordered_set>

namespace FileScoutNative::Shell
{
    namespace
    {
        struct CoTaskMemWString
        {
            wchar_t* ptr{ nullptr };
            CoTaskMemWString() = default;
            CoTaskMemWString(CoTaskMemWString const&) = delete;
            CoTaskMemWString& operator=(CoTaskMemWString const&) = delete;
            ~CoTaskMemWString() { if (ptr) CoTaskMemFree(ptr); }
        };

        std::filesystem::path NormalizePath(std::filesystem::path const& path)
        {
            std::error_code absoluteError;
            auto const absolute = std::filesystem::absolute(path, absoluteError);
            return absoluteError ? path.lexically_normal() : absolute.lexically_normal();
        }

        std::wstring PathKey(std::filesystem::path const& path)
        {
            return ::FileScoutNative::Domain::FoldCase(NormalizePath(path).wstring());
        }

        std::string ToUtf8(std::wstring_view const value)
        {
            if (value.empty())
            {
                return {};
            }

            auto const size = WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            std::string utf8(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                utf8.data(),
                size,
                nullptr,
                nullptr);
            return utf8;
        }

        std::wstring FromUtf8(std::string_view const value)
        {
            if (value.empty())
            {
                return {};
            }

            auto const size = MultiByteToWideChar(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            std::wstring wide(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                wide.data(),
                size);
            return wide;
        }

        std::wstring BuildDisplayNameForPinnedPath(std::filesystem::path const& path)
        {
            auto const normalized = NormalizePath(path);
            auto label = normalized.filename().wstring();
            if (label.empty())
            {
                label = normalized.root_name().wstring();
            }

            return label.empty() ? normalized.wstring() : label;
        }
    }

    std::filesystem::path FavoritesService::ResolveKnownFolder(KNOWNFOLDERID const& id)
    {
        CoTaskMemWString result;
        const HRESULT hr = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &result.ptr);
        if (FAILED(hr) || result.ptr == nullptr)
        {
            return {};
        }

        return std::filesystem::path{ result.ptr };
    }

    std::filesystem::path FavoritesService::GetUserHomePath() const
    {
        auto home = ResolveKnownFolder(FOLDERID_Profile);
        if (!home.empty())
        {
            return home;
        }

        wchar_t profileBuf[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", profileBuf, MAX_PATH) > 0)
        {
            return std::filesystem::path{ profileBuf };
        }

        return std::filesystem::current_path();
    }

    std::filesystem::path FavoritesService::GetPinnedFavoritesStoragePath()
    {
        wchar_t localAppDataBuf[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppDataBuf, MAX_PATH) > 0)
        {
            return std::filesystem::path{ localAppDataBuf } /
                L"FileScoutNative" /
                L"pinned-places.txt";
        }

        return std::filesystem::current_path() / L"pinned-places.txt";
    }

    std::vector<std::filesystem::path> FavoritesService::LoadPinnedFavoritePaths()
    {
        std::vector<std::filesystem::path> paths;
        auto const storagePath = GetPinnedFavoritesStoragePath();
        if (!std::filesystem::exists(storagePath))
        {
            return paths;
        }

        std::ifstream input(storagePath, std::ios::binary);
        if (!input)
        {
            return paths;
        }

        std::unordered_set<std::wstring> seen;
        std::string line;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            if (line.empty())
            {
                continue;
            }

            auto path = NormalizePath(std::filesystem::path{ FromUtf8(line) });
            auto const key = PathKey(path);
            if (!path.empty() && seen.insert(key).second)
            {
                paths.push_back(std::move(path));
            }
        }

        return paths;
    }

    void FavoritesService::SavePinnedFavoritePaths(std::vector<std::filesystem::path> const& paths)
    {
        auto const storagePath = GetPinnedFavoritesStoragePath();
        std::filesystem::create_directories(storagePath.parent_path());

        std::ofstream output(storagePath, std::ios::binary | std::ios::trunc);
        for (auto const& path : paths)
        {
            auto const normalized = NormalizePath(path);
            auto const utf8 = ToUtf8(normalized.wstring());
            output.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
            output.put('\n');
        }
    }

    std::vector<FileScoutNative::Domain::FavoriteEntry> FavoritesService::GetSidebarFavorites() const
    {
        auto favorites = GetBuiltinFavorites();
        std::unordered_set<std::wstring> seen;
        for (auto const& favorite : favorites)
        {
            if (!favorite.path.empty())
            {
                seen.insert(PathKey(favorite.path));
            }
        }

        for (auto const& path : LoadPinnedFavoritePaths())
        {
            auto const key = PathKey(path);
            if (seen.insert(key).second)
            {
                favorites.push_back(FileScoutNative::Domain::FavoriteEntry{
                    .display_name = BuildDisplayNameForPinnedPath(path),
                    .path = path,
                    .is_pinned = true,
                });
            }
        }

        return favorites;
    }

    std::vector<FileScoutNative::Domain::FavoriteEntry> FavoritesService::GetBuiltinFavorites() const
    {
        using FE = FileScoutNative::Domain::FavoriteEntry;

        std::vector<FE> favorites;

        auto homePath = GetUserHomePath();
        if (!homePath.empty())
        {
            favorites.push_back({ L"Home", homePath, false, false, false });
        }

        struct KnownFolderSpec
        {
            KNOWNFOLDERID id;
            std::wstring_view name;
        };

        static std::array<KnownFolderSpec, 6> const knownFolders{ {
            { FOLDERID_Desktop,   L"Desktop"   },
            { FOLDERID_Documents, L"Documents" },
            { FOLDERID_Downloads, L"Downloads" },
            { FOLDERID_Pictures,  L"Pictures"  },
            { FOLDERID_Music,     L"Music"     },
            { FOLDERID_Videos,    L"Videos"    },
        } };

        for (auto const& spec : knownFolders)
        {
            auto path = ResolveKnownFolder(spec.id);
            if (!path.empty())
            {
                favorites.push_back({ std::wstring{ spec.name }, std::move(path), false, false, false });
            }
        }

        favorites.push_back({ L"This PC",     {}, true,  false, false });
        favorites.push_back({ L"Recycle Bin", {}, false, true,  false });

        return favorites;
    }

    bool FavoritesService::AddPinnedFavorite(std::filesystem::path const& path) const
    {
        auto const normalized = NormalizePath(path);
        if (normalized.empty())
        {
            return false;
        }

        auto pinned = LoadPinnedFavoritePaths();
        auto const key = PathKey(normalized);

        std::unordered_set<std::wstring> seen;
        for (auto const& existing : GetBuiltinFavorites())
        {
            if (!existing.path.empty())
            {
                seen.insert(PathKey(existing.path));
            }
        }
        for (auto const& existing : pinned)
        {
            seen.insert(PathKey(existing));
        }

        if (seen.contains(key))
        {
            return false;
        }

        pinned.push_back(normalized);
        SavePinnedFavoritePaths(pinned);
        return true;
    }

    bool FavoritesService::RemovePinnedFavorite(std::filesystem::path const& path) const
    {
        auto const normalized = NormalizePath(path);
        if (normalized.empty())
        {
            return false;
        }

        auto pinned = LoadPinnedFavoritePaths();
        auto const key = PathKey(normalized);
        auto const original = pinned.size();
        pinned.erase(
            std::remove_if(pinned.begin(), pinned.end(),
                [&key](std::filesystem::path const& p) { return PathKey(p) == key; }),
            pinned.end());

        if (pinned.size() == original)
        {
            return false;
        }

        SavePinnedFavoritePaths(pinned);
        return true;
    }

    std::vector<FileScoutNative::Domain::FavoriteEntry> FavoritesService::GetPinnedFavorites() const
    {
        std::vector<FileScoutNative::Domain::FavoriteEntry> result;
        for (auto const& path : LoadPinnedFavoritePaths())
        {
            result.push_back(FileScoutNative::Domain::FavoriteEntry{
                .display_name = BuildDisplayNameForPinnedPath(path),
                .path = path,
                .is_pinned = true,
            });
        }
        return result;
    }

    bool FavoritesService::IsSidebarFavorite(std::filesystem::path const& path) const
    {
        auto const key = PathKey(path);
        for (auto const& favorite : GetSidebarFavorites())
        {
            if (!favorite.path.empty() && PathKey(favorite.path) == key)
            {
                return true;
            }
        }

        return false;
    }
}
