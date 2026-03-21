#define SDL_MAIN_HANDLED
#include "app/Game.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <charconv>
#include <cstring>
#include <iostream>

namespace {

auto parse_game_options(int argc, char** argv, valcraft::GameOptions& options) -> bool {
    for (int index = 1; index < argc; ++index) {
        const auto* argument = argv[index];
        if (std::strcmp(argument, "--smoke-test") == 0) {
            options.smoke_test = true;
            options.hidden_window = true;
            continue;
        }
        if (std::strcmp(argument, "--hidden-window") == 0) {
            options.hidden_window = true;
            continue;
        }
        if (std::strncmp(argument, "--smoke-frames=", 15) == 0) {
            const char* value_begin = argument + 15;
            int parsed_value = 0;
            const auto* value_end = value_begin + std::strlen(value_begin);
            const auto result = std::from_chars(value_begin, value_end, parsed_value);
            if (result.ec != std::errc {} || result.ptr != value_end || parsed_value <= 0) {
                std::cerr << "Invalid value for --smoke-frames" << std::endl;
                return false;
            }
            options.smoke_frames = parsed_value;
            continue;
        }

        std::cerr << "Unknown argument: " << argument << std::endl;
        return false;
    }

    return true;
}

auto run_valcraft(const valcraft::GameOptions& options) -> int {
    valcraft::Game game(options);
    return game.run();
}

} // namespace

auto main(int argc, char** argv) -> int {
    valcraft::GameOptions options {};
    if (!parse_game_options(argc, argv, options)) {
        return 2;
    }
    return run_valcraft(options);
}

#ifdef _WIN32
auto WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) -> int {
    valcraft::GameOptions options {};
    if (!parse_game_options(__argc, __argv, options)) {
        return 2;
    }
    return run_valcraft(options);
}
#endif
