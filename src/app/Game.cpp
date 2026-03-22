#include "app/Game.h"
#include "app/GameBranding.h"
#include "app/InputBindings.h"
#include "app/GameLoop.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace valcraft {

namespace {

#ifdef _WIN32
constexpr std::string_view kPerformancePlatform = "windows";
#else
constexpr std::string_view kPerformancePlatform = "unknown";
#endif

#ifdef VALCRAFT_BUILD_TYPE
constexpr std::string_view kPerformanceBuildType = VALCRAFT_BUILD_TYPE;
#else
constexpr std::string_view kPerformanceBuildType = "unknown";
#endif

auto hotbar_number_from_keycode(SDL_Keycode keycode) noexcept -> int {
    switch (keycode) {
    case SDLK_1:
    case SDLK_KP_1:
        return 1;
    case SDLK_2:
    case SDLK_KP_2:
        return 2;
    case SDLK_3:
    case SDLK_KP_3:
        return 3;
    case SDLK_4:
    case SDLK_KP_4:
        return 4;
    case SDLK_5:
    case SDLK_KP_5:
        return 5;
    case SDLK_6:
    case SDLK_KP_6:
        return 6;
    case SDLK_7:
    case SDLK_KP_7:
        return 7;
    case SDLK_8:
    case SDLK_KP_8:
        return 8;
    case SDLK_9:
    case SDLK_KP_9:
        return 9;
    default:
        return 0;
    }
}

void stash_carried_inventory_item(InventoryMenuState& inventory, HotbarState& hotbar) noexcept {
    if (!inventory.carrying_item || !inventory_slot_has_item(inventory.carried_slot)) {
        return;
    }

    inventory.carried_slot = inventory_try_store_stack(inventory, hotbar, inventory.carried_slot);
    inventory.carrying_item = inventory_slot_has_item(inventory.carried_slot);
}

void center_ui_cursor(SDL_Window* window, int window_width, int window_height, float& cursor_x, float& cursor_y) noexcept {
    const auto mouse_x = std::max(window_width / 2, 0);
    const auto mouse_y = std::max(window_height / 2, 0);
    if (window != nullptr) {
        SDL_WarpMouseInWindow(window, mouse_x, mouse_y);
    }
    cursor_x = static_cast<float>(mouse_x);
    cursor_y = static_cast<float>(mouse_y);
}

void clamp_ui_cursor(float& cursor_x, float& cursor_y, int window_width, int window_height) noexcept {
    const auto max_x = static_cast<float>(std::max(window_width - 1, 0));
    const auto max_y = static_cast<float>(std::max(window_height - 1, 0));
    cursor_x = std::clamp(cursor_x, 0.0F, max_x);
    cursor_y = std::clamp(cursor_y, 0.0F, max_y);
}

auto safe_drop_direction(const glm::vec3& look_direction) noexcept -> glm::vec3 {
    if (glm::dot(look_direction, look_direction) <= 1.0e-6F) {
        return {0.0F, 0.0F, -1.0F};
    }
    return glm::normalize(look_direction);
}

} // namespace

