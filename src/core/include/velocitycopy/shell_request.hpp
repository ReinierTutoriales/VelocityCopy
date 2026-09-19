#pragma once
#include "velocitycopy/copy_job.hpp"
#include <cstdint>
#include <filesystem>
#include <vector>
namespace velocitycopy {
enum class ShellAction : std::uint8_t { Transfer = 0, OpenVelocityCopy = 1 };
struct ShellRequest {
    std::uint32_t version{2};
    ShellAction action{ShellAction::OpenVelocityCopy};
    std::vector<std::filesystem::path> sources;
    std::filesystem::path destination;
    DestinationLayout layout{DestinationLayout::PreserveSourceFolder};
    FileOperation operation{FileOperation::Copy};
};
[[nodiscard]] constexpr bool shell_action_valid(ShellAction a) noexcept {
    return a == ShellAction::Transfer || a == ShellAction::OpenVelocityCopy;
}
[[nodiscard]] inline bool shell_request_valid(const ShellRequest& r) noexcept {
    if (r.version != 2 || !shell_action_valid(r.action) ||
        (r.operation != FileOperation::Copy && r.operation != FileOperation::Move) ||
        (r.layout != DestinationLayout::PreserveSourceFolder && r.layout != DestinationLayout::ContentsOnly)) return false;
    if (r.action == ShellAction::OpenVelocityCopy)
        return r.sources.empty() && r.destination.empty() && r.operation == FileOperation::Copy;
    auto valid_path = [](const std::filesystem::path& p) {
        return p.is_absolute() && p.native().find(L'\0') == std::wstring::npos;
    };
    if (r.sources.empty() || !valid_path(r.destination)) return false;
    for (const auto& source : r.sources) if (!valid_path(source)) return false;
    return true;
}
}
