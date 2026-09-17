#pragma once

#include "velocitycopy/copy_job.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace velocitycopy {

enum class ShellAction : std::uint8_t {
    CopySelection,
    PasteToFolder,
    CopySelectionTo,
    CopySelectionPromptDestination,
    OpenVelocityCopy,
};

struct ShellRequest {
    std::uint32_t version{1};
    ShellAction action{ShellAction::OpenVelocityCopy};
    std::vector<std::filesystem::path> sources;
    std::filesystem::path destination;
    DestinationLayout layout{DestinationLayout::PreserveSourceFolder};
};

[[nodiscard]] constexpr bool shell_action_valid(const ShellAction action) noexcept {
    return action >= ShellAction::CopySelection && action <= ShellAction::OpenVelocityCopy;
}

[[nodiscard]] constexpr bool shell_request_requires_sources(const ShellAction action) noexcept {
    return action == ShellAction::CopySelection ||
        action == ShellAction::CopySelectionTo ||
        action == ShellAction::CopySelectionPromptDestination;
}

[[nodiscard]] constexpr bool shell_request_requires_destination(const ShellAction action) noexcept {
    return action == ShellAction::PasteToFolder || action == ShellAction::CopySelectionTo;
}

[[nodiscard]] inline bool shell_request_valid(const ShellRequest& request) noexcept {
    if (request.version != 1 || !shell_action_valid(request.action)) {
        return false;
    }
    if (shell_request_requires_sources(request.action) && request.sources.empty()) {
        return false;
    }
    if (shell_request_requires_destination(request.action) && request.destination.empty()) {
        return false;
    }
    return true;
}

} // namespace velocitycopy
