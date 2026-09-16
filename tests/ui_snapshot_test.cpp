#include "velocitycopy/ui_snapshot.hpp"

#include <cassert>
#include <cmath>

int main() {
    velocitycopy::ProgressPresenter presenter{100};

    velocitycopy::JobProgress progress{};
    progress.total_bytes = 1'000;
    progress.total_files = 2;
    progress.current_source = L"C:\\Source\\file.bin";

    const auto first = presenter.observe(progress, 0);
    assert(first.has_value());
    assert(first->sequence == 1);
    assert(first->fraction == 0.0);

    progress.transferred_bytes = 100;
    const auto too_soon = presenter.observe(progress, 50);
    assert(!too_soon.has_value());

    progress.transferred_bytes = 250;
    const auto second = presenter.observe(progress, 100);
    assert(second.has_value());
    assert(second->sequence == 2);
    assert(std::abs(second->fraction - 0.25) < 0.0001);
    assert(second->bytes_per_second > 0.0);
    assert(second->eta_seconds > 0.0);

    progress.transferred_bytes = 1'000;
    progress.completed_files = 2;
    const auto finished = presenter.observe(progress, 120);
    assert(finished.has_value());
    assert(finished->fraction == 1.0);
    assert(finished->completed_files == 2);

    presenter.reset();
    const auto reset_snapshot = presenter.observe(progress, 1'000);
    assert(reset_snapshot.has_value());
    assert(reset_snapshot->sequence == 1);

    return 0;
}
