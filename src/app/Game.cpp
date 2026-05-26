#include "app/Game.h"
#include "app/GameBranding.h"
#include "app/InputBindings.h"
#include "app/GameLoop.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

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

constexpr std::size_t kMaxGameplayAnnouncementQueue = 6U;

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

auto finite_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) ? value : fallback;
}

auto finite_vec3_or(const glm::vec3& value, const glm::vec3& fallback) noexcept -> glm::vec3 {
    return {
        finite_or(value.x, fallback.x),
        finite_or(value.y, fallback.y),
        finite_or(value.z, fallback.z),
    };
}

auto safe_drop_direction(const glm::vec3& look_direction) noexcept -> glm::vec3 {
    if (!std::isfinite(look_direction.x) ||
        !std::isfinite(look_direction.y) ||
        !std::isfinite(look_direction.z) ||
        glm::dot(look_direction, look_direction) <= 1.0e-6F) {
        return {0.0F, 0.0F, -1.0F};
    }
    return glm::normalize(look_direction);
}

auto block_coord_from_position(const glm::vec3& position) noexcept -> BlockCoord {
    return {
        static_cast<int>(std::floor(position.x)),
        static_cast<int>(std::floor(position.y)),
        static_cast<int>(std::floor(position.z)),
    };
}

auto executable_directory_from_sdl() -> std::filesystem::path {
    std::filesystem::path executable_directory;
    if (char* base_path = SDL_GetBasePath(); base_path != nullptr) {
        executable_directory = std::filesystem::path(base_path);
        SDL_free(base_path);
    }

    return executable_directory;
}

void apply_window_icon(SDL_Window* window) noexcept {
    if (window == nullptr) {
        return;
    }

    try {
        const auto icon_path = resolve_window_icon_path(std::filesystem::current_path(), executable_directory_from_sdl());
        if (!icon_path.has_value()) {
            return;
        }

        SDL_Surface* icon_surface = SDL_LoadBMP(icon_path->string().c_str());
        if (icon_surface == nullptr) {
            return;
        }

        SDL_SetWindowIcon(window, icon_surface);
        SDL_FreeSurface(icon_surface);
    } catch (...) {
    }
}

void save_current_backbuffer_bmp(const std::filesystem::path& output_path, int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid frame capture dimensions");
    }

    const auto capture_width = static_cast<std::size_t>(width);
    const auto capture_height = static_cast<std::size_t>(height);
    std::vector<std::uint8_t> pixels(capture_width * capture_height * 4U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
        SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32),
        SDL_FreeSurface);
    if (!surface) {
        throw std::runtime_error("Unable to allocate frame capture surface");
    }

    const auto source_pitch = capture_width * 4U;
    auto* destination_pixels = static_cast<std::uint8_t*>(surface->pixels);
    for (int y = 0; y < height; ++y) {
        const auto source_y = height - 1 - y;
        const auto* source_row = pixels.data() + static_cast<std::size_t>(source_y) * source_pitch;
        auto* destination_row = destination_pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch);
        std::memcpy(destination_row, source_row, source_pitch);
    }

    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    if (SDL_SaveBMP(surface.get(), output_path.string().c_str()) != 0) {
        throw std::runtime_error(std::string("Unable to save frame capture: ") + SDL_GetError());
    }
}

} // namespace

Game::Game(const GameOptions& options)
    : environment_(options.initial_time_of_day, options.freeze_time || options.smoke_test, 1337U),
      renderer_(),
      world_(1337, options.performance.stream_radius),
      options_(options) {
    runtime_shadows_enabled_ = options_.performance.shadows_enabled;
    runtime_post_process_enabled_ = options_.performance.post_process_enabled;
    if (should_capture_performance()) {
        const auto reserved_frames = options_.smoke_test
                                         ? static_cast<std::size_t>(std::max(options_.smoke_frames, 0))
                                         : static_cast<std::size_t>(1024);
        frame_samples_.reserve(reserved_frames);
        performance_events_.reserve(options_.smoke_test ? 64U : 256U);
    }
    audit_second_accumulator_.reset(0);
    initialize_audit();
    sync_selected_hotbar_slot();
}

Game::~Game() {
    shutdown();
}

