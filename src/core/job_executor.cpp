#include "velocitycopy/job_executor.hpp"

#include <windows.h>

#include <system_error>

namespace velocitycopy {

JobResult JobExecutor::execute(
    const CopyJob& job,
    const JobProgressCallback& progress) const noexcept {
    try {
        return execute(planner_.build(job), progress);
    } catch (const std::filesystem::filesystem_error& error) {
        const auto code = error.code().value();
        return {false, false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(code == 0 ? ERROR_INVALID_DATA : code))};
    } catch (...) {
        return {false, false, static_cast<std::int32_t>(E_FAIL)};
    }
}

JobResult JobExecutor::execute(
    const CopyPlan& plan,
    const JobProgressCallback& progress) const noexcept {
    try {
        for (const auto& directory : plan.directories) {
            std::error_code ec;
            std::filesystem::create_directories(directory.destination, ec);
            if (ec) {
                return {false, false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ec.value()))};
            }
        }

        std::uint64_t completed_bytes = 0;
        std::uint64_t completed_files = 0;

        for (const auto& file : plan.files) {
            const auto result = engine_.copy_file(
                file.source,
                file.destination,
                [&](const CopyProgress& file_progress) {
                    if (!progress) {
                        return CopyDecision::Continue;
                    }

                    JobProgress aggregate{};
                    aggregate.total_bytes = plan.total_bytes;
                    aggregate.transferred_bytes = completed_bytes + file_progress.transferred_bytes;
                    aggregate.total_files = plan.files.size();
                    aggregate.completed_files = completed_files;
                    aggregate.current_source = file.source;
                    aggregate.current_destination = file.destination;

                    return progress(aggregate) == JobDecision::Cancel
                        ? CopyDecision::Cancel
                        : CopyDecision::Continue;
                });

            if (!result.success) {
                const bool cancelled = result.native_code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED));
                return {false, cancelled, result.native_code};
            }

            completed_bytes += file.size;
            ++completed_files;

            if (progress) {
                JobProgress aggregate{};
                aggregate.total_bytes = plan.total_bytes;
                aggregate.transferred_bytes = completed_bytes;
                aggregate.total_files = plan.files.size();
                aggregate.completed_files = completed_files;
                aggregate.current_source = file.source;
                aggregate.current_destination = file.destination;

                if (progress(aggregate) == JobDecision::Cancel) {
                    return {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED))};
                }
            }
        }

        return {true, false, S_OK};
    } catch (const std::filesystem::filesystem_error& error) {
        const auto code = error.code().value();
        return {false, false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(code == 0 ? ERROR_INVALID_DATA : code))};
    } catch (...) {
        return {false, false, static_cast<std::int32_t>(E_FAIL)};
    }
}

} // namespace velocitycopy
