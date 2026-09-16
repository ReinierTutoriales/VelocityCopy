#pragma once

#include "velocitycopy/copy_job.hpp"
#include "velocitycopy/shell_request.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace velocitycopy {

enum class ShellDispatchStatus : std::uint8_t {
    Accepted,
    NoStagedSources,
    InvalidRequest,
};

struct ShellDispatchResult {
    ShellDispatchStatus status{ShellDispatchStatus::InvalidRequest};
    bool show_window{};
    std::optional<CopyJob> job;
};

class ShellSession final {
public:
    [[nodiscard]] ShellDispatchResult dispatch(const ShellRequest& request) noexcept;

    [[nodiscard]] const std::vector<std::filesystem::path>& staged_sources() const noexcept;
    void clear_staged_sources() noexcept;

private:
    std::vector<std::filesystem::path> staged_sources_;
    std::uint64_t next_job_id_{1};
};

} // namespace velocitycopy
