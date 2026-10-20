#include "velocitycopy/ui_snapshot.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// CI builds Release (NDEBUG), which compiles assert() out. Checks must stay live.
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\\n", __FILE__, __LINE__, #condition); \
            std::abort();                                                       \
        }                                                                       \
    } while (false)

int main() {
    CHECK(velocitycopy::can_skip_current_file(true, 42, true, false, false, false, false));
    CHECK(!velocitycopy::can_skip_current_file(false, 42, true, false, false, false, false));
    CHECK(!velocitycopy::can_skip_current_file(true, 0, true, false, false, false, false));
    CHECK(!velocitycopy::can_skip_current_file(true, 42, false, false, false, false, false));
    CHECK(!velocitycopy::can_skip_current_file(true, 42, true, true, false, false, false));
    CHECK(!velocitycopy::can_skip_current_file(true, 42, true, false, true, false, false));
    CHECK(!velocitycopy::can_skip_current_file(true, 42, true, false, false, true, false));
    CHECK(!velocitycopy::can_skip_current_file(true, 42, true, false, false, false, true));

    velocitycopy::ProgressPresenter presenter{100};

    velocitycopy::JobProgress progress{};
    progress.total_bytes = 1'000;
    progress.total_files = 2;
    progress.current_file_id = 42;
    progress.current_source = L"C:\\Source\\file.bin";

    const auto first = presenter.observe(progress, 0);
    CHECK(first.has_value());
    CHECK(first->sequence == 1);
    CHECK(first->fraction == 0.0);
    CHECK(first->current_file_id == 42);

    progress.transferred_bytes = 100;
    progress.bytes_written_physical = 100;
    const auto too_soon = presenter.observe(progress, 50);
    CHECK(!too_soon.has_value());

    progress.transferred_bytes = 250;
    progress.bytes_written_physical = 250;
    const auto second = presenter.observe(progress, 100);
    CHECK(second.has_value());
    CHECK(second->sequence == 2);
    CHECK(second->current_file_id == 42);
    CHECK(std::abs(second->fraction - 0.25) < 0.0001);
    CHECK(second->bytes_per_second > 0.0);
    CHECK(second->eta_seconds == 0.0); // Warm-up hides an unstable early estimate.

    progress.transferred_bytes = 1'000;
    progress.bytes_written_physical = 1'000;
    progress.completed_files = 2;
    progress.current_file_id = 0;
    const auto finished = presenter.observe(progress, 120);
    CHECK(finished.has_value());
    CHECK(finished->fraction == 1.0);
    CHECK(finished->completed_files == 2);
    CHECK(finished->current_file_id == 0);

    // A skip resolves logical progress without physical I/O: the bar jumps,
    // speed must not.
    {
        velocitycopy::ProgressPresenter skip_presenter{100};
        velocitycopy::JobProgress skip{};
        skip.total_bytes = 10'000;
        skip.total_files = 2;
        (void)skip_presenter.observe(skip, 0);
        skip.transferred_bytes = 100;
        skip.bytes_written_physical = 100;
        const auto copying = skip_presenter.observe(skip, 1'000);
        CHECK(copying.has_value());
        const auto rate_before = copying->bytes_per_second;
        CHECK(rate_before > 0.0);
        skip.transferred_bytes = 9'000;
        const auto after_skip = skip_presenter.observe(skip, 2'000);
        CHECK(after_skip.has_value());
        CHECK(std::abs(after_skip->fraction - 0.9) < 0.0001);
        CHECK(after_skip->bytes_per_second <= rate_before);
    }

    presenter.reset();
    const auto reset_snapshot = presenter.observe(progress, 1'000);
    CHECK(reset_snapshot.has_value());
    CHECK(reset_snapshot->sequence == 1);
    CHECK(reset_snapshot->current_file_id == 0);

    // Time-based smoothing must be independent of callback frequency.
    double rates[2]{};
    for (int variant = 0; variant < 2; ++variant) {
        velocitycopy::ProgressPresenter timed{16};
        velocitycopy::JobProgress sample{};
        sample.total_bytes = 100'000;
        sample.total_files = 10;
        CHECK(timed.observe(sample, 10'000)->eta_seconds == 0.0);
        const std::uint64_t step = variant == 0 ? 100 : 1'000;
        for (std::uint64_t elapsed = step; elapsed <= 3'000; elapsed += step) {
            sample.bytes_written_physical = elapsed;
            sample.transferred_bytes = elapsed;
            const auto value = timed.observe(sample, 10'000 + elapsed);
            CHECK(value.has_value());
            if (elapsed < 3'000) CHECK(value->eta_seconds == 0.0);
            else {
                CHECK(value->eta_seconds > 0.0);
                rates[variant] = value->bytes_per_second;
            }
        }
        // A stalled transfer must lose its old throughput rather than freezing it.
        const auto idle = timed.observe(sample, 14'000);
        CHECK(idle->bytes_per_second < rates[variant]);
        CHECK(std::abs(idle->bytes_per_second - rates[variant] * std::exp(-1.0)) < 0.0001);
        timed.reset();
        CHECK(timed.observe(sample, 15'000)->eta_seconds == 0.0);
    }
    CHECK(std::abs(rates[0] - rates[1]) < 0.0001);

    // A rename-only Move resolves bytes but cannot invent physical speed or ETA.
    {
        velocitycopy::ProgressPresenter rename_presenter{16};
        velocitycopy::JobProgress rename{};
        rename.total_bytes = 100'000;
        rename.total_files = 10;
        (void)rename_presenter.observe(rename, 0);
        rename.transferred_bytes = 50'000;
        rename.completed_files = 5;
        const auto value = rename_presenter.observe(rename, 4'000);
        CHECK(value->bytes_per_second == 0.0);
        CHECK(value->eta_seconds == 0.0);
        CHECK(value->fraction == 0.5);
    }

    return 0;
}
