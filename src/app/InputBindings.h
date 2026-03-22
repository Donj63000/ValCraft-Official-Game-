#pragma once

#include "gameplay/PlayerController.h"

#include <SDL.h>

namespace valcraft {

inline auto read_player_movement_input(const Uint8* keys) noexcept -> PlayerInput {
    PlayerInput input {};
    if (keys == nullptr) {
        return input;
    }

    input.move_forward =
        (keys[SDL_SCANCODE_W] != 0 ? 1.0F : 0.0F) - (keys[SDL_SCANCODE_S] != 0 ? 1.0F : 0.0F);
    input.move_right =
        (keys[SDL_SCANCODE_D] != 0 ? 1.0F : 0.0F) - (keys[SDL_SCANCODE_A] != 0 ? 1.0F : 0.0F);
    input.move_up =
        (keys[SDL_SCANCODE_SPACE] != 0 ? 1.0F : 0.0F) -
        (((keys[SDL_SCANCODE_LCTRL] != 0) || (keys[SDL_SCANCODE_RCTRL] != 0)) ? 1.0F : 0.0F);
    input.jump = keys[SDL_SCANCODE_SPACE] != 0;
    return input;
}

inline auto is_drop_action_key(const SDL_Keysym& keysym) noexcept -> bool {
    return keysym.scancode == SDL_SCANCODE_Q;
}

} // namespace valcraft
