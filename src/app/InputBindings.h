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
    input.sprint = (keys[SDL_SCANCODE_LSHIFT] != 0) || (keys[SDL_SCANCODE_RSHIFT] != 0);
    return input;
}

inline auto is_drop_action_key(const SDL_Keysym& keysym) noexcept -> bool {
    return keysym.scancode == SDL_SCANCODE_Q;
}

inline auto is_flight_action_key(const SDL_Keysym& keysym) noexcept -> bool {
    return keysym.scancode == SDL_SCANCODE_F;
}

inline auto is_super_vision_action_key(const SDL_Keysym& keysym) noexcept -> bool {
    return keysym.scancode == SDL_SCANCODE_V;
}

inline auto is_reload_action_key(const SDL_Keysym& keysym) noexcept -> bool {
    return keysym.scancode == SDL_SCANCODE_R;
}

inline auto is_command_console_key(const SDL_Keysym& keysym) noexcept -> bool {
    // Je cible la position physique sous Echap : elle correspond a ² sur AZERTY.
    return keysym.scancode == SDL_SCANCODE_GRAVE;
}

} // namespace valcraft
