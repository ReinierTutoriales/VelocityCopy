#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

inline std::string read_source(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    std::erase(text, '\r');
    return text;
}

inline std::string body_of(const std::string& text, const std::string& signature) {
    const auto start = text.find(signature);
    if (start == std::string::npos) return {};
    const auto open = text.find('{', start);
    if (open == std::string::npos) return {};
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        else if (text[i] == '}' && --depth == 0) return text.substr(open, i - open + 1);
    }
    return {};
}

inline std::size_t count_occurrences(const std::string& text, const std::string& value) {
    if (value.empty()) return 0;
    std::size_t count = 0;
    for (std::size_t offset = 0; (offset = text.find(value, offset)) != std::string::npos; offset += value.size()) ++count;
    return count;
}
