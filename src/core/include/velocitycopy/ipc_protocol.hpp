#pragma once

#include "velocitycopy/shell_request.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace velocitycopy {

inline constexpr std::uint32_t kShellProtocolMagic = 0x31504356; // VCP1
inline constexpr std::size_t kMaxShellMessageBytes = 16 * 1024 * 1024;
inline constexpr std::size_t kMaxShellPathChars = 32768;
inline constexpr std::size_t kMaxShellSources = 65535;

[[nodiscard]] std::optional<std::vector<std::uint8_t>> serialize_shell_request(
    const ShellRequest& request) noexcept;

[[nodiscard]] std::optional<ShellRequest> deserialize_shell_request(
    std::span<const std::uint8_t> bytes) noexcept;

} // namespace velocitycopy
