#pragma once
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
namespace velocitycopy::ui {
inline double token_double(const wchar_t* key, double fallback) noexcept { try { return winrt::unbox_value<double>(winrt::Microsoft::UI::Xaml::Application::Current().Resources().Lookup(winrt::box_value(key))); } catch (...) { return fallback; } }
inline winrt::Microsoft::UI::Xaml::Thickness token_thickness(const wchar_t* key, winrt::Microsoft::UI::Xaml::Thickness fallback) noexcept { try { return winrt::unbox_value<winrt::Microsoft::UI::Xaml::Thickness>(winrt::Microsoft::UI::Xaml::Application::Current().Resources().Lookup(winrt::box_value(key))); } catch (...) { return fallback; } }
inline void apply_text_style(winrt::Microsoft::UI::Xaml::Controls::TextBlock const& text, const wchar_t* key) noexcept { try { text.Style(winrt::Microsoft::UI::Xaml::Application::Current().Resources().Lookup(winrt::box_value(key)).as<winrt::Microsoft::UI::Xaml::Style>()); } catch (...) {} }
inline void apply_icon_style(winrt::Microsoft::UI::Xaml::Controls::FontIcon const& icon, const wchar_t* key) noexcept { try { icon.Style(winrt::Microsoft::UI::Xaml::Application::Current().Resources().Lookup(winrt::box_value(key)).as<winrt::Microsoft::UI::Xaml::Style>()); } catch (...) {} }
inline int token_int(const wchar_t* key, int fallback) noexcept { return static_cast<int>(token_double(key, static_cast<double>(fallback))); }
}
