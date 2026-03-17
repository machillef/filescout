#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

namespace winrt::FileScoutNative::implementation
{
    namespace
    {
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

            ::OutputDebugStringW((std::wstring{ title } + L": " + message + L"\n").c_str());
            ::MessageBoxW(nullptr, message.c_str(), title, MB_OK | MB_ICONERROR);
        }
    }

    App::App()
    {
        InitializeComponent();

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e)
            {
                if (IsDebuggerPresent())
                {
                    auto message = e.Message();
                    (void)message;
                    __debugbreak();
                }
            });
#endif
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
            throw;
        }
    }
}
