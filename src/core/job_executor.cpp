#include "velocitycopy/job_executor.hpp"

#include <windows.h>

#include <algorithm>
#include <limits>
#include <mutex>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

namespace velocitycopy {
namespace {

constexpr std::uint32_t kMaxCopyWorkers = 4;

void remove_partial_destination(const std::filesystem::path& destination) noexcept {
    std::error_code ec;
    (void)std::filesystem::remove(destination, ec);
}

bool destination_is_safe_to_discard(const std::filesystem::path& destination) noexcept {
    std::error_code ec;
    const bool existed = std::filesystem::exists(destination, ec);
    return !ec && !existed;
}

bool is_destination_conflict(const std::int32_t code) noexcept {
    return code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_FILE_EXISTS)) ||
           code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
}

struct ConcurrentProgressState {
    mutable std::mutex mutex;
    std::uint64_t completed_bytes{};
    std::uint64_t completed_files{};
    std::unordered_map<std::uint64_t, std::uint64_t> active_bytes;

    ConcurrentProgressState(
        const std::uint64_t initial_completed_bytes,
        const std::uint64_t initial_completed_files) noexcept
        : completed_bytes(initial_completed_bytes),
          completed_files(initial_completed_files) {}

    void update_active(const PlannedFile& file, const std::uint64_t transferred) {
        std::lock_guard lock(mutex);
        auto& current = active_bytes[file.id];
        current = std::max(current, std::min(transferred, file.size));
    }

    void complete(const PlannedFile& file) {
        std::lock_guard lock(mutex);
        active_bytes.erase(file.id);
        if (std::numeric_limits<std::uint64_t>::max() - completed_bytes < file.size) {
            completed_bytes = std::numeric_limits<std::uint64_t>::max();
        } else {
            completed_bytes += file.size;
        }
        if (completed_files != std::numeric_limits<std::uint64_t>::max()) {
            ++completed_files;
        }
    }

    void release(const std::uint64_t file_id) {
        std::lock_guard lock(mutex);
        active_bytes.erase(file_id);
    }

    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> totals() const {
        std::lock_guard lock(mutex);
        std::uint64_t transferred = completed_bytes;
        for (const auto& [file_id, partial] : active_bytes) {
            (void)file_id;
            if (std::numeric_limits<std::uint64_t>::max() - transferred < partial) {
                transferred = std::numeric_limits<std::uint64_t>::max();
                break;
            }
            transferred += partial;
        }
        return {transferred, completed_files};
    }
};

struct ConcurrentResultState {
    mutable std::mutex mutex;
    std::int32_t first_error{S_OK};
    bool destination_conflict{};
    std::uint64_t conflict_file_id{};
    std::filesystem::path conflict_source;
    std::filesystem::path conflict_destination;

    void record_error(const std::int32_t code, const PlannedFile* file = nullptr) {
        std::lock_guard lock(mutex);
        if (first_error != S_OK) return;
        first_error = code;
        if (file != nullptr && is_destination_conflict(code)) {
            destination_conflict = true;
            conflict_file_id = file->id;
            conflict_source = file->source;
            conflict_destination = file->destination;
        }
    }

    [[nodiscard]] std::int32_t error() const {
        std::lock_guard lock(mutex);
        return first_error;
    }

    [[nodiscard]] JobResult failure_result() const {
        std::lock_guard lock(mutex);
        return {
            false, false, first_error, false,
            destination_conflict, conflict_file_id,
            conflict_source, conflict_destination,
        };
    }
};

WorkloadProfile workload_from_plan(const CopyPlan& plan) noexcept {
    return {plan.total_bytes, static_cast<std::uint64_t>(plan.files.size()), plan.largest_file_bytes};
}

WorkloadProfile workload_from_live_plan(const LiveCopyPlan& plan) noexcept {
    const auto total_bytes = plan.total_bytes();
    const auto completed_bytes = plan.completed_bytes();
    return {
        completed_bytes < total_bytes ? total_bytes - completed_bytes : 0,
        plan.remaining_files(),
        plan.largest_file_bytes(),
    };
}

