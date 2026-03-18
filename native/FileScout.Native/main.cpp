#include "pch.h"
#include "App.xaml.h"
#include "TraceHelper.h"

#include <fstream>
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>

namespace
{
    std::filesystem::path StartupLogPath()
    {
        return std::filesystem::temp_directory_path() / "FileScout.Native.startup.log";
    }

    void AppendStartupLog(std::wstring const& line)
    {
        std::wofstream stream{ StartupLogPath(), std::ios::app };
        if (!stream.is_open())
        {
            return;
        }

        stream << line << L"\n";
    }

    int ShowBootstrapFailure(HRESULT const hr)
    {
        std::wstringstream message;
        message << L"Windows App SDK bootstrap failed before FileScout could start."
                << L"\nHRESULT: 0x"
                << std::uppercase
                << std::hex
                << static_cast<std::uint32_t>(hr);

        auto const text = message.str();
        AppendStartupLog(L"[bootstrap] " + text);
        ::OutputDebugStringW((std::wstring{ L"FileScout Native bootstrap failed: " } + text + L"\n").c_str());
        ::MessageBoxW(nullptr, text.c_str(), L"FileScout Native bootstrap failed", MB_OK | MB_ICONERROR);
        return static_cast<int>(hr);
    }

    int ShowStartupFailure(std::wstring const& message)
    {
        AppendStartupLog(L"[startup] " + message);
        ::OutputDebugStringW((std::wstring{ L"FileScout Native startup failed: " } + message + L"\n").c_str());
        ::MessageBoxW(nullptr, message.c_str(), L"FileScout Native startup failed", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    TraceLog(L"--- wWinMain start ---");
    std::filesystem::remove(StartupLogPath());

    PACKAGE_VERSION minVersion{};
    minVersion.Version = WINDOWSAPPSDK_RUNTIME_VERSION_UINT64;

    TraceLog(L"MddBootstrapInitialize2 (manual call after auto-init)...");
    auto const bootstrapHr = ::MddBootstrapInitialize2(
        WINDOWSAPPSDK_RELEASE_MAJORMINOR,
        WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
        minVersion,
        MddBootstrapInitializeOptions_OnNoMatch_ShowUI);
    {
        std::wstringstream hrMsg;
        hrMsg << L"MddBootstrapInitialize2 returned: 0x" << std::uppercase << std::hex
              << static_cast<std::uint32_t>(bootstrapHr);
        TraceLog(hrMsg.str());
    }
    if (FAILED(bootstrapHr))
    {
        return ShowBootstrapFailure(bootstrapHr);
    }

    TraceLog(L"init_apartment...");
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    TraceLog(L"init_apartment done");

    int exitCode = EXIT_SUCCESS;
    try
    {
        TraceLog(L"Application::Start...");
        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&)
            {
                TraceLog(L"App ctor lambda...");
                winrt::make<winrt::FileScoutNative::implementation::App>();
                TraceLog(L"App ctor lambda done");
            });
        TraceLog(L"Application::Start returned");
    }
    catch (winrt::hresult_error const& error)
    {
        exitCode = ShowBootstrapFailure(error.code().value);
    }
    catch (std::exception const& error)
    {
        exitCode = ShowStartupFailure(
            std::wstring{ L"Startup failed before the WinUI app finished launching.\n" }
            + winrt::to_hstring(error.what()).c_str());
    }
    catch (...)
    {
        exitCode = ShowStartupFailure(
            L"Startup failed before the WinUI app finished launching.\nAn unknown exception escaped the entry point.");
    }

    ::MddBootstrapShutdown();
    return exitCode;
}