Game::Game(GameOptions options)
    : environment_(options.initial_time_of_day, options.freeze_time || options.smoke_test),
      renderer_(),
      world_(1337, options.performance.stream_radius),
      options_(std::move(options)) {
    if (should_capture_performance()) {
        frame_samples_.reserve(static_cast<std::size_t>(std::max(options_.smoke_frames, 0)));
    }
    sync_selected_hotbar_slot();
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
            const auto frame_begin = clock::now();
            FramePerformanceStats frame_stats {};
            process_events();

            const auto now = clock::now();
            const auto measured_frame_time = now - previous;
            previous = now;
            const auto frame_time = resolve_simulation_frame_time(options_.smoke_test, measured_frame_time, fixed_step);
            accumulator += frame_time;

            constexpr int kMaxFixedUpdatesPerFrame = 4;
            int fixed_updates = 0;
            while (accumulator >= fixed_step && fixed_updates < kMaxFixedUpdatesPerFrame) {
                update(static_cast<float>(fixed_step.count()), frame_stats);
                accumulator -= fixed_step;
                ++fixed_updates;
            }

            if (fixed_updates == kMaxFixedUpdatesPerFrame && accumulator > fixed_step) {
                accumulator = fixed_step;
            }

            const auto environment_state = environment_.current_state();
            item_drops_.build_render_instances(world_, item_drop_render_instances_);
            renderer_.render_frame(
                world_,
                player_,
                hotbar_,
                inventory_menu_,
                death_screen_,
                pause_menu_,
                creatures_.render_instances(),
                item_drop_render_instances_,
                environment_state,
                window_width_,
                window_height_);
            const auto& render_stats = renderer_.last_frame_stats();
            frame_stats.upload_ms += render_stats.upload_ms;
            frame_stats.shadow_ms += render_stats.shadow_ms;
            frame_stats.world_ms += render_stats.world_ms;
            frame_stats.uploaded_meshes += render_stats.uploaded_meshes;
            frame_stats.visible_chunks += render_stats.visible_chunks;
            frame_stats.shadow_chunks += render_stats.shadow_chunks;
            frame_stats.world_chunks += render_stats.world_chunks;

            SDL_GL_SwapWindow(window_);
            frame_stats.frame_total_ms =
                std::chrono::duration<double, std::milli>(clock::now() - frame_begin).count();
            record_frame_stats(frame_stats);
            ++rendered_frames_;

            if (options_.smoke_test && rendered_frames_ >= options_.smoke_frames) {
                running_ = false;
            }
        }

        if (options_.smoke_test && should_capture_performance()) {
            const auto report = build_performance_report();
            write_performance_report(report);
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
        kGameWindowTitle.data(),
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
    if (options_.smoke_test) {
        SDL_GL_SetSwapInterval(0);
    } else if (SDL_GL_SetSwapInterval(-1) != 0) {
        SDL_GL_SetSwapInterval(1);
    }

    if (gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)) == 0) {
        return false;
    }

    RendererOptions renderer_options {};
    renderer_options.shadows_enabled = options_.performance.shadows_enabled;
    renderer_options.shadow_map_size = options_.performance.shadow_map_size;
    renderer_options.post_process_enabled = options_.performance.post_process_enabled;
    if (!renderer_.initialize(renderer_options)) {
        return false;
    }

    set_mouse_capture(!options_.smoke_test);
    normalize_inventory_state(inventory_menu_, hotbar_);
    inventory_menu_.visible = false;
    inventory_menu_.cursor_x = static_cast<float>(window_width_) * 0.5F;
    inventory_menu_.cursor_y = static_cast<float>(window_height_) * 0.5F;
    death_screen_.visible = false;
    death_screen_.selected_action = DeathScreenAction::Respawn;
    death_screen_.cursor_x = static_cast<float>(window_width_) * 0.5F;
    death_screen_.cursor_y = static_cast<float>(window_height_) * 0.5F;
    pause_menu_.visible = false;
    pause_menu_.selected_action = PauseMenuAction::Resume;
    pause_menu_.cursor_x = static_cast<float>(window_width_) * 0.5F;
    pause_menu_.cursor_y = static_cast<float>(window_height_) * 0.5F;

    const auto preload_radius = options_.performance.spawn_preload_radius;
    const auto preload_center = world_.world_to_chunk(0, 0);
    for (int dz = -preload_radius; dz <= preload_radius; ++dz) {
        for (int dx = -preload_radius; dx <= preload_radius; ++dx) {
            world_.ensure_chunk_loaded({preload_center.x + dx, preload_center.z + dz});
        }
    }
    world_.rebuild_dirty_meshes();

    if (options_.smoke_test) {
        spawn_position_ = {0.5F, 80.0F, 0.5F};
        player_.set_position(spawn_position_);
        player_.set_velocity({});
    } else {
        spawn_position_ = find_initial_spawn_position();
        player_.set_position(spawn_position_);
    }

    (void)world_.update_streaming(player_.position());
    const auto environment_state = environment_.current_state();
    creatures_.update(0.0F, world_, player_.position(), environment_state, environment_.current_creature_cycle());
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
                if (death_screen_visible_) {
                    clamp_ui_cursor(death_screen_.cursor_x, death_screen_.cursor_y, window_width_, window_height_);
                    refresh_death_screen_hover();
                }
                if (inventory_visible_) {
                    clamp_ui_cursor(inventory_menu_.cursor_x, inventory_menu_.cursor_y, window_width_, window_height_);
                    refresh_inventory_hover();
                }
                if (paused_) {
                    clamp_ui_cursor(pause_menu_.cursor_x, pause_menu_.cursor_y, window_width_, window_height_);
                    refresh_pause_menu_hover();
                }
            }
            break;
        case SDL_MOUSEMOTION:
            if (death_screen_visible_) {
                death_screen_.cursor_x = static_cast<float>(event.motion.x);
                death_screen_.cursor_y = static_cast<float>(event.motion.y);
                refresh_death_screen_hover();
                break;
            }
            if (inventory_visible_) {
                inventory_menu_.cursor_x = static_cast<float>(event.motion.x);
                inventory_menu_.cursor_y = static_cast<float>(event.motion.y);
                refresh_inventory_hover();
                break;
            }
            if (paused_) {
                pause_menu_.cursor_x = static_cast<float>(event.motion.x);
                pause_menu_.cursor_y = static_cast<float>(event.motion.y);
                refresh_pause_menu_hover();
                break;
            }
            if (mouse_captured_) {
                pending_look_x_ += static_cast<float>(event.motion.xrel);
                pending_look_y_ += static_cast<float>(event.motion.yrel);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (death_screen_visible_) {
                death_screen_.cursor_x = static_cast<float>(event.button.x);
                death_screen_.cursor_y = static_cast<float>(event.button.y);
                refresh_death_screen_hover();
                if (event.button.button == SDL_BUTTON_LEFT) {
                    const auto layout = build_death_screen_layout(window_width_, window_height_, death_screen_);
                    const auto action = death_screen_action_at(layout, death_screen_.cursor_x, death_screen_.cursor_y);
                    if (action.has_value()) {
                        activate_death_screen_action(*action);
                    }
                }
                break;
            }
            if (inventory_visible_) {
                inventory_menu_.cursor_x = static_cast<float>(event.button.x);
                inventory_menu_.cursor_y = static_cast<float>(event.button.y);
                refresh_inventory_hover();
                if (event.button.button == SDL_BUTTON_LEFT) {
                    click_inventory_slot(false);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    click_inventory_slot(true);
                }
                break;
            }
            if (paused_) {
                pause_menu_.cursor_x = static_cast<float>(event.button.x);
                pause_menu_.cursor_y = static_cast<float>(event.button.y);
                refresh_pause_menu_hover();
                if (event.button.button == SDL_BUTTON_LEFT) {
                    const auto layout = build_pause_menu_layout(window_width_, window_height_, pause_menu_);
                    const auto action = pause_menu_action_at(layout, pause_menu_.cursor_x, pause_menu_.cursor_y);
                    if (action.has_value()) {
                        activate_pause_menu_action(*action);
                    }
                }
                break;
            }
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
        case SDL_MOUSEWHEEL: {
            if (death_screen_visible_ || paused_ || inventory_visible_) {
                break;
            }
            auto scroll_y = event.wheel.y;
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                scroll_y = -scroll_y;
            }
            if (scroll_y != 0) {
                cycle_hotbar_selection(-scroll_y);
            }
            break;
        }
        case SDL_KEYDOWN:
            if (event.key.repeat != 0) {
                break;
            }

            if (death_screen_visible_) {
                switch (event.key.keysym.sym) {
                case SDLK_UP:
                case SDLK_w:
                    death_screen_.selected_action = next_death_screen_action(death_screen_.selected_action, -1);
                    break;
                case SDLK_DOWN:
                case SDLK_s:
                case SDLK_TAB:
                    death_screen_.selected_action = next_death_screen_action(
                        death_screen_.selected_action,
                        (event.key.keysym.mod & KMOD_SHIFT) != 0 ? -1 : 1);
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                case SDLK_r:
                    activate_death_screen_action(death_screen_.selected_action);
                    break;
                default:
                    break;
                }
                break;
            }

            if (inventory_visible_) {
                if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_e) {
                    set_inventory_visible(false);
                    break;
                }
                if (is_drop_action_key(event.key.keysym)) {
                    const auto full_stack = (event.key.keysym.mod & KMOD_CTRL) != 0;
                    if (inventory_menu_.carrying_item) {
                        drop_carried_inventory_stack(full_stack);
                    } else {
                        drop_hovered_inventory_stack(full_stack);
                    }
                    break;
                }

                const auto hotbar_index = hotbar_index_from_number_key(hotbar_number_from_keycode(event.key.keysym.sym));
                if (hotbar_index.has_value()) {
                    assign_hovered_inventory_slot_to_hotbar(*hotbar_index);
                }
                break;
            }

            if (event.key.keysym.sym == SDLK_ESCAPE) {
                set_paused(!paused_);
                break;
            }

            if (paused_) {
                switch (event.key.keysym.sym) {
                case SDLK_UP:
                case SDLK_w:
                    pause_menu_.selected_action = next_pause_menu_action(pause_menu_.selected_action, -1);
                    break;
                case SDLK_DOWN:
                case SDLK_s:
                    pause_menu_.selected_action = next_pause_menu_action(pause_menu_.selected_action, 1);
                    break;
                case SDLK_TAB:
                    pause_menu_.selected_action =
                        next_pause_menu_action(pause_menu_.selected_action, (event.key.keysym.mod & KMOD_SHIFT) != 0 ? -1 : 1);
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    activate_pause_menu_action(pause_menu_.selected_action);
                    break;
                default:
                    break;
                }
                break;
            }

            if (event.key.keysym.sym == SDLK_e) {
                set_inventory_visible(true);
            } else if (is_drop_action_key(event.key.keysym)) {
                drop_selected_hotbar_items((event.key.keysym.mod & KMOD_CTRL) != 0);
            } else if (event.key.keysym.sym == SDLK_f) {
                pending_toggle_fly_ = true;
            } else {
                select_hotbar_slot_from_keycode(event.key.keysym.sym);
            }
            break;
        default:
            break;
        }
    }
}

