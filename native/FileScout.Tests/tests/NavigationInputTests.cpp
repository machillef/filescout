#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/NavigationInput.h"

using FileScoutNative::Domain::NavigationInputCommand;
using FileScoutNative::Domain::ResolveNavigationInputCommand;

TEST(NavigationInput, XButton1MapsToBack)
{
    EXPECT_EQ(
        ResolveNavigationInputCommand(WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), 0),
        NavigationInputCommand::Back);
}

TEST(NavigationInput, XButton2MapsToForward)
{
    EXPECT_EQ(
        ResolveNavigationInputCommand(WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), 0),
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
