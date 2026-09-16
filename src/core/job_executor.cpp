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

JobResult JobExecutor::execute(
    LiveCopyPlan& plan,
    const JobProgressCallback& progress) const noexcept {
    ExecutionControl control;
    return execute(plan, control, progress);
}

JobResult JobExecutor::execute(
    LiveCopyPlan& plan,
    ExecutionControl& control,
    const JobProgressCallback& progress) const noexcept {
    try {
        for (const auto& directory : plan.directories()) {
            std::error_code ec;
            std::filesystem::create_directories(directory.destination, ec);
            if (ec) {
                return {false, false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ec.value()))};
            }
        }

        std::uint64_t completed_bytes = 0;
        std::uint64_t completed_files = 0;

        for (;;) {
            auto directive = control.directive();
            if (directive == ExecutionDirective::Pause) {
                directive = control.wait_while_paused();
            }
            if (directive == ExecutionDirective::Cancel) {
                return {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED))};
            }
            if (directive == ExecutionDirective::Stop) {
                return {false, false, S_OK, true};
            }

            auto file = plan.acquire_next();
            if (!file) {
                return {true, false, S_OK};
            }

            const auto file_id = file->id;
            bool resume_from_pause = false;

            for (;;) {
                const auto result = engine_.copy_file(
                    file->source,
                    file->destination,
                    CopyOptions{resume_from_pause},
                    [&](const CopyProgress& file_progress) {
                        const auto current = control.directive();
                        if (current == ExecutionDirective::Pause) {
                            return CopyDecision::Pause;
                        }
                        if (current == ExecutionDirective::Cancel) {
                            return CopyDecision::Cancel;
                        }

                        if (!progress) {
                            return CopyDecision::Continue;
                        }

                        JobProgress aggregate{};
                        aggregate.total_bytes = plan.total_bytes();
                        aggregate.transferred_bytes = completed_bytes + file_progress.transferred_bytes;
                        aggregate.total_files = plan.total_files();
                        aggregate.completed_files = completed_files;
                        aggregate.current_source = file->source;
                        aggregate.current_destination = file->destination;

                        return progress(aggregate) == JobDecision::Cancel
                            ? CopyDecision::Cancel
                            : CopyDecision::Continue;
                    });

                if (result.success) {
                    break;
                }

                if (result.native_code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_PAUSED))) {
                    auto next = control.wait_while_paused();
                    if (next == ExecutionDirective::Cancel) {
                        std::error_code remove_error;
                        std::filesystem::remove(file->destination, remove_error);
                        plan.release_active(file_id);
                        return {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED))};
                    }
                    resume_from_pause = true;
                    continue;
                }

                plan.release_active(file_id);
                const bool cancelled = result.native_code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED));
                return {false, cancelled, result.native_code};
            }

            completed_bytes += file->size;
            ++completed_files;
            plan.complete_active(file_id);

            if (progress) {
                JobProgress aggregate{};
                aggregate.total_bytes = plan.total_bytes();
                aggregate.transferred_bytes = completed_bytes;
                aggregate.total_files = plan.total_files();
                aggregate.completed_files = completed_files;
                aggregate.current_source = file->source;
                aggregate.current_destination = file->destination;

                if (progress(aggregate) == JobDecision::Cancel) {
                    return {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED))};
                }
            }

            if (control.directive() == ExecutionDirective::Stop) {
                return {false, false, S_OK, true};
            }
        }
    } catch (const std::filesystem::filesystem_error& error) {
        const auto code = error.code().value();
        return {false, false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(code == 0 ? ERROR_INVALID_DATA : code))};
    } catch (...) {
        return {false, false, static_cast<std::int32_t>(E_FAIL)};
    }
}

} // namespace velocitycopy