JobExecutionOptions recommend_for_roots(
    const StorageProfiler& profiler,
    const StrategySelector& selector,
    const std::vector<std::filesystem::path>& source_roots,
    const std::filesystem::path& destination_root,
    const WorkloadProfile& workload) noexcept {
    JobExecutionOptions options{};
    if (source_roots.empty() || destination_root.empty() || workload.file_count < 2) return options;

    const auto destination = profiler.inspect(destination_root);
    std::uint32_t worker_count = kMaxCopyWorkers;
    for (const auto& source_path : source_roots) {
        const auto source = profiler.inspect(source_path);
        const auto recommendation = selector.choose(source, destination, workload);
        worker_count = std::min(worker_count, recommendation.suggested_queue_depth);
        if (worker_count <= 1) return options;
    }

    options.worker_count = std::clamp<std::uint32_t>(
        worker_count, 1,
        static_cast<std::uint32_t>(std::min<std::uint64_t>(workload.file_count, kMaxCopyWorkers)));
    return options;
}

} // namespace

JobResult JobExecutor::execute(const CopyJob& job, const JobProgressCallback& progress) const noexcept {
    try {
        return execute(planner_.build(job), progress);
    } catch (const std::filesystem::filesystem_error& error) {
        const auto code = error.code().value();
        return {false, false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(code == 0 ? ERROR_INVALID_DATA : code))};
    } catch (...) {
        return {false, false, static_cast<std::int32_t>(E_FAIL)};
    }
}

JobResult JobExecutor::execute(const CopyPlan& plan, const JobProgressCallback& progress) const noexcept {
    try {
        for (const auto& directory : plan.directories) {
            std::error_code ec;
            std::filesystem::create_directories(directory.destination, ec);
            if (ec) return {false, false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ec.value()))};
        }

        std::uint64_t completed_bytes = 0;
        std::uint64_t completed_files = 0;
        for (const auto& file : plan.files) {
            const auto result = engine_.copy_file(
                file.source, file.destination,
                [&](const CopyProgress& file_progress) {
                    if (!progress) return CopyDecision::Continue;
                    JobProgress aggregate{};
                    aggregate.total_bytes = plan.total_bytes;
                    aggregate.transferred_bytes = completed_bytes + file_progress.transferred_bytes;
                    aggregate.total_files = plan.files.size();
                    aggregate.completed_files = completed_files;
                    aggregate.current_file_id = file.id;
                    aggregate.current_source = file.source;
                    aggregate.current_destination = file.destination;
                    return progress(aggregate) == JobDecision::Cancel ? CopyDecision::Cancel : CopyDecision::Continue;
                });

            if (!result.success) {
                const bool cancelled = result.native_code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED));
                const bool conflict = is_destination_conflict(result.native_code);
                return {
                    false, cancelled, result.native_code, false,
                    conflict, conflict ? file.id : 0,
                    conflict ? file.source : std::filesystem::path{},
                    conflict ? file.destination : std::filesystem::path{},
                };
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

JobResult JobExecutor::execute(LiveCopyPlan& plan, const JobProgressCallback& progress) const noexcept {
    ExecutionControl control;
    return execute(plan, control, recommend_options(plan), progress);
}

JobResult JobExecutor::execute(
    LiveCopyPlan& plan, ExecutionControl& control,
    const JobProgressCallback& progress) const noexcept {
    return execute(plan, control, recommend_options(plan), progress);
}

JobExecutionOptions JobExecutor::recommend_options(const CopyJob& job, const CopyPlan& plan) const noexcept {
    return recommend_for_roots(storage_profiler_, strategy_selector_, job.sources, job.destination, workload_from_plan(plan));
}

JobExecutionOptions JobExecutor::recommend_options(const LiveCopyPlan& plan) const noexcept {
    return recommend_for_roots(
        storage_profiler_, strategy_selector_, plan.source_roots(),
        plan.destination_root(), workload_from_live_plan(plan));
}

