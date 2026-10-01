#pragma once
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
namespace velocitycopy::ui {
inline double token_double(const wchar_t* key, double fallback) noexcept { try { return winrt::unbox_value<double>(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(winrt::box_value(key))); } catch (...) { return fallback; } }
inline Microsoft::UI::Xaml::Thickness token_thickness(const wchar_t* key, Microsoft::UI::Xaml::Thickness fallback) noexcept { try { return winrt::unbox_value<Microsoft::UI::Xaml::Thickness>(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(winrt::box_value(key))); } catch (...) { return fallback; } }
inline int token_int(const wchar_t* key, int fallback) noexcept { return static_cast<int>(token_double(key, static_cast<double>(fallback))); }
}
