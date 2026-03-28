#pragma once

#include "App.xaml.g.h"

namespace winrt::FileScoutNative::implementation
{
    struct App : AppT<App>
    {
        App();

        [[nodiscard]] static App* Current() noexcept { return s_current; }
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);
        void ApplyTheme(std::wstring_view themeId);

    private:
        inline static App* s_current{ nullptr };
        Microsoft::UI::Xaml::Window m_window{ nullptr };
        Microsoft::UI::Xaml::ResourceDictionary m_themeResources{ nullptr };
    };
}
