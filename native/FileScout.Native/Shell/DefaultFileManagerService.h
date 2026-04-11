#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace FileScoutNative::Shell
{
    enum class DefaultFileManagerStatus
    {
        NotDefault,
        PartiallyDefault,
        FullyDefault
    };

    // Testable helper functions exposed for unit tests.
    namespace detail
    {
        // Builds the command value for Folder/Directory keys: "path" "%1"
        [[nodiscard]] std::wstring BuildFolderCommandValue(std::filesystem::path const& exePath);

        // Builds the command value for Win+E key: "path" (no %1)
        [[nodiscard]] std::wstring BuildWinECommandValue(std::filesystem::path const& exePath);

        // Returns true if the registry value contains the FileScout executable name.
        [[nodiscard]] bool ValuePointsToFileScout(std::wstring_view value);

        // Derives status from three boolean checks (one per key).
        [[nodiscard]] DefaultFileManagerStatus DeriveStatus(bool folder, bool directory, bool winE);
    }

    class DefaultFileManagerService
    {
    public:
        [[nodiscard]] DefaultFileManagerStatus GetStatus() const;
        [[nodiscard]] bool SetAsDefault();
        [[nodiscard]] bool RestoreDefault();

    private:
        [[nodiscard]] std::optional<std::wstring> ReadRegistryValue(
            std::wstring_view subKey) const;
        [[nodiscard]] bool WriteRegistryValue(
            std::wstring_view subKey, std::wstring_view value);
        [[nodiscard]] bool DeleteRegistryKey(std::wstring_view subKey);
    };
}
