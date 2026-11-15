#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/ShellCommandVerb.h"

using namespace FileScoutNative::Shell;

namespace
{
    // IContextMenu that answers GetCommandString like a shell extension that only fills the
    // Unicode verb and refuses a buffer that is too small (StringCchCopy semantics).
    struct FakeVerbMenu : winrt::implements<FakeVerbMenu, IContextMenu>
    {
        std::wstring verb;
        UINT lastFlags = 0;

        explicit FakeVerbMenu(std::wstring value) : verb(std::move(value)) {}

        HRESULT __stdcall QueryContextMenu(HMENU, UINT, UINT, UINT, UINT) noexcept override { return E_NOTIMPL; }
        HRESULT __stdcall InvokeCommand(CMINVOKECOMMANDINFO*) noexcept override { return E_NOTIMPL; }

        HRESULT __stdcall GetCommandString(UINT_PTR, UINT flags, UINT*, CHAR* name, UINT cchMax) noexcept override
        {
            lastFlags = flags;
            if (flags != GCS_VERBW)
            {
                return E_INVALIDARG;
            }

            if (cchMax < verb.size() + 1)
            {
                return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER); // STRSAFE_E_INSUFFICIENT_BUFFER
            }

            auto* const out = reinterpret_cast<wchar_t*>(name);
            std::copy(verb.begin(), verb.end(), out);
            out[verb.size()] = L'\0';
            return S_OK;
        }
    };
}

TEST(ShellCommandVerbTests, ReadsUnicodeVerb)
{
    auto const menu = winrt::make_self<FakeVerbMenu>(L"Ανοιγμα.open");
    EXPECT_EQ(ReadShellCommandVerb(menu.get(), 3), L"Ανοιγμα.open");
    EXPECT_EQ(menu->lastFlags, static_cast<UINT>(GCS_VERBW));
}

TEST(ShellCommandVerbTests, ReadsVerbLongerThan256Characters)
{
    std::wstring const longVerb(400, L'v');
    auto const menu = winrt::make_self<FakeVerbMenu>(longVerb);
    EXPECT_EQ(ReadShellCommandVerb(menu.get(), 0), longVerb);
}

TEST(ShellCommandVerbTests, FailedLookupGivesEmptyVerb)
{
    struct NoVerbMenu : winrt::implements<NoVerbMenu, IContextMenu>
    {
        HRESULT __stdcall QueryContextMenu(HMENU, UINT, UINT, UINT, UINT) noexcept override { return E_NOTIMPL; }
        HRESULT __stdcall InvokeCommand(CMINVOKECOMMANDINFO*) noexcept override { return E_NOTIMPL; }
        HRESULT __stdcall GetCommandString(UINT_PTR, UINT, UINT*, CHAR*, UINT) noexcept override { return E_NOTIMPL; }
    };
    auto const menu = winrt::make_self<NoVerbMenu>();
    EXPECT_TRUE(ReadShellCommandVerb(menu.get(), 0).empty());
}

TEST(ShellCommandVerbTests, BlocksClipboardVerbsCaseInsensitively)
{
    EXPECT_TRUE(IsBlockedShellVerb(L"copy"));
    EXPECT_TRUE(IsBlockedShellVerb(L"Cut"));
    EXPECT_TRUE(IsBlockedShellVerb(L"PASTE"));
    EXPECT_FALSE(IsBlockedShellVerb(L"open"));
    EXPECT_FALSE(IsBlockedShellVerb(L"copyto"));
    EXPECT_FALSE(IsBlockedShellVerb(L""));
}

namespace
{
    // A handler that truncates to the buffer and still returns S_OK.
    struct TruncatingVerbMenu : winrt::implements<TruncatingVerbMenu, IContextMenu>
    {
        std::wstring verb;
        explicit TruncatingVerbMenu(std::wstring value) : verb(std::move(value)) {}

        HRESULT __stdcall QueryContextMenu(HMENU, UINT, UINT, UINT, UINT) noexcept override { return E_NOTIMPL; }
        HRESULT __stdcall InvokeCommand(CMINVOKECOMMANDINFO*) noexcept override { return E_NOTIMPL; }
        HRESULT __stdcall GetCommandString(UINT_PTR, UINT flags, UINT*, CHAR* name, UINT cchMax) noexcept override
        {
            if (flags != GCS_VERBW || cchMax == 0)
            {
                return E_INVALIDARG;
            }
            auto* const out = reinterpret_cast<wchar_t*>(name);
            auto const count = (std::min)(verb.size(), static_cast<std::size_t>(cchMax - 1));
            std::copy_n(verb.begin(), count, out);
            out[count] = L'\0';
            return S_OK;
        }
    };
}

TEST(ShellCommandVerbTests, RetriesWhenAHandlerTruncatesSilently)
{
    std::wstring const longVerb(300, L'v');
    auto const menu = winrt::make_self<TruncatingVerbMenu>(longVerb);

    EXPECT_EQ(FileScoutNative::Shell::ReadShellCommandVerb(menu.get(), 0), longVerb);
}

TEST(ShellCommandVerbTests, ReadsASilentlyTruncatingVerbBetween16KAnd32K)
{
    std::wstring const longVerb(20'000, L'v');
    auto const menu = winrt::make_self<TruncatingVerbMenu>(longVerb);

    EXPECT_EQ(FileScoutNative::Shell::ReadShellCommandVerb(menu.get(), 0), longVerb);
}

TEST(ShellCommandVerbTests, AFullBufferAtTheLimitGivesNoVerbRatherThanATruncatedOne)
{
    auto const menu = winrt::make_self<TruncatingVerbMenu>(std::wstring(40'000, L'v'));

    EXPECT_TRUE(FileScoutNative::Shell::ReadShellCommandVerb(menu.get(), 0).empty());
}
