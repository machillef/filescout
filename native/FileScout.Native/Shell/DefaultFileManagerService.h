#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace FileScoutNative::Shell
{
    // Mechanism (no admin, no signature): per-user shell verbs under HKCU\Software\Classes.
    //   Directory\shell  (default) = FileScout, Directory\shell\FileScout\command = "<exe>" "%1"
    //   Directory\shell\open|explore\command = "<exe>" "%1" (explicit verbs: ShellExecute "open",
    //     Edge's "Open downloads folder" uses "explore")
    //   Drive\shell      the same for drive roots
    //   Folder\shell\explore\command  (Edge's "Open downloads folder" forces the Folder class)
    //   CLSID\{52205fd8-...}\shell\opennewwindow\command = "<exe>" with DelegateExecute = ""
    //     (the Win+E handler; an empty DelegateExecute makes the shell run the command instead
    //     of Explorer's COM delegate)
    // ShellExecute on a folder (desktop, Start-Process, other apps) and SHOpenFolderAndSelectItems
    // (browsers' "Open folder"/"Show in folder") resolve the Directory verb.
    enum class DefaultFileManagerStatus
    {
        NotDefault,
        PartiallyDefault,
        FullyDefault
    };

    struct RegistryValueSpec
    {
        std::wstring subKey;
        std::wstring valueName;   // empty = the key's default value
        std::wstring data;
    };

    namespace detail
    {
        inline constexpr std::wstring_view k_verbName = L"FileScout";
        inline constexpr std::wstring_view k_winEClsid = L"{52205fd8-5dfb-447d-801a-d0b52f2e83e1}";

        [[nodiscard]] std::wstring BuildFolderCommandValue(std::filesystem::path const& exePath);

        [[nodiscard]] std::wstring BuildWinECommandValue(std::filesystem::path const& exePath);

        [[nodiscard]] bool ValuePointsToExecutable(std::wstring_view value, std::filesystem::path const& exePath);

        [[nodiscard]] DefaultFileManagerStatus DeriveStatus(bool folder, bool directory, bool winE);

        [[nodiscard]] std::vector<RegistryValueSpec> BuildDefaultValues(std::filesystem::path const& exePath);

        struct ManifestEntry
        {
            bool isKey = false;
            std::wstring subKey;
            std::wstring valueName;
            bool existed = false;
            std::wstring oldData;
        };
        [[nodiscard]] std::wstring EncodeManifestEntry(ManifestEntry const& entry);
        [[nodiscard]] std::optional<ManifestEntry> DecodeManifestEntry(std::wstring_view line);
    }

    class DefaultFileManagerService
    {
    public:
        DefaultFileManagerService();

        DefaultFileManagerService(std::wstring classesRoot, std::wstring manifestKey, std::filesystem::path exePath);

        [[nodiscard]] DefaultFileManagerStatus GetStatus() const;

        [[nodiscard]] bool SetAsDefault();

        [[nodiscard]] bool RestoreDefault();

        [[nodiscard]] std::filesystem::path const& ExecutablePath() const noexcept { return m_exePath; }

    private:
        [[nodiscard]] bool CommandRunsExecutable(std::wstring const& commandKey) const;

        std::wstring m_classesRoot;
        std::wstring m_manifestKey;
        std::filesystem::path m_exePath;
    };
}
