#pragma once

namespace FileScoutNative::Shell
{
    enum class ShellOverflowMenuOutcome
    {
        Dismissed,
        Invoked,
    };

    class ShellOverflowMenuService
    {
    public:
        [[nodiscard]] ShellOverflowMenuOutcome ShowForPath(
            HWND ownerWindow,
            std::filesystem::path const& path,
            POINT screenPoint) const;
    };
}
