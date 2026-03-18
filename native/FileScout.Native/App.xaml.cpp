#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <fstream>

namespace winrt::FileScoutNative::implementation
{
    namespace
    {
        void AppendStartupLog(std::wstring const& line)
        {
            auto const path = std::filesystem::temp_directory_path() / "FileScout.Native.startup.log";
            std::wofstream stream{ path, std::ios::app };
            if (!stream.is_open())
            {
                return;
            }

            stream << line << L"\n";
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
        InitializeComponent();

        UnhandledException([](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e)
        {
            auto message = std::wstring{ e.Message().c_str() };
            if (message.empty())
            {
                message = L"XAML raised an unhandled exception without a message.";
            }

            ShowStartupError(L"FileScout Native XAML exception", message);
            e.Handled(true);
        });
    }

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        try
        {
            m_window = winrt::make<MainWindow>();
            m_window.Activate();
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
}
