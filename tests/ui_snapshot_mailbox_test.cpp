#include "UiSnapshotMailbox.h"
#include <atomic>
#include <thread>

namespace {
velocitycopy::UiSnapshot snapshot(std::uint64_t sequence) {
    velocitycopy::UiSnapshot value{};
    value.sequence = sequence;
    value.transferred_bytes = sequence * 10;
    value.current_file_id = sequence;
    value.current_source = L"source";
    return value;
}
}
int main() {
    velocitycopy::ui::UiSnapshotMailbox mailbox;
    if (!mailbox.publish(snapshot(1))) return 1;
    for (std::uint64_t i = 2; i <= 10000; ++i) {
        if (mailbox.publish(snapshot(i))) return 2; // No second queued callback.
    }
    auto value = mailbox.consume();
    if (!value || value->sequence != 10000 || mailbox.consume()) return 3;
    if (mailbox.publish(snapshot(9999))) return 4; // Late older progress is ignored.
    if (!mailbox.publish(snapshot(10001))) return 5;
    mailbox.discard(); // Dispatcher shutdown releases its pending snapshot.
    if (mailbox.consume() || !mailbox.publish(snapshot(10002))) return 6;
    mailbox.consume();

    constexpr std::uint64_t last = 60000;
    std::atomic<bool> finished{false};
    std::jthread writer([&] {
        for (std::uint64_t i = 10003; i <= last; ++i) mailbox.publish(snapshot(i));
        finished = true;
    });
    std::uint64_t observed = 10002;
    while (!finished.load() || observed < last) {
        if (const auto next = mailbox.consume()) {
            if (next->sequence <= observed || next->transferred_bytes != next->sequence * 10 ||
                next->current_file_id != next->sequence || next->current_source != L"source") return 7;
            observed = next->sequence;
        } else std::this_thread::yield();
    }
    return observed == last ? 0 : 8;
}
