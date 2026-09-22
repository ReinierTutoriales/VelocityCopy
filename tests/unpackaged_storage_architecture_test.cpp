#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

int main() {
    const auto root = std::filesystem::path{VELOCITYCOPY_SOURCE_DIR} / "src/ui";
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        std::ifstream stream(it->path(), std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        if (text.find("ApplicationData::Current") != std::string::npos) {
            std::cerr << "unpackaged storage contract: ApplicationData::Current is forbidden in src/ui: "
                      << it->path().string() << '\n';
            return 1;
        }
    }
    return ec ? 2 : 0;
}
