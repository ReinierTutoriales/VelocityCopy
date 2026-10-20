#include "velocitycopy/streaming_copy.hpp"
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <thread>

namespace velocitycopy {
namespace {
std::size_t entries(const CopyPlan& plan) {
    return plan.files.size() + plan.directories.size() + plan.failures.size();
}
}

StreamingCopyResult execute_streaming_copy(const CopyJob& job, ExecutionControl& control,
    const JobExecutionOptions& execution, const StreamingCopyOptions& streaming,
    const JobProgressCallback& progress) noexcept {
    StreamingCopyResult result{};
    try {
        if (job.operation != FileOperation::Copy || streaming.batch_entries == 0 ||
            streaming.batch_entries > 250'000 || streaming.buffered_entries < streaming.batch_entries) {
            result.result.native_code = HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
            return result;
        }
        // Allocate the original intent before any transfer starts. Even if a
        // later snapshot cannot allocate, all source data remains rediscoverable.
        result.recovery.queued_jobs.push_back(job);
        CopyPlan seed{}; seed.destination_root = job.destination; seed.source_roots = job.sources;
        LiveCopyPlan live(std::move(seed));
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<CopyPlan> batches;
        std::size_t buffered = 0, peak = 0;
        bool done = false;
        std::int32_t producer_error = S_OK;
        std::jthread producer([&](std::stop_token stop) {
            std::int32_t error = S_OK;
            try {
                JobPlanner{}.stream(job, control, streaming.batch_entries, [&](CopyPlan batch) {
                    const auto count = entries(batch);
                    std::unique_lock lock(mutex);
                    for (;;) {
                        if (stop.stop_requested() || control.directive() == ExecutionDirective::Cancel ||
                            control.directive() == ExecutionDirective::Stop) return false;
                        if (control.directive() == ExecutionDirective::Pause) {
                            lock.unlock();
                            (void)control.wait_while_paused(stop);
                            lock.lock();
                            continue;
                        }
                        if (count <= streaming.buffered_entries - buffered) break;
                        changed.wait_for(lock, std::chrono::milliseconds(25));
                    }
                    batches.push_back(std::move(batch));
                    buffered += count;
                    peak = std::max(peak, buffered);
                    changed.notify_all();
                    return true;
                }, stop);
            } catch (const std::system_error& failure) { error = planning_error_hresult(failure.code()); }
            catch (const std::bad_alloc&) { error = E_OUTOFMEMORY; }
            catch (...) { error = E_FAIL; }
            std::lock_guard lock(mutex);
            producer_error = error;
            done = true;
            changed.notify_all();
        });

        result.result.success = true;
        bool all_admitted = false;
        for (;;) {
            auto directive = control.directive();
            if (directive == ExecutionDirective::Pause) directive = control.wait_while_paused(producer.get_stop_token());
            if (directive != ExecutionDirective::Run) {
                result.result.success = false;
                result.result.cancelled = directive == ExecutionDirective::Cancel;
                result.result.stopped = directive == ExecutionDirective::Stop;
                result.result.native_code = directive == ExecutionDirective::Cancel ? HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED) : S_OK;
                break;
            }
            CopyPlan batch;
            {
                std::unique_lock lock(mutex);
                changed.wait_for(lock, std::chrono::milliseconds(25), [&] {
                    return done || !batches.empty() || control.directive() != ExecutionDirective::Run;
                });
                if (producer_error != S_OK) {
                    result.result.success = false; result.result.native_code = producer_error; break;
                }
                if (batches.empty()) {
                    if (done) { all_admitted = true; break; }
                    continue;
                }
                batch = std::move(batches.front()); batches.pop_front();
            }
            const auto count = entries(batch);
            batch.source_roots.clear(); // The seed owns the original roots once.
            if (live.append(std::move(batch), true) != LivePlanAppendResult::Appended) {
                result.result.success = false; result.result.native_code = HRESULT_FROM_WIN32(ERROR_INVALID_DATA); break;
            }
            result.admitted_files = live.total_files();
            if (streaming.checkpoint) {
                result.recovery.current_plan = live.export_remaining_plan();
                if (!streaming.checkpoint(result.recovery)) {
                    result.result.success = false; result.result.native_code = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT); break;
                }
            }
            // Admission occurs between execution batches: empty directories and
            // their metadata are materialized before that batch's files run.
            result.result = JobExecutor{}.execute(live, control, execution, [&](const JobProgress& value) {
                changed.notify_all();
                if (!progress) return JobDecision::Continue;
                auto dynamic = value;
                { std::lock_guard lock(mutex); dynamic.totals_final = done && producer_error == S_OK && batches.empty(); }
                return progress(dynamic);
            });
            {
                std::lock_guard lock(mutex);
                buffered -= count;
                changed.notify_all();
            }
            if (!result.result.success || result.result.parked_files) {
                result.result.success = false;
                if (result.result.native_code == S_OK && result.result.parked_files)
                    result.result.native_code = HRESULT_FROM_WIN32(ERROR_MORE_DATA);
                break;
            }
            live.discard_materialized_directory_history();
        }
        if (!all_admitted) producer.request_stop();
        changed.notify_all();
        producer.join();
        result.scan_complete = done && producer_error == S_OK;
        result.peak_buffered_entries = peak;
        const auto resolution = live.resolution_view();
        result.result.outcomes = resolution.outcomes;
        result.result.parked_files = resolution.parked_files;
        if (all_admitted && result.scan_complete && result.result.success) {
            if (streaming.checkpoint && !streaming.checkpoint(QueueArchive{})) {
                result.result.success = false; result.result.native_code = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
            } else result.recovery = {};
        } else {
            result.result.success = false;
            result.recovery.current_plan = live.export_remaining_plan();
        }
        if (progress && result.result.success) {
            JobProgress final{}; final.total_bytes = resolution.counters.resolution_total;
            final.transferred_bytes = resolution.counters.resolution_weight;
            final.bytes_written_physical = resolution.counters.bytes_written_physical;
            final.completed_files = resolution.outcomes.succeeded + resolution.outcomes.skipped +
                resolution.outcomes.failed + resolution.outcomes.copied_source_retained;
            final.total_files = final.completed_files;
            if (progress(final) == JobDecision::Cancel) {
                control.request_cancel(); result.result.success = false; result.result.cancelled = true;
                result.result.native_code = HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED);
            }
        }
        return result;
    } catch (const std::system_error& error) { result.result.native_code = planning_error_hresult(error.code()); }
    catch (const std::bad_alloc&) { result.result.native_code = E_OUTOFMEMORY; }
    catch (...) { result.result.native_code = E_FAIL; }
    result.result.success = false;
    return result;
}
} // namespace velocitycopy
