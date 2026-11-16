#include "pch.h"
#include "App.xaml.h"
#include "Domain/ThemeService.h"
#include "Domain/UnhandledExceptionReport.h"
#include "MainWindow.xaml.h"
#include "Shell/TempStagingCleanup.h"
#include "TraceHelper.h"

#include <fstream>

namespace winrt::FileScoutNative::implementation
{
    namespace
    {
        winrt::Windows::UI::Color ColorFromArgb(std::uint32_t const argb)
        {
            return winrt::Windows::UI::Color{
                static_cast<std::uint8_t>((argb >> 24) & 0xFF),
                static_cast<std::uint8_t>((argb >> 16) & 0xFF),
                static_cast<std::uint8_t>((argb >> 8) & 0xFF),
                static_cast<std::uint8_t>(argb & 0xFF),
            };
        }

        winrt::Microsoft::UI::Xaml::Media::SolidColorBrush BuildThemeBrush(
            std::uint32_t const argb)
        {
            return winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(ColorFromArgb(argb));
        }

        winrt::Microsoft::UI::Xaml::ResourceDictionary BuildThemeResourceDictionary(
            ::FileScoutNative::Domain::ThemeDefinition const& theme)
        {
            auto dictionary = winrt::Microsoft::UI::Xaml::ResourceDictionary{};
            for (auto const& entry : ::FileScoutNative::Domain::BuildThemeResourceEntries(theme))
            {
                dictionary.Insert(
                    winrt::box_value(winrt::hstring{ entry.resource_key }),
                    BuildThemeBrush(entry.color));
            }
            return dictionary;
        }

        void UpdateThemeResourceDictionary(
            winrt::Microsoft::UI::Xaml::ResourceDictionary const& dictionary,
            ::FileScoutNative::Domain::ThemeDefinition const& theme)
        {
            for (auto const& entry : ::FileScoutNative::Domain::BuildThemeResourceEntries(theme))
            {
                auto const key = winrt::box_value(winrt::hstring{ entry.resource_key });
                auto const color = ColorFromArgb(entry.color);

                if (dictionary.HasKey(key))
                {
                    if (auto const brush =
                        dictionary.Lookup(key).try_as<winrt::Microsoft::UI::Xaml::Media::SolidColorBrush>())
                    {
                        brush.Color(color);
                        continue;
                    }
                }

                dictionary.Insert(key, BuildThemeBrush(entry.color));
            }
        }

        // Best effort, as UTF-8: an error path must still reach its message box (UA-09).
        void AppendStartupLog(std::wstring const& line) noexcept
        {
            try
            {
                AppendUtf8LogLine(std::filesystem::temp_directory_path() / "FileScout.Native.startup.log", line);
            }
            catch (...)
            {
            }
        }

        void ShowStartupError(wchar_t const* title, winrt::hresult_error const& error)
        {
            auto message = std::wstring{ error.message().c_str() };
            if (message.empty())
            {
                message = L"The error description was not found.";
            }

            message += L"\nHRESULT: 0x";

            std::wstringstream stream;
            stream << std::uppercase << std::hex << static_cast<std::uint32_t>(error.code().value);
            message += stream.str();

            AppendStartupLog(std::wstring{ L"[" } + title + L"] " + message);
            ::OutputDebugStringW((std::wstring{ title } + L": " + message + L"\n").c_str());
            ::MessageBoxW(nullptr, message.c_str(), title, MB_OK | MB_ICONERROR);
        }

        void ShowStartupError(wchar_t const* title, std::wstring const& message)
        {
            AppendStartupLog(std::wstring{ L"[" } + title + L"] " + message);
            ::OutputDebugStringW((std::wstring{ title } + L": " + message + L"\n").c_str());
            ::MessageBoxW(nullptr, message.c_str(), title, MB_OK | MB_ICONERROR);
        }
    }

    App::App()
    {
        TraceLog(L"App::App() start");
        s_current = this;
        InitializeComponent();
        TraceLog(L"App::App() InitializeComponent done");

        UnhandledException([](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e)
        {
            // UA-09: log, tell the user, then let WinUI terminate the process. Continuing in an
            // unknown state on top of the user's files is worse than closing.
            auto const report = ::FileScoutNative::Domain::BuildUnhandledExceptionReport(
                e.Message().c_str(), static_cast<int32_t>(e.Exception().value));
            TraceLog(report.log_line);
            AppendStartupLog(report.log_line);
            ::MessageBoxW(nullptr, report.message.c_str(), report.title.c_str(), MB_OK | MB_ICONERROR | MB_TASKMODAL);
            e.Handled(::FileScoutNative::Domain::ShouldMarkUnhandledExceptionHandled());
        });
    }

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        TraceLog(L"App::OnLaunched start");
        try
        {
            TraceLog(L"App::OnLaunched: ApplyTheme...");
            ApplyTheme(::FileScoutNative::Domain::ThemeService::LoadSavedThemeId());
            TraceLog(L"App::OnLaunched: ApplyTheme done");
            TraceLog(L"App::OnLaunched: make<MainWindow>...");
            m_window = winrt::make<MainWindow>();
            TraceLog(L"App::OnLaunched: Activate...");
            m_window.Activate();
            TraceLog(L"App::OnLaunched: Activate done");

            // Drag staging and archive-open copies stay in %TEMP% while drop targets or opened apps
            // may still use them; sweep the ones older than a day, off the UI thread.
            std::thread([]
            {
                try
                {
                    auto const deleted = ::FileScoutNative::Shell::CleanupStaleStagingDirectories(
                        std::filesystem::temp_directory_path(),
                        std::filesystem::file_time_type::clock::now());
                    TraceLog(L"Temp staging cleanup: deleted " + std::to_wstring(deleted) + L" dir(s)");
                }
                catch (...)
                {
                }
            }).detach();
        }
        catch (winrt::hresult_error const& error)
        {
            ShowStartupError(L"FileScout Native startup failed", error);
        }
        catch (std::exception const& error)
        {
            ShowStartupError(
                L"FileScout Native startup failed",
                winrt::to_hstring(error.what()).c_str());
        }
        catch (...)
        {
            ShowStartupError(
                L"FileScout Native startup failed",
                L"An unknown non-WinRT exception escaped during launch.");
        }
    }

    void App::ApplyTheme(std::wstring_view const themeId)
    {
        auto const theme = ::FileScoutNative::Domain::ThemeService::GetTheme(themeId);
        auto const mergedDictionaries = Resources().MergedDictionaries();

        if (!m_themeResources)
        {
            m_themeResources = BuildThemeResourceDictionary(theme);
            mergedDictionaries.Append(m_themeResources);
            return;
        }

        UpdateThemeResourceDictionary(m_themeResources, theme);
    }
}
