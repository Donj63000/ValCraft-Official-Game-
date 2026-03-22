#include "app/Game.h"
#include "app/GameLoop.h"

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

            renderer_.render_frame(world_, player_, hotbar_, environment_.current_state(), window_width_, window_height_);
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
        "ValCraft - WASD move, Space jump, F fly, 1-9 hotbar, wheel select, LMB/RMB interact",
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
    if (!renderer_.initialize(renderer_options)) {
        return false;
    }

    set_mouse_capture(!options_.smoke_test);

    const auto preload_radius = options_.performance.spawn_preload_radius;
    const auto preload_center = world_.world_to_chunk(0, 0);
    for (int dz = -preload_radius; dz <= preload_radius; ++dz) {
        for (int dx = -preload_radius; dx <= preload_radius; ++dx) {
            world_.ensure_chunk_loaded({preload_center.x + dx, preload_center.z + dz});
        }
    }
    world_.rebuild_dirty_meshes();

    if (options_.smoke_test) {
        player_.set_position({0.5F, 80.0F, 0.5F});
        player_.set_velocity({});
    } else {
        const auto spawn_y = static_cast<float>(world_.surface_height(0, 0)) + 1.001F;
        player_.set_position({0.5F, spawn_y, 0.5F});
    }

    (void)world_.update_streaming(player_.position());
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
        case SDL_MOUSEWHEEL: {
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

            if (event.key.keysym.sym == SDLK_ESCAPE) {
                set_mouse_capture(!mouse_captured_);
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

    environment_.update(dt);

    if (options_.smoke_test) {
        update_smoke_player(dt);
    } else {
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

    if (options_.smoke_test) {
        validate_smoke_frame(options_.performance.world_budget(), world_stats);
    }
}

void Game::set_mouse_capture(bool captured) {
    mouse_captured_ = captured;
    SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
    SDL_ShowCursor(captured ? SDL_DISABLE : SDL_ENABLE);
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
