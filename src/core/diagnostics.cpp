#include "velocitycopy/diagnostics.hpp"
#include "velocitycopy/app_storage.hpp"
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <format>
#include <string>
namespace velocitycopy {
namespace {
constexpr std::uintmax_t kMaxLogBytes = 256u * 1024u;
std::wstring timestamp() noexcept {
    SYSTEMTIME value{}; GetLocalTime(&value);
    try { return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", value.wYear,value.wMonth,value.wDay,value.wHour,value.wMinute,value.wSecond,value.wMilliseconds); } catch (...) { return L"unknown-time"; }
}
}
void log_diagnostic(std::wstring_view message) noexcept {
    try {
        const auto dir = app_data_directory(); if (!dir) return;
        const auto path = *dir / L"VelocityCopy.log";
        const auto rotated = *dir / L"VelocityCopy.log.1";
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (!ec && size >= kMaxLogBytes) {
            std::filesystem::remove(rotated, ec); ec.clear();
            std::filesystem::rename(path, rotated, ec);
        }
        std::wofstream stream(path, std::ios::app);
        if (!stream) return;
        stream << L"[" << timestamp() << L"] " << message << L"\n";
        stream.flush();
    } catch (...) {}
}
}