void Game::update(float dt, FramePerformanceStats& frame_stats) {
    using clock = std::chrono::steady_clock;

    if (!options_.smoke_test && (death_screen_visible_ || paused_)) {
        (void)dt;
        (void)frame_stats;
        return;
    }

    environment_.update(dt);
    const auto environment_state = environment_.current_state();
    const auto creature_cycle = environment_.current_creature_cycle();

    if (options_.smoke_test) {
        update_smoke_player(dt);
    } else {
        PlayerInput input {};
        if (!inventory_visible_) {
            input = read_player_movement_input(SDL_GetKeyboardState(nullptr));
        }
        input.toggle_fly = std::exchange(pending_toggle_fly_, false);
        input.look_delta_x = mouse_captured_ ? std::exchange(pending_look_x_, 0.0F) : 0.0F;
        input.look_delta_y = mouse_captured_ ? std::exchange(pending_look_y_, 0.0F) : 0.0F;

        player_.update(input, dt, world_);

        if (!inventory_visible_ && pending_break_block_) {
            if (const auto broken_block = player_.try_break_block(world_); broken_block.has_value()) {
                const auto drop_direction = safe_drop_direction(player_.look_direction());
                const auto drop_origin = glm::vec3 {
                    static_cast<float>(broken_block->block.x) + 0.5F,
                    static_cast<float>(broken_block->block.y) + 0.28F,
                    static_cast<float>(broken_block->block.z) + 0.5F,
                };
                spawn_dropped_stack(
                    inventory_make_slot(broken_block->block_id, 1),
                    drop_origin,
                    drop_direction * 1.4F + glm::vec3 {0.0F, 1.8F, 0.0F});
            }
            pending_break_block_ = false;
        }
        if (!inventory_visible_ && pending_place_block_) {
            auto& selected_slot = hotbar_.slots[hotbar_.selected_index];
            if (inventory_slot_has_item(selected_slot) && player_.try_place_block(world_)) {
                (void)inventory_take_from_slot(selected_slot, 1);
                normalize_inventory_state(inventory_menu_, hotbar_);
                sync_selected_hotbar_slot();
            }
            pending_place_block_ = false;
        }
        if (inventory_visible_) {
            pending_break_block_ = false;
            pending_place_block_ = false;
        }

        item_drops_.update(dt, world_, player_.position(), inventory_menu_, hotbar_);
        sync_selected_hotbar_slot();

        if (player_.is_dead()) {
            set_death_screen_visible(true, player_.state().death_cause);
            return;
        }
    }

    const auto stream_start = clock::now();
    const auto stream_stats = world_.update_streaming(player_.position());
    frame_stats.streaming_ms +=
        std::chrono::duration<double, std::milli>(clock::now() - stream_start).count();
    frame_stats.stream_chunk_changes += stream_stats.chunk_changed ? 1U : 0U;
    frame_stats.generation_enqueued += stream_stats.generation_enqueued;
    frame_stats.generation_pruned += stream_stats.generation_pruned;
    frame_stats.unloaded_chunks += stream_stats.unloaded_chunks;

    const auto world_stats = world_.process_pending_work(options_.performance.world_budget());
    frame_stats.generation_ms += world_stats.generation_ms;
    frame_stats.lighting_ms += world_stats.lighting_ms;
    frame_stats.meshing_ms += world_stats.meshing_ms;
    frame_stats.generated_chunks += world_stats.generated_chunks;
    frame_stats.meshed_chunks += world_stats.meshed_chunks;
    frame_stats.light_nodes_processed += world_stats.light_nodes_processed;
    frame_stats.pending_generation = std::max(frame_stats.pending_generation, world_stats.pending_generation);
    frame_stats.pending_mesh = std::max(frame_stats.pending_mesh, world_stats.pending_mesh);
    frame_stats.pending_lighting = std::max(frame_stats.pending_lighting, world_stats.pending_lighting);
    frame_stats.lighting_jobs_completed += world_stats.lighting_jobs_completed;

    creatures_.update(dt, world_, player_.position(), environment_state, creature_cycle);

    if (options_.smoke_test) {
        validate_smoke_frame(options_.performance.world_budget(), world_stats);
    }
}

