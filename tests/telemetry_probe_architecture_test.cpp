#include "architecture_support.hpp"

#include <iostream>
#include <regex>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
bool contains(const std::string& text, const std::string& value) { return text.find(value) != std::string::npos; }
int fail(const int code, const char* message) {
    std::cerr << "telemetry probe architecture contract " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main() {
    const std::filesystem::path ui{std::filesystem::path{VELOCITYCOPY_SOURCE_DIR} / "src/ui/VelocityCopy.UI"};
    const auto header = read_source(ui / "MainWindow.xaml.h");
    const auto main_cpp = read_source(ui / "MainWindow.xaml.cpp");
    const auto probe_cpp = read_source(ui / "MainWindow.Diagnostics.cpp");
    const auto xaml = read_source(ui / "MainWindow.xaml");
    const auto tokens = read_source(std::filesystem::path{VELOCITYCOPY_SOURCE_DIR} / "src/ui/DesignTokens.xaml");
    const auto project = read_source(ui / "VelocityCopy.UI.vcxproj");
    if (header.empty() || main_cpp.empty() || probe_cpp.empty() || xaml.empty() || tokens.empty() || project.empty())
        return fail(1, "required source missing");

    // 1. Declared AND defined, with a real body.
    if (!contains(header, "void LogTelemetryGeometry();") || !contains(header, "void ScheduleTelemetryGeometryProbe();"))
        return fail(2, "probe declarations missing");
    const auto body = body_of(probe_cpp, "void MainWindow::LogTelemetryGeometry()");
    if (body.size() < 1500) return fail(3, "LogTelemetryGeometry is declared but not implemented");
    if (!contains(project, "MainWindow.Diagnostics.cpp")) return fail(4, "probe source not part of the UI project");

    // 2. Measures through the real formatters and records the required metrics.
    for (const char* needle : {"FormatSpeed(", "FormatEta(", "FormatProgressPercent(", "DesiredSize()", "validation",
                               "eta_unbounded", "declared", "live_gaps", "holgura", "last_text_scale_factor_",
                               "last_rasterization_scale_", "VELOCITYCOPY_BUILD_SHA", "OutputDebugStringW",
                               "app_data_directory"}) {
        if (!contains(body, needle)) return fail(5, "probe body lacks a required metric or output");
    }

    // 3. Packaged-only storage is unavailable to this unpackaged build.
    if (contains(probe_cpp, "ApplicationData") || contains(probe_cpp, "LocalFolder"))
        return fail(6, "probe must use app_data_directory(), not packaged ApplicationData");

    // 4. No personal data and no permanent layout logger.
    for (const char* forbidden : {"SourcePathText", "DestinationPathText", "DetailsSourceText", "DetailsDestinationText",
                                  "FileNameText", "current_source", "current_destination", "LayoutUpdated"}) {
        if (contains(probe_cpp, forbidden)) return fail(7, "probe touches transfer data or a permanent layout hook");
    }

    // 5. Measuring must not change layout: no resize call and no geometry/text setter with an argument.
    if (contains(body, "ResizeWindow")) return fail(8, "probe body resizes the window");
    static const std::regex setter{R"re(\.(Width|MinWidth|MaxWidth|Height|Margin|Padding|Spacing|ColumnSpacing|Text)\([^)\s])re"};
    if (std::regex_search(body, setter)) return fail(8, "probe body mutates geometry or text");

    // 6. Triggered at the right moments, debounced, diagnostic builds only.
    if (count_occurrences(main_cpp, "ScheduleTelemetryGeometryProbe()") < 4)
        return fail(9, "probe is not scheduled at load, text-size, DPI and mode changes");
    if (!contains(probe_cpp, "#if defined(VELOCITYCOPY_GEOMETRY_PROBE)")) return fail(10, "probe start is not diagnostic-gated");

    // 7. The retired Width=68/36/48 experiment must not return.
    for (const char* name : {"SpeedText", "ProgressPercentText", "EtaText"}) {
        const auto at = xaml.find(std::string{"x:Name=\""} + name + "\"");
        if (at == std::string::npos) return fail(11, "telemetry TextBlock missing");
        const auto end = xaml.find("/>", at);
        const auto element = xaml.substr(at, end - at);
        for (const char* width : {"Width=\"68\"", "Width=\"36\"", "Width=\"48\""}) {
            std::string needle{width};
            std::size_t pos = 0;
            while ((pos = element.find(needle, pos)) != std::string::npos) {
                if (pos == 0 || element[pos - 1] != 'n') return fail(12, "fixed telemetry Width resurfaced");
                pos += needle.size();
            }
        }
    }
    for (const char* key : {"TelemetrySpeedWidth", "TelemetryPercentWidth", "TelemetryEtaWidth"}) {
        if (contains(tokens, key) || contains(xaml, key)) return fail(13, "retired fixed-width telemetry token returned");
    }
    return 0;
}
