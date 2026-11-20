#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/NavigationInput.h"

using FileScoutNative::Domain::MouseNavigationDeduplicator;
using FileScoutNative::Domain::NavigationInputCommand;
using FileScoutNative::Domain::ResolveNavigationInputCommand;

TEST(NavigationInput, XButton1MapsToBack)
{
    EXPECT_EQ(
        ResolveNavigationInputCommand(WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), 0),
        NavigationInputCommand::Back);
    EXPECT_EQ(
        ResolveNavigationInputCommand(WM_NCXBUTTONUP, MAKEWPARAM(HTCAPTION, XBUTTON1), 0),
        NavigationInputCommand::Back);
}

TEST(NavigationInput, XButton2MapsToForward)
{
    EXPECT_EQ(
        ResolveNavigationInputCommand(WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), 0),
        NavigationInputCommand::Forward);
    EXPECT_EQ(
        ResolveNavigationInputCommand(WM_NCXBUTTONUP, MAKEWPARAM(HTCAPTION, XBUTTON2), 0),
        NavigationInputCommand::Forward);
}

TEST(NavigationInput, BrowserBackAppCommandMapsToBack)
{
    EXPECT_EQ(
        ResolveNavigationInputCommand(WM_APPCOMMAND, 0, static_cast<LPARAM>(APPCOMMAND_BROWSER_BACKWARD << 16)),
        NavigationInputCommand::Back);
}

TEST(NavigationInput, BrowserForwardAppCommandMapsToForward)
{
    EXPECT_EQ(
        ResolveNavigationInputCommand(WM_APPCOMMAND, 0, static_cast<LPARAM>(APPCOMMAND_BROWSER_FORWARD << 16)),
        NavigationInputCommand::Forward);
}

TEST(NavigationInput, UnrelatedMessagesMapToNone)
{
    EXPECT_EQ(
        ResolveNavigationInputCommand(WM_MOUSEMOVE, 0, 0),
        NavigationInputCommand::None);
}

TEST(NavigationInput, DeduplicatorSuppressesBubbledWin32AfterPointerPress)
{
    MouseNavigationDeduplicator dedup;
    LPARAM const mouseBackAppCmd =
        static_cast<LPARAM>(static_cast<DWORD>(APPCOMMAND_BROWSER_BACKWARD | FAPPCOMMAND_MOUSE) << 16);

    dedup.OnPointerPressed(NavigationInputCommand::Back, 1000);
    // Bubbled WM_XBUTTONUP arriving before PointerReleased is suppressed.
    EXPECT_TRUE(dedup.ShouldSuppressWin32Command(WM_XBUTTONUP, 0, NavigationInputCommand::Back, 1040));
    // Subsequent WM_APPCOMMAND(FAPPCOMMAND_MOUSE) within release window is also suppressed.
    EXPECT_TRUE(dedup.ShouldSuppressWin32Command(WM_APPCOMMAND, mouseBackAppCmd, NavigationInputCommand::Back, 1050));
    // Non-client click later without a WinUI press is NOT suppressed.
    EXPECT_FALSE(dedup.ShouldSuppressWin32Command(WM_NCXBUTTONUP, 0, NavigationInputCommand::Back, 1500));
}

TEST(NavigationInput, DeduplicatorSuppressesAppCommandAfterLongHoldRelease)
{
    MouseNavigationDeduplicator dedup;
    LPARAM const mouseFwdAppCmd =
        static_cast<LPARAM>(static_cast<DWORD>(APPCOMMAND_BROWSER_FORWARD | FAPPCOMMAND_MOUSE) << 16);

    dedup.OnPointerPressed(NavigationInputCommand::Forward, 1000);
    dedup.OnPointerReleased(NavigationInputCommand::Forward, 1600);
    EXPECT_TRUE(dedup.ShouldSuppressWin32Command(WM_APPCOMMAND, mouseFwdAppCmd, NavigationInputCommand::Forward, 1610));
    EXPECT_FALSE(dedup.ShouldSuppressWin32Command(WM_APPCOMMAND, mouseFwdAppCmd, NavigationInputCommand::Forward, 2000));
}

TEST(NavigationInput, DeduplicatorNeverSuppressesKeyboardAppCommand)
{
    MouseNavigationDeduplicator dedup;
    LPARAM const keyBackAppCmd =
        static_cast<LPARAM>(static_cast<DWORD>(APPCOMMAND_BROWSER_BACKWARD | FAPPCOMMAND_KEY) << 16);

    dedup.OnPointerPressed(NavigationInputCommand::Back, 1000);
    EXPECT_FALSE(dedup.ShouldSuppressWin32Command(WM_APPCOMMAND, keyBackAppCmd, NavigationInputCommand::Back, 1010));
}

TEST(NavigationInput, DeduplicatorExpiresOrResetsStalePress)
{
    MouseNavigationDeduplicator dedup;

    dedup.OnPointerPressed(NavigationInputCommand::Back, 1000);
    EXPECT_FALSE(dedup.ShouldSuppressWin32Command(
        WM_NCXBUTTONUP, 0, NavigationInputCommand::Back, 1000 + MouseNavigationDeduplicator::kMaxPressHoldSuppressMs + 1));

    dedup.OnPointerPressed(NavigationInputCommand::Forward, 5000);
    dedup.ResetPressedState();
    EXPECT_FALSE(dedup.ShouldSuppressWin32Command(WM_NCXBUTTONUP, 0, NavigationInputCommand::Forward, 5050));
}
