#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/storage_topology.hpp"
#include "velocitycopy/source_removal_recovery.hpp"
#include "destination_path_guard.hpp"

#include <windows.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <iterator>
#include <mutex>
#include <new>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace velocitycopy {
namespace {

constexpr std::uint32_t kMaxCopyWorkers = 4;
constexpr std::int32_t kInvalidPlanState =
    static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
constexpr std::int32_t kUnhandledExecutorError =
    static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_UNHANDLED_EXCEPTION));

std::int32_t native_hresult(const std::error_code& code, const DWORD fallback = ERROR_INVALID_DATA) noexcept {
    const auto value = code.value();
    return static_cast<std::int32_t>(HRESULT_FROM_WIN32(value == 0 ? fallback : static_cast<DWORD>(value)));
}

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

// Destination-wide conditions invalidate every remaining item, so they stay
// session-fatal instead of becoming one Failed result per file. A future cut
// may turn them into pause + notice.
bool is_session_fatal(const std::int32_t code) noexcept {
    return code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_DISK_FULL)) ||
           code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_HANDLE_DISK_FULL)) ||
           code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED)) ||
           code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_READY));
}

std::int32_t remove_moved_source_file(const std::filesystem::path& source) noexcept {
    std::error_code ec;
    const bool removed = std::filesystem::remove(source, ec);
    if (ec) {
        return static_cast<std::int32_t>(HRESULT_FROM_WIN32(ec.value()));
    }
    if (removed) {
        return S_OK;
    }

    ec.clear();
    const bool still_exists = std::filesystem::exists(source, ec);
    if (ec) {
        return static_cast<std::int32_t>(HRESULT_FROM_WIN32(ec.value()));
    }
    return still_exists
        ? static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))
        : S_OK;
}

bool is_already_gone(const std::error_code& ec) noexcept {
    return ec.value() == ERROR_FILE_NOT_FOUND || ec.value() == ERROR_PATH_NOT_FOUND;
}

// Keep each ancestor pinned while descending. Delete only the directory whose
// non-reparse handle was inspected, and keep memory proportional to tree depth.
std::int32_t remove_empty_source_directories(
    const std::vector<std::filesystem::path>& source_roots,
    ExecutionControl& control) noexcept {
    struct Frame {
        std::filesystem::path path;
        HANDLE handle{INVALID_HANDLE_VALUE};
        std::filesystem::directory_iterator cursor;
        ~Frame() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    };
    try {
        for (const auto& root : source_roots) {
            std::error_code ec;
            if (!std::filesystem::is_directory(root, ec)) {
                if (ec && !is_already_gone(ec)) return native_hresult(ec);
                continue;
            }
            detail::DestinationPathGuard ancestors;
            if (!ancestors.lock_existing_chain(root.parent_path(), ec, FILE_SHARE_READ)) return native_hresult(ec);
            std::vector<std::unique_ptr<Frame>> stack;
            auto descend = [&](const std::filesystem::path& path) {
                auto frame = std::make_unique<Frame>();
                frame->path = path;
                frame->handle = CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (frame->handle == INVALID_HANDLE_VALUE) {
                    ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
                    return false;
                }
                if (!detail::safe_directory_handle(frame->handle, ec)) return false;
                frame->cursor = std::filesystem::directory_iterator(path, ec);
                if (ec) return false;
                stack.push_back(std::move(frame));
                return true;
            };
            if (!descend(root)) {
                if (is_already_gone(ec)) continue;
                return native_hresult(ec);
            }
            while (!stack.empty()) {
                auto directive = control.directive();
                if (directive == ExecutionDirective::Pause) directive = control.wait_while_paused();
                if (directive != ExecutionDirective::Run) return HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED);
                auto& frame = *stack.back();
                if (frame.cursor != std::filesystem::directory_iterator{}) {
                    const auto child = frame.cursor->path();
                    const bool directory = frame.cursor->is_directory(ec);
                    if (ec && !is_already_gone(ec)) return native_hresult(ec);
                    ec.clear();
                    frame.cursor.increment(ec);
                    if (ec && !is_already_gone(ec)) return native_hresult(ec);
                    ec.clear();
                    if (directory && !descend(child) && !is_already_gone(ec)) return native_hresult(ec);
                } else {
                    // Close enumeration before marking this pinned directory for deletion.
                    frame.cursor = {};
                    FILE_DISPOSITION_INFO disposition{TRUE};
                    if (!SetFileInformationByHandle(frame.handle, FileDispositionInfo,
                            &disposition, sizeof(disposition))) {
                        const auto native = GetLastError();
                        if (native != ERROR_DIR_NOT_EMPTY && native != ERROR_FILE_NOT_FOUND &&
                            native != ERROR_PATH_NOT_FOUND) return HRESULT_FROM_WIN32(native);
                    }
                    stack.pop_back();
                }
            }
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        return static_cast<std::int32_t>(E_OUTOFMEMORY);
    } catch (const std::system_error& error) {
        return native_hresult(error.code());
    } catch (...) {
        return kUnhandledExecutorError;
    }
}

