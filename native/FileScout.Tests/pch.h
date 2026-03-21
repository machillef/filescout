#pragma once

// pch for test project.
// Includes needed to compile domain, ContextMenuService (pure C++),
// and ShellInteropService (requires COM + winrt::com_ptr).

// COM/Shell headers — included before <winrt/base.h> as required.
#include <windows.h>
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shobjidl.h>
#include <propkey.h>
#include <commctrl.h>

#undef GetCurrentTime

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// CppWinRT base (winrt::com_ptr) — headers from main project's Generated Files.
#include <winrt/base.h>