void Game::set_mouse_capture(bool captured) {
    mouse_captured_ = captured;
    pending_look_x_ = 0.0F;
    pending_look_y_ = 0.0F;
    SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
    SDL_ShowCursor(captured ? SDL_DISABLE : SDL_ENABLE);
}

void Game::set_death_screen_visible(bool visible, PlayerDeathCause cause) {
    if (options_.smoke_test) {
        return;
    }
    if (death_screen_visible_ == visible && (!visible || death_screen_.cause == cause)) {
        return;
    }

    death_screen_visible_ = visible;
    death_screen_.visible = visible;
    pending_toggle_fly_ = false;
    pending_break_block_ = false;
    pending_place_block_ = false;

    if (death_screen_visible_) {
        if (inventory_visible_) {
            set_inventory_visible(false);
        }
        if (paused_) {
            paused_ = false;
            pause_menu_.visible = false;
        }
        death_screen_.selected_action = DeathScreenAction::Respawn;
        death_screen_.cause = cause;
        set_mouse_capture(false);
        center_ui_cursor(window_, window_width_, window_height_, death_screen_.cursor_x, death_screen_.cursor_y);
        refresh_death_screen_hover();
        return;
    }

    death_screen_.cause = PlayerDeathCause::None;
    if (!paused_ && !inventory_visible_) {
        set_mouse_capture(true);
    }
}

