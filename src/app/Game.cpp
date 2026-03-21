#include "app/Game.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <utility>

namespace valcraft {

Game::Game(GameOptions options)
    : options_(options) {
}

Game::~Game() {
    shutdown();
}

auto Game::run() -> int {
    try {
        if (!initialize()) {
            shutdown();
            return 1;
        }

        using clock = std::chrono::steady_clock;
        constexpr auto fixed_step = std::chrono::duration<double>(1.0 / 60.0);

        auto previous = clock::now();
        auto accumulator = std::chrono::duration<double>::zero();

        while (running_) {
            process_events();

            const auto now = clock::now();
            auto frame_time = now - previous;
            previous = now;
            if (frame_time > std::chrono::milliseconds(250)) {
                frame_time = std::chrono::milliseconds(250);
            }
            accumulator += frame_time;

            while (accumulator >= fixed_step) {
                update(static_cast<float>(fixed_step.count()));
                accumulator -= fixed_step;
            }

            renderer_.render_frame(world_, player_, window_width_, window_height_);
            SDL_GL_SwapWindow(window_);
            ++rendered_frames_;

            if (options_.smoke_test && rendered_frames_ >= options_.smoke_frames) {
                running_ = false;
            }
        }

        shutdown();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ValCraft fatal error: " << exception.what() << std::endl;
        shutdown();
        return 1;
    } catch (...) {
        std::cerr << "ValCraft fatal error: unknown exception" << std::endl;
        shutdown();
        return 1;
    }
}

auto Game::initialize() -> bool {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    const auto window_flags = static_cast<Uint32>(
        SDL_WINDOW_OPENGL |
        (options_.hidden_window ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE));

    window_ = SDL_CreateWindow(
        "ValCraft - WASD move, Space jump, F fly, 1-7 blocks, LMB/RMB interact",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        window_width_,
        window_height_,
        window_flags);
    if (window_ == nullptr) {
        return false;
    }

    gl_context_ = SDL_GL_CreateContext(window_);
    if (gl_context_ == nullptr) {
        return false;
    }

    SDL_GL_MakeCurrent(window_, gl_context_);
    SDL_GL_SetSwapInterval(options_.smoke_test ? 0 : 1);

    if (gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)) == 0) {
        return false;
    }

    if (!renderer_.initialize()) {
        return false;
    }

    set_mouse_capture(!options_.smoke_test);

    world_.update_streaming({0.0F, 0.0F, 0.0F});
    const auto spawn_y = static_cast<float>(world_.surface_height(0, 0)) + 1.001F;
    player_.set_position({0.5F, spawn_y, 0.5F});
    world_.update_streaming(player_.position());
    world_.rebuild_dirty_meshes();

    return true;
}

void Game::shutdown() {
    renderer_.shutdown();

    if (gl_context_ != nullptr) {
        SDL_GL_DeleteContext(gl_context_);
        gl_context_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
        SDL_Quit();
    }
}

void Game::process_events() {
    SDL_Event event {};
    while (SDL_PollEvent(&event) != 0) {
        if (options_.smoke_test) {
            if (event.type == SDL_QUIT) {
                running_ = false;
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                window_width_ = event.window.data1;
                window_height_ = event.window.data2;
            }
            continue;
        }

        switch (event.type) {
        case SDL_QUIT:
            running_ = false;
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                window_width_ = event.window.data1;
                window_height_ = event.window.data2;
            }
            break;
        case SDL_MOUSEMOTION:
            if (mouse_captured_) {
                pending_look_x_ += static_cast<float>(event.motion.xrel);
                pending_look_y_ += static_cast<float>(event.motion.yrel);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (!mouse_captured_) {
                set_mouse_capture(true);
                break;
            }
            if (event.button.button == SDL_BUTTON_LEFT) {
                pending_break_block_ = true;
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                pending_place_block_ = true;
            }
            break;
        case SDL_KEYDOWN:
            if (event.key.repeat != 0) {
                break;
            }

            if (event.key.keysym.sym == SDLK_ESCAPE) {
                set_mouse_capture(!mouse_captured_);
            } else if (event.key.keysym.sym == SDLK_f) {
                pending_toggle_fly_ = true;
            } else {
                select_block_from_key(event.key.keysym.sym);
            }
            break;
        default:
            break;
        }
    }
}

void Game::update(float dt) {
    world_.update_streaming(player_.position());

    const auto* keys = SDL_GetKeyboardState(nullptr);
    PlayerInput input {};
    input.move_forward = (keys[SDL_SCANCODE_W] ? 1.0F : 0.0F) - (keys[SDL_SCANCODE_S] ? 1.0F : 0.0F);
    input.move_right = (keys[SDL_SCANCODE_D] ? 1.0F : 0.0F) - (keys[SDL_SCANCODE_A] ? 1.0F : 0.0F);
    input.move_up = (keys[SDL_SCANCODE_SPACE] ? 1.0F : 0.0F) -
                    ((keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) ? 1.0F : 0.0F);
    input.jump = keys[SDL_SCANCODE_SPACE] != 0;
    input.toggle_fly = std::exchange(pending_toggle_fly_, false);
    input.look_delta_x = mouse_captured_ ? std::exchange(pending_look_x_, 0.0F) : 0.0F;
    input.look_delta_y = mouse_captured_ ? std::exchange(pending_look_y_, 0.0F) : 0.0F;

    player_.update(input, dt, world_);

    if (pending_break_block_) {
        player_.try_break_block(world_);
        pending_break_block_ = false;
    }
    if (pending_place_block_) {
        player_.try_place_block(world_);
        pending_place_block_ = false;
    }

    world_.update_streaming(player_.position());
    world_.rebuild_dirty_meshes();
}

void Game::set_mouse_capture(bool captured) {
    mouse_captured_ = captured;
    SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
    SDL_ShowCursor(captured ? SDL_DISABLE : SDL_ENABLE);
}

void Game::select_block_from_key(SDL_Keycode keycode) {
    switch (keycode) {
    case SDLK_1:
        player_.set_selected_block(to_block_id(BlockType::Grass));
        break;
    case SDLK_2:
        player_.set_selected_block(to_block_id(BlockType::Dirt));
        break;
    case SDLK_3:
        player_.set_selected_block(to_block_id(BlockType::Stone));
        break;
    case SDLK_4:
        player_.set_selected_block(to_block_id(BlockType::Sand));
        break;
    case SDLK_5:
        player_.set_selected_block(to_block_id(BlockType::Wood));
        break;
    case SDLK_6:
        player_.set_selected_block(to_block_id(BlockType::Leaves));
        break;
    case SDLK_7:
        player_.set_selected_block(to_block_id(BlockType::Air));
        break;
    default:
        break;
    }
}

} // namespace valcraft
