#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int main() {
    const auto ui = std::filesystem::path{VELOCITYCOPY_SOURCE_DIR} / "src/ui/VelocityCopy.UI";
    std::error_code ec;
    for (std::filesystem::directory_iterator it(ui, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const auto name = it->path().filename().string();
        if (!name.starts_with("MainWindow.") || it->path().extension() != ".cpp") continue;
        const auto text = read(it->path());
        if (text.find("SetProcessInformation") != std::string::npos ||
            text.find("ProcessPowerThrottling") != std::string::npos) return 1;
    }
    const auto app = read(ui / "App.xaml.cpp");
    if (app.find("SetProcessInformation") == std::string::npos ||
        app.find("ProcessPowerThrottling") == std::string::npos) return 2;
    const auto header = read(ui / "MainWindow.xaml.h");
    if (header.find("efficiency_mode_enabled_") != std::string::npos) return 3;
    return ec ? 4 : 0;
}
