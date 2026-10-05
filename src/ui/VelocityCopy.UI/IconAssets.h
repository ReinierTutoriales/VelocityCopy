#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <windows.h>
#include <winrt/base.h>

namespace velocitycopy::ui {
inline const std::wstring& application_icon_path() {
    static const auto path = [] {
        std::array<wchar_t, 32768> module_path{};
        const auto length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
        if (!length) winrt::throw_last_error();
        if (length >= module_path.size()) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
        return (std::filesystem::path{module_path.data()}.parent_path() / L"Assets" / L"VelocityCopy.ico").wstring();
    }();
    return path;
}
}
