#include "pch.h"
#include "TraceHelper.h"

#include <fstream>
#ifndef FILESCOUT_TESTING
#include "App.xaml.h"
#include "Shell/DefaultFileManagerService.h"
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#endif

namespace
{
    std::filesystem::path StartupLogPath()
    {
        return std::filesystem::temp_directory_path() / "FileScout.Native.startup.log";
    }

    void AppendStartupLog(std::wstring const& line) noexcept
    {
        try
        {
            AppendUtf8LogLine(StartupLogPath(), line);
        }
        catch (...)
        {
        }
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
}

namespace FileScoutNative::detail
{
    enum class StartupAction { OpenWindow, SetDefaultFileManager, RestoreExplorer };

    StartupAction ResolveStartupAction(std::span<wchar_t const* const> args)
    {
        for (auto const* arg : args)
        {
            if (arg == nullptr)
            {
                continue;
            }
            std::wstring_view const value{ arg };
            if (value == L"--set-default-file-manager")
            {
                return StartupAction::SetDefaultFileManager;
            }
            if (value == L"--restore-explorer")
            {
                return StartupAction::RestoreExplorer;
            }
        }
        return StartupAction::OpenWindow;
    }

    // Virtual shell folders ("::{CLSID}" parsing names, "shell:" monikers) reach FileScout through the
    // Folder class verbs once it is the default file manager. FileScout browses the file system only,
    // so it hands these back to Explorer.
    bool ShouldDelegateToExplorer(std::wstring_view argument)
    {
        auto const startsWith = [&](std::wstring_view prefix)
        {
            return argument.size() >= prefix.size() &&
                ::CompareStringOrdinal(argument.data(), static_cast<int>(prefix.size()),
                    prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
        };
        return startsWith(L"::") || startsWith(L"shell:");
    }

    std::optional<std::filesystem::path> ResolveLaunchPathArgument(std::span<wchar_t const* const> args)
    {
        for (auto const* arg : args)
        {
            if (arg != nullptr && arg[0] != L'\0')
            {
                std::wstring value{ arg };
                // "%1" for a drive root is "C:\"; CommandLineToArgvW reads \" as an escaped quote and
                // yields C:". A trailing quote can never be part of a Windows path, so restore the
                // backslash it replaced.
                if (value.size() > 1 && value.back() == L'"')
                {
                    value.back() = L'\\';
                }
                return std::filesystem::path{ value };
            }
        }

        return std::nullopt;
    }
}

#ifndef FILESCOUT_TESTING
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int)
{
    TraceLog(L"--- wWinMain start ---");
    std::filesystem::remove(StartupLogPath());
    LogSessionStart();

    std::optional<std::filesystem::path> launchPath;
    int argc = 0;
    if (auto const* const* const argv = CommandLineToArgvW(GetCommandLineW(), &argc); argv != nullptr)
    {
        if (argc > 1)
        {
            launchPath = FileScoutNative::detail::ResolveLaunchPathArgument(
                std::span<wchar_t const* const>{ argv + 1, static_cast<size_t>(argc - 1) });
        }
        LocalFree(const_cast<LPWSTR*>(argv));
    }

    {
        int actionArgc = 0;
        auto* const actionArgv = CommandLineToArgvW(GetCommandLineW(), &actionArgc);
        auto const action = actionArgv != nullptr && actionArgc > 1
            ? FileScoutNative::detail::ResolveStartupAction(
                  std::span<wchar_t const* const>{ actionArgv + 1, static_cast<size_t>(actionArgc - 1) })
            : FileScoutNative::detail::StartupAction::OpenWindow;
        if (actionArgv != nullptr)
        {
            LocalFree(actionArgv);
        }
        if (action != FileScoutNative::detail::StartupAction::OpenWindow)
        {
            FileScoutNative::Shell::DefaultFileManagerService service;
            bool const ok = action == FileScoutNative::detail::StartupAction::SetDefaultFileManager
                ? service.SetAsDefault()
                : service.RestoreDefault();
            TraceLog(std::wstring{ L"[default-fm] headless " } +
                (action == FileScoutNative::detail::StartupAction::SetDefaultFileManager ? L"set" : L"restore") +
                (ok ? L" ok" : L" FAILED"));
            return ok ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    if (launchPath.has_value() && FileScoutNative::detail::ShouldDelegateToExplorer(launchPath->native()))
    {
        TraceLog(L"[launch] delegating shell namespace path to Explorer: " + launchPath->wstring());
        // Absolute path: a bare "explorer.exe" is searched in the working directory first (CWE-426).
        wchar_t windowsDir[MAX_PATH]{};
        UINT const length = ::GetWindowsDirectoryW(windowsDir, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            TraceLog(L"[launch] GetWindowsDirectoryW failed; not launching Explorer");
            return EXIT_FAILURE;
        }
        auto const explorer = std::filesystem::path{ windowsDir } / L"explorer.exe";
        auto const result = reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, nullptr, explorer.c_str(),
            (L"\"" + launchPath->wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL));
        if (result <= 32)
        {
            TraceLog(L"[launch] ShellExecuteW(explorer.exe) failed: " + std::to_wstring(result));
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
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
#endif
