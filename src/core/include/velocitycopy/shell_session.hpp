#pragma once
#include "velocitycopy/shell_request.hpp"
#include <optional>
namespace velocitycopy {
enum class ShellDispatchStatus : std::uint8_t { Accepted, InvalidRequest };
struct ShellDispatchResult {
    ShellDispatchStatus status{ShellDispatchStatus::InvalidRequest};
    bool show_window{};
    std::optional<CopyJob> job;
};
class ShellSession final {
public:
    [[nodiscard]] ShellDispatchResult dispatch(const ShellRequest& request);
private:
    std::uint64_t next_job_id_{1};
};
}
