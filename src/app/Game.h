#pragma once

#include "gameplay/PlayerController.h"
#include "render/Renderer.h"
#include "world/World.h"

#include <SDL.h>

namespace valcraft {

struct GameOptions {
    bool smoke_test = false;
    int smoke_frames = 60;
    bool hidden_window = false;
};

class Game {
public:
    explicit Game(GameOptions options = {});
    ~Game();

    auto run() -> int;

private:
    auto initialize() -> bool;
    void shutdown();
    void process_events();
    void update(float dt);
    void set_mouse_capture(bool captured);
    void select_block_from_key(SDL_Keycode keycode);

    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    bool running_ = true;
    bool mouse_captured_ = true;
    bool pending_toggle_fly_ = false;
    bool pending_break_block_ = false;
    bool pending_place_block_ = false;
    float pending_look_x_ = 0.0F;
    float pending_look_y_ = 0.0F;
    int window_width_ = 1600;
    int window_height_ = 900;
    int rendered_frames_ = 0;

    Renderer renderer_ {};
    World world_ {};
    PlayerController player_ {};
    GameOptions options_ {};
};

} // namespace valcraft
