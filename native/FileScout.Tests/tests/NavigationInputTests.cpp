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

    dedup.OnPointerPressed(NavigationInputCommand::Back);
    EXPECT_TRUE(dedup.ShouldSuppressWin32Command(WM_XBUTTONUP, 0, NavigationInputCommand::Back, 1040));
    EXPECT_TRUE(dedup.ShouldSuppressWin32Command(WM_APPCOMMAND, mouseBackAppCmd, NavigationInputCommand::Back, 1050));
    EXPECT_FALSE(dedup.ShouldSuppressWin32Command(WM_NCXBUTTONUP, 0, NavigationInputCommand::Back, 1500));
}

TEST(NavigationInput, DeduplicatorSuppressesLiveHoldBeyondTwoSeconds)
{
    MouseNavigationDeduplicator dedup;
    LPARAM const mouseBackAppCmd =
        static_cast<LPARAM>(static_cast<DWORD>(APPCOMMAND_BROWSER_BACKWARD | FAPPCOMMAND_MOUSE) << 16);
    LPARAM const mouseFwdAppCmd =
        static_cast<LPARAM>(static_cast<DWORD>(APPCOMMAND_BROWSER_FORWARD | FAPPCOMMAND_MOUSE) << 16);

    dedup.OnPointerPressed(NavigationInputCommand::Back);
    EXPECT_TRUE(dedup.ShouldSuppressWin32Command(WM_XBUTTONUP, 0, NavigationInputCommand::Back, 5500));
    EXPECT_TRUE(dedup.ShouldSuppressWin32Command(WM_APPCOMMAND, mouseBackAppCmd, NavigationInputCommand::Back, 5510));
    dedup.OnPointerReleased(NavigationInputCommand::Back, 5515);

    dedup.OnPointerPressed(NavigationInputCommand::Forward);
    EXPECT_TRUE(dedup.ShouldSuppressWin32Command(WM_XBUTTONUP, 0, NavigationInputCommand::Forward, 9500));
    EXPECT_TRUE(dedup.ShouldSuppressWin32Command(WM_APPCOMMAND, mouseFwdAppCmd, NavigationInputCommand::Forward, 9510));
    dedup.OnPointerReleased(NavigationInputCommand::Forward, 9515);
}

TEST(NavigationInput, DeduplicatorSuppressesAppCommandAfterLongHoldRelease)
{
    MouseNavigationDeduplicator dedup;
    LPARAM const mouseFwdAppCmd =
        static_cast<LPARAM>(static_cast<DWORD>(APPCOMMAND_BROWSER_FORWARD | FAPPCOMMAND_MOUSE) << 16);

    dedup.OnPointerPressed(NavigationInputCommand::Forward);
    dedup.OnPointerReleased(NavigationInputCommand::Forward, 4600);
    EXPECT_TRUE(dedup.ShouldSuppressWin32Command(WM_APPCOMMAND, mouseFwdAppCmd, NavigationInputCommand::Forward, 4610));
    EXPECT_FALSE(dedup.ShouldSuppressWin32Command(WM_APPCOMMAND, mouseFwdAppCmd, NavigationInputCommand::Forward, 5000));
}

TEST(NavigationInput, DeduplicatorPreservesSeparateTitlebarClickWhileSuppressingDraggedRelease)
{
    MouseNavigationDeduplicator dedup;

    dedup.OnPointerPressed(NavigationInputCommand::Back);
    dedup.OnPointerReleased(NavigationInputCommand::Back, 1040);
    dedup.OnNonClientButtonDown(NavigationInputCommand::Back);
    EXPECT_FALSE(dedup.ShouldSuppressWin32Command(WM_NCXBUTTONUP, 0, NavigationInputCommand::Back, 1160));

    dedup.OnPointerPressed(NavigationInputCommand::Forward);
    dedup.OnPointerReleased(NavigationInputCommand::Forward, 2040);
    dedup.OnNonClientButtonDown(NavigationInputCommand::Forward);
    EXPECT_FALSE(dedup.ShouldSuppressWin32Command(WM_NCXBUTTONUP, 0, NavigationInputCommand::Forward, 2160));

    dedup.OnPointerPressed(NavigationInputCommand::Back);
    EXPECT_TRUE(dedup.ShouldSuppressWin32Command(WM_NCXBUTTONUP, 0, NavigationInputCommand::Back, 3100));
}

TEST(NavigationInput, DeduplicatorNeverSuppressesKeyboardAppCommand)
{
    MouseNavigationDeduplicator dedup;
    LPARAM const keyBackAppCmd =
        static_cast<LPARAM>(static_cast<DWORD>(APPCOMMAND_BROWSER_BACKWARD | FAPPCOMMAND_KEY) << 16);

    dedup.OnPointerPressed(NavigationInputCommand::Back);
    EXPECT_FALSE(dedup.ShouldSuppressWin32Command(WM_APPCOMMAND, keyBackAppCmd, NavigationInputCommand::Back, 1010));
}

TEST(NavigationInput, DeduplicatorResetsStalePressOnDeactivateOrNewTitlebarClick)
{
    MouseNavigationDeduplicator dedup;

    dedup.OnPointerPressed(NavigationInputCommand::Back);
    dedup.OnNonClientButtonDown(NavigationInputCommand::Back);
    EXPECT_FALSE(dedup.ShouldSuppressWin32Command(WM_NCXBUTTONUP, 0, NavigationInputCommand::Back, 4000));

    dedup.OnPointerPressed(NavigationInputCommand::Forward);
    dedup.ResetPressedState();
    EXPECT_FALSE(dedup.ShouldSuppressWin32Command(WM_NCXBUTTONUP, 0, NavigationInputCommand::Forward, 5050));
}
