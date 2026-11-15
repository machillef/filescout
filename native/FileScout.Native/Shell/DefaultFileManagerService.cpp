#include "pch.h"
#include "DefaultFileManagerService.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <unordered_set>
#include <vector>

#include <shlobj.h>

namespace FileScoutNative::Shell
{
    namespace
    {
        constexpr wchar_t k_manifestValue[] = L"Entries";

        std::wstring Join(std::wstring_view root, std::wstring_view sub)
        {
            std::wstring result{ root };
            if (!sub.empty())
            {
                result += L'\\';
                result += sub;
            }
            return result;
        }

        bool KeyExists(std::wstring const& path)
        {
            HKEY key{};
            if (::RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
            {
                return false;
            }
            ::RegCloseKey(key);
            return true;
        }

        std::optional<std::wstring> ReadString(std::wstring const& path, std::wstring const& name)
        {
            DWORD type = 0;
            DWORD bytes = 0;
            LSTATUS status = ::RegGetValueW(HKEY_CURRENT_USER, path.c_str(), name.empty() ? nullptr : name.c_str(),
                RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &type, nullptr, &bytes);
            if (status != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            std::wstring buffer(bytes / sizeof(wchar_t) + 1, L'\0');
            status = ::RegGetValueW(HKEY_CURRENT_USER, path.c_str(), name.empty() ? nullptr : name.c_str(),
                RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &type, buffer.data(), &bytes);
            if (status != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            buffer.resize(wcsnlen(buffer.c_str(), buffer.size()));
            return buffer;
        }

        bool WriteString(std::wstring const& path, std::wstring const& name, std::wstring const& data)
        {
            HKEY key{};
            if (::RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            {
                return false;
            }
            auto const status = ::RegSetValueExW(key, name.empty() ? nullptr : name.c_str(), 0, REG_SZ,
                reinterpret_cast<BYTE const*>(data.c_str()), static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
            ::RegCloseKey(key);
            return status == ERROR_SUCCESS;
        }

        bool DeleteValue(std::wstring const& path, std::wstring const& name)
        {
            HKEY key{};
            auto status = ::RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_SET_VALUE, &key);
            if (status == ERROR_FILE_NOT_FOUND)
            {
                return true;
            }
            if (status != ERROR_SUCCESS)
            {
                return false;
            }
            status = ::RegDeleteValueW(key, name.empty() ? nullptr : name.c_str());
            ::RegCloseKey(key);
            return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
        }

        // Deletes the key only when it has no subkeys and no values left (something else may have
        // written there since FileScout created it; that data is not ours to remove).
        bool DeleteKeyIfEmpty(std::wstring const& path)
        {
            HKEY key{};
            auto status = ::RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key);
            if (status == ERROR_FILE_NOT_FOUND)
            {
                return true;
            }
            if (status != ERROR_SUCCESS)
            {
                return false;
            }
            DWORD subKeys = 0;
            DWORD values = 0;
            status = ::RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subKeys, nullptr, nullptr, &values,
                nullptr, nullptr, nullptr, nullptr);
            ::RegCloseKey(key);
            if (status != ERROR_SUCCESS || subKeys != 0 || values != 0)
            {
                return status == ERROR_SUCCESS;
            }
            status = ::RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str());
            return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
        }

        std::vector<std::wstring> ReadManifest(std::wstring const& manifestKey)
        {
            DWORD bytes = 0;
            if (::RegGetValueW(HKEY_CURRENT_USER, manifestKey.c_str(), k_manifestValue, RRF_RT_REG_MULTI_SZ,
                    nullptr, nullptr, &bytes) != ERROR_SUCCESS)
            {
                return {};
            }
            std::wstring buffer(bytes / sizeof(wchar_t) + 2, L'\0');
            if (::RegGetValueW(HKEY_CURRENT_USER, manifestKey.c_str(), k_manifestValue, RRF_RT_REG_MULTI_SZ,
                    nullptr, buffer.data(), &bytes) != ERROR_SUCCESS)
            {
                return {};
            }
            std::vector<std::wstring> lines;
            for (wchar_t const* cursor = buffer.c_str(); *cursor != L'\0'; cursor += wcslen(cursor) + 1)
            {
                lines.emplace_back(cursor);
            }
            return lines;
        }

        bool WriteManifest(std::wstring const& manifestKey, std::vector<std::wstring> const& lines)
        {
            std::wstring blob;
            for (auto const& line : lines)
            {
                blob += line;
                blob += L'\0';
            }
            blob += L'\0';
            HKEY key{};
            if (::RegCreateKeyExW(HKEY_CURRENT_USER, manifestKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            {
                return false;
            }
            auto const status = ::RegSetValueExW(key, k_manifestValue, 0, REG_MULTI_SZ,
                reinterpret_cast<BYTE const*>(blob.data()), static_cast<DWORD>(blob.size() * sizeof(wchar_t)));
            ::RegCloseKey(key);
            return status == ERROR_SUCCESS;
        }

        std::wstring FoldCase(std::wstring_view value)
        {
            std::wstring result{ value };
            if (!result.empty())
            {
                ::CharLowerBuffW(result.data(), static_cast<DWORD>(result.size()));
            }
            return result;
        }

        std::filesystem::path CurrentExecutablePath()
        {
            std::wstring buffer(MAX_PATH, L'\0');
            for (;;)
            {
                DWORD const length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0)
                {
                    return {};
                }
                if (length < buffer.size())
                {
                    buffer.resize(length);
                    return std::filesystem::path{ buffer };
                }
                buffer.resize(buffer.size() * 2);
            }
        }

        void NotifyAssociationsChanged()
        {
            ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        }
    }

    namespace detail
    {
        std::wstring BuildFolderCommandValue(std::filesystem::path const& exePath)
        {
            return L"\"" + exePath.wstring() + L"\" \"%1\"";
        }

        std::wstring BuildWinECommandValue(std::filesystem::path const& exePath)
        {
            return L"\"" + exePath.wstring() + L"\"";
        }

        bool ValuePointsToExecutable(std::wstring_view value, std::filesystem::path const& exePath)
        {
            auto const begin = value.find_first_not_of(L" \t");
            if (begin == std::wstring_view::npos || exePath.empty())
            {
                return false;
            }
            value.remove_prefix(begin);
            std::wstring_view token;
            if (value.front() == L'"')
            {
                auto const close = value.find(L'"', 1);
                if (close == std::wstring_view::npos)
                {
                    return false;
                }
                token = value.substr(1, close - 1);
            }
            else
            {
                token = value.substr(0, value.find_first_of(L" \t"));
            }
            return FoldCase(token) == FoldCase(exePath.wstring());
        }

        DefaultFileManagerStatus DeriveStatus(bool const folder, bool const directory, bool const winE)
        {
            if (folder && directory && winE)
            {
                return DefaultFileManagerStatus::FullyDefault;
            }
            if (folder || directory || winE)
            {
                return DefaultFileManagerStatus::PartiallyDefault;
            }
            return DefaultFileManagerStatus::NotDefault;
        }

        std::vector<RegistryValueSpec> BuildDefaultValues(std::filesystem::path const& exePath)
        {
            std::wstring const verb{ k_verbName };
            std::wstring const winEKey = L"CLSID\\" + std::wstring{ k_winEClsid } + L"\\shell\\opennewwindow\\command";
            std::vector<RegistryValueSpec> values;
            for (std::wstring const cls : { L"Directory", L"Drive" })
            {
                values.push_back({ cls + L"\\shell\\" + verb, L"", L"Open in FileScout" });
                values.push_back({ cls + L"\\shell\\" + verb + L"\\command", L"", BuildFolderCommandValue(exePath) });
                values.push_back({ cls + L"\\shell", L"", verb });
                // Explicit verbs: many apps call ShellExecute(L"open", folder) and Chromium/Edge's
                // "Open downloads folder" uses L"explore"; without these, both resolve to
                // Explorer's Folder\shell verbs. The empty DelegateExecute makes the shell run the
                // command instead of a COM delegate inherited from HKLM.
                for (std::wstring const explicitVerb : { L"open", L"explore" })
                {
                    auto const key = cls + L"\\shell\\" + explicitVerb + L"\\command";
                    values.push_back({ key, L"", BuildFolderCommandValue(exePath) });
                    values.push_back({ key, L"DelegateExecute", L"" });
                }
            }
            // Edge's "Open downloads folder" forces the Folder class with the explore verb (traced on
            // win1: Folder\shell\explore received C:\Users\<user>\Downloads). Folder also covers
            // virtual folders; FileScout hands "::{CLSID}" paths back to Explorer (main.cpp).
            values.push_back({ L"Folder\\shell\\explore\\command", L"", BuildFolderCommandValue(exePath) });
            values.push_back({ L"Folder\\shell\\explore\\command", L"DelegateExecute", L"" });
            values.push_back({ winEKey, L"", BuildWinECommandValue(exePath) });
            values.push_back({ winEKey, L"DelegateExecute", L"" });
            return values;
        }

        std::wstring EncodeManifestEntry(ManifestEntry const& entry)
        {
            if (entry.isKey)
            {
                return L"K|" + entry.subKey;
            }
            return L"V|" + entry.subKey + L"|" + entry.valueName + L"|" + (entry.existed ? L"1|" : L"0|") + entry.oldData;
        }

        std::optional<ManifestEntry> DecodeManifestEntry(std::wstring_view line)
        {
            if (line.starts_with(L"K|"))
            {
                ManifestEntry entry;
                entry.isKey = true;
                entry.subKey = std::wstring{ line.substr(2) };
                return entry.subKey.empty() ? std::nullopt : std::optional{ entry };
            }
            if (!line.starts_with(L"V|"))
            {
                return std::nullopt;
            }
            line.remove_prefix(2);
            std::array<std::wstring_view, 3> fields{};
            for (auto& field : fields)
            {
                auto const bar = line.find(L'|');
                if (bar == std::wstring_view::npos)
                {
                    return std::nullopt;
                }
                field = line.substr(0, bar);
                line.remove_prefix(bar + 1);
            }
            if (fields[0].empty() || (fields[2] != L"0" && fields[2] != L"1"))
            {
                return std::nullopt;
            }
            ManifestEntry entry;
            entry.subKey = std::wstring{ fields[0] };
            entry.valueName = std::wstring{ fields[1] };
            entry.existed = fields[2] == L"1";
            entry.oldData = std::wstring{ line };
            return entry;
        }
    }

    DefaultFileManagerService::DefaultFileManagerService()
        : DefaultFileManagerService(L"Software\\Classes", L"Software\\FileScout\\DefaultFileManager", CurrentExecutablePath())
    {
    }

    DefaultFileManagerService::DefaultFileManagerService(
        std::wstring classesRoot, std::wstring manifestKey, std::filesystem::path exePath)
        : m_classesRoot(std::move(classesRoot)), m_manifestKey(std::move(manifestKey)), m_exePath(std::move(exePath))
    {
    }

    // A verb runs our command only when its DelegateExecute is present and empty: a non-empty one
    // (Explorer's COM delegate) wins over the command string.
    bool DefaultFileManagerService::CommandRunsExecutable(std::wstring const& commandKey) const
    {
        auto const command = ReadString(commandKey, L"");
        auto const delegate = ReadString(commandKey, L"DelegateExecute");
        return command.has_value() && detail::ValuePointsToExecutable(*command, m_exePath) &&
            delegate.has_value() && delegate->empty();
    }

    DefaultFileManagerStatus DefaultFileManagerService::GetStatus() const
    {
        std::wstring const verb{ detail::k_verbName };
        auto const verbIsDefault = [&](std::wstring const& cls)
        {
            auto const defaultVerb = ReadString(Join(m_classesRoot, cls + L"\\shell"), L"");
            auto const command = ReadString(Join(m_classesRoot, cls + L"\\shell\\" + verb + L"\\command"), L"");
            auto const explicitVerbsPointHere = [&]
            {
                for (std::wstring const explicitVerb : { L"open", L"explore" })
                {
                    auto const key = Join(m_classesRoot, cls + L"\\shell\\" + explicitVerb + L"\\command");
                    if (!CommandRunsExecutable(key))
                    {
                        return false;
                    }
                }
                return true;
            };
            return defaultVerb.has_value() && FoldCase(*defaultVerb) == FoldCase(verb) &&
                command.has_value() && detail::ValuePointsToExecutable(*command, m_exePath) &&
                explicitVerbsPointHere();
        };
        std::wstring const winEKey = Join(m_classesRoot,
            L"CLSID\\" + std::wstring{ detail::k_winEClsid } + L"\\shell\\opennewwindow\\command");
        auto const winECommand = ReadString(winEKey, L"");
        auto const delegate = ReadString(winEKey, L"DelegateExecute");
        bool const winE = winECommand.has_value() && detail::ValuePointsToExecutable(*winECommand, m_exePath) &&
            delegate.has_value() && delegate->empty();
        bool const folderVerbs = CommandRunsExecutable(Join(m_classesRoot, L"Folder\\shell\\explore\\command"));
        return detail::DeriveStatus(verbIsDefault(L"Drive") && folderVerbs, verbIsDefault(L"Directory"), winE);
    }

    bool DefaultFileManagerService::SetAsDefault()
    {
        if (m_exePath.empty())
        {
            return false;
        }
        auto const values = detail::BuildDefaultValues(m_exePath);

        // Prior state is recorded once per value: entries already in the manifest are kept (a second
        // Set must not overwrite the original Explorer state with FileScout's own values), and values
        // or keys a newer build writes that an older manifest does not cover are appended.
        {
            auto manifest = ReadManifest(m_manifestKey);
            auto const originalSize = manifest.size();
            std::unordered_set<std::wstring> recordedKeys;
            std::unordered_set<std::wstring> recordedValues;
            for (auto const& line : manifest)
            {
                if (auto const entry = detail::DecodeManifestEntry(line))
                {
                    if (entry->isKey)
                    {
                        recordedKeys.insert(entry->subKey);
                    }
                    else
                    {
                        recordedValues.insert(entry->subKey + L"|" + entry->valueName);
                    }
                }
            }
            for (auto const& spec : values)
            {
                // Each missing path component below the classes root is a key FileScout creates.
                std::wstring prefix;
                for (auto const& part : std::filesystem::path{ spec.subKey })
                {
                    prefix = prefix.empty() ? part.wstring() : prefix + L"\\" + part.wstring();
                    if (!recordedKeys.contains(prefix) && !KeyExists(Join(m_classesRoot, prefix)))
                    {
                        manifest.push_back(detail::EncodeManifestEntry({ true, prefix, {}, false, {} }));
                    }
                    recordedKeys.insert(prefix);
                }
                auto const id = spec.subKey + L"|" + spec.valueName;
                if (recordedValues.insert(id).second)
                {
                    auto const old = ReadString(Join(m_classesRoot, spec.subKey), spec.valueName);
                    manifest.push_back(detail::EncodeManifestEntry(
                        { false, spec.subKey, spec.valueName, old.has_value(), old.value_or(L"") }));
                }
            }
            if (manifest.size() != originalSize && !WriteManifest(m_manifestKey, manifest))
            {
                return false;
            }
        }

        bool ok = true;
        for (auto const& spec : values)
        {
            ok = WriteString(Join(m_classesRoot, spec.subKey), spec.valueName, spec.data) && ok;
        }
        NotifyAssociationsChanged();
        return ok && GetStatus() == DefaultFileManagerStatus::FullyDefault;
    }

    bool DefaultFileManagerService::RestoreDefault()
    {
        auto const lines = ReadManifest(m_manifestKey);
        if (lines.empty())
        {
            return true;
        }
        std::vector<detail::ManifestEntry> entries;
        for (auto const& line : lines)
        {
            if (auto entry = detail::DecodeManifestEntry(line))
            {
                entries.push_back(std::move(*entry));
            }
        }

        bool ok = true;
        // Values first (reverse write order), then the keys FileScout created, deepest first.
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        {
            if (it->isKey)
            {
                continue;
            }
            auto const path = Join(m_classesRoot, it->subKey);
            ok = (it->existed ? WriteString(path, it->valueName, it->oldData) : DeleteValue(path, it->valueName)) && ok;
        }
        std::vector<std::wstring> createdKeys;
        for (auto const& entry : entries)
        {
            if (entry.isKey)
            {
                createdKeys.push_back(entry.subKey);
            }
        }
        std::ranges::sort(createdKeys, [](auto const& a, auto const& b) { return a.size() > b.size(); });
        for (auto const& key : createdKeys)
        {
            ok = DeleteKeyIfEmpty(Join(m_classesRoot, key)) && ok;
        }

        if (ok)
        {
            ::RegDeleteKeyW(HKEY_CURRENT_USER, m_manifestKey.c_str());
            // The manifest's parent (HKCU\Software\FileScout) goes too when nothing else lives there.
            auto const parent = std::filesystem::path{ m_manifestKey }.parent_path().wstring();
            if (!parent.empty())
            {
                DeleteKeyIfEmpty(parent);
            }
        }
        NotifyAssociationsChanged();
        return ok;
    }
}
