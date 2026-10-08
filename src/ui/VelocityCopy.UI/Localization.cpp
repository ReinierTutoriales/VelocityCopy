#include "pch.h"
#include "Localization.h"
#include "velocitycopy/diagnostics.hpp"

#include <filesystem>
#include <mutex>

namespace velocitycopy::localization {
namespace {
using ResourceManager = winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceManager;

std::once_flag init_once;
std::optional<ResourceManager> manager;

std::filesystem::path executable_directory() {
    std::wstring buffer(260, L'\0');
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()));
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }
        if (buffer.size() >= 32768) {
            throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
        }
        buffer.resize((std::min<std::size_t>)(buffer.size() * 2, 32768));
    }
}

void initialize_once() {
    const auto pri = executable_directory() / kPriFileName;
    if (!std::filesystem::is_regular_file(pri)) {
        log_diagnostic(L"localization: required PRI is missing: " + pri.wstring());
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    }
    manager.emplace(winrt::hstring(pri.c_str()));
}

winrt::hstring lookup(std::wstring_view key, std::wstring_view language) {
    std::call_once(init_once, initialize_once);
    auto map = manager.value().MainResourceMap();
    auto context = manager.value().CreateResourceContext();
    if (!language.empty()) {
        context.QualifierValues().Insert(L"Language", winrt::hstring(language));
    }
    const std::wstring resource_name = L"Resources/" + std::wstring(key);
    return map.GetValue(winrt::hstring(resource_name), context).ValueAsString();
}

void log_hresult(std::wstring_view operation, winrt::hresult_error const& error) noexcept {
    try {
        log_diagnostic(
            L"localization: " + std::wstring(operation) +
            L" HRESULT=0x" + std::format(L"{:08X}", static_cast<std::uint32_t>(error.code().value)));
    } catch (...) {}
}
}

bool initialize() noexcept {
    try {
        std::call_once(init_once, initialize_once);
        return manager.has_value();
    } catch (winrt::hresult_error const& error) {
        log_hresult(L"PRI initialization failed", error);
        return false;
    } catch (std::exception const&) {
        log_diagnostic(L"localization: PRI initialization failed with standard exception");
        return false;
    } catch (...) {
        log_diagnostic(L"localization: PRI initialization failed with unknown exception");
        return false;
    }
}

winrt::hstring get_string(std::wstring_view key) noexcept {
    try {
        return lookup(key, {});
    } catch (winrt::hresult_error const& error) {
        log_hresult(L"resource lookup failed", error);
    } catch (...) {
        try { log_diagnostic(L"localization: resource lookup failed"); } catch (...) {}
    }
    try { return winrt::hstring(key); } catch (...) { return {}; }
}
}
