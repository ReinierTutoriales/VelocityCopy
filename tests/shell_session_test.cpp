#include "velocitycopy/shell_session.hpp"

#include <filesystem>
#include <iostream>

int wmain() {
    using namespace velocitycopy;

    ShellSession session;

    ShellRequest copy{};
    copy.action = ShellAction::CopySelection;
    copy.sources = {
        std::filesystem::path(L"C:\\Media\\Novela\\capitulo1.mkv"),
        std::filesystem::path(L"C:\\Media\\Novela\\capitulo2.mkv")};

    const auto copy_result = session.dispatch(copy);
    if (copy_result.status != ShellDispatchStatus::Accepted || copy_result.job ||
        session.staged_sources().size() != 2) {
        return 1;
    }

    ShellRequest paste{};
    paste.action = ShellAction::PasteToFolder;
    paste.destination = L"D:\\Backup";
    paste.layout = DestinationLayout::PreserveSourceFolder;

    const auto paste_result = session.dispatch(paste);
    if (paste_result.status != ShellDispatchStatus::Accepted || !paste_result.job ||
        paste_result.job->sources != copy.sources || paste_result.job->destination != paste.destination ||
        paste_result.job->layout != paste.layout) {
        return 2;
    }

    ShellRequest direct{};
    direct.action = ShellAction::CopySelectionTo;
    direct.sources = {std::filesystem::path(L"C:\\Temp\\setup.exe")};
    direct.destination = L"E:\\Tools";
    direct.layout = DestinationLayout::ContentsOnly;

    const auto direct_result = session.dispatch(direct);
    if (direct_result.status != ShellDispatchStatus::Accepted || !direct_result.job ||
        direct_result.job->sources != direct.sources || direct_result.job->destination != direct.destination) {
        return 3;
    }

    ShellRequest prompt{};
    prompt.action = ShellAction::CopySelectionPromptDestination;
    prompt.sources = {std::filesystem::path(L"C:\\Temp\\prompt.bin")};
    const auto prompt_result = session.dispatch(prompt);
    if (prompt_result.status != ShellDispatchStatus::Accepted || prompt_result.job ||
        !prompt_result.show_window) {
        return 4;
    }

    session.clear_staged_sources();
    const auto empty_paste = session.dispatch(paste);
    if (empty_paste.status != ShellDispatchStatus::NoStagedSources || empty_paste.job) {
        return 5;
    }

    std::wcout << L"VelocityCopy shell session test passed.\n";
    return 0;
}
