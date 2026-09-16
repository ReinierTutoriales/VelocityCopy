#include <filesystem>
#include <fstream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {

std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}

} // namespace

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};

    const auto shell = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Shell.cpp");
    const auto xaml = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml");
    const auto append = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.CopyAppend.cpp");

    if (shell.empty() || xaml.empty() || append.empty()) {
        return 1;
    }

    // Explorer and drag/drop must share the same queue-aware production route.
    if (!contains(shell, "QueueOrStartCopy(*dispatch.job)") ||
        !contains(xaml, "Click=\"OnQueueOrStartCopyClick\"") ||
        contains(xaml, "Click=\"OnStartCopyClick\"")) {
        return 2;
    }

    // Planning must stay off the UI thread and compatible batches must merge
    // into the live plan rather than invoke a second independent copy.
    if (!contains(append, "append_planner_.enqueue") ||
        !contains(append, "target_plan->append") ||
        !contains(append, "LivePlanAppendResult::Appended")) {
        return 3;
    }

    return 0;
}
