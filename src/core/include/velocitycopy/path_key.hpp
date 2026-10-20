#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace velocitycopy {

// Binary, component-delimited comparison keys, never paths passed to Windows
// or serialized to a queue. Case policy is snapshotted per directory.
struct PathKeyLess {
    [[nodiscard]] bool operator()(std::wstring_view left, std::wstring_view right) const noexcept;
};

[[nodiscard]] bool path_key_equal(std::wstring_view left, std::wstring_view right) noexcept;
[[nodiscard]] bool path_key_is_prefix(std::wstring_view prefix, std::wstring_view candidate) noexcept;
[[nodiscard]] bool directory_case_sensitive(const std::filesystem::path& directory);

struct OrdinalNameLess {
    bool case_sensitive{};
    [[nodiscard]] bool operator()(std::wstring_view left, std::wstring_view right) const noexcept;
};

class PathKeyFactory final {
public:
    [[nodiscard]] std::wstring make(const std::filesystem::path& path);
    [[nodiscard]] bool directory_case_sensitive(const std::filesystem::path& path);
private:
    [[nodiscard]] std::wstring make_impl(const std::filesystem::path& path, bool* directory_case);
    [[nodiscard]] bool directory_policy(const std::filesystem::path& path, bool inherited);
    std::unordered_map<std::wstring, bool> directories_;
};

} // namespace velocitycopy
