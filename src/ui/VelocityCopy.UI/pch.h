#pragma once

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <unknwn.h>
#include <microsoft.ui.xaml.window.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.Windows.AppLifecycle.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>
#include <winrt/Microsoft.Windows.Storage.Pickers.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

// DragEventArgs::Modifiers is projected from Windows.ApplicationModel.DataTransfer.DragDrop.
// Keep the existing Microsoft::UI::Input call sites source-compatible with that projection.
namespace winrt::Microsoft::UI::Input {
namespace DragDrop = winrt::Windows::ApplicationModel::DataTransfer::DragDrop;
}

namespace winrt::Windows::ApplicationModel::DataTransfer::DragDrop {
inline constexpr DragDropModifiers operator&(
    const DragDropModifiers left,
    const DragDropModifiers right) noexcept {
    using underlying = std::underlying_type_t<DragDropModifiers>;
    return static_cast<DragDropModifiers>(
        static_cast<underlying>(left) & static_cast<underlying>(right));
}
}