void Game::set_paused(bool paused) {
    if (options_.smoke_test) {
        return;
    }
    if (death_screen_visible_) {
        return;
    }

    paused_ = paused;
    pause_menu_.visible = paused;
    pause_menu_.selected_action = PauseMenuAction::Resume;
    pending_toggle_fly_ = false;
    pending_break_block_ = false;
    pending_place_block_ = false;

    if (paused_) {
        if (inventory_visible_) {
            set_inventory_visible(false);
        }
        set_mouse_capture(false);
        center_ui_cursor(window_, window_width_, window_height_, pause_menu_.cursor_x, pause_menu_.cursor_y);
        refresh_pause_menu_hover();
    } else if (!inventory_visible_) {
        set_mouse_capture(true);
    }
}

void Game::set_inventory_visible(bool visible) {
    if (options_.smoke_test) {
        return;
    }
    if (visible && (paused_ || death_screen_visible_)) {
        return;
    }
    if (inventory_visible_ == visible) {
        return;
    }

    inventory_visible_ = visible;
    inventory_menu_.visible = visible;
    pending_toggle_fly_ = false;
    pending_break_block_ = false;
    pending_place_block_ = false;

    if (inventory_visible_) {
        set_mouse_capture(false);
        center_ui_cursor(window_, window_width_, window_height_, inventory_menu_.cursor_x, inventory_menu_.cursor_y);
        refresh_inventory_hover();
        return;
    }

    stash_carried_inventory_item(inventory_menu_, hotbar_);
    if (inventory_menu_.carrying_item && inventory_slot_has_item(inventory_menu_.carried_slot)) {
        drop_carried_inventory_stack(true);
    }
    normalize_inventory_state(inventory_menu_, hotbar_);
    inventory_menu_.hovered_slot.reset();
    if (!paused_) {
        set_mouse_capture(true);
    }
    sync_selected_hotbar_slot();
}

