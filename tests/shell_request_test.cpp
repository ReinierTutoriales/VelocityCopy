#include "velocitycopy/shell_request.hpp"

int main() {
    velocitycopy::ShellRequest open{};
    open.action = velocitycopy::ShellAction::OpenVelocityCopy;
    if (!velocitycopy::shell_request_valid(open)) {
        return 1;
    }

    velocitycopy::ShellRequest copy{};
    copy.action = velocitycopy::ShellAction::CopySelection;
    if (velocitycopy::shell_request_valid(copy)) {
        return 2;
    }
    copy.sources = {L"C:\\Source\\file.txt"};
    if (!velocitycopy::shell_request_valid(copy)) {
        return 3;
    }

    velocitycopy::ShellRequest paste{};
    paste.action = velocitycopy::ShellAction::PasteToFolder;
    if (velocitycopy::shell_request_valid(paste)) {
        return 4;
    }
    paste.destination = L"D:\\Destination";
    if (!velocitycopy::shell_request_valid(paste)) {
        return 5;
    }

    velocitycopy::ShellRequest copy_to{};
    copy_to.action = velocitycopy::ShellAction::CopySelectionTo;
    copy_to.sources = {L"C:\\Source\\file.txt"};
    copy_to.destination = L"D:\\Destination";
    if (!velocitycopy::shell_request_valid(copy_to)) {
        return 6;
    }

    ShellRequest prompt{};
    prompt.action = ShellAction::CopySelectionPromptDestination;
    if (shell_request_valid(prompt)) {
        return 7;
    }
    prompt.sources = {L"C:\\Source\\prompt.txt"};
    if (!shell_request_valid(prompt)) {
        return 8;
    }

    copy_to.version = 2;
    if (velocitycopy::shell_request_valid(copy_to)) {
        return 9;
    }

    return 0;
}