struct ConcurrentResultState {
    mutable std::mutex mutex;
    std::int32_t first_error{S_OK};
    bool destination_conflict{};
    std::uint64_t conflict_file_id{};
    std::filesystem::path conflict_source;
    std::filesystem::path conflict_destination;

    void record_error(const std::int32_t code, const PlannedFile* file = nullptr) {
        std::lock_guard lock(mutex);
        if (first_error != S_OK) {
            return;
        }
        first_error = code;
        if (file != nullptr) {
            if (is_destination_conflict(code)) {
                destination_conflict = true;
            }
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
            false,
            false,
            first_error,
            false,
            destination_conflict,
            conflict_file_id,
            conflict_source,
            conflict_destination,
        };
    }
};

WorkloadProfile workload_from_plan(const CopyPlan& plan) noexcept {
    return {
        plan.total_bytes,
        static_cast<std::uint64_t>(plan.files.size()),
        plan.largest_file_bytes,
    };
}

WorkloadProfile workload_from_live_plan(const LiveCopyPlan& plan) noexcept {
    const auto view = plan.resolution_view();
    const auto& counters = view.counters;
    return {
        counters.resolution_weight < counters.resolution_total
            ? counters.resolution_total - counters.resolution_weight
            : 0,
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
    if (source_roots.empty() || destination_root.empty() || workload.file_count == 0) {
        return options;
    }

    const auto destination = profiler.inspect(destination_root);
    std::uint32_t worker_count = kMaxCopyWorkers;
    std::uint32_t shared_copy_flags = 0;
    std::uint32_t shared_buffer_bytes = 0;
    bool first_recommendation = true;
    bool source_destination_share_disk = false;

    for (const auto& source_path : source_roots) {
        const auto source = profiler.inspect(source_path);
        const auto recommendation = selector.choose(source, destination, workload);
        worker_count = std::min(worker_count, recommendation.suggested_queue_depth);

        source_destination_share_disk = source_destination_share_disk ||
            physical_storage_relationship(source, destination) == PhysicalStorageRelationship::SharedDisk;

        if (first_recommendation) {
            shared_copy_flags = recommendation.copy_flags;
            shared_buffer_bytes = recommendation.suggested_buffer_bytes;
            first_recommendation = false;
        } else {
            shared_copy_flags &= recommendation.copy_flags;
            shared_buffer_bytes = std::min(shared_buffer_bytes, recommendation.suggested_buffer_bytes);
        }
    }

    if (source_destination_share_disk) {
        worker_count = 1;
    }

    options.worker_count = std::clamp<std::uint32_t>(
        worker_count,
        1,
        static_cast<std::uint32_t>(std::min<std::uint64_t>(workload.file_count, kMaxCopyWorkers)));
    options.copy_flags = shared_copy_flags;
    options.suggested_buffer_bytes = shared_buffer_bytes;
    return options;
}

} // namespace