void Game::activate_death_screen_action(DeathScreenAction action) {
    switch (action) {
    case DeathScreenAction::Respawn:
        respawn_player();
        break;
    case DeathScreenAction::Quit:
        running_ = false;
        break;
    default:
        break;
    }
}

void Game::activate_pause_menu_action(PauseMenuAction action) {
    switch (action) {
    case PauseMenuAction::Resume:
        set_paused(false);
        break;
    case PauseMenuAction::Options:
        break;
    case PauseMenuAction::Quit:
        running_ = false;
        break;
    default:
        break;
    }
}

void Game::refresh_death_screen_hover() noexcept {
    if (!death_screen_visible_) {
        return;
    }

    const auto layout = build_death_screen_layout(window_width_, window_height_, death_screen_);
    const auto hovered_action = death_screen_action_at(layout, death_screen_.cursor_x, death_screen_.cursor_y);
    if (hovered_action.has_value()) {
        death_screen_.selected_action = *hovered_action;
    }
}

void Game::refresh_pause_menu_hover() noexcept {
    const auto layout = build_pause_menu_layout(window_width_, window_height_, pause_menu_);
    const auto hovered_action = pause_menu_action_at(layout, pause_menu_.cursor_x, pause_menu_.cursor_y);
    if (hovered_action.has_value()) {
        pause_menu_.selected_action = *hovered_action;
    }
}

void Game::refresh_inventory_hover() noexcept {
    if (!inventory_visible_) {
        inventory_menu_.hovered_slot.reset();
        return;
    }

    const auto layout = build_inventory_menu_layout(window_width_, window_height_, inventory_menu_, hotbar_);
    inventory_menu_.hovered_slot = inventory_slot_at(layout, inventory_menu_.cursor_x, inventory_menu_.cursor_y);
}

void Game::click_inventory_slot(bool secondary) {
    refresh_inventory_hover();
    if (!inventory_menu_.hovered_slot.has_value()) {
        if (inventory_menu_.carrying_item && inventory_slot_has_item(inventory_menu_.carried_slot)) {
            drop_carried_inventory_stack(!secondary);
        }
        return;
    }

    if (secondary) {
        inventory_secondary_click(inventory_menu_, hotbar_, *inventory_menu_.hovered_slot);
    } else {
        inventory_primary_click(inventory_menu_, hotbar_, *inventory_menu_.hovered_slot);
    }
    refresh_inventory_hover();
    sync_selected_hotbar_slot();
}

void Game::assign_hovered_inventory_slot_to_hotbar(std::size_t hotbar_index) noexcept {
    refresh_inventory_hover();
    if (!inventory_menu_.hovered_slot.has_value()) {
        return;
    }

    inventory_swap_with_hotbar(inventory_menu_, hotbar_, *inventory_menu_.hovered_slot, hotbar_index);
    refresh_inventory_hover();
    sync_selected_hotbar_slot();
}

void Game::drop_selected_hotbar_items(bool full_stack) noexcept {
    if (death_screen_visible_ || paused_ || inventory_visible_) {
        return;
    }

    auto& selected_slot = hotbar_.slots[hotbar_.selected_index];
    const auto removed = inventory_take_from_slot(
        selected_slot,
        full_stack ? selected_slot.count : static_cast<std::uint8_t>(1));
    if (!inventory_slot_has_item(removed)) {
        return;
    }

    const auto drop_direction = safe_drop_direction(player_.look_direction());
    spawn_dropped_stack(
        removed,
        player_.eye_position() + drop_direction * 0.55F + glm::vec3 {0.0F, -0.35F, 0.0F},
        drop_direction * (full_stack ? 4.3F : 3.3F) + glm::vec3 {0.0F, 1.6F, 0.0F});
    normalize_inventory_state(inventory_menu_, hotbar_);
    sync_selected_hotbar_slot();
}

void Game::drop_hovered_inventory_stack(bool full_stack) noexcept {
    refresh_inventory_hover();
    if (!inventory_menu_.hovered_slot.has_value()) {
        return;
    }

    const auto removed = inventory_take_from_ref(
        inventory_menu_,
        hotbar_,
        *inventory_menu_.hovered_slot,
        full_stack ? kMaxItemStackCount : static_cast<std::uint8_t>(1));
    if (!inventory_slot_has_item(removed)) {
        return;
    }

    const auto drop_direction = safe_drop_direction(player_.look_direction());
    spawn_dropped_stack(
        removed,
        player_.eye_position() + drop_direction * 0.48F + glm::vec3 {0.0F, -0.38F, 0.0F},
        drop_direction * (full_stack ? 4.0F : 3.0F) + glm::vec3 {0.0F, 1.4F, 0.0F});
    refresh_inventory_hover();
    sync_selected_hotbar_slot();
}

