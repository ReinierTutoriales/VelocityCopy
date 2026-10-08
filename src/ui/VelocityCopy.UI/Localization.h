#pragma once
#include <string_view>
#include <winrt/base.h>
#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>

namespace velocitycopy::localization {
inline constexpr wchar_t kPriFileName[] = L"VelocityCopy.WinUI.pri";
bool initialize() noexcept;
winrt::hstring get_string(std::wstring_view key) noexcept;
}