JobResult JobExecutor::execute(
    const CopyJob& job,
    const JobProgressCallback& progress) const noexcept {
    try {
        auto plan = planner_.build(job);
        LiveCopyPlan live_plan(std::move(plan));
        return execute(live_plan, progress);
    } catch (const std::filesystem::filesystem_error& error) {
        return {
            false,
            false,
            native_hresult(error.code()),
        };
    } catch (const std::bad_alloc&) {
        return {false, false, static_cast<std::int32_t>(E_OUTOFMEMORY)};
    } catch (const std::system_error& error) {
        return {false, false, native_hresult(error.code())};
    } catch (...) {
        return {false, false, kUnhandledExecutorError};
    }
}

JobResult JobExecutor::execute(
    const CopyPlan& plan,
    const JobProgressCallback& progress) const noexcept {
    try {
        LiveCopyPlan live_plan(plan);
        return execute(live_plan, progress);
    } catch (const std::filesystem::filesystem_error& error) {
        return {
            false,
            false,
            native_hresult(error.code()),
        };
    } catch (const std::bad_alloc&) {
        return {false, false, static_cast<std::int32_t>(E_OUTOFMEMORY)};
    } catch (const std::system_error& error) {
        return {false, false, native_hresult(error.code())};
    } catch (...) {
        return {false, false, kUnhandledExecutorError};
    }
}

JobResult JobExecutor::execute(
    LiveCopyPlan& plan,
    const JobProgressCallback& progress) const noexcept {
    ExecutionControl control;
    return execute(plan, control, recommend_options(plan), progress);
}

JobResult JobExecutor::execute(
    LiveCopyPlan& plan,
    ExecutionControl& control,
    const JobProgressCallback& progress) const noexcept {
    return execute(plan, control, recommend_options(plan), progress);
}

JobExecutionOptions JobExecutor::recommend_options(
    const CopyJob& job,
    const CopyPlan& plan) const noexcept {
    return recommend_for_roots(
        storage_profiler_,
        strategy_selector_,
        job.sources,
        job.destination,
        workload_from_plan(plan));
}

JobExecutionOptions JobExecutor::recommend_options(const LiveCopyPlan& plan) const noexcept {
    return recommend_for_roots(
        storage_profiler_,
        strategy_selector_,
        plan.source_roots(),
        plan.destination_root(),
        workload_from_live_plan(plan));
}

