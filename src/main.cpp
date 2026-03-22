#define SDL_MAIN_HANDLED
#include "app/Game.h"
#include "app/GameOptions.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <iostream>

namespace {

auto run_valcraft(const valcraft::GameOptions& options) -> int {
    valcraft::Game game(options);
    return game.run();
}

} // namespace

auto main(int argc, char** argv) -> int {
    const auto parse_result = valcraft::parse_game_options(argc, argv);
    if (!parse_result.ok) {
        std::cerr << parse_result.error_message << std::endl;
        return 2;
    }
    return run_valcraft(parse_result.options);
}

#ifdef _WIN32
auto WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) -> int {
    const auto parse_result = valcraft::parse_game_options(__argc, __argv);
    if (!parse_result.ok) {
        std::cerr << parse_result.error_message << std::endl;
        return 2;
    }
    return run_valcraft(parse_result.options);
}
#endif
