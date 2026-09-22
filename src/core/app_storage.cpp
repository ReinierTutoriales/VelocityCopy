#include "velocitycopy/app_storage.hpp"

#include <windows.h>
#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>

#include <algorithm>
#include <cwchar>
#include <system_error>

namespace velocitycopy {
namespace {
constexpr std::wstring_view kPrefix = L"VelocityCopy.Recovery.";
constexpr std::wstring_view kSuffix = L".vcq";

bool valid_guid(const std::wstring& value) noexcept {
    GUID guid{};
    return CLSIDFromString(value.c_str(), &guid) == S_OK;
}
} // namespace

std::optional<std::filesystem::path> app_data_directory() noexcept {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)) || raw == nullptr) {
        return std::nullopt;
    }
    try {
        std::filesystem::path dir = std::filesystem::path(raw) / L"VelocityCopy";
        CoTaskMemFree(raw);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec && !std::filesystem::is_directory(dir, ec)) return std::nullopt;
        return dir;
    } catch (...) {
        CoTaskMemFree(raw);
        return std::nullopt;
    }
}

std::wstring new_session_id() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return {};
    wchar_t buffer[39]{};
    if (StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer))) == 0) return {};
    return buffer;
}

std::filesystem::path recovery_file(const std::filesystem::path& dir, const std::wstring_view session_id) {
    return dir / (std::wstring(kPrefix) + std::wstring(session_id) + std::wstring(kSuffix));
}

std::optional<std::wstring> recovery_session_id(const std::filesystem::path& file_name) {
    const auto name = file_name.filename().wstring();
    if (name.size() <= kPrefix.size() + kSuffix.size() ||
        name.compare(0, kPrefix.size(), kPrefix) != 0 ||
        name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
        return std::nullopt;
    }
    const auto id = name.substr(kPrefix.size(), name.size() - kPrefix.size() - kSuffix.size());
    if (!valid_guid(id)) return std::nullopt;
    return id;
}

std::vector<std::filesystem::path> list_recovery_files(const std::filesystem::path& dir) noexcept {
    std::vector<std::filesystem::path> result;
    try {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec) || ec) return result;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const auto path = it->path();
            const auto name = path.filename().wstring();
            if (name.ends_with(L".vcq.tmp")) {
                std::error_code remove_ec;
                std::filesystem::remove(path, remove_ec);
                continue;
            }
            std::error_code type_ec;
            if (std::filesystem::is_regular_file(path, type_ec) && !type_ec && recovery_session_id(path)) {
                result.push_back(path);
            }
        }
        std::sort(result.begin(), result.end());
    } catch (...) {
        result.clear();
    }
    return result;
}

void retire_recovery_file(const std::filesystem::path& path, const std::wstring_view fallback_suffix) noexcept {
    try {
        std::error_code ec;
        if (std::filesystem::remove(path, ec)) return;
        ec.clear();
        if (!std::filesystem::exists(path, ec) || ec) return;
        auto retired = path;
        retired += fallback_suffix;
        std::filesystem::remove(retired, ec);
        ec.clear();
        std::filesystem::rename(path, retired, ec);
    } catch (...) {
    }
}

} // namespace velocitycopy
