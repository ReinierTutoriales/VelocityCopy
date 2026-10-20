#pragma once
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/queue_archive.hpp"

namespace velocitycopy {
struct StreamingCopyOptions {
    std::size_t batch_entries{512};
    std::size_t buffered_entries{2048};
    // Optional durable checkpoint, invoked between batches before copying.
    // The archive retains a rediscovery job until the whole stream finishes.
    std::function<bool(const QueueArchive&)> checkpoint;
};

struct StreamingCopyResult {
    JobResult result;
    bool scan_complete{};
    std::uint64_t admitted_files{};
    std::size_t peak_buffered_entries{};
    QueueArchive recovery;
};

// Copy only. Move needs a separate discovery/removal protocol and is rejected
// rather than deleting source entries while their directory is enumerated.
// Pending batches are bounded. Collision reservations and directory history
// remain proportional to all discovered entries, not constant memory.
[[nodiscard]] StreamingCopyResult execute_streaming_copy(
    const CopyJob& job, ExecutionControl& control,
    const JobExecutionOptions& execution = {}, const StreamingCopyOptions& streaming = {},
    const JobProgressCallback& progress = {}) noexcept;
} // namespace velocitycopy