JobResult JobExecutor::execute(
    LiveCopyPlan& plan,
    ExecutionControl& control,
    const JobExecutionOptions& options,
    const JobProgressCallback& progress) const noexcept {
    try {
        auto finish = [&plan](JobResult result) noexcept {
            const auto view = plan.resolution_view();
            result.outcomes = view.outcomes;
            result.parked_files = view.parked_files;
            return result;
        };

        auto check_control = [&]() -> std::optional<JobResult> {
            auto directive = control.directive();
            if (directive == ExecutionDirective::Pause) directive = control.wait_while_paused();
            if (directive == ExecutionDirective::Cancel)
                return finish({false, true, HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED), false});
            if (directive == ExecutionDirective::Stop) return finish({false, false, S_OK, true});
            return std::nullopt;
        };
        if (const auto result = check_control()) return *result;

        // A RetrySourceRemoval is already copied. It is deliberately never
        // unparked into transfer work; a resumed decision session retries only
        // deletion of the original source.
        if (options.retry_source_removals) {
            for (const auto& recovery : plan.parked_source_removals()) {
                if (const auto result = check_control()) return *result;
                const auto validation = validate_source_removal_recovery(recovery);
                if (validation == SourceRemovalValidation::TemporarilyUnavailable) {
                    continue;
                }
                if (validation == SourceRemovalValidation::ChangedOrMissing) {
                    if (!plan.resolve_parked(
                            recovery.file_id, ItemOutcome::CopiedSourceRetained,
                            recovery.hresult, recovery.destination_preexisted)) {
                        return finish({false, false, kInvalidPlanState});
                    }
                    continue;
                }
                if (validation == SourceRemovalValidation::AlreadyRemoved) {
                    if (!plan.resolve_parked(
                            recovery.file_id, ItemOutcome::Succeeded, S_OK,
                            recovery.destination_preexisted)) {
                        return finish({false, false, kInvalidPlanState});
                    }
                    continue;
                }
                if (!plan.begin_parked_retry(
                        recovery.file_id, RecoveryAction::RetrySourceRemoval)) {
                    return finish({false, false, kInvalidPlanState});
                }
                if (const auto result = check_control()) return *result;
                const auto remove_source = remove_moved_source_file(recovery.source);
                if (remove_source == S_OK) {
                    if (!plan.resolve_parked(
                            recovery.file_id, ItemOutcome::Succeeded, S_OK,
                            recovery.destination_preexisted)) {
                        return finish({false, false, kInvalidPlanState});
                    }
                } else if (!plan.record_parked_retry_failure(recovery.file_id, remove_source)) {
                    return finish({false, false, kInvalidPlanState});
                }
            }
        }

        const auto directory_batch = plan.pending_directories();
        for (const auto& directory : directory_batch.directories) {
            if (const auto result = check_control()) return *result;
            detail::DestinationPathGuard directory_guard;
            std::error_code ec;
            if (!directory_guard.prepare_directory(directory.destination, ec)) {
                const auto code = native_hresult(ec, ERROR_CANT_ACCESS_FILE);
                if (is_session_fatal(code)) {
                    return finish({false, false, code});
                }
                // Planned directories use the same locked, non-reparse chain
                // as file parents. Empty directories therefore cannot be
                // materialized through a junction/symlink outside the target.
                (void)plan.fail_pending_under(directory.destination, code);
            }
        }
        plan.mark_directories_materialized(directory_batch.through_index);

        if (const auto result = check_control()) return *result;
        const auto remaining_files = plan.remaining_files();
        if (remaining_files == 0) {
            // Parked items are still unresolved work. In particular, a Move
            // may have copied its destination while source deletion is parked
            // for an explicit retry decision. Do not touch source directories
            // until every item has reached a terminal outcome.
            if (const auto result = check_control()) return *result;
            if (plan.operation() == FileOperation::Move && plan.unresolved_files() == 0) {
                const auto cleanup = remove_empty_source_directories(plan.source_roots(), control);
                if (const auto result = check_control()) return *result;
                if (cleanup != S_OK) {
                    return finish({false, false, cleanup});
                }
            }
            return finish({true, false, S_OK});
        }

        const auto worker_count = std::clamp<std::uint32_t>(
            options.worker_count,
            1,
            static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining_files, kMaxCopyWorkers)));
        ConcurrentResultState result_state;
        std::mutex callback_mutex;
        std::vector<JobResult> worker_results(worker_count, {true, false, S_OK, false});
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);

        auto emit_progress = [&](const PlannedFile& file, bool active, bool skippable) -> bool {
            if (!progress) {
                return true;
            }
            std::lock_guard callback_lock(callback_mutex);
            // Single source of truth: the plan's per-item resolution state.
            const auto view = plan.resolution_view();
            const auto& outcomes = view.outcomes;
            const auto resolved_files = outcomes.succeeded + outcomes.skipped +
                outcomes.failed + outcomes.copied_source_retained;
            JobProgress aggregate{};
            aggregate.total_bytes = view.counters.resolution_total;
            aggregate.transferred_bytes = std::min(view.counters.resolution_weight, aggregate.total_bytes);
            aggregate.bytes_written_physical = view.counters.bytes_written_physical;
            aggregate.total_files = resolved_files + view.unresolved_files();
            aggregate.completed_files = resolved_files;
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
                std::uint64_t held_file_id = 0;
                for (;;) {
                try {
                    for (;;) {
                        auto directive = control.directive();
                        if (directive == ExecutionDirective::Pause) {
                            directive = control.wait_while_paused();
                        }
                        if (directive == ExecutionDirective::Cancel) {
                            worker_results[worker_index] = {
                                false,
                                true,
                                static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)),
                                false,
                            };
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
                        held_file_id = file_id;

                        directive = control.directive();
                        if (directive == ExecutionDirective::Cancel || directive == ExecutionDirective::Stop) {
                            plan.release_active(file_id);
                            worker_results[worker_index] = directive == ExecutionDirective::Cancel
                                ? JobResult{
                                      false,
                                      true,
                                      static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)),
                                      false,
                                  }
                                : JobResult{false, false, S_OK, true};
                            return;
                        }

                        if (directive == ExecutionDirective::Pause) {
                            directive = control.wait_while_paused();
                            if (directive == ExecutionDirective::Cancel || directive == ExecutionDirective::Stop) {
                                plan.release_active(file_id);
                                worker_results[worker_index] = directive == ExecutionDirective::Cancel
                                    ? JobResult{
                                          false,
                                          true,
                                          static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)),
                                          false,
                                      }
                                    : JobResult{false, false, S_OK, true};
                                return;
                            }
                        }

                        const bool skip_allowed = destination_is_safe_to_discard(file->destination);
                        if (skip_allowed && control.consume_skip(file_id)) {

                            if (!plan.resolve_active(file_id, ItemOutcome::Skipped, S_OK, false)) {
                                result_state.record_error(kInvalidPlanState);
                                control.request_cancel();
                                worker_results[worker_index] = {
                                    false,
                                    false,
                                    kInvalidPlanState,
                                    false,
                                };
                                return;
                            }
                            if (!emit_progress(*file, false, false)) {
                                worker_results[worker_index] = {
                                    false,
                                    true,
                                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)),
                                    false,
                                };
                                return;
                            }
                            continue;
                        }
                        if (!skip_allowed) {
                            (void)control.consume_skip(file_id);
                        }

                        bool resume_from_pause = false;
                        bool skipped = false;
                        bool failed = false;

                        for (;;) {
                            bool skip_requested = false;
                            const auto existing_policy =
                                options.replace_file_id == file_id ||
                                options.conflict_policy == ConflictPolicy::ReplaceAll
                                    ? ExistingDestinationPolicy::Replace
                                    : options.existing_destination;

                            const auto result = engine_.copy_file(
                                file->source,
                                file->destination,
                                CopyOptions{
                                    resume_from_pause,
                                    existing_policy,
                                    options.copy_flags,
                                    options.suggested_buffer_bytes,
                                },
                                [&](const CopyProgress& file_progress) {
                                    plan.record_attempt_bytes(file_id, file_progress.transferred_bytes);
                                    if (skip_allowed && control.consume_skip(file_id)) {
                                        skip_requested = true;
                                        return CopyDecision::Skip;
                                    }
                                    const auto current = control.directive();
                                    if (current == ExecutionDirective::Pause) {
                                        return CopyDecision::Pause;
                                    }
                                    if (current == ExecutionDirective::Stop) {
                                        return CopyDecision::Stop;
                                    }
                                    if (current == ExecutionDirective::Cancel) {
                                        return CopyDecision::Cancel;
                                    }
                                    return emit_progress(*file, true, skip_allowed)
                                        ? CopyDecision::Continue
                                        : CopyDecision::Cancel;
                                });

                            if (result.success) {
                                break;
                            }

                            if (skip_requested) {

                                remove_partial_destination(file->destination);
                                if (!plan.resolve_active(file_id, ItemOutcome::Skipped, S_OK, false)) {
                                    result_state.record_error(kInvalidPlanState);
                                    control.request_cancel();
                                    worker_results[worker_index] = {
                                        false,
                                        false,
                                        kInvalidPlanState,
                                        false,
                                    };
                                    return;
                                }
                                skipped = true;
                                break;
                            }

                            if (result.native_code ==
                                static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_PAUSED))) {
                                const auto next = control.wait_while_paused();
                                if (next == ExecutionDirective::Cancel) {

                                    if (skip_allowed) {
                                        remove_partial_destination(file->destination);
                                    }
                                    plan.release_active(file_id);
                                    worker_results[worker_index] = {
                                        false,
                                        true,
                                        static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)),
                                        false,
                                    };
                                    return;
                                }
                                if (next == ExecutionDirective::Stop) {

                                    if (skip_allowed) {
                                        remove_partial_destination(file->destination);
                                    }
                                    plan.release_active(file_id);
                                    worker_results[worker_index] = {false, false, S_OK, true};
                                    return;
                                }
                                resume_from_pause = true;
                                continue;
                            }

                            const bool aborted = result.native_code ==
                                static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED));
                            if (aborted && control.directive() == ExecutionDirective::Stop) {

                                if (skip_allowed) {
                                    remove_partial_destination(file->destination);
                                }
                                plan.release_active(file_id);
                                worker_results[worker_index] = {false, false, S_OK, true};
                                return;
                            }


                            const bool cancelled = aborted;
                            if (!cancelled &&
                                options.conflict_policy == ConflictPolicy::SkipAll &&
                                is_destination_conflict(result.native_code)) {
                                if (!plan.resolve_active(file_id, ItemOutcome::Skipped, S_OK, true)) {
                                    result_state.record_error(kInvalidPlanState);
                                    control.request_cancel();
                                    worker_results[worker_index] = {
                                        false,
                                        false,
                                        kInvalidPlanState,
                                        false,
                                    };
                                    return;
                                }
                                skipped = true;
                                break;
                            }
                            if (!cancelled && !is_session_fatal(result.native_code) &&
                                !is_destination_conflict(result.native_code)) {
                                if (skip_allowed) remove_partial_destination(file->destination);
                                if (!plan.park_active(
                                        file_id, result.native_code, !skip_allowed,
                                        RecoveryAction::RetryTransfer)) {
                                    result_state.record_error(kInvalidPlanState);
                                    control.request_cancel();
                                    worker_results[worker_index] = {false, false, kInvalidPlanState, false};
                                    return;
                                }
                                failed = true;
                                break;
                            }
                            if (cancelled && skip_allowed) remove_partial_destination(file->destination);
                            plan.release_active(file_id);
                            if (!cancelled) result_state.record_error(result.native_code, &*file);
                            control.request_cancel();
                            worker_results[worker_index] = {
                                false,
                                cancelled,
                                result.native_code,
                                false,
                            };
                            return;
                        }

                        if (skipped || failed) {
                            if (!emit_progress(*file, false, false)) {
                                worker_results[worker_index] = {
                                    false,
                                    true,
                                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)),
                                    false,
                                };
                                return;
                            }
                            continue;
                        }

                        if (plan.operation() == FileOperation::Move) {
                            const auto remove_source = remove_moved_source_file(file->source);
                            if (remove_source != S_OK) {
                                const auto source_fingerprint = probe_file_fingerprint(file->source);
                                const auto destination_fingerprint = probe_file_fingerprint(file->destination);
                                if (source_fingerprint.status != FingerprintProbe::Present ||
                                    destination_fingerprint.status != FingerprintProbe::Present) {
                                    if (!plan.resolve_active(
                                            file_id, ItemOutcome::CopiedSourceRetained,
                                            remove_source, !skip_allowed)) {
                                        result_state.record_error(kInvalidPlanState);
                                        control.request_cancel();
                                        worker_results[worker_index] = {
                                            false, false, kInvalidPlanState, false,
                                        };
                                        return;
                                    }
                                    continue;
                                }
                                if (!plan.park_active_source_removal(
                                        file_id, remove_source, !skip_allowed,
                                        source_fingerprint.fingerprint,
                                        destination_fingerprint.fingerprint)) {
                                    result_state.record_error(kInvalidPlanState);
                                    control.request_cancel();
                                    worker_results[worker_index] = {
                                        false, false, kInvalidPlanState, false,
                                    };
                                    return;
                                }
                                if (!emit_progress(*file, false, false)) {
                                    worker_results[worker_index] = {
                                        false, true,
                                        static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false,
                                    };
                                    return;
                                }
                                continue;
                            }
                        }

                        if (!plan.resolve_active(file_id, ItemOutcome::Succeeded, S_OK, !skip_allowed)) {
                            result_state.record_error(kInvalidPlanState);
                            control.request_cancel();
                            worker_results[worker_index] = {false, false, kInvalidPlanState, false};
                            return;
                        }
                        if (!emit_progress(*file, false, false)) {
                            worker_results[worker_index] = {
                                false,
                                true,
                                static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)),
                                false,
                            };
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
                    const auto native = native_hresult(error.code());
                    if (!is_session_fatal(native) && held_file_id != 0 &&
                        plan.park_active(held_file_id, native, false, RecoveryAction::RetryTransfer)) {
                        held_file_id = 0;
                        continue;
                    }
                    result_state.record_error(native);
                    control.request_cancel();
                    worker_results[worker_index] = {false, false, native, false};
                    return;
                } catch (const std::bad_alloc&) {
                    const auto native = static_cast<std::int32_t>(E_OUTOFMEMORY);
                    result_state.record_error(native);
                    control.request_cancel();
                    worker_results[worker_index] = {false, false, native, false};
                    return;
                } catch (const std::system_error& error) {
                    const auto native = native_hresult(error.code());
                    if (!is_session_fatal(native) && held_file_id != 0 &&
                        plan.park_active(held_file_id, native, false, RecoveryAction::RetryTransfer)) {
                        held_file_id = 0;
                        continue;
                    }
                    result_state.record_error(native);
                    control.request_cancel();
                    worker_results[worker_index] = {false, false, native, false};
                    return;
                } catch (...) {
                    result_state.record_error(kUnhandledExecutorError);
                    control.request_cancel();
                    worker_results[worker_index] = {
                        false,
                        false,
                        kUnhandledExecutorError,
                        false,
                    };
                    return;
                }
                }
            });
        }

        workers.clear();

        if (result_state.error() != S_OK) {
            return finish(result_state.failure_result());
        }
        for (const auto& worker_result : worker_results) {
            if (worker_result.cancelled) {
                return finish(worker_result);
            }
        }
        for (const auto& worker_result : worker_results) {
            if (worker_result.stopped) {
                return finish(worker_result);
            }
        }
        if (const auto result = check_control()) return *result;
        if (plan.operation() == FileOperation::Move && plan.unresolved_files() == 0) {
            const auto cleanup = remove_empty_source_directories(plan.source_roots(), control);
            if (const auto result = check_control()) return *result;
            if (cleanup != S_OK) {
                return finish({false, false, cleanup, false});
            }
        }
        return finish({true, false, S_OK, false});
    } catch (const std::filesystem::filesystem_error& error) {
        return {
            false,
            false,
            native_hresult(error.code()),
        };
    } catch (const std::bad_alloc&) {
        return {false, false, static_cast<std::int32_t>(E_OUTOFMEMORY)};
    } catch (const std::system_error& error) {
        return {false, false, native_hresult(error.code())};
    } catch (...) {
        return {false, false, kUnhandledExecutorError};
    }
}

} // namespace velocitycopy
