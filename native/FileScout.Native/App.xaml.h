#pragma once

#include "App.xaml.g.h"
#include <filesystem>
#include <optional>

namespace winrt::FileScoutNative::implementation
{
    struct App : AppT<App>
    {
        App();

        [[nodiscard]] static App* Current() noexcept { return s_current; }
        [[nodiscard]] static std::optional<std::filesystem::path> LaunchPath() noexcept { return s_launchPath; }
        static void SetLaunchPath(std::filesystem::path path) { s_launchPath = std::move(path); }
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);
        void ApplyTheme(std::wstring_view themeId);

    private:
        inline static App* s_current{ nullptr };
        inline static std::optional<std::filesystem::path> s_launchPath;
        Microsoft::UI::Xaml::Window m_window{ nullptr };
        Microsoft::UI::Xaml::ResourceDictionary m_themeResources{ nullptr };
    };
}
