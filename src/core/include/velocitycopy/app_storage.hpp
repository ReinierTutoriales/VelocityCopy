#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace velocitycopy {

std::optional<std::filesystem::path> app_data_directory() noexcept;
std::wstring new_session_id();
std::filesystem::path recovery_file(const std::filesystem::path& dir, std::wstring_view session_id);
std::optional<std::wstring> recovery_session_id(const std::filesystem::path& file_name);
std::vector<std::filesystem::path> list_recovery_files(
    const std::filesystem::path& dir,
    std::span<const std::wstring> active_session_ids = {}) noexcept;
void retire_recovery_file(const std::filesystem::path& path, std::wstring_view fallback_suffix = L".consumed") noexcept;

} // namespace velocitycopy