auto Game::run() -> int {
    PerformanceRunReport final_report {};
    auto final_status = AuditRunStatus::Aborted;

    try {
        if (!initialize()) {
            if (should_capture_performance()) {
                final_report = build_performance_report();
                finalize_audit(final_report, AuditRunStatus::Aborted);
            }
            shutdown();
            return 1;
        }

        record_audit_event(
            AuditEventCategory::Session,
            "initialize_complete",
            AuditSeverity::Info,
            audit_json_object({
                {"smoke_test", audit_json_bool(options_.smoke_test)},
                {"hidden_window", audit_json_bool(options_.hidden_window)},
                {"window_width", audit_json_number(window_width_)},
                {"window_height", audit_json_number(window_height_)},
            }),
            AuditPriority::Critical);

        using clock = std::chrono::steady_clock;
        constexpr auto fixed_step = std::chrono::duration<double>(1.0 / 60.0);

        auto previous = clock::now();
        auto accumulator = std::chrono::duration<double>::zero();

        while (running_) {
            const auto frame_begin = clock::now();
            FramePerformanceStats frame_stats {};
            frame_raw_input_events_ = 0;
            frame_input_action_events_ = 0;
            process_events();

            const auto now = clock::now();
            const auto measured_frame_time = now - previous;
            previous = now;
            const auto frame_time = resolve_simulation_frame_time(options_.smoke_test, measured_frame_time, fixed_step);
            accumulator += frame_time;

            constexpr int kMaxFixedUpdatesPerFrame = 4;
            int fixed_updates = 0;
            while (accumulator >= fixed_step && fixed_updates < kMaxFixedUpdatesPerFrame) {
                update_simulation(static_cast<float>(fixed_step.count()), frame_stats);
                accumulator -= fixed_step;
                ++fixed_updates;
            }

            if (fixed_updates == kMaxFixedUpdatesPerFrame && accumulator > fixed_step) {
                accumulator = fixed_step;
            }

            update_world_pipeline(frame_stats);

            const auto environment_state = environment_.current_state();
            const auto creature_cycle = environment_.current_creature_cycle();
            music_.sync_environment(environment_state, creature_cycle, has_active_session_, front_end_visible());
            music_.pump();
            item_drops_.build_render_instances(world_, item_drop_render_instances_);
            renderer_.render_frame(
                world_,
                render_player(),
                hotbar_,
                inventory_menu_,
                death_screen_,
                pause_menu_,
                main_menu_,
                save_slot_menu_,
                options_menu_,
                confirm_dialog_,
                creatures_.render_instances(),
                item_drop_render_instances_,
                progression_.state(),
                super_vision_active_ && progression_.has_super_vision_power(),
                current_gameplay_announcement_view(),
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

            capture_current_frame_if_requested();
            SDL_GL_SwapWindow(window_);
            frame_stats.frame_total_ms =
                std::chrono::duration<double, std::milli>(clock::now() - frame_begin).count();
            record_frame_stats(frame_stats);
            ++rendered_frames_;

            if (options_.smoke_test && rendered_frames_ >= options_.smoke_frames) {
                running_ = false;
            }
        }

        if (should_capture_performance()) {
            final_report = build_performance_report();
            try {
                write_performance_report(final_report);
            } catch (const std::exception& exception) {
                if (audit_) {
                    audit_->record_error(std::string("Performance report write failed: ") + exception.what());
                }
                std::cerr << "ValCraft audit warning: " << exception.what() << std::endl;
            }
            final_status = AuditRunStatus::Completed;
            finalize_audit(final_report, final_status);
        }

        shutdown();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ValCraft fatal error: " << exception.what() << std::endl;
        if (should_capture_performance()) {
            final_report = build_performance_report();
            finalize_audit(final_report, AuditRunStatus::Aborted);
        }
        shutdown();
        return 1;
    } catch (...) {
        std::cerr << "ValCraft fatal error: unknown exception" << std::endl;
        if (should_capture_performance()) {
            final_report = build_performance_report();
            finalize_audit(final_report, AuditRunStatus::Aborted);
        }
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

    apply_window_icon(window_);

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
    renderer_options.shadows_enabled = runtime_shadows_enabled_;
    renderer_options.shadow_map_size = options_.performance.shadow_map_size;
    renderer_options.post_process_enabled = runtime_post_process_enabled_;
    if (!renderer_.initialize(renderer_options)) {
        return false;
    }

    if (!options_.smoke_test) {
        (void)music_.initialize();
    }

    save_root_directory_ = resolve_save_root_directory();
    normalize_inventory_state(inventory_menu_, hotbar_);
    sync_selected_hotbar_slot();
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
    main_menu_.visible = false;
    main_menu_.selected_action = MainMenuAction::Play;
    main_menu_.cursor_x = static_cast<float>(window_width_) * 0.5F;
    main_menu_.cursor_y = static_cast<float>(window_height_) * 0.5F;
    save_slot_menu_.visible = false;
    save_slot_menu_.selected_index = 0;
    save_slot_menu_.cursor_x = static_cast<float>(window_width_) * 0.5F;
    save_slot_menu_.cursor_y = static_cast<float>(window_height_) * 0.5F;
    options_menu_.visible = false;
    options_menu_.parent = OptionsMenuParent::MainMenu;
    options_menu_.selected_action = OptionsMenuAction::ToggleShadows;
    options_menu_.cursor_x = static_cast<float>(window_width_) * 0.5F;
    options_menu_.cursor_y = static_cast<float>(window_height_) * 0.5F;
    options_menu_.shadows_enabled = runtime_shadows_enabled_;
    options_menu_.post_process_enabled = runtime_post_process_enabled_;
    confirm_dialog_.visible = false;
    confirm_dialog_.cursor_x = static_cast<float>(window_width_) * 0.5F;
    confirm_dialog_.cursor_y = static_cast<float>(window_height_) * 0.5F;

    present_loading_screen("VALCRAFT", "PREPARATION DU MENU", 0.05F);
    initialize_preview_world();
    if (!running_) {
        return false;
    }
    present_loading_screen("VALCRAFT", "LECTURE DES SAUVEGARDES", 0.92F);
    refresh_save_slots();

    if (options_.startup_ui_overlay != StartupUiOverlay::None) {
        has_active_session_ = true;
        environment_.set_frozen(true);
        prepare_game_session();
        if (options_.startup_ui_overlay == StartupUiOverlay::Inventory) {
            set_inventory_visible(true);
        } else if (options_.startup_ui_overlay == StartupUiOverlay::Pause) {
            set_paused(true);
        }
    } else if (options_.smoke_test) {
        has_active_session_ = true;
        player_.set_position({0.5F, 80.0F, 0.5F});
        player_.set_velocity({});
        set_mouse_capture(true);
        environment_.set_frozen(true);
    } else {
        has_active_session_ = false;
        set_mouse_capture(false);
        open_main_menu(false);
    }
    SDL_SetWindowTitle(window_, kGameWindowTitle.data());
    return true;
}

void Game::initialize_audit() {
    if (!options_.audit.enabled) {
        return;
    }

    AuditStartContext context {};
    context.platform = std::string(kPerformancePlatform);
    context.build_type = std::string(kPerformanceBuildType.empty() ? std::string_view("unknown") : kPerformanceBuildType);
    context.working_directory = std::filesystem::current_path();
    context.arguments = options_.raw_arguments;
    context.smoke_test = options_.smoke_test;

    audit_ = std::make_unique<AuditRecorder>(options_.audit, std::move(context));
    last_audit_ui_screen_ = active_ui_screen();
    last_audit_mouse_captured_ = mouse_captured_;

    record_audit_event(
        AuditEventCategory::Session,
        "session_start",
        AuditSeverity::Info,
        audit_json_object({
            {"mode", audit_json_string(audit_mode_name(options_.audit.mode))},
            {"label", audit_json_string(options_.audit.label)},
            {"smoke_test", audit_json_bool(options_.smoke_test)},
            {"freeze_time", audit_json_bool(options_.freeze_time)},
            {"stream_radius", audit_json_number(options_.performance.stream_radius)},
            {"shadow_map_size", audit_json_number(options_.performance.shadow_map_size)},
            {"shadows_enabled", audit_json_bool(options_.performance.shadows_enabled)},
            {"post_process_enabled", audit_json_bool(options_.performance.post_process_enabled)},
        }),
        AuditPriority::Critical);
}

void Game::finalize_audit(const PerformanceRunReport& report, AuditRunStatus status) {
    if (!audit_) {
        return;
    }

    flush_audit_second_sample(true);
    record_audit_event(
        AuditEventCategory::Session,
        status == AuditRunStatus::Completed ? "session_stop" : "session_abort",
        status == AuditRunStatus::Completed ? AuditSeverity::Info : AuditSeverity::Error,
        audit_json_object({
            {"status", audit_json_string(audit_run_status_name(status))},
            {"frames", audit_json_number(report.summary.frame_count)},
            {"spikes", audit_json_number(report.spike_windows.size())},
            {"events", audit_json_number(report.event_summary.total_events)},
        }),
        AuditPriority::Critical);

    AuditFinalizeContext context {};
    context.status = status;
    context.performance_report = report;
    audit_->finalize(context);
}

void Game::shutdown() {
    music_.shutdown();
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
        record_raw_input_event(event);
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
                record_audit_event(
                    AuditEventCategory::Ui,
                    "window_resized",
                    AuditSeverity::Info,
                    audit_json_object({
                        {"width", audit_json_number(window_width_)},
                        {"height", audit_json_number(window_height_)},
                    }),
                    AuditPriority::Normal);
                if (confirm_dialog_.visible) {
                    clamp_ui_cursor(confirm_dialog_.cursor_x, confirm_dialog_.cursor_y, window_width_, window_height_);
                    refresh_confirm_dialog_hover();
                }
                if (death_screen_visible_) {
                    clamp_ui_cursor(death_screen_.cursor_x, death_screen_.cursor_y, window_width_, window_height_);
                    refresh_death_screen_hover();
                }
                if (save_slot_menu_.visible) {
                    clamp_ui_cursor(save_slot_menu_.cursor_x, save_slot_menu_.cursor_y, window_width_, window_height_);
                    refresh_save_slot_menu_hover();
                }
                if (options_menu_.visible) {
                    clamp_ui_cursor(options_menu_.cursor_x, options_menu_.cursor_y, window_width_, window_height_);
                    refresh_options_menu_hover();
                }
                if (main_menu_.visible) {
                    clamp_ui_cursor(main_menu_.cursor_x, main_menu_.cursor_y, window_width_, window_height_);
                    refresh_main_menu_hover();
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
            if (confirm_dialog_.visible) {
                confirm_dialog_.cursor_x = static_cast<float>(event.motion.x);
                confirm_dialog_.cursor_y = static_cast<float>(event.motion.y);
                refresh_confirm_dialog_hover();
                break;
            }
            if (death_screen_visible_) {
                death_screen_.cursor_x = static_cast<float>(event.motion.x);
                death_screen_.cursor_y = static_cast<float>(event.motion.y);
                refresh_death_screen_hover();
                break;
            }
            if (save_slot_menu_.visible) {
                save_slot_menu_.cursor_x = static_cast<float>(event.motion.x);
                save_slot_menu_.cursor_y = static_cast<float>(event.motion.y);
                refresh_save_slot_menu_hover();
                break;
            }
            if (options_menu_.visible) {
                options_menu_.cursor_x = static_cast<float>(event.motion.x);
                options_menu_.cursor_y = static_cast<float>(event.motion.y);
                refresh_options_menu_hover();
                break;
            }
            if (main_menu_.visible) {
                main_menu_.cursor_x = static_cast<float>(event.motion.x);
                main_menu_.cursor_y = static_cast<float>(event.motion.y);
                refresh_main_menu_hover();
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
            if (confirm_dialog_.visible) {
                confirm_dialog_.cursor_x = static_cast<float>(event.button.x);
                confirm_dialog_.cursor_y = static_cast<float>(event.button.y);
                refresh_confirm_dialog_hover();
                if (event.button.button == SDL_BUTTON_LEFT) {
                    const auto layout = build_confirm_dialog_layout(window_width_, window_height_, confirm_dialog_);
                    const auto choice = confirm_dialog_choice_at(layout, confirm_dialog_.cursor_x, confirm_dialog_.cursor_y);
                    if (choice.has_value()) {
                        activate_confirm_dialog_choice(*choice);
                    }
                }
                break;
            }
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
            if (save_slot_menu_.visible) {
                save_slot_menu_.cursor_x = static_cast<float>(event.button.x);
                save_slot_menu_.cursor_y = static_cast<float>(event.button.y);
                refresh_save_slot_menu_hover();
                if (event.button.button == SDL_BUTTON_LEFT) {
                    const auto layout = build_save_slot_menu_layout(window_width_, window_height_, save_slot_menu_);
                    if (const auto delete_slot_index = save_slot_delete_at(layout, save_slot_menu_.cursor_x, save_slot_menu_.cursor_y);
                        delete_slot_index.has_value()) {
                        set_confirm_dialog_visible(true, ConfirmDialogIntent::DeleteSlot, *delete_slot_index);
                    } else if (const auto card_slot_index = save_slot_card_at(layout, save_slot_menu_.cursor_x, save_slot_menu_.cursor_y);
                        card_slot_index.has_value()) {
                        activate_save_slot_selection(*card_slot_index);
                    } else if (save_slot_back_hovered(layout, save_slot_menu_.cursor_x, save_slot_menu_.cursor_y)) {
                        close_frontend_menu_to_parent();
                    }
                }
                break;
            }
            if (options_menu_.visible) {
                options_menu_.cursor_x = static_cast<float>(event.button.x);
                options_menu_.cursor_y = static_cast<float>(event.button.y);
                refresh_options_menu_hover();
                if (event.button.button == SDL_BUTTON_LEFT) {
                    const auto layout = build_options_menu_layout(window_width_, window_height_, options_menu_);
                    const auto action = options_menu_action_at(layout, options_menu_.cursor_x, options_menu_.cursor_y);
                    if (action.has_value()) {
                        activate_options_menu_action(*action);
                    }
                }
                break;
            }
            if (main_menu_.visible) {
                main_menu_.cursor_x = static_cast<float>(event.button.x);
                main_menu_.cursor_y = static_cast<float>(event.button.y);
                refresh_main_menu_hover();
                if (event.button.button == SDL_BUTTON_LEFT) {
                    const auto layout = build_main_menu_layout(window_width_, window_height_, main_menu_);
                    const auto action = main_menu_action_at(layout, main_menu_.cursor_x, main_menu_.cursor_y);
                    if (action.has_value()) {
                        activate_main_menu_action(*action);
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
                record_audit_event(
                    AuditEventCategory::InputAction,
                    "mouse_capture_request",
                    AuditSeverity::Info,
                    audit_json_object({
                        {"button", audit_json_number(event.button.button)},
                        {"x", audit_json_number(event.button.x)},
                        {"y", audit_json_number(event.button.y)},
                    }),
                    AuditPriority::Normal);
                break;
            }
            if (event.button.button == SDL_BUTTON_LEFT) {
                pending_break_block_ = true;
                pending_primary_attack_ = true;
                record_audit_event(
                    AuditEventCategory::InputAction,
                    "primary_action_pressed",
                    AuditSeverity::Trace,
                    audit_json_object({
                        {"x", audit_json_number(event.button.x)},
                        {"y", audit_json_number(event.button.y)},
                    }),
                    AuditPriority::Normal);
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                pending_place_block_ = true;
                record_audit_event(
                    AuditEventCategory::InputAction,
                    "secondary_action_pressed",
                    AuditSeverity::Trace,
                    audit_json_object({
                        {"x", audit_json_number(event.button.x)},
                        {"y", audit_json_number(event.button.y)},
                    }),
                    AuditPriority::Normal);
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                pending_break_block_ = false;
                pending_primary_attack_ = false;
                player_.cancel_block_breaking();
                if (mouse_captured_ && !front_end_visible() && !paused_ && !inventory_visible_ && !death_screen_visible_) {
                    record_audit_event(
                        AuditEventCategory::InputAction,
                        "primary_action_released",
                        AuditSeverity::Trace,
                        audit_json_object({
                            {"x", audit_json_number(event.button.x)},
                            {"y", audit_json_number(event.button.y)},
                        }),
                        AuditPriority::Normal);
                }
            }
            break;
        case SDL_MOUSEWHEEL: {
            if (confirm_dialog_.visible || death_screen_visible_ || paused_ || inventory_visible_ ||
                save_slot_menu_.visible || options_menu_.visible || main_menu_.visible) {
                break;
            }
            auto scroll_y = event.wheel.y;
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                scroll_y = -scroll_y;
            }
            if (scroll_y != 0) {
                cycle_hotbar_selection(-scroll_y);
                record_audit_event(
                    AuditEventCategory::InputAction,
                    "hotbar_cycle",
                    AuditSeverity::Info,
                    audit_json_object({
                        {"delta", audit_json_number(-scroll_y)},
                        {"selected_index", audit_json_number(hotbar_.selected_index)},
                    }),
                    AuditPriority::Normal);
            }
            break;
        }
        case SDL_KEYDOWN:
            if (event.key.repeat != 0) {
                break;
            }

            if (confirm_dialog_.visible) {
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    set_confirm_dialog_visible(false);
                    break;
                case SDLK_LEFT:
                case SDLK_RIGHT:
                case SDLK_a:
                case SDLK_d:
                case SDLK_TAB:
                    confirm_dialog_.selected_choice = next_confirm_dialog_choice(
                        confirm_dialog_.selected_choice,
                        (event.key.keysym.mod & KMOD_SHIFT) != 0 ? -1 : 1);
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    activate_confirm_dialog_choice(confirm_dialog_.selected_choice);
                    break;
                default:
                    break;
                }
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

            if (save_slot_menu_.visible) {
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    close_frontend_menu_to_parent();
                    break;
                case SDLK_UP:
                case SDLK_w:
                case SDLK_LEFT:
                case SDLK_a:
                    save_slot_menu_.selected_index = next_save_slot_menu_index(save_slot_menu_, -1);
                    break;
                case SDLK_DOWN:
                case SDLK_s:
                case SDLK_RIGHT:
                case SDLK_d:
                case SDLK_TAB:
                    save_slot_menu_.selected_index = next_save_slot_menu_index(
                        save_slot_menu_,
                        (event.key.keysym.mod & KMOD_SHIFT) != 0 ? -1 : 1);
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    if (save_slot_menu_.selected_index >= kSaveSlotCount) {
                        close_frontend_menu_to_parent();
                    } else {
                        activate_save_slot_selection(save_slot_menu_.selected_index);
                    }
                    break;
                default:
                    break;
                }
                break;
            }

            if (options_menu_.visible) {
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    close_frontend_menu_to_parent();
                    break;
                case SDLK_UP:
                case SDLK_w:
                    options_menu_.selected_action = next_options_menu_action(options_menu_.selected_action, -1);
                    break;
                case SDLK_DOWN:
                case SDLK_s:
                case SDLK_TAB:
                    options_menu_.selected_action = next_options_menu_action(
                        options_menu_.selected_action,
                        (event.key.keysym.mod & KMOD_SHIFT) != 0 ? -1 : 1);
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    activate_options_menu_action(options_menu_.selected_action);
                    break;
                default:
                    break;
                }
                break;
            }

            if (main_menu_.visible) {
                switch (event.key.keysym.sym) {
                case SDLK_UP:
                case SDLK_w:
                    main_menu_.selected_action = next_main_menu_action(main_menu_.selected_action, -1);
                    break;
                case SDLK_DOWN:
                case SDLK_s:
                case SDLK_TAB:
                    main_menu_.selected_action = next_main_menu_action(
                        main_menu_.selected_action,
                        (event.key.keysym.mod & KMOD_SHIFT) != 0 ? -1 : 1);
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    activate_main_menu_action(main_menu_.selected_action);
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
                record_audit_event(
                    AuditEventCategory::InputAction,
                    "pause_toggle",
                    AuditSeverity::Info,
                    audit_json_object({
                        {"paused", audit_json_bool(paused_)},
                    }),
                    AuditPriority::Normal);
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
                record_audit_event(
                    AuditEventCategory::InputAction,
                    "inventory_open",
                    AuditSeverity::Info,
                    audit_json_object({
                        {"visible", audit_json_bool(inventory_visible_)},
                    }),
                    AuditPriority::Normal);
            } else if (is_drop_action_key(event.key.keysym)) {
                drop_selected_hotbar_items((event.key.keysym.mod & KMOD_CTRL) != 0);
                record_audit_event(
                    AuditEventCategory::InputAction,
                    "hotbar_drop",
                    AuditSeverity::Info,
                    audit_json_object({
                        {"full_stack", audit_json_bool((event.key.keysym.mod & KMOD_CTRL) != 0)},
                    }),
                    AuditPriority::Normal);
            } else if (is_flight_action_key(event.key.keysym)) {
                const auto flight_unlocked = progression_.has_flight_power();
                // Je bloque le vol ici pour que la touche F ne contourne jamais le niveau 100.
                if (flight_unlocked) {
                    pending_toggle_fly_ = true;
                    queue_gameplay_announcement(
                        player_.state().fly_mode ? "VOL COUPE" : "VOL ACTIVE",
                        player_.state().fly_mode ? "RETOUR AU SOL" : "TOUCHE F POUR DESCENDRE",
                        2.4F);
                } else {
                    pending_toggle_fly_ = false;
                    queue_gameplay_announcement("VOL", "NIVEAU 100 REQUIS", 2.6F);
                }
                record_audit_event(
                    AuditEventCategory::InputAction,
                    "fly_toggle_request",
                    AuditSeverity::Info,
                    audit_json_object({
                        {"unlocked", audit_json_bool(flight_unlocked)},
                        {"requested_active", audit_json_bool(flight_unlocked && !player_.state().fly_mode)},
                    }),
                    AuditPriority::Normal);
            } else if (is_super_vision_action_key(event.key.keysym)) {
                toggle_super_vision();
                record_audit_event(
                    AuditEventCategory::InputAction,
                    "super_vision_toggle_request",
                    AuditSeverity::Info,
                    audit_json_object({
                        {"unlocked", audit_json_bool(progression_.has_super_vision_power())},
                        {"active", audit_json_bool(super_vision_active_)},
                    }),
                    AuditPriority::Normal);
            } else {
                select_hotbar_slot_from_keycode(event.key.keysym.sym);
                if (hotbar_number_from_keycode(event.key.keysym.sym) != 0) {
                    record_audit_event(
                        AuditEventCategory::InputAction,
                        "hotbar_select",
                        AuditSeverity::Info,
                        audit_json_object({
                            {"selected_index", audit_json_number(hotbar_.selected_index)},
                        }),
                        AuditPriority::Normal);
                }
            }
            break;
        default:
            break;
        }
    }
}

void Game::update_simulation(float dt, FramePerformanceStats& frame_stats) {
    if (!options_.smoke_test && front_end_visible()) {
        sync_menu_preview_environment();
        update_menu_preview_camera(dt);
        const auto environment_state = environment_.current_state();
        creatures_.update(dt, world_, preview_player_.position(), environment_state, environment_.current_creature_cycle());
        if (const auto creature_stats = creatures_.consume_audit_stats();
            creature_stats.spawned != 0 || creature_stats.despawned != 0 || creature_stats.attacks != 0) {
            audit_second_accumulator_.creature_spawns += creature_stats.spawned;
            audit_second_accumulator_.creature_despawns += creature_stats.despawned;
            audit_second_accumulator_.creature_attacks += creature_stats.attacks;
            audit_second_accumulator_.active_creatures_max =
                std::max(audit_second_accumulator_.active_creatures_max, creature_stats.active_creatures);
            record_audit_event(
                AuditEventCategory::Creatures,
                "creature_activity",
                AuditSeverity::Info,
                audit_json_object({
                    {"spawned", audit_json_number(creature_stats.spawned)},
                    {"despawned", audit_json_number(creature_stats.despawned)},
                    {"attacks", audit_json_number(creature_stats.attacks)},
                    {"active_creatures", audit_json_number(creature_stats.active_creatures)},
                }),
                AuditPriority::Normal);
        }
        (void)frame_stats;
        return;
    }

    if (!options_.smoke_test && (death_screen_visible_ || paused_)) {
        (void)dt;
        (void)frame_stats;
        return;
    }

    update_gameplay_announcements(dt);

    environment_.set_frozen(options_.freeze_time || options_.smoke_test);
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

        if (!inventory_visible_ && pending_place_block_) {
            player_.trigger_secondary_action();
        }

        player_.update(input, dt, world_);

        if (!inventory_visible_ && pending_primary_attack_) {
            pending_primary_attack_ = false;
            if (const auto weapon = inventory_active_weapon_stats(inventory_menu_, hotbar_); weapon.has_value()) {
                player_.trigger_primary_action();
                music_.play_sfx(GameSfxKind::SwordSwing, 0.72F);
                player_.cancel_block_breaking();
                pending_break_block_ = false;

                auto weapon_range = weapon->range;
                const auto block_hit = player_.current_target(world_, weapon_range);
                if (block_hit.hit) {
                    weapon_range = std::clamp(block_hit.distance, 0.0F, weapon_range);
                }

                const auto hit_result = creatures_.try_damage_from_player(
                    player_.eye_position(),
                    player_.look_direction(),
                    weapon_range,
                    weapon->damage * progression_.attack_damage_multiplier());
                if (hit_result.hit) {
                    music_.play_sfx(hit_result.killed ? GameSfxKind::CreatureDeath : GameSfxKind::CreatureHit,
                                    hit_result.killed ? 0.88F : 0.72F);
                    if (hit_result.killed) {
                        grant_player_experience(
                            creature_kill_experience(
                                hit_result.species,
                                hit_result.position,
                                static_cast<std::uint32_t>(world_.seed()) ^ static_cast<std::uint32_t>(rendered_frames_)),
                            block_coord_from_position(hit_result.position),
                            "creature_kill");
                    }
                    record_audit_event(
                        AuditEventCategory::Creatures,
                        hit_result.killed ? "creature_killed" : "creature_damaged",
                        hit_result.killed ? AuditSeverity::Warning : AuditSeverity::Info,
                        audit_json_object({
                            {"species", audit_json_number(static_cast<int>(hit_result.species))},
                            {"damage", audit_json_number(hit_result.damage)},
                            {"remaining_health", audit_json_number(hit_result.remaining_health)},
                        }),
                        hit_result.killed ? AuditPriority::High : AuditPriority::Normal);
                }
            }
        }

        if (!inventory_visible_ && pending_break_block_) {
            if (const auto broken_block = player_.update_block_breaking(world_, dt, true); broken_block.has_value()) {
                record_performance_event(
                    PerformanceEventKind::BlockBreak,
                    broken_block->block,
                    inventory_item_label(broken_block->block_id));
                grant_player_experience(
                    block_break_experience(broken_block->block_id),
                    broken_block->block,
                    "block_break");
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
        } else {
            player_.cancel_block_breaking();
        }
        if (!inventory_visible_ && pending_place_block_) {
            auto& selected_slot = hotbar_.slots[hotbar_.selected_index];
            if (inventory_slot_has_item(selected_slot)) {
                const auto placed_block = player_.try_place_block(world_);
                if (placed_block.has_value()) {
                    record_performance_event(
                        PerformanceEventKind::BlockPlace,
                        placed_block->block,
                        inventory_item_label(placed_block->block_id));
                    (void)inventory_take_from_slot(selected_slot, 1);
                    normalize_inventory_state(inventory_menu_, hotbar_);
                    sync_selected_hotbar_slot();
                }
            }
            pending_place_block_ = false;
        }
        if (inventory_visible_) {
            pending_break_block_ = false;
            pending_primary_attack_ = false;
            pending_place_block_ = false;
            player_.cancel_block_breaking();
        }

        item_drops_.update(dt, world_, player_.position(), inventory_menu_, hotbar_);
        if (const auto item_stats = item_drops_.consume_audit_stats();
            item_stats.spawned != 0 || item_stats.merged != 0 || item_stats.picked_up != 0 || item_stats.expired != 0) {
            audit_second_accumulator_.item_spawns += item_stats.spawned;
            audit_second_accumulator_.item_merges += item_stats.merged;
            audit_second_accumulator_.item_pickups += item_stats.picked_up;
            audit_second_accumulator_.item_expired += item_stats.expired;
            audit_second_accumulator_.active_item_drops_max =
                std::max(audit_second_accumulator_.active_item_drops_max, item_stats.active_drops);
            record_audit_event(
                AuditEventCategory::Items,
                "item_drop_activity",
                AuditSeverity::Info,
                audit_json_object({
                    {"spawned", audit_json_number(item_stats.spawned)},
                    {"merged", audit_json_number(item_stats.merged)},
                    {"picked_up", audit_json_number(item_stats.picked_up)},
                    {"expired", audit_json_number(item_stats.expired)},
                    {"active_drops", audit_json_number(item_stats.active_drops)},
                    {"rejected_spawns", audit_json_number(item_stats.rejected_spawns)},
                }),
                AuditPriority::Normal);
        }
        sync_selected_hotbar_slot();
    }

    creatures_.update(dt, world_, player_.position(), environment_state, creature_cycle);
    if (const auto creature_stats = creatures_.consume_audit_stats();
        creature_stats.spawned != 0 || creature_stats.despawned != 0 || creature_stats.attacks != 0) {
        audit_second_accumulator_.creature_spawns += creature_stats.spawned;
        audit_second_accumulator_.creature_despawns += creature_stats.despawned;
        audit_second_accumulator_.creature_attacks += creature_stats.attacks;
        audit_second_accumulator_.active_creatures_max =
            std::max(audit_second_accumulator_.active_creatures_max, creature_stats.active_creatures);
        record_audit_event(
            AuditEventCategory::Creatures,
            "creature_activity",
            AuditSeverity::Info,
            audit_json_object({
                {"spawned", audit_json_number(creature_stats.spawned)},
                {"despawned", audit_json_number(creature_stats.despawned)},
                {"attacks", audit_json_number(creature_stats.attacks)},
                {"active_creatures", audit_json_number(creature_stats.active_creatures)},
            }),
            AuditPriority::Normal);
    }

    if (!options_.smoke_test) {
        for (const auto& attack : creatures_.recent_attacks()) {
            player_.apply_external_damage(attack.damage, PlayerDeathCause::Zombie);
        }

        if (!creatures_.recent_attacks().empty()) {
            music_.play_sfx(GameSfxKind::CreatureAttack, 0.55F);
            record_audit_event(
                AuditEventCategory::Creatures,
                "creature_attack",
                AuditSeverity::Warning,
                audit_json_object({
                    {"count", audit_json_number(creatures_.recent_attacks().size())},
                }),
                AuditPriority::High);
        }

        if (player_.is_dead()) {
            record_audit_event(
                AuditEventCategory::Player,
                "player_death",
                AuditSeverity::Error,
                audit_json_object({
                    {"cause", audit_json_number(static_cast<int>(player_.state().death_cause))},
                }),
                AuditPriority::Critical);
            set_death_screen_visible(true, player_.state().death_cause);
            return;
        }

        if (has_active_session_) {
            mark_session_dirty();
        }
    }
}

void Game::update_world_pipeline(FramePerformanceStats& frame_stats) {
    using clock = std::chrono::steady_clock;

    if (!options_.smoke_test && (death_screen_visible_ || paused_) && !front_end_visible()) {
        (void)frame_stats;
        return;
    }

    const auto stream_start = clock::now();
    const auto stream_stats = world_.update_streaming(streaming_focus_position());
    frame_stats.streaming_ms +=
        std::chrono::duration<double, std::milli>(clock::now() - stream_start).count();
    frame_stats.stream_chunk_changes += stream_stats.chunk_changed ? 1U : 0U;
    frame_stats.generation_enqueued += stream_stats.generation_enqueued;
    frame_stats.generation_pruned += stream_stats.generation_pruned;
    frame_stats.unloaded_chunks += stream_stats.unloaded_chunks;
    if (stream_stats.chunk_changed || stream_stats.generation_enqueued != 0 || stream_stats.generation_pruned != 0 ||
        stream_stats.unloaded_chunks != 0) {
        record_audit_event(
            AuditEventCategory::World,
            "stream_update",
            AuditSeverity::Info,
            audit_json_object({
                {"chunk_changed", audit_json_bool(stream_stats.chunk_changed)},
                {"generation_enqueued", audit_json_number(stream_stats.generation_enqueued)},
                {"generation_pruned", audit_json_number(stream_stats.generation_pruned)},
                {"unloaded_chunks", audit_json_number(stream_stats.unloaded_chunks)},
            }),
            AuditPriority::High);
    }

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
    if (world_stats.generated_chunks != 0 || world_stats.meshed_chunks != 0 || world_stats.light_nodes_processed != 0 ||
        world_stats.lighting_jobs_completed != 0 || world_stats.pending_generation != 0 || world_stats.pending_mesh != 0 ||
        world_stats.pending_lighting != 0) {
        record_audit_event(
            AuditEventCategory::World,
            "world_work",
            AuditSeverity::Info,
            audit_json_object({
                {"generated_chunks", audit_json_number(world_stats.generated_chunks)},
                {"meshed_chunks", audit_json_number(world_stats.meshed_chunks)},
                {"light_nodes_processed", audit_json_number(world_stats.light_nodes_processed)},
                {"lighting_jobs_completed", audit_json_number(world_stats.lighting_jobs_completed)},
                {"pending_generation", audit_json_number(world_stats.pending_generation)},
                {"pending_mesh", audit_json_number(world_stats.pending_mesh)},
                {"pending_lighting", audit_json_number(world_stats.pending_lighting)},
            }),
            AuditPriority::High);
    }

    if (options_.smoke_test) {
        validate_smoke_frame(options_.performance.world_budget(), world_stats);
    }
}

void Game::set_mouse_capture(bool captured) {
    const auto changed = mouse_captured_ != captured;
    mouse_captured_ = captured;
    pending_look_x_ = 0.0F;
    pending_look_y_ = 0.0F;
    if (!captured) {
        pending_break_block_ = false;
        pending_primary_attack_ = false;
        player_.cancel_block_breaking();
    }
    SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
    SDL_ShowCursor(captured ? SDL_DISABLE : SDL_ENABLE);
    if (changed) {
        record_audit_event(
            AuditEventCategory::Ui,
            "mouse_capture_changed",
            AuditSeverity::Info,
            audit_json_object({
                {"captured", audit_json_bool(captured)},
            }),
            AuditPriority::High);
    }
}

void Game::set_death_screen_visible(bool visible, PlayerDeathCause cause) {
    if (options_.smoke_test) {
        return;
    }
    if (!has_active_session_) {
        return;
    }
    if (death_screen_visible_ == visible && (!visible || death_screen_.cause == cause)) {
        return;
    }

    death_screen_visible_ = visible;
    death_screen_.visible = visible;
    pending_toggle_fly_ = false;
    pending_break_block_ = false;
    pending_primary_attack_ = false;
    pending_place_block_ = false;
    player_.cancel_block_breaking();

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
        record_audit_event(
            AuditEventCategory::Ui,
            "death_screen_opened",
            AuditSeverity::Warning,
            audit_json_object({
                {"cause", audit_json_number(static_cast<int>(cause))},
            }),
            AuditPriority::High);
        return;
    }

    death_screen_.cause = PlayerDeathCause::None;
    if (!paused_ && !inventory_visible_) {
        set_mouse_capture(true);
    }
    record_audit_event(
        AuditEventCategory::Ui,
        "death_screen_closed",
        AuditSeverity::Info,
        audit_json_object({}),
        AuditPriority::High);
}

void Game::set_paused(bool paused) {
    if (options_.smoke_test) {
        return;
    }
    if (death_screen_visible_ || front_end_visible() || !has_active_session_) {
        return;
    }

    paused_ = paused;
    pause_menu_.visible = paused;
    pause_menu_.selected_action = PauseMenuAction::Resume;
    pending_toggle_fly_ = false;
    pending_break_block_ = false;
    pending_primary_attack_ = false;
    pending_place_block_ = false;
    player_.cancel_block_breaking();

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
    record_audit_event(
        AuditEventCategory::Ui,
        paused ? "pause_opened" : "pause_closed",
        AuditSeverity::Info,
        audit_json_object({
            {"paused", audit_json_bool(paused)},
        }),
        AuditPriority::High);
}

void Game::set_inventory_visible(bool visible) {
    if (options_.smoke_test) {
        return;
    }
    if (visible && (paused_ || death_screen_visible_ || front_end_visible() || !has_active_session_)) {
        return;
    }
    if (inventory_visible_ == visible) {
        return;
    }

    inventory_visible_ = visible;
    inventory_menu_.visible = visible;
    pending_toggle_fly_ = false;
    pending_break_block_ = false;
    pending_primary_attack_ = false;
    pending_place_block_ = false;
    player_.cancel_block_breaking();

    if (inventory_visible_) {
        set_mouse_capture(false);
        center_ui_cursor(window_, window_width_, window_height_, inventory_menu_.cursor_x, inventory_menu_.cursor_y);
        refresh_inventory_hover();
        record_audit_event(
            AuditEventCategory::Ui,
            "inventory_opened",
            AuditSeverity::Info,
            audit_json_object({}),
            AuditPriority::High);
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
    record_audit_event(
        AuditEventCategory::Ui,
        "inventory_closed",
        AuditSeverity::Info,
        audit_json_object({}),
        AuditPriority::High);
}

void Game::set_confirm_dialog_visible(bool visible,
                                      ConfirmDialogIntent intent,
                                      std::optional<std::size_t> slot_index) {
    if (options_.smoke_test) {
        return;
    }

    confirm_dialog_.visible = visible;
    confirm_dialog_.intent = visible ? intent : ConfirmDialogIntent::None;
    confirm_dialog_.selected_choice = ConfirmDialogChoice::Confirm;
    pending_confirm_slot_ = visible ? slot_index : std::nullopt;

    if (confirm_dialog_.visible) {
        set_mouse_capture(false);
        center_ui_cursor(window_, window_width_, window_height_, confirm_dialog_.cursor_x, confirm_dialog_.cursor_y);
        refresh_confirm_dialog_hover();
    } else if (!death_screen_visible_ && !inventory_visible_ && !paused_ && !front_end_visible()) {
        set_mouse_capture(true);
    }
    record_audit_event(
        AuditEventCategory::Ui,
        visible ? "confirm_dialog_opened" : "confirm_dialog_closed",
        visible ? AuditSeverity::Warning : AuditSeverity::Info,
        audit_json_object({
            {"intent", audit_json_number(static_cast<int>(intent))},
            {"has_slot", audit_json_bool(slot_index.has_value())},
        }),
        AuditPriority::High);
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
    case PauseMenuAction::Save:
        open_save_slot_menu(SaveSlotMenuMode::SaveGame, SaveSlotMenuParent::PauseMenu);
        break;
    case PauseMenuAction::Load:
        open_save_slot_menu(SaveSlotMenuMode::LoadGame, SaveSlotMenuParent::PauseMenu);
        break;
    case PauseMenuAction::Options:
        open_options_menu(OptionsMenuParent::PauseMenu);
        break;
    case PauseMenuAction::ReturnToMainMenu:
        request_return_to_main_menu();
        break;
    default:
        break;
    }
}

void Game::activate_main_menu_action(MainMenuAction action) {
    switch (action) {
    case MainMenuAction::Play:
        open_save_slot_menu(SaveSlotMenuMode::NewGame, SaveSlotMenuParent::MainMenu);
        break;
    case MainMenuAction::Load:
        open_save_slot_menu(SaveSlotMenuMode::LoadGame, SaveSlotMenuParent::MainMenu);
        break;
    case MainMenuAction::Options:
        open_options_menu(OptionsMenuParent::MainMenu);
        break;
    default:
        break;
    }
}

void Game::activate_save_slot_selection(std::size_t slot_index) {
    switch (resolve_save_slot_primary_action(save_slot_menu_, slot_index, session_dirty_)) {
    case SaveSlotPrimaryAction::StartNewGame:
        start_new_game_in_slot(slot_index);
        break;
    case SaveSlotPrimaryAction::LoadGame:
        (void)load_game_from_slot(slot_index);
        break;
    case SaveSlotPrimaryAction::SaveGame:
        save_game_to_slot(slot_index);
        close_frontend_menu_to_parent();
        break;
    case SaveSlotPrimaryAction::ConfirmOverwrite:
        set_confirm_dialog_visible(true, ConfirmDialogIntent::OverwriteSlot, slot_index);
        break;
    case SaveSlotPrimaryAction::ConfirmLoad:
        set_confirm_dialog_visible(true, ConfirmDialogIntent::LoadSlot, slot_index);
        break;
    case SaveSlotPrimaryAction::None:
    default:
        break;
    }
}

void Game::activate_options_menu_action(OptionsMenuAction action) {
    switch (action) {
    case OptionsMenuAction::ToggleShadows:
        runtime_shadows_enabled_ = !runtime_shadows_enabled_;
        options_menu_.shadows_enabled = runtime_shadows_enabled_;
        apply_renderer_options();
        break;
    case OptionsMenuAction::TogglePostProcess:
        runtime_post_process_enabled_ = !runtime_post_process_enabled_;
        options_menu_.post_process_enabled = runtime_post_process_enabled_;
        apply_renderer_options();
        break;
    case OptionsMenuAction::Back:
        close_frontend_menu_to_parent();
        break;
    default:
        break;
    }
}

void Game::activate_confirm_dialog_choice(ConfirmDialogChoice choice) {
    if (choice == ConfirmDialogChoice::Cancel) {
        set_confirm_dialog_visible(false);
        return;
    }

    const auto intent = confirm_dialog_.intent;
    const auto slot_index = pending_confirm_slot_;
    set_confirm_dialog_visible(false);

    switch (intent) {
    case ConfirmDialogIntent::OverwriteSlot:
        if (!slot_index.has_value()) {
            return;
        }
        if (save_slot_menu_.mode == SaveSlotMenuMode::NewGame) {
            start_new_game_in_slot(*slot_index);
        } else if (save_slot_menu_.mode == SaveSlotMenuMode::SaveGame) {
            save_game_to_slot(*slot_index);
            close_frontend_menu_to_parent();
        }
        break;
    case ConfirmDialogIntent::LoadSlot:
        if (slot_index.has_value()) {
            (void)load_game_from_slot(*slot_index);
        }
        break;
    case ConfirmDialogIntent::DeleteSlot:
        if (slot_index.has_value()) {
            (void)remove_save_slot(save_root_directory_, *slot_index);
            refresh_save_slots();
            if (!save_slot_menu_slot_enabled(save_slot_menu_, save_slot_menu_.selected_index)) {
                save_slot_menu_.selected_index = first_save_slot_menu_index(save_slot_menu_);
            }
            refresh_save_slot_menu_hover();
        }
        break;
    case ConfirmDialogIntent::ReturnToMainMenu:
        open_main_menu(true);
        break;
    case ConfirmDialogIntent::None:
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

void Game::refresh_main_menu_hover() noexcept {
    if (!main_menu_.visible) {
        return;
    }

    const auto layout = build_main_menu_layout(window_width_, window_height_, main_menu_);
    const auto hovered_action = main_menu_action_at(layout, main_menu_.cursor_x, main_menu_.cursor_y);
    if (hovered_action.has_value()) {
        main_menu_.selected_action = *hovered_action;
    }
}

void Game::refresh_save_slot_menu_hover() noexcept {
    if (!save_slot_menu_.visible) {
        return;
    }

    const auto layout = build_save_slot_menu_layout(window_width_, window_height_, save_slot_menu_);
    if (const auto delete_slot_index = save_slot_delete_at(layout, save_slot_menu_.cursor_x, save_slot_menu_.cursor_y);
        delete_slot_index.has_value()) {
        save_slot_menu_.selected_index = *delete_slot_index;
    } else if (const auto card_slot_index = save_slot_card_at(layout, save_slot_menu_.cursor_x, save_slot_menu_.cursor_y);
        card_slot_index.has_value()) {
        save_slot_menu_.selected_index = *card_slot_index;
    } else if (save_slot_back_hovered(layout, save_slot_menu_.cursor_x, save_slot_menu_.cursor_y)) {
        save_slot_menu_.selected_index = kSaveSlotCount;
    }
}

void Game::refresh_options_menu_hover() noexcept {
    if (!options_menu_.visible) {
        return;
    }

    const auto layout = build_options_menu_layout(window_width_, window_height_, options_menu_);
    const auto hovered_action = options_menu_action_at(layout, options_menu_.cursor_x, options_menu_.cursor_y);
    if (hovered_action.has_value()) {
        options_menu_.selected_action = *hovered_action;
    }
}

void Game::refresh_confirm_dialog_hover() noexcept {
    if (!confirm_dialog_.visible) {
        return;
    }

    const auto layout = build_confirm_dialog_layout(window_width_, window_height_, confirm_dialog_);
    const auto hovered_choice = confirm_dialog_choice_at(layout, confirm_dialog_.cursor_x, confirm_dialog_.cursor_y);
    if (hovered_choice.has_value()) {
        confirm_dialog_.selected_choice = *hovered_choice;
    }
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
    normalize_inventory_state(inventory_menu_, hotbar_);
    sync_selected_hotbar_slot();
}

void Game::spawn_dropped_stack(const HotbarSlot& stack, const glm::vec3& origin, const glm::vec3& initial_velocity) noexcept {
    item_drops_.spawn_drop(stack, origin, initial_velocity);
}

void Game::grant_player_experience(std::uint64_t base_experience, const BlockCoord& activity_block, std::string_view source) {
    if (base_experience == 0ULL || progression_.is_max_level()) {
        return;
    }

    const auto surface_y = world_.loaded_surface_height(activity_block.x, activity_block.z);
    const auto multiplier = experience_multiplier_for_activity(environment_.current_creature_cycle(), surface_y, activity_block.y);
    const auto awarded_experience = multiply_experience(base_experience, multiplier);
    const auto previous_level = progression_.level();
    const auto result = progression_.add_experience(awarded_experience);
    if (result.awarded_experience == 0ULL) {
        return;
    }

    if (progression_.level() != previous_level) {
        sync_selected_hotbar_slot();
        queue_level_up_announcements(previous_level, progression_.level());
    }

    record_audit_event(
        AuditEventCategory::Player,
        result.levels_gained > 0U ? "player_level_up" : "experience_gain",
        result.levels_gained > 0U ? AuditSeverity::Warning : AuditSeverity::Info,
        audit_json_object({
            {"source", audit_json_string(source)},
            {"base_experience", audit_json_number(base_experience)},
            {"multiplier", audit_json_number(multiplier)},
            {"awarded_experience", audit_json_number(result.awarded_experience)},
            {"levels_gained", audit_json_number(result.levels_gained)},
            {"level", audit_json_number(progression_.level())},
            {"experience", audit_json_number(progression_.experience())},
            {"experience_for_next_level", audit_json_number(progression_.experience_for_next_level())},
        }),
        result.levels_gained > 0U ? AuditPriority::High : AuditPriority::Normal);
}

void Game::toggle_super_vision() {
    if (!progression_.has_super_vision_power()) {
        super_vision_active_ = false;
        queue_gameplay_announcement("SUPER VISION", "NIVEAU 30 REQUIS", 2.6F);
        return;
    }

    super_vision_active_ = !super_vision_active_;
    if (super_vision_active_) {
        queue_gameplay_announcement("SUPER VISION ACTIVE", "CREATURES LUMINEUSES DANS LE NOIR", 3.0F);
    } else {
        queue_gameplay_announcement("SUPER VISION COUPEE", "VISION NORMALE RESTAUREE", 2.4F);
    }
}

void Game::queue_gameplay_announcement(std::string title, std::string detail, float duration_seconds) {
    if (title.empty() && detail.empty()) {
        return;
    }

    GameplayAnnouncement announcement {};
    announcement.title = std::move(title);
    announcement.detail = std::move(detail);
    announcement.duration_seconds = std::clamp(duration_seconds, 1.0F, 8.0F);
    if (gameplay_announcements_.size() >= kMaxGameplayAnnouncementQueue) {
        gameplay_announcements_.pop_back();
    }
    gameplay_announcements_.push_back(std::move(announcement));
}

void Game::queue_level_up_announcements(std::uint32_t previous_level, std::uint32_t current_level) {
    if (current_level <= previous_level) {
        return;
    }

    const auto bonus_percent = static_cast<int>(std::lround(player_progression_bonus_percent(current_level)));
    queue_gameplay_announcement(
        std::string("NIVEAU ") + std::to_string(current_level),
        std::string("BONUS +") + std::to_string(bonus_percent) + "% DEGATS DEF VIT MINAGE APNEE CHUTE",
        3.35F);

    if (!player_has_super_vision_power(previous_level) && player_has_super_vision_power(current_level)) {
        queue_gameplay_announcement("SUPER VISION DEBLOQUEE", "TOUCHE V POUR VOIR DANS LE NOIR", 4.2F);
    }
    if (!player_has_flight_power(previous_level) && player_has_flight_power(current_level)) {
        queue_gameplay_announcement("VOL DEBLOQUE", "TOUCHE F POUR VOLER", 4.2F);
    }
}

void Game::update_gameplay_announcements(float dt) noexcept {
    if (dt <= 0.0F || !std::isfinite(dt) || gameplay_announcements_.empty()) {
        return;
    }

    gameplay_announcements_.front().elapsed_seconds += dt;
    while (!gameplay_announcements_.empty() &&
           gameplay_announcements_.front().elapsed_seconds >= gameplay_announcements_.front().duration_seconds) {
        gameplay_announcements_.pop_front();
    }
}

auto Game::current_gameplay_announcement_view() const noexcept -> GameplayHudAnnouncementView {
    if (gameplay_announcements_.empty()) {
        return {};
    }

    const auto& announcement = gameplay_announcements_.front();
    const auto duration = std::max(announcement.duration_seconds, 0.001F);
    return {
        announcement.title,
        announcement.detail,
        std::clamp(announcement.elapsed_seconds / duration, 0.0F, 1.0F),
        true,
    };
}

void Game::sync_selected_hotbar_slot() noexcept {
    if (!progression_.has_super_vision_power()) {
        super_vision_active_ = false;
    }
    if (!progression_.has_flight_power()) {
        // Je coupe le vol si une ancienne sauvegarde le contient avant le niveau 100.
        pending_toggle_fly_ = false;
        player_.set_fly_mode_enabled(false);
    }
    player_.set_selected_block(selected_hotbar_block(hotbar_));
    player_.set_damage_resistance_percent(
        inventory_equipment_resistance_percent(inventory_menu_) + progression_.damage_resistance_percent());
    player_.set_apnea_resistance_percent(progression_.apnea_resistance_percent());
    player_.set_fall_safety_multiplier(progression_.fall_safety_multiplier());
    player_.set_movement_speed_multiplier(progression_.movement_speed_multiplier());
    player_.set_block_break_speed_multiplier(progression_.block_break_speed_multiplier());
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
    if (starting_village_enabled_ && !starting_village_.buildings.empty()) {
        return starting_village_.player_spawn;
    }

    constexpr int kSpawnSearchRadius = 12;

    for (int radius = 0; radius <= kSpawnSearchRadius; ++radius) {
        for (int z = -radius; z <= radius; ++z) {
            for (int x = -radius; x <= radius; ++x) {
                if (radius > 0 && std::abs(x) != radius && std::abs(z) != radius) {
                    continue;
                }

                const auto surface_y = world_.surface_height(x, z);
                if (world_.has_water(x, surface_y + 1, z)) {
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
    record_audit_event(
        AuditEventCategory::Player,
        "respawn",
        AuditSeverity::Info,
        audit_json_object({
            {"x", audit_json_number(spawn_position_.x)},
            {"y", audit_json_number(spawn_position_.y)},
            {"z", audit_json_number(spawn_position_.z)},
        }),
        AuditPriority::High);
}

auto Game::active_ui_screen() const noexcept -> UiScreen {
    if (death_screen_visible_) {
        return UiScreen::Death;
    }
    if (save_slot_menu_.visible) {
        return UiScreen::SaveSlots;
    }
    if (options_menu_.visible) {
        return UiScreen::Options;
    }
    if (main_menu_.visible) {
        return UiScreen::MainMenu;
    }
    if (inventory_visible_) {
        return UiScreen::Inventory;
    }
    if (paused_) {
        return UiScreen::Pause;
    }
    return UiScreen::Gameplay;
}

auto Game::front_end_visible() const noexcept -> bool {
    return main_menu_.visible ||
           (save_slot_menu_.visible && save_slot_menu_.parent == SaveSlotMenuParent::MainMenu) ||
           (options_menu_.visible && options_menu_.parent == OptionsMenuParent::MainMenu);
}

auto Game::gameplay_interaction_blocked() const noexcept -> bool {
    return death_screen_visible_ || paused_ || confirm_dialog_.visible || front_end_visible();
}

auto Game::render_player() const noexcept -> const PlayerController& {
    return front_end_visible() ? preview_player_ : player_;
}

auto Game::streaming_focus_position() const noexcept -> glm::vec3 {
    return front_end_visible() ? preview_player_.position() : player_.position();
}

auto Game::current_renderer_options() const noexcept -> RendererOptions {
    RendererOptions renderer_options {};
    renderer_options.shadows_enabled = runtime_shadows_enabled_;
    renderer_options.shadow_map_size = options_.performance.shadow_map_size;
    renderer_options.post_process_enabled = runtime_post_process_enabled_;
    return renderer_options;
}

auto Game::resolve_save_root_directory() const -> std::filesystem::path {
    if (char* pref_path = SDL_GetPrefPath("ValCraft", "ValCraft"); pref_path != nullptr) {
        std::filesystem::path root(pref_path);
        SDL_free(pref_path);
        return root;
    }

    return std::filesystem::current_path() / "saves";
}

auto Game::make_world_snapshot() const -> SaveGameSnapshot {
    SaveGameSnapshot snapshot {};
    snapshot.chunk_snapshots = world_.modified_chunk_snapshots();
    snapshot.metadata.exists = true;
    snapshot.metadata.seed = world_.seed();
    snapshot.metadata.time_of_day = environment_.time_of_day();
    snapshot.metadata.weather_time_seconds = environment_.weather_time_seconds();
    snapshot.metadata.modified_chunk_count = static_cast<std::uint32_t>(std::min<std::size_t>(
        snapshot.chunk_snapshots.size(),
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
    snapshot.metadata.has_starting_village = starting_village_enabled_;
    snapshot.spawn_position = spawn_position_;
    snapshot.player_state = player_.state();
    snapshot.progression = progression_.state();
    snapshot.hotbar = hotbar_;
    snapshot.inventory = inventory_menu_;
    snapshot.inventory.visible = false;
    snapshot.inventory.hovered_slot.reset();
    snapshot.creatures.assign(creatures_.active_creatures().begin(), creatures_.active_creatures().end());
    snapshot.item_drops = item_drops_.drops();
    return snapshot;
}

void Game::configure_starting_village(bool enabled, bool apply_layout_to_world) {
    starting_village_enabled_ = enabled;
    if (!enabled) {
        starting_village_ = {};
        creatures_.set_settlement_residents({});
        return;
    }

    StartingVillageGenerator generator(world_.seed());
    starting_village_ = generator.build_layout();
    if (apply_layout_to_world) {
        generator.apply(world_, starting_village_);
    }
    creatures_.set_settlement_residents(starting_village_.residents);
}

void Game::apply_renderer_options() {
    if (!renderer_.initialize(current_renderer_options())) {
        throw std::runtime_error("Unable to reconfigure renderer options");
    }

    world_.enqueue_loaded_mesh_uploads();
    renderer_.drain_pending_world_meshes(
        world_,
        std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<double>::infinity());
}

void Game::pump_loading_events() noexcept {
    SDL_Event event {};
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
            running_ = false;
            return;
        }
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            window_width_ = std::max(event.window.data1, 1);
            window_height_ = std::max(event.window.data2, 1);
        }
    }
}

void Game::present_loading_screen(std::string_view title, std::string_view detail, float progress) {
    if (window_ == nullptr || options_.hidden_window) {
        return;
    }

    std::string window_title(kGameWindowTitle);
    if (!detail.empty()) {
        window_title += " - ";
        window_title += detail;
    }
    SDL_SetWindowTitle(window_, window_title.c_str());
    renderer_.render_loading_screen(title, detail, progress, window_width_, window_height_);
    SDL_GL_SwapWindow(window_);
}

auto Game::preload_readiness(const glm::vec3& focus, int radius) const -> float {
    const auto target_radius = std::max(radius, 0);
    const auto center = world_.world_to_chunk(
        static_cast<int>(std::floor(focus.x)),
        static_cast<int>(std::floor(focus.z)));

    auto total_chunks = 0;
    auto ready_chunks = 0;
    for (int dz = -target_radius; dz <= target_radius; ++dz) {
        for (int dx = -target_radius; dx <= target_radius; ++dx) {
            ++total_chunks;
            const ChunkCoord coord {center.x + dx, center.z + dz};
            const auto* chunk = world_.find_chunk(coord);
            if (chunk == nullptr) {
                continue;
            }
            if (world_.mesh_revision(coord) == 0 || chunk->is_dirty() || chunk->is_lighting_dirty()) {
                continue;
            }
            ++ready_chunks;
        }
    }

    if (total_chunks == 0) {
        return 1.0F;
    }
    return static_cast<float>(ready_chunks) / static_cast<float>(total_chunks);
}

void Game::refresh_save_slots() {
    if (save_root_directory_.empty()) {
        return;
    }

    save_slot_menu_.slots = scan_save_slots(save_root_directory_);
    save_slot_menu_.active_slot = active_save_slot_;
}

void Game::prime_world_around(const glm::vec3& focus, std::string_view loading_title, std::string_view loading_detail) {
    const auto center = world_.world_to_chunk(
        static_cast<int>(std::floor(focus.x)),
        static_cast<int>(std::floor(focus.z)));
    const auto preload_radius = std::max(options_.performance.spawn_preload_radius, 1);
    for (int dz = -preload_radius; dz <= preload_radius; ++dz) {
        for (int dx = -preload_radius; dx <= preload_radius; ++dx) {
            world_.ensure_chunk_loaded({center.x + dx, center.z + dz});
        }
    }

    (void)world_.update_streaming(focus);

    WorldWorkBudget warm_budget {};
    warm_budget.chunk_generation_budget = std::max<std::size_t>(options_.performance.chunk_generation_budget * 4U, 24U);
    warm_budget.mesh_rebuild_budget = std::max<std::size_t>(options_.performance.mesh_rebuild_budget * 4U, 24U);
    warm_budget.light_node_budget = std::max<std::size_t>(options_.performance.light_node_budget * 2U, 32768U);
    warm_budget.max_generation_ms = std::numeric_limits<double>::infinity();
    warm_budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    warm_budget.max_meshing_ms = std::numeric_limits<double>::infinity();
    warm_budget.max_fluid_ms = std::numeric_limits<double>::infinity();
    warm_budget.fluid_cell_budget = std::max<std::size_t>(warm_budget.light_node_budget / 4U, 2048U);

    auto last_presented_progress = -1.0F;
    // Only block on the immediate gameplay neighborhood. The wider stream radius
    // can keep filling in once rendering has started.
    while (running_ && !world_.are_chunks_ready(focus, preload_radius)) {
        pump_loading_events();
        if (!running_) {
            return;
        }

        (void)world_.process_pending_work(warm_budget);
        renderer_.drain_pending_world_meshes(world_, 32U, 1.5);
        const auto readiness = preload_readiness(focus, preload_radius);
        if (readiness - last_presented_progress >= 0.04F || readiness >= 0.999F) {
            present_loading_screen(loading_title, loading_detail, readiness);
            last_presented_progress = readiness;
        }
    }

    if (running_) {
        renderer_.drain_pending_world_meshes(world_, 1024U, std::numeric_limits<double>::infinity());
    }
}

void Game::prepare_game_session() {
    main_menu_.visible = false;
    save_slot_menu_.visible = false;
    options_menu_.visible = false;
    confirm_dialog_.visible = false;
    confirm_dialog_.intent = ConfirmDialogIntent::None;
    pending_confirm_slot_.reset();
    paused_ = false;
    pause_menu_.visible = false;
    pause_menu_.selected_action = PauseMenuAction::Resume;
    inventory_visible_ = false;
    inventory_menu_.visible = false;
    inventory_menu_.hovered_slot.reset();
    death_screen_visible_ = false;
    death_screen_.visible = false;
    death_screen_.cause = PlayerDeathCause::None;
    set_mouse_capture(true);
    record_audit_event(
        AuditEventCategory::Session,
        "game_session_prepared",
        AuditSeverity::Info,
        audit_json_object({}),
        AuditPriority::Critical);
}

void Game::sync_menu_preview_environment() noexcept {
    if (front_end_visible()) {
        environment_.set_time_of_day(menu_preview_time_of_day_);
        environment_.set_frozen(true);
        return;
    }

    environment_.set_frozen(options_.freeze_time || options_.smoke_test);
}

void Game::initialize_preview_world() {
    present_loading_screen("VALCRAFT", "CREATION DU MONDE DE MENU", 0.12F);
    world_ = World(1337, options_.performance.stream_radius);
    creatures_.clear();
    item_drops_.clear();
    hotbar_ = make_default_hotbar_state();
    inventory_menu_ = make_default_inventory_menu_state();
    normalize_inventory_state(inventory_menu_, hotbar_);
    sync_selected_hotbar_slot();

    menu_preview_time_of_day_ = 8.25F;
    environment_.set_weather_seed(1337U);
    environment_.set_weather_time_seconds(0.0F);
    sync_menu_preview_environment();

    // The main menu only needs a scenic background; building the whole starting
    // village here causes a long black startup before the first frame.
    configure_starting_village(false, false);
    present_loading_screen("VALCRAFT", "POSITIONNEMENT DE LA CAMERA", 0.28F);
    spawn_position_ = find_initial_spawn_position();
    player_.respawn(spawn_position_);
    preview_player_.respawn(spawn_position_);
    prime_world_around(spawn_position_, "VALCRAFT", "CHARGEMENT DU PAYSAGE");
    if (!running_) {
        return;
    }
    update_menu_preview_camera(0.0F);
    creatures_.update(0.0F, world_, spawn_position_, environment_.current_state(), environment_.current_creature_cycle());
    present_loading_screen("VALCRAFT", "FINALISATION DU MENU", 1.0F);
}

void Game::open_main_menu(bool from_session) {
    main_menu_.visible = true;
    main_menu_.selected_action = MainMenuAction::Play;
    save_slot_menu_.visible = false;
    options_menu_.visible = false;
    paused_ = false;
    pause_menu_.visible = false;
    inventory_visible_ = false;
    inventory_menu_.visible = false;
    death_screen_visible_ = false;
    death_screen_.visible = false;
    super_vision_active_ = false;
    gameplay_announcements_.clear();
    set_confirm_dialog_visible(false);

    menu_preview_time_of_day_ = 8.25F;
    if (from_session && has_active_session_) {
        menu_preview_time_of_day_ = environment_.time_of_day();
    }
    sync_menu_preview_environment();
    update_menu_preview_camera(0.0F);
    set_mouse_capture(false);
    center_ui_cursor(window_, window_width_, window_height_, main_menu_.cursor_x, main_menu_.cursor_y);
    refresh_main_menu_hover();
    record_audit_event(
        AuditEventCategory::Ui,
        "main_menu_opened",
        AuditSeverity::Info,
        audit_json_object({
            {"from_session", audit_json_bool(from_session)},
        }),
        AuditPriority::High);
}

void Game::open_save_slot_menu(SaveSlotMenuMode mode, SaveSlotMenuParent parent) {
    refresh_save_slots();
    save_slot_menu_.visible = true;
    save_slot_menu_.mode = mode;
    save_slot_menu_.parent = parent;
    save_slot_menu_.active_slot = active_save_slot_;
    main_menu_.visible = false;
    options_menu_.visible = false;
    pause_menu_.visible = false;

    if (mode == SaveSlotMenuMode::SaveGame && active_save_slot_.has_value()) {
        save_slot_menu_.selected_index = *active_save_slot_;
    } else {
        save_slot_menu_.selected_index = first_save_slot_menu_index(save_slot_menu_);
    }

    set_mouse_capture(false);
    center_ui_cursor(window_, window_width_, window_height_, save_slot_menu_.cursor_x, save_slot_menu_.cursor_y);
    refresh_save_slot_menu_hover();
    record_audit_event(
        AuditEventCategory::Ui,
        "save_slot_menu_opened",
        AuditSeverity::Info,
        audit_json_object({
            {"mode", audit_json_number(static_cast<int>(mode))},
            {"parent", audit_json_number(static_cast<int>(parent))},
        }),
        AuditPriority::High);
}

void Game::open_options_menu(OptionsMenuParent parent) {
    options_menu_.visible = true;
    options_menu_.parent = parent;
    options_menu_.selected_action = OptionsMenuAction::ToggleShadows;
    options_menu_.shadows_enabled = runtime_shadows_enabled_;
    options_menu_.post_process_enabled = runtime_post_process_enabled_;
    main_menu_.visible = false;
    save_slot_menu_.visible = false;
    pause_menu_.visible = false;

    set_mouse_capture(false);
    center_ui_cursor(window_, window_width_, window_height_, options_menu_.cursor_x, options_menu_.cursor_y);
    refresh_options_menu_hover();
    record_audit_event(
        AuditEventCategory::Ui,
        "options_menu_opened",
        AuditSeverity::Info,
        audit_json_object({
            {"parent", audit_json_number(static_cast<int>(parent))},
        }),
        AuditPriority::High);
}

void Game::close_frontend_menu_to_parent() {
    if (options_menu_.visible) {
        const auto parent = options_menu_.parent;
        options_menu_.visible = false;
        if (parent == OptionsMenuParent::PauseMenu) {
            pause_menu_.visible = true;
            center_ui_cursor(window_, window_width_, window_height_, pause_menu_.cursor_x, pause_menu_.cursor_y);
            refresh_pause_menu_hover();
        } else {
            main_menu_.visible = true;
            center_ui_cursor(window_, window_width_, window_height_, main_menu_.cursor_x, main_menu_.cursor_y);
            refresh_main_menu_hover();
        }
        set_mouse_capture(false);
        return;
    }

    if (save_slot_menu_.visible) {
        const auto parent = save_slot_menu_.parent;
        save_slot_menu_.visible = false;
        if (parent == SaveSlotMenuParent::PauseMenu) {
            pause_menu_.visible = true;
            center_ui_cursor(window_, window_width_, window_height_, pause_menu_.cursor_x, pause_menu_.cursor_y);
            refresh_pause_menu_hover();
        } else {
            main_menu_.visible = true;
            center_ui_cursor(window_, window_width_, window_height_, main_menu_.cursor_x, main_menu_.cursor_y);
            refresh_main_menu_hover();
        }
        set_mouse_capture(false);
    }
}

void Game::request_return_to_main_menu() {
    if (has_active_session_ && session_dirty_) {
        set_confirm_dialog_visible(true, ConfirmDialogIntent::ReturnToMainMenu);
        return;
    }

    open_main_menu(true);
}

void Game::start_new_game_in_slot(std::size_t slot_index) {
    const auto time_seed = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::random_device random_device {};
    const auto seed = static_cast<int>(time_seed ^ random_device() ^ static_cast<std::uint32_t>(slot_index * 7919U));

    present_loading_screen("NOUVELLE PARTIE", "CONSTRUCTION DU VILLAGE", 0.08F);
    world_ = World(seed, options_.performance.stream_radius);
    creatures_.clear();
    item_drops_.clear();
    progression_.reset();
    super_vision_active_ = false;
    gameplay_announcements_.clear();
    hotbar_ = make_default_hotbar_state();
    inventory_menu_ = make_default_inventory_menu_state();
    normalize_inventory_state(inventory_menu_, hotbar_);
    configure_starting_village(true, true);
    present_loading_screen("NOUVELLE PARTIE", "PREPARATION DU POINT D'APPARITION", 0.24F);
    spawn_position_ = find_initial_spawn_position();
    player_.respawn(spawn_position_);
    sync_selected_hotbar_slot();

    environment_.set_time_of_day(8.25F);
    environment_.set_weather_seed(static_cast<std::uint32_t>(seed));
    environment_.set_weather_time_seconds(0.0F);
    environment_.set_frozen(options_.freeze_time);
    prime_world_around(spawn_position_, "NOUVELLE PARTIE", "CHARGEMENT DES CHUNKS");
    if (!running_) {
        return;
    }
    creatures_.update(0.0F, world_, player_.position(), environment_.current_state(), environment_.current_creature_cycle());
    preview_orbit_radians_ = 0.0F;
    update_menu_preview_camera(0.0F);

    present_loading_screen("NOUVELLE PARTIE", "INITIALISATION DU RENDU", 0.94F);
    renderer_.shutdown();
    apply_renderer_options();
    SDL_SetWindowTitle(window_, kGameWindowTitle.data());

    has_active_session_ = true;
    active_save_slot_ = slot_index;
    session_dirty_ = false;
    prepare_game_session();
    save_game_to_slot(slot_index);
    record_audit_event(
        AuditEventCategory::Session,
        "new_game_started",
        AuditSeverity::Info,
        audit_json_object({
            {"slot_index", audit_json_number(slot_index)},
            {"seed", audit_json_number(world_.seed())},
        }),
        AuditPriority::Critical);
}

auto Game::load_game_from_slot(std::size_t slot_index) -> bool {
    const auto snapshot = load_save_slot(save_root_directory_, slot_index);
    if (!snapshot.has_value()) {
        refresh_save_slots();
        return false;
    }

    load_snapshot_into_session(*snapshot, slot_index);
    return true;
}

void Game::load_snapshot_into_session(const SaveGameSnapshot& snapshot, std::optional<std::size_t> slot_index) {
    present_loading_screen("CHARGEMENT", "LECTURE DE LA SAUVEGARDE", 0.08F);
    world_ = World(snapshot.metadata.seed, options_.performance.stream_radius);
    world_.replace_chunk_snapshots(snapshot.chunk_snapshots);
    hotbar_ = snapshot.hotbar;
    inventory_menu_ = snapshot.inventory;
    normalize_inventory_state(inventory_menu_, hotbar_);
    inventory_menu_.visible = false;
    inventory_menu_.hovered_slot.reset();
    item_drops_.load_drops(snapshot.item_drops);
    creatures_.clear();
    configure_starting_village(snapshot.metadata.has_starting_village, false);
    spawn_position_ = finite_vec3_or(snapshot.spawn_position, {0.5F, 70.0F, 0.5F});
    progression_.load_state(snapshot.progression);
    player_.load_state(snapshot.player_state);
    super_vision_active_ = false;
    gameplay_announcements_.clear();
    sync_selected_hotbar_slot();

    environment_.set_time_of_day(snapshot.metadata.time_of_day);
    environment_.set_weather_seed(static_cast<std::uint32_t>(snapshot.metadata.seed));
    environment_.set_weather_time_seconds(snapshot.metadata.weather_time_seconds);
    environment_.set_frozen(options_.freeze_time);
    prime_world_around(player_.position(), "CHARGEMENT", "RESTAURATION DU MONDE");
    if (!running_) {
        return;
    }
    creatures_.load_creatures(snapshot.creatures, environment_.current_state());
    preview_orbit_radians_ = 0.0F;
    menu_preview_time_of_day_ = environment_.time_of_day();
    update_menu_preview_camera(0.0F);

    present_loading_screen("CHARGEMENT", "INITIALISATION DU RENDU", 0.94F);
    renderer_.shutdown();
    apply_renderer_options();
    SDL_SetWindowTitle(window_, kGameWindowTitle.data());

    has_active_session_ = true;
    active_save_slot_ = slot_index;
    session_dirty_ = false;
    prepare_game_session();
    refresh_save_slots();
    record_audit_event(
        AuditEventCategory::Session,
        "game_loaded",
        AuditSeverity::Info,
        audit_json_object({
            {"has_slot", audit_json_bool(slot_index.has_value())},
            {"seed", audit_json_number(snapshot.metadata.seed)},
        }),
        AuditPriority::Critical);
}

void Game::save_game_to_slot(std::size_t slot_index) {
    if (!has_active_session_) {
        return;
    }

    auto snapshot = make_world_snapshot();
    write_save_slot(save_root_directory_, slot_index, snapshot);
    active_save_slot_ = slot_index;
    session_dirty_ = false;
    refresh_save_slots();
    record_audit_event(
        AuditEventCategory::Session,
        "game_saved",
        AuditSeverity::Info,
        audit_json_object({
            {"slot_index", audit_json_number(slot_index)},
            {"modified_chunks", audit_json_number(snapshot.metadata.modified_chunk_count)},
        }),
        AuditPriority::High);
}

void Game::mark_session_dirty() noexcept {
    session_dirty_ = true;
}

void Game::update_menu_preview_camera(float dt) {
    constexpr float kTwoPi = 6.28318530718F;
    const auto clamped_dt = std::max(finite_or(dt, 0.0F), 0.0F);
    preview_orbit_radians_ = std::fmod(finite_or(preview_orbit_radians_, 0.0F) + clamped_dt * 0.12F, kTwoPi);
    const auto focus = spawn_position_ + glm::vec3 {0.0F, 5.0F, 0.0F};
    const auto radius = 26.0F;
    const auto position = glm::vec3 {
        focus.x + std::cos(preview_orbit_radians_) * radius,
        focus.y + 7.0F + std::sin(preview_orbit_radians_ * 0.7F) * 1.8F,
        focus.z + std::sin(preview_orbit_radians_) * radius,
    };
    const auto eye_position = position + glm::vec3 {0.0F, 1.62F, 0.0F};
    const auto direction = glm::normalize(focus - eye_position);

    auto preview_state = preview_player_.state();
    preview_state.position = position;
    preview_state.velocity = {};
    preview_state.fly_mode = true;
    preview_state.on_ground = false;
    preview_state.dead = false;
    preview_state.head_underwater = false;
    preview_state.swimming = false;
    preview_state.animation_time += clamped_dt;
    preview_state.yaw_degrees = glm::degrees(std::atan2(direction.z, direction.x));
    preview_state.pitch_degrees = glm::degrees(std::asin(std::clamp(direction.y, -1.0F, 1.0F)));
    preview_state.body_yaw_degrees = preview_state.yaw_degrees;
    preview_player_.load_state(preview_state);
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

void Game::capture_current_frame_if_requested() {
    if (frame_capture_written_ || options_.frame_capture_path.empty()) {
        return;
    }
    if (options_.smoke_test && rendered_frames_ + 1 < options_.smoke_frames) {
        return;
    }

    const std::filesystem::path output_path(options_.frame_capture_path);
    save_current_backbuffer_bmp(output_path, window_width_, window_height_);
    frame_capture_written_ = true;
    record_audit_event(
        AuditEventCategory::Session,
        "frame_captured",
        AuditSeverity::Info,
        audit_json_object({
            {"path", audit_json_string(output_path.string())},
            {"width", audit_json_number(window_width_)},
            {"height", audit_json_number(window_height_)},
        }),
        AuditPriority::High);
}

void Game::record_frame_stats(const FramePerformanceStats& frame_stats) {
    if (!should_capture_performance()) {
        return;
    }

    const auto sample = FramePerformanceSample {
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
    };
    frame_samples_.push_back(sample);
    note_frame_for_audit(frame_stats);
}

void Game::record_performance_event(PerformanceEventKind kind, const BlockCoord& block, std::string_view label) {
    if (!should_capture_performance()) {
        return;
    }

    const auto chunk_coord = world_.world_to_chunk(block.x, block.z);
    performance_events_.push_back({
        static_cast<std::size_t>(rendered_frames_),
        kind,
        std::string(label),
        block.x,
        block.y,
        block.z,
        chunk_coord.x,
        chunk_coord.z,
        world_.pending_generation_count(),
        world_.pending_mesh_count(),
        world_.pending_lighting_count(),
    });

    record_audit_event(
        AuditEventCategory::Player,
        kind == PerformanceEventKind::BlockBreak ? "block_break" : "block_place",
        AuditSeverity::Info,
        audit_json_object({
            {"label", audit_json_string(label)},
            {"world_x", audit_json_number(block.x)},
            {"world_y", audit_json_number(block.y)},
            {"world_z", audit_json_number(block.z)},
            {"chunk_x", audit_json_number(chunk_coord.x)},
            {"chunk_z", audit_json_number(chunk_coord.z)},
            {"pending_generation", audit_json_number(world_.pending_generation_count())},
            {"pending_mesh", audit_json_number(world_.pending_mesh_count())},
            {"pending_lighting", audit_json_number(world_.pending_lighting_count())},
        }),
        AuditPriority::High);
}

void Game::record_audit_event(AuditEventCategory category,
                              std::string_view kind,
                              AuditSeverity severity,
                              std::string payload_json,
                              AuditPriority priority) {
    if (!audit_ || !audit_->enabled()) {
        return;
    }

    note_audit_event(category, kind);
    AuditEvent event {};
    event.frame_index = static_cast<std::size_t>(rendered_frames_);
    event.second_index = audit_second_accumulator_.second_index;
    event.category = category;
    event.kind = std::string(kind);
    event.severity = severity;
    event.payload_json = std::move(payload_json);
    audit_->record_event(std::move(event), priority);
}

void Game::record_raw_input_event(const SDL_Event& event) {
    if (!audit_ || !audit_->enabled() || options_.audit.mode != AuditMode::Forensic) {
        return;
    }

    std::string payload_json = audit_json_object({
        {"type", audit_json_number(event.type)},
    });

    switch (event.type) {
    case SDL_QUIT:
        payload_json = audit_json_object({});
        break;
    case SDL_WINDOWEVENT:
        payload_json = audit_json_object({
            {"event", audit_json_number(event.window.event)},
            {"data1", audit_json_number(event.window.data1)},
            {"data2", audit_json_number(event.window.data2)},
        });
        break;
    case SDL_MOUSEMOTION:
        payload_json = audit_json_object({
            {"x", audit_json_number(event.motion.x)},
            {"y", audit_json_number(event.motion.y)},
            {"xrel", audit_json_number(event.motion.xrel)},
            {"yrel", audit_json_number(event.motion.yrel)},
        });
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        payload_json = audit_json_object({
            {"button", audit_json_number(event.button.button)},
            {"x", audit_json_number(event.button.x)},
            {"y", audit_json_number(event.button.y)},
        });
        break;
    case SDL_MOUSEWHEEL:
        payload_json = audit_json_object({
            {"x", audit_json_number(event.wheel.x)},
            {"y", audit_json_number(event.wheel.y)},
            {"direction", audit_json_number(event.wheel.direction)},
        });
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        payload_json = audit_json_object({
            {"sym", audit_json_number(event.key.keysym.sym)},
            {"scancode", audit_json_number(event.key.keysym.scancode)},
            {"repeat", audit_json_number(event.key.repeat)},
            {"mod", audit_json_number(event.key.keysym.mod)},
        });
        break;
    default:
        break;
    }

    record_audit_event(
        AuditEventCategory::InputRaw,
        "sdl_event",
        AuditSeverity::Trace,
        std::move(payload_json),
        AuditPriority::Low);
}

void Game::note_audit_event(AuditEventCategory category, std::string_view kind) {
    switch (category) {
    case AuditEventCategory::InputRaw:
        ++frame_raw_input_events_;
        ++audit_second_accumulator_.input_raw_events;
        break;
    case AuditEventCategory::InputAction:
        ++frame_input_action_events_;
        ++audit_second_accumulator_.input_action_events;
        break;
    case AuditEventCategory::Ui:
        ++audit_second_accumulator_.ui_events;
        break;
    case AuditEventCategory::Player:
        ++audit_second_accumulator_.player_events;
        if (kind == "block_break") {
            ++audit_second_accumulator_.block_breaks;
        } else if (kind == "block_place") {
            ++audit_second_accumulator_.block_places;
        }
        break;
    case AuditEventCategory::Creatures:
        break;
    case AuditEventCategory::Items:
    case AuditEventCategory::World:
    case AuditEventCategory::Render:
    case AuditEventCategory::Performance:
    case AuditEventCategory::Session:
    default:
        break;
    }
}

void Game::note_frame_for_audit(const FramePerformanceStats& frame_stats) {
    if (!audit_ || !audit_->enabled()) {
        return;
    }

    audit_elapsed_ms_ += frame_stats.frame_total_ms;
    audit_second_accumulator_.frame_ms_values.push_back(frame_stats.frame_total_ms);
    if (frame_stats.frame_total_ms > 0.0) {
        audit_second_accumulator_.fps_values.push_back(1000.0 / frame_stats.frame_total_ms);
    }

    audit_second_accumulator_.streaming_ms_total += frame_stats.streaming_ms;
    audit_second_accumulator_.generation_ms_total += frame_stats.generation_ms;
    audit_second_accumulator_.lighting_ms_total += frame_stats.lighting_ms;
    audit_second_accumulator_.meshing_ms_total += frame_stats.meshing_ms;
    audit_second_accumulator_.upload_ms_total += frame_stats.upload_ms;
    audit_second_accumulator_.shadow_ms_total += frame_stats.shadow_ms;
    audit_second_accumulator_.world_ms_total += frame_stats.world_ms;
    audit_second_accumulator_.stream_chunk_changes += frame_stats.stream_chunk_changes;
    audit_second_accumulator_.generation_enqueued += frame_stats.generation_enqueued;
    audit_second_accumulator_.generation_pruned += frame_stats.generation_pruned;
    audit_second_accumulator_.unloaded_chunks += frame_stats.unloaded_chunks;
    audit_second_accumulator_.generated_chunks += frame_stats.generated_chunks;
    audit_second_accumulator_.meshed_chunks += frame_stats.meshed_chunks;
    audit_second_accumulator_.light_nodes_processed += frame_stats.light_nodes_processed;
    audit_second_accumulator_.lighting_jobs_completed += frame_stats.lighting_jobs_completed;
    audit_second_accumulator_.uploaded_meshes += frame_stats.uploaded_meshes;
    audit_second_accumulator_.visible_chunks_max =
        std::max(audit_second_accumulator_.visible_chunks_max, frame_stats.visible_chunks);
    audit_second_accumulator_.shadow_chunks_max =
        std::max(audit_second_accumulator_.shadow_chunks_max, frame_stats.shadow_chunks);
    audit_second_accumulator_.world_chunks_max =
        std::max(audit_second_accumulator_.world_chunks_max, frame_stats.world_chunks);
    audit_second_accumulator_.pending_generation_max =
        std::max(audit_second_accumulator_.pending_generation_max, frame_stats.pending_generation);
    audit_second_accumulator_.pending_mesh_max =
        std::max(audit_second_accumulator_.pending_mesh_max, frame_stats.pending_mesh);
    audit_second_accumulator_.pending_lighting_max =
        std::max(audit_second_accumulator_.pending_lighting_max, frame_stats.pending_lighting);
    audit_second_accumulator_.active_creatures_max =
        std::max(audit_second_accumulator_.active_creatures_max, creatures_.active_creatures().size());
    audit_second_accumulator_.active_item_drops_max =
        std::max(audit_second_accumulator_.active_item_drops_max, item_drops_.active_drop_count());

    if (frame_stats.frame_total_ms > kPerformanceLagThreshold16Ms) {
        ++audit_second_accumulator_.spike_frames;
        record_audit_event(
            AuditEventCategory::Performance,
            "frame_spike",
            frame_stats.frame_total_ms > kPerformanceLagThreshold50Ms ? AuditSeverity::Error : AuditSeverity::Warning,
            audit_json_object({
                {"frame_total_ms", audit_json_number(frame_stats.frame_total_ms)},
                {"streaming_ms", audit_json_number(frame_stats.streaming_ms)},
                {"generation_ms", audit_json_number(frame_stats.generation_ms)},
                {"lighting_ms", audit_json_number(frame_stats.lighting_ms)},
                {"meshing_ms", audit_json_number(frame_stats.meshing_ms)},
                {"upload_ms", audit_json_number(frame_stats.upload_ms)},
                {"shadow_ms", audit_json_number(frame_stats.shadow_ms)},
                {"world_ms", audit_json_number(frame_stats.world_ms)},
            }),
            AuditPriority::High);
    }

    audit_->record_frame(make_audit_frame_sample(frame_stats), AuditPriority::Low);
    flush_audit_second_sample(false);
}

void Game::flush_audit_second_sample(bool force) {
    if (!audit_ || !audit_->enabled()) {
        return;
    }
    if (audit_second_accumulator_.frame_ms_values.empty()) {
        return;
    }
    if (!force && audit_elapsed_ms_ < 1000.0) {
        return;
    }

    AuditSecondSample sample {};
    sample.second_index = audit_second_accumulator_.second_index;
    sample.frame_count = audit_second_accumulator_.frame_ms_values.size();
    sample.fps_avg = summarize_metric(audit_second_accumulator_.fps_values).average;
    sample.fps_min = audit_second_accumulator_.fps_values.empty()
                         ? 0.0
                         : *std::min_element(audit_second_accumulator_.fps_values.begin(), audit_second_accumulator_.fps_values.end());
    sample.fps_max = audit_second_accumulator_.fps_values.empty()
                         ? 0.0
                         : *std::max_element(audit_second_accumulator_.fps_values.begin(), audit_second_accumulator_.fps_values.end());
    const auto frame_metrics = summarize_metric(audit_second_accumulator_.frame_ms_values);
    sample.frame_ms_avg = frame_metrics.average;
    sample.frame_ms_p95 = frame_metrics.p95;
    sample.frame_ms_max = frame_metrics.maximum;
    const auto frame_count = static_cast<double>(sample.frame_count);
    sample.streaming_ms_avg = audit_second_accumulator_.streaming_ms_total / frame_count;
    sample.generation_ms_avg = audit_second_accumulator_.generation_ms_total / frame_count;
    sample.lighting_ms_avg = audit_second_accumulator_.lighting_ms_total / frame_count;
    sample.meshing_ms_avg = audit_second_accumulator_.meshing_ms_total / frame_count;
    sample.upload_ms_avg = audit_second_accumulator_.upload_ms_total / frame_count;
    sample.shadow_ms_avg = audit_second_accumulator_.shadow_ms_total / frame_count;
    sample.world_ms_avg = audit_second_accumulator_.world_ms_total / frame_count;
    sample.input_raw_events = audit_second_accumulator_.input_raw_events;
    sample.input_action_events = audit_second_accumulator_.input_action_events;
    sample.ui_events = audit_second_accumulator_.ui_events;
    sample.player_events = audit_second_accumulator_.player_events;
    sample.block_breaks = audit_second_accumulator_.block_breaks;
    sample.block_places = audit_second_accumulator_.block_places;
    sample.stream_chunk_changes = audit_second_accumulator_.stream_chunk_changes;
    sample.generation_enqueued = audit_second_accumulator_.generation_enqueued;
    sample.generation_pruned = audit_second_accumulator_.generation_pruned;
    sample.unloaded_chunks = audit_second_accumulator_.unloaded_chunks;
    sample.generated_chunks = audit_second_accumulator_.generated_chunks;
    sample.meshed_chunks = audit_second_accumulator_.meshed_chunks;
    sample.light_nodes_processed = audit_second_accumulator_.light_nodes_processed;
    sample.lighting_jobs_completed = audit_second_accumulator_.lighting_jobs_completed;
    sample.uploaded_meshes = audit_second_accumulator_.uploaded_meshes;
    sample.visible_chunks_max = audit_second_accumulator_.visible_chunks_max;
    sample.shadow_chunks_max = audit_second_accumulator_.shadow_chunks_max;
    sample.world_chunks_max = audit_second_accumulator_.world_chunks_max;
    sample.pending_generation_max = audit_second_accumulator_.pending_generation_max;
    sample.pending_mesh_max = audit_second_accumulator_.pending_mesh_max;
    sample.pending_lighting_max = audit_second_accumulator_.pending_lighting_max;
    sample.creature_spawns = audit_second_accumulator_.creature_spawns;
    sample.creature_despawns = audit_second_accumulator_.creature_despawns;
    sample.creature_attacks = audit_second_accumulator_.creature_attacks;
    sample.active_creatures_max = audit_second_accumulator_.active_creatures_max;
    sample.item_spawns = audit_second_accumulator_.item_spawns;
    sample.item_merges = audit_second_accumulator_.item_merges;
    sample.item_pickups = audit_second_accumulator_.item_pickups;
    sample.item_expired = audit_second_accumulator_.item_expired;
    sample.active_item_drops_max = audit_second_accumulator_.active_item_drops_max;
    sample.spike_frames = audit_second_accumulator_.spike_frames;
    audit_->record_second(std::move(sample), AuditPriority::High);

    audit_elapsed_ms_ = force ? 0.0 : std::max(0.0, audit_elapsed_ms_ - 1000.0);
    audit_second_accumulator_.reset(audit_second_accumulator_.second_index + 1);
}

auto Game::make_audit_frame_sample(const FramePerformanceStats& frame_stats) const -> AuditFrameSample {
    AuditFrameSample sample {};
    sample.frame_index = static_cast<std::size_t>(rendered_frames_);
    sample.second_index = audit_second_accumulator_.second_index;
    sample.fps = frame_stats.frame_total_ms > 0.0 ? 1000.0 / frame_stats.frame_total_ms : 0.0;
    switch (active_ui_screen()) {
    case UiScreen::MainMenu:
        sample.ui_screen = "main_menu";
        break;
    case UiScreen::SaveSlots:
        sample.ui_screen = "save_slots";
        break;
    case UiScreen::Options:
        sample.ui_screen = "options";
        break;
    case UiScreen::Inventory:
        sample.ui_screen = "inventory";
        break;
    case UiScreen::Pause:
        sample.ui_screen = "pause";
        break;
    case UiScreen::Death:
        sample.ui_screen = "death";
        break;
    case UiScreen::Gameplay:
    default:
        sample.ui_screen = "gameplay";
        break;
    }
    sample.mouse_captured = mouse_captured_;
    sample.input_raw_events = frame_raw_input_events_;
    sample.input_action_events = frame_input_action_events_;
    sample.active_creatures = creatures_.active_creatures().size();
    sample.active_item_drops = item_drops_.active_drop_count();
    sample.performance = {
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
    };
    sample.performance.dominant_stage = detect_dominant_stage(sample.performance);
    return sample;
}

auto Game::should_capture_performance() const noexcept -> bool {
    return options_.performance.report_frame_stats ||
           !options_.performance.perf_json_path.empty() ||
           options_.audit.enabled;
}

auto Game::build_performance_report() const -> PerformanceRunReport {
    PerformanceReportMetadata metadata {};
    metadata.platform = std::string(kPerformancePlatform);
    metadata.build_type = std::string(kPerformanceBuildType.empty() ? std::string_view("unknown") : kPerformanceBuildType);
    metadata.capture_mode = options_.audit.enabled
                                ? std::string(audit_mode_name(options_.audit.mode))
                                : (options_.smoke_test ? "smoke" : "interactive");
    metadata.smoke_frames = options_.smoke_test ? static_cast<std::size_t>(options_.smoke_frames) : 0U;
    metadata.stream_radius = options_.performance.stream_radius;
    metadata.shadows_enabled = runtime_shadows_enabled_;
    metadata.shadow_map_size = options_.performance.shadow_map_size;
    metadata.post_process_enabled = runtime_post_process_enabled_;
    metadata.freeze_time = options_.freeze_time || options_.smoke_test;
    metadata.scenario = !options_.performance.perf_scenario.empty()
                            ? options_.performance.perf_scenario
                            : options_.audit.label;
    return valcraft::build_performance_report(
        metadata,
        frame_samples_,
        options_.performance.perf_trace_enabled || options_.audit.trace_frames || options_.audit.mode == AuditMode::Forensic,
        10,
        performance_events_);
}

void Game::write_performance_report(const PerformanceRunReport& report) const {
    if (options_.performance.report_frame_stats) {
        const auto text_report = format_performance_report(report);
        if (!text_report.empty()) {
            std::cout << text_report;
        }
    }

    if (options_.performance.perf_json_path.empty() || options_.audit.enabled) {
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
