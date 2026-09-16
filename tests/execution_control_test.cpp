#include "velocitycopy/execution_control.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

int main() {
    velocitycopy::ExecutionControl control;
    assert(control.directive() == velocitycopy::ExecutionDirective::Run);

    control.request_pause();
    assert(control.directive() == velocitycopy::ExecutionDirective::Pause);

    std::atomic_bool returned{false};
    std::atomic<velocitycopy::ExecutionDirective> result{velocitycopy::ExecutionDirective::Pause};
    std::jthread waiter([&] {
        result.store(control.wait_while_paused(), std::memory_order_relaxed);
        returned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(25ms);
    assert(!returned.load(std::memory_order_acquire));

    control.resume();
    waiter.join();
    assert(returned.load(std::memory_order_acquire));
    assert(result.load(std::memory_order_relaxed) == velocitycopy::ExecutionDirective::Run);

    control.request_pause();
    std::jthread stopper([&] {
        result.store(control.wait_while_paused(), std::memory_order_relaxed);
    });
    std::this_thread::sleep_for(25ms);
    control.request_stop();
    stopper.join();
    assert(result.load(std::memory_order_relaxed) == velocitycopy::ExecutionDirective::Stop);

    control.reset();
    control.request_cancel();
    assert(control.directive() == velocitycopy::ExecutionDirective::Cancel);
    control.request_stop();
    assert(control.directive() == velocitycopy::ExecutionDirective::Cancel);

    // Contract for the future worker pool: all paused workers must wake together.
    control.reset();
    control.request_pause();
    std::array<std::atomic<velocitycopy::ExecutionDirective>, 4> multi_results{};
    std::array<std::jthread, 4> waiters;
    for (std::size_t index = 0; index < waiters.size(); ++index) {
        multi_results[index].store(velocitycopy::ExecutionDirective::Pause, std::memory_order_relaxed);
        waiters[index] = std::jthread([&, index] {
            multi_results[index].store(control.wait_while_paused(), std::memory_order_release);
        });
    }

    std::this_thread::sleep_for(25ms);
    for (const auto& value : multi_results) {
        assert(value.load(std::memory_order_acquire) == velocitycopy::ExecutionDirective::Pause);
    }

    control.resume();
    for (auto& thread : waiters) {
        thread.join();
    }
    for (const auto& value : multi_results) {
        assert(value.load(std::memory_order_acquire) == velocitycopy::ExecutionDirective::Run);
    }

    control.reset();
    assert(control.directive() == velocitycopy::ExecutionDirective::Run);
    return 0;
}
