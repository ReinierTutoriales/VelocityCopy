#include "velocitycopy/live_copy_plan.hpp"
#include "velocitycopy/queue_archive.hpp"

#include <filesystem>
#include <fstream>

namespace {

velocitycopy::CopyPlan make_plan(const std::filesystem::path& root) {
    velocitycopy::CopyPlan plan{};
    plan.destination_root = root / L"destino";
    plan.source_roots = {root / L"origen-á"};
    plan.directories = {{plan.destination_root}, {plan.destination_root / L"sub"}};
    plan.files = {
        {41, root / L"origen-á" / L"uno.txt", plan.destination_root / L"uno.txt", 10},
        {42, root / L"origen-á" / L"dos.txt", plan.destination_root / L"dos.txt", 20},
        {43, root / L"origen-á" / L"tres.txt", plan.destination_root / L"sub" / L"tres.txt", 30},
    };
    plan.total_bytes = 60;
    plan.largest_file_bytes = 30;
    return plan;
}

} // namespace

int wmain() {
    namespace fs = std::filesystem;
    using namespace velocitycopy;

    const auto root = fs::temp_directory_path() / L"VelocityCopyQueueArchiveTest";
    const auto archive_path = root / L"cola.vcq";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec) return 1;

    LiveCopyPlan live(make_plan(root));
    const auto active = live.acquire_next();
    if (!active || active->source.filename() != L"uno.txt") return 2;

    QueueArchive archive{};
    archive.current_plan = live.export_remaining_plan();

    CopyJob append{};
    append.id = 555;
    append.sources = {root / L"append" / L"uno-extra.txt"};
    append.destination = root / L"destino";
    append.layout = DestinationLayout::ContentsOnly;
    append.state = JobState::Running;
    append.display_name = L"Añadido a sesión";
    archive.current_append_jobs.push_back(append);

    CopyJob future{};
    future.id = 999;
    future.sources = {root / L"futuro" / L"A", root / L"futuro" / L"B"};
    future.destination = root / L"otro-destino";
    future.layout = DestinationLayout::ContentsOnly;
    future.state = JobState::Running;
    future.display_name = L"Sesión futura ñ";
    archive.queued_jobs.push_back(future);

    if (!archive.current_plan || archive.current_plan->files.size() != 3 ||
        archive.current_plan->files[0].id != 1 ||
        archive.current_plan->files[1].id != 2 ||
        archive.current_plan->files[2].id != 3 ||
        archive.current_plan->files[0].source.filename() != L"uno.txt" ||
        archive.current_plan->files[1].source.filename() != L"dos.txt" ||
        archive.current_plan->total_bytes != 60 ||
        archive.current_plan->largest_file_bytes != 30) {
        return 3;
    }

    QueueArchiveStore store;
    if (!store.save(archive_path, archive)) return 4;
    auto loaded = store.load(archive_path);
    if (!loaded || !loaded->current_plan ||
        loaded->current_append_jobs.size() != 1 || loaded->queued_jobs.size() != 1) {
        return 5;
    }

    const auto& restored = *loaded->current_plan;
    if (restored.destination_root != archive.current_plan->destination_root ||
        restored.source_roots != archive.current_plan->source_roots ||
        restored.directories.size() != 2 || restored.files.size() != 3 ||
        restored.total_bytes != 60 || restored.largest_file_bytes != 30 ||
        restored.files[0].id != 1 || restored.files[2].id != 3 ||
        restored.files[2].destination.filename() != L"tres.txt") {
        return 6;
    }

    const auto& restored_append = loaded->current_append_jobs.front();
    if (restored_append.id != 0 || restored_append.state != JobState::Pending ||
        restored_append.sources != append.sources || restored_append.destination != append.destination ||
        restored_append.layout != append.layout || restored_append.display_name != append.display_name) {
        return 7;
    }

    const auto& restored_job = loaded->queued_jobs.front();
    if (restored_job.id != 0 || restored_job.state != JobState::Pending ||
        restored_job.sources != future.sources || restored_job.destination != future.destination ||
        restored_job.layout != future.layout || restored_job.display_name != future.display_name) {
        return 8;
    }

    // Saving again must atomically replace the previous archive.
    archive.current_append_jobs.clear();
    archive.queued_jobs.clear();
    archive.current_plan.reset();
    if (!store.save(archive_path, archive)) return 9;
    loaded = store.load(archive_path);
    if (!loaded || loaded->current_plan || !loaded->current_append_jobs.empty() ||
        !loaded->queued_jobs.empty()) {
        return 10;
    }

    // Corrupt or unknown formats must be rejected without partial recovery.
    {
        std::ofstream corrupt(archive_path, std::ios::binary | std::ios::trunc);
        corrupt << "not-a-velocitycopy-queue";
    }
    if (store.load(archive_path)) return 11;

    fs::remove_all(root, ec);
    return 0;
}
