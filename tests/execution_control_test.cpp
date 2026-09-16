#include "velocitycopy/execution_control.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

int main() {
    using velocitycopy::ExecutionDirective;

    velocitycopy::ExecutionControl control;
    if (control.directive() != ExecutionDirective::Run) {
        return 1;
    }

    control.request_pause();
    if (control.directive() != ExecutionDirective::Pause) {
        return 2;
    }

    std::atomic_bool returned{false};
    std::atomic<ExecutionDirective> result{ExecutionDirective::Pause};
    std::jthread waiter([&] {
        result.store(control.wait_while_paused(), std::memory_order_relaxed);
        returned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(25ms);
    if (returned.load(std::memory_order_acquire)) {
        control.resume();
        waiter.join();
        return 3;
    }

    control.resume();
    waiter.join();
    if (!returned.load(std::memory_order_acquire) ||
        result.load(std::memory_order_relaxed) != ExecutionDirective::Run) {
        return 4;
    }

    control.request_pause();
    std::jthread stopper([&] {
        result.store(control.wait_while_paused(), std::memory_order_relaxed);
    });
    std::this_thread::sleep_for(25ms);
    control.request_stop();
    stopper.join();
    if (result.load(std::memory_order_relaxed) != ExecutionDirective::Stop) {
        return 5;
    }

    control.reset();
    control.request_cancel();
    if (control.directive() != ExecutionDirective::Cancel) {
        return 6;
    }
    control.request_stop();
    if (control.directive() != ExecutionDirective::Cancel) {
        return 7;
    }

    // Multi-worker contract: all paused workers remain blocked until resume,
    // then every waiter observes Run after the notify_all wake-up.
    control.reset();
    control.request_pause();
    std::array<std::atomic<ExecutionDirective>, 4> multi_results{};
    std::array<std::jthread, 4> waiters;
    for (std::size_t index = 0; index < waiters.size(); ++index) {
        multi_results[index].store(ExecutionDirective::Pause, std::memory_order_relaxed);
        waiters[index] = std::jthread([&, index] {
            multi_results[index].store(control.wait_while_paused(), std::memory_order_release);
        });
    }

    std::this_thread::sleep_for(25ms);
    for (const auto& value : multi_results) {
        if (value.load(std::memory_order_acquire) != ExecutionDirective::Pause) {
            control.resume();
            for (auto& thread : waiters) {
                thread.join();
            }
            return 8;
        }
    }

    control.resume();
    for (auto& thread : waiters) {
        thread.join();
    }
    for (const auto& value : multi_results) {
        if (value.load(std::memory_order_acquire) != ExecutionDirective::Run) {
            return 9;
        }
    }

    control.reset();
    if (control.directive() != ExecutionDirective::Run) {
        return 10;
    }
    return 0;
}
