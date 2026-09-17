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
    const auto ipc = read_all(root / "src/core/ipc_transport.cpp");
    const auto window = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto tray = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Tray.cpp");
    const auto workflow = read_all(root / ".github/workflows/build.yml");
    const auto installer = read_all(root / "tools/Install-VelocityCopy-Test.ps1");
    const auto docs = read_all(root / "docs/SYSTEM_INTEGRATION.md");

    if (manifest.empty() || app.empty() || shell.empty() || ipc.empty() || window.empty() || tray.empty() ||
        workflow.empty() || installer.empty() || docs.empty()) {
        return fail(1, "required integration source missing");
    }

    if (!contains(manifest, "Category=\"windows.startupTask\"") ||
        !contains(manifest, "TaskId=\"VelocityCopyStartup\"") ||
        !contains(manifest, "Enabled=\"true\"")) {
        return fail(2, "MSIX must register the enabled packaged startup task");
    }

    if (!contains(manifest, "Category=\"windows.comServer\"") ||
        !contains(manifest, "Category=\"windows.fileExplorerContextMenus\"") ||
        !contains(manifest, "VelocityCopy.Shell.dll") ||
        !contains(manifest, "VelocityCopy.CopyTo") ||
        !contains(manifest, "VelocityCopy.OpenBackground")) {
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

    if (!contains(window, "IsMinimizable(true)") ||
        !contains(window, "IsMaximizable(false)") ||
        !contains(tray, "Shell_NotifyIconW(NIM_ADD") ||
        !contains(tray, "AddClipboardFormatListener") ||
        !contains(tray, "CFSTR_PREFERREDDROPEFFECT") ||
        !contains(tray, "SC_MINIMIZE") ||
        !contains(tray, "WM_CLOSE") ||
        !contains(tray, "SetWindowsHookExW") ||
        !contains(tray, "WH_KEYBOARD_LL") ||
        !contains(tray, "is_explorer_process") ||
        !contains(tray, "IsClipboardFormatAvailable(CF_HDROP)") ||
        !contains(tray, "explorer_folder_for_window") ||
        !contains(tray, "CallNextHookEx")) {
        return fail(6, "resident UI must scope Ctrl-V interception to valid Explorer file pastes");
    }

    if (!contains(ipc, "ConvertSidToStringSidW") ||
        !contains(ipc, "D:P(A;;GA;;;SY)(A;;GA;;;") ||
        !contains(ipc, "PIPE_REJECT_REMOTE_CLIENTS") ||
        !contains(ipc, "CreateNamedPipeW") ||
        contains(ipc, "0, nullptr);")) {
        return fail(7, "IPC pipe and mutex must use explicit local-user security");
    }

    if (!contains(workflow, "GenerateAppxPackageOnBuild=true") ||
        !contains(workflow, "Sign Windows test MSIX") ||
        !contains(workflow, "signtool") ||
        !contains(workflow, "VelocityCopy-Test.cer")) {
        return fail(8, "CI must build and sign an installable test MSIX");
    }

    if (!contains(installer, "Import-Certificate") ||
        !contains(installer, "Add-AppxPackage") ||
        !contains(installer, "Remove-AppxPackage")) {
        return fail(9, "test package must ship install and uninstall flow");
    }

    if (!contains(docs, "near-zero-CPU") || !contains(docs, "IExplorerCommand") ||
        !contains(docs, "notification-area icon") ||
        !contains(docs, "AddClipboardFormatListener")) {
        return fail(10, "system-impact constraints must remain documented");
    }

    return 0;
}
