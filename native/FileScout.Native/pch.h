#pragma once

#include <windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shobjidl.h>
#include <propkey.h>

#undef GetCurrentTime

#include <algorithm>
#include <array>
#include <cwchar>
#include <iomanip>
#include <memory>
#include <ranges>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <future>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <microsoft.ui.xaml.window.h>

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>

#include <winrt/Windows.Storage.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.DragDrop.h>