JobResult JobExecutor::execute(
    LiveCopyPlan& plan,
    ExecutionControl& control,
    const JobExecutionOptions& options,
    const JobProgressCallback& progress) const noexcept {
    try {
        for (const auto& directory : plan.directories()) {
            std::error_code ec;
            std::filesystem::create_directories(directory.destination, ec);
            if (ec) return {false, false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ec.value()))};
        }

        const auto remaining_files = plan.remaining_files();
        if (remaining_files == 0) return {true, false, S_OK};

        const auto worker_count = std::clamp<std::uint32_t>(
            options.worker_count, 1,
            static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining_files, kMaxCopyWorkers)));

        ConcurrentProgressState progress_state{plan.completed_bytes(), plan.completed_files()};
        ConcurrentResultState result_state;
        std::mutex callback_mutex;
        std::vector<JobResult> worker_results(worker_count, {true, false, S_OK, false});
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);

        auto emit_progress = [&](const PlannedFile& file, bool active, bool skippable) -> bool {
            if (!progress) return true;
            std::lock_guard callback_lock(callback_mutex);
            const auto [transferred, completed] = progress_state.totals();
            JobProgress aggregate{};
            aggregate.total_bytes = plan.total_bytes();
            aggregate.transferred_bytes = std::min(transferred, aggregate.total_bytes);
            aggregate.total_files = plan.total_files();
            aggregate.completed_files = std::min(completed, aggregate.total_files);
            aggregate.current_file_id = active ? file.id : 0;
            aggregate.current_file_skippable = active && skippable;
            aggregate.current_source = file.source;
            aggregate.current_destination = file.destination;
            if (progress(aggregate) == JobDecision::Cancel) {
                control.request_cancel();
                return false;
            }
            return true;
        };

        for (std::uint32_t worker_index = 0; worker_index < worker_count; ++worker_index) {
            workers.emplace_back([&, worker_index] {
                try {
                    for (;;) {
                        auto directive = control.directive();
                        if (directive == ExecutionDirective::Pause) directive = control.wait_while_paused();
                        if (directive == ExecutionDirective::Cancel) {
                            worker_results[worker_index] = {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false};
                            return;
                        }
                        if (directive == ExecutionDirective::Stop) {
                            worker_results[worker_index] = {false, false, S_OK, true};
                            return;
                        }

                        auto file = plan.acquire_next();
                        if (!file) {
                            worker_results[worker_index] = {true, false, S_OK, false};
                            return;
                        }

                        const auto file_id = file->id;
                        directive = control.directive();
                        if (directive == ExecutionDirective::Cancel || directive == ExecutionDirective::Stop) {
                            plan.release_active(file_id);
                            worker_results[worker_index] = directive == ExecutionDirective::Cancel
                                ? JobResult{false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false}
                                : JobResult{false, false, S_OK, true};
                            return;
                        }
                        if (directive == ExecutionDirective::Pause) {
                            directive = control.wait_while_paused();
                            if (directive == ExecutionDirective::Cancel || directive == ExecutionDirective::Stop) {
                                plan.release_active(file_id);
                                worker_results[worker_index] = directive == ExecutionDirective::Cancel
                                    ? JobResult{false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false}
                                    : JobResult{false, false, S_OK, true};
                                return;
                            }
                        }

                        const bool skip_allowed = destination_is_safe_to_discard(file->destination);
                        if (!skip_allowed) {
                            (void)control.consume_skip(file_id);
                        } else if (control.consume_skip(file_id)) {
                            progress_state.release(file_id);
                            if (!plan.skip_active(file_id)) {
                                result_state.record_error(static_cast<std::int32_t>(E_FAIL));
                                control.request_cancel();
                                worker_results[worker_index] = {false, false, static_cast<std::int32_t>(E_FAIL), false};
                                return;
                            }
                            if (!emit_progress(*file, false, false)) {
                                worker_results[worker_index] = {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false};
                                return;
                            }
                            continue;
                        }

                        bool resume_from_pause = false;
                        bool skipped = false;
                        for (;;) {
                            bool skip_requested = false;
                            const auto existing_policy = options.replace_file_id == file_id
                                ? ExistingDestinationPolicy::Replace
                                : options.existing_destination;
                            const auto result = engine_.copy_file(
                                file->source, file->destination,
                                CopyOptions{resume_from_pause, existing_policy},
                                [&](const CopyProgress& file_progress) {
                                    progress_state.update_active(*file, file_progress.transferred_bytes);
                                    if (skip_allowed && control.consume_skip(file_id)) {
                                        skip_requested = true;
                                        return CopyDecision::Skip;
                                    }
                                    const auto current = control.directive();
                                    if (current == ExecutionDirective::Pause) return CopyDecision::Pause;
                                    if (current == ExecutionDirective::Cancel) return CopyDecision::Cancel;
                                    return emit_progress(*file, true, skip_allowed) ? CopyDecision::Continue : CopyDecision::Cancel;
                                });

                            if (result.success) break;

                            if (skip_requested) {
                                progress_state.release(file_id);
                                remove_partial_destination(file->destination);
                                if (!plan.skip_active(file_id)) {
                                    result_state.record_error(static_cast<std::int32_t>(E_FAIL));
                                    control.request_cancel();
                                    worker_results[worker_index] = {false, false, static_cast<std::int32_t>(E_FAIL), false};
                                    return;
                                }
                                skipped = true;
                                break;
                            }

                            if (result.native_code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_PAUSED))) {
                                const auto next = control.wait_while_paused();
                                if (next == ExecutionDirective::Cancel) {
                                    progress_state.release(file_id);
                                    if (skip_allowed) remove_partial_destination(file->destination);
                                    plan.release_active(file_id);
                                    worker_results[worker_index] = {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false};
                                    return;
                                }
                                resume_from_pause = true;
                                continue;
                            }

                            progress_state.release(file_id);
                            const bool cancelled = result.native_code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED));
                            if (cancelled && skip_allowed) remove_partial_destination(file->destination);
                            plan.release_active(file_id);
                            if (!cancelled) result_state.record_error(result.native_code, &*file);
                            control.request_cancel();
                            worker_results[worker_index] = {false, cancelled, result.native_code, false};
                            return;
                        }

                        if (skipped) {
                            if (!emit_progress(*file, false, false)) {
                                worker_results[worker_index] = {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false};
                                return;
                            }
                            continue;
                        }

                        progress_state.complete(*file);
                        plan.complete_active(file_id);
                        if (!emit_progress(*file, false, false)) {
                            worker_results[worker_index] = {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false};
                            return;
                        }

                        if (options.replace_file_id == file_id) {
                            worker_results[worker_index] = {true, false, S_OK, false};
                            return;
                        }

                        if (control.directive() == ExecutionDirective::Stop) {
                            worker_results[worker_index] = {false, false, S_OK, true};
                            return;
                        }
                    }
                } catch (const std::filesystem::filesystem_error& error) {
                    const auto code = error.code().value();
                    const auto native = static_cast<std::int32_t>(HRESULT_FROM_WIN32(code == 0 ? ERROR_INVALID_DATA : code));
                    result_state.record_error(native);
                    control.request_cancel();
                    worker_results[worker_index] = {false, false, native, false};
                } catch (...) {
                    result_state.record_error(static_cast<std::int32_t>(E_FAIL));
                    control.request_cancel();
                    worker_results[worker_index] = {false, false, static_cast<std::int32_t>(E_FAIL), false};
                }
            });
        }

        for (auto& worker : workers) worker.join();

        if (result_state.error() != S_OK) return result_state.failure_result();

        bool stopped = false;
        bool cancelled = false;
        for (const auto& result : worker_results) {
            stopped = stopped || result.stopped;
            cancelled = cancelled || result.cancelled;
        }
        if (stopped || control.directive() == ExecutionDirective::Stop) return {false, false, S_OK, true};
        if (cancelled || control.directive() == ExecutionDirective::Cancel) {
            return {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false};
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