void Game::drop_carried_inventory_stack(bool full_stack) noexcept {
    auto removed = inventory_take_from_slot(
        inventory_menu_.carried_slot,
        full_stack ? inventory_menu_.carried_slot.count : static_cast<std::uint8_t>(1));
    inventory_menu_.carrying_item = inventory_slot_has_item(inventory_menu_.carried_slot);
    if (!inventory_slot_has_item(removed)) {
        return;
    }

    const auto drop_direction = safe_drop_direction(player_.look_direction());
    spawn_dropped_stack(
        removed,
        player_.eye_position() + drop_direction * 0.45F + glm::vec3 {0.0F, -0.40F, 0.0F},
        drop_direction * (full_stack ? 3.8F : 2.7F) + glm::vec3 {0.0F, 1.3F, 0.0F});
}

void Game::spawn_dropped_stack(const HotbarSlot& stack, const glm::vec3& origin, const glm::vec3& initial_velocity) noexcept {
    item_drops_.spawn_drop(stack, origin, initial_velocity);
}

void Game::sync_selected_hotbar_slot() noexcept {
    player_.set_selected_block(selected_hotbar_block(hotbar_));
}

void Game::select_hotbar_slot(std::size_t index) noexcept {
    valcraft::select_hotbar_index(hotbar_, index);
    sync_selected_hotbar_slot();
}

void Game::cycle_hotbar_selection(int delta) noexcept {
    valcraft::cycle_hotbar_selection(hotbar_, delta);
    sync_selected_hotbar_slot();
}

void Game::select_hotbar_slot_from_keycode(SDL_Keycode keycode) {
    const auto slot_index = hotbar_index_from_number_key(hotbar_number_from_keycode(keycode));
    if (!slot_index.has_value()) {
        return;
    }
    select_hotbar_slot(*slot_index);
}

auto Game::find_initial_spawn_position() -> glm::vec3 {
    constexpr int kSpawnSearchRadius = 12;

    for (int radius = 0; radius <= kSpawnSearchRadius; ++radius) {
        for (int z = -radius; z <= radius; ++z) {
            for (int x = -radius; x <= radius; ++x) {
                if (radius > 0 && std::abs(x) != radius && std::abs(z) != radius) {
                    continue;
                }

                const auto surface_y = world_.surface_height(x, z);
                const auto surface_block = world_.get_block(x, surface_y, z);
                if (is_block_liquid(surface_block)) {
                    continue;
                }
                if (world_.get_block(x, surface_y + 1, z) != to_block_id(BlockType::Air)) {
                    continue;
                }
                if (!is_world_y_valid(surface_y + 2) || world_.get_block(x, surface_y + 2, z) != to_block_id(BlockType::Air)) {
                    continue;
                }

                return {
                    static_cast<float>(x) + 0.5F,
                    static_cast<float>(surface_y) + 1.001F,
                    static_cast<float>(z) + 0.5F,
                };
            }
        }
    }

    const auto spawn_y = static_cast<float>(world_.surface_height(0, 0)) + 1.001F;
    return {0.5F, spawn_y, 0.5F};
}

void Game::respawn_player() {
    spawn_position_ = find_initial_spawn_position();
    player_.respawn(spawn_position_);
    sync_selected_hotbar_slot();
    set_death_screen_visible(false);
    (void)world_.update_streaming(player_.position());
    creatures_.update(0.0F, world_, player_.position(), environment_.current_state(), environment_.current_creature_cycle());
}

void Game::update_smoke_player(float dt) {
    smoke_elapsed_seconds_ += dt;
    constexpr float kSmokeSpeedX = 8.0F;
    constexpr float kSmokeSpeedZ = 3.0F;

    player_.set_position({
        0.5F + smoke_elapsed_seconds_ * kSmokeSpeedX,
        80.0F,
        0.5F + smoke_elapsed_seconds_ * kSmokeSpeedZ,
    });
    player_.set_velocity({});
}

