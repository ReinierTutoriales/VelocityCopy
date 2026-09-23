#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    std::erase(text, '\r');
    return text;
}

bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}

std::optional<int> version_macro(const std::string& header, const char* name) {
    std::smatch match;
    const std::regex pattern(std::string{"#define\\s+"} + name + "\\s+(\\d+)");
    if (!std::regex_search(header, match, pattern)) return std::nullopt;
    return std::stoi(match[1].str());
}

int fail(const int code, const char* message) {
    std::cerr << "version identity architecture contract " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto cmake = read_all(root / "CMakeLists.txt");
    const auto version_h = read_all(root / "src/ui/VelocityCopy.UI/Version.h");
    const auto resource = read_all(root / "src/ui/VelocityCopy.UI/AppIcon.rc");
    const auto project = read_all(root / "src/ui/VelocityCopy.UI/VelocityCopy.UI.vcxproj");
    const auto about = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.About.cpp");
    const auto persistence = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.QueuePersistence.cpp");
    const auto en = read_all(root / "src/ui/Strings/en-US/Resources.resw");
    const auto es = read_all(root / "src/ui/Strings/es-ES/Resources.resw");
    const auto package = read_all(root / ".github/workflows/package.yml");
    const auto spec = read_all(root / "docs/UI_SPEC.md");

    if (cmake.empty() || version_h.empty() || resource.empty() || project.empty() || about.empty() ||
        persistence.empty() || en.empty() || es.empty() || package.empty() || spec.empty()) {
        return fail(1, "required version/about source missing");
    }

    std::smatch cmake_version;
    if (!std::regex_search(cmake, cmake_version,
            std::regex{R"(project\(VelocityCopy VERSION (\d+)\.(\d+)\.(\d+) LANGUAGES CXX\))"})) {
        return fail(2, "CMake product version is not parseable");
    }

    const auto major = version_macro(version_h, "VELOCITYCOPY_VERSION_MAJOR");
    const auto minor = version_macro(version_h, "VELOCITYCOPY_VERSION_MINOR");
    const auto patch = version_macro(version_h, "VELOCITYCOPY_VERSION_PATCH");
    const auto build = version_macro(version_h, "VELOCITYCOPY_VERSION_BUILD");
    if (!major || !minor || !patch || !build ||
        *major != std::stoi(cmake_version[1].str()) ||
        *minor != std::stoi(cmake_version[2].str()) ||
        *patch != std::stoi(cmake_version[3].str())) {
        return fail(3, "Version.h MAJOR/MINOR/PATCH must match project(VelocityCopy VERSION ...)");
    }

    if (!contains(resource, "VS_VERSION_INFO VERSIONINFO") ||
        !contains(resource, "FILEVERSION VELOCITYCOPY_VERSION_COMMA") ||
        !contains(resource, "PRODUCTVERSION VELOCITYCOPY_VERSION_COMMA") ||
        !contains(resource, "CompanyName") || !contains(resource, "FileDescription") ||
        !contains(resource, "FileVersion") || !contains(resource, "ProductVersion")) {
        return fail(4, "WinUI executable must embed a complete VERSIONINFO resource");
    }

    if (!contains(project, "ClInclude Include=\"Version.h\"") ||
        !contains(project, "ClCompile Include=\"MainWindow.About.cpp\"") ||
        !contains(project, "version.lib")) {
        return fail(5, "WinUI project must compile About/version identity support and link Version APIs");
    }

    if (!contains(about, "GetFileVersionInfoSizeW") || !contains(about, "GetFileVersionInfoW") ||
        !contains(about, "VerQueryValueW") || !contains(about, "compiled_version()") ||
        !contains(about, "VELOCITYCOPY_VERSION_MAJOR") || contains(about, "return L\"Unknown\"") ||
        !contains(about, "Window about") || !contains(about, "about.ExtendsContentIntoTitleBar(true)") ||
        !contains(about, "about.SetTitleBar(title_bar)") || !contains(about, "AccentFillColorDefaultBrush") ||
        !contains(about, "Assets/VelocityCopy.png") || !contains(about, "HyperlinkButton") ||
        !contains(about, "NavigateUri") || contains(about, "Flyout about") ||
        contains(about, "TaskDialogIndirect") || contains(about, "ContentDialog")) {
        return fail(6, "About must be a themed movable WinUI window, show product identity, and never degrade to an Unknown version");
    }

    if (!contains(persistence, "ActionAbout") || !contains(persistence, "OnAboutClick") ||
        !contains(persistence, "about_menu_item_")) {
        return fail(7, "options flyout must expose the localized About command");
    }

    for (const auto* resources : {&en, &es}) {
        if (!contains(*resources, "name=\"ActionAbout\"") ||
            !contains(*resources, "name=\"AboutTitle\"") ||
            !contains(*resources, "name=\"AboutTagline\"") ||
            !contains(*resources, "name=\"AboutVersionFormat\"") ||
            !contains(*resources, "name=\"AboutPublisher\"") ||
            !contains(*resources, "name=\"AboutLicense\"") ||
            !contains(*resources, "name=\"AboutRepositoryLabel\"") ||
            contains(*resources, "name=\"AboutBodyFormat\"")) {
            return fail(8, "all supported UI languages must carry the structured About resources without the legacy body blob");
        }
    }

    if (!contains(package, "Get-Content src/ui/VelocityCopy.UI/Version.h -Raw") ||
        !contains(package, "VELOCITYCOPY_VERSION_MAJOR") ||
        !contains(package, "VELOCITYCOPY_VERSION_BUILD") ||
        contains(package, "$version = \"0.21.$env:GITHUB_RUN_NUMBER.0\"")) {
        return fail(9, "installer DISPLAY_VERSION must derive from the same executable version header");
    }

    if (!contains(spec, "About is a themed, movable WinUI window") ||
        !contains(spec, "must never display `Unknown`")) {
        return fail(10, "UI specification must lock the About visual and version fallback contract");
    }

    return 0;
}
