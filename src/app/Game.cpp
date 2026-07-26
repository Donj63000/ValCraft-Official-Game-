#include "app/Game.h"
#include "app/SmokeCamera.h"
#include "app/GameBranding.h"
#include "app/InputBindings.h"
#include "app/GameLoop.h"
#include "gameplay/StartingPort.h"
#include "render/ShipMesh.h"
#include "render/StylizedShipMesh.h"
#include "world/OceanSimulation.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <atomic>
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
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace valcraft {

namespace {

#ifdef _WIN32
constexpr std::string_view kPerformancePlatform = "windows";
#elif defined(__linux__)
constexpr std::string_view kPerformancePlatform = "linux";
#elif defined(__APPLE__)
constexpr std::string_view kPerformancePlatform = "macos";
#else
constexpr std::string_view kPerformancePlatform = "unknown";
#endif

#ifdef VALCRAFT_BUILD_TYPE
constexpr std::string_view kPerformanceBuildType = VALCRAFT_BUILD_TYPE;
#else
constexpr std::string_view kPerformanceBuildType = "unknown";
#endif

#if defined(VALCRAFT_COVERAGE_BUILD) && VALCRAFT_COVERAGE_BUILD
constexpr bool kCoverageInstrumentationEnabled = true;
#else
constexpr bool kCoverageInstrumentationEnabled = false;
#endif

// Je garde le contrat production a 50 ms et j'accorde au build Debug non
// optimise la marge necessaire a la generation atomique d'un chunk oceanique.
constexpr double kMaritimeSmokeSliceLimitMs = kPerformanceBuildType == "Debug" ? 100.0 : 50.0;

constexpr std::size_t kMaxGameplayAnnouncementQueue = 6U;
constexpr std::size_t kMaxPerformanceSamples = 36'000U;
constexpr std::size_t kMaxPerformanceEvents = 4'096U;
constexpr int kWorldMemorySamplePeriodFrames = 30;
constexpr int kMinimumWindowWidth = 640;
constexpr int kMinimumWindowHeight = 360;

auto make_nonblocking_world_seed(std::size_t slot_index) noexcept -> int {
    static std::atomic<std::uint32_t> sequence {0U};
    const auto ticks = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto value = static_cast<std::uint32_t>(ticks) ^
                 static_cast<std::uint32_t>(ticks >> 32U) ^
                 static_cast<std::uint32_t>(slot_index) ^
                 sequence.fetch_add(0x9E3779B9U, std::memory_order_relaxed);
    // Je melange une source locale non bloquante : une seed de monde n'a pas
    // besoin d'etre cryptographique et ne doit jamais figer le thread d'UI.
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return static_cast<int>(value & static_cast<std::uint32_t>((std::numeric_limits<int>::max)()));
}

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

auto crafting_tool_from_keycode(SDL_Keycode keycode) noexcept -> BlockId {
    // Je garde des raccourcis courts dans l'inventaire sans ajouter un nouvel ecran de craft.
    switch (keycode) {
    case SDLK_p:
    case SDLK_F1:
        return to_block_id(BlockType::Pickaxe);
    case SDLK_h:
    case SDLK_F2:
        return to_block_id(BlockType::Axe);
    case SDLK_b:
    case SDLK_F3:
        return to_block_id(BlockType::Shovel);
    default:
        return to_block_id(BlockType::Air);
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
      world_(
          1337,
          options.performance.stream_radius,
          WorldGenerationProfile::Continental,
          WorldGenerationVersion::Latest,
          options.visual_pipeline),
      options_(options) {
    window_width_ =
        std::clamp(
            options_.window_width,
            kMinimumWindowWidth,
            7680);
    window_height_ =
        std::clamp(
            options_.window_height,
            kMinimumWindowHeight,
            4320);
    environment_.set_weather_time_seconds(
        options_.initial_weather_time_seconds);
    runtime_shadows_enabled_ = options_.performance.shadows_enabled;
    runtime_post_process_enabled_ = options_.performance.post_process_enabled;
    if (should_capture_performance()) {
        const auto reserved_frames = options_.smoke_test
                                         ? std::min(
                                               static_cast<std::size_t>(std::max(options_.smoke_frames, 0)),
                                               kMaxPerformanceSamples)
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
        auto pending_frame_stats = std::optional<FramePerformanceStats> {};
        auto pending_frame_begin = clock::time_point {};
        const auto finalize_pending_frame = [&](clock::time_point frame_end) {
            if (!pending_frame_stats.has_value()) {
                return;
            }

            auto& completed_stats = *pending_frame_stats;
            completed_stats.frame_total_ms =
                std::chrono::duration<double, std::milli>(frame_end - pending_frame_begin).count();
            const auto accounted_ms = completed_stats.event_processing_ms + completed_stats.simulation_ms +
                                      completed_stats.audio_ms + completed_stats.render_preparation_ms +
                                      completed_stats.streaming_ms + completed_stats.generation_ms +
                                      completed_stats.fluid_ms + completed_stats.lighting_ms +
                                      completed_stats.meshing_ms + completed_stats.render_cpu_ms +
                                      completed_stats.present_ms + completed_stats.telemetry_ms;
            completed_stats.residual_ms = std::max(0.0, completed_stats.frame_total_ms - accounted_ms);
            // Je transmets le cout actif complet au controleur adaptatif. Je
            // retire la presentation, car elle contient la VSync et le pacing.
            renderer_.submit_cpu_frame_time_sample(
                std::max(
                    0.0,
                    completed_stats.frame_total_ms -
                        completed_stats.present_ms));
            recording_frame_index_ = completed_stats.frame_index;
            record_frame_stats(completed_stats);
            recording_frame_index_.reset();
            pending_frame_stats.reset();
        };

        while (running_) {
            const auto frame_begin = clock::now();
            FramePerformanceStats frame_stats {};
            frame_stats.frame_index = static_cast<std::size_t>(rendered_frames_);
            const auto telemetry_begin = clock::now();
            finalize_pending_frame(frame_begin);
            frame_stats.telemetry_ms =
                std::chrono::duration<double, std::milli>(clock::now() - telemetry_begin).count();
            frame_raw_input_events_ = 0;
            frame_input_action_events_ = 0;
            const auto event_begin = clock::now();
            finish_pending_save(false);
            finish_pending_world_release(false);
            process_events();
            frame_stats.event_processing_ms =
                std::chrono::duration<double, std::milli>(clock::now() - event_begin).count();

            const auto now = clock::now();
            const auto measured_frame_time = now - previous;
            previous = now;
            const auto frame_time = resolve_simulation_frame_time(options_.smoke_test, measured_frame_time, fixed_step);
            accumulator += frame_time;

            constexpr int kMaxFixedUpdatesPerFrame = 4;
            int fixed_updates = 0;
            const auto simulation_begin = clock::now();
            while (accumulator >= fixed_step && fixed_updates < kMaxFixedUpdatesPerFrame) {
                update_simulation(static_cast<float>(fixed_step.count()), frame_stats);
                accumulator -= fixed_step;
                ++fixed_updates;
            }
            frame_stats.simulation_ms =
                std::chrono::duration<double, std::milli>(clock::now() - simulation_begin).count();
            frame_stats.fixed_updates = static_cast<std::size_t>(fixed_updates);

            if (fixed_updates == kMaxFixedUpdatesPerFrame && accumulator > fixed_step) {
                const auto queued_updates = static_cast<std::size_t>(accumulator / fixed_step);
                frame_stats.dropped_fixed_updates = queued_updates > 1U ? queued_updates - 1U : 0U;
                accumulator = fixed_step;
            }

            update_world_pipeline(frame_stats);

            const auto audio_begin = clock::now();
            const auto environment_state = environment_.current_state();
            const auto creature_cycle = environment_.current_creature_cycle();
            const auto front_end_is_visible = front_end_visible();
            const auto maritime_gameplay_active =
                active_game_mode_ == GameMode::SeaAdventure && sea_adventure_.active();
            float voyage_motion = 0.0F;
            float maritime_danger = 0.0F;

            if (has_active_session_ && maritime_gameplay_active) {
                const auto maritime_state = sea_adventure_.hud_state(player_);

                // Je traduis ici le voyage en intensite musicale pour garder
                // le syntheseur independant des types propres au gameplay.
                switch (maritime_state.phase) {
                case SeaVoyagePhase::Moored:
                    voyage_motion = 0.0F;
                    break;
                case SeaVoyagePhase::Departing:
                    voyage_motion = maritime_state.departure_ratio;
                    break;
                case SeaVoyagePhase::Underway:
                    voyage_motion = 1.0F;
                    break;
                }

                maritime_danger = maritime_state.danger
                    ? 1.0F
                    : environment_state.storm_intensity;
            }

            const auto music_context = make_game_music_context({
                .has_active_session = has_active_session_,
                .front_end_visible = front_end_is_visible,
                .maritime_gameplay_active = maritime_gameplay_active,
                .voyage_motion = voyage_motion,
                .danger = maritime_danger,
                .world_seed = world_.seed(),
            });

            music_.sync_environment(
                environment_state,
                creature_cycle,
                has_active_session_,
                front_end_is_visible,
                music_context);
            music_.pump();
            frame_stats.audio_ms =
                std::chrono::duration<double, std::milli>(clock::now() - audio_begin).count();

            const auto render_preparation_begin = clock::now();
            item_drops_.build_render_instances(world_, item_drop_render_instances_);
            frame_stats.render_preparation_ms =
                std::chrono::duration<double, std::milli>(clock::now() - render_preparation_begin).count();

            const auto render_begin = clock::now();
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
                sea_adventure_.crew_render_instances(),
                sea_adventure_.old_guard_render_instances(),
                sea_adventure_.old_guard_flashes(),
                sea_adventure_.old_guard_smoke(),
                item_drop_render_instances_,
                sea_adventure_.ship_render_state(),
                progression_.state(),
                super_vision_active_ && progression_.has_super_vision_power(),
                current_gameplay_announcement_view(),
                current_maritime_hud_view(),
                command_console_.view(),
                environment_state,
                window_width_,
                window_height_);
            frame_stats.render_cpu_ms =
                std::chrono::duration<double, std::milli>(clock::now() - render_begin).count();
            const auto& render_stats = renderer_.last_frame_stats();
            frame_stats.upload_ms += render_stats.upload_ms;
            frame_stats.shadow_ms += render_stats.shadow_ms;
            frame_stats.world_ms += render_stats.world_ms;
            frame_stats.uploaded_meshes += render_stats.uploaded_meshes;
            frame_stats.visible_chunks += render_stats.visible_chunks;
            frame_stats.shadow_chunks += render_stats.shadow_chunks;
            frame_stats.world_chunks += render_stats.world_chunks;
            frame_stats.draw_calls = render_stats.draw_calls;
            frame_stats.triangles = render_stats.triangles;
            frame_stats.uploaded_bytes = render_stats.uploaded_bytes;
            frame_stats.gpu_buffer_bytes = render_stats.gpu_buffer_bytes;
            frame_stats.gpu_texture_bytes = render_stats.gpu_texture_bytes;
            frame_stats.resolved_quality = static_cast<std::uint8_t>(render_stats.resolved_quality);
            frame_stats.adaptive_frame_ema_ms = render_stats.adaptive_frame_ema_ms;
            frame_stats.adaptive_frame_p95_ms = render_stats.adaptive_frame_p95_ms;
            frame_stats.render_overhead_ms = std::max(
                0.0,
                frame_stats.render_cpu_ms - frame_stats.upload_ms - frame_stats.shadow_ms - frame_stats.world_ms);
            if (render_stats.gpu.valid) {
                frame_stats.gpu_timing_valid = true;
                frame_stats.gpu_source_frame = static_cast<std::size_t>(render_stats.gpu.source_frame);
                frame_stats.gpu_latency_frames = static_cast<std::size_t>(render_stats.gpu.latency_frames);
                frame_stats.gpu_shadow_ms = render_stats.gpu.shadow_ms;
                frame_stats.gpu_world_ms = render_stats.gpu.opaque_ms;
                frame_stats.gpu_sky_ms = render_stats.gpu.sky_ms;
                frame_stats.gpu_water_ms = render_stats.gpu.water_ms;
                frame_stats.gpu_entities_ms = render_stats.gpu.entities_ms;
                frame_stats.gpu_post_process_ms = render_stats.gpu.post_process_ms;
                frame_stats.gpu_hud_ms = render_stats.gpu.ui_ms;
                frame_stats.gpu_frame_ms = render_stats.gpu.total_ms();
            }

            const auto present_begin = clock::now();
            capture_current_frame_if_requested();
            SDL_GL_SwapWindow(window_);
            if (software_frame_pacing_enabled_) {
                const auto frame_deadline = frame_begin + std::chrono::duration_cast<clock::duration>(fixed_step);
                if (clock::now() < frame_deadline) {
                    std::this_thread::sleep_until(frame_deadline);
                }
            }
            frame_stats.present_ms =
                std::chrono::duration<double, std::milli>(clock::now() - present_begin).count();
            pending_frame_stats = frame_stats;
            pending_frame_begin = frame_begin;
            ++rendered_frames_;

            if (options_.smoke_test && rendered_frames_ >= options_.smoke_frames) {
                running_ = false;
            }
        }

        finalize_pending_frame(clock::now());
        finish_pending_save(true);
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
        finish_pending_save(true);
        if (should_capture_performance()) {
            final_report = build_performance_report();
            finalize_audit(final_report, AuditRunStatus::Aborted);
        }
        shutdown();
        return 1;
    } catch (...) {
        std::cerr << "ValCraft fatal error: unknown exception" << std::endl;
        finish_pending_save(true);
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

    // Je conserve la taille minimale deja imposee au demarrage pour que
    // toutes les interfaces restent lisibles apres un redimensionnement.
    SDL_SetWindowMinimumSize(
        window_,
        kMinimumWindowWidth,
        kMinimumWindowHeight);
    SDL_StopTextInput();
    apply_window_icon(window_);

    gl_context_ = SDL_GL_CreateContext(window_);
    if (gl_context_ == nullptr) {
        return false;
    }

    SDL_GL_MakeCurrent(window_, gl_context_);
    if (options_.smoke_test) {
        (void)SDL_GL_SetSwapInterval(0);
        vsync_mode_ = "disabled";
    } else if (SDL_GL_SetSwapInterval(-1) != 0) {
        if (SDL_GL_SetSwapInterval(1) == 0) {
            vsync_mode_ = "enabled";
        } else {
            software_frame_pacing_enabled_ = true;
            vsync_mode_ = "software_60hz";
        }
    } else {
        vsync_mode_ = "adaptive";
    }

    if (gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)) == 0) {
        return false;
    }

    if (!renderer_.initialize(current_renderer_options())) {
        if (!renderer_.last_initialization_error().empty()) {
            const auto renderer_error =
                std::string("Renderer initialization failed: ") +
                std::string(renderer_.last_initialization_error());
            std::cerr << "ValCraft " << renderer_error << std::endl;
            if (audit_) {
                audit_->record_error(renderer_error);
            }
        }
        return false;
    }

    if (!options_.smoke_test) {
        (void)music_.initialize();
    }

    save_root_directory_ = resolve_save_root_directory();
    if (options_.smoke_test && options_.smoke_session != SmokeSessionMode::Menu) {
        const auto unique_suffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto temp_directory = std::filesystem::temp_directory_path();
        for (std::size_t attempt = 0U; attempt < 64U && !smoke_temp_root_.has_value(); ++attempt) {
            const auto candidate = temp_directory /
                                   (std::string("valcraft-sea-smoke-") + unique_suffix + "-" +
                                    std::to_string(attempt));
            std::error_code create_error {};
            if (std::filesystem::create_directory(candidate, create_error)) {
                smoke_temp_root_ = candidate;
            } else if (create_error) {
                throw std::runtime_error("Unable to create the isolated maritime smoke directory");
            }
        }
        if (!smoke_temp_root_.has_value()) {
            throw std::runtime_error("Unable to reserve an isolated maritime smoke directory");
        }
        save_root_directory_ = *smoke_temp_root_;
    }
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

    if (options_.smoke_test && options_.smoke_session != SmokeSessionMode::Menu) {
        if (!start_smoke_session()) {
            return false;
        }
        SDL_SetWindowTitle(window_, kGameWindowTitle.data());
        return true;
    }

    begin_loading_screen(LoadingScreenTheme::Standard, 1337U);
    update_loading_screen("VALCRAFT", "PREPARATION DU MENU", LoadingPhase::Preparation, 1.0F, true);
    initialize_preview_world();
    if (!running_) {
        return false;
    }
    present_loading_screen("VALCRAFT", "LECTURE DES SAUVEGARDES", 0.985F, true);
    refresh_save_slots();
    complete_loading_screen("VALCRAFT", "PRET");

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
            {"visual_pipeline", audit_json_string(visual_pipeline_name(options_.visual_pipeline))},
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
    finish_pending_save(true);
    finish_pending_world_release(true);
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
        SDL_StopTextInput();
    }
    command_console_.close();
    music_.shutdown();
    renderer_.shutdown();

    if (smoke_temp_root_.has_value()) {
        const auto normalized_root = smoke_temp_root_->lexically_normal();
        const auto normalized_temp = std::filesystem::temp_directory_path().lexically_normal();
        const auto filename = normalized_root.filename().string();
        if (normalized_root.parent_path() == normalized_temp &&
            filename.starts_with("valcraft-sea-smoke-")) {
            std::error_code cleanup_error {};
            std::filesystem::remove_all(normalized_root, cleanup_error);
        }
        smoke_temp_root_.reset();
    }

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
                window_width_ = std::max(event.window.data1, 1);
                window_height_ = std::max(event.window.data2, 1);
            }
            continue;
        }

        if (event.type == SDL_KEYDOWN &&
            is_command_console_key(event.key.keysym)) {
            const auto action =
                command_console_toggle_.handle_key_down(
                    event.key.repeat != 0,
                    command_console_.visible(),
                    can_open_command_console());
            if (action ==
                CommandConsoleToggleAction::Close) {
                set_command_console_visible(false);
            }
            continue;
        }
        if (event.type == SDL_KEYUP &&
            is_command_console_key(event.key.keysym)) {
            const auto action =
                command_console_toggle_.handle_key_up(
                    can_open_command_console());
            // J'ouvre au relachement pour que le caractere produit par la
            // touche physique ne puisse jamais entrer dans la commande.
            if (action ==
                CommandConsoleToggleAction::Open) {
                set_command_console_visible(true);
            }
            continue;
        }

        if (command_console_.visible()) {
            if (event.type == SDL_TEXTINPUT) {
                command_console_.insert_text(
                    event.text.text);
                continue;
            }
            if (event.type == SDL_KEYDOWN) {
                handle_command_console_keydown(
                    event.key);
                continue;
            }
            if (event.type == SDL_KEYUP ||
                event.type == SDL_TEXTEDITING ||
                event.type == SDL_MOUSEMOTION ||
                event.type == SDL_MOUSEBUTTONDOWN ||
                event.type == SDL_MOUSEBUTTONUP ||
                event.type == SDL_MOUSEWHEEL) {
                continue;
            }
        }

        switch (event.type) {
        case SDL_QUIT:
            running_ = false;
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event ==
                SDL_WINDOWEVENT_FOCUS_LOST) {
                command_console_toggle_.cancel();
            }
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                window_width_ = std::max(event.window.data1, 1);
                window_height_ = std::max(event.window.data2, 1);
                if (command_console_.visible()) {
                    refresh_command_console_text_input_rect();
                }
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
                if (const auto crafted_tool = crafting_tool_from_keycode(event.key.keysym.sym);
                    crafted_tool != to_block_id(BlockType::Air)) {
                    craft_inventory_tool(crafted_tool);
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
            } else if (active_game_mode_ == GameMode::SeaAdventure && is_flight_action_key(event.key.keysym)) {
                pending_fishing_ = true;
                record_audit_event(
                    AuditEventCategory::InputAction,
                    "sea_fishing_request",
                    AuditSeverity::Info,
                    audit_json_object({}),
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
            audit_ && audit_->enabled() &&
            (creature_stats.spawned != 0 || creature_stats.despawned != 0 || creature_stats.attacks != 0)) {
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
    const auto maritime_session_active =
        active_game_mode_ == GameMode::SeaAdventure &&
        sea_adventure_.active();
    std::optional<OceanState> maritime_ocean {};
    if (maritime_session_active) {
        maritime_ocean =
            OceanSimulation::evaluate(
                environment_state,
                OceanSimulation::surface_profile_for_world(
                    world_.generation_profile()));
    }

    if (options_.smoke_test) {
        if (maritime_session_active) {
            PlayerInput smoke_input {};
            player_.update(
                smoke_input,
                dt,
                world_,
                &sea_adventure_.ship_entity(),
                &*maritime_ocean);
            (void)sea_adventure_.update(world_, player_, environment_state, dt, false);
            update_smoke_ship_camera();
        } else {
            update_smoke_player(dt);
        }

        if (terrain_edit_stress_enabled(options_)) {
            const auto stress_frame =
                static_cast<std::size_t>(
                    std::max(rendered_frames_, 0));
            const auto smoke_frame_count =
                static_cast<std::size_t>(
                    std::max(options_.smoke_frames, 0));
            // Je ne commence une paire que si sa restauration dispose encore
            // d'une frame de simulation. Le smoke ne laisse ainsi aucun bloc
            // de benchmark dans le monde au moment de produire son rapport.
            const auto remaining_frames =
                stress_frame < smoke_frame_count
                    ? smoke_frame_count - stress_frame
                    : 0U;
            const auto operation =
                terrain_edit_stress_.update(
                    world_,
                    player_.position(),
                    stress_frame,
                    remaining_frames >
                        kTerrainEditStressIntervalFrames);
            if (operation.has_value()) {
                const auto is_break =
                    operation->action ==
                    TerrainEditStressAction::Break;
                record_performance_event(
                    is_break
                        ? PerformanceEventKind::BlockBreak
                        : PerformanceEventKind::BlockPlace,
                    operation->block,
                    is_break
                        ? "terrain_edit_stress_break"
                        : "terrain_edit_stress_place");
            }
        }
    } else {
        const auto gameplay_input_enabled =
            !inventory_visible_ &&
            !command_console_.visible();
        PlayerInput input {};
        if (gameplay_input_enabled) {
            input = read_player_movement_input(SDL_GetKeyboardState(nullptr));
        }
        input.toggle_fly = std::exchange(pending_toggle_fly_, false);
        input.look_delta_x = mouse_captured_ ? std::exchange(pending_look_x_, 0.0F) : 0.0F;
        input.look_delta_y = mouse_captured_ ? std::exchange(pending_look_y_, 0.0F) : 0.0F;

        if (gameplay_input_enabled &&
            pending_place_block_ &&
            !player_.is_dead()) {

            player_.trigger_secondary_action();
        }

        const auto* dynamic_ship =
            maritime_session_active
                ? &sea_adventure_.ship_entity()
                : nullptr;

        player_.update(
            input,
            dt,
            world_,
            dynamic_ship,
            maritime_ocean.has_value()
                ? &*maritime_ocean
                : nullptr);

        if (maritime_session_active) {
            const auto sea_result = sea_adventure_.update(
                world_,
                player_,
                environment_state,
                dt,
                std::exchange(pending_fishing_, false));
            if (sea_result.fishing_started) {
                queue_gameplay_announcement("PECHE", "LA LIGNE EST A L'EAU", 2.4F);
            } else if (sea_result.fishing_failed) {
                queue_gameplay_announcement("PECHE IMPOSSIBLE", "REVIENS SUR LE NAVIRE", 2.6F);
            }
            if (sea_result.fish_caught) {
                queue_gameplay_announcement("POISSON ATTRAPE", "LA FAIM REMONTE", 2.6F);
            }
            if (sea_result.consumed_food) {
                queue_gameplay_announcement("VIVRES", "RATION CONSOMMEE", 2.2F);
            }
            if (sea_result.consumed_water) {
                queue_gameplay_announcement("EAU", "RESERVE CONSOMMEE", 2.2F);
            }
            if (sea_result.crew_fish_delivered) {
                queue_gameplay_announcement("EQUIPAGE", "POISSON RANGE DANS LA CALE", 2.4F);
            }
            if (sea_result.crew_water_delivered) {
                queue_gameplay_announcement("EQUIPAGE", "EAU RANGEE DANS LA CALE", 2.4F);
            }
            if (sea_result.departure_started) {
                queue_gameplay_announcement("LARGUEZ LES AMARRES", "DEPART DU PORT", 3.0F);
            }
            if (sea_result.reached_open_sea) {
                queue_gameplay_announcement("CAP SUR LE LARGE", "NAVIGATION DE CROISIERE", 3.0F);
            }
            if (sea_result.stranded_warning) {
                queue_gameplay_announcement("NAVIRE LOINTAIN", "RETOURNE A BORD", 3.0F);
            }
            if (sea_result.stranded) {
                queue_gameplay_announcement("PERDU EN MER", "LE NAVIRE A DISPARU", 3.0F);
            }
        } else {
            pending_fishing_ = false;
        }

        if (gameplay_input_enabled &&
            pending_primary_attack_ &&
            !player_.is_dead()) {

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
                if (maritime_session_active) {
                    if (const auto ship_hit =
                            sea_adventure_.ship_entity().raycast_collidable_distance(
                                player_.eye_position(),
                                player_.look_direction(),
                                weapon_range);
                        ship_hit.has_value()) {
                        weapon_range =
                            std::clamp(
                                *ship_hit,
                                0.0F,
                                weapon_range);
                    }
                }

                const auto damage = weapon->damage * progression_.attack_damage_multiplier();
                const auto old_guard_hit =
                    maritime_session_active
                        ? sea_adventure_.intercept_old_guard(
                              player_.eye_position(),
                              player_.look_direction(),
                              weapon_range)
                        : OldGuardRayHit {};
                const auto entity_weapon_range =
                    old_guard_hit.hit
                        ? std::min(
                              weapon_range,
                              old_guard_hit.distance)
                        : weapon_range;
                const auto crew_hit =
                    maritime_session_active
                        ? sea_adventure_.try_damage_crew(
                              player_.eye_position(),
                              player_.look_direction(),
                              entity_weapon_range,
                              damage)
                        : ShipCrewDamageResult {};
                if (crew_hit.hit) {
                    music_.play_sfx(GameSfxKind::CreatureHit, 0.72F);
                    if (crew_hit.knocked_out) {
                        queue_gameplay_announcement("EQUIPAGE", "MARIN ASSOMME", 2.4F);
                    }
                    record_audit_event(
                        AuditEventCategory::Creatures,
                        crew_hit.knocked_out ? "ship_crew_knocked_out" : "ship_crew_damaged",
                        crew_hit.knocked_out ? AuditSeverity::Warning : AuditSeverity::Info,
                        audit_json_object({
                            {"member_id", audit_json_number(crew_hit.member_id)},
                            {"damage", audit_json_number(crew_hit.damage)},
                            {"remaining_health", audit_json_number(crew_hit.remaining_health)},
                        }),
                        crew_hit.knocked_out ? AuditPriority::High : AuditPriority::Normal);
                } else {
                    const auto hit_result = creatures_.try_damage_from_player(
                        player_.eye_position(),
                        player_.look_direction(),
                        entity_weapon_range,
                        damage);
                    if (hit_result.hit) {
                        music_.play_sfx(hit_result.killed ? GameSfxKind::CreatureDeath : GameSfxKind::CreatureHit,
                                        hit_result.killed ? 0.88F : 0.72F);
                        if (hit_result.killed) {
                            if (maritime_session_active &&
                                sea_adventure_.record_hunt(
                                    hit_result.species)) {

                                queue_gameplay_announcement(
                                    "CHASSE",
                                    "VIANDE RECUPEREE",
                                    2.4F);
                            }
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
                    } else if (old_guard_hit.hit) {
                        // Je laisse le garde invulnerable tout en consommant le
                        // rayon : aucun coup du joueur ne traverse son corps.
                        music_.play_sfx(
                            GameSfxKind::CreatureHit,
                            0.42F);
                        record_audit_event(
                            AuditEventCategory::Creatures,
                            "old_guard_intercepted_player_attack",
                            AuditSeverity::Info,
                            audit_json_object({
                                {"guard_id", audit_json_number(old_guard_hit.guard_id)},
                                {"distance", audit_json_number(old_guard_hit.distance)},
                            }),
                            AuditPriority::Normal);
                    }
                }
            }
        }

        if (gameplay_input_enabled &&
            pending_break_block_) {
            const auto break_target = player_.current_target(world_);
            const auto tool_speed_multiplier =
                break_target.hit ? selected_tool_break_speed_multiplier(break_target.block_id) : 1.0F;
            if (const auto broken_block =
                    player_.update_block_breaking(world_, dt, true, break_target, tool_speed_multiplier);
                broken_block.has_value()) {
                record_performance_event(
                    PerformanceEventKind::BlockBreak,
                    broken_block->block,
                    inventory_item_label(broken_block->block_id));
                grant_player_experience(
                    block_break_experience(broken_block->block_id),
                    broken_block->block,
                    "block_break");
                if (maritime_session_active &&
                    sea_adventure_.collect_resource(
                        broken_block->block_id)) {

                    queue_gameplay_announcement(
                        "RESSOURCE",
                        "SOUTE MISE A JOUR",
                        2.1F);
                }
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
        if (gameplay_input_enabled &&
            pending_place_block_ &&
            !player_.is_dead()) {

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
        if (!gameplay_input_enabled) {
            pending_break_block_ = false;
            pending_primary_attack_ = false;
            pending_place_block_ = false;
            player_.cancel_block_breaking();
        }

        item_drops_.update(
            dt,
            world_,
            player_.position(),
            inventory_menu_,
            hotbar_,
            dynamic_ship);
        if (const auto item_stats = item_drops_.consume_audit_stats();
            audit_ && audit_->enabled() &&
            (item_stats.spawned != 0 || item_stats.merged != 0 || item_stats.picked_up != 0 ||
             item_stats.expired != 0)) {
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

    if (maritime_session_active) {
        // Je maintiens quatre chunks autour de L'Amelie pour que la perception
        // de 50 m des gardes ne depende jamais de l'alignement du joueur.
        creatures_.set_secondary_population_interest(
            sea_adventure_.ship_position(),
            4);
    } else {
        creatures_.clear_secondary_population_interest();
    }

    creatures_.update(dt, world_, player_.position(), environment_state, creature_cycle);
    if (maritime_session_active) {
        const auto& guard_events =
            sea_adventure_.update_old_guard_combat(
                world_,
                creatures_,
                player_,
                environment_state,
                dt);

        auto listener_right =
            glm::cross(
                player_.look_direction(),
                glm::vec3 {0.0F, 1.0F, 0.0F});
        const auto right_length_squared =
            glm::dot(listener_right, listener_right);
        if (!std::isfinite(right_length_squared) ||
            right_length_squared <= 1.0e-6F) {
            listener_right = {1.0F, 0.0F, 0.0F};
        } else {
            listener_right /=
                std::sqrt(right_length_squared);
        }

        for (const auto& shot : guard_events.shots) {
            const auto listener_delta =
                shot.muzzle_position -
                player_.eye_position();
            const auto distance_squared =
                glm::dot(listener_delta, listener_delta);
            const auto distance =
                std::isfinite(distance_squared) &&
                        distance_squared > 0.0F
                    ? std::sqrt(distance_squared)
                    : 0.0F;
            const auto direction =
                distance > 1.0e-4F
                    ? listener_delta / distance
                    : glm::vec3 {0.0F, 0.0F, 1.0F};
            const auto pan =
                std::clamp(
                    glm::dot(direction, listener_right),
                    -1.0F,
                    1.0F);
            const auto normalized_distance =
                distance / 32.0F;
            const auto attenuation =
                1.0F /
                (1.0F +
                 normalized_distance *
                     normalized_distance);
            music_.play_sfx(
                GameSfxKind::MusketShot,
                1.0F,
                pan,
                attenuation,
                static_cast<std::uint32_t>(
                    shot.sequence ^
                    (static_cast<std::uint64_t>(shot.guard_id) << 24U)));

            record_audit_event(
                AuditEventCategory::Creatures,
                "old_guard_musket_shot",
                AuditSeverity::Info,
                audit_json_object({
                    {"guard_id", audit_json_number(shot.guard_id)},
                    {"target_id", audit_json_number(shot.target_id)},
                    {"damage", audit_json_number(shot.damage)},
                }),
                AuditPriority::Normal);
        }
        for (const auto& bayonet : guard_events.bayonet_hits) {
            record_audit_event(
                AuditEventCategory::Creatures,
                "old_guard_bayonet_hit",
                AuditSeverity::Info,
                audit_json_object({
                    {"guard_id", audit_json_number(bayonet.guard_id)},
                    {"target_id", audit_json_number(bayonet.target_id)},
                    {"damage", audit_json_number(bayonet.damage)},
                }),
                AuditPriority::Normal);
        }
    }
    if (const auto creature_stats = creatures_.consume_audit_stats();
        audit_ && audit_->enabled() &&
        (creature_stats.spawned != 0 || creature_stats.despawned != 0 || creature_stats.attacks != 0)) {
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
            if (has_active_session_) {
                // Je conserve la mort et l'annulation de peche comme etat sale,
                // meme si ce tick quitte la simulation avant le marquage normal.
                mark_session_dirty();
            }
            return;
        }

        if (has_active_session_) {
            mark_session_dirty();
        }
    }
}

void Game::update_world_pipeline(FramePerformanceStats& frame_stats) {
    using clock = std::chrono::steady_clock;

    const auto capture_world_memory = [&] {
        if (should_capture_performance() &&
            frame_stats.frame_index % static_cast<std::size_t>(kWorldMemorySamplePeriodFrames) == 0U) {
            const auto memory_begin = clock::now();
            last_world_memory_ = world_.memory_stats();
            frame_stats.telemetry_ms +=
                std::chrono::duration<double, std::milli>(clock::now() - memory_begin).count();
        }
        frame_stats.world_cpu_bytes = last_world_memory_.world_cpu_bytes;
        frame_stats.mesh_cpu_bytes = last_world_memory_.mesh_cpu_bytes;
        frame_stats.override_bytes = last_world_memory_.override_bytes;
    };

    if (!options_.smoke_test && (death_screen_visible_ || paused_) && !front_end_visible()) {
        capture_world_memory();
        return;
    }

    const auto stream_start = clock::now();
    const auto active_stream_radius =
        options_.performance.adaptive_quality
            ? resolve_adaptive_stream_radius(
                  options_.performance.stream_radius,
                  renderer_.last_frame_stats().resolved_quality)
            : options_.performance.stream_radius;
    const auto stream_stats =
        world_.update_streaming(
            streaming_focus_position(),
            active_stream_radius);
    frame_stats.streaming_ms +=
        std::chrono::duration<double, std::milli>(clock::now() - stream_start).count();
    frame_stats.stream_chunk_changes += stream_stats.chunk_changed ? 1U : 0U;
    frame_stats.generation_enqueued += stream_stats.generation_enqueued;
    frame_stats.generation_pruned += stream_stats.generation_pruned;
    frame_stats.unloaded_chunks += stream_stats.unloaded_chunks;
    if (audit_ && audit_->enabled() &&
        (stream_stats.chunk_changed || stream_stats.generation_enqueued != 0 || stream_stats.generation_pruned != 0 ||
         stream_stats.unloaded_chunks != 0)) {
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
    frame_stats.fluid_ms += world_stats.fluid_ms;
    frame_stats.lighting_ms += world_stats.lighting_ms;
    frame_stats.meshing_ms += world_stats.meshing_ms;
    frame_stats.generated_chunks += world_stats.generated_chunks;
    frame_stats.meshed_chunks += world_stats.meshed_chunks;
    frame_stats.light_nodes_processed += world_stats.light_nodes_processed;
    frame_stats.processed_fluid_cells += world_stats.processed_fluid_cells;
    frame_stats.pending_generation = std::max(frame_stats.pending_generation, world_stats.pending_generation);
    frame_stats.pending_mesh = std::max(frame_stats.pending_mesh, world_stats.pending_mesh);
    frame_stats.pending_lighting = std::max(frame_stats.pending_lighting, world_stats.pending_lighting);
    frame_stats.pending_fluid = std::max(frame_stats.pending_fluid, world_stats.pending_fluid);
    frame_stats.lighting_jobs_completed += world_stats.lighting_jobs_completed;
    capture_world_memory();
    if (audit_ && audit_->enabled() && options_.audit.mode == AuditMode::Forensic &&
        (world_stats.generated_chunks != 0 || world_stats.meshed_chunks != 0 ||
         world_stats.light_nodes_processed != 0 || world_stats.processed_fluid_cells != 0 ||
         world_stats.lighting_jobs_completed != 0 || world_stats.pending_generation != 0 ||
         world_stats.pending_fluid != 0 || world_stats.pending_mesh != 0 || world_stats.pending_lighting != 0)) {
        record_audit_event(
            AuditEventCategory::World,
            "world_work",
            AuditSeverity::Info,
            audit_json_object({
                {"generated_chunks", audit_json_number(world_stats.generated_chunks)},
                {"meshed_chunks", audit_json_number(world_stats.meshed_chunks)},
                {"light_nodes_processed", audit_json_number(world_stats.light_nodes_processed)},
                {"processed_fluid_cells", audit_json_number(world_stats.processed_fluid_cells)},
                {"lighting_jobs_completed", audit_json_number(world_stats.lighting_jobs_completed)},
                {"pending_generation", audit_json_number(world_stats.pending_generation)},
                {"pending_fluid", audit_json_number(world_stats.pending_fluid)},
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

auto Game::can_open_command_console() const noexcept -> bool {
    return !options_.smoke_test &&
           has_active_session_ &&
           !front_end_visible() &&
           !confirm_dialog_.visible &&
           !death_screen_visible_ &&
           !paused_ &&
           !inventory_visible_;
}

void Game::set_command_console_visible(bool visible) {
    if (visible == command_console_.visible()) {
        return;
    }
    if (visible && !can_open_command_console()) {
        return;
    }

    pending_toggle_fly_ = false;
    pending_break_block_ = false;
    pending_primary_attack_ = false;
    pending_place_block_ = false;
    pending_fishing_ = false;
    pending_look_x_ = 0.0F;
    pending_look_y_ = 0.0F;
    player_.cancel_block_breaking();

    if (visible) {
        command_console_.open();
        if (command_console_.view().feedback.empty()) {
            command_console_.set_feedback(
                "SAISISSEZ UNE COMMANDE",
                false);
        }
        set_mouse_capture(false);
        refresh_command_console_text_input_rect();
        SDL_StartTextInput();
    } else {
        command_console_.close();
        SDL_StopTextInput();
        if (has_active_session_ &&
            !death_screen_visible_ &&
            !paused_ &&
            !inventory_visible_ &&
            !confirm_dialog_.visible &&
            !front_end_visible()) {
            set_mouse_capture(true);
        }
    }

    record_audit_event(
        AuditEventCategory::Ui,
        visible
            ? "command_console_opened"
            : "command_console_closed",
        AuditSeverity::Info,
        audit_json_object({
            {"visible", audit_json_bool(visible)},
        }),
        AuditPriority::High);
}

void Game::refresh_command_console_text_input_rect() noexcept {
    const auto layout =
        build_command_console_layout(
            window_width_,
            window_height_);
    const SDL_Rect input_rect {
        static_cast<int>(
            std::lround(layout.input_x)),
        static_cast<int>(
            std::lround(layout.input_y)),
        std::max(
            static_cast<int>(
                std::lround(layout.input_width)),
            1),
        std::max(
            static_cast<int>(
                std::lround(layout.input_height)),
            1),
    };
    SDL_SetTextInputRect(&input_rect);
}

void Game::handle_command_console_keydown(
    const SDL_KeyboardEvent& event) {
    const auto key = event.keysym.sym;
    const auto repeated =
        event.repeat != 0;

    switch (key) {
    case SDLK_ESCAPE:
        if (!repeated) {
            set_command_console_visible(false);
        }
        return;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (!repeated) {
            submit_command_console();
        }
        return;
    case SDLK_BACKSPACE:
        command_console_.backspace();
        return;
    case SDLK_DELETE:
        command_console_.delete_forward();
        return;
    case SDLK_LEFT:
        command_console_.move_cursor_left();
        return;
    case SDLK_RIGHT:
        command_console_.move_cursor_right();
        return;
    case SDLK_HOME:
        command_console_.move_cursor_home();
        return;
    case SDLK_END:
        command_console_.move_cursor_end();
        return;
    case SDLK_UP:
        command_console_.show_previous_history();
        return;
    case SDLK_DOWN:
        command_console_.show_next_history();
        return;
    default:
        break;
    }

    if (!repeated &&
        (event.keysym.mod & KMOD_CTRL) != 0 &&
        event.keysym.scancode == SDL_SCANCODE_V) {
        if (auto* clipboard_text =
                SDL_GetClipboardText();
            clipboard_text != nullptr) {
            command_console_.insert_text(
                clipboard_text);
            SDL_free(clipboard_text);
        }
    }
}

void Game::submit_command_console() {
    const auto parsed =
        command_console_.submit();
    switch (parsed.status) {
    case CommandConsoleParseStatus::Empty:
        command_console_.set_feedback(
            "SAISISSEZ UNE COMMANDE",
            true);
        return;
    case CommandConsoleParseStatus::InvalidUsage:
        command_console_.set_feedback(
            "UTILISATION : /METEO TEMPETE",
            true);
        return;
    case CommandConsoleParseStatus::UnknownCommand:
        command_console_.set_feedback(
            "COMMANDE INCONNUE",
            true);
        return;
    case CommandConsoleParseStatus::Ready:
        break;
    }

    if (parsed.command !=
        CommandConsoleCommand::StartTempest) {
        command_console_.set_feedback(
            "COMMANDE INCONNUE",
            true);
        return;
    }

    if (!environment_.start_weather_event(
            WeatherKind::Tempest)) {
        command_console_.set_feedback(
            "IMPOSSIBLE DE LANCER LA TEMPETE",
            true);
        return;
    }

    // Je modifie l'horloge meteo persistante : tout le rendu, la musique et
    // la simulation oceanique recoivent la Tempest des la prochaine image.
    command_console_.set_feedback(
        "TEMPETE LANCEE",
        false);
    queue_gameplay_announcement(
        "TEMPETE",
        "LA HOULE SE LEVE",
        3.0F);
    mark_session_dirty();
    record_audit_event(
        AuditEventCategory::InputAction,
        "command_console_tempest_started",
        AuditSeverity::Info,
        audit_json_object({
            {
                "weather_time_seconds",
                audit_json_number(
                    environment_.weather_time_seconds()),
            },
        }),
        AuditPriority::High);
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
    if (death_screen_visible_ &&
        command_console_.visible()) {
        set_command_console_visible(false);
    }
    pending_toggle_fly_ = false;
    pending_break_block_ = false;
    pending_primary_attack_ = false;
    pending_place_block_ = false;
    pending_fishing_ = false;
    player_.cancel_block_breaking();

    if (death_screen_visible_) {
        if (active_game_mode_ == GameMode::SeaAdventure) {
            sea_adventure_.cancel_fishing();
        }
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
    if (!paused_ &&
        !inventory_visible_ &&
        !command_console_.visible()) {
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
    if (paused_ &&
        command_console_.visible()) {
        set_command_console_visible(false);
    }
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
    } else if (!inventory_visible_ &&
               !command_console_.visible()) {
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
    if (inventory_visible_ &&
        command_console_.visible()) {
        set_command_console_visible(false);
    }
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
    if (!paused_ &&
        !command_console_.visible()) {
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
    if (confirm_dialog_.visible &&
        command_console_.visible()) {
        set_command_console_visible(false);
    }

    if (confirm_dialog_.visible) {
        set_mouse_capture(false);
        center_ui_cursor(window_, window_width_, window_height_, confirm_dialog_.cursor_x, confirm_dialog_.cursor_y);
        refresh_confirm_dialog_hover();
    } else if (!death_screen_visible_ &&
               !inventory_visible_ &&
               !paused_ &&
               !command_console_.visible() &&
               !front_end_visible()) {
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
        open_save_slot_menu(SaveSlotMenuMode::NewGame, SaveSlotMenuParent::MainMenu, GameMode::ClassicAdventure);
        break;
    case MainMenuAction::SeaAdventure:
        open_save_slot_menu(SaveSlotMenuMode::NewGame, SaveSlotMenuParent::MainMenu, GameMode::SeaAdventure);
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
    switch (resolve_save_slot_primary_action(save_slot_menu_, slot_index, session_save_state_.dirty())) {
    case SaveSlotPrimaryAction::StartNewGame:
        start_new_game_in_slot(slot_index, save_slot_menu_.new_game_mode);
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
            start_new_game_in_slot(*slot_index, save_slot_menu_.new_game_mode);
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
            finish_pending_save(true);
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

void Game::craft_inventory_tool(BlockId tool_id) {
    if (!inventory_visible_) {
        return;
    }

    const auto item_id = block_item_id(tool_id);
    if (!is_tool_item(item_id)) {
        return;
    }

    const auto crafted = inventory_craft_tool(inventory_menu_, hotbar_, item_id);
    const auto item_label = std::string(inventory_item_label(item_id));
    queue_gameplay_announcement(
        crafted ? "CRAFT" : "CRAFT IMPOSSIBLE",
        crafted ? item_label + " AJOUTEE" : "3 BOIS OU PLANCHES ET UNE PLACE",
        2.6F);
    record_audit_event(
        AuditEventCategory::InputAction,
        "inventory_craft_tool",
        AuditSeverity::Info,
        audit_json_object({
            {"tool", audit_json_string(item_label)},
            {"crafted", audit_json_bool(crafted)},
        }),
        AuditPriority::Normal);

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

auto Game::current_maritime_hud_view() const noexcept -> MaritimeHudView {
    if (active_game_mode_ != GameMode::SeaAdventure || !sea_adventure_.active()) {
        return {};
    }

    const auto state = sea_adventure_.hud_state(player_);
    MaritimeHudView view {};
    view.visible = state.visible;
    view.on_ship = state.on_ship;
    view.fishing_active = state.fishing_active;
    view.danger = state.danger;
    view.moored = state.phase == SeaVoyagePhase::Moored;
    view.departing = state.phase == SeaVoyagePhase::Departing;
    view.hunger_ratio = state.hunger_ratio;
    view.thirst_ratio = state.thirst_ratio;
    view.stamina_ratio = state.stamina_ratio;
    view.fishing_ratio = state.fishing_ratio;
    view.ship_distance = state.ship_distance;
    view.ship_speed = state.ship_speed;
    view.departure_seconds_remaining = state.departure_seconds_remaining;
    view.food_rations = state.food_rations;
    view.water_flasks = state.water_flasks;
    view.fish = state.fish;

    const auto& focus = state.crew_focus;
    const auto guard_has_priority =
        state.old_guard_focus.visible &&
        (!focus.visible ||
         state.old_guard_focus.distance <=
             focus.distance);
    if (guard_has_priority) {
        view.crew_focus_visible = true;
        view.crew_moving = false;
        view.crew_blocked = false;
        view.crew_knocked_out = false;
        view.crew_has_progress = false;
        view.crew_role = "VIEILLE GARDE - PROTECTEUR";
        view.crew_activity = "SURVEILLANCE DU NAVIRE";
        view.crew_health_ratio = 1.0F;
        view.crew_distance =
            state.old_guard_focus.distance;

        const auto guard_members =
            sea_adventure_.old_guard_members();
        const auto guard_id =
            static_cast<std::size_t>(
                state.old_guard_focus.guard_id);
        if (guard_id < guard_members.size()) {
            const auto& guard = guard_members[guard_id];
            view.crew_moving =
                guard.action == OldGuardAction::Patrol;
            switch (guard.action) {
            case OldGuardAction::Patrol:
                view.crew_activity = "RONDE SUR LE PONT";
                break;
            case OldGuardAction::Watch:
                view.crew_activity = "SURVEILLANCE DU NAVIRE";
                break;
            case OldGuardAction::RaiseMusket:
                view.crew_activity = "MISE EN JOUE";
                break;
            case OldGuardAction::StabilizeAim:
                view.crew_activity = "VISEE STABILISEE";
                break;
            case OldGuardAction::Fire:
                view.crew_activity = "FEU";
                break;
            case OldGuardAction::Reload:
                view.crew_activity = "RECHARGEMENT DU MOUSQUET";
                view.crew_has_progress = true;
                view.crew_progress_ratio =
                    std::clamp(
                        1.0F -
                            guard.reload_remaining /
                                kOldGuardReloadSeconds,
                        0.0F,
                        1.0F);
                break;
            case OldGuardAction::Bayonet:
                view.crew_activity = "DEFENSE A LA BAIONNETTE";
                break;
            }
        }
        return view;
    }

    view.crew_focus_visible = focus.visible;
    view.crew_moving = focus.moving;
    view.crew_blocked = focus.blocked;
    view.crew_knocked_out = focus.knocked_out;
    view.crew_has_progress = focus.has_progress;
    view.crew_role = ship_crew_role_label(focus.role);
    view.crew_activity = ship_crew_activity_label(focus.activity);
    view.crew_cargo = ship_crew_cargo_label(focus.cargo);
    view.crew_destination = ship_crew_station_label(focus.destination_station);
    view.crew_progress_ratio = focus.progress_ratio;
    view.crew_health_ratio = focus.health_ratio;
    view.crew_distance = focus.distance;
    return view;
}

void Game::sync_selected_hotbar_slot() noexcept {
    if (!progression_.has_super_vision_power()) {
        super_vision_active_ = false;
    }

    const auto flight_allowed =
        active_game_mode_ != GameMode::SeaAdventure &&
        progression_.has_flight_power();

    if (!flight_allowed) {
        // En mer, F pilote la peche et ne peut pas servir a quitter le vol. Je
        // neutralise donc aussi les anciennes sauvegardes arrivees en mode vol.
        pending_toggle_fly_ = false;
        player_.set_fly_mode_enabled(false);
    }

    player_.set_selected_block(
        selected_hotbar_block(hotbar_));

    player_.set_damage_resistance_percent(
        inventory_equipment_resistance_percent(
            inventory_menu_) +
        progression_.damage_resistance_percent());

    player_.set_apnea_resistance_percent(
        progression_.apnea_resistance_percent());

    player_.set_fall_safety_multiplier(
        progression_.fall_safety_multiplier());

    player_.set_movement_speed_multiplier(
        progression_.movement_speed_multiplier());

    player_.set_block_break_speed_multiplier(
        progression_.block_break_speed_multiplier());
}

auto Game::selected_tool_break_speed_multiplier(BlockId target_block_id) const noexcept -> float {
    const auto& selected_slot = hotbar_.selected_slot();
    if (!inventory_slot_has_item(selected_slot)) {
        return 1.0F;
    }
    return tool_break_speed_multiplier(selected_slot.block_id, target_block_id);
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
    return find_initial_spawn_position(
        world_,
        starting_village_enabled_ ? &starting_village_ : nullptr);
}

auto Game::find_initial_spawn_position(
    World& world,
    const StartingVillageLayout* starting_village) -> glm::vec3 {
    if (starting_village != nullptr && !starting_village->buildings.empty()) {
        return starting_village->player_spawn;
    }

    constexpr int kSpawnSearchRadius = 12;

    for (int radius = 0; radius <= kSpawnSearchRadius; ++radius) {
        for (int z = -radius; z <= radius; ++z) {
            for (int x = -radius; x <= radius; ++x) {
                if (radius > 0 && std::abs(x) != radius && std::abs(z) != radius) {
                    continue;
                }

                const auto surface_y = world.surface_height(x, z);
                if (world.has_water(x, surface_y + 1, z)) {
                    continue;
                }
                if (world.get_block(x, surface_y + 1, z) != to_block_id(BlockType::Air)) {
                    continue;
                }
                if (!is_world_y_valid(surface_y + 2) || world.get_block(x, surface_y + 2, z) != to_block_id(BlockType::Air)) {
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

    const auto spawn_y = static_cast<float>(world.surface_height(0, 0)) + 1.001F;
    return {0.5F, spawn_y, 0.5F};
}

void Game::respawn_player() {
    const auto maritime_respawn =
        active_game_mode_ == GameMode::SeaAdventure &&
        sea_adventure_.active();

    if (maritime_respawn) {
        // Le voyage continue, mais les jauges transitoires doivent laisser au
        // joueur une fenetre reelle pour reprendre le controle apres sa mort.
        sea_adventure_.on_player_respawn();
    }

    spawn_position_ =
        maritime_respawn
            ? sea_adventure_.deck_spawn_position()
            : find_initial_spawn_position();

    player_.respawn(spawn_position_);
    sync_selected_hotbar_slot();
    set_death_screen_visible(false);

    (void)world_.update_streaming(
        player_.position());

    creatures_.update(
        0.0F,
        world_,
        player_.position(),
        environment_.current_state(),
        environment_.current_creature_cycle());

    record_audit_event(
        AuditEventCategory::Player,
        "respawn",
        AuditSeverity::Info,
        audit_json_object({
            {
                "x",
                audit_json_number(spawn_position_.x),
            },
            {
                "y",
                audit_json_number(spawn_position_.y),
            },
            {
                "z",
                audit_json_number(spawn_position_.z),
            },
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
    if (command_console_.visible()) {
        return UiScreen::CommandConsole;
    }
    return UiScreen::Gameplay;
}

auto Game::front_end_visible() const noexcept -> bool {
    return main_menu_.visible ||
           (save_slot_menu_.visible && save_slot_menu_.parent == SaveSlotMenuParent::MainMenu) ||
           (options_menu_.visible && options_menu_.parent == OptionsMenuParent::MainMenu);
}

auto Game::gameplay_interaction_blocked() const noexcept -> bool {
    return death_screen_visible_ ||
           paused_ ||
           inventory_visible_ ||
           command_console_.visible() ||
           confirm_dialog_.visible ||
           front_end_visible();
}

auto Game::render_player() const noexcept -> const PlayerController& {
    if (options_.smoke_test && options_.smoke_ship_view != SmokeShipView::None &&
        active_game_mode_ == GameMode::SeaAdventure && sea_adventure_.active()) {
        return preview_player_;
    }
    // Je fais suivre au smoke son joueur mobile, meme si le menu de demarrage
    // reste affiche, afin de tester le rendu du meme monde que le streaming.
    return !options_.smoke_test && front_end_visible() ? preview_player_ : player_;
}

auto Game::streaming_focus_position() const noexcept -> glm::vec3 {
    if (options_.smoke_test && options_.smoke_ship_view != SmokeShipView::None &&
        active_game_mode_ == GameMode::SeaAdventure && sea_adventure_.active()) {
        return preview_player_.position();
    }
    return !options_.smoke_test && front_end_visible() ? preview_player_.position() : player_.position();
}

auto Game::current_renderer_options() const noexcept -> RendererOptions {
    RendererOptions renderer_options {};
    renderer_options.shadows_enabled = runtime_shadows_enabled_;
    renderer_options.shadow_map_size = options_.performance.shadow_map_size;
    renderer_options.post_process_enabled = runtime_post_process_enabled_;
    renderer_options.collect_detailed_stats = should_capture_performance();
    renderer_options.quality = options_.performance.adaptive_quality ? RendererQuality::Dynamic : RendererQuality::High;
    renderer_options.visual_pipeline = options_.visual_pipeline;
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
    snapshot.metadata.exists = true;
    snapshot.metadata.seed = world_.seed();
    snapshot.metadata.time_of_day = environment_.time_of_day();
    snapshot.metadata.weather_time_seconds = environment_.weather_time_seconds();
    snapshot.metadata.has_starting_village = starting_village_enabled_;
    snapshot.metadata.game_mode = active_game_mode_;
    const auto save_active_ship =
        active_game_mode_ == GameMode::SeaAdventure &&
        sea_adventure_.active();
    // Je sauvegarde le point de retour avec le navire courant, mais dans sa
    // pose neutre persistante afin qu'il reste exact apres le rechargement.
    snapshot.spawn_position =
        save_active_ship
            ? sea_adventure_.ship_entity().world_point_in_persisted_neutral_pose(
                  sea_adventure_.deck_spawn_position())
            : spawn_position_;
    snapshot.player_state = player_.state();
    snapshot.progression = progression_.state();
    snapshot.sea_adventure = sea_adventure_.save_state();
    snapshot.hotbar = hotbar_;
    snapshot.inventory = inventory_menu_;
    snapshot.inventory.visible = false;
    snapshot.inventory.hovered_slot.reset();
    snapshot.creatures.assign(creatures_.active_creatures().begin(), creatures_.active_creatures().end());
    snapshot.item_drops = item_drops_.drops();
    if (save_active_ship) {
        const auto& ship = sea_adventure_.ship_entity();
        (void)normalize_supported_player_for_ship_save(
            ship,
            snapshot.player_state,
            player_.is_climbing_dynamic_obstacle());
        for (auto& drop : snapshot.item_drops) {
            (void)normalize_supported_item_drop_for_ship_save(
                ship,
                drop);
        }
    }
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

auto Game::active_generation_profile() const noexcept -> WorldGenerationProfile {
    return world_.generation_profile();
}

void Game::apply_renderer_options() {
    if (!renderer_.initialize(current_renderer_options())) {
        throw std::runtime_error("Unable to reconfigure renderer options");
    }

    world_.enqueue_loaded_mesh_uploads();
    // Je garde le reset graphique reactif: le reste des uploads partira sur les frames suivantes.
    renderer_.drain_pending_world_meshes(world_, 32U, 2.0);
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

void Game::begin_loading_screen(LoadingScreenTheme theme, std::uint32_t quote_seed) noexcept {
    loading_theme_ = theme;
    loading_quote_seed_ = quote_seed;
    loading_progress_.reset();
    loading_started_at_ = std::chrono::steady_clock::now();
    loading_last_presented_at_ = loading_started_at_ - std::chrono::seconds(1);
    loading_last_presented_progress_ = -1.0F;
    loading_last_title_.clear();
    loading_last_detail_.clear();
    loading_window_title_.clear();
    loading_update_count_ = 0U;
    loading_max_step_ms_ = 0.0;
    loading_max_step_label_ = {};
    loading_active_ = true;
    loading_completed_ = false;
}

void Game::record_loading_step(
    std::string_view label,
    std::chrono::steady_clock::time_point started_at) noexcept {
    const auto elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started_at).count();
    if (elapsed_ms > loading_max_step_ms_) {
        loading_max_step_ms_ = elapsed_ms;
        loading_max_step_label_ = label;
    }
}

auto Game::prepare_ship_mesh_during_loading(
    const ShipRenderState& ship,
    std::string_view loading_title,
    bool restoring) -> bool {
    using clock = std::chrono::steady_clock;

    if (renderer_.ship_mesh_ready(ship)) {
        return true;
    }

    const auto dispatch_begin = clock::now();
    auto parts = std::vector<ShipPart> {ship.parts.begin(), ship.parts.end()};
    const auto* blueprint = ship.blueprint;
    const auto geometry_revision = ship.geometry_revision;
    const auto visual_pipeline = options_.visual_pipeline;
    auto mesh_future = std::async(
        std::launch::async,
        [parts = std::move(parts), blueprint, geometry_revision, visual_pipeline] {
            if (visual_pipeline == VisualPipeline::ModernStylized &&
                blueprint != nullptr) {
                // Je construis la coque organique hors du thread OpenGL tout en
                // conservant exactement le plan logique et sa revision.
                auto local_blueprint = *blueprint;
                local_blueprint.parts = std::span<const ShipPart> {parts};
                local_blueprint.geometry_revision = geometry_revision;
                return build_stylized_ship_mesh(
                           local_blueprint,
                           StylizedShipLod::Near)
                    .mesh;
            }
            return build_ship_mesh_data(std::span<const ShipPart> {parts});
        });
    record_loading_step(
        restoring ? "ship_mesh_restore_dispatch" : "ship_mesh_dispatch",
        dispatch_begin);

    // Je laisse le calcul geometrique lourd au worker tout en continuant a
    // traiter SDL et a presenter une progression vivante sur le thread principal.
    while (running_ && mesh_future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
        update_loading_screen(
            loading_title,
            "ASSEMBLAGE DU NAVIRE",
            LoadingPhase::ShipPreparation,
            0.65F);
    }
    if (!running_) {
        mesh_future.wait();
        return false;
    }

    auto mesh = mesh_future.get();
    const auto upload_begin = clock::now();
    const auto ready = renderer_.upload_prepared_ship_mesh(ship, mesh);
    record_loading_step(
        restoring ? "ship_mesh_restore_upload" : "ship_mesh_upload",
        upload_begin);
    return ready;
}

void Game::update_loading_screen(std::string_view title,
                                 std::string_view detail,
                                 LoadingPhase phase,
                                 float local_progress,
                                 bool force) {
    if (!loading_active_) {
        begin_loading_screen(LoadingScreenTheme::Standard, 0U);
    }
    const auto progress = loading_progress_.update(phase, local_progress);
    present_loading_screen(title, detail, progress, force);
}

void Game::present_loading_screen(std::string_view title,
                                  std::string_view detail,
                                  float progress,
                                  bool force) {
    const auto presentation_begin = std::chrono::steady_clock::now();
    if (!loading_active_) {
        begin_loading_screen(LoadingScreenTheme::Standard, 0U);
    }

    pump_loading_events();
    if (!running_) {
        return;
    }

    const auto monotone_progress = loading_progress_.update_absolute(progress);
    const auto now = std::chrono::steady_clock::now();
    const auto phase_changed = title != loading_last_title_ || detail != loading_last_detail_;
    const auto cadence_elapsed = now - loading_last_presented_at_ >= std::chrono::milliseconds(33);
    const auto progress_advanced = monotone_progress - loading_last_presented_progress_ >= 0.01F;

    if (!force && !phase_changed && !cadence_elapsed && !progress_advanced) {
        return;
    }

    loading_last_title_.assign(title);
    loading_last_detail_.assign(detail);
    loading_last_presented_progress_ = monotone_progress;
    loading_last_presented_at_ = now;

    if (window_ == nullptr) {
        return;
    }

    if (phase_changed || loading_window_title_.empty()) {
        loading_window_title_.assign(kGameWindowTitle);
        if (!detail.empty()) {
            loading_window_title_ += " - ";
            loading_window_title_.append(detail);
        }
        SDL_SetWindowTitle(window_, loading_window_title_.c_str());
    }

    const auto elapsed_seconds = std::chrono::duration<double>(now - loading_started_at_).count();
    const auto quotes = loading_theme_ == LoadingScreenTheme::Maritime
                            ? make_maritime_loading_quote_view(loading_quote_seed_, elapsed_seconds)
                            : LoadingQuoteSelection {};
    LoadingScreenView view {};
    view.title = title;
    view.detail = detail;
    view.progress = monotone_progress;
    view.theme = loading_theme_;
    view.current_quote = quotes.current;
    view.next_quote = quotes.next;
    view.quote_blend = quotes.blend;
    view.animation_phase = loading_animation_phase(elapsed_seconds);
    renderer_.render_loading_screen(view, window_width_, window_height_);
    // Je mesure le travail produit par le jeu avant le swap : l'attente VSync
    // depend du pilote et ne represente pas une tranche CPU de chargement.
    record_loading_step("loading_present", presentation_begin);
    SDL_GL_SwapWindow(window_);
    ++loading_update_count_;
}

void Game::complete_loading_screen(std::string_view title, std::string_view detail) {
    const auto completed_progress = loading_progress_.complete();
    present_loading_screen(title, detail, completed_progress, true);
    loading_completed_ = true;
    loading_active_ = false;
}

auto Game::preload_readiness(const World& world, const glm::vec3& focus, int radius) const -> float {
    const auto target_radius = std::max(radius, 0);
    const auto center = world.world_to_chunk(
        static_cast<int>(std::floor(focus.x)),
        static_cast<int>(std::floor(focus.z)));

    auto total_chunks = 0;
    auto ready_chunks = 0;
    for (int dz = -target_radius; dz <= target_radius; ++dz) {
        for (int dx = -target_radius; dx <= target_radius; ++dx) {
            ++total_chunks;
            const ChunkCoord coord {center.x + dx, center.z + dz};
            const auto* chunk = world.find_chunk(coord);
            if (chunk == nullptr) {
                continue;
            }
            if (world.mesh_revision(coord) == 0 || chunk->is_dirty() || chunk->is_lighting_dirty()) {
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

auto Game::preload_gpu_readiness(const World& world, const glm::vec3& focus, int radius) const -> float {
    const auto target_radius = std::max(radius, 0);
    const auto center = world.world_to_chunk(
        static_cast<int>(std::floor(focus.x)),
        static_cast<int>(std::floor(focus.z)));

    auto total_chunks = 0;
    auto ready_chunks = 0;
    for (int dz = -target_radius; dz <= target_radius; ++dz) {
        for (int dx = -target_radius; dx <= target_radius; ++dx) {
            ++total_chunks;
            const ChunkCoord coord {center.x + dx, center.z + dz};
            const auto* chunk = world.find_chunk(coord);
            const auto revision = world.mesh_revision(coord);
            if (chunk != nullptr && !chunk->is_dirty() && !chunk->is_lighting_dirty() && revision != 0U &&
                renderer_.world_mesh_uploaded(coord, revision)) {
                ++ready_chunks;
            }
        }
    }
    return total_chunks == 0
               ? 1.0F
               : static_cast<float>(ready_chunks) / static_cast<float>(total_chunks);
}

void Game::refresh_save_slots() {
    if (save_root_directory_.empty()) {
        return;
    }

    save_slot_menu_.slots = scan_save_slots(save_root_directory_);
    save_slot_menu_.active_slot = active_save_slot_;
}

auto Game::finish_pending_save(bool wait_for_completion) -> bool {
    if (!pending_save_.valid()) {
        return session_save_state_.transition_allowed();
    }
    if (!wait_for_completion && pending_save_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return true;
    }

    const auto completed_slot = pending_save_slot_;
    try {
        pending_save_.get();
    } catch (const std::exception& exception) {
        pending_save_slot_.reset();
        session_save_state_.fail_save();
        try {
            record_audit_event(
                AuditEventCategory::Session,
                "game_save_failed",
                AuditSeverity::Error,
                audit_json_object({
                    {"message", audit_json_string(exception.what())},
                }),
                AuditPriority::Critical);
            if (audit_) {
                audit_->record_error(std::string("Background save failed: ") + exception.what());
            }
        } catch (...) {
            // Je ne masque jamais l'etat d'echec de la sauvegarde si la
            // telemetrie rencontre elle-meme une erreur.
        }
        std::cerr << "ValCraft save warning: " << exception.what() << std::endl;
        return false;
    }

    pending_save_slot_.reset();
    session_save_state_.complete_save();
    try {
        refresh_save_slots();
        record_audit_event(
            AuditEventCategory::Session,
            "game_save_completed",
            AuditSeverity::Info,
            audit_json_object({
                {"has_slot", audit_json_bool(completed_slot.has_value())},
                {"slot_index", audit_json_number(completed_slot.value_or(0U))},
            }),
            AuditPriority::High);
    } catch (const std::exception& exception) {
        std::cerr << "ValCraft save metadata warning: " << exception.what() << std::endl;
    }
    return true;
}

auto Game::wait_for_pending_save_during_loading(std::string_view title) -> bool {
    while (running_ && pending_save_.valid() &&
           pending_save_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        update_loading_screen(
            title,
            "SECURISATION DE LA SAUVEGARDE",
            LoadingPhase::SaveRead,
            0.0F);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!running_) {
        return false;
    }
    if (finish_pending_save(true)) {
        return true;
    }

    present_loading_screen(
        title,
        "SAUVEGARDE NON SECURISEE",
        loading_progress_.progress(),
        true);
    return false;
}

void Game::finish_pending_world_release(bool wait_for_completion) {
    if (!pending_world_release_.valid()) {
        return;
    }
    if (!wait_for_completion &&
        pending_world_release_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    try {
        pending_world_release_.get();
    } catch (const std::exception& exception) {
        // Je journalise cette anomalie sans interrompre la session : le monde
        // actif n'est jamais touche par la liberation en arriere-plan.
        record_audit_event(
            AuditEventCategory::Session,
            "world_release_failed",
            AuditSeverity::Error,
            audit_json_object({{"message", audit_json_string(exception.what())}}),
            AuditPriority::High);
    }
}

auto Game::wait_for_pending_world_release_during_loading(std::string_view title) -> bool {
    while (running_ && pending_world_release_.valid() &&
           pending_world_release_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        update_loading_screen(
            title,
            "LIBERATION DE L'ANCIEN MONDE",
            LoadingPhase::Preparation,
            0.25F);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!running_) {
        return false;
    }
    finish_pending_world_release(true);
    return true;
}

void Game::install_prepared_world(World prepared_world) {
    finish_pending_world_release(true);

    // Je garde une reference locale sur l'ancien monde jusqu'a ce que le worker
    // soit effectivement lance. Si std::async echoue, je peux donc restaurer la
    // session precedente sans creer un monde hybride.
    std::promise<void> release_signal {};
    auto release_ready = release_signal.get_future().share();
    auto retired_world = std::make_shared<World>(std::move(world_));
    auto release_future = std::future<void> {};
    try {
        release_future = std::async(
            std::launch::async,
            [retired_world, release_ready = std::move(release_ready)]() mutable {
                release_ready.wait();
                retired_world.reset();
        });
        world_ = std::move(prepared_world);
        terrain_edit_stress_.reset();
    } catch (...) {
        world_ = std::move(*retired_world);
        try {
            release_signal.set_value();
        } catch (...) {
            // Je preserve ici l'exception d'origine du lancement du worker.
        }
        throw;
    }

    pending_world_release_ = std::move(release_future);
    // Je cede la derniere reference de l'ancien monde au worker avant de le
    // reveiller; sa destruction ne peut donc plus retomber sur le thread UI.
    retired_world.reset();
    try {
        release_signal.set_value();
    } catch (...) {
        // La destruction du promise produit aussi le signal broken_promise et
        // debloque le worker; je ne propage rien apres le commit du monde.
    }
}

auto Game::reset_renderer_world_resources_during_loading(std::string_view title) -> bool {
    using clock = std::chrono::steady_clock;

    const auto begin = clock::now();
    renderer_.begin_world_resource_reset();
    record_loading_step("gpu_reset_begin", begin);
    const auto total_resources = std::max<std::size_t>(
        renderer_.pending_world_resource_reset_count(),
        1U);

    auto completed = false;
    while (running_ && !completed) {
        const auto step_begin = clock::now();
        completed = renderer_.process_world_resource_reset(8U, 2.0);
        record_loading_step("gpu_reset_slice", step_begin);
        const auto remaining = renderer_.pending_world_resource_reset_count();
        const auto progress = 1.0F - static_cast<float>(std::min(remaining, total_resources)) /
                                         static_cast<float>(total_resources);
        update_loading_screen(
            title,
            "LIBERATION DES RESSOURCES GRAPHIQUES",
            LoadingPhase::Preparation,
            progress,
            completed);
    }
    return running_ && completed;
}

void Game::prime_world_around(
    World& world,
    const glm::vec3& focus,
    std::string_view loading_title,
    std::string_view loading_detail) {
    using clock = std::chrono::steady_clock;

    const auto center = world.world_to_chunk(
        static_cast<int>(std::floor(focus.x)),
        static_cast<int>(std::floor(focus.z)));
    const auto preload_radius = std::min(
        std::max(options_.performance.spawn_preload_radius, 1),
        std::max(world.stream_radius(), 0));
    const auto side = static_cast<std::size_t>(preload_radius * 2 + 1);
    const auto target_count = side * side;
    const auto maritime = loading_theme_ == LoadingScreenTheme::Maritime;
    const auto loading_deadline = clock::now() + std::chrono::seconds(60);

    const auto for_each_target = [&](const auto& visitor) {
        for (int dz = -preload_radius; dz <= preload_radius; ++dz) {
            for (int dx = -preload_radius; dx <= preload_radius; ++dx) {
                visitor(ChunkCoord {center.x + dx, center.z + dz});
            }
        }
    };
    const auto target_ratio = [&](const auto& ready) {
        auto ready_count = std::size_t {0U};
        for_each_target([&](const ChunkCoord& coord) {
            if (ready(coord)) {
                ++ready_count;
            }
        });
        return target_count == 0U
                   ? 1.0F
                   : static_cast<float>(ready_count) / static_cast<float>(target_count);
    };
    const auto check_loading_deadline = [&] {
        if (clock::now() > loading_deadline) {
            throw std::runtime_error("Timed out while preparing the immediate world area");
        }
    };
    const auto process_world_step = [&](const WorldWorkBudget& budget) {
        const auto step_begin = clock::now();
        (void)world.process_pending_work(budget);
        record_loading_step("world_pipeline_slice", step_begin);
    };

    // Je ne mets d'abord en file que le voisinage indispensable. Les anneaux
    // exterieurs reprendront apres la premiere frame jouable.
    (void)world.update_streaming(focus, preload_radius);

    WorldWorkBudget generation_budget {};
    // Je borne la generation maritime a un chunk par tranche : deux chunks
    // consecutifs rendaient le chargement moins reactif sur les builds controles.
    generation_budget.chunk_generation_budget = maritime ? 1U : 2U;
    generation_budget.fluid_cell_budget = 0U;
    generation_budget.mesh_rebuild_budget = 0U;
    generation_budget.light_node_budget = 0U;
    generation_budget.max_generation_ms = 3.0;
    generation_budget.max_fluid_ms = 0.0;
    generation_budget.max_lighting_ms = 0.0;
    generation_budget.max_meshing_ms = 0.0;
    auto generated_ratio = target_ratio([&](const ChunkCoord& coord) {
        return world.find_chunk(coord) != nullptr;
    });
    while (running_ && generated_ratio < 1.0F) {
        check_loading_deadline();
        process_world_step(generation_budget);
        generated_ratio = target_ratio([&](const ChunkCoord& coord) {
            return world.find_chunk(coord) != nullptr;
        });
        update_loading_screen(
            loading_title,
            maritime ? std::string_view("GENERATION DE L'OCEAN") : loading_detail,
            LoadingPhase::Generation,
            generated_ratio);
    }
    if (!running_) {
        return;
    }

    WorldWorkBudget fluid_budget {};
    fluid_budget.chunk_generation_budget = 0U;
    fluid_budget.fluid_cell_budget = 1024U;
    fluid_budget.mesh_rebuild_budget = 0U;
    fluid_budget.light_node_budget = 0U;
    fluid_budget.max_generation_ms = 0.0;
    fluid_budget.max_fluid_ms = 1.0;
    fluid_budget.max_lighting_ms = 0.0;
    fluid_budget.max_meshing_ms = 0.0;
    const auto initial_fluid_work = std::max<std::size_t>(world.pending_fluid_count(), 1U);
    auto processed_fluid_work = std::size_t {0U};
    const auto fluid_warmup_deadline = clock::now() + std::chrono::seconds(2);
    while (running_ && world.pending_fluid_count() > 0U && clock::now() < fluid_warmup_deadline) {
        const auto before = world.pending_fluid_count();
        process_world_step(fluid_budget);
        const auto after = world.pending_fluid_count();
        processed_fluid_work += before > after ? before - after : 0U;
        const auto denominator = std::max(initial_fluid_work, processed_fluid_work + after);
        update_loading_screen(
            loading_title,
            maritime ? std::string_view("STABILISATION DES EAUX") : std::string_view("STABILISATION DU MONDE"),
            LoadingPhase::Fluids,
            static_cast<float>(processed_fluid_work) / static_cast<float>(denominator));
    }
    update_loading_screen(
        loading_title,
        maritime ? std::string_view("STABILISATION DES EAUX") : std::string_view("STABILISATION DU MONDE"),
        LoadingPhase::Fluids,
        1.0F,
        true);

    WorldWorkBudget lighting_budget = fluid_budget;
    lighting_budget.fluid_cell_budget = 256U;
    lighting_budget.light_node_budget = 8192U;
    lighting_budget.max_fluid_ms = 0.5;
    lighting_budget.max_lighting_ms = 3.0;
    auto lighting_ratio = target_ratio([&](const ChunkCoord& coord) {
        const auto* chunk = world.find_chunk(coord);
        return chunk != nullptr && !chunk->is_lighting_dirty();
    });
    while (running_ && lighting_ratio < 1.0F) {
        check_loading_deadline();
        process_world_step(lighting_budget);
        lighting_ratio = target_ratio([&](const ChunkCoord& coord) {
            const auto* chunk = world.find_chunk(coord);
            return chunk != nullptr && !chunk->is_lighting_dirty();
        });
        update_loading_screen(
            loading_title,
            maritime ? std::string_view("CALCUL DE LA LUMIERE") : std::string_view("ECLAIRAGE DU PAYSAGE"),
            LoadingPhase::Lighting,
            lighting_ratio);
    }

    WorldWorkBudget meshing_budget = lighting_budget;
    meshing_budget.light_node_budget = 4096U;
    meshing_budget.mesh_rebuild_budget = 2U;
    meshing_budget.max_lighting_ms = 1.0;
    meshing_budget.max_meshing_ms = 3.0;
    auto meshing_ratio = preload_readiness(world, focus, preload_radius);
    while (running_ && meshing_ratio < 1.0F) {
        check_loading_deadline();
        process_world_step(meshing_budget);
        meshing_ratio = preload_readiness(world, focus, preload_radius);
        update_loading_screen(
            loading_title,
            maritime ? std::string_view("CONSTRUCTION DE L'HORIZON") : std::string_view("CONSTRUCTION DU PAYSAGE"),
            LoadingPhase::Meshing,
            meshing_ratio);
    }

    auto gpu_ratio = target_ratio([&](const ChunkCoord& coord) {
        const auto* chunk = world.find_chunk(coord);
        const auto revision = world.mesh_revision(coord);
        return chunk != nullptr && !chunk->is_dirty() && !chunk->is_lighting_dirty() && revision != 0U &&
               renderer_.world_mesh_uploaded(coord, revision);
    });
    while (running_ && gpu_ratio < 1.0F) {
        check_loading_deadline();
        process_world_step(meshing_budget);
        const auto upload_begin = clock::now();
        renderer_.drain_pending_world_meshes(world, 4U, 2.0);
        record_loading_step("gpu_upload_slice", upload_begin);
        gpu_ratio = target_ratio([&](const ChunkCoord& coord) {
            const auto* chunk = world.find_chunk(coord);
            const auto revision = world.mesh_revision(coord);
            return chunk != nullptr && !chunk->is_dirty() && !chunk->is_lighting_dirty() && revision != 0U &&
                   renderer_.world_mesh_uploaded(coord, revision);
        });
        update_loading_screen(
            loading_title,
            "TRANSFERT VERS LE GPU",
            LoadingPhase::GpuUpload,
            gpu_ratio);
    }

    if (running_) {
        // Je relance seulement maintenant le streaming large: il ne concurrence
        // plus les neuf chunks indispensables a l'apparition du joueur.
        const auto streaming_expand_begin = clock::now();
        (void)world.update_streaming(focus);
        record_loading_step("streaming_expansion", streaming_expand_begin);
    }
}

void Game::prepare_game_session() {
    if (command_console_.visible()) {
        set_command_console_visible(false);
    }
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
    pending_fishing_ = false;
    set_mouse_capture(true);
    try {
        record_audit_event(
            AuditEventCategory::Session,
            "game_session_prepared",
            AuditSeverity::Info,
            audit_json_object({}),
            AuditPriority::Critical);
    } catch (const std::exception& exception) {
        std::cerr << "ValCraft session telemetry warning: " << exception.what() << std::endl;
    }
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
    world_ = World(
        1337,
        options_.performance.stream_radius,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        options_.visual_pipeline);
    terrain_edit_stress_.reset();
    creatures_.clear();
    item_drops_.clear();
    hotbar_ = make_default_hotbar_state();
    inventory_menu_ = make_default_inventory_menu_state();
    normalize_inventory_state(inventory_menu_, hotbar_);
    sync_selected_hotbar_slot();

    menu_preview_time_of_day_ = 8.25F;
    environment_.set_weather_seed(1337U);
    environment_.set_weather_time_seconds(
        options_.initial_weather_time_seconds);
    sync_menu_preview_environment();

    // The main menu only needs a scenic background; building the whole starting
    // village here causes a long black startup before the first frame.
    configure_starting_village(false, false);
    present_loading_screen("VALCRAFT", "POSITIONNEMENT DE LA CAMERA", 0.28F);
    spawn_position_ = find_initial_spawn_position();
    player_.respawn(spawn_position_);
    preview_player_.respawn(spawn_position_);
    prime_world_around(world_, spawn_position_, "VALCRAFT", "CHARGEMENT DU PAYSAGE");
    if (!running_) {
        return;
    }
    update_menu_preview_camera(0.0F);
    creatures_.update(0.0F, world_, spawn_position_, environment_.current_state(), environment_.current_creature_cycle());
    present_loading_screen("VALCRAFT", "FINALISATION DU MENU", 0.97F, true);
}

void Game::open_main_menu(bool from_session) {
    main_menu_.visible = true;
    if (command_console_.visible()) {
        set_command_console_visible(false);
    }
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

void Game::open_save_slot_menu(SaveSlotMenuMode mode, SaveSlotMenuParent parent, GameMode new_game_mode) {
    refresh_save_slots();
    save_slot_menu_.visible = true;
    save_slot_menu_.mode = mode;
    save_slot_menu_.parent = parent;
    save_slot_menu_.new_game_mode = new_game_mode;
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
            {"new_game_mode", audit_json_number(static_cast<int>(new_game_mode))},
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
    if (has_active_session_ && session_save_state_.dirty()) {
        set_confirm_dialog_visible(true, ConfirmDialogIntent::ReturnToMainMenu);
        return;
    }

    open_main_menu(true);
}

void Game::start_new_game_in_slot(std::size_t slot_index, GameMode game_mode) {
    using clock = std::chrono::steady_clock;

    const auto next_game_mode = is_known_game_mode(game_mode) ? game_mode : GameMode::ClassicAdventure;
    const auto sea_mode = next_game_mode == GameMode::SeaAdventure;
    const auto generation_profile = sea_mode
                                        ? WorldGenerationProfile::OceanAdventure
                                        : WorldGenerationProfile::Continental;
    const auto loading_title = sea_mode ? std::string_view("AVENTURE EN MER") : std::string_view("NOUVELLE PARTIE");
    begin_loading_screen(
        sea_mode ? LoadingScreenTheme::Maritime : LoadingScreenTheme::Standard,
        static_cast<std::uint32_t>(slot_index));
    update_loading_screen(
        loading_title,
        "OUVERTURE DU JOURNAL DE BORD",
        LoadingPhase::Preparation,
        0.02F,
        true);

    auto seed = 1337;
    if (!(options_.smoke_test && options_.smoke_session != SmokeSessionMode::Menu)) {
        seed = make_nonblocking_world_seed(slot_index);
    }
    loading_quote_seed_ = static_cast<std::uint32_t>(seed);
    auto renderer_staged = false;
    auto session_committed = false;

    update_loading_screen(
        loading_title,
        sea_mode ? std::string_view("PREPARATION DU NAVIRE") : std::string_view("CONSTRUCTION DU VILLAGE"),
        LoadingPhase::Preparation,
        0.10F,
        true);

    try {
        if (!wait_for_pending_save_during_loading(loading_title) ||
            !wait_for_pending_world_release_during_loading(loading_title)) {
            loading_active_ = false;
            SDL_SetWindowTitle(window_, kGameWindowTitle.data());
            return;
        }

        const auto preparation_begin = clock::now();
        World prepared_world(
            seed,
            options_.performance.stream_radius,
            generation_profile,
            sea_mode ? WorldGenerationVersion::SparseArchipelagoV2
                     : WorldGenerationVersion::LegacyV1,
            options_.visual_pipeline);
        SeaAdventureSystem prepared_sea_adventure {};
        StartingVillageLayout prepared_village {};
        CreatureSystem prepared_creatures {};
        const auto prepared_village_enabled = !sea_mode;
        auto preparation_finalize_begin = preparation_begin;
        if (sea_mode) {
            prepared_sea_adventure.reset(seed);
            update_loading_screen(
                loading_title,
                "CONSTRUCTION DU PORT",
                LoadingPhase::Preparation,
                0.35F,
                true);
            const StartingPortGenerator port_generator(seed);
            const auto port_layout = port_generator.build_layout();
            record_loading_step("new_session_prepare", preparation_begin);

            // Je construis les milliers d'overrides du port sur un monde encore
            // isole. Le thread principal peut ainsi continuer a pomper SDL et a
            // animer l'ecran de chargement sans tranche bloquante de plusieurs
            // centaines de millisecondes en Debug.
            auto port_future = std::async(
                std::launch::async,
                [&prepared_world, port_generator, port_layout] {
                    port_generator.apply(prepared_world, port_layout);
                });
            while (running_ && port_future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
                update_loading_screen(
                    loading_title,
                    "CONSTRUCTION DU PORT",
                    LoadingPhase::Preparation,
                    0.60F);
            }
            if (!running_) {
                port_future.wait();
                return;
            }
            port_future.get();
            preparation_finalize_begin = clock::now();
        } else {
            prepared_sea_adventure.load_state({}, seed);
            StartingVillageGenerator village_generator(seed);
            prepared_village = village_generator.build_layout();
            village_generator.apply(prepared_world, prepared_village);
            prepared_creatures.set_settlement_residents(prepared_village.residents);
        }

        auto prepared_hotbar = make_default_hotbar_state();
        auto prepared_inventory = make_default_inventory_menu_state();
        normalize_inventory_state(prepared_inventory, prepared_hotbar);
        const auto prepared_spawn = sea_mode
                                        ? prepared_sea_adventure.deck_spawn_position()
                                        : find_initial_spawn_position(prepared_world, &prepared_village);
        PlayerController prepared_player {};
        prepared_player.respawn(prepared_spawn);
        EnvironmentClock prepared_environment {};
        prepared_environment.set_time_of_day(8.25F);
        prepared_environment.set_weather_seed(static_cast<std::uint32_t>(seed));
        prepared_environment.set_weather_time_seconds(
            options_.initial_weather_time_seconds);
        prepared_environment.set_frozen(options_.freeze_time || options_.smoke_test);
        record_loading_step(
            sea_mode ? "new_session_finalize" : "new_session_prepare",
            sea_mode ? preparation_finalize_begin : preparation_begin);

        update_loading_screen(
            loading_title,
            "PREPARATION DU POINT D'APPARITION",
            LoadingPhase::LegacyMigration,
            1.0F,
            true);
        if (!reset_renderer_world_resources_during_loading(loading_title)) {
            loading_active_ = false;
            return;
        }
        renderer_staged = true;

        prime_world_around(prepared_world, prepared_spawn, loading_title, "CHARGEMENT DES CHUNKS");
        if (!running_) {
            return;
        }
        const auto creature_sync_begin = clock::now();
        prepared_creatures.update(
            0.0F,
            prepared_world,
            prepared_player.position(),
            prepared_environment.current_state(),
            prepared_environment.current_creature_cycle());
        record_loading_step("creature_initial_sync", creature_sync_begin);

        if (sea_mode) {
            update_loading_screen(
                loading_title,
                "ASSEMBLAGE DU NAVIRE",
                LoadingPhase::ShipPreparation,
                0.25F,
                true);
            const auto ship_state = prepared_sea_adventure.ship_render_state();
            const auto ship_ready = prepare_ship_mesh_during_loading(ship_state, loading_title, false);
            if (!running_) {
                return;
            }
            if (!ship_ready) {
                throw std::runtime_error("Unable to prepare the maritime ship mesh");
            }
        }
        update_loading_screen(
            loading_title,
            sea_mode ? std::string_view("ASSEMBLAGE DU NAVIRE") : std::string_view("INITIALISATION DU RENDU"),
            LoadingPhase::ShipPreparation,
            1.0F,
            true);

        const auto commit_begin = clock::now();
        install_prepared_world(std::move(prepared_world));
        // Je considere le remplacement du monde comme la frontiere de commit :
        // l'ancien monde est desormais libere en arriere-plan et ne peut plus
        // servir de repli si une etape de finalisation echoue ensuite.
        session_committed = true;
        active_game_mode_ = next_game_mode;
        sea_adventure_ = std::move(prepared_sea_adventure);
        creatures_ = std::move(prepared_creatures);
        item_drops_.clear();
        progression_.reset();
        hotbar_ = std::move(prepared_hotbar);
        inventory_menu_ = std::move(prepared_inventory);
        player_ = std::move(prepared_player);
        environment_ = prepared_environment;
        starting_village_enabled_ = prepared_village_enabled;
        starting_village_ = std::move(prepared_village);
        spawn_position_ = prepared_spawn;
        super_vision_active_ = false;
        gameplay_announcements_.clear();
        sync_selected_hotbar_slot();
        preview_orbit_radians_ = 0.0F;
        update_menu_preview_camera(0.0F);
        has_active_session_ = true;
        active_save_slot_ = slot_index;
        session_save_state_.reset_clean();
        prepare_game_session();
        record_loading_step("new_session_commit", commit_begin);

        update_loading_screen(
            loading_title,
            "OUVERTURE DU JOURNAL DE BORD",
            LoadingPhase::Finalization,
            0.10F,
            true);
        if (!wait_for_pending_world_release_during_loading(loading_title)) {
            loading_active_ = false;
            SDL_SetWindowTitle(window_, kGameWindowTitle.data());
            return;
        }
        const auto save_begin = clock::now();
        save_game_to_slot(slot_index);
        record_loading_step("initial_save_capture", save_begin);
        if (!wait_for_pending_save_during_loading(loading_title)) {
            loading_active_ = false;
            SDL_SetWindowTitle(window_, kGameWindowTitle.data());
            return;
        }
        update_loading_screen(
            loading_title,
            "JOURNAL DE BORD SECURISE",
            LoadingPhase::Finalization,
            1.0F,
            true);
        complete_loading_screen(
            loading_title,
            sea_mode ? std::string_view("PRET A LARGUER LES AMARRES") : std::string_view("AVENTURE PRETE"));
        SDL_SetWindowTitle(window_, kGameWindowTitle.data());
        record_audit_event(
            AuditEventCategory::Session,
            "new_game_started",
            AuditSeverity::Info,
            audit_json_object({
                {"slot_index", audit_json_number(slot_index)},
                {"seed", audit_json_number(world_.seed())},
                {"game_mode", audit_json_number(static_cast<int>(active_game_mode_))},
            }),
            AuditPriority::Critical);
    } catch (const std::exception& exception) {
        if (renderer_staged && !session_committed) {
            renderer_.reset_world_resources();
            world_.enqueue_loaded_mesh_uploads();
        }
        try {
            record_audit_event(
                AuditEventCategory::Session,
                session_committed ? "new_game_finalize_failed" : "new_game_start_failed",
                AuditSeverity::Error,
                audit_json_object({{"message", audit_json_string(exception.what())}}),
                AuditPriority::Critical);
        } catch (...) {
            // Je ne laisse pas une erreur de telemetrie masquer le resultat de
            // la transition de session.
        }
        loading_active_ = false;
        if (!session_committed) {
            loading_completed_ = false;
        }
        SDL_SetWindowTitle(window_, kGameWindowTitle.data());
    }
}

auto Game::load_game_from_slot(std::size_t slot_index) -> bool {
    struct AsyncSaveProgress {
        std::atomic<std::uint8_t> phase {static_cast<std::uint8_t>(SaveLoadPhase::OpeningFile)};
        std::atomic<float> normalized {0.0F};
        std::atomic<bool> cancelled {false};
    };

    const auto metadata = slot_index < save_slot_menu_.slots.size()
                              ? save_slot_menu_.slots[slot_index]
                              : SaveSlotMetadata {};
    const auto expected_sea_mode = metadata.exists && metadata.game_mode == GameMode::SeaAdventure;
    begin_loading_screen(
        expected_sea_mode ? LoadingScreenTheme::Maritime : LoadingScreenTheme::Standard,
        static_cast<std::uint32_t>(metadata.seed));
    const auto loading_title = expected_sea_mode ? std::string_view("AVENTURE EN MER") : std::string_view("CHARGEMENT");
    update_loading_screen(
        loading_title,
        "OUVERTURE DU JOURNAL DE BORD",
        LoadingPhase::Preparation,
        1.0F,
        true);
    if (!wait_for_pending_save_during_loading(loading_title)) {
        loading_active_ = false;
        SDL_SetWindowTitle(window_, kGameWindowTitle.data());
        return false;
    }

    auto async_progress = std::shared_ptr<AsyncSaveProgress> {};
    auto load_future = std::future<std::optional<SaveGameSnapshot>> {};
    try {
        async_progress = std::make_shared<AsyncSaveProgress>();
        const auto save_root = save_root_directory_;
        load_future = std::async(
            std::launch::async,
            [save_root, slot_index, async_progress] {
                return load_save_slot(
                    save_root,
                    slot_index,
                    [async_progress](const SaveLoadProgress& progress) {
                        async_progress->phase.store(
                            static_cast<std::uint8_t>(progress.phase),
                            std::memory_order_relaxed);
                        async_progress->normalized.store(progress.normalized, std::memory_order_relaxed);
                        return async_progress->cancelled.load(std::memory_order_relaxed)
                                   ? SaveLoadControl::Cancel
                                   : SaveLoadControl::Continue;
                    });
            });
    } catch (const std::exception& exception) {
        try {
            record_audit_event(
                AuditEventCategory::Session,
                "game_load_worker_start_failed",
                AuditSeverity::Error,
                audit_json_object({{"message", audit_json_string(exception.what())}}),
                AuditPriority::Critical);
        } catch (...) {
            // Je rends toujours la main proprement, meme si l'audit est sature.
        }
        present_loading_screen(
            loading_title,
            "LECTURE IMPOSSIBLE",
            loading_progress_.progress(),
            true);
        loading_active_ = false;
        SDL_SetWindowTitle(window_, kGameWindowTitle.data());
        return false;
    }

    while (load_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        if (!running_) {
            async_progress->cancelled.store(true, std::memory_order_relaxed);
        } else {
            const auto phase = static_cast<SaveLoadPhase>(async_progress->phase.load(std::memory_order_relaxed));
            const auto detail = phase == SaveLoadPhase::ReadingWorld
                                    ? std::string_view("LECTURE DE LA CARTE")
                                    : phase == SaveLoadPhase::ReadingEntities
                                          ? std::string_view("LECTURE DU JOURNAL DE BORD")
                                          : std::string_view("LECTURE DE LA SAUVEGARDE");
            update_loading_screen(
                loading_title,
                detail,
                LoadingPhase::SaveRead,
                async_progress->normalized.load(std::memory_order_relaxed));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto snapshot = std::optional<SaveGameSnapshot> {};
    try {
        snapshot = load_future.get();
    } catch (const std::exception& exception) {
        record_audit_event(
            AuditEventCategory::Session,
            "game_load_failed",
            AuditSeverity::Error,
            audit_json_object({{"message", audit_json_string(exception.what())}}),
            AuditPriority::Critical);
        present_loading_screen(
            loading_title,
            "ERREUR DE LECTURE",
            loading_progress_.progress(),
            true);
        loading_active_ = false;
        SDL_SetWindowTitle(window_, kGameWindowTitle.data());
        return false;
    }
    if (!running_) {
        loading_active_ = false;
        return false;
    }
    if (!snapshot.has_value()) {
        present_loading_screen(
            loading_title,
            "SAUVEGARDE INVALIDE",
            loading_progress_.progress(),
            true);
        loading_active_ = false;
        SDL_SetWindowTitle(window_, kGameWindowTitle.data());
        refresh_save_slots();
        return false;
    }

    const auto actual_sea_mode = snapshot->metadata.game_mode == GameMode::SeaAdventure;
    loading_theme_ = actual_sea_mode ? LoadingScreenTheme::Maritime : LoadingScreenTheme::Standard;
    loading_quote_seed_ = static_cast<std::uint32_t>(snapshot->metadata.seed);
    try {
        return load_snapshot_into_session(std::move(*snapshot), slot_index);
    } catch (const std::exception& exception) {
        record_audit_event(
            AuditEventCategory::Session,
            "game_restore_failed",
            AuditSeverity::Error,
            audit_json_object({{"message", audit_json_string(exception.what())}}),
            AuditPriority::Critical);
        present_loading_screen(
            loading_title,
            "SAUVEGARDE INCOMPATIBLE",
            loading_progress_.progress(),
            true);
        loading_active_ = false;
        loading_completed_ = false;
        SDL_SetWindowTitle(window_, kGameWindowTitle.data());
        return false;
    }
}

auto Game::load_snapshot_into_session(SaveGameSnapshot snapshot, std::optional<std::size_t> slot_index) -> bool {
    using clock = std::chrono::steady_clock;

    const auto next_game_mode = is_known_game_mode(snapshot.metadata.game_mode)
                                    ? snapshot.metadata.game_mode
                                    : GameMode::ClassicAdventure;
    const auto sea_mode = next_game_mode == GameMode::SeaAdventure;
    const auto loading_title = sea_mode ? std::string_view("AVENTURE EN MER") : std::string_view("CHARGEMENT");
    const auto generation_profile = snapshot.world_save_plan.generation_profile;
    const auto generation_version = snapshot.world_save_plan.generation_version;
    auto renderer_staged = false;
    auto session_committed = false;

    try {
        if (!wait_for_pending_world_release_during_loading(loading_title)) {
            loading_active_ = false;
            return false;
        }

        const auto world_begin = clock::now();
        World prepared_world(
            snapshot.metadata.seed,
            options_.performance.stream_radius,
            generation_profile,
            generation_version,
            options_.visual_pipeline);
        prepared_world.begin_restore_save_plan(std::move(snapshot.world_save_plan));
        record_loading_step("save_restore_begin", world_begin);
        while (running_ && prepared_world.has_pending_save_restore()) {
            const auto step_begin = clock::now();
            const auto stats = prepared_world.process_save_restore(2048U, 2.0);
            record_loading_step("save_restore_slice", step_begin);
            update_loading_screen(
                loading_title,
                "RESTAURATION DE LA CARTE",
                LoadingPhase::SaveRestore,
                stats.progress);
        }
        if (!running_) {
            return false;
        }
        update_loading_screen(
            loading_title,
            "RESTAURATION DE LA CARTE",
            LoadingPhase::SaveRestore,
            1.0F,
            true);

        auto legacy_ship_layout_present = false;
        auto legacy_ship_world_origin = std::optional<glm::vec3> {};
        SeaAdventureSystem prepared_sea_adventure {};
        if (sea_mode) {
            auto sea_state = snapshot.sea_adventure;
            sea_state.active = true;
            prepared_sea_adventure.load_state(sea_state, snapshot.metadata.seed);
            const auto& sanitized_sea_state = prepared_sea_adventure.save_state();
            legacy_ship_layout_present = sanitized_sea_state.has_stamped_ship;
            if (legacy_ship_layout_present) {
                // Je conserve le repere exact du navire v7 avant que la migration
                // ne remplace ses coordonnees par celles de la nouvelle entite.
                legacy_ship_world_origin = glm::vec3 {
                    static_cast<float>(sanitized_sea_state.stamped_ship_x),
                    prepared_sea_adventure.ship_entity().world_origin().y,
                    static_cast<float>(sanitized_sea_state.stamped_ship_z),
                };
            }
            prepared_sea_adventure.begin_legacy_ship_migration(prepared_world);
            while (running_ && prepared_sea_adventure.has_pending_legacy_ship_migration()) {
                const auto step_begin = clock::now();
                const auto stats = prepared_sea_adventure.migrate_legacy_ship_step(prepared_world, 128U, 2.0);
                record_loading_step("legacy_ship_slice", step_begin);
                update_loading_screen(
                    loading_title,
                    "EFFACEMENT DE L'ANCIEN NAVIRE",
                    LoadingPhase::LegacyMigration,
                    stats.progress);
            }
        } else {
            prepared_sea_adventure.load_state({}, snapshot.metadata.seed);
        }
        if (!running_) {
            return false;
        }
        update_loading_screen(
            loading_title,
            sea_mode ? std::string_view("PREPARATION DU NAVIRE") : std::string_view("PREPARATION DU MONDE"),
            LoadingPhase::LegacyMigration,
            1.0F,
            true);

        const auto state_begin = clock::now();
        if (sea_mode && prepared_sea_adventure.active()) {
            const auto& ship = prepared_sea_adventure.ship_entity();
            const auto player_reconciliation = reconcile_loaded_ship_occupant(
                ship,
                snapshot.player_state.position,
                0.30F,
                1.80F,
                legacy_ship_layout_present,
                ShipInvalidPositionPolicy::Relocate,
                legacy_ship_world_origin);
            if (player_reconciliation.relocated) {
                // Je restaure le joueur sur l'ancre logique la plus proche si
                // l'ancien agencement le placerait dans une cloison ou une cale.
                snapshot.player_state.position = player_reconciliation.position;
                snapshot.player_state.velocity = {};
                snapshot.player_state.fall_start_y = player_reconciliation.position.y;
                snapshot.player_state.airborne_time = 0.0F;
                snapshot.player_state.on_ground = true;
                snapshot.player_state.head_underwater = false;
                snapshot.player_state.swimming = false;
            }

            for (auto& drop : snapshot.item_drops) {
                const auto drop_reconciliation = reconcile_loaded_ship_occupant(
                    ship,
                    drop.position,
                    0.15F,
                    0.24F,
                    legacy_ship_layout_present,
                    ShipInvalidPositionPolicy::Preserve,
                    legacy_ship_world_origin);
                if (!drop_reconciliation.relocated) {
                    continue;
                }
                // Je reveille les objets recales : leur cache de support ne doit
                // jamais continuer a designer un bloc du navire v7 supprime.
                drop.position = drop_reconciliation.position;
                drop.velocity = {};
                drop.grounded = true;
                drop.sleeping = false;
                drop.sleep_support_valid = false;
                drop.sleep_candidate_seconds = 0.0F;
                drop.sleep_support_check_timer = 0.0F;
                drop.sleep_support_block = {};
            }
        }
        auto prepared_hotbar = snapshot.hotbar;
        auto prepared_inventory = snapshot.inventory;
        normalize_inventory_state(prepared_inventory, prepared_hotbar);
        prepared_inventory.visible = false;
        prepared_inventory.hovered_slot.reset();
        ItemDropSystem prepared_item_drops {};
        prepared_item_drops.load_drops(snapshot.item_drops);
        PlayerProgression prepared_progression {};
        prepared_progression.load_state(snapshot.progression);
        PlayerController prepared_player {};
        prepared_player.load_state(snapshot.player_state);
        EnvironmentClock prepared_environment {};
        prepared_environment.set_time_of_day(snapshot.metadata.time_of_day);
        prepared_environment.set_weather_seed(static_cast<std::uint32_t>(snapshot.metadata.seed));
        // Je conserve le temps meteo persiste : l'option de demarrage ne doit
        // jamais remplacer l'etat d'une sauvegarde existante.
        prepared_environment.set_weather_time_seconds(snapshot.metadata.weather_time_seconds);
        prepared_environment.set_frozen(options_.freeze_time || options_.smoke_test);
        const auto prepared_village_enabled =
            next_game_mode == GameMode::ClassicAdventure && snapshot.metadata.has_starting_village;
        StartingVillageLayout prepared_village {};
        CreatureSystem prepared_creatures {};
        if (prepared_village_enabled) {
            StartingVillageGenerator village_generator(snapshot.metadata.seed);
            prepared_village = village_generator.build_layout();
            prepared_creatures.set_settlement_residents(prepared_village.residents);
        }
        prepared_creatures.load_creatures(snapshot.creatures, prepared_environment.current_state());
        auto prepared_spawn = finite_vec3_or(snapshot.spawn_position, {0.5F, 70.0F, 0.5F});
        if (sea_mode && prepared_sea_adventure.active()) {
            prepared_spawn = prepared_sea_adventure.deck_spawn_position();
        }
        record_loading_step("saved_session_prepare", state_begin);

        if (!reset_renderer_world_resources_during_loading(loading_title)) {
            loading_active_ = false;
            return false;
        }
        renderer_staged = true;
        prime_world_around(prepared_world, prepared_player.position(), loading_title, "RESTAURATION DU MONDE");
        if (!running_) {
            return false;
        }

        if (sea_mode) {
            update_loading_screen(
                loading_title,
                "ASSEMBLAGE DU NAVIRE",
                LoadingPhase::ShipPreparation,
                0.25F,
                true);
            const auto ship_state = prepared_sea_adventure.ship_render_state();
            const auto ship_ready = prepare_ship_mesh_during_loading(ship_state, loading_title, true);
            if (!running_) {
                return false;
            }
            if (!ship_ready) {
                throw std::runtime_error("Unable to restore the maritime ship mesh");
            }
        }
        update_loading_screen(
            loading_title,
            sea_mode ? std::string_view("ASSEMBLAGE DU NAVIRE") : std::string_view("INITIALISATION DU RENDU"),
            LoadingPhase::ShipPreparation,
            1.0F,
            true);

        const auto commit_begin = clock::now();
        install_prepared_world(std::move(prepared_world));
        // Je fixe la meme frontiere transactionnelle au chargement : apres ce
        // point, je conserve les ressources du monde installe en cas d'erreur.
        session_committed = true;
        sea_adventure_ = std::move(prepared_sea_adventure);
        active_game_mode_ = next_game_mode;
        hotbar_ = std::move(prepared_hotbar);
        inventory_menu_ = std::move(prepared_inventory);
        item_drops_ = std::move(prepared_item_drops);
        progression_ = std::move(prepared_progression);
        player_ = std::move(prepared_player);
        environment_ = prepared_environment;
        creatures_ = std::move(prepared_creatures);
        starting_village_enabled_ = prepared_village_enabled;
        starting_village_ = std::move(prepared_village);
        spawn_position_ = prepared_spawn;
        super_vision_active_ = false;
        gameplay_announcements_.clear();
        sync_selected_hotbar_slot();
        preview_orbit_radians_ = 0.0F;
        menu_preview_time_of_day_ = environment_.time_of_day();
        update_menu_preview_camera(0.0F);
        has_active_session_ = true;
        active_save_slot_ = slot_index;
        session_save_state_.reset_clean();
        prepare_game_session();
        record_loading_step("saved_session_commit", commit_begin);

        update_loading_screen(
            loading_title,
            "VALIDATION DE LA SESSION",
            LoadingPhase::Finalization,
            1.0F,
            true);
        if (!wait_for_pending_world_release_during_loading(loading_title)) {
            loading_active_ = false;
            SDL_SetWindowTitle(window_, kGameWindowTitle.data());
            return false;
        }
        const auto slot_refresh_begin = clock::now();
        try {
            refresh_save_slots();
        } catch (const std::exception& exception) {
            std::cerr << "ValCraft save slot refresh warning: " << exception.what() << std::endl;
        }
        record_loading_step("save_slots_refresh", slot_refresh_begin);
        complete_loading_screen(
            loading_title,
            sea_mode ? std::string_view("PRET A LARGUER LES AMARRES") : std::string_view("AVENTURE PRETE"));
        SDL_SetWindowTitle(window_, kGameWindowTitle.data());
        record_audit_event(
            AuditEventCategory::Session,
            "game_loaded",
            AuditSeverity::Info,
            audit_json_object({
                {"has_slot", audit_json_bool(slot_index.has_value())},
                {"seed", audit_json_number(snapshot.metadata.seed)},
                {"game_mode", audit_json_number(static_cast<int>(active_game_mode_))},
            }),
            AuditPriority::Critical);
        return true;
    } catch (...) {
        if (!session_committed) {
            if (renderer_staged) {
                renderer_.reset_world_resources();
                world_.enqueue_loaded_mesh_uploads();
            }
            throw;
        }
        // Je ne transforme pas une erreur de finition en faux echec de
        // restauration une fois la nouvelle session effectivement installee.
        loading_active_ = false;
        SDL_SetWindowTitle(window_, kGameWindowTitle.data());
        return true;
    }
}

auto Game::start_smoke_session() -> bool {
    constexpr auto kSmokeSlot = std::size_t {0U};
    constexpr auto kSmokeSeed = 1337;

    if (options_.smoke_session == SmokeSessionMode::SeaNew) {
        start_new_game_in_slot(kSmokeSlot, GameMode::SeaAdventure);
    } else if (options_.smoke_session == SmokeSessionMode::SeaLegacy) {
        begin_loading_screen(LoadingScreenTheme::Maritime, static_cast<std::uint32_t>(kSmokeSeed));
        update_loading_screen(
            "AVENTURE EN MER",
            "PREPARATION D'UNE SAUVEGARDE HISTORIQUE",
            LoadingPhase::Preparation,
            1.0F,
            true);

        SeaAdventureSystem legacy_sea {};
        legacy_sea.reset(kSmokeSeed);
        auto sea_state = legacy_sea.save_state();
        sea_state.active = true;
        sea_state.voyage_phase = SeaVoyagePhase::Underway;
        sea_state.voyage_phase_elapsed = 0.0F;
        sea_state.has_stamped_ship = true;
        sea_state.stamped_ship_x = static_cast<std::int32_t>(std::floor(sea_state.ship_position.x));
        sea_state.stamped_ship_z = static_cast<std::int32_t>(std::floor(sea_state.ship_position.z));

        SaveGameSnapshot fixture {};
        fixture.metadata.exists = true;
        fixture.metadata.seed = kSmokeSeed;
        fixture.metadata.time_of_day = 8.25F;
        // Je passe l'override par la fixture pour valider le meme chemin de
        // chargement que celui d'une vraie sauvegarde maritime.
        fixture.metadata.weather_time_seconds =
            options_.initial_weather_time_seconds;
        fixture.metadata.game_mode = GameMode::SeaAdventure;
        fixture.sea_adventure = sea_state;
        const auto legacy_world_origin = glm::vec3 {
            static_cast<float>(sea_state.stamped_ship_x),
            legacy_sea.ship_entity().world_origin().y,
            static_cast<float>(sea_state.stamped_ship_z),
        };
        const auto legacy_player_position = legacy_world_origin + glm::vec3 {7.5F, 4.0F, 0.0F};
        const auto legacy_drop_position = legacy_world_origin + glm::vec3 {0.0F, 2.0F, -5.0F};
        fixture.spawn_position = legacy_player_position;
        fixture.player_state.position = fixture.spawn_position;
        // Je decris explicitement un occupant pose sur l'ancien pont. Sans cet
        // etat, la fixture demandait au chargeur de conserver puis de corriger
        // simultanement un joueur valide mais artificiellement marque en chute.
        fixture.player_state.velocity = {};
        fixture.player_state.fall_start_y = fixture.player_state.position.y;
        fixture.player_state.on_ground = true;
        fixture.hotbar = make_default_hotbar_state();
        fixture.inventory = make_default_inventory_menu_state();
        normalize_inventory_state(fixture.inventory, fixture.hotbar);
        ItemDrop legacy_drop {};
        legacy_drop.position = legacy_drop_position;
        legacy_drop.stack = make_item_stack(to_block_id(BlockType::Stone), 1);
        fixture.item_drops.push_back(legacy_drop);

        WorldSavePlan fixture_plan {};
        fixture_plan.seed = kSmokeSeed;
        fixture_plan.generation_profile = WorldGenerationProfile::OceanAdventure;
        fixture_plan.generation_version = WorldGenerationVersion::LegacyV1;
        write_save_slot(save_root_directory_, kSmokeSlot, fixture, fixture_plan);
        refresh_save_slots();
        if (!load_game_from_slot(kSmokeSlot)) {
            return false;
        }
        const auto& migrated_ship = sea_adventure_.ship_entity();
        if (item_drops_.drops().size() != 1U) {
            throw std::runtime_error("Legacy maritime smoke did not restore its historical item drop");
        }
        const auto& migrated_drop = item_drops_.drops().front();
        // Je conserve désormais l'ancien point x=7,5 lorsqu'il reste sain sur
        // la coque agrandie. Le smoke vérifie la validité finale, pas un
        // déplacement devenu artificiel, tandis que l'objet sans support doit
        // toujours être réellement replacé dans la nouvelle cale.
        const auto player_supported = migrated_ship.support_height(player_.position()).has_value();
        const auto player_blocked = player_.overlaps_dynamic_obstacle(migrated_ship);
        const auto player_grounded = player_.state().on_ground;
        const auto drop_moved = migrated_drop.position != legacy_drop_position;
        const auto drop_supported = migrated_ship.support_height(migrated_drop.position).has_value();
        const auto drop_grounded = migrated_drop.grounded;
        const auto drop_stationary = migrated_drop.velocity == glm::vec3 {};
        if (!player_supported || player_blocked || !player_grounded || !drop_moved ||
            !drop_supported || !drop_grounded || !drop_stationary) {
            // Je conserve chaque invariant dans le message : un smoke en CI doit
            // nommer la cause concrete au lieu de masquer sept controles differents.
            std::ostringstream details;
            details << "Legacy maritime smoke did not reconcile its historical occupants"
                    << " (player_supported=" << player_supported
                    << ", player_blocked=" << player_blocked
                    << ", player_grounded=" << player_grounded
                    << ", drop_moved=" << drop_moved
                    << ", drop_supported=" << drop_supported
                    << ", drop_grounded=" << drop_grounded
                    << ", drop_stationary=" << drop_stationary << ')';
            throw std::runtime_error(details.str());
        }
    } else {
        return true;
    }

    if (!running_) {
        return false;
    }
    if (!has_active_session_ || active_game_mode_ != GameMode::SeaAdventure ||
        !sea_adventure_.active() || !loading_completed_ || !loading_progress_.completed() ||
        loading_progress_.progress() != 1.0F || loading_update_count_ < 2U ||
        pending_save_.valid() || pending_world_release_.valid() ||
        session_save_state_.failed() || session_save_state_.dirty()) {
        throw std::runtime_error("Maritime smoke loading did not complete a valid sea session");
    }
    if (options_.smoke_session == SmokeSessionMode::SeaNew) {
        const auto port_layout = StartingPortGenerator(world_.seed()).build_layout();
        const auto gangway_ready = world_.get_block(
                                       port_layout.gangway.max_x,
                                       port_layout.gangway.surface_y,
                                       -8) == to_block_id(BlockType::Planks);
        const auto quay_ready = is_block_collidable(world_.get_block(
            port_layout.stone_quay.min_x,
            port_layout.stone_quay.surface_y,
            0));
        // Je verrouille ici les trois invariants propres a une nouvelle mer :
        // revision V2, navire encore amarre et port effectivement applique.
        if (world_.generation_version() != WorldGenerationVersion::SparseArchipelagoV2 ||
            sea_adventure_.save_state().voyage_phase != SeaVoyagePhase::Moored ||
            !gangway_ready || !quay_ready) {
            throw std::runtime_error("New maritime smoke did not start moored in its V2 harbor");
        }
    }
    const auto smoke_preload_radius = std::min(
        std::max(options_.performance.spawn_preload_radius, 1),
        std::max(world_.stream_radius(), 0));
    if (preload_readiness(world_, player_.position(), smoke_preload_radius) < 1.0F) {
        throw std::runtime_error("Maritime smoke loading entered gameplay before CPU chunks were ready");
    }
    if (preload_gpu_readiness(world_, player_.position(), smoke_preload_radius) < 1.0F) {
        throw std::runtime_error("Maritime smoke loading entered gameplay before GPU chunks were ready");
    }
    if (!renderer_.ship_mesh_ready(sea_adventure_.ship_render_state())) {
        throw std::runtime_error("Maritime smoke loading entered gameplay before the ship mesh was ready");
    }
    // Je mesure encore chaque tranche sous gcov, mais je ne compare pas ce
    // binaire force en -O0 et instrumente au SLA d'un executable de production.
    // Le build strict non instrumente conserve le contrat de 50 ms pour les
    // deux parcours maritimes.
    const auto enforce_loading_slice_budget =
        !kCoverageInstrumentationEnabled &&
        (kPerformanceBuildType != "Debug" ||
         options_.smoke_session != SmokeSessionMode::SeaLegacy);
    if (enforce_loading_slice_budget && loading_max_step_ms_ > kMaritimeSmokeSliceLimitMs) {
        throw std::runtime_error(
            std::string("Maritime smoke loading exceeded the ") +
            std::to_string(static_cast<int>(kMaritimeSmokeSliceLimitMs)) +
            " ms main-thread slice limit at " +
            std::string(loading_max_step_label_) + " (" + std::to_string(loading_max_step_ms_) + " ms)");
    }
    update_smoke_ship_camera();
    return true;
}

void Game::save_game_to_slot(std::size_t slot_index) {
    if (!has_active_session_) {
        return;
    }

    try {
        (void)finish_pending_save(true);
        auto snapshot = make_world_snapshot();
        auto world_save_plan = world_.capture_save_plan();
        const auto modified_chunk_count = static_cast<std::uint32_t>(std::min<std::size_t>(
            world_save_plan.chunks.size(),
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
        snapshot.metadata.modified_chunk_count = modified_chunk_count;
        const auto save_root = save_root_directory_;
        pending_save_slot_ = slot_index;
        session_save_state_.begin_save();
        pending_save_ = std::async(
            std::launch::async,
            [save_root,
             slot_index,
             snapshot = std::move(snapshot),
             world_save_plan = std::move(world_save_plan)]() mutable {
                write_save_slot(save_root, slot_index, snapshot, world_save_plan);
            });
        active_save_slot_ = slot_index;
        try {
            record_audit_event(
                AuditEventCategory::Session,
                "game_save_queued",
                AuditSeverity::Info,
                audit_json_object({
                    {"slot_index", audit_json_number(slot_index)},
                    {"modified_chunks", audit_json_number(modified_chunk_count)},
                }),
                AuditPriority::High);
        } catch (const std::exception& exception) {
            std::cerr << "ValCraft save telemetry warning: " << exception.what() << std::endl;
        }
    } catch (const std::exception& exception) {
        // Je laisse la partie jouable et marquee comme modifiee si la capture ou
        // le lancement du worker echoue, au lieu de fermer brutalement le jeu.
        pending_save_slot_.reset();
        session_save_state_.fail_save();
        try {
            record_audit_event(
                AuditEventCategory::Session,
                "game_save_queue_failed",
                AuditSeverity::Error,
                audit_json_object({{"message", audit_json_string(exception.what())}}),
                AuditPriority::Critical);
        } catch (...) {
            // Je conserve prioritairement l'etat sale de la session.
        }
        std::cerr << "ValCraft save warning: " << exception.what() << std::endl;
    }
}

void Game::mark_session_dirty() noexcept {
    session_save_state_.mark_dirty();
}

void Game::update_menu_preview_camera(float dt) {
    constexpr float kTwoPi = 6.28318530718F;
    const auto clamped_dt = std::max(finite_or(dt, 0.0F), 0.0F);
    preview_orbit_radians_ = std::fmod(finite_or(preview_orbit_radians_, 0.0F) + clamped_dt * 0.12F, kTwoPi);
    const auto maritime_preview = active_game_mode_ == GameMode::SeaAdventure && sea_adventure_.active();
    const auto& ship_bounds = amelie_ship_blueprint().bounds;
    const auto ship_extent = ship_bounds.max - ship_bounds.min;
    const auto ship_local_center = (ship_bounds.min + ship_bounds.max) * 0.5F;
    const auto focus =
        maritime_preview
            ? sea_adventure_.ship_entity()
                  .local_to_world_point({
                      ship_local_center.x,
                      9.0F,
                      ship_local_center.z,
                  })
            : spawn_position_ +
                  glm::vec3 {
                      0.0F,
                      5.0F,
                      0.0F,
                  };
    // Je derive le recul des limites reelles : une future evolution de la
    // coque ne pourra plus sortir silencieusement du cadre du menu.
    const auto radius = maritime_preview ? std::max(ship_extent.x, ship_extent.z) * 0.78F + 8.0F : 26.0F;
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
    const auto streaming_stress =
        options_.performance.perf_scenario == "world_stress";
    const auto horizontal_pose =
        make_land_smoke_camera_pose(
            smoke_elapsed_seconds_,
            0,
            streaming_stress);
    const auto world_x =
        static_cast<int>(std::floor(horizontal_pose.position.x));
    const auto world_z =
        static_cast<int>(std::floor(horizontal_pose.position.z));
    const auto previous_surface =
        static_cast<int>(
            std::floor(
                std::max(
                    player_.position().y - 2.40F,
                    0.0F)));
    const auto surface_height =
        world_.loaded_surface_height(world_x, world_z)
            .value_or(previous_surface);
    const auto pose =
        make_land_smoke_camera_pose(
            smoke_elapsed_seconds_,
            surface_height,
            streaming_stress);

    auto state = player_.state();
    state.position = pose.position;
    state.velocity = {};
    state.fly_mode = true;
    state.on_ground = false;
    state.dead = false;
    state.head_underwater = false;
    state.swimming = false;
    state.yaw_degrees = pose.yaw_degrees;
    state.pitch_degrees = pose.pitch_degrees;
    state.body_yaw_degrees = pose.yaw_degrees;
    state.look_sway_yaw = 0.0F;
    state.look_sway_pitch = 0.0F;
    player_.load_state(state);
}

void Game::update_smoke_ship_camera() {
    if (!options_.smoke_test ||
        options_.smoke_ship_view ==
            SmokeShipView::None ||
        active_game_mode_ !=
            GameMode::SeaAdventure ||
        !sea_adventure_.active()) {
        return;
    }

    const auto& ship =
        sea_adventure_.ship_entity();
    const auto& blueprint =
        amelie_ship_blueprint();
    const auto extent =
        blueprint.bounds.max -
        blueprint.bounds.min;
    const auto local_center =
        (blueprint.bounds.min +
         blueprint.bounds.max) *
        0.5F;
    const auto to_world =
        [&ship](
            const glm::vec3& local_point) noexcept {
            return ship.local_to_world_point(
                local_point);
        };

    const auto ship_center =
        to_world({
            local_center.x,
            0.0F,
            local_center.z,
        });
    auto camera_position =
        ship_center;
    auto camera_focus =
        to_world({0.0F, 0.0F, 0.0F});

    // Toutes les positions sont exprimees dans le repere du blueprint puis
    // transformees par la pose du navire. L'horizon reste toutefois vertical,
    // car la matrice de vue du PlayerController conserve l'axe monde Y.
    switch (options_.smoke_ship_view) {
    case SmokeShipView::Deck:
        camera_position = to_world({
            extent.x * 1.05F,
            15.0F,
            blueprint.anchors.aft_hatch.z -
                12.0F,
        });
        camera_focus = to_world({
            0.0F,
            4.8F,
            blueprint.anchors.galley.z,
        });
        break;

    case SmokeShipView::Bow:
        camera_position = to_world({
            -extent.x * 0.55F,
            12.0F,
            blueprint.bounds.max.z +
                10.0F,
        });
        camera_focus = to_world({
            0.0F,
            6.8F,
            blueprint.bounds.max.z -
                extent.z * 0.38F,
        });
        break;

    case SmokeShipView::Stern:
        camera_position = to_world({
            0.0F,
            9.0F,
            blueprint.bounds.min.z -
                11.0F,
        });
        camera_focus = to_world({
            0.0F,
            6.5F,
            blueprint.anchors.helm.z +
                9.0F,
        });
        break;

    case SmokeShipView::Port:
        camera_position = to_world({
            local_center.x -
                std::min(
                    15.0F,
                    extent.x * 1.05F),
            24.0F,
            local_center.z - 5.0F,
        });
        camera_focus = to_world({
            0.0F,
            7.0F,
            local_center.z,
        });
        break;

    case SmokeShipView::Starboard:
        camera_position = to_world({
            local_center.x +
                std::min(
                    15.0F,
                    extent.x * 1.05F),
            24.0F,
            local_center.z + 5.0F,
        });
        camera_focus = to_world({
            0.0F,
            7.0F,
            local_center.z,
        });
        break;

    case SmokeShipView::Interior:
        camera_position = to_world(
            blueprint.anchors.crew_quarters +
            glm::vec3 {
                0.70F,
                0.0F,
                -3.0F,
            });
        camera_focus = to_world(
            blueprint.anchors.galley +
            glm::vec3 {
                0.15F,
                1.0F,
                5.0F,
            });
        break;

    case SmokeShipView::CaptainCabin:
        camera_position = to_world({
            -0.40F,
            1.01F,
            -32.30F,
        });
        camera_focus = to_world(
            blueprint.anchors.captain_cabin +
            glm::vec3 {
                0.0F,
                1.10F,
                1.5F,
            });
        break;

    case SmokeShipView::CargoHold:
        camera_position = to_world(
            blueprint.anchors.cargo_hold +
            glm::vec3 {
                0.0F,
                0.0F,
                -7.0F,
            });
        camera_focus = to_world(
            blueprint.anchors.cargo_hold +
            glm::vec3 {
                0.0F,
                1.10F,
                6.0F,
            });
        break;

    case SmokeShipView::CrewDeck:
        camera_position = to_world({
            7.0F,
            4.90F,
            -27.0F,
        });
        camera_focus = to_world({
            -0.5F,
            5.35F,
            -28.0F,
        });
        break;

    case SmokeShipView::None:
    default:
        return;
    }

    constexpr float kPlayerEyeHeight =
        1.62F;
    const auto eye_position =
        camera_position +
        glm::vec3 {
            0.0F,
            kPlayerEyeHeight,
            0.0F,
        };
    const auto direction =
        glm::normalize(
            camera_focus -
            eye_position);

    auto camera_state =
        preview_player_.state();
    camera_state.position =
        camera_position;
    camera_state.velocity = {};
    camera_state.yaw_degrees =
        glm::degrees(
            std::atan2(
                direction.z,
                direction.x));
    camera_state.pitch_degrees =
        glm::degrees(
            std::asin(
                std::clamp(
                    direction.y,
                    -1.0F,
                    1.0F)));
    camera_state.body_yaw_degrees =
        camera_state.yaw_degrees;
    camera_state.animation_time = 0.0F;
    camera_state.step_phase = 0.0F;
    camera_state.look_sway_yaw = 0.0F;
    camera_state.look_sway_pitch = 0.0F;
    camera_state.fly_mode = true;
    camera_state.on_ground = false;
    camera_state.dead = false;
    camera_state.head_underwater = false;
    camera_state.swimming = false;
    preview_player_.load_state(
        camera_state);
}

void Game::validate_smoke_frame(const WorldWorkBudget& budget, const WorldWorkStats& stats) const {
    if (stats.generated_chunks > budget.chunk_generation_budget) {
        std::ostringstream message;
        message << "Smoke test exceeded chunk generation budget (generated=" << stats.generated_chunks
                << ", budget=" << budget.chunk_generation_budget << ")";
        throw std::runtime_error(message.str());
    }
    if (stats.meshed_chunks > budget.mesh_rebuild_budget) {
        std::ostringstream message;
        message << "Smoke test exceeded mesh rebuild budget (prioritized=" << stats.prioritized_meshed_chunks
                << ", total=" << stats.meshed_chunks
                << ", budget=" << budget.mesh_rebuild_budget << ")";
        throw std::runtime_error(message.str());
    }
    if (stats.processed_fluid_cells > budget.fluid_cell_budget) {
        std::ostringstream message;
        message << "Smoke test exceeded fluid budget (processed=" << stats.processed_fluid_cells
                << ", budget=" << budget.fluid_cell_budget << ")";
        throw std::runtime_error(message.str());
    }
    if (stats.light_nodes_processed > budget.light_node_budget) {
        std::ostringstream message;
        message << "Smoke test exceeded lighting node budget (processed=" << stats.light_nodes_processed
                << ", budget=" << budget.light_node_budget << ")";
        throw std::runtime_error(message.str());
    }
    if (!world_.are_chunks_ready(player_.position(), options_.performance.spawn_preload_radius)) {
        std::ostringstream message;
        message << "Smoke test detected missing ready chunks near the player"
                << " (frame=" << rendered_frames_
                << ", position=" << player_.position().x << ',' << player_.position().y << ',' << player_.position().z
                << ", pending_generation=" << world_.pending_generation_count()
                << ", pending_lighting=" << world_.pending_lighting_count()
                << ", pending_mesh=" << world_.pending_mesh_count() << ')';
        throw std::runtime_error(message.str());
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

    const auto process_memory = query_process_memory();
    if (process_memory.valid) {
        last_process_memory_ = process_memory;
    }

    if (frame_stats.frame_index >= options_.performance.perf_warmup_frames) {
        constexpr std::size_t kTrimmedSampleBatch = 600U;
        if (frame_samples_.size() >= kMaxPerformanceSamples) {
            const auto trim_count = std::min(kTrimmedSampleBatch, frame_samples_.size());
            frame_samples_.erase(frame_samples_.begin(), frame_samples_.begin() + static_cast<std::ptrdiff_t>(trim_count));
        }
        frame_samples_.push_back(make_performance_sample(frame_stats));
    }
    note_frame_for_audit(frame_stats);
}

auto Game::make_performance_sample(const FramePerformanceStats& frame_stats) const -> FramePerformanceSample {
    FramePerformanceSample sample {};
    sample.frame_index = frame_stats.frame_index;
    sample.frame_total_ms = frame_stats.frame_total_ms;
    sample.streaming_ms = frame_stats.streaming_ms;
    sample.generation_ms = frame_stats.generation_ms;
    sample.lighting_ms = frame_stats.lighting_ms;
    sample.meshing_ms = frame_stats.meshing_ms;
    sample.upload_ms = frame_stats.upload_ms;
    sample.shadow_ms = frame_stats.shadow_ms;
    sample.world_ms = frame_stats.world_ms;
    sample.generated_chunks = frame_stats.generated_chunks;
    sample.meshed_chunks = frame_stats.meshed_chunks;
    sample.light_nodes_processed = frame_stats.light_nodes_processed;
    sample.uploaded_meshes = frame_stats.uploaded_meshes;
    sample.pending_generation = frame_stats.pending_generation;
    sample.pending_mesh = frame_stats.pending_mesh;
    sample.pending_lighting = frame_stats.pending_lighting;
    sample.stream_chunk_changes = frame_stats.stream_chunk_changes;
    sample.generation_enqueued = frame_stats.generation_enqueued;
    sample.generation_pruned = frame_stats.generation_pruned;
    sample.unloaded_chunks = frame_stats.unloaded_chunks;
    sample.lighting_jobs_completed = frame_stats.lighting_jobs_completed;
    sample.visible_chunks = frame_stats.visible_chunks;
    sample.shadow_chunks = frame_stats.shadow_chunks;
    sample.world_chunks = frame_stats.world_chunks;
    sample.event_processing_ms = frame_stats.event_processing_ms;
    sample.simulation_ms = frame_stats.simulation_ms;
    sample.audio_ms = frame_stats.audio_ms;
    sample.render_preparation_ms = frame_stats.render_preparation_ms;
    sample.fluid_ms = frame_stats.fluid_ms;
    sample.render_cpu_ms = frame_stats.render_cpu_ms;
    sample.render_overhead_ms = frame_stats.render_overhead_ms;
    sample.present_ms = frame_stats.present_ms;
    sample.telemetry_ms = frame_stats.telemetry_ms;
    sample.residual_ms = frame_stats.residual_ms;
    sample.gpu_shadow_ms = frame_stats.gpu_shadow_ms;
    sample.gpu_world_ms = frame_stats.gpu_world_ms;
    sample.gpu_sky_ms = frame_stats.gpu_sky_ms;
    sample.gpu_water_ms = frame_stats.gpu_water_ms;
    sample.gpu_entities_ms = frame_stats.gpu_entities_ms;
    sample.gpu_post_process_ms = frame_stats.gpu_post_process_ms;
    sample.gpu_hud_ms = frame_stats.gpu_hud_ms;
    sample.gpu_frame_ms = frame_stats.gpu_frame_ms;
    sample.gpu_source_frame = frame_stats.gpu_source_frame;
    sample.gpu_latency_frames = frame_stats.gpu_latency_frames;
    sample.gpu_timing_valid = frame_stats.gpu_timing_valid &&
                              frame_stats.gpu_source_frame >= options_.performance.perf_warmup_frames;
    sample.resolved_quality = frame_stats.resolved_quality;
    sample.adaptive_frame_ema_ms = frame_stats.adaptive_frame_ema_ms;
    sample.adaptive_frame_p95_ms = frame_stats.adaptive_frame_p95_ms;
    sample.processed_fluid_cells = frame_stats.processed_fluid_cells;
    sample.pending_fluid = frame_stats.pending_fluid;
    sample.fixed_updates = frame_stats.fixed_updates;
    sample.dropped_fixed_updates = frame_stats.dropped_fixed_updates;
    sample.draw_calls = frame_stats.draw_calls;
    sample.triangles = frame_stats.triangles;
    sample.uploaded_bytes = frame_stats.uploaded_bytes;
    sample.process_working_set_bytes = last_process_memory_.working_set_bytes;
    sample.process_private_bytes = last_process_memory_.private_bytes;
    sample.world_cpu_bytes = frame_stats.world_cpu_bytes;
    sample.mesh_cpu_bytes = frame_stats.mesh_cpu_bytes;
    sample.override_bytes = frame_stats.override_bytes;
    sample.gpu_buffer_bytes = frame_stats.gpu_buffer_bytes;
    sample.gpu_texture_bytes = frame_stats.gpu_texture_bytes;

    // Je conserve ici les passes effectivement chronometrees par le renderer.
    // La passe opaque historique contient encore terrain, vegetation et navire :
    // je l'attribue donc une seule fois au terrain tant que ces sous-passes ne
    // disposent pas de requetes separees, afin de ne jamais gonfler le total.
    sample.render_category_cpu_ms[
        PerformanceRenderCategory::Terrain] =
        frame_stats.world_ms;
    sample.render_category_cpu_ms[
        PerformanceRenderCategory::Shadows] =
        frame_stats.shadow_ms;
    sample.render_category_gpu_ms[
        PerformanceRenderCategory::Terrain] =
        frame_stats.gpu_world_ms;
    sample.render_category_gpu_ms[
        PerformanceRenderCategory::Entities] =
        frame_stats.gpu_entities_ms;
    sample.render_category_gpu_ms[
        PerformanceRenderCategory::Water] =
        frame_stats.gpu_water_ms;
    sample.render_category_gpu_ms[
        PerformanceRenderCategory::Atmosphere] =
        frame_stats.gpu_sky_ms;
    sample.render_category_gpu_ms[
        PerformanceRenderCategory::PostProcess] =
        frame_stats.gpu_post_process_ms;
    sample.render_category_gpu_ms[
        PerformanceRenderCategory::Ui] =
        frame_stats.gpu_hud_ms;
    sample.render_category_gpu_ms[
        PerformanceRenderCategory::Shadows] =
        frame_stats.gpu_shadow_ms;

    sample.dominant_stage = detect_dominant_stage(sample);
    return sample;
}

void Game::record_performance_event(PerformanceEventKind kind, const BlockCoord& block, std::string_view label) {
    if (!should_capture_performance()) {
        return;
    }

    const auto chunk_coord = world_.world_to_chunk(block.x, block.z);
    if (performance_events_.size() >= kMaxPerformanceEvents) {
        constexpr std::size_t kTrimmedEventBatch = 64U;
        const auto trim_count = std::min(kTrimmedEventBatch, performance_events_.size());
        performance_events_.erase(
            performance_events_.begin(),
            performance_events_.begin() + static_cast<std::ptrdiff_t>(trim_count));
    }
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
    event.frame_index = recording_frame_index_.value_or(static_cast<std::size_t>(rendered_frames_));
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

    audit_second_accumulator_.event_processing_ms_total += frame_stats.event_processing_ms;
    audit_second_accumulator_.simulation_ms_total += frame_stats.simulation_ms;
    audit_second_accumulator_.audio_ms_total += frame_stats.audio_ms;
    audit_second_accumulator_.render_preparation_ms_total += frame_stats.render_preparation_ms;
    audit_second_accumulator_.streaming_ms_total += frame_stats.streaming_ms;
    audit_second_accumulator_.generation_ms_total += frame_stats.generation_ms;
    audit_second_accumulator_.fluid_ms_total += frame_stats.fluid_ms;
    audit_second_accumulator_.lighting_ms_total += frame_stats.lighting_ms;
    audit_second_accumulator_.meshing_ms_total += frame_stats.meshing_ms;
    audit_second_accumulator_.upload_ms_total += frame_stats.upload_ms;
    audit_second_accumulator_.shadow_ms_total += frame_stats.shadow_ms;
    audit_second_accumulator_.world_ms_total += frame_stats.world_ms;
    audit_second_accumulator_.render_cpu_ms_total += frame_stats.render_cpu_ms;
    audit_second_accumulator_.render_overhead_ms_total += frame_stats.render_overhead_ms;
    audit_second_accumulator_.present_ms_total += frame_stats.present_ms;
    audit_second_accumulator_.telemetry_ms_total += frame_stats.telemetry_ms;
    audit_second_accumulator_.residual_ms_total += frame_stats.residual_ms;
    if (frame_stats.gpu_timing_valid) {
        audit_second_accumulator_.gpu_frame_ms_total += frame_stats.gpu_frame_ms;
        ++audit_second_accumulator_.gpu_timing_samples;
    }
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
    audit_second_accumulator_.pending_fluid_max =
        std::max(audit_second_accumulator_.pending_fluid_max, frame_stats.pending_fluid);
    audit_second_accumulator_.process_working_set_bytes_max = std::max(
        audit_second_accumulator_.process_working_set_bytes_max,
        last_process_memory_.working_set_bytes);
    audit_second_accumulator_.process_private_bytes_max = std::max(
        audit_second_accumulator_.process_private_bytes_max,
        last_process_memory_.private_bytes);
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
                {"event_processing_ms", audit_json_number(frame_stats.event_processing_ms)},
                {"simulation_ms", audit_json_number(frame_stats.simulation_ms)},
                {"audio_ms", audit_json_number(frame_stats.audio_ms)},
                {"render_preparation_ms", audit_json_number(frame_stats.render_preparation_ms)},
                {"streaming_ms", audit_json_number(frame_stats.streaming_ms)},
                {"generation_ms", audit_json_number(frame_stats.generation_ms)},
                {"fluid_ms", audit_json_number(frame_stats.fluid_ms)},
                {"lighting_ms", audit_json_number(frame_stats.lighting_ms)},
                {"meshing_ms", audit_json_number(frame_stats.meshing_ms)},
                {"upload_ms", audit_json_number(frame_stats.upload_ms)},
                {"shadow_ms", audit_json_number(frame_stats.shadow_ms)},
                {"world_ms", audit_json_number(frame_stats.world_ms)},
                {"render_cpu_ms", audit_json_number(frame_stats.render_cpu_ms)},
                {"render_overhead_ms", audit_json_number(frame_stats.render_overhead_ms)},
                {"present_ms", audit_json_number(frame_stats.present_ms)},
                {"telemetry_ms", audit_json_number(frame_stats.telemetry_ms)},
                {"residual_ms", audit_json_number(frame_stats.residual_ms)},
                {"gpu_frame_ms", audit_json_number(frame_stats.gpu_frame_ms)},
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
    const auto accumulated_frame_ms = std::accumulate(
        audit_second_accumulator_.frame_ms_values.begin(),
        audit_second_accumulator_.frame_ms_values.end(),
        0.0);
    sample.fps_avg = accumulated_frame_ms > 0.0
                         ? static_cast<double>(sample.frame_count) * 1000.0 / accumulated_frame_ms
                         : 0.0;
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
    sample.event_processing_ms_avg = audit_second_accumulator_.event_processing_ms_total / frame_count;
    sample.simulation_ms_avg = audit_second_accumulator_.simulation_ms_total / frame_count;
    sample.audio_ms_avg = audit_second_accumulator_.audio_ms_total / frame_count;
    sample.render_preparation_ms_avg = audit_second_accumulator_.render_preparation_ms_total / frame_count;
    sample.streaming_ms_avg = audit_second_accumulator_.streaming_ms_total / frame_count;
    sample.generation_ms_avg = audit_second_accumulator_.generation_ms_total / frame_count;
    sample.fluid_ms_avg = audit_second_accumulator_.fluid_ms_total / frame_count;
    sample.lighting_ms_avg = audit_second_accumulator_.lighting_ms_total / frame_count;
    sample.meshing_ms_avg = audit_second_accumulator_.meshing_ms_total / frame_count;
    sample.upload_ms_avg = audit_second_accumulator_.upload_ms_total / frame_count;
    sample.shadow_ms_avg = audit_second_accumulator_.shadow_ms_total / frame_count;
    sample.world_ms_avg = audit_second_accumulator_.world_ms_total / frame_count;
    sample.render_cpu_ms_avg = audit_second_accumulator_.render_cpu_ms_total / frame_count;
    sample.render_overhead_ms_avg = audit_second_accumulator_.render_overhead_ms_total / frame_count;
    sample.present_ms_avg = audit_second_accumulator_.present_ms_total / frame_count;
    sample.telemetry_ms_avg = audit_second_accumulator_.telemetry_ms_total / frame_count;
    sample.residual_ms_avg = audit_second_accumulator_.residual_ms_total / frame_count;
    sample.gpu_timing_samples = audit_second_accumulator_.gpu_timing_samples;
    sample.gpu_frame_ms_avg = sample.gpu_timing_samples == 0U
                                  ? 0.0
                                  : audit_second_accumulator_.gpu_frame_ms_total /
                                        static_cast<double>(sample.gpu_timing_samples);
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
    sample.pending_fluid_max = audit_second_accumulator_.pending_fluid_max;
    sample.process_working_set_bytes_max = audit_second_accumulator_.process_working_set_bytes_max;
    sample.process_private_bytes_max = audit_second_accumulator_.process_private_bytes_max;
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

    // Je vide la fenetre avec toutes ses frames : conserver son depassement
    // raccourcirait artificiellement l'echantillon suivant.
    audit_elapsed_ms_ = 0.0;
    audit_second_accumulator_.reset(audit_second_accumulator_.second_index + 1);
}

auto Game::make_audit_frame_sample(const FramePerformanceStats& frame_stats) const -> AuditFrameSample {
    AuditFrameSample sample {};
    sample.frame_index = frame_stats.frame_index;
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
    case UiScreen::CommandConsole:
        sample.ui_screen = "command_console";
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
    sample.performance = make_performance_sample(frame_stats);
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
    metadata.warmup_frames = options_.performance.perf_warmup_frames;
    metadata.stream_radius = options_.performance.stream_radius;
    metadata.shadows_enabled = runtime_shadows_enabled_;
    metadata.shadow_map_size = options_.performance.shadow_map_size;
    metadata.viewport_width = window_width_;
    metadata.viewport_height = window_height_;
    metadata.post_process_enabled = runtime_post_process_enabled_;
    metadata.freeze_time = options_.freeze_time || options_.smoke_test;
    metadata.scenario = !options_.performance.perf_scenario.empty()
                            ? options_.performance.perf_scenario
                            : options_.audit.label;
    metadata.quality_profile = options_.performance.adaptive_quality ? "adaptive" : "fixed_high";
    metadata.vsync_mode = vsync_mode_;
    metadata.visual_pipeline =
        std::string(
            visual_pipeline_name(
                options_.visual_pipeline));
    metadata.material_pack_version =
        static_cast<std::uint32_t>(
            renderer_.material_pack_version());
    metadata.material_pack_checksum =
        renderer_.material_pack_checksum();
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
