#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::string strip_comments_and_literals(const std::string& input) {
    std::string out = input;
    enum class State { Code, LineComment, BlockComment, String, Character };
    State state = State::Code;
    bool escaped = false;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        const char n = i + 1 < input.size() ? input[i + 1] : '\0';
        switch (state) {
        case State::Code:
            if (c == '/' && n == '/') { out[i] = out[i + 1] = ' '; ++i; state = State::LineComment; }
            else if (c == '/' && n == '*') { out[i] = out[i + 1] = ' '; ++i; state = State::BlockComment; }
            else if (c == '"') { out[i] = ' '; state = State::String; escaped = false; }
            else if (c == '\'') { out[i] = ' '; state = State::Character; escaped = false; }
            break;
        case State::LineComment:
            if (c == '\n') state = State::Code; else out[i] = ' ';
            break;
        case State::BlockComment:
            out[i] = ' ';
            if (c == '*' && n == '/') { out[i + 1] = ' '; ++i; state = State::Code; }
            break;
        case State::String:
        case State::Character: {
            const State current = state;
            out[i] = ' ';
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if ((current == State::String && c == '"') || (current == State::Character && c == '\'')) state = State::Code;
            break;
        }
        }
    }
    return out;
}

bool has_default_resource_loader(const std::string& source) {
    const auto code = strip_comments_and_literals(source);
    const std::regex forbidden(
        R"((?:(?:winrt::)?Microsoft::Windows::ApplicationModel::Resources::)?ResourceLoader\s*(?:[A-Za-z_]\w*\s*(?:;|\{\s*\}|\(\s*\))|\{\s*\}|\(\s*\)))");
    return std::regex_search(code, forbidden);
}

bool production_sources_are_clean(const std::filesystem::path& root) {
    const auto source_root = root / "src";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source_root)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h" && ext != ".idl") continue;
        if (has_default_resource_loader(read_all(entry.path()))) {
            std::cerr << "forbidden default ResourceLoader in " << entry.path().string() << '\n';
            return false;
        }
    }
    return true;
}
}

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto negative = read_all(root / "tests/contracts/fixtures/resource_loader_default.cpp.txt");
    const auto positive = read_all(root / "tests/contracts/fixtures/resource_loader_explicit.cpp.txt");
    if (negative.empty() || positive.empty()) {
        std::cerr << "resource loader fixtures are missing\n";
        return 1;
    }
    if (!has_default_resource_loader(negative)) {
        std::cerr << "negative ResourceLoader fixture was not rejected\n";
        return 2;
    }
    if (has_default_resource_loader(positive)) {
        std::cerr << "explicit ResourceLoader fixture was rejected\n";
        return 3;
    }
    if (!production_sources_are_clean(root)) return 4;
    return 0;
}
