#pragma once

#include "velocitycopy/shell_request.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace velocitycopy {

[[nodiscard]] bool launch_velocitycopy_with_request(
    const std::filesystem::path& executable,
    const ShellRequest& request) noexcept;

[[nodiscard]] std::optional<ShellRequest> read_inherited_shell_request(
    std::uintptr_t handle_value,
    std::size_t payload_size) noexcept;

} // namespace velocitycopy
