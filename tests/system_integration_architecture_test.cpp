#include <filesystem>
#include <fstream>
#include <iostream>
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
int fail(int code, const char* message) {
    std::cerr << "system integration architecture contract " << code << ": " << message << '\n';
    return code;
}
}

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto manifest = read_all(root / "src/ui/VelocityCopy.UI/Package.appxmanifest");
    const auto app = read_all(root / "src/ui/VelocityCopy.UI/App.xaml.cpp");
    const auto shell = read_all(root / "src/shell/explorer_commands.cpp");
    const auto workflow = read_all(root / ".github/workflows/build.yml");
    const auto installer = read_all(root / "tools/Install-VelocityCopy-Test.ps1");
    const auto docs = read_all(root / "docs/SYSTEM_INTEGRATION.md");

    if (manifest.empty() || app.empty() || shell.empty() || workflow.empty() || installer.empty() || docs.empty()) {
        return fail(1, "required integration source missing");
    }

    if (!contains(manifest, "Category=\"windows.startupTask\"") ||
        !contains(manifest, "TaskId=\"VelocityCopyStartup\"") ||
        !contains(manifest, "Enabled=\"true\"")) {
        return fail(2, "MSIX must register the enabled packaged startup task");
    }

    if (!contains(manifest, "Category=\"windows.comServer\"") ||
        !contains(manifest, "Category=\"windows.fileExplorerContextMenus\"") ||
        !contains(manifest, "VelocityCopy.Shell.dll")) {
        return fail(3, "MSIX must own modern Explorer COM/context-menu registration");
    }

    if (!contains(app, "ExtendedActivationKind::StartupTask") ||
        !contains(app, "!startup_activation && !is_stage_only_activation") ||
        !contains(app, "ShellAction::OpenVelocityCopy")) {
        return fail(4, "startup must remain hidden and later launches must wake the primary instance");
    }

    if (!contains(shell, "send_shell_request(request, 25)") ||
        !contains(shell, "launch_velocitycopy_with_request") ||
        contains(shell, "SetWindowsHookEx") ||
        contains(shell, "ReadDirectoryChangesW")) {
        return fail(5, "Explorer extension must stay bounded and IPC-only");
    }

    if (!contains(workflow, "GenerateAppxPackageOnBuild=true") ||
        !contains(workflow, "Sign Windows test MSIX") ||
        !contains(workflow, "signtool") ||
        !contains(workflow, "VelocityCopy-Test.cer")) {
        return fail(6, "CI must build and sign an installable test MSIX");
    }

    if (!contains(installer, "Import-Certificate") ||
        !contains(installer, "Add-AppxPackage") ||
        !contains(installer, "Remove-AppxPackage")) {
        return fail(7, "test package must ship install and uninstall flow");
    }

    if (!contains(docs, "near-zero-CPU") || !contains(docs, "IExplorerCommand") ||
        !contains(docs, "does not hook Explorer")) {
        return fail(8, "system-impact constraints must remain documented");
    }

    return 0;
}
