#include "pch.h"
#include "FavoritesService.h"

namespace FileScoutNative::Shell
{
    namespace
    {
        // RAII wrapper around the CoTaskMem string returned by SHGetKnownFolderPath.
        struct CoTaskMemWString
        {
            wchar_t* ptr{ nullptr };
            CoTaskMemWString() = default;
            CoTaskMemWString(CoTaskMemWString const&) = delete;
            CoTaskMemWString& operator=(CoTaskMemWString const&) = delete;
            ~CoTaskMemWString() { if (ptr) CoTaskMemFree(ptr); }
        };
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

        // Fallback: USERPROFILE environment variable.
        wchar_t profileBuf[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", profileBuf, MAX_PATH) > 0)
        {
            return std::filesystem::path{ profileBuf };
        }

        return std::filesystem::current_path();
    }

    std::vector<FileScoutNative::Domain::FavoriteEntry> FavoritesService::GetBuiltinFavorites() const
    {
        using FE = FileScoutNative::Domain::FavoriteEntry;

        std::vector<FE> favorites;

        // Home folder.
        auto homePath = GetUserHomePath();
        if (!homePath.empty())
        {
            favorites.push_back({ L"Home", homePath, false, false });
        }

        // Standard known folders — skip entries whose paths could not be resolved.
        struct KnownFolderSpec
        {
            KNOWNFOLDERID id;
            std::wstring_view name;
        };

        // KNOWNFOLDERID is a GUID (16 bytes); stored by value to avoid pointer-to-external-linkage.
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
                favorites.push_back({ std::wstring{ spec.name }, std::move(path), false, false });
            }
        }

        // Virtual nodes.
        favorites.push_back({ L"This PC",     {}, true,  false });
        favorites.push_back({ L"Recycle Bin", {}, false, true  });

        return favorites;
    }
}
