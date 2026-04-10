#include "app/GameOptions.h"

#include <charconv>
#include <vector>

namespace valcraft {

namespace {

template <typename Number>
auto parse_number(std::string_view text, Number& value) -> bool {
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc {} && result.ptr == end;
}

auto make_error(std::string_view message) -> GameOptionParseResult {
    GameOptionParseResult result {};
    result.error_message = std::string(message);
    return result;
}

} // namespace

auto parse_game_options(std::span<const std::string_view> arguments) -> GameOptionParseResult {
    GameOptionParseResult result {};
    result.ok = true;
    result.options.raw_arguments.reserve(arguments.size());

    for (const auto argument : arguments) {
        result.options.raw_arguments.emplace_back(argument);
        if (argument == "--smoke-test") {
            result.options.smoke_test = true;
            result.options.hidden_window = true;
            continue;
        }
        if (argument == "--hidden-window") {
            result.options.hidden_window = true;
            continue;
        }
        if (argument == "--freeze-time") {
            result.options.freeze_time = true;
            continue;
        }
        if (argument == "--disable-shadows") {
            result.options.performance.shadows_enabled = false;
            continue;
        }
        if (argument == "--disable-post-process") {
            result.options.performance.post_process_enabled = false;
            continue;
        }
        if (argument == "--perf-report") {
            result.options.performance.report_frame_stats = true;
            result.options.audit.enabled = true;
            result.options.audit.console_summary = true;
            continue;
        }
        if (argument == "--perf-trace") {
            result.options.performance.perf_trace_enabled = true;
            result.options.audit.enabled = true;
            result.options.audit.trace_frames = true;
            continue;
        }
        if (argument == "--audit") {
            result.options.audit.enabled = true;
            continue;
        }
        if (argument == "--audit-trace-frames") {
            result.options.audit.enabled = true;
            result.options.audit.trace_frames = true;
            continue;
        }
        if (argument.starts_with("--smoke-frames=")) {
            int parsed_value = 0;
            if (!parse_number(argument.substr(15), parsed_value) || parsed_value <= 0) {
                return make_error("Invalid value for --smoke-frames");
            }
            result.options.smoke_frames = parsed_value;
            continue;
        }
        if (argument.starts_with("--initial-time=")) {
            float parsed_value = 0.0F;
            if (!parse_number(argument.substr(15), parsed_value)) {
                return make_error("Invalid value for --initial-time");
            }
            result.options.initial_time_of_day = parsed_value;
            continue;
        }
        if (argument.starts_with("--shadow-map-size=")) {
            int parsed_value = 0;
            if (!parse_number(argument.substr(18), parsed_value) || parsed_value <= 0) {
                return make_error("Invalid value for --shadow-map-size");
            }
            result.options.performance.shadow_map_size = parsed_value;
            continue;
        }
        if (argument.starts_with("--stream-radius=")) {
            int parsed_value = 0;
            if (!parse_number(argument.substr(16), parsed_value) || parsed_value < 0) {
                return make_error("Invalid value for --stream-radius");
            }
            result.options.performance.stream_radius = parsed_value;
            continue;
        }
        if (argument.starts_with("--perf-json=")) {
            const auto path = argument.substr(12);
            if (path.empty()) {
                return make_error("Invalid value for --perf-json");
            }
            result.options.performance.perf_json_path = std::string(path);
            result.options.audit.enabled = true;
            result.options.audit.compatibility_json_path = std::string(path);
            continue;
        }
        if (argument.starts_with("--perf-scenario=")) {
            const auto scenario = argument.substr(16);
            if (scenario.empty()) {
                return make_error("Invalid value for --perf-scenario");
            }
            result.options.performance.perf_scenario = std::string(scenario);
            result.options.audit.enabled = true;
            result.options.audit.label = std::string(scenario);
            continue;
        }
        if (argument.starts_with("--audit-mode=")) {
            const auto parsed_mode = parse_audit_mode(argument.substr(13));
            if (!parsed_mode.has_value()) {
                return make_error("Invalid value for --audit-mode");
            }
            result.options.audit.enabled = true;
            result.options.audit.mode = *parsed_mode;
            continue;
        }
        if (argument.starts_with("--audit-dir=")) {
            const auto path = argument.substr(12);
            if (path.empty()) {
                return make_error("Invalid value for --audit-dir");
            }
            result.options.audit.enabled = true;
            result.options.audit.root_directory = std::string(path);
            continue;
        }
        if (argument.starts_with("--audit-label=")) {
            const auto label = argument.substr(14);
            if (label.empty()) {
                return make_error("Invalid value for --audit-label");
            }
            result.options.audit.enabled = true;
            result.options.audit.label = std::string(label);
            continue;
        }

        result.ok = false;
        result.error_message = "Unknown argument: " + std::string(argument);
        return result;
    }

    return result;
}

auto parse_game_options(int argc, char** argv) -> GameOptionParseResult {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return parse_game_options(arguments);
}

} // namespace valcraft
