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

    std::wstring WindowsVersionString()
    {
        // RtlGetVersion is the correct non-deprecated API for OS version detection.
        using RtlGetVersionFn = NTSTATUS(WINAPI*)(PRTL_OSVERSIONINFOW);
        auto* const ntdll = ::GetModuleHandleW(L"ntdll.dll");
        if (!ntdll) { return L"unknown"; }
        auto const rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            ::GetProcAddress(ntdll, "RtlGetVersion"));
        if (!rtlGetVersion) { return L"unknown"; }
        RTL_OSVERSIONINFOW osvi{};
        osvi.dwOSVersionInfoSize = sizeof(osvi);
        rtlGetVersion(&osvi);
        return L"Windows " + std::to_wstring(osvi.dwMajorVersion) +
               L"." + std::to_wstring(osvi.dwMinorVersion) +
               L" Build " + std::to_wstring(osvi.dwBuildNumber);
    }

    void LogSessionStart()
    {
        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        wchar_t date[32]{};
        swprintf_s(date, L"%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
        TraceLog(L"=== SESSION START | " + std::wstring{ date } +
                 L" | PID:" + std::to_wstring(::GetCurrentProcessId()) +
                 L" | " + WindowsVersionString() + L" ===");
    }
} // anonymous namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int)
{
    TraceLog(L"--- wWinMain start ---");
    std::filesystem::remove(StartupLogPath());
    LogSessionStart();

    // Parse command-line for a path argument (from "Open in FileScout" context menu).
    std::optional<std::filesystem::path> launchPath;
    int argc = 0;
    if (auto const* const* const argv = CommandLineToArgvW(GetCommandLineW(), &argc); argv != nullptr)
    {
        for (int i = 1; i < argc; ++i)
        {
            std::wstring arg{ argv[i] };
            // Skip flags like --something
            if (!arg.empty() && arg[0] != L'-')
            {
                launchPath = arg;
                break;
            }
        }
        LocalFree(const_cast<LPWSTR*>(argv));
    }

    if (launchPath.has_value())
    {
        winrt::FileScoutNative::implementation::App::SetLaunchPath(*launchPath);
        TraceLog(L"[launch] Path argument: " + launchPath->wstring());
    }

    PACKAGE_VERSION minVersion{};
    minVersion.Version = WINDOWSAPPSDK_RUNTIME_VERSION_UINT64;

    HRESULT bootstrapHr = E_FAIL;
    {
        ScopedTimer timer{ L"MddBootstrapInitialize2" };
        bootstrapHr = ::MddBootstrapInitialize2(
            WINDOWSAPPSDK_RELEASE_MAJORMINOR,
            WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
            minVersion,
            MddBootstrapInitializeOptions_OnNoMatch_ShowUI);
    }
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

    {
        ScopedTimer timer{ L"init_apartment" };
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    }

    int exitCode = EXIT_SUCCESS;
    try
    {
        ScopedTimer appTimer{ L"Application::Start" };
        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&)
            {
                winrt::make<winrt::FileScoutNative::implementation::App>();
            });
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
