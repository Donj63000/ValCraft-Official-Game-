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

    for (const auto argument : arguments) {
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
            continue;
        }
        if (argument == "--perf-trace") {
            result.options.performance.perf_trace_enabled = true;
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
            continue;
        }
        if (argument.starts_with("--perf-scenario=")) {
            const auto scenario = argument.substr(16);
            if (scenario.empty()) {
                return make_error("Invalid value for --perf-scenario");
            }
            result.options.performance.perf_scenario = std::string(scenario);
            continue;
        }

        result.ok = false;
        result.error_message = "Unknown argument: " + std::string(argument);
        return result;
    }

    const auto uses_smoke_only_perf_options =
        result.options.performance.perf_trace_enabled ||
        !result.options.performance.perf_json_path.empty() ||
        !result.options.performance.perf_scenario.empty();
    if (uses_smoke_only_perf_options && !result.options.smoke_test) {
        return make_error("--perf-json, --perf-trace and --perf-scenario require --smoke-test");
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
