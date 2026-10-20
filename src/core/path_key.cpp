#include "velocitycopy/path_key.hpp"
#include "win32_handle.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <optional>
#include <system_error>

namespace velocitycopy {
namespace {

int ordinal_compare(const std::wstring_view left, const std::wstring_view right, const bool sensitive) noexcept {
    const auto result = CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
        right.data(), static_cast<int>(right.size()), sensitive ? FALSE : TRUE);
    if (result) return result - CSTR_EQUAL;
    // Key components are bounded by GetFullPathNameW. An invalid API input
    // must still leave a strict order, rather than accidentally equating names.
    return left.compare(right);
}

std::filesystem::path absolute_path(const std::filesystem::path& path) {
    if (path.empty() || path.native().find(L'\0') != std::wstring::npos) return {};
    std::array<wchar_t, 32768> buffer;
    const auto length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (!length || length >= buffer.size()) return {};
    std::wstring value(buffer.data(), length);
    std::replace(value.begin(), value.end(), L'/', L'\\');
    // This is only a comparison key; the original extended path is retained
    // by every caller for actual I/O and long-path behavior.
    if (value.size() >= 8 && ordinal_compare(std::wstring_view(value).substr(0, 8), L"\\\\?\\UNC\\", false) == 0)
        value = L"\\\\" + value.substr(8);
    else if (value.starts_with(L"\\\\?\\") && value.size() > 5 && value[5] == L':') value.erase(0, 4);
    return std::filesystem::path(std::move(value)).lexically_normal();
}

std::optional<bool> query_case(const std::filesystem::path& path, const bool strict = false) {
    auto native = path.native();
    if (native.size() >= 3 && native[1] == L':' && native[2] == L'\\') native = L"\\\\?\\" + native;
    else if (native.starts_with(L"\\\\") && !native.starts_with(L"\\\\?\\")) native = L"\\\\?\\UNC\\" + native.substr(2);
    detail::Win32Handle handle(CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (handle.get() == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_DRIVE)
            return std::nullopt;
        if (!strict) return false; // Unknown policies conservatively reserve case variants together.
        throw std::filesystem::filesystem_error("Unable to inspect directory case policy", path,
            std::error_code(static_cast<int>(error), std::system_category()));
    }
    FILE_CASE_SENSITIVE_INFO info{};
    if (GetFileInformationByHandleEx(handle.get(), FileCaseSensitiveInfo, &info, sizeof(info)))
        return (info.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR) != 0;
    const auto error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED || error == ERROR_INVALID_FUNCTION)
        return false;
    if (!strict) return false;
    throw std::filesystem::filesystem_error("Unable to inspect directory case policy", path,
        std::error_code(static_cast<int>(error), std::system_category()));
}

std::wstring_view take_component(std::wstring_view& key) noexcept {
    const auto end = key.find(L'\0');
    const auto component = key.substr(0, end);
    key.remove_prefix(end == std::wstring_view::npos ? key.size() : end + 1);
    return component;
}

int compare_component(const std::wstring_view left, const std::wstring_view right) noexcept {
    if (left.empty() || right.empty()) return left.compare(right);
    // A changed policy is a different snapshot. Execution verifies the leaf
    // policy again before mutation instead of trusting a stale plan.
    if (left.front() != right.front()) return left.front() < right.front() ? -1 : 1;
    return ordinal_compare(left.substr(1), right.substr(1), left.front() == L'S');
}

} // namespace

bool OrdinalNameLess::operator()(const std::wstring_view left, const std::wstring_view right) const noexcept {
    return ordinal_compare(left, right, case_sensitive) < 0;
}

bool PathKeyLess::operator()(std::wstring_view left, std::wstring_view right) const noexcept {
    while (!left.empty() && !right.empty()) {
        const auto a = take_component(left);
        const auto b = take_component(right);
        if (const auto compared = compare_component(a, b)) return compared < 0;
    }
    return left.empty() && !right.empty();
}

bool path_key_equal(const std::wstring_view left, const std::wstring_view right) noexcept {
    return !PathKeyLess{}(left, right) && !PathKeyLess{}(right, left);
}

bool path_key_is_prefix(std::wstring_view prefix, std::wstring_view candidate) noexcept {
    if (prefix.empty() || candidate.empty()) return false;
    while (!prefix.empty()) {
        if (candidate.empty()) return false;
        const auto a = take_component(prefix);
        const auto b = take_component(candidate);
        if (compare_component(a, b)) return false;
    }
    return true;
}

bool directory_case_sensitive(const std::filesystem::path& path) {
    const auto policy = query_case(path, true);
    if (!policy) throw std::filesystem::filesystem_error("Destination parent disappeared", path,
        std::error_code(ERROR_PATH_NOT_FOUND, std::system_category()));
    return *policy;
}

bool PathKeyFactory::directory_policy(const std::filesystem::path& path, const bool inherited) {
    if (const auto found = directories_.find(path.native()); found != directories_.end()) return found->second;
    const auto policy = query_case(path).value_or(inherited);
    directories_.emplace(path.native(), policy);
    return policy;
}

std::wstring PathKeyFactory::make_impl(const std::filesystem::path& path, bool* directory_case) {
    const auto absolute = absolute_path(path);
    if (absolute.empty()) return {};
    const auto& value = absolute.native();
    auto parent = absolute.root_path();
    std::wstring key = L"I" + parent.native();
    key.push_back(L'\0');
    // A UNC server by itself is not an openable directory; query the share
    // after appending its first component.
    bool sensitive = parent.native().starts_with(L"\\\\") ? false : directory_policy(parent, false);
    auto start = parent.native().size();
    while (start < value.size()) {
        const auto end = value.find(L'\\', start);
        const auto length = (end == std::wstring::npos ? value.size() : end) - start;
        if (length) {
            key.push_back(sensitive ? L'S' : L'I');
            key.append(value, start, length);
            key.push_back(L'\0');
            parent /= std::wstring_view(value.data() + start, length);
            if (end != std::wstring::npos || directory_case) sensitive = directory_policy(parent, sensitive);
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    if (directory_case) *directory_case = sensitive;
    return key;
}

std::wstring PathKeyFactory::make(const std::filesystem::path& path) { return make_impl(path, nullptr); }

bool PathKeyFactory::directory_case_sensitive(const std::filesystem::path& path) {
    bool policy = false;
    if (make_impl(path, &policy).empty()) throw std::filesystem::filesystem_error("Invalid directory", path,
        std::make_error_code(std::errc::invalid_argument));
    return policy;
}

} // namespace velocitycopy