void Game::validate_smoke_frame(const WorldWorkBudget& budget, const WorldWorkStats& stats) const {
    if (stats.generated_chunks > budget.chunk_generation_budget) {
        std::ostringstream message;
        message << "Smoke test exceeded chunk generation budget (generated=" << stats.generated_chunks
                << ", budget=" << budget.chunk_generation_budget << ")";
        throw std::runtime_error(message.str());
    }
    const auto regular_meshed_chunks = stats.meshed_chunks - stats.prioritized_meshed_chunks;
    if (regular_meshed_chunks > budget.mesh_rebuild_budget) {
        std::ostringstream message;
        message << "Smoke test exceeded mesh rebuild budget (regular=" << regular_meshed_chunks
                << ", prioritized=" << stats.prioritized_meshed_chunks
                << ", total=" << stats.meshed_chunks
                << ", budget=" << budget.mesh_rebuild_budget << ")";
        throw std::runtime_error(message.str());
    }
    if (stats.light_nodes_processed > budget.light_node_budget) {
        std::ostringstream message;
        message << "Smoke test exceeded lighting node budget (processed=" << stats.light_nodes_processed
                << ", budget=" << budget.light_node_budget << ")";
        throw std::runtime_error(message.str());
    }
    if (!world_.are_chunks_ready(player_.position(), options_.performance.spawn_preload_radius)) {
        throw std::runtime_error("Smoke test detected missing ready chunks near the player");
    }
}

void Game::record_frame_stats(const FramePerformanceStats& frame_stats) {
    if (!should_capture_performance()) {
        return;
    }

    frame_samples_.push_back({
        static_cast<std::size_t>(rendered_frames_),
        frame_stats.frame_total_ms,
        frame_stats.streaming_ms,
        frame_stats.generation_ms,
        frame_stats.lighting_ms,
        frame_stats.meshing_ms,
        frame_stats.upload_ms,
        frame_stats.shadow_ms,
        frame_stats.world_ms,
        frame_stats.generated_chunks,
        frame_stats.meshed_chunks,
        frame_stats.light_nodes_processed,
        frame_stats.uploaded_meshes,
        frame_stats.pending_generation,
        frame_stats.pending_mesh,
        frame_stats.pending_lighting,
        frame_stats.stream_chunk_changes,
        frame_stats.generation_enqueued,
        frame_stats.generation_pruned,
        frame_stats.unloaded_chunks,
        frame_stats.lighting_jobs_completed,
        frame_stats.visible_chunks,
        frame_stats.shadow_chunks,
        frame_stats.world_chunks,
        PerformanceStage::Unattributed,
    });
}

auto Game::should_capture_performance() const noexcept -> bool {
    return options_.smoke_test &&
           (options_.performance.report_frame_stats ||
            !options_.performance.perf_json_path.empty() ||
            options_.performance.perf_trace_enabled ||
            !options_.performance.perf_scenario.empty());
}

auto Game::build_performance_report() const -> PerformanceRunReport {
    PerformanceReportMetadata metadata {};
    metadata.platform = std::string(kPerformancePlatform);
    metadata.build_type = std::string(kPerformanceBuildType.empty() ? std::string_view("unknown") : kPerformanceBuildType);
    metadata.smoke_frames = static_cast<std::size_t>(options_.smoke_frames);
    metadata.stream_radius = options_.performance.stream_radius;
    metadata.shadows_enabled = options_.performance.shadows_enabled;
    metadata.shadow_map_size = options_.performance.shadow_map_size;
    metadata.post_process_enabled = options_.performance.post_process_enabled;
    metadata.freeze_time = options_.freeze_time || options_.smoke_test;
    metadata.scenario = options_.performance.perf_scenario.empty() ? "smoke" : options_.performance.perf_scenario;
    return valcraft::build_performance_report(metadata, frame_samples_, options_.performance.perf_trace_enabled);
}

void Game::write_performance_report(const PerformanceRunReport& report) const {
    if (options_.performance.report_frame_stats) {
        const auto text_report = format_performance_report(report);
        if (!text_report.empty()) {
            std::cout << text_report;
        }
    }

    if (options_.performance.perf_json_path.empty()) {
        return;
    }

    const auto json_report = format_performance_json(report);
    std::filesystem::path output_path(options_.performance.perf_json_path);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Unable to open performance JSON output file");
    }
    output << json_report;
    if (!output.good()) {
        throw std::runtime_error("Unable to write performance JSON output file");
    }
}

} // namespace valcraft
