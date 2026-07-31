#define SDL_MAIN_HANDLED
#include "app/Game.h"
#include "app/GameOptions.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <iostream>
#include <memory>
#include <exception>
// Game dev by V.GIDON FR63500
namespace {

auto run_valcraft(const valcraft::GameOptions& options) -> int {
    try {
        auto game = std::make_unique<valcraft::Game>(options);
        return game->run();
    } catch (const std::exception& error) {
        // Je transforme une erreur d'initialisation en diagnostic exploitable
        // au lieu de laisser Windows interrompre brutalement le processus.
        std::cerr << "ValCraft fatal error: " << error.what() << std::endl;
        return 1;
    } catch (...) {
        // Je garde aussi un dernier filet pour les exceptions non standard.
        std::cerr << "ValCraft fatal error: unknown exception" << std::endl;
        return 1;
    }
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
