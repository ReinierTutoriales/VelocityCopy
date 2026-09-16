#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>

namespace velocitycopy {

struct CopyProgress {
    std::uint64_t total_bytes{};
    std::uint64_t transferred_bytes{};
};

enum class CopyDecision {
    Continue,
    Pause,
    Stop,
    Skip,
    Cancel,
};

using ProgressCallback = std::function<CopyDecision(const CopyProgress&)>;

struct CopyOptions {
    bool resume_from_pause{};
};

struct CopyResult {
    bool success{};
    std::int32_t native_code{};
};

class CopyEngine final {
public:
    [[nodiscard]] CopyResult copy_file(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        const ProgressCallback& progress = {}) const noexcept;

    [[nodiscard]] CopyResult copy_file(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        const CopyOptions& options,
        const ProgressCallback& progress = {}) const noexcept;
};

} // namespace velocitycopy
