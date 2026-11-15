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
