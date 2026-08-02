#include "app/Game.h"
#include "app/SmokeCamera.h"
#include "app/StreamingFocus.h"
#include "app/GameBranding.h"
#include "app/InputBindings.h"
#include "app/GameLoop.h"
#include "gameplay/StartingPort.h"
#include "gameplay/progression/VanguardTargeting.h"
#include "player/PlayerGeometry.h"
#include "render/BackroomsVisibility.h"
#include "render/SeaHorizon.h"
#include "render/ShipMesh.h"
#include "render/StylizedShipMesh.h"
#include "world/OceanAdventureLayout.h"
#include "world/OceanSimulation.h"
#include "world/BackroomsGenerator.h"
#include "world/BackroomsSpatialStack.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
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

[[nodiscard]] constexpr auto backrooms_runtime_spatial_profile(
    WorldGenerationVersion version) noexcept -> BackroomsSpatialProfile {
    return version == WorldGenerationVersion::BackroomsV4
               ? BackroomsSpatialProfile::FloodedPoolroomsV4
               : BackroomsSpatialProfile::RecessedPoolroomsV3;
}

[[nodiscard]] constexpr auto backrooms_runtime_pool_geometry_profile(
    WorldGenerationVersion version) noexcept -> BackroomsPoolGeometryProfile {
    return version == WorldGenerationVersion::BackroomsV4
               ? BackroomsPoolGeometryProfile::FloodedDistrictsV4
               : BackroomsPoolGeometryProfile::RecessedOneBlock;
}

[[nodiscard]] auto backrooms_runtime_stack(
    int seed,
    int logical_level,
    WorldGenerationVersion version =
        WorldGenerationVersion::BackroomsV4) noexcept
    -> BackroomsSpatialStack {
    return BackroomsSpatialStack(
        seed,
        logical_level,
        backrooms_runtime_spatial_profile(version));
}

auto backrooms_spawn_position(
    int seed,
    int logical_level = 0,
    WorldGenerationVersion version =
        WorldGenerationVersion::BackroomsV4) noexcept -> glm::vec3 {
    const auto stack =
        backrooms_runtime_stack(seed, logical_level, version);
    const auto block = stack.spawn_block(logical_level);
    return {
        static_cast<float>(block.x) + 0.5F,
        static_cast<float>(block.y) + 0.001F,
        static_cast<float>(block.z) + 0.5F,
    };
}

[[nodiscard]] auto backrooms_runtime_anchor_y_offset(
    int seed,
    int anchor_level,
    int logical_level,
    WorldGenerationVersion version =
        WorldGenerationVersion::BackroomsV4) noexcept -> int {
    const auto stack = BackroomsSpatialStack(
        seed,
        anchor_level,
        backrooms_runtime_spatial_profile(version));
    const auto placement =
        stack.placement_for_level(logical_level);
    // Je conserve ce decalage en entier : les deux plans sont des hauteurs de
    // blocs exactes et la requete spatiale de Jack ne doit jamais les arrondir.
    return placement.has_value()
               ? placement->floor_y -
                     kBackroomsFloorY
               : 0;
}

void translate_backrooms_jack_state_y(
    BackroomsJackState& state,
    float delta_y) noexcept {
    state.position.y += delta_y;
    state.last_seen_player_position.y += delta_y;
    state.previous_player_position.y += delta_y;
}

void translate_backrooms_jack_result_y(
    BackroomsJackUpdateResult& result,
    float delta_y) noexcept {
    result.render.position.y += delta_y;
    result.light_interference.position.y += delta_y;
    const auto event_count =
        std::min(result.event_count, result.events.size());
    for (std::size_t index = 0U;
         index < event_count;
         ++index) {
        result.events[index].position.y += delta_y;
    }
}

auto backrooms_spawn_position(
    const World& world,
    int logical_level) noexcept -> glm::vec3 {
    const auto block = world.backrooms_spawn_block(logical_level);
    return {
        static_cast<float>(block.x) + 0.5F,
        static_cast<float>(block.y) + 0.001F,
        static_cast<float>(block.z) + 0.5F,
    };
}

constexpr int kBackroomsBlackoutSmokeSearchModules = 24;
constexpr int kBackroomsBlackoutFixtureClearance = 15;
constexpr int kBackroomsBlackoutMinimumSightline = 9;
constexpr int kPoolroomsSmokeSearchRadius = kBackroomsModuleSize * 2;
constexpr int kPoolroomsSmokeMaximumBasinDistance = 8;
constexpr int kPoolroomsSmokeSightline = 18;
constexpr int kPoolroomsSmokeMinimumWetDepth = 5;
constexpr int kPoolroomsSmokeMinimumWetWidth = 3;
constexpr int kPoolroomsSmokeLightSearchRadius = 7;
constexpr float kBackroomsJackSmokeHalfWidth = 0.42F;

struct BackroomsSmokeCameraPose {
    glm::vec3 position {0.0F};
    float yaw_degrees = -90.0F;
};

[[nodiscard]] auto backrooms_blackout_smoke_pose(
    int seed) -> std::optional<BackroomsSmokeCameraPose> {
    const BackroomsGenerator generator {seed};
    constexpr std::array<BackroomsJackGridPoint, 4U> directions {{
        {0, -1},
        {1, 0},
        {0, 1},
        {-1, 0},
    }};
    constexpr std::array<float, 4U> direction_yaws {{
        -90.0F,
        0.0F,
        90.0F,
        180.0F,
    }};
    const auto fixture_clearance_squared =
        kBackroomsBlackoutFixtureClearance *
        kBackroomsBlackoutFixtureClearance;

    // Je privilegie le vrai archetype Blackout, puis je garde les autres
    // volumes de tension Blackout comme repli deterministe.
    for (const auto require_blackout_archetype :
         {true, false}) {
        for (auto module_radius = 1;
             module_radius <=
                 kBackroomsBlackoutSmokeSearchModules;
             ++module_radius) {
            for (auto module_z = -module_radius;
                 module_z <= module_radius;
                 ++module_z) {
                for (auto module_x = -module_radius;
                     module_x <= module_radius;
                     ++module_x) {
                    if (std::max(
                            std::abs(module_x),
                            std::abs(module_z)) !=
                        module_radius) {
                        continue;
                    }

                    const auto descriptor =
                        generator.module_descriptor(
                            module_x,
                            module_z);
                    if (descriptor.tension !=
                            BackroomsTension::Blackout ||
                        (require_blackout_archetype &&
                         descriptor.archetype !=
                             BackroomsArchetype::Blackout)) {
                        continue;
                    }

                    const auto origin_x =
                        module_x *
                        kBackroomsModuleSize;
                    const auto origin_z =
                        module_z *
                        kBackroomsModuleSize;
                    std::vector<
                        BackroomsJackGridPoint>
                        luminous_fixtures {};
                    luminous_fixtures.reserve(256U);
                    for (auto world_z =
                             origin_z -
                             kBackroomsBlackoutFixtureClearance;
                         world_z <=
                             origin_z +
                                 kBackroomsModuleSize - 1 +
                                 kBackroomsBlackoutFixtureClearance;
                         ++world_z) {
                        for (auto world_x =
                                 origin_x -
                                 kBackroomsBlackoutFixtureClearance;
                             world_x <=
                                 origin_x +
                                     kBackroomsModuleSize - 1 +
                                     kBackroomsBlackoutFixtureClearance;
                             ++world_x) {
                            const auto light_state =
                                generator
                                    .sample_column(
                                        world_x,
                                        world_z)
                                    .light_state;
                            if (light_state ==
                                    BackroomsLightState::Active ||
                                light_state ==
                                    BackroomsLightState::Emergency) {
                                luminous_fixtures.push_back({
                                    world_x,
                                    world_z,
                                });
                            }
                        }
                    }

                    for (auto local_z = 3;
                         local_z <
                         kBackroomsModuleSize - 3;
                         local_z += 2) {
                        for (auto local_x = 3;
                             local_x <
                             kBackroomsModuleSize - 3;
                             local_x += 2) {
                            const auto world_x =
                                origin_x + local_x;
                            const auto world_z =
                                origin_z + local_z;
                            if (!generator.is_walkable(
                                    world_x,
                                    world_z)) {
                                continue;
                            }

                            const auto too_close_to_light =
                                std::ranges::any_of(
                                    luminous_fixtures,
                                    [&](const auto& fixture) {
                                        const auto delta_x =
                                            fixture.x -
                                            world_x;
                                        const auto delta_z =
                                            fixture.z -
                                            world_z;
                                        return
                                            delta_x * delta_x +
                                                delta_z * delta_z <=
                                            fixture_clearance_squared;
                                    });
                            if (too_close_to_light) {
                                continue;
                            }

                            auto best_direction =
                                std::size_t {0U};
                            auto best_sightline = 0;
                            for (std::size_t direction_index = 0U;
                                 direction_index <
                                 directions.size();
                                 ++direction_index) {
                                auto sightline = 0;
                                for (auto step = 1;
                                     step <= 16;
                                     ++step) {
                                    const auto sample_x =
                                        world_x +
                                        directions[
                                            direction_index]
                                                .x *
                                            step;
                                    const auto sample_z =
                                        world_z +
                                        directions[
                                            direction_index]
                                                .z *
                                            step;
                                    if (!generator.is_walkable(
                                            sample_x,
                                            sample_z)) {
                                        break;
                                    }
                                    sightline = step;
                                }
                                if (sightline >
                                    best_sightline) {
                                    best_sightline =
                                        sightline;
                                    best_direction =
                                        direction_index;
                                }
                            }
                            if (best_sightline <
                                kBackroomsBlackoutMinimumSightline) {
                                continue;
                            }

                            const auto column =
                                generator.sample_column(
                                    world_x,
                                    world_z);
                            return BackroomsSmokeCameraPose {
                                {
                                    static_cast<float>(
                                        world_x) +
                                        0.5F,
                                    static_cast<float>(
                                        column.floor_y + 1) +
                                        0.001F,
                                    static_cast<float>(
                                        world_z) +
                                        0.5F,
                                },
                                direction_yaws[
                                    best_direction],
                            };
                        }
                    }
                }
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto backrooms_poolrooms_smoke_pose(
    int seed,
    int logical_level) -> std::optional<BackroomsSmokeCameraPose> {

    const BackroomsGenerator generator {
        seed,
        logical_level,
        kBackroomsSpatialConnectorDistrictModules,
        backrooms_runtime_pool_geometry_profile(
            WorldGenerationVersion::BackroomsV4),
    };
    const auto stack =
        backrooms_runtime_stack(seed, logical_level);
    const auto spawn =
        stack.spawn_block(logical_level);
    const auto placement =
        stack.placement_for_level(logical_level);
    if (!placement.has_value()) {
        return std::nullopt;
    }
    constexpr std::array<BackroomsJackGridPoint, 4U> directions {{
        {0, -1},
        {1, 0},
        {0, 1},
        {-1, 0},
    }};
    constexpr std::array<float, 4U> direction_yaws {{
        -90.0F,
        0.0F,
        90.0F,
        180.0F,
    }};

    const auto column_is_walkable =
        [](const BackroomsColumnSample& column) noexcept {
            return !column.wall &&
                   column.ceiling_y - column.floor_y >= 5;
        };
    const auto column_is_wet =
        [](const BackroomsColumnSample& column) noexcept {
            return water_level_from_state(
                       column.water_state) >
                   0U;
        };

    const auto find_pose =
        [&](bool require_active_light,
            bool require_broad_basin)
        -> std::optional<BackroomsSmokeCameraPose> {

        // Je parcours des anneaux fixes autour du spawn pour garder exactement
        // le meme cadrage avec une seed et un niveau identiques.
        for (auto radius = 0;
             radius <= kPoolroomsSmokeSearchRadius;
             ++radius) {
            std::optional<BackroomsSmokeCameraPose>
                best_pose {};
            auto best_score =
                (std::numeric_limits<int>::min)();

            for (auto offset_z = -radius;
                 offset_z <= radius;
                 ++offset_z) {
                for (auto offset_x = -radius;
                     offset_x <= radius;
                     ++offset_x) {
                    if (radius > 0 &&
                        std::abs(offset_x) != radius &&
                        std::abs(offset_z) != radius) {
                        continue;
                    }

                    const auto camera_x =
                        spawn.x + offset_x;
                    const auto camera_z =
                        spawn.z + offset_z;
                    const auto camera_column =
                        generator.sample_column(
                            camera_x,
                            camera_z);
                    if (!column_is_walkable(
                            camera_column) ||
                        column_is_wet(
                            camera_column)) {
                        continue;
                    }

                    for (std::size_t direction_index =
                             0U;
                         direction_index <
                         directions.size();
                         ++direction_index) {
                        const auto direction =
                            directions[direction_index];
                        auto first_wet_step = 0;
                        auto wet_depth = 0;

                        // Je garde le carrelage sec au premier plan et une
                        // ligne d'eau continue derriere, sans mur intermediaire.
                        for (auto step = 1;
                             step <=
                             kPoolroomsSmokeSightline;
                             ++step) {
                            const auto sight_column =
                                generator.sample_column(
                                    camera_x +
                                        direction.x *
                                            step,
                                    camera_z +
                                        direction.z *
                                            step);
                            if (!column_is_walkable(
                                    sight_column)) {
                                break;
                            }

                            if (column_is_wet(
                                    sight_column)) {
                                if (first_wet_step == 0) {
                                    first_wet_step =
                                        step;
                                }
                                ++wet_depth;
                            } else if (
                                first_wet_step != 0) {
                                break;
                            }
                        }

                        if (first_wet_step == 0 ||
                            first_wet_step >
                                kPoolroomsSmokeMaximumBasinDistance ||
                            wet_depth <
                                (require_broad_basin
                                     ? kPoolroomsSmokeMinimumWetDepth
                                     : 3)) {
                            continue;
                        }

                        const auto basin_depth_step =
                            first_wet_step +
                            std::min(wet_depth - 1, 2);
                        const BackroomsJackGridPoint
                            lateral {
                                -direction.z,
                                direction.x,
                            };
                        auto wet_width = 0;
                        for (auto lateral_step = -2;
                             lateral_step <= 2;
                             ++lateral_step) {
                            const auto basin_column =
                                generator.sample_column(
                                    camera_x +
                                        direction.x *
                                            basin_depth_step +
                                        lateral.x *
                                            lateral_step,
                                    camera_z +
                                        direction.z *
                                            basin_depth_step +
                                        lateral.z *
                                            lateral_step);
                            if (column_is_walkable(
                                    basin_column) &&
                                column_is_wet(
                                    basin_column)) {
                                ++wet_width;
                            }
                        }

                        // Je vérifie aussi le voisinage réel du bassin. Une
                        // rampe aperçue tout au fond du couloir ne suffit pas :
                        // la capture doit recevoir son éclairage sur l'eau.
                        const auto basin_center_x =
                            camera_x +
                            direction.x *
                                basin_depth_step;
                        const auto basin_center_z =
                            camera_z +
                            direction.z *
                                basin_depth_step;
                        auto active_light_near_basin =
                            false;
                        for (auto light_z =
                                 -kPoolroomsSmokeLightSearchRadius;
                             light_z <=
                                 kPoolroomsSmokeLightSearchRadius &&
                             !active_light_near_basin;
                             ++light_z) {
                            for (auto light_x =
                                     -kPoolroomsSmokeLightSearchRadius;
                                 light_x <=
                                     kPoolroomsSmokeLightSearchRadius;
                                 ++light_x) {
                                if (light_x * light_x +
                                        light_z * light_z >
                                    kPoolroomsSmokeLightSearchRadius *
                                        kPoolroomsSmokeLightSearchRadius) {
                                    continue;
                                }
                                active_light_near_basin =
                                    generator
                                        .sample_column(
                                            basin_center_x +
                                                light_x,
                                            basin_center_z +
                                                light_z)
                                        .light_state ==
                                    BackroomsLightState::
                                        Active;
                                if (active_light_near_basin) {
                                    break;
                                }
                            }
                        }
                        if ((require_broad_basin &&
                             wet_width <
                                 kPoolroomsSmokeMinimumWetWidth) ||
                            (require_active_light &&
                             !active_light_near_basin)) {
                            continue;
                        }

                        const auto score =
                            wet_depth * 16 +
                            wet_width * 8 -
                            first_wet_step * 2 +
                            (active_light_near_basin
                                 ? 28
                                 : 0) +
                            (camera_column.light_state ==
                                     BackroomsLightState::
                                         Active
                                 ? 12
                                 : 0);
                        if (score <= best_score) {
                            continue;
                        }

                        best_score = score;
                        best_pose =
                            BackroomsSmokeCameraPose {
                                {
                                    static_cast<float>(
                                        camera_x) +
                                        0.5F,
                                    static_cast<float>(
                                        placement->floor_y) +
                                        1.001F,
                                    static_cast<float>(
                                        camera_z) +
                                        0.5F,
                                },
                                direction_yaws[
                                    direction_index],
                            };
                    }
                }
            }

            if (best_pose.has_value()) {
                return best_pose;
            }
        }
        return std::nullopt;
    };

    // Je privilegie un bassin large sous un neon actif. Les replis ne
    // relachent qu'un critere a la fois afin de toujours montrer de l'eau.
    if (const auto lit_broad_pose =
            find_pose(true, true);
        lit_broad_pose.has_value()) {
        return lit_broad_pose;
    }
    if (const auto broad_pose =
            find_pose(false, true);
        broad_pose.has_value()) {
        return broad_pose;
    }
    if (const auto lit_pose =
            find_pose(true, false);
        lit_pose.has_value()) {
        return lit_pose;
    }
    return find_pose(false, false);
}

[[nodiscard]] auto backrooms_jack_smoke_pose(
    int seed,
    int logical_level,
    BackroomsJackSmokeMode mode,
    float requested_distance) -> std::optional<BackroomsSmokeCameraPose> {

    if (mode != BackroomsJackSmokeMode::CorridorStare &&
        mode != BackroomsJackSmokeMode::RearStare) {
        return std::nullopt;
    }

    const BackroomsGenerator generator {
        seed,
        logical_level,
        kBackroomsSpatialConnectorDistrictModules,
        backrooms_runtime_pool_geometry_profile(
            WorldGenerationVersion::BackroomsV4),
    };
    const BackroomsSpatialStack stack {
        seed,
        logical_level,
        backrooms_runtime_spatial_profile(
            WorldGenerationVersion::BackroomsV4),
    };
    const auto spawn = generator.spawn_block();
    const auto target_distance =
        mode == BackroomsJackSmokeMode::RearStare
            ? 24
            : std::max(
                  1,
                  static_cast<int>(
                      std::lround(
                          std::clamp(
                              finite_or(requested_distance, 40.0F),
                              kBackroomsJackSmokeCorridorDistanceMinimum,
                              kBackroomsJackSmokeCorridorDistanceMaximum))));
    constexpr std::array<BackroomsJackGridPoint, 4U> directions {{
        {1, 0},
        {0, 1},
        {-1, 0},
        {0, -1},
    }};
    constexpr std::array<float, 4U> direction_yaws {{
        0.0F,
        90.0F,
        180.0F,
        -90.0F,
    }};
    constexpr auto kMaximumSearchRadius =
        kBackroomsModuleSize * 8;
    const auto world_y_offset =
        backrooms_runtime_anchor_y_offset(
            seed,
            logical_level,
            logical_level);

    // Je choisis une vraie ligne de vue avant de charger le monde. La capture
    // ne peut donc plus placer Jack derriere l'escalier du hub ou une cloison
    // des Poolrooms, meme aux bornes exactes de 32 et 52 metres.
    for (auto radius = 0; radius <= kMaximumSearchRadius; ++radius) {
        std::optional<BackroomsSmokeCameraPose> best_pose {};
        auto best_score = (std::numeric_limits<int>::min)();
        for (auto offset_z = -radius; offset_z <= radius; ++offset_z) {
            for (auto offset_x = -radius; offset_x <= radius; ++offset_x) {
                if (radius > 0 &&
                    std::abs(offset_x) != radius &&
                    std::abs(offset_z) != radius) {
                    continue;
                }
                const auto camera_x = spawn.x + offset_x;
                const auto camera_z = spawn.z + offset_z;
                const auto camera_column =
                    generator.sample_column(camera_x, camera_z);
                const auto camera_clearance =
                    camera_column.ceiling_y -
                    (camera_column.floor_y + 1);
                if (camera_column.wall ||
                    camera_clearance < 3 ||
                    generator.connector_near(
                        camera_x,
                        camera_column.floor_y + 1,
                        camera_z,
                        14).has_value() ||
                    (logical_level <= -2 &&
                     water_level_from_state(camera_column.water_state) > 0U)) {
                    continue;
                }

                for (auto direction_index = std::size_t {0U};
                     direction_index < directions.size();
                     ++direction_index) {
                    const auto& direction = directions[direction_index];
                    const auto jack_x =
                        camera_x + direction.x * target_distance;
                    const auto jack_z =
                        camera_z + direction.z * target_distance;
                    const auto jack_column =
                        generator.sample_column(jack_x, jack_z);
                    const auto jack_clearance = static_cast<float>(
                        jack_column.ceiling_y -
                        (jack_column.floor_y + 1));
                    if (jack_column.wall ||
                        jack_column.floor_y != camera_column.floor_y ||
                        jack_clearance <
                            kBackroomsJackStandingClearance ||
                        generator.connector_near(
                            jack_x,
                            jack_column.floor_y + 1,
                            jack_z,
                            10).has_value()) {
                        continue;
                    }

                    const glm::vec3 local_camera {
                        static_cast<float>(camera_x) + 0.5F,
                        static_cast<float>(camera_column.floor_y + 1) +
                            0.001F,
                        static_cast<float>(camera_z) + 0.5F,
                    };
                    const glm::vec3 local_jack {
                        static_cast<float>(jack_x) + 0.5F,
                        static_cast<float>(jack_column.floor_y + 1) +
                            0.001F,
                        static_cast<float>(jack_z) + 0.5F,
                    };
                    const auto world_feet_y =
                        local_camera.y +
                        static_cast<float>(world_y_offset);
                    if (!stack.has_body_clearance(
                            local_camera.x,
                            world_feet_y,
                            local_camera.z,
                            3,
                            0.30F) ||
                        !stack.has_body_clearance(
                            local_jack.x,
                            world_feet_y,
                            local_jack.z,
                            5,
                            kBackroomsJackSmokeHalfWidth)) {
                        continue;
                    }
                    auto physical_corridor_clear = true;
                    for (auto step = 1; step < target_distance; ++step) {
                        if (!stack.has_body_clearance(
                                static_cast<float>(
                                    camera_x + direction.x * step) +
                                    0.5F,
                                world_feet_y,
                                static_cast<float>(
                                    camera_z + direction.z * step) +
                                    0.5F,
                                3,
                                0.08F)) {
                            physical_corridor_clear = false;
                            break;
                        }
                    }
                    if (!physical_corridor_clear) {
                        continue;
                    }
                    const auto jack_eye_height =
                        jack_clearance >= kBackroomsJackStandingClearance
                            ? 4.08F
                            : 2.88F;
                    if (!backrooms_jack_has_line_of_sight(
                            generator,
                            local_camera + glm::vec3 {0.0F, 1.62F, 0.0F},
                            local_jack +
                                glm::vec3 {0.0F, jack_eye_height, 0.0F})) {
                        continue;
                    }

                    const BackroomsJackGridPoint perpendicular {
                        -direction.z,
                        direction.x,
                    };
                    auto lateral_clearance = 0;
                    for (const auto side : {-1, 1}) {
                        lateral_clearance +=
                            generator.is_walkable(
                                camera_x + perpendicular.x * side,
                                camera_z + perpendicular.z * side)
                                ? 1
                                : 0;
                        lateral_clearance +=
                            generator.is_walkable(
                                jack_x + perpendicular.x * side,
                                jack_z + perpendicular.z * side)
                                ? 1
                                : 0;
                    }
                    const auto score =
                        lateral_clearance * 16 +
                        (camera_column.light_state ==
                                 BackroomsLightState::Active
                             ? 8
                             : 0) +
                        (jack_column.light_state ==
                                 BackroomsLightState::Active
                             ? 4
                             : 0);
                    if (score <= best_score) {
                        continue;
                    }

                    auto stored_yaw = direction_yaws[direction_index];
                    if (mode == BackroomsJackSmokeMode::RearStare) {
                        stored_yaw = std::remainder(
                            stored_yaw - 180.0F,
                            360.0F);
                    }
                    best_score = score;
                    best_pose = BackroomsSmokeCameraPose {
                        {
                            local_camera.x,
                            local_camera.y +
                                static_cast<float>(world_y_offset),
                            local_camera.z,
                        },
                        stored_yaw,
                    };
                }
            }
        }
        if (best_pose.has_value()) {
            return best_pose;
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto backrooms_smoke_camera_pose(
    int seed,
    bool blackout,
    int logical_level = 0,
    BackroomsJackSmokeMode jack_mode = BackroomsJackSmokeMode::None,
    float jack_distance = kBackroomsJackSmokeCorridorDistanceDefault)
    -> std::optional<BackroomsSmokeCameraPose> {
    if (jack_mode == BackroomsJackSmokeMode::CorridorStare ||
        jack_mode == BackroomsJackSmokeMode::RearStare) {
        return backrooms_jack_smoke_pose(
            seed,
            logical_level,
            jack_mode,
            jack_distance);
    }
    if (blackout && logical_level >= -1) {
        return backrooms_blackout_smoke_pose(
            seed);
    }
    if (logical_level <= -2) {
        if (const auto poolrooms_pose =
                backrooms_poolrooms_smoke_pose(
                    seed,
                    logical_level);
            poolrooms_pose.has_value()) {
            return poolrooms_pose;
        }
    }
    return BackroomsSmokeCameraPose {
        backrooms_spawn_position(
            seed,
            logical_level),
        -90.0F,
    };
}

void sanitize_backrooms_player_state(
    PlayerState& state,
    int seed,
    int logical_level = 0,
    WorldGenerationVersion version =
        WorldGenerationVersion::BackroomsV4) noexcept {
    const auto stack =
        backrooms_runtime_stack(seed, logical_level, version);
    const auto fallback =
        backrooms_spawn_position(seed, logical_level, version);
    state.position = finite_vec3_or(state.position, fallback);

    // Je réutilise la validation volumique de la pile : une position de
    // sauvegarde n'est légitime que si toute l'AABB du joueur est libre.
    if (!stack.has_body_clearance(
            state.position.x,
            state.position.y,
            state.position.z,
            3,
            0.30F)) {
        state.position = fallback;
    }

    // Une sauvegarde manipulée ou issue d'un ancien prototype ne doit jamais
    // réactiver le vol ou une interaction de bloc. Le contact aquatique sera
    // recalculé par la physique dès la première frame des Poolrooms.
    state.velocity = {};
    state.fly_mode = false;
    state.head_underwater = false;
    state.swimming = false;
    state.primary_action_active = false;
    state.secondary_action_active = false;
    state.primary_action_progress = 0.0F;
    state.secondary_action_progress = 0.0F;
    state.dead = false;
    state.death_cause = PlayerDeathCause::None;
    state.health = std::max(finite_or(state.health, 20.0F), 1.0F);
    state.air_seconds = 10.0F;
    state.hurt_timer = 0.0F;
    state.damage_cooldown = 0.0F;
    state.drowning_tick_timer = 0.0F;
    state.fall_start_y = state.position.y;
    state.airborne_time = 0.0F;
    state.on_ground = false;
}

void configure_backrooms_smoke_camera(
    PlayerController& player,
    int seed,
    const glm::vec3& position,
    float yaw_degrees,
    int logical_level = 0,
    bool ceiling_view = false) noexcept {

    auto state = player.state();
    state.position = position;
    sanitize_backrooms_player_state(
        state,
        seed,
        logical_level);

    // Je conserve le grand axe choisi par la pose. Le smoke normal regarde le
    // nord depuis le hub et la variante Blackout choisit son plus long couloir.
    state.yaw_degrees =
        finite_or(
            yaw_degrees,
            -90.0F);
    // Dans les Poolrooms je baisse un peu plus le regard : le premier plan
    // montre ainsi la rive et la surface turquoise, pas uniquement le mur
    // opposé. Les bureaux gardent leur cadrage historique plus horizontal.
    state.pitch_degrees =
        ceiling_view
            ? 32.0F
            : logical_level <= -2
                  ? -15.0F
                  : -8.0F;
    state.body_yaw_degrees = state.yaw_degrees;
    state.animation_time = 0.0F;
    state.step_phase = 0.0F;
    state.landing_impact = 0.0F;
    state.look_sway_yaw = 0.0F;
    state.look_sway_pitch = 0.0F;
    state.fall_start_y = state.position.y;
    state.airborne_time = 0.0F;
    state.on_ground = true;
    player.load_state(state);
}

struct BackroomsJackWorldLight {
    float sky_light = 0.0F;
    float block_light = 0.0F;
};

[[nodiscard]] auto safe_light_block_coordinate(
    float coordinate) noexcept -> std::optional<int> {
    if (!std::isfinite(coordinate)) {
        return std::nullopt;
    }
    constexpr auto minimum =
        static_cast<double>(
            std::numeric_limits<int>::lowest());
    constexpr auto maximum_exclusive =
        static_cast<double>(
            std::numeric_limits<int>::max()) +
        1.0;
    const auto value =
        static_cast<double>(coordinate);
    if (value < minimum ||
        value >= maximum_exclusive) {
        return std::nullopt;
    }
    return static_cast<int>(
        std::floor(value));
}

[[nodiscard]] auto sample_backrooms_jack_world_light(
    const World& world,
    const BackroomsJackRenderView& view)
    -> BackroomsJackWorldLight {
    if (!view.visible ||
        !std::isfinite(view.position.x) ||
        !std::isfinite(view.position.y) ||
        !std::isfinite(view.position.z)) {
        return {};
    }

    const auto hunch =
        std::clamp(
            std::isfinite(view.hunch_ratio)
                ? view.hunch_ratio
                : 0.0F,
            0.0F,
            1.0F);
    const auto body_height =
        kBackroomsJackStandingHeight +
        (kBackroomsJackBentHeight -
         kBackroomsJackStandingHeight) *
            hunch;
    const auto torso_y =
        body_height * 0.47F;
    const auto head_y =
        body_height - 0.28F;
    const std::array<glm::vec3, 7U> samples {{
        view.position +
            glm::vec3 {0.0F, 0.20F, 0.0F},
        view.position +
            glm::vec3 {0.0F, torso_y, 0.0F},
        view.position +
            glm::vec3 {0.42F, torso_y, 0.0F},
        view.position +
            glm::vec3 {-0.42F, torso_y, 0.0F},
        view.position +
            glm::vec3 {0.0F, torso_y, 0.42F},
        view.position +
            glm::vec3 {0.0F, torso_y, -0.42F},
        view.position +
            glm::vec3 {0.0F, head_y, 0.0F},
    }};

    auto maximum_sky =
        std::uint8_t {0U};
    auto maximum_block =
        std::uint8_t {0U};
    for (const auto& sample : samples) {
        const auto x =
            safe_light_block_coordinate(
                sample.x);
        const auto y =
            safe_light_block_coordinate(
                sample.y);
        const auto z =
            safe_light_block_coordinate(
                sample.z);
        if (!x.has_value() ||
            !y.has_value() ||
            !z.has_value() ||
            !is_world_y_valid(*y)) {
            continue;
        }

        // Je distingue une vraie valeur nulle d'un chunk absent. Jack ne doit
        // jamais devenir artificiellement lumineux pendant le streaming.
        const auto chunk_coord =
            world.world_to_chunk(
                *x,
                *z);
        if (world.find_chunk(
                chunk_coord) == nullptr) {
            continue;
        }
        maximum_sky =
            std::max(
                maximum_sky,
                world.get_sky_light(
                    *x,
                    *y,
                    *z));
        maximum_block =
            std::max(
                maximum_block,
                world.get_block_light(
                    *x,
                    *y,
                    *z));
    }

    constexpr auto inverse_maximum_light =
        1.0F / 15.0F;
    return {
        std::clamp(
            static_cast<float>(
                std::min<std::uint8_t>(
                    maximum_sky,
                    15U)) *
                inverse_maximum_light,
            0.0F,
            1.0F),
        std::clamp(
            static_cast<float>(
                std::min<std::uint8_t>(
                    maximum_block,
                    15U)) *
                inverse_maximum_light,
            0.0F,
            1.0F),
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

constexpr auto kIssouArenaProtectionRegionId =
    UINT64_C(0x4953534F550001);
constexpr auto kStartingVillageProtectionRegionId =
    UINT64_C(0x56494C4C41474501);

auto safe_horizontal_direction(
    const glm::vec3& direction,
    const glm::vec3& fallback =
        glm::vec3 {0.0F, 0.0F, -1.0F}) noexcept
    -> glm::vec3 {
    auto horizontal = glm::vec3 {
        direction.x,
        0.0F,
        direction.z,
    };
    const auto length_squared =
        glm::dot(horizontal, horizontal);
    if (!std::isfinite(length_squared) ||
        length_squared <= 1.0e-6F) {
        return fallback;
    }
    return horizontal /
           std::sqrt(length_squared);
}

constexpr auto kBackroomsJackScreamerHoldSeconds = 1.15F;
constexpr auto kBackroomsJackAudioReferenceDistance = 18.0F;
constexpr auto kBackroomsJackFogSafetyMargin = 2.0F;
constexpr auto kBackroomsJackVisibleDistanceCap = 64.0F;
constexpr auto kRadiansToDegrees = 57.29577951308232F;

struct BackroomsJackSpatialAudio {
    float pan = 0.0F;
    float attenuation = 1.0F;
};

[[nodiscard]] auto backrooms_jack_event_sfx(
    BackroomsJackEventKind kind) noexcept
    -> std::optional<GameSfxKind> {
    switch (kind) {
    case BackroomsJackEventKind::Notice:
        return GameSfxKind::JackNotice;
    case BackroomsJackEventKind::Chase:
        return GameSfxKind::JackChase;
    case BackroomsJackEventKind::BootStep:
    case BackroomsJackEventKind::DistantBootStep:
        return GameSfxKind::JackBootStep;
    case BackroomsJackEventKind::WoodenLegStep:
    case BackroomsJackEventKind::DistantWoodenLegStep:
        return GameSfxKind::JackPegStep;
    case BackroomsJackEventKind::Screamer:
        return GameSfxKind::JackScreamer;
    case BackroomsJackEventKind::Vanished:
    default:
        // Je garde la disparition silencieuse : elle doit laisser le joueur
        // douter de la présence de Jack au lieu de confirmer son départ.
        return std::nullopt;
    }
}

[[nodiscard]] auto backrooms_jack_event_volume(
    BackroomsJackEventKind kind) noexcept -> float {
    switch (kind) {
    case BackroomsJackEventKind::Notice:
        return 0.78F;
    case BackroomsJackEventKind::Chase:
        return 0.96F;
    case BackroomsJackEventKind::BootStep:
        return 0.76F;
    case BackroomsJackEventKind::WoodenLegStep:
        return 0.88F;
    case BackroomsJackEventKind::DistantBootStep:
        return 0.41F;
    case BackroomsJackEventKind::DistantWoodenLegStep:
        return 0.48F;
    case BackroomsJackEventKind::Screamer:
        return 1.0F;
    case BackroomsJackEventKind::Vanished:
    default:
        return 0.0F;
    }
}

[[nodiscard]] auto backrooms_jack_spatial_audio(
    const glm::vec3& listener_position,
    const glm::vec3& listener_look,
    const glm::vec3& source_position) noexcept
    -> BackroomsJackSpatialAudio {
    auto offset = finite_vec3_or(
        source_position - listener_position,
        glm::vec3 {0.0F});
    offset.y = 0.0F;
    const auto distance_squared =
        glm::dot(offset, offset);
    if (!std::isfinite(distance_squared) ||
        distance_squared <= 1.0e-6F) {
        return {};
    }

    const auto distance =
        std::sqrt(distance_squared);
    const auto direction =
        offset / distance;
    const auto forward =
        safe_horizontal_direction(listener_look);
    const auto right =
        glm::cross(
            forward,
            glm::vec3 {0.0F, 1.0F, 0.0F});
    const auto normalized_distance =
        distance /
        kBackroomsJackAudioReferenceDistance;
    return {
        std::clamp(
            glm::dot(direction, right),
            -1.0F,
            1.0F),
        std::clamp(
            1.0F /
                (1.0F +
                 normalized_distance *
                     normalized_distance),
            0.0F,
            1.0F),
    };
}

[[nodiscard]] auto backrooms_jack_maximum_visible_distance(
    const BackroomsTerminalFogSnapshot& snapshot,
    int world_seed,
    int logical_level) noexcept -> float {
    // Je garde deux metres de brouillard opaque au-dela de Jack. Une
    // silhouette choisie par l'IA ne peut ainsi jamais depasser la frontiere
    // que le GPU a reellement engagee lors de l'image precedente.
    return snapshot.safe_visible_distance(
        world_seed,
        logical_level,
        kBackroomsJackFogSafetyMargin,
        kBackroomsJackVisibleDistanceCap);
}

[[nodiscard]] auto backrooms_jack_chunk_readiness(
    const World& world,
    const Renderer& renderer,
    const glm::vec3& player_position) noexcept
    -> BackroomsJackChunkReadiness {
    BackroomsJackChunkReadiness readiness {};
    readiness.center_chunk =
        backrooms_jack_chunk_at(player_position);

    static_assert(
        kBackroomsJackReadinessCellCount ==
        static_cast<std::size_t>(
            kBackroomsJackReadinessChunkSide *
            kBackroomsJackReadinessChunkSide));
    const auto minimum_chunk_x =
        static_cast<std::int64_t>(readiness.center_chunk.x) -
        kBackroomsJackReadinessChunkRadius;
    const auto maximum_chunk_x =
        static_cast<std::int64_t>(readiness.center_chunk.x) +
        kBackroomsJackReadinessChunkRadius;
    const auto minimum_chunk_z =
        static_cast<std::int64_t>(readiness.center_chunk.z) -
        kBackroomsJackReadinessChunkRadius;
    const auto maximum_chunk_z =
        static_cast<std::int64_t>(readiness.center_chunk.z) +
        kBackroomsJackReadinessChunkRadius;
    if (minimum_chunk_x < std::numeric_limits<int>::lowest() ||
        maximum_chunk_x > std::numeric_limits<int>::max() ||
        minimum_chunk_z < std::numeric_limits<int>::lowest() ||
        maximum_chunk_z > std::numeric_limits<int>::max()) {
        return readiness;
    }

    for (auto delta_z = -kBackroomsJackReadinessChunkRadius;
         delta_z <= kBackroomsJackReadinessChunkRadius;
         ++delta_z) {
        for (auto delta_x = -kBackroomsJackReadinessChunkRadius;
             delta_x <= kBackroomsJackReadinessChunkRadius;
             ++delta_x) {
            const auto index =
                static_cast<std::size_t>(
                    (delta_z +
                     kBackroomsJackReadinessChunkRadius) *
                        kBackroomsJackReadinessChunkSide +
                    delta_x +
                    kBackroomsJackReadinessChunkRadius);
            const auto chunk_x =
                static_cast<std::int64_t>(
                    readiness.center_chunk.x) +
                delta_x;
            const auto chunk_z =
                static_cast<std::int64_t>(
                    readiness.center_chunk.z) +
                delta_z;
            if (chunk_x <
                    std::numeric_limits<int>::lowest() ||
                chunk_x >
                    std::numeric_limits<int>::max() ||
                chunk_z <
                    std::numeric_limits<int>::lowest() ||
                chunk_z >
                    std::numeric_limits<int>::max()) {
                readiness.ready[index] = false;
                continue;
            }
            const ChunkCoord chunk {
                static_cast<int>(chunk_x),
                static_cast<int>(chunk_z),
            };
            const auto revision =
                world.mesh_revision(chunk);
            const auto* world_chunk = world.find_chunk(chunk);
            // Je ne publie a Jack que la geometrie propre deja engagee sur le
            // GPU. L'A* et les rayons ne peuvent ainsi jamais anticiper un mur
            // encore visible dans son ancienne revision.
            readiness.ready[index] =
                world_chunk != nullptr &&
                !world_chunk->is_dirty() &&
                !world_chunk->is_lighting_dirty() &&
                revision > 0U &&
                renderer.world_mesh_uploaded(chunk, revision);
            readiness.mesh_revisions[index] =
                readiness.ready[index] ? revision : 0U;
        }
    }
    return readiness;
}

[[nodiscard]] auto backrooms_marlow_chunk_readiness(
    const World& world,
    const Renderer& renderer,
    const glm::vec3& player_position) noexcept
    -> BackroomsMarlowChunkReadiness {
    static_assert(
        kBackroomsMarlowReadinessCellCount ==
        kBackroomsJackReadinessCellCount);
    const auto jack = backrooms_jack_chunk_readiness(
        world,
        renderer,
        player_position);
    BackroomsMarlowChunkReadiness readiness {};
    readiness.center_chunk = jack.center_chunk;
    readiness.ready = jack.ready;
    readiness.mesh_revisions = jack.mesh_revisions;
    return readiness;
}

[[nodiscard]] auto backrooms_marlow_event_sfx(
    BackroomsMarlowEventKind kind) noexcept
    -> std::optional<GameSfxKind> {
    switch (kind) {
    case BackroomsMarlowEventKind::WaterSignal:
        return GameSfxKind::MarlowWaterSignal;
    case BackroomsMarlowEventKind::BuoyAppeared:
        return GameSfxKind::MarlowDistantSplash;
    case BackroomsMarlowEventKind::Surfaced:
        return GameSfxKind::MarlowSurface;
    case BackroomsMarlowEventKind::Submerged:
        return GameSfxKind::MarlowSubmerge;
    case BackroomsMarlowEventKind::GrabbedPlayer:
        return GameSfxKind::MarlowGrab;
    case BackroomsMarlowEventKind::Screamer:
        return GameSfxKind::MarlowScreamer;
    case BackroomsMarlowEventKind::Vanished:
    default:
        // Je garde aussi la disparition de Marlow silencieuse : le plongeon
        // est un signal physique, pas une confirmation artificielle du despawn.
        return std::nullopt;
    }
}

[[nodiscard]] auto backrooms_marlow_event_volume(
    BackroomsMarlowEventKind kind) noexcept -> float {
    switch (kind) {
    case BackroomsMarlowEventKind::WaterSignal:
        return 0.46F;
    case BackroomsMarlowEventKind::BuoyAppeared:
        return 0.38F;
    case BackroomsMarlowEventKind::Surfaced:
        return 0.76F;
    case BackroomsMarlowEventKind::Submerged:
        return 0.66F;
    case BackroomsMarlowEventKind::GrabbedPlayer:
        return 0.95F;
    case BackroomsMarlowEventKind::Screamer:
        return 1.0F;
    case BackroomsMarlowEventKind::Vanished:
    default:
        return 0.0F;
    }
}

[[nodiscard]] auto backrooms_flashlight_hits_water(
    const World& world,
    const glm::vec3& eye,
    const glm::vec3& look_direction,
    const BackroomsFlashlightState& flashlight) -> bool {
    if (backrooms_flashlight_intensity(flashlight) <= 0.0F) {
        return false;
    }
    auto direction = finite_vec3_or(
        look_direction,
        glm::vec3 {0.0F, 0.0F, -1.0F});
    const auto length_squared = glm::dot(direction, direction);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-6F) {
        return false;
    }
    direction /= std::sqrt(length_squared);
    // Je sonde le coeur du faisceau tous les 50 cm. Cela couvre les surfaces
    // horizontales proches sans lancer un raycast opaque qui ignorerait l'eau.
    for (auto step = 1; step <= 36; ++step) {
        const auto point = eye + direction * (static_cast<float>(step) * 0.5F);
        const auto x = safe_light_block_coordinate(point.x);
        const auto y = safe_light_block_coordinate(point.y);
        const auto z = safe_light_block_coordinate(point.z);
        if (x.has_value() && y.has_value() && z.has_value() &&
            is_world_y_valid(*y) && world.has_water(*x, *y, *z)) {
            return true;
        }
    }
    return false;
}

void translate_backrooms_marlow_result_y(
    BackroomsMarlowUpdateResult& result,
    float delta_y) noexcept {
    if (!std::isfinite(delta_y) || delta_y == 0.0F) {
        return;
    }
    for (auto& event : result.events) {
        event.position.y += delta_y;
    }
    result.render.position.y += delta_y;
    result.buoy.position.y += delta_y;
    result.interference.position.y += delta_y;
    result.capture.water_target.y += delta_y;
}

[[nodiscard]] auto requested_backrooms_jack_smoke_pose(
    BackroomsJackSmokeMode mode) noexcept
    -> std::optional<BackroomsJackSmokePose> {
    switch (mode) {
    case BackroomsJackSmokeMode::Standing:
        return BackroomsJackSmokePose::Standing;
    case BackroomsJackSmokeMode::Hunched:
        return BackroomsJackSmokePose::Bent;
    case BackroomsJackSmokeMode::Stare:
    case BackroomsJackSmokeMode::CorridorStare:
    case BackroomsJackSmokeMode::RearStare:
        return BackroomsJackSmokePose::Watching;
    case BackroomsJackSmokeMode::Chase:
        return BackroomsJackSmokePose::Chasing;
    case BackroomsJackSmokeMode::Jumpscare:
        return BackroomsJackSmokePose::Jumpscare;
    case BackroomsJackSmokeMode::None:
    default:
        return std::nullopt;
    }
}

[[nodiscard]] auto backrooms_jack_smoke_camera_yaw(
    float base_yaw_degrees,
    BackroomsJackSmokeMode mode) noexcept -> float {
    const auto safe_yaw =
        finite_or(base_yaw_degrees, -90.0F);
    // La variante Rear photographie le second temps de la scene : le joueur
    // vient de pivoter de 180 degres vers la silhouette placee dans son dos.
    return mode == BackroomsJackSmokeMode::RearStare
               ? safe_yaw + 180.0F
               : safe_yaw;
}

[[nodiscard]] auto backrooms_jack_yaw_facing(
    const glm::vec3& from,
    const glm::vec3& target) noexcept -> float {
    const auto direction =
        safe_horizontal_direction(
            target - from,
            glm::vec3 {0.0F, 0.0F, 1.0F});
    return std::atan2(
               direction.x,
               -direction.z) *
           kRadiansToDegrees;
}

[[nodiscard]] auto backrooms_jack_blocks_input_event(
    Uint32 event_type) noexcept -> bool {
    switch (event_type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    case SDL_TEXTEDITING:
    case SDL_TEXTINPUT:
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
        return true;
    default:
        return false;
    }
}

auto colossal_target_weight(
    EntityWeight weight) noexcept -> ColossalTargetWeight {
    switch (weight) {
    case EntityWeight::Normal:
        return ColossalTargetWeight::Normal;
    case EntityWeight::Heavy:
        return ColossalTargetWeight::Heavy;
    case EntityWeight::Boss:
        return ColossalTargetWeight::Boss;
    case EntityWeight::Light:
    default:
        return ColossalTargetWeight::Light;
    }
}

auto colossal_cell_material(
    BlockId block_id) noexcept -> ColossalCellMaterial {
    switch (static_cast<BlockType>(
        block_item_id(block_id))) {
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
        return ColossalCellMaterial::FragileFlower;
    case BlockType::TallGrass:
    case BlockType::DeadShrub:
        return ColossalCellMaterial::FragileGrass;
    case BlockType::Leaves:
    case BlockType::PineLeaves:
        return ColossalCellMaterial::FragileLeaves;
    case BlockType::Glass:
        return ColossalCellMaterial::FragileGlass;
    case BlockType::Torch:
    case BlockType::TorchWallPositiveX:
    case BlockType::TorchWallNegativeX:
    case BlockType::TorchWallPositiveZ:
    case BlockType::TorchWallNegativeZ:
        return ColossalCellMaterial::LightDecoration;
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Sand:
    case BlockType::Gravel:
    case BlockType::Snow:
        return ColossalCellMaterial::Soil;
    case BlockType::Wood:
    case BlockType::PineWood:
    case BlockType::Planks:
    case BlockType::Cactus:
        return ColossalCellMaterial::Wood;
    case BlockType::CoalOre:
    case BlockType::IronOre:
    case BlockType::GoldOre:
    case BlockType::DiamondOre:
    case BlockType::MetallicAlloyOre:
        return ColossalCellMaterial::Ore;
    case BlockType::Pastron:
    case BlockType::RoundShield:
    case BlockType::Sword:
    case BlockType::Spear:
    case BlockType::Pickaxe:
    case BlockType::Axe:
    case BlockType::Shovel:
    case BlockType::Musket:
    case BlockType::LeviathanSpine:
        return ColossalCellMaterial::Metal;
    case BlockType::Water:
        return ColossalCellMaterial::Liquid;
    case BlockType::Stone:
    case BlockType::Cobblestone:
    case BlockType::MossyStone:
        return ColossalCellMaterial::Stone;
    case BlockType::Air:
    default:
        return ColossalCellMaterial::Unknown;
    }
}

auto colossus_zone_center(
    const ChainedColossusState& state,
    DamageZoneId zone_id) noexcept -> glm::vec3 {
    auto local = glm::vec3 {0.0F, 2.55F, 0.0F};
    switch (zone_id) {
    case kColossusHeadZone:
        local = {0.0F, 4.15F, 0.0F};
        break;
    case kColossusLeftArmZone:
        local = {-1.10F, 2.85F, 0.0F};
        break;
    case kColossusRightArmZone:
        local = {1.10F, 2.85F, 0.0F};
        break;
    case kColossusLeftLegZone:
        local = {-0.48F, 1.05F, 0.0F};
        break;
    case kColossusRightLegZone:
        local = {0.48F, 1.05F, 0.0F};
        break;
    case kColossusHornZone:
        local = {0.0F, 4.72F, 0.03F};
        break;
    case kColossusTorsoZone:
    default:
        break;
    }
    const auto cosine = std::cos(state.yaw_radians);
    const auto sine = std::sin(state.yaw_radians);
    return state.position +
           glm::vec3 {
               local.x * cosine -
                   local.z * sine,
               local.y,
               local.x * sine +
                   local.z * cosine,
           };
}

auto colossal_blade_pose(
    const PlayerController& player,
    const ColossalWeaponStateSnapshot& weapon,
    LegendaryWeaponAwakening awakening)
    -> ColossalBladePose {
    const auto forward =
        safe_horizontal_direction(
            player.look_direction());
    auto right =
        glm::cross(
            forward,
            glm::vec3 {0.0F, 1.0F, 0.0F});
    const auto right_length_squared =
        glm::dot(right, right);
    if (right_length_squared <= 1.0e-6F) {
        right = {1.0F, 0.0F, 0.0F};
    } else {
        right /=
            std::sqrt(right_length_squared);
    }

    auto actor_transform = glm::mat4 {1.0F};
    actor_transform[0] = glm::vec4 {
        right,
        0.0F,
    };
    actor_transform[1] = glm::vec4 {
        0.0F,
        1.0F,
        0.0F,
        0.0F,
    };
    actor_transform[2] = glm::vec4 {
        -forward,
        0.0F,
    };
    actor_transform[3] = glm::vec4 {
        player.eye_position(),
        1.0F,
    };
    const auto pose =
        solve_leviathan_weapon_pose({
            actor_transform,
            LeviathanViewMode::FirstPerson,
            weapon.state,
            weapon.attack,
            awakening,
            weapon.state_progress,
            weapon.charge_progress,
            player.state().animation_time,
            weapon.contextual_vertical,
        });
    return {
        glm::vec3 {
            pose.root_transform *
            glm::vec4 {0.0F, 0.04F, 0.0F, 1.0F}},
        glm::vec3 {
            pose.root_transform *
            glm::vec4 {
                0.0F,
                kLeviathanWeaponVisualLengthBlocks,
                0.0F,
                1.0F,
            }},
    };
}

auto block_coord_from_position(const glm::vec3& position) noexcept -> BlockCoord {
    return {
        static_cast<int>(std::floor(position.x)),
        static_cast<int>(std::floor(position.y)),
        static_cast<int>(std::floor(position.z)),
    };
}

auto ability_cast_failure_detail(
    AbilityCastFailure failure) noexcept
    -> std::string_view {
    switch (failure) {
    case AbilityCastFailure::None:
        return "LANCEMENT REUSSI";
    case AbilityCastFailure::UnimplementedAbility:
        return "COMPETENCE EN PREPARATION";
    case AbilityCastFailure::AbilityNotLearned:
        return "COMPETENCE NON APPRISE";
    case AbilityCastFailure::AbilityNotEquipped:
        return "COMPETENCE NON EQUIPEE";
    case AbilityCastFailure::GlobalCooldown:
    case AbilityCastFailure::Cooldown:
    case AbilityCastFailure::NoCharges:
        return "RECHARGE EN COURS";
    case AbilityCastFailure::InsufficientEnergy:
        return "PAS ASSEZ D'ENERGIE";
    case AbilityCastFailure::InvalidTarget:
    case AbilityCastFailure::TargetOutOfRange:
        return "CIBLE INVALIDE";
    case AbilityCastFailure::MovingShipConstruction:
        return "CONSTRUCTION IMPOSSIBLE A BORD";
    case AbilityCastFailure::InvalidConstructionPlan:
        return "PLAN DE CHANTIER INVALIDE";
    case AbilityCastFailure::ExternalValidationRejected:
        return "ZONE NON CHARGEE";
    case AbilityCastFailure::ExternalCommitRejected:
        return "ACTION REFUSEE";
    case AbilityCastFailure::InvalidAbility:
    case AbilityCastFailure::PassiveAbility:
    case AbilityCastFailure::MissingCommitter:
    default:
        return "COMPETENCE INVALIDE";
    }
}

auto construction_plan_editor_failure_text(
    ConstructionPlanEditorFailure failure) noexcept
    -> std::string_view {
    switch (failure) {
    case ConstructionPlanEditorFailure::InvalidMaterial:
        return "MATERIAU INVALIDE OU ABSENT";
    case ConstructionPlanEditorFailure::PlanFull:
        return "LIMITE DU RANG ATTEINTE";
    case ConstructionPlanEditorFailure::CellMissing:
        return "AUCUNE CELLULE ICI";
    case ConstructionPlanEditorFailure::MirrorLocked:
        return "MAITRISE REQUISE POUR LE MIROIR";
    case ConstructionPlanEditorFailure::ConcurrentBuildMutation:
        return "BUILD MODIFIE - ROUVRE LE PLAN";
    case ConstructionPlanEditorFailure::InvalidPlan:
    case ConstructionPlanEditorFailure::InvalidShape:
        return "PLAN INVALIDE";
    case ConstructionPlanEditorFailure::Inactive:
    case ConstructionPlanEditorFailure::None:
    default:
        return "ACTION DE PLAN REFUSEE";
    }
}

auto inventory_material_count(
    const InventoryMenuState& inventory,
    const HotbarState& hotbar,
    BlockId block_id) noexcept -> std::uint32_t {
    const auto item_id =
        block_item_id(block_id);
    auto total = std::uint32_t {0U};
    const auto accumulate =
        [&](const HotbarSlot& slot) noexcept {
            if (inventory_slot_has_item(slot) &&
                block_item_id(slot.block_id) ==
                    item_id) {
                total += slot.count;
            }
        };
    for (const auto& slot : hotbar.slots) {
        accumulate(slot);
    }
    for (const auto& slot :
         inventory.storage_slots) {
        accumulate(slot);
    }
    if (inventory.carrying_item) {
        accumulate(inventory.carried_slot);
    }
    return total;
}

auto consume_inventory_materials(
    InventoryMenuState& inventory,
    HotbarState& hotbar,
    BlockId block_id,
    std::uint32_t count) noexcept -> bool {
    if (inventory_material_count(
            inventory,
            hotbar,
            block_id) < count) {
        return false;
    }
    const auto item_id =
        block_item_id(block_id);
    auto remaining = count;
    const auto consume =
        [&](HotbarSlot& slot) noexcept {
            if (remaining == 0U ||
                !inventory_slot_has_item(slot) ||
                block_item_id(slot.block_id) !=
                    item_id) {
                return;
            }
            const auto removed =
                std::min<std::uint32_t>(
                    remaining,
                    slot.count);
            static_cast<void>(
                inventory_take_from_slot(
                    slot,
                    static_cast<std::uint8_t>(
                        removed)));
            remaining -= removed;
        };
    for (auto& slot : hotbar.slots) {
        consume(slot);
    }
    for (auto& slot :
         inventory.storage_slots) {
        consume(slot);
    }
    if (inventory.carrying_item) {
        consume(inventory.carried_slot);
    }
    normalize_inventory_state(
        inventory,
        hotbar);
    return remaining == 0U;
}

auto horizontal_distance_squared(
    const glm::vec3& lhs,
    const glm::vec3& rhs) noexcept -> float {
    const auto dx = lhs.x - rhs.x;
    const auto dz = lhs.z - rhs.z;
    return dx * dx + dz * dz;
}

[[nodiscard]] auto horizontal_segment_distance_squared(
    const glm::vec3& point,
    const glm::vec3& segment_start,
    const glm::vec3& segment_end) noexcept -> float {
    const auto segment =
        glm::vec2 {
            segment_end.x - segment_start.x,
            segment_end.z - segment_start.z,
        };
    const auto relative =
        glm::vec2 {
            point.x - segment_start.x,
            point.z - segment_start.z,
        };
    const auto length_squared =
        glm::dot(segment, segment);
    const auto projection =
        length_squared > 1.0e-6F
            ? std::clamp(
                  glm::dot(relative, segment) /
                      length_squared,
                  0.0F,
                  1.0F)
            : 0.0F;
    const auto closest =
        segment_start +
        glm::vec3 {
            segment.x * projection,
            0.0F,
            segment.y * projection,
        };
    return horizontal_distance_squared(
        point,
        closest);
}

[[nodiscard]] auto is_construction_plan_material(
    BlockId block_id) noexcept -> bool {
    block_id =
        block_item_id(
            block_id);
    return is_placeable_item(block_id) &&
           block_id != to_block_id(BlockType::Air) &&
           block_id != to_block_id(BlockType::Water) &&
           !is_torch_block(block_id) &&
           is_block_collidable(block_id);
}

[[nodiscard]] auto cycle_construction_plan_material(
    const InventoryMenuState& inventory,
    const HotbarState& hotbar,
    std::uint16_t current_material,
    int direction) noexcept
    -> std::optional<std::uint16_t> {
    std::array<std::uint16_t, 256U>
        materials {};
    auto material_count =
        std::size_t {0U};
    for (std::uint32_t raw = 0U;
         raw <=
         static_cast<std::uint32_t>(
             std::numeric_limits<
                 BlockId>::max());
         ++raw) {
        const auto block_id =
            static_cast<BlockId>(raw);
        if (!is_construction_plan_material(
                block_id) ||
            inventory_material_count(
                inventory,
                hotbar,
                block_id) == 0U) {
            continue;
        }
        materials[material_count++] =
            static_cast<std::uint16_t>(
                block_id);
    }
    if (material_count == 0U) {
        return std::nullopt;
    }

    auto current_index =
        material_count;
    for (std::size_t index = 0U;
         index < material_count;
         ++index) {
        if (materials[index] ==
            current_material) {
            current_index = index;
            break;
        }
    }
    if (current_index ==
        material_count) {
        return direction < 0
                   ? materials[
                         material_count - 1U]
                   : materials[0U];
    }
    const auto offset =
        direction < 0
            ? material_count - 1U
            : 1U;
    return materials[
        (current_index + offset) %
        material_count];
}

[[nodiscard]] auto block_overlaps_actor(
    const BlockCoord& block,
    const glm::vec3& feet,
    float half_width,
    float height) noexcept -> bool {
    const auto block_min =
        glm::vec3 {
            static_cast<float>(block.x),
            static_cast<float>(block.y),
            static_cast<float>(block.z),
        };
    const auto block_max =
        block_min + glm::vec3 {1.0F};
    const auto actor_min =
        feet +
        glm::vec3 {
            -half_width,
            0.0F,
            -half_width,
        };
    const auto actor_max =
        feet +
        glm::vec3 {
            half_width,
            height,
            half_width,
        };
    return actor_min.x < block_max.x &&
           actor_max.x > block_min.x &&
           actor_min.y < block_max.y &&
           actor_max.y > block_min.y &&
           actor_min.z < block_max.z &&
           actor_max.z > block_min.z;
}

[[nodiscard]] auto ability_display_title(
    AbilityId id) noexcept -> std::string_view {
    switch (id) {
    case AbilityId::KnightVanguardStrike:
        return "FRAPPE DU VANGUARD";
    case AbilityId::KnightIronGuard:
        return "GARDE DE FER";
    case AbilityId::NinjaWindAcceleration:
        return "ACCELERATION DU VENT";
    case AbilityId::CommanderFootman:
        return "FANTASSIN INVOQUE";
    case AbilityId::BuilderConstructionPlan:
        return "PLAN DE CHANTIER";
    default:
        return "POUVOIR RUNIQUE";
    }
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
            const auto environment_state = current_environment_state();
            const auto creature_cycle = environment_.current_creature_cycle();
            const auto front_end_is_visible = front_end_visible();
            const auto maritime_gameplay_active =
                active_game_mode_ == GameMode::SeaAdventure &&
                sea_adventure_.active();
            const auto backrooms_gameplay_active =
                backrooms_active();
            const auto jack_session_active =
                session_backrooms_supports_jack();
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
                .backrooms_gameplay_active =
                    backrooms_gameplay_active,
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
            auto render_musket =
                player_musket_.view();
            // Je reflete la selection des cette image, meme si aucun pas fixe
            // n'a encore active le controleur depuis le changement de slot.
            render_musket.active =
                !backrooms_active() &&
                selected_musket_active() &&
                !player_.is_dead();
            prepare_legendary_presentation(
                environment_
                    .weather_time_seconds());
            if (jack_session_active &&
                !front_end_is_visible) {
                auto jack_render_view =
                    backrooms_jack_last_result_
                        .render;
                const auto jack_world_light =
                    sample_backrooms_jack_world_light(
                        world_,
                        jack_render_view);
                // Je prends ici la lumiere effectivement propagee dans le
                // monde. Jack reste donc noir dans une poche sans source et
                // retrouve naturellement ses details sous un vrai luminaire.
                jack_render_view.sky_light =
                    jack_world_light.sky_light;
                jack_render_view.block_light =
                    jack_world_light.block_light;
                renderer_.set_backrooms_jack(
                    jack_render_view,
                    backrooms_jack_last_result_
                        .light_interference);
            } else {
                // Je écrase chaque frame la vue précédente : Jack ne peut pas
                // survivre visuellement à une sortie ou un changement de mode.
                renderer_.set_backrooms_jack(
                    {},
                    {});
            }
            if (jack_session_active &&
                !front_end_is_visible) {
                const auto& marlow_view =
                    backrooms_marlow_last_result_.render;
                const auto light_anchor =
                    marlow_view.visible
                        ? marlow_view.position
                        : backrooms_marlow_last_result_.buoy.position;
                const auto marlow_world_light =
                    sample_backrooms_jack_world_light(
                        world_,
                        BackroomsJackRenderView {
                            .position = light_anchor,
                            .visible =
                                marlow_view.visible ||
                                backrooms_marlow_last_result_.buoy.visible,
                        });
                renderer_.set_backrooms_marlow(
                    backrooms_marlow_last_result_,
                    backrooms_elapsed_seconds_,
                    marlow_world_light.sky_light,
                    marlow_world_light.block_light);
            } else {
                renderer_.set_backrooms_marlow(
                    {},
                    0.0F,
                    0.0F,
                    0.0F);
            }
            frame_stats.render_preparation_ms =
                std::chrono::duration<double, std::milli>(clock::now() - render_preparation_begin).count();

            renderer_.set_progression_hud(
                progression_menu_.make_view_model(
                    player_build_,
                    progression_.level()),
                player_build_);
            rebuild_progression_creature_render_instances(
                environment_state);
            const auto render_begin = clock::now();
            renderer_.render_frame(
                world_,
                render_player(),
                render_musket,
                hotbar_,
                inventory_menu_,
                death_screen_,
                pause_menu_,
                main_menu_,
                save_slot_menu_,
                options_menu_,
                confirm_dialog_,
                progression_creature_render_instances_,
                sea_adventure_.crew_render_instances(),
                sea_adventure_.old_guard_render_instances(),
                sea_adventure_.old_guard_flashes(),
                sea_adventure_.old_guard_smoke(),
                player_musket_effects_.flashes(),
                player_musket_effects_.smoke(),
                item_drop_render_instances_,
                sea_adventure_.ship_render_state(),
                progression_.state(),
                super_vision_active_ && progression_.has_super_vision_power(),
                make_backrooms_flashlight_hud_view(
                    backrooms_flashlight_,
                    backrooms_gameplay_active),
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
                frame_stats.gpu_water_resolve_ms =
                    render_stats.gpu.water_resolve_ms;
                frame_stats.gpu_water_surface_ms =
                    render_stats.gpu.water_surface_ms;
                frame_stats.gpu_transparent_weather_ms =
                    render_stats.gpu.transparent_weather_ms;
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
    if (options_.smoke_test &&
        smoke_session_starts_gameplay(options_.smoke_session)) {
        const auto unique_suffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto temp_directory = std::filesystem::temp_directory_path();
        for (std::size_t attempt = 0U; attempt < 64U && !smoke_temp_root_.has_value(); ++attempt) {
            const auto candidate = temp_directory /
                                   (std::string("valcraft-session-smoke-") + unique_suffix + "-" +
                                     std::to_string(attempt));
            std::error_code create_error {};
            if (std::filesystem::create_directory(candidate, create_error)) {
                smoke_temp_root_ = candidate;
            } else if (create_error) {
                throw std::runtime_error("Unable to create the isolated session smoke directory");
            }
        }
        if (!smoke_temp_root_.has_value()) {
            throw std::runtime_error("Unable to reserve an isolated session smoke directory");
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

    if (options_.smoke_test &&
        smoke_session_starts_gameplay(options_.smoke_session)) {
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
            filename.starts_with("valcraft-session-smoke-")) {
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

        if (backrooms_jack_jumpscare_active() &&
            backrooms_jack_blocks_input_event(
                event.type)) {
            // Je laisse uniquement les événements de fenêtre et de fermeture
            // traverser le screamer. Aucun mouvement, regard, menu ou F ne
            // peut interrompre sa durée volontairement très courte.
            command_console_toggle_.cancel();
            pending_look_x_ = 0.0F;
            pending_look_y_ = 0.0F;
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
                reset_musket_interaction();
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
            if (progression_menu_.visible()) {
                break;
            }
            if (mouse_captured_) {
                const auto aim_ratio =
                    selected_musket_active()
                        ? std::clamp(
                              player_musket_.view().aim_ratio,
                              0.0F,
                              1.0F)
                        : 0.0F;
                const auto look_scale =
                    player_musket_look_scale(
                        aim_ratio);
                pending_look_x_ +=
                    static_cast<float>(event.motion.xrel) *
                    look_scale;
                pending_look_y_ +=
                    static_cast<float>(event.motion.yrel) *
                    look_scale;
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
            if (progression_menu_.visible()) {
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
            if (backrooms_active()) {
                // Dans les BackRooms, les clics restent réservés à la reprise
                // de capture de la souris : aucun bloc ni objet n'est manipulable.
                break;
            }
            if (event.button.button == SDL_BUTTON_LEFT) {
                if (selected_colossal_weapon_active()) {
                    colossal_primary_held_ = true;
                    pending_colossal_primary_press_ = true;
                    pending_colossal_primary_release_ = false;
                    pending_break_block_ = false;
                    pending_primary_attack_ = false;
                    player_.cancel_block_breaking();
                } else if (selected_musket_active()) {
                    musket_fire_held_ = true;
                    pending_musket_fire_press_ = true;
                    pending_break_block_ = false;
                    pending_primary_attack_ = false;
                    player_.cancel_block_breaking();
                } else {
                    pending_break_block_ = true;
                    pending_primary_attack_ = true;
                }
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
                if (selected_colossal_weapon_active()) {
                    colossal_guard_held_ = true;
                    pending_colossal_guard_press_ = true;
                    pending_colossal_guard_release_ = false;
                    pending_place_block_ = false;
                } else if (selected_musket_active()) {
                    musket_aim_held_ = true;
                    pending_place_block_ = false;
                } else {
                    pending_place_block_ = true;
                }
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
                if (colossal_primary_held_) {
                    pending_colossal_primary_release_ = true;
                }
                colossal_primary_held_ = false;
                musket_fire_held_ = false;
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
            } else if (
                event.button.button ==
                SDL_BUTTON_RIGHT) {
                if (colossal_guard_held_) {
                    pending_colossal_guard_release_ = true;
                }
                colossal_guard_held_ = false;
                musket_aim_held_ = false;
                pending_place_block_ = false;
            }
            break;
        case SDL_MOUSEWHEEL: {
            if (backrooms_active() ||
                confirm_dialog_.visible || death_screen_visible_ || paused_ || inventory_visible_ ||
                progression_menu_.visible() ||
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

            if (progression_menu_.visible()) {
                handle_progression_menu_keydown(
                    event.key);
                break;
            }

            if (!backrooms_active() &&
                event.key.repeat == 0 &&
                is_progression_menu_key(
                    event.key.keysym)) {
                set_progression_menu_visible(true);
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

            if (event.key.keysym.sym ==
                    SDLK_r &&
                issou_scenario_.active() &&
                (issou_scenario_.state()
                         .phase ==
                     IssouArenaPhase::Victory ||
                 issou_scenario_.state()
                         .phase ==
                     IssouArenaPhase::Defeat)) {
                static_cast<void>(
                    reset_issou_scenario());
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

            if (backrooms_active()) {
                if (is_backrooms_flashlight_action_key(
                        event.key.keysym)) {
                    static_cast<void>(
                        toggle_backrooms_flashlight(
                            backrooms_flashlight_));
                    mark_session_dirty();
                }
                // Les déplacements sont lus en continu plus bas ; toutes les
                // autres touches d'inventaire, pouvoir, vol et équipement sont
                // ignorées.
                break;
            }

            if (event.key.keysym.sym == SDLK_e) {
                if (!try_interact_legendary_weapon_quest()) {
                    set_inventory_visible(true);
                    record_audit_event(
                        AuditEventCategory::InputAction,
                        "inventory_open",
                        AuditSeverity::Info,
                        audit_json_object({
                            {"visible", audit_json_bool(inventory_visible_)},
                        }),
                        AuditPriority::Normal);
                }
            } else if (const auto ability_slot =
                           ability_slot_from_key(
                               event.key.keysym);
                       ability_slot.has_value() &&
                       event.key.repeat == 0) {
                pending_ability_slot_ =
                    *ability_slot;
            } else if (
                is_reload_action_key(
                    event.key.keysym) &&
                event.key.repeat == 0 &&
                selected_musket_active()) {
                pending_musket_reload_ = true;
                record_audit_event(
                    AuditEventCategory::InputAction,
                    "musket_reload_requested",
                    AuditSeverity::Trace,
                    audit_json_object({}),
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
        const auto environment_state =
            current_environment_state();
        if (backrooms_active()) {
            creatures_.clear();
        } else {
            creatures_.update(
                dt,
                world_,
                preview_player_.position(),
                environment_state,
                environment_.current_creature_cycle());
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
        (void)frame_stats;
        return;
    }

    if (!options_.smoke_test &&
        (death_screen_visible_ ||
         paused_ ||
         progression_menu_.visible())) {
        (void)dt;
        (void)frame_stats;
        return;
    }

    if (backrooms_active()) {
        update_backrooms_simulation(dt);
        (void)frame_stats;
        return;
    }

    update_gameplay_announcements(dt);
    ability_system_.update(
        player_build_,
        dt,
        player_ability_energy_parameters(
            player_build_));
    static_cast<void>(
        leviathan_knight_synergy_
            .advance(
                player_build_,
                dt));
    const auto ability_effect_update =
        player_ability_effects_.update(
            dt);
    if (ability_effect_update
            .iron_guard_expired) {
        AbilityEventPayload payload {};
        payload.ability_id =
            AbilityId::KnightIronGuard;
        payload.source_id = 1U;
        payload.position =
            player_.position();
        static_cast<void>(
            ability_system_
                .publish_logical_event(
                    AbilityEventType::Expired,
                    ability_effect_update
                        .iron_guard_cast_sequence,
                    payload));
    }
    if (ability_effect_update
            .expired_effect_count != 0U) {
        sync_selected_hotbar_slot();
    }
    melee_attack_cooldown_remaining_ =
        std::max(
            melee_attack_cooldown_remaining_ -
                dt,
            0.0F);
    wind_dodge_remaining_ =
        std::max(
            wind_dodge_remaining_ - dt,
            0.0F);
    if (seconds_since_successful_shield_block_ >=
        0.0F) {
        seconds_since_successful_shield_block_ +=
            dt;
        if (seconds_since_successful_shield_block_ >
            3.0F) {
            seconds_since_successful_shield_block_ =
                -1.0F;
        }
    }
    if (wind_acceleration_remaining_ > 0.0F) {
        wind_acceleration_remaining_ =
            std::max(
                wind_acceleration_remaining_ -
                    dt,
                0.0F);
        if (wind_acceleration_remaining_ <=
            0.0F) {
            if (wind_acceleration_cast_sequence_ !=
                0U) {
                AbilityEventPayload payload {};
                payload.ability_id =
                    AbilityId::NinjaWindAcceleration;
                payload.source_id = 1U;
                payload.position =
                    player_.position();
                static_cast<void>(
                    ability_system_
                        .publish_logical_event(
                            AbilityEventType::Expired,
                            wind_acceleration_cast_sequence_,
                            payload));
                wind_acceleration_cast_sequence_ =
                    0U;
            }
            wind_movement_bonus_ = 0.0F;
            wind_recovery_bonus_ = 0.0F;
            wind_blade_available_ = false;
            sync_selected_hotbar_slot();
        }
    }

    environment_.set_frozen(options_.freeze_time || options_.smoke_test);
    environment_.update(dt);
    const auto environment_state = environment_.current_state();
    const auto wind_velocity =
        current_wind_velocity(
            environment_state);
    player_musket_effects_.update(
        dt,
        wind_velocity);
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
            !selected_musket_active() &&
            !selected_colossal_weapon_active() &&
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

        update_colossal_weapon(
            dt,
            input,
            gameplay_input_enabled,
            maritime_session_active);

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
                award_player_experience(
                    FishingExperienceEvent {},
                    block_coord_from_position(
                        player_.position()),
                    "fishing_common");
            }
            if (sea_result.consumed_food) {
                queue_gameplay_announcement("VIVRES", "RATION CONSOMMEE", 2.2F);
            }
            if (sea_result.consumed_water) {
                queue_gameplay_announcement("EAU", "RESERVE CONSOMMEE", 2.2F);
            }
            if (sea_result.crew_fish_delivered) {
                queue_gameplay_announcement("EQUIPAGE", "POISSON RANGE DANS LA CALE", 2.4F);
                award_player_experience(
                    FirstDeliveryExperienceEvent {
                        0U,
                    },
                    block_coord_from_position(
                        player_.position()),
                    "crew_first_fish_delivery");
            }
            if (sea_result.crew_water_delivered) {
                queue_gameplay_announcement("EQUIPAGE", "EAU RANGEE DANS LA CALE", 2.4F);
                award_player_experience(
                    FirstDeliveryExperienceEvent {
                        1U,
                    },
                    block_coord_from_position(
                        player_.position()),
                    "crew_first_water_delivery");
            }
            if (sea_result.departure_started) {
                queue_gameplay_announcement("LARGUEZ LES AMARRES", "DEPART DU PORT", 3.0F);
                award_player_experience(
                    DepartureExperienceEvent {},
                    block_coord_from_position(
                        player_.position()),
                    "sea_departure");
            }
            if (sea_result.reached_open_sea) {
                queue_gameplay_announcement("CAP SUR LE LARGE", "NAVIGATION DE CROISIERE", 3.0F);
                award_player_experience(
                    OpenSeaReachedExperienceEvent {},
                    block_coord_from_position(
                        player_.position()),
                    "open_sea_reached");
            }
            const auto route_distance =
                sea_adventure_
                    .save_state()
                    .route_distance;
            const auto route_meters =
                std::isfinite(route_distance) &&
                        route_distance > 0.0F
                    ? static_cast<std::uint64_t>(
                          std::floor(
                              static_cast<double>(
                                  route_distance)))
                    : 0ULL;
            award_player_experience(
                NavigationExperienceEvent {
                    route_meters,
                },
                block_coord_from_position(
                    player_.position()),
                "sea_navigation");
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
            !player_.is_dead()) {
            resolve_pending_player_ability(
                maritime_session_active);
        } else {
            pending_ability_slot_.reset();
        }

        const auto musket_active =
            gameplay_input_enabled &&
            mouse_captured_ &&
            !player_.is_dead() &&
            selected_musket_active();
        PlayerMusketInput musket_input {};
        musket_input.active = musket_active;
        musket_input.aim_held =
            musket_active &&
            musket_aim_held_;
        musket_input.fire_held =
            musket_active &&
            musket_fire_held_;
        musket_input.fire_pressed =
            musket_active &&
            std::exchange(
                pending_musket_fire_press_,
                false);
        musket_input.reload_pressed =
            musket_active &&
            std::exchange(
                pending_musket_reload_,
                false);
        musket_input.damage_multiplier =
            progression_.attack_damage_multiplier();

        const auto& musket_events =
            player_musket_.update(
                musket_input,
                dt,
                player_.look_direction());

        if (musket_active) {
            // Je reserve les deux boutons au fusil tant que son icone reste
            // selectionnee, meme lorsque la chambre est vide.
            pending_break_block_ = false;
            pending_primary_attack_ = false;
            pending_place_block_ = false;
            player_.cancel_block_breaking();
        }

        if (musket_events.chamber_state_changed &&
            selected_musket_active()) {
            auto& selected =
                hotbar_.slots[
                    normalize_hotbar_index(
                        hotbar_.selected_index)];
            set_musket_loaded(
                selected,
                musket_events.loaded_after);
            mark_session_dirty();
        }

        if (musket_events.fired) {
            music_.play_sfx(
                GameSfxKind::MusketShot,
                1.0F);

            const auto viewmodel =
                build_player_viewmodel_parts(
                    player_,
                    to_block_id(
                        BlockType::Musket),
                    player_musket_.view());
            const auto visual_forward =
                safe_drop_direction(
                    viewmodel.pose
                        .muzzle_forward);
            const auto muzzle_position =
                finite_vec3_or(
                    viewmodel.pose
                        .muzzle_position,
                    player_.eye_position() +
                        visual_forward * 0.78F);
            auto inherited_velocity =
                finite_vec3_or(
                    player_.state().velocity,
                    glm::vec3 {0.0F});
            if (maritime_session_active) {
                inherited_velocity +=
                    sea_adventure_.ship_entity()
                        .velocity();
            }
            player_musket_effects_.spawn(
                muzzle_position,
                visual_forward,
                inherited_velocity,
                wind_velocity,
                musket_events.shot_sequence);
            resolve_player_musket_shot(
                musket_events,
                maritime_session_active);
        } else if (musket_events.dry_fired) {
            queue_gameplay_announcement(
                "FUSIL VIDE",
                "R POUR RECHARGER",
                1.8F);
        }

        if (musket_events.reload_started) {
            queue_gameplay_announcement(
                "RECHARGEMENT",
                "GARDE LE FUSIL EN MAIN",
                1.8F);
        } else if (musket_events.reload_completed) {
            queue_gameplay_announcement(
                "FUSIL PRET",
                "UN COUP CHARGE",
                1.5F);
        }

        if (pending_primary_attack_ &&
            melee_attack_cooldown_remaining_ >
                0.0F) {
            pending_primary_attack_ = false;
        }
        if (gameplay_input_enabled &&
            pending_primary_attack_ &&
            !selected_colossal_weapon_active() &&
            !player_.is_dead() &&
            melee_attack_cooldown_remaining_ <=
                0.0F) {

            pending_primary_attack_ = false;
            if (const auto weapon = inventory_active_weapon_stats(inventory_menu_, hotbar_); weapon.has_value()) {
                player_.trigger_primary_action();
                music_.play_sfx(GameSfxKind::SwordSwing, 0.72F);
                const auto agility =
                    player_attribute_value(
                        player_build_.attributes,
                        PlayerAttribute::Agility);
                melee_attack_cooldown_remaining_ =
                    0.22F /
                    std::max(
                        0.25F,
                        player_melee_recovery_multiplier(
                            agility) *
                            (1.0F +
                             wind_recovery_bonus_));
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

                const auto strength =
                    player_attribute_value(
                        player_build_.attributes,
                        PlayerAttribute::Strength);
                const auto damage =
                    weapon->damage *
                    player_melee_damage_multiplier(
                        progression_.level(),
                        strength);
                const auto wind_blade_was_armed =
                    std::exchange(
                        wind_blade_available_,
                        false);
                const auto wind_rank =
                    player_ability_rank(
                        player_build_,
                        AbilityId::
                            NinjaWindAcceleration);
                const auto* wind_rank_definition =
                    ability_rank_definition(
                        AbilityId::
                            NinjaWindAcceleration,
                        wind_rank);
                const auto wind_blade_damage =
                    wind_rank_definition != nullptr
                        ? std::max(
                              wind_rank_definition
                                  ->values[
                                      kWindBladeDamageValueIndex],
                              0.0F)
                        : 0.0F;
                const auto wind_blade_range =
                    wind_rank_definition != nullptr
                        ? std::max(
                              wind_rank_definition
                                  ->values[
                                      kWindBladeRangeValueIndex],
                              0.0F)
                        : 0.0F;
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
                            grant_creature_kill_rewards(
                                hit_result,
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
                        if (wind_blade_was_armed &&
                            wind_blade_damage > 0.0F &&
                            wind_blade_range > 0.0F) {
                            const auto origin =
                                player_.eye_position();
                            const auto direction =
                                safe_drop_direction(
                                    player_.look_direction());
                            const auto maximum_projection =
                                hit_result.distance +
                                wind_blade_range;
                            auto selected_id =
                                CreatureId {0U};
                            auto selected_projection =
                                maximum_projection +
                                1.0F;
                            for (const auto& candidate :
                                 creatures_
                                     .active_creatures()) {
                                const auto candidate_id =
                                    creature_id_from_anchor(
                                        candidate.anchor);
                                if (candidate_id ==
                                        hit_result.id ||
                                    !is_hostile_creature(
                                        candidate)) {
                                    continue;
                                }
                                const auto aim_position =
                                    candidate.position +
                                    glm::vec3 {
                                        0.0F,
                                        0.75F,
                                        0.0F,
                                    };
                                const auto delta =
                                    aim_position -
                                    origin;
                                const auto projection =
                                    glm::dot(
                                        delta,
                                        direction);
                                if (projection <=
                                        hit_result.distance +
                                            0.10F ||
                                    projection >
                                        maximum_projection) {
                                    continue;
                                }
                                const auto lateral =
                                    delta -
                                    direction *
                                        projection;
                                if (glm::dot(
                                        lateral,
                                        lateral) >
                                    0.90F * 0.90F) {
                                    continue;
                                }
                                const auto target_distance =
                                    glm::length(delta);
                                if (!std::isfinite(
                                        target_distance) ||
                                    target_distance <=
                                        0.06F) {
                                    continue;
                                }
                                const auto target_ray =
                                    delta /
                                    target_distance;
                                const auto maximum_ray =
                                    target_distance -
                                    0.06F;
                                const auto obstruction =
                                    world_
                                        .raycast_collidable(
                                            origin,
                                            target_ray,
                                            maximum_ray);
                                if (obstruction.hit ||
                                    (maritime_session_active &&
                                     sea_adventure_
                                         .ship_entity()
                                         .raycast_collidable_distance(
                                             origin,
                                             target_ray,
                                             maximum_ray)
                                         .has_value())) {
                                    continue;
                                }
                                if (projection <
                                        selected_projection ||
                                    (projection ==
                                         selected_projection &&
                                     candidate_id <
                                         selected_id)) {
                                    selected_id =
                                        candidate_id;
                                    selected_projection =
                                        projection;
                                }
                            }
                            if (selected_id != 0U) {
                                const auto blade =
                                    creatures_.apply_damage(
                                        selected_id,
                                        wind_blade_damage *
                                            player_ninja_damage_multiplier(
                                                progression_
                                                    .level(),
                                                agility),
                                        CreatureDamageSource::
                                            PlayerAbility,
                                        direction);
                                if (blade.hit) {
                                    grant_creature_kill_rewards(
                                        blade,
                                        "wind_blade");
                                }
                            }
                        }
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
            pending_break_block_ &&
            !selected_colossal_weapon_active()) {
            const auto break_target = player_.current_target(world_);
            const auto target_was_player_placed =
                break_target.hit &&
                world_.was_player_placed(
                    break_target.block.x,
                    break_target.block.y,
                    break_target.block.z);
            const auto tool_speed_multiplier =
                break_target.hit ? selected_tool_break_speed_multiplier(break_target.block_id) : 1.0F;
            if (const auto broken_block =
                    player_.update_block_breaking(world_, dt, true, break_target, tool_speed_multiplier);
                broken_block.has_value()) {
                record_performance_event(
                    PerformanceEventKind::BlockBreak,
                    broken_block->block,
                    inventory_item_label(broken_block->block_id));
                award_player_experience(
                    HarvestExperienceEvent {
                        broken_block->block_id,
                        target_was_player_placed,
                    },
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
            !selected_colossal_weapon_active() &&
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

    if (!issou_scenario_.active()) {
        creatures_.update(
            dt,
            world_,
            player_.position(),
            environment_state,
            creature_cycle);
        update_summoned_footman(
            dt,
            maritime_session_active);
    }
    update_legendary_weapon_quest();
    update_legendary_encounters(
        dt,
        maritime_session_active);
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
        const auto trigger_iron_guard_reaction =
            [&](const IronGuardDamageInterceptionResult&
                    interception) {
                const auto& reactive =
                    interception.reactive;
                if (!reactive.triggered) {
                    return;
                }

                const auto energy =
                    player_ability_energy_parameters(
                        player_build_);
                player_build_.val_energy =
                    std::min(
                        energy.maximum_energy,
                        player_build_.val_energy +
                            reactive.energy_refund);
                if (player_build_.revision !=
                    std::numeric_limits<
                        std::uint64_t>::max()) {
                    ++player_build_.revision;
                }

                std::array<
                    CreatureId,
                    kCreatureMaxActiveCount>
                    wave_targets {};
                auto wave_target_count =
                    std::size_t {0U};
                const auto radius =
                    std::max(
                        reactive.wave_radius,
                        0.0F);
                for (const auto& creature :
                     creatures_.active_creatures()) {
                    if (!is_hostile_creature(
                            creature) ||
                        horizontal_distance_squared(
                            player_.position(),
                            creature.position) >
                            radius * radius ||
                        wave_target_count >=
                            wave_targets.size()) {
                        continue;
                    }
                    wave_targets[
                        wave_target_count++] =
                        creature_id_from_anchor(
                            creature.anchor);
                }

                for (std::size_t index = 0U;
                     index < wave_target_count;
                     ++index) {
                    const auto hit =
                        creatures_.apply_damage(
                            wave_targets[index],
                            reactive.wave_damage,
                            CreatureDamageSource::
                                PlayerAbility,
                            player_.look_direction());
                    grant_creature_kill_rewards(
                        hit,
                        "iron_guard_reactive");
                    if (!hit.hit) {
                        continue;
                    }
                    AbilityEventPayload hit_payload {};
                    hit_payload.ability_id =
                        AbilityId::KnightIronGuard;
                    hit_payload.source_id = 1U;
                    hit_payload.target_id =
                        hit.id;
                    hit_payload.position =
                        hit.position;
                    hit_payload.primary_value =
                        hit.damage;
                    hit_payload.detail_code =
                        hit.killed ? 1U : 0U;
                    static_cast<void>(
                        ability_system_
                            .publish_logical_event(
                                AbilityEventType::Hit,
                                reactive.cast_sequence,
                                hit_payload));
                }

                AbilityEventPayload
                    blocked_payload {};
                blocked_payload.ability_id =
                    AbilityId::KnightIronGuard;
                blocked_payload.source_id = 1U;
                blocked_payload.position =
                    player_.position();
                blocked_payload.primary_value =
                    interception.absorbed_damage;
                blocked_payload.secondary_value =
                    reactive.energy_refund;
                blocked_payload.detail_code = 1U;
                static_cast<void>(
                    ability_system_
                        .publish_logical_event(
                            AbilityEventType::Blocked,
                            reactive.cast_sequence,
                            blocked_payload));
                queue_gameplay_announcement(
                    "FER REACTIF",
                    "ONDE DEFENSIVE - 5 EV",
                    1.4F);
            };

        for (const auto& attack :
             creatures_.recent_attacks()) {
            if (!std::isfinite(attack.damage) ||
                attack.damage <= 0.0F) {
                continue;
            }

            const auto player_target =
                player_.position() +
                glm::vec3 {
                    0.0F,
                    0.85F,
                    0.0F,
                };
            const auto attack_delta =
                player_target - attack.origin;
            const auto attack_distance =
                glm::length(attack_delta);
            if (!std::isfinite(attack_distance) ||
                attack_distance <= 1.0e-4F) {
                continue;
            }
            const auto attack_ray =
                attack_delta / attack_distance;
            const auto occlusion_distance =
                std::max(
                    attack_distance - 0.06F,
                    0.0F);
            if (world_
                    .raycast_collidable(
                        attack.origin,
                        attack_ray,
                        occlusion_distance)
                    .hit ||
                (sea_adventure_.active() &&
                 sea_adventure_
                     .ship_entity()
                     .raycast_collidable_distance(
                         attack.origin,
                         attack_ray,
                         occlusion_distance)
                     .has_value())) {
                continue;
            }

            auto* protecting_footman =
                static_cast<SummonedUnitSystem*>(
                    nullptr);
            auto protecting_distance =
                0.95F * 0.95F;
            for (auto& footman :
                 summoned_footmen_) {
                const auto state =
                    footman.state();
                if (!state.active ||
                    horizontal_distance_squared(
                        player_.position(),
                        state.position) >
                        3.0F * 3.0F) {
                    continue;
                }
                const auto distance =
                    horizontal_segment_distance_squared(
                        state.position,
                        attack.origin,
                        player_target);
                if (distance <=
                    protecting_distance) {
                    protecting_distance =
                        distance;
                    protecting_footman =
                        &footman;
                }
            }
            if (protecting_footman != nullptr) {
                const auto intercepted =
                    protecting_footman
                        ->apply_damage({
                            attack.damage,
                            attack.kind ==
                                    CreatureAttackKind::
                                        Projectile
                                ? SummonedUnitDamageKind::
                                      Projectile
                                : SummonedUnitDamageKind::
                                      Melee,
                        });
                if (intercepted.handled) {
                    continue;
                }
            }

            if (wind_dodge_remaining_ > 0.0F) {
                wind_dodge_remaining_ = 0.0F;
                queue_gameplay_announcement(
                    "ESQUIVE",
                    "LE VENT DETOURNE L'ATTAQUE",
                    1.4F);
                continue;
            }

            const auto interception =
                player_ability_effects_
                    .intercept_iron_guard_damage(
                        attack.damage);
            if (interception.absorbed) {
                trigger_iron_guard_reaction(
                    interception);
                continue;
            }
            auto remaining_damage =
                interception.accepted
                    ? interception
                          .remaining_damage
                    : attack.damage;

            auto to_attacker =
                attack.origin -
                player_.position();
            to_attacker.y = 0.0F;
            auto look =
                player_.look_direction();
            look.y = 0.0F;
            const auto to_attacker_length_squared =
                glm::dot(
                    to_attacker,
                    to_attacker);
            const auto look_length_squared =
                glm::dot(look, look);
            const auto frontal_alignment =
                to_attacker_length_squared >
                            1.0e-6F &&
                        look_length_squared >
                            1.0e-6F
                    ? glm::dot(
                          to_attacker /
                              std::sqrt(
                                  to_attacker_length_squared),
                          look /
                              std::sqrt(
                                  look_length_squared))
                    : -1.0F;
            const auto frontal =
                frontal_alignment >= 0.5F;
            const auto temporary_effects =
                player_ability_effects_
                    .aggregate(
                        player_.max_health());
            if (attack.kind ==
                    CreatureAttackKind::
                        Projectile &&
                frontal) {
                remaining_damage *=
                    std::clamp(
                        1.0F -
                            temporary_effects
                                .frontal_projectile_reduction,
                        0.0F,
                        1.0F);
            }

            auto colossal_guard_blocked = false;
            if (colossal_weapon_drawn()) {
                const auto attacker_profile =
                    creature_combat_profile(
                        attack.species,
                        CreaturePhase::Night);
                const auto target_weight =
                    static_cast<ColossalTargetWeight>(
                        static_cast<std::uint8_t>(
                            attacker_profile.weight));
                const auto weapon_state =
                    colossal_weapon_.snapshot();
                const auto guard_result =
                    intercept_colossal_guard({
                            remaining_damage,
                            1.0F,
                            frontal_alignment,
                            weapon_state.stability,
                            weapon_state.state ==
                                    ColossalWeaponState::Guard
                                ? weapon_state
                                      .state_elapsed_seconds
                                : 0.0F,
                            target_weight,
                            attack.kind ==
                                    CreatureAttackKind::
                                        Projectile
                                ? ColossalIncomingAttackKind::
                                      Projectile
                                : ColossalIncomingAttackKind::
                                      Melee,
                            weapon_state.state ==
                                ColossalWeaponState::Guard,
                            false,
                        });
                if (guard_result.blocked) {
                    colossal_guard_blocked = true;
                    remaining_damage =
                        guard_result.resulting_damage;
                    record_legendary_quest_tutorial(
                        LegendaryQuestAction::
                            TutorialGuardSucceeded,
                        attack.target_id == 0U
                            ? 1U
                            : attack.target_id,
                        attack.damage);
                    if (guard_result.perfect) {
                        issou_scenario_.notify_combat_event(
                            IssouArenaCombatEvent::
                                PerfectGuard);
                        music_.play_sfx(
                            GameSfxKind::
                                PerfectGuard,
                            1.0F);
                        queue_gameplay_announcement(
                            "GARDE PARFAITE",
                            "L'IMPACT EST RETOURNE",
                            1.35F);
                    }
                }
            }

            const auto shield_blocking =
                frontal &&
                !colossal_guard_blocked &&
                !colossal_weapon_drawn() &&
                inventory_slot_has_item(
                    inventory_menu_
                        .equipment_slots[
                            equipment_slot_index(
                                EquipmentSlot::
                                    Shield)]) &&
                player_.state()
                    .secondary_action_active;
            if (shield_blocking) {
                constexpr auto
                    kActiveShieldDamageRetained =
                        0.35F;
                remaining_damage *=
                    kActiveShieldDamageRetained;
                seconds_since_successful_shield_block_ =
                    0.0F;
            }

            const auto damage =
                player_
                    .apply_external_damage_report(
                        remaining_damage,
                        PlayerDeathCause::
                            Zombie);
            if (!damage.applied()) {
                continue;
            }

            auto knockback_direction =
                attack.direction;
            knockback_direction.y = 0.0F;
            auto knockback_length_squared =
                glm::dot(
                    knockback_direction,
                    knockback_direction);
            if (knockback_length_squared <=
                1.0e-6F) {
                knockback_direction =
                    player_.position() -
                    attack.origin;
                knockback_direction.y = 0.0F;
                knockback_length_squared =
                    glm::dot(
                        knockback_direction,
                        knockback_direction);
            }
            if (knockback_length_squared >
                1.0e-6F) {
                constexpr auto
                    kCreatureKnockbackSpeed =
                        2.0F;
                const auto knockback =
                    knockback_direction /
                    std::sqrt(
                        knockback_length_squared) *
                    kCreatureKnockbackSpeed *
                    temporary_effects
                        .knockback_multiplier();
                auto velocity =
                    player_.state().velocity;
                velocity.x += knockback.x;
                velocity.z += knockback.z;
                player_.set_velocity(
                    velocity);
            }
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

        consume_ability_logical_events();

        if (player_.is_dead()) {
            if (issou_scenario_.active()) {
                issou_scenario_.notify_player_death();
            }
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

void Game::consume_ability_logical_events() {
    std::array<
        AbilityLogicalEvent,
        kAbilityLogicalEventCapacity>
        events {};
    const auto count =
        ability_system_
            .drain_logical_events(
                events);
    if (count == 0U ||
        !audit_ ||
        !audit_->enabled()) {
        return;
    }

    auto critical_count =
        std::size_t {0U};
    auto hit_count =
        std::size_t {0U};
    for (std::size_t index = 0U;
         index < count;
         ++index) {
        critical_count +=
            events[index].priority ==
                    AbilityEventPriority::
                        Critical
                ? 1U
                : 0U;
        hit_count +=
            events[index].type ==
                    AbilityEventType::Hit
                ? 1U
                : 0U;
    }

    // Je consomme la file après la simulation : les observateurs ne peuvent
    // jamais décider du résultat d'un sort ni saturer le gameplay à long terme.
    record_audit_event(
        AuditEventCategory::InputAction,
        "ability_logical_events",
        AuditSeverity::Info,
        audit_json_object({
            {
                "count",
                audit_json_number(
                    count),
            },
            {
                "critical_count",
                audit_json_number(
                    critical_count),
            },
            {
                "hit_count",
                audit_json_number(
                    hit_count),
            },
        }),
        critical_count != 0U
            ? AuditPriority::High
            : AuditPriority::Normal);
}

void Game::resolve_pending_player_ability(
    bool maritime_session_active) {
    if (!pending_ability_slot_.has_value()) {
        return;
    }

    const auto slot =
        *std::exchange(
            pending_ability_slot_,
            std::nullopt);
    if (slot >=
        player_build_.equipped_abilities.size()) {
        return;
    }

    const auto ability_id =
        player_build_.equipped_abilities[slot];
    if (!ability_id_is_valid(ability_id)) {
        queue_gameplay_announcement(
            "EMPLACEMENT VIDE",
            "EQUIPE UNE COMPETENCE AVEC P",
            2.0F);
        return;
    }

    struct AbilityRuntimeContext {
        struct DeferredEvent {
            AbilityEventType type =
                AbilityEventType::Hit;
            AbilityEventPayload payload {};
        };

        Game* game = nullptr;
        AbilityId ability = AbilityId::None;
        bool maritime = false;
        WeaponStats weapon {2.0F, 3.0F};
        std::array<
            VanguardTargetCandidate,
            kCreatureMaxActiveCount>
            vanguard_candidates {};
        VanguardTargetSelection
            vanguard_targets {};
        std::size_t summon_slot =
            std::numeric_limits<std::size_t>::max();
        glm::vec3 summon_position {0.0F};
        std::optional<glm::vec3>
            summon_ship_local_position {};
        RaycastHit construction_hit {};
        std::array<
            WorldEditCell,
            kWorldEditShapeMaximumCellCount>
            authorized_cells {};
        std::array<
            bool,
            kWorldEditShapeMaximumCellCount>
            construction_was_player_placed {};
        std::size_t authorized_cell_count = 0U;
        HotbarState hotbar_before {};
        InventoryMenuState inventory_before {};
        WorldEditTransactionResult transaction {};
        std::array<
            DeferredEvent,
            kCreatureMaxActiveCount * 2U +
                kWorldEditShapeMaximumCellCount +
                2U>
            deferred_events {};
        std::size_t deferred_event_count =
            0U;
        std::uint32_t
            construction_xp_cell_count =
                0U;
        bool missing_materials = false;
        bool no_authorized_cells = false;

        void defer_event(
            AbilityEventType type,
            const AbilityEventPayload&
                payload) noexcept {
            if (deferred_event_count >=
                deferred_events.size()) {
                return;
            }
            deferred_events[
                deferred_event_count++] = {
                type,
                payload,
            };
        }
    };

    AbilityRuntimeContext context {};
    context.game = this;
    context.ability = ability_id;
    context.maritime =
        maritime_session_active;
    context.hotbar_before = hotbar_;
    context.inventory_before =
        inventory_menu_;

    AbilityCastRequest request {};
    request.id = ability_id;
    const auto origin =
        player_.eye_position();
    const auto direction =
        safe_drop_direction(
            player_.look_direction());
    request.event_payload.ability_id =
        ability_id;
    request.event_payload.source_id = 1U;
    request.event_payload.position =
        player_.position();
    request.event_payload.direction =
        direction;

    switch (ability_id) {
    case AbilityId::KnightVanguardStrike: {
        context.weapon =
            inventory_active_weapon_stats(
                inventory_menu_,
                hotbar_)
                .value_or(
                    WeaponStats {
                        2.0F,
                        3.0F,
                    });
        auto candidate_count =
            std::size_t {0U};
        for (const auto& creature :
             creatures_.active_creatures()) {
            if (candidate_count >=
                context
                    .vanguard_candidates
                    .size()) {
                break;
            }
            context.vanguard_candidates[
                candidate_count++] = {
                creature_id_from_anchor(
                    creature.anchor),
                creature.position +
                    glm::vec3 {
                        0.0F,
                        0.75F,
                        0.0F,
                    },
                is_hostile_creature(
                    creature),
            };
        }
        context.vanguard_targets =
            select_vanguard_targets(
                VanguardTargetingQuery {
                .origin = origin,
                .forward = direction,
                .range_meters =
                    context.weapon.range,
                .half_angle_degrees =
                    45.0F,
                .candidates =
                    std::span<
                        const VanguardTargetCandidate> {
                        context
                            .vanguard_candidates
                            .data(),
                        candidate_count,
                    },
                .visibility_user_data =
                    &context,
                .is_visible =
                    +[](void* user_data,
                        const glm::vec3&
                            ray_origin,
                        const glm::vec3&
                            ray_direction,
                        float target_distance)
                        noexcept {
                    const auto& runtime =
                        *static_cast<
                            AbilityRuntimeContext*>(
                            user_data);
                    constexpr auto
                        kTargetTolerance =
                            0.06F;
                    const auto maximum =
                        std::max(
                            0.0F,
                            target_distance -
                                kTargetTolerance);
                    const auto world_hit =
                        runtime.game
                            ->world_
                            .raycast_collidable(
                                ray_origin,
                                ray_direction,
                                maximum);
                    if (world_hit.hit) {
                        return false;
                    }
                    if (!runtime.maritime) {
                        return true;
                    }
                    return !runtime.game
                                ->sea_adventure_
                                .ship_entity()
                                .raycast_collidable_distance(
                                    ray_origin,
                                    ray_direction,
                                    maximum)
                                .has_value();
                },
            });
        request.target_valid =
            !context.vanguard_targets
                 .empty();
        request.target_distance_meters =
            request.target_valid
                ? glm::length(
                      context
                          .vanguard_targets
                          .targets[0U]
                          .aim_position -
                      origin)
                : 0.0F;
        request.effective_range_meters =
            context.weapon.range;
        request
            .seconds_since_successful_shield_block =
            seconds_since_successful_shield_block_;
        break;
    }
    case AbilityId::KnightIronGuard:
        request.target_valid = true;
        break;
    case AbilityId::NinjaWindAcceleration:
        request.target_valid = true;
        break;
    case AbilityId::CommanderFootman: {
        for (std::size_t index = 0U;
             index < summoned_footmen_.size();
             ++index) {
            if (!summoned_footmen_[index]
                     .active()) {
                context.summon_slot =
                    index;
                break;
            }
        }
        if (context.summon_slot >=
            summoned_footmen_.size()) {
            request.target_valid = false;
            break;
        }

        const auto on_ship =
            maritime_session_active &&
            player_
                .dynamic_support_height(
                    sea_adventure_
                        .ship_entity())
                .has_value();
        if (on_ship) {
            const auto horizontal =
                safe_drop_direction({
                    direction.x,
                    0.0F,
                    direction.z,
                });
            auto candidate =
                player_.position() +
                horizontal * 2.0F;
            const auto& ship =
                sea_adventure_
                    .ship_entity();
            auto support =
                ship.support_height_in_range(
                    candidate,
                    player_.position().y -
                        2.0F,
                    player_.position().y +
                        2.0F);
            if (!support.has_value()) {
                candidate =
                    sea_adventure_
                        .deck_spawn_position();
                support =
                    ship.support_height_in_range(
                        candidate,
                        candidate.y - 2.0F,
                        candidate.y + 2.0F);
            }
            if (support.has_value()) {
                candidate.y =
                    *support + 0.002F;
                context.summon_position =
                    candidate;
                context
                    .summon_ship_local_position =
                    ship.world_to_local_point(
                        candidate);
                request.target_valid = true;
                request.ground_target_valid =
                    true;
                request.target_distance_meters =
                    glm::length(
                        candidate -
                        player_.position());
            }
        } else {
            const auto hit =
                world_.raycast_collidable(
                    origin,
                    direction,
                    8.0F);
            if (hit.hit) {
                context.summon_position = {
                    static_cast<float>(
                        hit.block.x) +
                        0.5F,
                    static_cast<float>(
                        hit.block.y) +
                        1.002F,
                    static_cast<float>(
                        hit.block.z) +
                        0.5F,
                };
                request.target_valid = true;
                request.ground_target_valid =
                    true;
                request.target_distance_meters =
                    hit.distance;
            }
        }
        break;
    }
    case AbilityId::BuilderConstructionPlan: {
        const auto on_ship =
            maritime_session_active &&
            player_
                .dynamic_support_height(
                    sea_adventure_
                        .ship_entity())
                .has_value();
        const auto ship_velocity =
            maritime_session_active
                ? sea_adventure_
                      .ship_entity()
                      .velocity()
                : glm::vec3 {0.0F};
        request.on_moving_ship =
            on_ship &&
            glm::dot(
                ship_velocity,
                ship_velocity) >
                0.0001F;
        if (request.on_moving_ship) {
            // Je fournis une cible syntaxiquement valide pour que le système
            // retourne précisément le refus de construction permanente à bord.
            request.target_valid = true;
            request.ground_target_valid =
                true;
            request.target_distance_meters =
                0.0F;
            break;
        }
        context.construction_hit =
            world_.raycast_collidable(
                origin,
                direction,
                8.0F);
        request.target_valid =
            context.construction_hit.hit;
        if (request.target_valid &&
            maritime_session_active) {
            const auto ship_hit =
                sea_adventure_
                    .ship_entity()
                    .raycast_collidable_distance(
                        origin,
                        direction,
                        context.construction_hit
                            .distance);
            if (ship_hit.has_value() &&
                *ship_hit <=
                    context.construction_hit
                        .distance) {
                request.target_valid = false;
            }
        }
        request.ground_target_valid =
            request.target_valid;
        request.target_distance_meters =
            context.construction_hit.distance;
        request.construction_cell_count =
            0U;
        break;
    }
    default:
        request.target_valid = true;
        request.ground_target_valid = true;
        break;
    }

    const auto validate =
        +[](void* user_data,
            const AbilityCastRequest&,
            const AbilityCastResolution& resolution) noexcept -> bool {
        auto& runtime =
            *static_cast<
                AbilityRuntimeContext*>(
                user_data);
        auto& game =
            *runtime.game;

        switch (resolution.id) {
        case AbilityId::KnightVanguardStrike:
            if (runtime.vanguard_targets
                    .empty()) {
                return false;
            }
            for (std::size_t target_index =
                     0U;
                 target_index <
                 runtime.vanguard_targets
                     .target_count;
                 ++target_index) {
                const auto id =
                    runtime.vanguard_targets
                        .targets[
                            target_index]
                        .id;
                const auto found =
                    std::any_of(
                        game.creatures_
                            .active_creatures()
                            .begin(),
                        game.creatures_
                            .active_creatures()
                            .end(),
                        [&](const CreatureInstance&
                                creature) noexcept {
                            return is_hostile_creature(
                                       creature) &&
                                   creature_id_from_anchor(
                                       creature.anchor) ==
                                       id;
                        });
                if (!found) {
                    return false;
                }
            }
            return true;
        case AbilityId::KnightIronGuard:
            return true;
        case AbilityId::NinjaWindAcceleration:
            return true;
        case AbilityId::CommanderFootman: {
            if (runtime.summon_slot >=
                    game.summoned_footmen_
                        .size() ||
                game.summoned_footmen_[
                        runtime.summon_slot]
                    .active()) {
                return false;
            }
            const auto& position =
                runtime.summon_position;
            if (!std::isfinite(position.x) ||
                !std::isfinite(position.y) ||
                !std::isfinite(position.z)) {
                return false;
            }
            if (runtime
                    .summon_ship_local_position
                    .has_value()) {
                const auto support =
                    game.sea_adventure_
                        .ship_entity()
                        .support_height_in_range(
                            position,
                            position.y - 0.25F,
                            position.y + 0.25F);
                if (!support.has_value()) {
                    return false;
                }
            } else {
                const auto feet =
                    block_coord_from_position(
                        position);
                const auto chunk =
                    game.world_.world_to_chunk(
                        feet.x,
                        feet.z);
                if (!is_world_y_valid(
                        feet.y) ||
                    !is_world_y_valid(
                        feet.y + 1) ||
                    game.world_
                            .find_chunk(chunk) ==
                        nullptr ||
                    feet.y <= 0 ||
                    !is_block_collidable(
                        game.world_.get_block(
                            feet.x,
                            feet.y - 1,
                            feet.z)) ||
                    game.world_.has_water(
                        feet.x,
                        feet.y,
                        feet.z) ||
                    game.world_.has_water(
                        feet.x,
                        feet.y + 1,
                        feet.z) ||
                    !is_block_replaceable(
                        game.world_.get_block(
                            feet.x,
                            feet.y,
                            feet.z)) ||
                    !is_block_replaceable(
                        game.world_.get_block(
                            feet.x,
                            feet.y + 1,
                            feet.z))) {
                    return false;
                }
            }
            if (horizontal_distance_squared(
                    position,
                    game.player_.position()) <
                0.85F * 0.85F) {
                return false;
            }
            for (const auto& creature :
                 game.creatures_
                     .active_creatures()) {
                if (horizontal_distance_squared(
                        position,
                        creature.position) <
                    0.90F * 0.90F) {
                    return false;
                }
            }
            for (const auto& footman :
                 game.summoned_footmen_) {
                const auto state =
                    footman.state();
                if (state.active &&
                    horizontal_distance_squared(
                        position,
                        state.position) <
                        0.90F * 0.90F) {
                    return false;
                }
            }
            return true;
        }
        case AbilityId::BuilderConstructionPlan: {
            runtime.authorized_cell_count =
                0U;
            runtime.missing_materials =
                false;
            runtime.no_authorized_cells =
                false;
            if (!runtime.construction_hit.hit) {
                return false;
            }

            const auto look =
                game.player_
                    .look_direction();
            auto forward_x = 0;
            auto forward_z = 1;
            if (std::abs(look.x) >=
                    std::abs(look.z) &&
                std::abs(look.x) >
                    0.0001F) {
                forward_x =
                    look.x < 0.0F ? -1 : 1;
                forward_z = 0;
            } else if (std::abs(look.z) >
                       0.0001F) {
                forward_z =
                    look.z < 0.0F ? -1 : 1;
            }
            const auto right_x =
                forward_z;
            const auto right_z =
                -forward_x;
            const auto placement_origin =
                runtime
                    .construction_hit
                    .adjacent;
            std::array<
                WorldEditCell,
                kWorldEditShapeMaximumCellCount>
                generated {};
            auto generated_count =
                std::size_t {0U};
            const auto append_cell =
                [&](const ConstructionPlanCell&
                        source,
                    int local_x) noexcept {
                if (source.material_id >
                    std::numeric_limits<
                        BlockId>::max()) {
                    return false;
                }
                const auto material =
                    static_cast<BlockId>(
                        source.material_id);
                if (!is_construction_plan_material(
                        material)) {
                    return false;
                }
                const auto world_x =
                    static_cast<std::int64_t>(
                        placement_origin.x) +
                    static_cast<std::int64_t>(
                        right_x) *
                        local_x +
                    static_cast<std::int64_t>(
                        forward_x) *
                        source.z;
                const auto world_y =
                    static_cast<std::int64_t>(
                        placement_origin.y) +
                    source.y;
                const auto world_z =
                    static_cast<std::int64_t>(
                        placement_origin.z) +
                    static_cast<std::int64_t>(
                        right_z) *
                        local_x +
                    static_cast<std::int64_t>(
                        forward_z) *
                        source.z;
                if (world_x <
                        std::numeric_limits<int>::
                            min() ||
                    world_x >
                        std::numeric_limits<int>::
                            max() ||
                    world_y <
                        std::numeric_limits<int>::
                            min() ||
                    world_y >
                        std::numeric_limits<int>::
                            max() ||
                    world_z <
                        std::numeric_limits<int>::
                            min() ||
                    world_z >
                        std::numeric_limits<int>::
                            max()) {
                    return false;
                }
                const BlockCoord coordinate {
                    static_cast<int>(world_x),
                    static_cast<int>(world_y),
                    static_cast<int>(world_z),
                };
                for (std::size_t index = 0U;
                     index < generated_count;
                     ++index) {
                    if (generated[index]
                            .coordinate ==
                        coordinate) {
                        return generated[index]
                                   .block_id ==
                               material;
                    }
                }
                if (generated_count >=
                    generated.size()) {
                    return false;
                }
                generated[generated_count++] = {
                    coordinate,
                    material,
                };
                return true;
            };

            const auto& plan =
                resolution
                    .construction_plan;
            for (std::size_t index = 0U;
                 index < plan.cell_count;
                 ++index) {
                const auto& cell =
                    plan.cells[index];
                if (!append_cell(
                        cell,
                        static_cast<int>(
                            cell.x))) {
                    return false;
                }
                if (ability_effects_contain(
                        resolution.effects,
                        AbilityEffectFlag::
                            ConstructionMirror) &&
                    cell.x != 0 &&
                    !append_cell(
                        cell,
                        -static_cast<int>(
                            cell.x))) {
                    return false;
                }
            }
            if (generated_count == 0U ||
                generated_count >
                    resolution
                        .maximum_construction_cells) {
                return false;
            }

            const auto cell_is_occupied =
                [&](const BlockCoord& coordinate)
                    noexcept {
                if (block_overlaps_actor(
                        coordinate,
                        game.player_
                            .position(),
                        0.30F,
                        1.80F)) {
                    return true;
                }
                for (const auto& creature :
                     game.creatures_
                         .active_creatures()) {
                    if (block_overlaps_actor(
                            coordinate,
                            creature.position,
                            0.42F,
                            1.55F)) {
                        return true;
                    }
                }
                for (const auto& footman :
                     game
                         .summoned_footmen_) {
                    const auto state =
                        footman.state();
                    if (state.active &&
                        block_overlaps_actor(
                            coordinate,
                            state.position,
                            0.40F,
                            1.75F)) {
                        return true;
                    }
                }
                if (runtime.maritime) {
                    const auto minimum =
                        glm::vec3 {
                            static_cast<float>(
                                coordinate.x),
                            static_cast<float>(
                                coordinate.y),
                            static_cast<float>(
                                coordinate.z),
                        };
                    if (game.sea_adventure_
                            .ship_entity()
                            .intersects_aabb(
                                minimum,
                                minimum +
                                    glm::vec3 {
                                        1.0F,
                                    })) {
                        return true;
                    }
                }
                return false;
            };

            for (std::size_t generated_index =
                     0U;
                 generated_index <
                 generated_count;
                 ++generated_index) {
                const auto& cell =
                    generated[generated_index];
                const auto& coordinate =
                    cell.coordinate;
                if (!is_world_y_valid(
                        coordinate.y) ||
                    game.world_
                            .find_chunk(
                                game.world_
                                    .world_to_chunk(
                                        coordinate.x,
                                        coordinate.z)) ==
                        nullptr) {
                    continue;
                }
                const auto current =
                    game.world_.get_block(
                        coordinate.x,
                        coordinate.y,
                        coordinate.z);
                if (!is_block_replaceable(
                        current) ||
                    is_block_liquid(current) ||
                    game.world_.has_water(
                        coordinate.x,
                        coordinate.y,
                        coordinate.z) ||
                    cell_is_occupied(
                        coordinate)) {
                    continue;
                }
                if (runtime
                        .authorized_cell_count <
                    runtime.authorized_cells
                        .size()) {
                    const auto authorized_index =
                        runtime
                            .authorized_cell_count;
                    runtime.authorized_cells[
                        authorized_index] =
                        cell;
                    runtime
                        .construction_was_player_placed[
                            authorized_index] =
                        game.world_
                            .was_player_placed(
                                coordinate.x,
                                coordinate.y,
                                coordinate.z);
                    ++runtime
                          .authorized_cell_count;
                }
            }
            if (runtime
                    .authorized_cell_count == 0U) {
                runtime.no_authorized_cells =
                    true;
                return false;
            }
            std::array<
                std::uint32_t,
                256U>
                required_materials {};
            for (std::size_t index = 0U;
                 index <
                 runtime
                     .authorized_cell_count;
                 ++index) {
                ++required_materials[
                    runtime
                        .authorized_cells[index]
                        .block_id];
            }
            for (std::size_t material = 0U;
                 material <
                 required_materials.size();
                 ++material) {
                const auto required =
                    required_materials[material];
                if (required == 0U) {
                    continue;
                }
                if (inventory_material_count(
                        game.inventory_menu_,
                        game.hotbar_,
                        static_cast<BlockId>(
                            material)) <
                    required) {
                    runtime.missing_materials =
                        true;
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
        }
    };

    const auto commit =
        +[](void* user_data,
            const AbilityCastRequest&,
            const AbilityCastResolution& resolution) noexcept -> bool {
        auto& runtime =
            *static_cast<
                AbilityRuntimeContext*>(
                user_data);
        auto& game =
            *runtime.game;
        try {
            switch (resolution.id) {
            case AbilityId::KnightVanguardStrike: {
                std::array<
                    CreatureId,
                    kCreatureMaxActiveCount>
                    secondary_targets {};
                auto secondary_count =
                    std::size_t {0U};
                const auto primary_contains =
                    [&](CreatureId id) noexcept {
                    for (std::size_t index = 0U;
                         index <
                         runtime.vanguard_targets
                             .target_count;
                         ++index) {
                        if (runtime
                                .vanguard_targets
                                .targets[index]
                                .id == id) {
                            return true;
                        }
                    }
                    return false;
                };
                if (ability_effects_contain(
                        resolution.effects,
                        AbilityEffectFlag::
                            VanguardSecondaryImpact)) {
                    const auto radius =
                        std::max(
                            resolution.values[2],
                            0.0F);
                    for (const auto& creature :
                         game.creatures_
                             .active_creatures()) {
                        const auto id =
                            creature_id_from_anchor(
                            creature.anchor);
                        if (primary_contains(id) ||
                            !is_hostile_creature(
                                creature) ||
                            horizontal_distance_squared(
                                creature.position,
                                runtime
                                    .vanguard_targets
                                    .targets[0U]
                                    .aim_position) >
                                radius * radius ||
                            secondary_count >=
                                secondary_targets
                                    .size()) {
                            continue;
                        }
                        const auto target =
                            creature.position +
                            glm::vec3 {
                                0.0F,
                                0.75F,
                                0.0F,
                            };
                        const auto ray =
                            target -
                            game.player_
                                .eye_position();
                        const auto distance =
                            glm::length(ray);
                        if (!std::isfinite(distance) ||
                            distance <= 0.06F) {
                            continue;
                        }
                        const auto line =
                            ray / distance;
                        const auto maximum =
                            distance - 0.06F;
                        if (game.world_
                                .raycast_collidable(
                                    game.player_
                                        .eye_position(),
                                    line,
                                    maximum)
                                .hit ||
                            (runtime.maritime &&
                             game.sea_adventure_
                                 .ship_entity()
                                 .raycast_collidable_distance(
                                     game.player_
                                         .eye_position(),
                                     line,
                                     maximum)
                                 .has_value())) {
                            continue;
                        }
                        secondary_targets[
                            secondary_count++] =
                            id;
                    }
                }

                const auto strength =
                    player_attribute_value(
                        game.player_build_
                            .attributes,
                        PlayerAttribute::
                            Strength);
                const auto multiplier =
                    player_melee_damage_multiplier(
                        game.progression_
                            .level(),
                        strength);
                auto any_hit = false;
                auto any_killed = false;
                AbilityEventPayload
                    hit_payload {};
                hit_payload.ability_id =
                    resolution.id;
                hit_payload.source_id = 1U;
                hit_payload.direction =
                    game.player_
                        .look_direction();
                for (std::size_t index = 0U;
                     index <
                     runtime.vanguard_targets
                         .target_count;
                     ++index) {
                    const auto id =
                        runtime.vanguard_targets
                            .targets[index]
                            .id;
                    const auto hit =
                        game.creatures_
                            .apply_damage(
                                id,
                                runtime.weapon
                                        .damage *
                                    resolution
                                        .values[0] *
                                    multiplier,
                                CreatureDamageSource::
                                    PlayerAbility,
                                game.player_
                                    .look_direction());
                    if (!hit.hit) {
                        continue;
                    }
                    any_hit = true;
                    any_killed =
                        any_killed ||
                        hit.killed;
                    static_cast<void>(
                        game.creatures_
                            .apply_stagger(
                                id,
                                resolution
                                    .values[1]));
                    game.grant_creature_kill_rewards(
                        hit,
                        "vanguard_strike");
                    auto primary_payload =
                        hit_payload;
                    primary_payload.target_id =
                        hit.id;
                    primary_payload.position =
                        hit.position;
                    primary_payload.primary_value =
                        hit.damage;
                    primary_payload.detail_code =
                        hit.killed ? 1U : 0U;
                    runtime.defer_event(
                        AbilityEventType::Hit,
                        primary_payload);
                }
                if (!any_hit) {
                    return false;
                }
                for (std::size_t index = 0U;
                     index < secondary_count;
                     ++index) {
                    const auto secondary =
                        game.creatures_
                            .apply_damage(
                                secondary_targets[
                                    index],
                                resolution
                                    .values[3] *
                                    multiplier,
                                CreatureDamageSource::
                                    PlayerAbility,
                                game.player_
                                    .look_direction());
                    game.grant_creature_kill_rewards(
                        secondary,
                        "vanguard_wave");
                    if (secondary.hit) {
                        any_killed =
                            any_killed ||
                            secondary.killed;
                        auto secondary_payload =
                            hit_payload;
                        secondary_payload.target_id =
                            secondary.id;
                        secondary_payload.position =
                            secondary.position;
                        secondary_payload.primary_value =
                            secondary.damage;
                        secondary_payload.secondary_value =
                            1.0F;
                        secondary_payload.detail_code =
                            secondary.killed
                                ? 1U
                                : 0U;
                        runtime.defer_event(
                            AbilityEventType::Hit,
                            secondary_payload);
                    }
                }
                game.music_.play_sfx(
                    any_killed
                        ? GameSfxKind::
                              CreatureDeath
                        : GameSfxKind::
                              CreatureHit,
                    0.90F);
                return true;
            }
            case AbilityId::KnightIronGuard: {
                const auto shield_equipped =
                    inventory_slot_has_item(
                        game.inventory_menu_
                            .equipment_slots[
                                equipment_slot_index(
                                    EquipmentSlot::
                                        Shield)]);
                const auto activation =
                    game.player_ability_effects_
                        .activate_iron_guard(
                            resolution,
                            shield_equipped);
                if (!activation.applied) {
                    return false;
                }
                game.sync_selected_hotbar_slot();
                return true;
            }
            case AbilityId::NinjaWindAcceleration:
                game.wind_acceleration_remaining_ =
                    resolution
                        .duration_seconds;
                game.wind_movement_bonus_ =
                    std::max(
                        resolution.values[
                            kWindMovementBonusValueIndex],
                        0.0F);
                game.wind_recovery_bonus_ =
                    std::max(
                        resolution.values[
                            kWindRecoveryBonusValueIndex],
                        0.0F);
                game.wind_blade_available_ =
                    ability_effects_contain(
                        resolution.effects,
                        AbilityEffectFlag::
                            WindBlade);
                game
                    .wind_acceleration_cast_sequence_ =
                    resolution.cast_sequence;
                if (ability_effects_contain(
                        resolution.effects,
                        AbilityEffectFlag::
                            WindMasteryCleanseSlow)) {
                    static_cast<void>(
                        game.player_ability_effects_
                            .clear_slow_effects());
                }
                if (ability_effects_contain(
                        resolution.effects,
                        AbilityEffectFlag::
                            WindMasteryDodge)) {
                    game.wind_dodge_remaining_ =
                        std::max(
                            resolution.values[
                                kWindMasteryDodgeValueIndex],
                            0.0F);
                }
                game.sync_selected_hotbar_slot();
                return true;
            case AbilityId::CommanderFootman: {
                if (runtime.summon_slot >=
                    game.summoned_footmen_
                        .size()) {
                    return false;
                }
                const auto wisdom =
                    player_attribute_value(
                        game.player_build_
                            .attributes,
                        PlayerAttribute::Wisdom);
                SummonedUnitStats
                    resolved_stats {};
                resolved_stats.duration_seconds =
                    resolution.duration_seconds;
                resolved_stats.maximum_health =
                    resolution.values[
                        kFootmanHealthValueIndex];
                resolved_stats.attack_damage =
                    resolution.values[
                        kFootmanDamageValueIndex];
                resolved_stats.attack_interval_seconds =
                    resolution.values[
                        kFootmanAttackIntervalValueIndex];
                resolved_stats.has_light_taunt =
                    ability_effects_contain(
                        resolution.effects,
                        AbilityEffectFlag::
                            FootmanLightTaunt);
                resolved_stats.has_projectile_block =
                    ability_effects_contain(
                        resolution.effects,
                        AbilityEffectFlag::
                            FootmanProjectileBlock);
                if (resolved_stats.has_light_taunt) {
                    resolved_stats
                        .taunt_interval_seconds =
                        resolution.values[
                            kFootmanTauntIntervalValueIndex];
                    resolved_stats.taunt_radius =
                        resolution.values[
                            kFootmanTauntRadiusValueIndex];
                }
                if (resolved_stats
                        .has_projectile_block) {
                    resolved_stats
                        .projectile_block_interval_seconds =
                        resolution.values[
                            kFootmanProjectileBlockIntervalValueIndex];
                }
                resolved_stats
                    .mastery_survival_health =
                    resolution.values[
                        kFootmanMasteryHealthValueIndex];
                resolved_stats
                    .mastery_damage_reduction =
                    resolution.values[
                        kFootmanMasteryReductionValueIndex];
                const auto summon =
                    game.summoned_footmen_[
                            runtime.summon_slot]
                        .summon(
                            SummonedUnitSpawnRequest {
                                .owner_id = 1U,
                                .position =
                                    runtime
                                        .summon_position,
                                .rank =
                                    static_cast<
                                        SummonedUnitRank>(
                                        std::clamp<
                                            std::uint8_t>(
                                            resolution.rank,
                                            1U,
                                            3U)),
                                .mastered =
                                    resolution
                                        .mastery_active,
                                .power_multiplier =
                                    1.0F,
                                .health_power_multiplier =
                                    player_summon_health_multiplier(
                                        game.progression_
                                            .level(),
                                        wisdom),
                                .attack_power_multiplier =
                                    player_summon_damage_multiplier(
                                        game.progression_
                                            .level(),
                                        wisdom),
                                .stats =
                                    resolved_stats,
                                .cast_sequence =
                                    resolution
                                        .cast_sequence,
                            });
                if (!summon.spawned) {
                    return false;
                }
                game
                    .summoned_footman_ship_local_positions_[
                        runtime.summon_slot] =
                    runtime
                        .summon_ship_local_position;
                game.summoned_footman_far_seconds_[
                    runtime.summon_slot] =
                    0.0F;
                game
                    .summoned_footman_cast_sequences_[
                        runtime.summon_slot] =
                    resolution.cast_sequence;
                AbilityEventPayload
                    summon_payload {};
                summon_payload.ability_id =
                    resolution.id;
                summon_payload.source_id = 1U;
                summon_payload.target_id =
                    summon.unit_id;
                summon_payload.position =
                    runtime.summon_position;
                summon_payload.duration_seconds =
                    resolution
                        .duration_seconds;
                runtime.defer_event(
                    AbilityEventType::
                        SummonSpawned,
                    summon_payload);
                return true;
            }
            case AbilityId::BuilderConstructionPlan: {
                if (runtime
                            .authorized_cell_count ==
                        0U) {
                    return false;
                }
                WorldEditTransactionCallbacks
                    callbacks {};
                callbacks.validate_cell =
                    [&](const WorldEditCell& cell) {
                    if (!is_world_y_valid(
                            cell.coordinate.y) ||
                        game.world_.find_chunk(
                            game.world_
                                .world_to_chunk(
                                    cell.coordinate.x,
                                    cell.coordinate.z)) ==
                            nullptr) {
                        return false;
                    }
                    const auto current =
                        game.world_.get_block(
                            cell.coordinate.x,
                            cell.coordinate.y,
                            cell.coordinate.z);
                    return is_block_replaceable(
                               current) &&
                           !is_block_liquid(
                               current) &&
                           !game.world_
                                .has_water(
                                    cell.coordinate.x,
                                    cell.coordinate.y,
                                    cell.coordinate.z);
                };
                callbacks
                    .cell_contains_player_or_creature =
                    [&](const BlockCoord&
                            coordinate) {
                    if (block_overlaps_actor(
                            coordinate,
                            game.player_
                                .position(),
                            0.30F,
                            1.80F)) {
                        return true;
                    }
                    for (const auto& creature :
                         game.creatures_
                             .active_creatures()) {
                        if (block_overlaps_actor(
                                coordinate,
                                creature.position,
                                0.42F,
                                1.55F)) {
                            return true;
                        }
                    }
                    for (const auto& footman :
                         game
                             .summoned_footmen_) {
                        const auto state =
                            footman.state();
                        if (state.active &&
                            block_overlaps_actor(
                                coordinate,
                                state.position,
                                0.40F,
                                1.75F)) {
                            return true;
                        }
                    }
                    return false;
                };
                callbacks.read_current =
                    [&](const BlockCoord&
                            coordinate)
                    -> std::optional<
                        WorldEditCellState> {
                    const auto snapshot =
                        game.world_
                            .capture_cell_snapshot(
                                coordinate.x,
                                coordinate.y,
                                coordinate.z);
                    if (!snapshot.has_value()) {
                        return std::nullopt;
                    }
                    return WorldEditCellState {
                        snapshot->coordinate,
                        snapshot->block,
                        snapshot->water_state,
                        snapshot->player_placed,
                    };
                };
                callbacks.commit_cell =
                    [&](const WorldEditCell& cell) {
                    return game.world_
                        .set_player_block(
                            cell.coordinate.x,
                            cell.coordinate.y,
                            cell.coordinate.z,
                            cell.block_id);
                };
                callbacks.rollback_cell =
                    [&](const WorldEditCellState&
                            cell) {
                    static_cast<void>(
                        game.world_
                            .restore_cell_snapshot({
                                cell.coordinate,
                                cell.block_id,
                                cell.water_state,
                                cell.player_placed,
                            }));
                };
                callbacks.materials_available =
                    [&](BlockId requested,
                        std::uint32_t count) {
                    return is_construction_plan_material(
                               requested) &&
                           inventory_material_count(
                               game.inventory_menu_,
                               game.hotbar_,
                               requested) >=
                               count;
                };
                callbacks.consume_materials =
                    [&](BlockId requested,
                        std::uint32_t count) {
                    return is_construction_plan_material(
                               requested) &&
                           consume_inventory_materials(
                               game.inventory_menu_,
                               game.hotbar_,
                               requested,
                               count);
                };
                callbacks.refund_materials =
                    [&](BlockId,
                        std::uint32_t) {
                    // Je restaure l'instantané complet : aucun échec partiel
                    // ne peut dupliquer ou perdre une pile de l'inventaire.
                    game.hotbar_ =
                        runtime.hotbar_before;
                    game.inventory_menu_ =
                        runtime.inventory_before;
                };
                runtime.transaction =
                    WorldEditTransaction::
                        execute(
                            {
                                runtime
                                    .authorized_cells
                                    .data(),
                                runtime
                                    .authorized_cell_count,
                            },
                            callbacks);
                game.sync_selected_hotbar_slot();
                if (!runtime.transaction
                         .succeeded() ||
                    runtime.transaction
                            .changed_cell_count ==
                        0U) {
                    return false;
                }
                auto newly_marked_count =
                    std::uint32_t {0U};
                for (std::size_t index = 0U;
                     index <
                     runtime
                         .authorized_cell_count;
                     ++index) {
                    newly_marked_count +=
                        runtime
                                .construction_was_player_placed[
                                    index]
                            ? 0U
                            : 1U;
                }
                runtime
                    .construction_xp_cell_count =
                    newly_marked_count;
                for (std::size_t index = 0U;
                     index <
                     runtime
                         .authorized_cell_count;
                     ++index) {
                    const auto& coordinate =
                        runtime.authorized_cells[
                                   index]
                            .coordinate;
                    AbilityEventPayload
                        construct_payload {};
                    construct_payload.ability_id =
                        resolution.id;
                    construct_payload.source_id =
                        1U;
                    construct_payload.position = {
                        static_cast<float>(
                            coordinate.x) +
                            0.5F,
                        static_cast<float>(
                            coordinate.y) +
                            0.5F,
                        static_cast<float>(
                            coordinate.z) +
                            0.5F,
                    };
                    construct_payload.primary_value =
                        static_cast<float>(
                            runtime
                                .authorized_cells[
                                    index]
                                .block_id);
                    runtime.defer_event(
                        AbilityEventType::
                            ConstructPlaced,
                        construct_payload);
                }
                return true;
            }
            default:
                return false;
            }
        } catch (...) {
            game.hotbar_ =
                runtime.hotbar_before;
            game.inventory_menu_ =
                runtime.inventory_before;
            game.sync_selected_hotbar_slot();
            return false;
        }
    };

    const auto result =
        ability_system_.try_cast(
            player_build_,
            request,
            {
                &context,
                validate,
                commit,
            });
    if (!result.succeeded()) {
        auto detail =
            ability_cast_failure_detail(
                result.failure);
        if (context.missing_materials) {
            detail =
                "MATERIAUX INSUFFISANTS";
        } else if (
            context.no_authorized_cells) {
            detail =
                "AUCUNE CELLULE AUTORISEE";
        } else if (
            ability_id ==
                AbilityId::
                    CommanderFootman &&
            context.summon_slot >=
                summoned_footmen_.size()) {
            detail =
                "LIMITE DE 8 INVOCATIONS";
        }
        queue_gameplay_announcement(
            std::string(
                ability_display_title(
                    ability_id)),
            std::string(detail),
            2.1F);
        record_audit_event(
            AuditEventCategory::InputAction,
            "ability_cast_rejected",
            AuditSeverity::Info,
            audit_json_object({
                {
                    "ability",
                    audit_json_number(
                        static_cast<int>(
                            ability_id)),
                },
                {
                    "failure",
                    audit_json_number(
                        static_cast<int>(
                            result.failure)),
                },
            }),
            AuditPriority::Normal);
        return;
    }

    // Je publie les impacts uniquement après CastSucceeded : les observateurs
    // ne peuvent ainsi jamais confondre une validation réussie avec un effet.
    for (std::size_t index = 0U;
         index <
         context.deferred_event_count;
         ++index) {
        const auto& event =
            context.deferred_events[index];
        static_cast<void>(
            ability_system_
                .publish_logical_event(
                    event.type,
                    result.resolution
                        .cast_sequence,
                event.payload));
    }
    if (context.construction_xp_cell_count !=
        0U) {
        award_player_experience(
            ConstructionExperienceEvent {
                context
                    .construction_xp_cell_count,
            },
            context
                .authorized_cells[0U]
                .coordinate,
            "construction_plan");
    }

    const auto leviathan_activation =
        leviathan_knight_synergy_
            .activate(
                player_build_,
                {
                    result.resolution.id,
                    result.resolution
                        .cast_sequence,
                    result.resolution
                        .duration_seconds,
                    true,
                    true,
                });
    if (leviathan_activation.accepted() &&
        leviathan_activation.synergy ==
            LeviathanKnightSynergyKind::
                BulwarkCharge) {
        const auto weapon_state =
            colossal_weapon_.snapshot().state;
        // La, je ne consomme la charge du rempart que si l'arme peut
        // garantir le balayage force sur la prochaine attaque.
        if (weapon_state ==
                ColossalWeaponState::Idle ||
            weapon_state ==
                ColossalWeaponState::Guard) {
            const auto sweep =
                leviathan_knight_synergy_
                    .complete_bulwark_charge(
                        player_build_,
                        {
                            result.resolution
                                .cast_sequence,
                            true,
                        });
            static_cast<void>(
                colossal_weapon_
                    .queue_next_attack_override(
                        sweep,
                        result.resolution
                            .cast_sequence));
        }
    }

    if (ability_effects_contain(
            result.resolution.effects,
            AbilityEffectFlag::
                VanguardBlockSynergy)) {
        seconds_since_successful_shield_block_ =
            -1.0F;
    }
    pending_primary_attack_ = false;
    pending_break_block_ = false;
    player_.cancel_block_breaking();
    mark_session_dirty();
    queue_gameplay_announcement(
        std::string(
            ability_display_title(
                ability_id)),
        ability_id ==
                AbilityId::
                    BuilderConstructionPlan
            ? "CONSTRUCTION VALIDEE"
            : ability_id ==
                      AbilityId::
                          NinjaWindAcceleration
                  ? "LE VENT VOUS PORTE"
                  : "POUVOIR ACTIVE",
        1.8F);
    record_audit_event(
        AuditEventCategory::InputAction,
        "ability_cast_succeeded",
        AuditSeverity::Info,
        audit_json_object({
            {
                "ability",
                audit_json_number(
                    static_cast<int>(
                        ability_id)),
            },
            {
                "rank",
                audit_json_number(
                    result.resolution.rank),
            },
            {
                "energy_cost",
                audit_json_number(
                    result.resolution
                        .energy_cost),
            },
        }),
        AuditPriority::High);
}

void Game::update_summoned_footman(
    float dt,
    bool maritime_session_active) {
    const auto safe_dt =
        std::isfinite(dt)
            ? std::max(dt, 0.0F)
            : 0.0F;
    const auto publish_expiration =
        [this](
            std::size_t index,
            const glm::vec3& position) {
        const auto cast_sequence =
            summoned_footman_cast_sequences_[
                index];
        if (cast_sequence == 0U) {
            return;
        }
        AbilityEventPayload payload {};
        payload.ability_id =
            AbilityId::CommanderFootman;
        payload.source_id = 1U;
        payload.position = position;
        static_cast<void>(
            ability_system_
                .publish_logical_event(
                    AbilityEventType::Expired,
                    cast_sequence,
                    payload));
        summoned_footman_cast_sequences_[
            index] = 0U;
    };
    for (std::size_t index = 0U;
         index < summoned_footmen_.size();
         ++index) {
        auto& footman =
            summoned_footmen_[index];
        if (!footman.active()) {
            publish_expiration(
                index,
                player_.position());
            summoned_footman_ship_local_positions_[
                index]
                .reset();
            summoned_footman_far_seconds_[index] =
                0.0F;
            continue;
        }

        if (summoned_footman_ship_local_positions_[
                index]
                .has_value()) {
            if (!maritime_session_active) {
                publish_expiration(
                    index,
                    footman.state()
                        .position);
                footman.clear();
                summoned_footman_ship_local_positions_[
                    index]
                    .reset();
                continue;
            }
            footman.set_position(
                sea_adventure_
                    .ship_entity()
                    .local_to_world_point(
                        *summoned_footman_ship_local_positions_[
                            index]));
        } else {
            const auto state =
                footman.state();
            const auto distance_squared =
                horizontal_distance_squared(
                    state.position,
                    player_.position());
            if (distance_squared >
                48.0F * 48.0F) {
                summoned_footman_far_seconds_[
                    index] += safe_dt;
                if (summoned_footman_far_seconds_[
                        index] >= 10.0F) {
                    publish_expiration(
                        index,
                        state.position);
                    footman.clear();
                    summoned_footman_far_seconds_[
                        index] = 0.0F;
                    continue;
                }
            } else {
                summoned_footman_far_seconds_[
                    index] = 0.0F;
            }

            if (distance_squared >
                    3.5F * 3.5F &&
                distance_squared <
                    48.0F * 48.0F) {
                auto delta =
                    player_.position() -
                    state.position;
                delta.y = 0.0F;
                const auto length_squared =
                    glm::dot(delta, delta);
                if (length_squared > 1.0e-5F) {
                    const auto candidate =
                        state.position +
                        delta /
                            std::sqrt(
                                length_squared) *
                            std::min(
                                2.8F * safe_dt,
                                std::sqrt(
                                    length_squared));
                    const auto feet =
                        block_coord_from_position(
                            candidate);
                    if (feet.y > 0 &&
                        is_world_y_valid(
                            feet.y + 1) &&
                        world_.find_chunk(
                            world_.world_to_chunk(
                                feet.x,
                                feet.z)) !=
                            nullptr &&
                        is_block_collidable(
                            world_.get_block(
                                feet.x,
                                feet.y - 1,
                                feet.z)) &&
                        is_block_replaceable(
                            world_.get_block(
                                feet.x,
                                feet.y,
                                feet.z)) &&
                        is_block_replaceable(
                            world_.get_block(
                                feet.x,
                                feet.y + 1,
                                feet.z)) &&
                        !world_.has_water(
                            feet.x,
                            feet.y,
                            feet.z)) {
                        footman.set_position(
                            candidate);
                    }
                }
            }
        }

        const auto callbacks =
            SummonedUnitCallbacks {
                .acquire_target =
                    [this](
                        const SummonedUnitAcquireRequest&
                            acquire)
                    -> std::optional<
                        SummonedUnitTarget> {
                    auto best_id =
                        CreatureId {0U};
                    auto best_position =
                        glm::vec3 {0.0F};
                    auto best_distance_squared =
                        3.4F * 3.4F;
                    for (const auto& creature :
                         creatures_
                             .active_creatures()) {
                        if (!is_hostile_creature(
                                creature)) {
                            continue;
                        }
                        const auto distance_squared =
                            horizontal_distance_squared(
                                acquire.origin,
                                creature.position);
                        const auto id =
                            creature_id_from_anchor(
                                creature.anchor);
                        if (distance_squared >
                                best_distance_squared ||
                            (distance_squared ==
                                 best_distance_squared &&
                             best_id != 0U &&
                             id >= best_id)) {
                            continue;
                        }
                        const auto sight_origin =
                            acquire.origin +
                            glm::vec3 {
                                0.0F,
                                0.75F,
                                0.0F,
                            };
                        const auto sight_target =
                            creature.position +
                            glm::vec3 {
                                0.0F,
                                0.65F,
                                0.0F,
                            };
                        auto target_direction =
                            sight_target -
                            sight_origin;
                        const auto target_distance =
                            glm::length(
                                target_direction);
                        if (!std::isfinite(
                                target_distance) ||
                            target_distance <=
                                1.0e-4F) {
                            continue;
                        }
                        target_direction /=
                            target_distance;
                        const auto world_hit =
                            world_
                                .raycast_collidable(
                                    sight_origin,
                                    target_direction,
                                    target_distance);
                        if (world_hit.hit &&
                            world_hit.distance <
                                target_distance -
                                    0.12F) {
                            continue;
                        }
                        best_id = id;
                        best_position =
                            creature.position;
                        best_distance_squared =
                            distance_squared;
                    }
                    if (best_id == 0U) {
                        return std::nullopt;
                    }
                    return SummonedUnitTarget {
                        best_id,
                        best_position,
                    };
                },
                .strike_target =
                    [this, index](
                        const SummonedUnitStrikeRequest&
                            strike) {
                    const auto hit =
                        creatures_.apply_damage(
                            static_cast<
                                CreatureId>(
                                strike.target
                                    .target_id),
                            strike.damage,
                            CreatureDamageSource::
                                PlayerSummon,
                            strike.target.position -
                                strike.origin);
                    grant_creature_kill_rewards(
                        hit,
                        "footman_kill");
                    if (hit.hit) {
                        AbilityEventPayload
                            hit_payload {};
                        hit_payload.ability_id =
                            AbilityId::
                                CommanderFootman;
                        hit_payload.source_id =
                            strike.unit_id;
                        hit_payload.target_id =
                            hit.id;
                        hit_payload.position =
                            hit.position;
                        hit_payload.direction =
                            strike.target.position -
                            strike.origin;
                        hit_payload.primary_value =
                            hit.damage;
                        hit_payload.detail_code =
                            hit.killed ? 1U : 0U;
                        const auto cast_sequence =
                            summoned_footman_cast_sequences_[
                                index];
                        if (cast_sequence != 0U) {
                            static_cast<void>(
                                ability_system_
                                    .publish_logical_event(
                                        AbilityEventType::
                                            Hit,
                                        cast_sequence,
                                        hit_payload));
                        }
                        music_.play_sfx(
                            hit.killed
                                ? GameSfxKind::
                                      CreatureDeath
                                : GameSfxKind::
                                      CreatureHit,
                            0.58F);
                    }
                    return SummonedUnitStrikeResult {
                        hit.hit,
                        hit.killed,
                        hit.damage,
                    };
                },
                .taunt =
                    [this](
                        const SummonedUnitTauntRequest&
                            taunt) {
                    record_audit_event(
                        AuditEventCategory::
                            Creatures,
                        taunt.mastery_triggered
                            ? "footman_mastery_taunt"
                            : "footman_taunt",
                        AuditSeverity::Info,
                        audit_json_object({
                            {
                                "unit_id",
                                audit_json_number(
                                    taunt.unit_id),
                            },
                            {
                                "radius",
                                audit_json_number(
                                    taunt.radius),
                            },
                        }),
                        AuditPriority::Normal);
                },
            };
        const auto update =
            footman.update(
                safe_dt,
                callbacks);
        if (update.expired ||
            !footman.active()) {
            publish_expiration(
                index,
                footman.state()
                    .position);
            summoned_footman_ship_local_positions_[
                index]
                .reset();
            summoned_footman_far_seconds_[index] =
                0.0F;
        }
    }
}

void Game::rebuild_progression_creature_render_instances(
    const EnvironmentState& environment) {
    const auto base =
        creatures_.render_instances();
    progression_creature_render_instances_
        .assign(
            base.begin(),
            base.end());
    progression_creature_render_instances_
        .reserve(
            base.size() +
            summoned_footmen_.size());
    const auto daylight =
        std::clamp(
            finite_or(
                environment.daylight_factor,
                1.0F),
            0.0F,
            1.0F);
    for (const auto& footman :
         summoned_footmen_) {
        for (const auto& snapshot :
             footman.render_snapshots()) {
            progression_creature_render_instances_
                .push_back({
                    CreatureSpecies::Villager,
                    snapshot.position,
                    snapshot.yaw_radians,
                    snapshot.animation_time,
                    0.0F,
                    daylight,
                    snapshot.taunt_amount,
                    static_cast<std::uint32_t>(
                        snapshot.unit_id ^
                        UINT64_C(
                            0xF07A4A11)),
                    snapshot.attack_amount > 0.0F
                        ? CreatureBehaviorState::
                              Strike
                        : CreatureBehaviorState::
                              Idle,
                    CreaturePhase::Day,
                    0.0F,
                    1.0F,
                    snapshot.attack_amount,
                    0.0F,
                    0.0F,
                    {0.0F, 0.0F, 1.0F},
                });
        }
    }
}

void Game::grant_creature_kill_rewards(
    const CreatureDamageResult& result,
    std::string_view source) {
    if (!result.killed ||
        !result.grants_player_rewards) {
        return;
    }

    if (active_game_mode_ ==
            GameMode::SeaAdventure &&
        sea_adventure_.active() &&
        sea_adventure_.record_hunt(
            result.species)) {
        queue_gameplay_announcement(
            "CHASSE",
            "VIANDE RECUPEREE",
            2.4F);
    }

    const auto activity_block =
        block_coord_from_position(
            result.position);
    const auto explicit_ocean_surface =
        active_game_mode_ ==
            GameMode::SeaAdventure &&
        sea_adventure_.active() &&
        std::abs(
            result.position.y -
            static_cast<float>(
                kSeaLevel)) <=
            12.0F &&
        world_.has_water(
            activity_block.x,
            kSeaLevel,
            activity_block.z);
    award_player_experience(
        CombatExperienceEvent {
            static_cast<std::uint64_t>(
                result.experience_reward
                    .experience_points),
            result.disposition ==
                CreatureDisposition::Hostile,
            explicit_ocean_surface,
            environment_
                .current_creature_cycle()
                .phase,
        },
        activity_block,
        source);
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
    const auto resolved_stream_radius =
        options_.performance.adaptive_quality
            ? resolve_adaptive_stream_radius(
                  options_.performance.stream_radius,
                  renderer_.last_frame_stats().resolved_quality)
            : options_.performance.stream_radius;
    const auto active_stream_radius =
        backrooms_active()
            // Je garde l'anneau resident des Backrooms stable : une remontée
            // de qualite ne doit jamais devancer la generation d'une frame.
            ? backrooms_stream_radius(
                  options_.performance.stream_radius)
            : resolved_stream_radius;
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
        clear_colossal_weapon_input();
        musket_fire_held_ = false;
        pending_musket_fire_press_ = false;
        musket_aim_held_ = false;
        pending_musket_reload_ = false;
        pending_ability_slot_.reset();
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
           !inventory_visible_ &&
           !progression_menu_.visible();
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
        reset_musket_interaction();
    }

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
            !progression_menu_.visible() &&
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
        if (const auto usage =
                command_console_usage(
                    parsed.family);
            !usage.empty()) {
            command_console_.set_feedback(
                std::string(usage),
                true);
            return;
        }
        command_console_.set_feedback(
            "UTILISATION INVALIDE",
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

    if (parsed.command ==
        CommandConsoleCommand::GiveMusket) {
        const auto destination =
            inventory_try_grant_loaded_musket(
                inventory_menu_,
                hotbar_);
        if (!destination.has_value()) {
            command_console_.set_feedback(
                "INVENTAIRE PLEIN",
                true);
            return;
        }

        reset_musket_interaction();
        sync_selected_hotbar_slot();
        const auto placed_in_hotbar =
            destination->group ==
            InventorySlotGroup::Hotbar;
        command_console_.set_feedback(
            placed_in_hotbar
                ? "FUSIL AJOUTE ET EQUIPE"
                : "FUSIL AJOUTE AU STOCKAGE",
            false);
        queue_gameplay_announcement(
            "FUSIL A SILEX",
            placed_in_hotbar
                ? "CLIC DROIT VISER - R RECHARGER"
                : "PLACE-LE DANS LA BARRE RAPIDE",
            3.0F);
        mark_session_dirty();
        record_audit_event(
            AuditEventCategory::InputAction,
            "command_console_musket_granted",
            AuditSeverity::Info,
            audit_json_object({
                {
                    "slot_group",
                    audit_json_number(
                        static_cast<int>(
                            destination->group)),
                },
                {
                    "slot_index",
                    audit_json_number(
                        destination->index),
                },
            }),
            AuditPriority::High);
        return;
    }

    if (parsed.family ==
        CommandConsoleFamily::Issou) {
        auto succeeded = false;
        auto feedback =
            std::string {"ACTION /ISSOU REFUSEE"};
        switch (parsed.command) {
        case CommandConsoleCommand::EnterIssou:
            succeeded = issou_scenario_.active()
                ? reset_issou_scenario()
                : enter_issou_scenario();
            feedback = succeeded
                ? "ARENE DU COLOSSE PRETE"
                : "IMPOSSIBLE D'OUVRIR L'ARENE";
            break;
        case CommandConsoleCommand::ResetIssou:
            succeeded = reset_issou_scenario();
            feedback = succeeded
                ? "ARENE REINITIALISEE"
                : "AUCUNE ARENE ACTIVE";
            break;
        case CommandConsoleCommand::ExitIssou:
            succeeded = exit_issou_scenario();
            feedback = succeeded
                ? "PARTIE PRECEDENTE RESTAUREE"
                : "AUCUNE ARENE ACTIVE";
            break;
        case CommandConsoleCommand::SkipIssouCountdown:
            succeeded =
                issou_scenario_.skip_countdown();
            feedback = succeeded
                ? "COMPTE A REBOURS PASSE"
                : "LE COMBAT A DEJA COMMENCE";
            break;
        case CommandConsoleCommand::DisableIssouGore:
            succeeded =
                issou_scenario_.set_gore_mode(
                    IssouGoreMode::Disabled);
            feedback = succeeded
                ? "GORE DESACTIVE"
                : "AUCUNE ARENE ACTIVE";
            break;
        case CommandConsoleCommand::EnableIssouGore:
            succeeded =
                issou_scenario_.set_gore_mode(
                    IssouGoreMode::Full);
            feedback = succeeded
                ? "GORE COMPLET ACTIVE"
                : "AUCUNE ARENE ACTIVE";
            break;
        case CommandConsoleCommand::SetIssouAwakening0:
        case CommandConsoleCommand::SetIssouAwakening1:
        case CommandConsoleCommand::SetIssouAwakening2:
        case CommandConsoleCommand::SetIssouAwakening3: {
            const auto awakening =
                static_cast<std::uint8_t>(
                    static_cast<std::uint8_t>(
                        parsed.command) -
                    static_cast<std::uint8_t>(
                        CommandConsoleCommand::
                            SetIssouAwakening0));
            succeeded =
                issou_scenario_
                    .set_awakening_override(
                        awakening);
            feedback = succeeded
                ? "EVEIL DE DEMONSTRATION : " +
                      std::to_string(awakening)
                : "AUCUNE ARENE ACTIVE";
            break;
        }
        case CommandConsoleCommand::None:
        case CommandConsoleCommand::StartTempest:
        case CommandConsoleCommand::GiveMusket:
        default:
            break;
        }
        command_console_.set_feedback(
            std::move(feedback),
            !succeeded);
        return;
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
        reset_musket_interaction();
    }

    if (death_screen_visible_) {
        if (active_game_mode_ == GameMode::SeaAdventure) {
            sea_adventure_.cancel_fishing();
        }
        if (inventory_visible_) {
            set_inventory_visible(false);
        }
        if (progression_menu_.visible()) {
            set_progression_menu_visible(false);
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
    musket_fire_held_ = false;
    pending_musket_fire_press_ = false;
    musket_aim_held_ = false;
    pending_musket_reload_ = false;
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
        reset_musket_interaction();
    }

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

void Game::set_progression_menu_visible(
    bool visible) {
    if (options_.smoke_test ||
        !has_active_session_ ||
        (visible &&
         (death_screen_visible_ ||
          front_end_visible()))) {
        return;
    }
    if (visible &&
        (paused_ ||
         inventory_visible_ ||
         confirm_dialog_.visible)) {
        return;
    }
    if (progression_menu_.visible() ==
        visible) {
        return;
    }

    if (!visible &&
        construction_plan_editor_
            .active()) {
        static_cast<void>(
            construction_plan_editor_
                .cancel());
    }
    if (visible) {
        static_cast<void>(
            progression_menu_
                .sync_selected_path_from_build(
                    player_build_,
                    progression_.level()));
    }
    progression_menu_.set_visible(
        visible);
    pending_toggle_fly_ = false;
    pending_break_block_ = false;
    pending_primary_attack_ = false;
    pending_place_block_ = false;
    pending_fishing_ = false;
    pending_look_x_ = 0.0F;
    pending_look_y_ = 0.0F;
    pending_ability_slot_.reset();
    player_.cancel_block_breaking();
    reset_musket_interaction(false);
    set_mouse_capture(
        !visible &&
        !paused_ &&
        !inventory_visible_ &&
        !command_console_.visible());
    record_audit_event(
        AuditEventCategory::Ui,
        visible
            ? "progression_opened"
            : "progression_closed",
        AuditSeverity::Info,
        audit_json_object({
            {
                "visible",
                audit_json_bool(visible),
            },
        }),
        AuditPriority::High);
}

void Game::handle_progression_menu_keydown(
    const SDL_KeyboardEvent& event) {
    if (!progression_menu_.visible() ||
        event.repeat != 0) {
        return;
    }

    if (construction_plan_editor_
            .active()) {
        auto editor_result =
            ConstructionPlanEditorResult {};
        auto handled = true;
        switch (event.keysym.sym) {
        case SDLK_ESCAPE:
            editor_result =
                construction_plan_editor_
                    .cancel();
            break;
        case SDLK_1:
        case SDLK_KP_1:
        case SDLK_2:
        case SDLK_KP_2:
        case SDLK_3:
        case SDLK_KP_3: {
            const auto number =
                hotbar_number_from_keycode(
                    event.keysym.sym);
            editor_result =
                construction_plan_editor_
                    .select_plan(
                        static_cast<
                            std::size_t>(
                            number - 1));
            break;
        }
        case SDLK_LEFT:
            editor_result =
                construction_plan_editor_
                    .move_cursor(
                        -1,
                        0,
                        0);
            break;
        case SDLK_RIGHT:
            editor_result =
                construction_plan_editor_
                    .move_cursor(
                        1,
                        0,
                        0);
            break;
        case SDLK_UP:
            editor_result =
                construction_plan_editor_
                    .move_cursor(
                        0,
                        0,
                        -1);
            break;
        case SDLK_DOWN:
            editor_result =
                construction_plan_editor_
                    .move_cursor(
                        0,
                        0,
                        1);
            break;
        case SDLK_PAGEUP:
            editor_result =
                construction_plan_editor_
                    .move_cursor(
                        0,
                        1,
                        0);
            break;
        case SDLK_PAGEDOWN:
            editor_result =
                construction_plan_editor_
                    .move_cursor(
                        0,
                        -1,
                        0);
            break;
        case SDLK_q:
        case SDLK_e: {
            const auto material =
                cycle_construction_plan_material(
                    inventory_menu_,
                    hotbar_,
                    construction_plan_editor_
                        .selected_material(),
                    event.keysym.sym ==
                            SDLK_q
                        ? -1
                        : 1);
            if (!material.has_value()) {
                queue_gameplay_announcement(
                    "PLAN DE CHANTIER",
                    "AUCUN BLOC SOLIDE DANS L'INVENTAIRE",
                    2.6F);
                return;
            }
            editor_result =
                construction_plan_editor_
                    .set_material(
                        *material);
            break;
        }
        case SDLK_m:
            editor_result =
                construction_plan_editor_
                    .toggle_mirrored();
            break;
        case SDLK_SPACE: {
            const auto view =
                construction_plan_editor_
                    .make_view_model();
            editor_result =
                view.can_remove
                    ? construction_plan_editor_
                          .remove_cell()
                    : construction_plan_editor_
                          .place_cell();
            break;
        }
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            editor_result =
                construction_plan_editor_
                    .commit(
                        player_build_);
            break;
        default:
            handled = false;
            break;
        }
        if (!handled) {
            return;
        }
        if (editor_result.build_changed) {
            sync_selected_hotbar_slot();
            mark_session_dirty();
        }
        if (!editor_result.succeeded()) {
            queue_gameplay_announcement(
                "PLAN DE CHANTIER",
                std::string(
                    construction_plan_editor_failure_text(
                        editor_result
                            .failure)),
                2.6F);
        }
        return;
    }

    if (event.keysym.sym == SDLK_c) {
        if (progression_menu_
                .selected_ability() !=
                AbilityId::
                    BuilderConstructionPlan) {
            return;
        }
        if (player_ability_rank(
                player_build_,
                AbilityId::
                    BuilderConstructionPlan) ==
            0U) {
            queue_gameplay_announcement(
                "PLAN DE CHANTIER",
                "APPRENDS D'ABORD LA COMPETENCE",
                2.6F);
            return;
        }
        const auto opened =
            construction_plan_editor_
                .begin_editing(
                    player_build_);
        if (!opened.succeeded()) {
            queue_gameplay_announcement(
                "PLAN DE CHANTIER",
                std::string(
                    construction_plan_editor_failure_text(
                        opened.failure)),
                2.6F);
            return;
        }
        static_cast<void>(
            construction_plan_editor_
                .set_shape(
                    ConstructionPlanShape::
                        Grid));
        const auto selected_material =
            construction_plan_editor_
                .selected_material();
        if (selected_material >
                std::numeric_limits<
                    BlockId>::max() ||
            inventory_material_count(
                inventory_menu_,
                hotbar_,
                static_cast<BlockId>(
                    selected_material)) ==
                0U) {
            const auto material =
                cycle_construction_plan_material(
                    inventory_menu_,
                    hotbar_,
                    std::numeric_limits<
                        std::uint16_t>::max(),
                    1);
            if (!material.has_value()) {
                static_cast<void>(
                    construction_plan_editor_
                        .cancel());
                queue_gameplay_announcement(
                    "PLAN DE CHANTIER",
                    "AUCUN BLOC SOLIDE DANS L'INVENTAIRE",
                    2.6F);
                return;
            }
            static_cast<void>(
                construction_plan_editor_
                    .set_material(
                        *material));
        }
        return;
    }

    auto input =
        std::optional<ProgressionMenuInput> {};
    switch (event.keysym.sym) {
    case SDLK_ESCAPE:
    case SDLK_p:
        set_progression_menu_visible(false);
        return;
    case SDLK_LEFT:
    case SDLK_a:
        input = ProgressionMenuInput::PreviousPath;
        break;
    case SDLK_RIGHT:
    case SDLK_d:
        input = ProgressionMenuInput::NextPath;
        break;
    case SDLK_UP:
    case SDLK_w:
        input = ProgressionMenuInput::PreviousAbility;
        break;
    case SDLK_DOWN:
    case SDLK_s:
        input = ProgressionMenuInput::NextAbility;
        break;
    case SDLK_TAB:
        input =
            (event.keysym.mod &
             KMOD_SHIFT) != 0
                ? ProgressionMenuInput::PreviousSlot
                : ProgressionMenuInput::NextSlot;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        input = ProgressionMenuInput::PurchaseRank;
        break;
    case SDLK_m:
        input = ProgressionMenuInput::PurchaseMastery;
        break;
    case SDLK_SPACE:
    case SDLK_e:
        input = ProgressionMenuInput::EquipOrUnequip;
        break;
    case SDLK_1:
    case SDLK_KP_1:
        input = ProgressionMenuInput::AllocateStrength;
        break;
    case SDLK_2:
    case SDLK_KP_2:
        input = ProgressionMenuInput::AllocateWisdom;
        break;
    case SDLK_3:
    case SDLK_KP_3:
        input = ProgressionMenuInput::AllocateAgility;
        break;
    case SDLK_4:
    case SDLK_KP_4:
        input = ProgressionMenuInput::AllocateRobustness;
        break;
    default:
        return;
    }

    const auto result =
        progression_menu_.handle_input(
            *input,
            player_build_,
            progression_.level());
    if (result.build_changed) {
        sync_selected_hotbar_slot();
        mark_session_dirty();
    }
    if (!result.succeeded()) {
        const auto detail =
            result.failure ==
                    ProgressionMenuFailure::
                        AbilityBuildRejected
                ? progression_ability_failure_text(
                      result.ability_failure)
                : progression_menu_failure_text(
                      result.failure);
        queue_gameplay_announcement(
            "PROGRESSION",
            std::string(detail),
            2.4F);
    }
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
               !progression_menu_.visible() &&
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
        if (issou_scenario_.active()) {
            static_cast<void>(
                reset_issou_scenario());
        } else {
            respawn_player();
        }
        break;
    case DeathScreenAction::Quit:
        if (issou_scenario_.active()) {
            static_cast<void>(
                exit_issou_scenario());
        } else {
            running_ = false;
        }
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
        if (!scenario_session_.saves_allowed() ||
            issou_scenario_
                .saving_suspended()) {
            queue_gameplay_announcement(
                "SAUVEGARDE SUSPENDUE",
                "QUITTEZ L'ARENE POUR RETROUVER VOTRE PARTIE",
                2.6F);
        } else {
            open_save_slot_menu(
                SaveSlotMenuMode::SaveGame,
                SaveSlotMenuParent::
                    PauseMenu);
        }
        break;
    case PauseMenuAction::Load:
        if (issou_scenario_.active() &&
            !exit_issou_scenario()) {
            queue_gameplay_announcement(
                "CHARGEMENT REFUSE",
                "LA SESSION NORMALE N'A PAS PU ETRE RESTAUREE",
                3.0F);
            break;
        }
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
    case MainMenuAction::Backrooms:
        open_save_slot_menu(
            SaveSlotMenuMode::NewGame,
            SaveSlotMenuParent::MainMenu,
            GameMode::Backrooms);
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
    if (!item_stack_can_be_dropped(selected_slot)) {
        // Je protège l'identité de l'arme légendaire avant toute mutation de
        // l'inventaire : le système de drops ne doit jamais avoir à la rendre.
        queue_gameplay_announcement(
            "OBJET LEGENDAIRE",
            "L'ECHINE DU LEVIATHAN NE PEUT PAS ETRE ABANDONNEE",
            2.5F);
        return;
    }
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

    const auto* hovered_slot =
        inventory_slot_ptr(
            inventory_menu_,
            hotbar_,
            *inventory_menu_.hovered_slot);
    if (hovered_slot == nullptr ||
        !item_stack_can_be_dropped(*hovered_slot)) {
        queue_gameplay_announcement(
            "OBJET LEGENDAIRE",
            "L'ECHINE DU LEVIATHAN NE PEUT PAS ETRE ABANDONNEE",
            2.5F);
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
    if (!item_stack_can_be_dropped(
            inventory_menu_.carried_slot)) {
        queue_gameplay_announcement(
            "OBJET LEGENDAIRE",
            "L'ECHINE DU LEVIATHAN NE PEUT PAS ETRE ABANDONNEE",
            2.5F);
        return;
    }
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

void Game::award_player_experience(
    const ExperienceAwardEvent& event,
    const BlockCoord& activity_block,
    std::string_view source) {
    if (!scenario_session_
             .permanent_rewards_allowed() ||
        !issou_scenario_
             .permanent_rewards_allowed()) {
        return;
    }
    const auto award =
        experience_awards_.award(
            event);
    if (!award.awarded() ||
        progression_.is_max_level()) {
        return;
    }

    const auto previous_level = progression_.level();
    const auto result =
        progression_.add_experience(
            award.awarded_experience);
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
            {
                "base_experience",
                audit_json_number(
                    award.base_experience),
            },
            {
                "night_surface_bonus",
                audit_json_bool(
                    award.awarded_experience !=
                    award.base_experience),
            },
            {"awarded_experience", audit_json_number(result.awarded_experience)},
            {
                "units_awarded",
                audit_json_number(
                    award.units_awarded),
            },
            {
                "activity_x",
                audit_json_number(
                    activity_block.x),
            },
            {
                "activity_y",
                audit_json_number(
                    activity_block.y),
            },
            {
                "activity_z",
                audit_json_number(
                    activity_block.z),
            },
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

    const auto delta =
        player_derived_stats_delta(
            previous_level,
            current_level);
    const auto as_percent =
        [](float value) noexcept {
        return static_cast<int>(
            std::lround(
                value * 100.0F));
    };
    std::ostringstream primary_detail {};
    primary_detail
        << "PV +"
        << static_cast<int>(
               std::lround(
                   delta.base_max_health))
        << "  DEG +"
        << as_percent(
               delta
                   .attack_damage_multiplier)
        << "%  DEF +"
        << static_cast<int>(
               std::lround(
                   delta
                       .damage_reduction_percent))
        << "%";
    queue_gameplay_announcement(
        std::string("NIVEAU ") + std::to_string(current_level),
        primary_detail.str(),
        3.35F);
    std::ostringstream secondary_detail {};
    secondary_detail
        << "VIT +"
        << as_percent(
               delta
                   .movement_speed_multiplier)
        << "%  MINAGE +"
        << as_percent(
               delta
                   .mining_speed_multiplier)
        << "%  APNEE +"
        << static_cast<int>(
               std::lround(
                   delta
                       .apnea_resistance_percent))
        << "%  CHUTE +"
        << as_percent(
               delta
                   .safe_fall_multiplier)
        << "%";
    queue_gameplay_announcement(
        "STATISTIQUES",
        secondary_detail.str(),
        3.35F);

    if (!player_has_super_vision_power(previous_level) && player_has_super_vision_power(current_level)) {
        queue_gameplay_announcement("SUPER VISION DEBLOQUEE", "TOUCHE V POUR VOIR DANS LE NOIR", 4.2F);
    }
    if (!player_has_flight_power(previous_level) && player_has_flight_power(current_level)) {
        queue_gameplay_announcement(
            "VOL DEBLOQUE",
            active_game_mode_ ==
                    GameMode::SeaAdventure
                ? "INDISPONIBLE EN MER - F RESTE LA PECHE"
                : "TOUCHE F POUR VOLER",
            4.2F);
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

void Game::prepare_legendary_presentation(
    float animation_time_seconds) {
    if (!has_active_session_ ||
        front_end_visible()) {
        pending_leviathan_visual_events_.clear();
        renderer_
            .clear_legendary_presentation();
        return;
    }

    std::vector<LeviathanWeaponPartInstance>
        weapon_parts {};
    std::vector<ChainedColossusPartInstance>
        colossus_parts {};
    std::vector<IssouCrowdInstance>
        crowd {};
    std::vector<IssouArenaDecorInstance>
        arena_decor {};
    std::vector<IssouHudElement>
        issou_hud {};
    auto issou_results =
        std::optional<
            IssouResultsPresentation> {};
    auto visual_events =
        std::exchange(
            pending_leviathan_visual_events_,
            {});
    std::array<
        LegendaryEnemyRenderSnapshot,
        kMaximumLegendaryEnemies>
        enemy_snapshots {};
    const auto enemy_count =
        legendary_enemies_
            .render_snapshots(
                enemy_snapshots);
    auto sea_snapshot =
        std::optional<
            SeaLeviathanRenderSnapshot> {};

    const auto weapon =
        colossal_weapon_.snapshot();
    if (!player_.is_dead() &&
        (selected_colossal_weapon_active() ||
         weapon.state !=
             ColossalWeaponState::Holstered)) {
        const auto awakening_level =
            issou_scenario_.active()
                ? issou_scenario_.state()
                      .awakening_override
                : static_cast<std::uint8_t>(
                      legendary_weapon_progression_
                          .state()
                          .awakening);
        auto up = glm::vec3 {0.0F, 1.0F, 0.0F};
        if (sea_adventure_.active()) {
            up =
                sea_adventure_.ship_entity()
                    .local_to_world_direction(
                        {0.0F, 1.0F, 0.0F});
            const auto up_length = glm::length(up);
            up = std::isfinite(up_length) &&
                         up_length > 1.0e-5F
                     ? up / up_length
                     : glm::vec3 {0.0F, 1.0F, 0.0F};
        }
        auto forward = player_.look_direction();
        const auto forward_length = glm::length(forward);
        forward =
            std::isfinite(forward_length) &&
                    forward_length > 1.0e-5F
                ? forward / forward_length
                : glm::vec3 {0.0F, 0.0F, -1.0F};
        auto right = glm::cross(forward, up);
        const auto right_length = glm::length(right);
        if (!std::isfinite(right_length) ||
            right_length <= 1.0e-5F) {
            right = {1.0F, 0.0F, 0.0F};
        } else {
            right /= right_length;
        }
        forward = glm::normalize(
            glm::cross(up, right));
        auto actor_transform = glm::mat4 {1.0F};
        actor_transform[0] =
            glm::vec4 {right, 0.0F};
        actor_transform[1] =
            glm::vec4 {up, 0.0F};
        actor_transform[2] =
            glm::vec4 {-forward, 0.0F};
        actor_transform[3] =
            glm::vec4 {
                player_.eye_position(),
                1.0F,
            };
        const auto pose =
            solve_leviathan_weapon_pose({
                actor_transform,
                LeviathanViewMode::FirstPerson,
                weapon.state,
                weapon.attack,
                static_cast<
                    LegendaryWeaponAwakening>(
                    std::min<std::uint8_t>(
                        awakening_level,
                        static_cast<std::uint8_t>(
                            LegendaryWeaponAwakening::
                                Awakened))),
                weapon.state_progress,
                weapon.charge_progress,
                animation_time_seconds,
                weapon.contextual_vertical,
            });
        if (pose.visible) {
            weapon_parts = pose.parts;
            auto trail =
                build_leviathan_visual_events({
                    player_.eye_position() +
                        forward * 1.4F,
                    forward,
                    weapon.attack,
                    LeviathanImpactSurface::Flesh,
                    LeviathanImpactWeight::Light,
                    static_cast<
                        LegendaryWeaponAwakening>(
                        std::min<std::uint8_t>(
                            awakening_level,
                            static_cast<std::uint8_t>(
                                LegendaryWeaponAwakening::
                                    Awakened))),
                    {},
                    weapon.state_progress,
                    false,
                    false,
                });
            visual_events.insert(
                visual_events.end(),
                trail.begin(),
                trail.end());
        }
    }

    if (issou_scenario_.active()) {
        const auto& arena =
            issou_scenario_.state();
        const auto& boss =
            chained_colossus_.state();
        const auto stagger =
            chained_colossus_
                .stagger_state();
        auto hidden_parts_mask =
            std::uint32_t {0U};
        auto wounded_zones_mask =
            boss.health <
                    kChainedColossusMaximumHealth
                ? (std::uint32_t {1U} <<
                   kColossusTorsoZone)
                : 0U;
        const auto hidden_part_for_zone =
            [](DamageZoneId zone_id) noexcept {
                switch (zone_id) {
                case kColossusHeadZone:
                    return ColossusHiddenPart::Head;
                case kColossusLeftArmZone:
                    return ColossusHiddenPart::LeftArm;
                case kColossusRightArmZone:
                    return ColossusHiddenPart::RightArm;
                case kColossusLeftLegZone:
                    return ColossusHiddenPart::LeftLeg;
                case kColossusRightLegZone:
                    return ColossusHiddenPart::RightLeg;
                case kColossusHornZone:
                    return ColossusHiddenPart::Horn;
                default:
                    return ColossusHiddenPart::None;
                }
            };
        for (const auto& limb :
             chained_colossus_
                 .limb_views()) {
            if (limb.zone_id < 32U &&
                (limb.condition !=
                     DamageZoneCondition::Intact ||
                 limb.armor ==
                     ColossusArmorState::Broken)) {
                wounded_zones_mask |=
                    std::uint32_t {1U} <<
                    limb.zone_id;
            }
            if (limb.part_state ==
                DismembermentPartState::Severed) {
                hidden_parts_mask |=
                    static_cast<std::uint32_t>(
                        hidden_part_for_zone(
                            limb.zone_id));
            }
        }
        const auto boss_x =
            static_cast<int>(
                std::floor(boss.position.x));
        const auto boss_y =
            std::clamp(
                static_cast<int>(
                    std::floor(
                        boss.position.y +
                        2.0F)),
                kWorldMinY,
                kWorldMaxY);
        const auto boss_z =
            static_cast<int>(
                std::floor(boss.position.z));
        const auto gore =
            arena.gore_mode ==
                    IssouGoreMode::Disabled
                ? GorePresentationMode::Disabled
                : arena.gore_mode ==
                          IssouGoreMode::Reduced
                      ? GorePresentationMode::Reduced
                      : GorePresentationMode::Full;
        colossus_parts =
            build_chained_colossus_parts({
                boss.position,
                boss.yaw_radians,
                boss.animation_seconds,
                std::clamp(
                    boss.health /
                        kChainedColossusMaximumHealth,
                    0.0F,
                    1.0F),
                stagger.maximum > 0.0F
                    ? std::clamp(
                          stagger.current /
                              stagger.maximum,
                          0.0F,
                          1.0F)
                    : 0.0F,
                boss.movement_amount,
                boss.phase,
                boss.attack,
                boss.attack_stage,
                boss.armor_states,
                hidden_parts_mask,
                wounded_zones_mask,
                gore,
                static_cast<float>(
                    world_.get_sky_light(
                        boss_x,
                        boss_y,
                        boss_z)) /
                    15.0F,
                static_cast<float>(
                    world_.get_block_light(
                        boss_x,
                        boss_y,
                        boss_z)) /
                    15.0F,
            });
        crowd =
            build_issou_crowd(
                arena.layout,
                {
                    render_player()
                        .eye_position(),
                    140U,
                    12U,
                    static_cast<std::uint32_t>(
                        arena.layout.seed) ^
                        arena.run_sequence,
                    animation_time_seconds,
                    arena.crowd_excitement,
                    latest_issou_event_,
                    false,
                });
        arena_decor =
            build_issou_arena_decor(
                arena);
        const auto weapon_stability_ratio =
            weapon.maximum_stability > 0.0F
                ? weapon.stability /
                      weapon.maximum_stability
                : 0.0F;
        issou_hud =
            build_issou_arena_hud({
                arena.phase,
                static_cast<float>(
                    window_width_),
                static_cast<float>(
                    window_height_),
                std::clamp(
                    boss.health /
                        kChainedColossusMaximumHealth,
                    0.0F,
                    1.0F),
                stagger.maximum > 0.0F
                    ? stagger.current /
                          stagger.maximum
                    : 0.0F,
                std::clamp(
                    weapon_stability_ratio,
                    0.0F,
                    1.0F),
                weapon.charge_progress,
                arena.countdown_seconds,
                weapon.momentum,
                {},
            });
        if (arena.phase ==
                IssouArenaPhase::Victory ||
            arena.phase ==
                IssouArenaPhase::Defeat) {
            issou_results =
                build_issou_results(
                    arena.statistics,
                    arena.phase ==
                        IssouArenaPhase::Victory);
        }
    }

    if (sea_adventure_.active() &&
        sea_leviathan_.active()) {
        const auto& ship =
            sea_adventure_
                .ship_entity();
        sea_snapshot =
            sea_leviathan_
                .render_snapshot({
                    ship.world_origin(),
                    ship.local_to_world_direction(
                        {1.0F, 0.0F, 0.0F}),
                    ship.local_to_world_direction(
                        {0.0F, 1.0F, 0.0F}),
                    ship.local_to_world_direction(
                        {0.0F, 0.0F, 1.0F}),
                });
    }

    renderer_.set_legendary_presentation({
        std::span<const LeviathanWeaponPartInstance> {
            weapon_parts},
        std::span<const ChainedColossusPartInstance> {
            colossus_parts},
        issou_scenario_.active()
            ? colossus_blood_traces_
                  .traces()
            : std::span<
                  const ColossusBloodTrace> {},
        std::span<const IssouCrowdInstance> {
            crowd},
        std::span<const IssouArenaDecorInstance> {
            arena_decor},
        std::span<const LegendaryEnemyRenderSnapshot> {
            enemy_snapshots.data(),
            enemy_count,
        },
        sea_snapshot,
        std::span<const IssouHudElement> {
            issou_hud},
        issou_results,
        std::span<const LeviathanVisualEvent> {
            visual_events},
    });
}

auto Game::selected_musket_active() const noexcept -> bool {
    const auto& selected =
        hotbar_.selected_slot();
    return inventory_slot_has_item(selected) &&
           block_item_id(selected.block_id) ==
               to_block_id(BlockType::Musket);
}

auto Game::selected_colossal_weapon_active() const noexcept
    -> bool {
    const auto& selected =
        hotbar_.selected_slot();
    if (is_legendary_weapon_item(selected)) {
        return true;
    }
    // Je ne vole pas la sélection d'un outil, d'une arme ou d'un bloc :
    // l'arme équipée prend la main uniquement sur un emplacement réellement vide.
    return !inventory_slot_has_item(selected) &&
           inventory_has_equipped_legendary_weapon(
               inventory_menu_);
}

auto Game::colossal_weapon_drawn() const noexcept -> bool {
    return colossal_weapon_.snapshot().state !=
           ColossalWeaponState::Holstered;
}

auto Game::intercept_colossal_guard(
    const ColossalGuardRequest& request,
    std::uint8_t allies_behind) noexcept
    -> ColossalGuardResult {
    const auto preview =
        resolve_colossal_guard(request);
    auto adjusted = request;
    const auto synergy =
        leviathan_knight_synergy_
            .modify_guard(
                player_build_,
                {
                    preview.stability_lost,
                    allies_behind,
                    request.guard_active,
                    preview.blocked,
                    request.attack_kind ==
                        ColossalIncomingAttackKind::
                            Projectile,
                });
    if (preview.stability_lost >
            1.0e-6F &&
        synergy.stability_loss >= 0.0F &&
        std::isfinite(
            synergy.stability_loss)) {
        adjusted.attack_coefficient *=
            std::clamp(
                synergy.stability_loss /
                    preview.stability_lost,
                0.0F,
                1.0F);
    }
    const auto result =
        colossal_weapon_
            .intercept_incoming_attack(
                adjusted);
    if (result.perfect) {
        const auto view =
            leviathan_knight_synergy_
                .view();
        const auto index =
            static_cast<std::size_t>(
                LeviathanKnightSynergyKind::
                    PerfectRiposte);
        if (view.active[index]) {
            const auto riposte =
                leviathan_knight_synergy_
                    .consume_perfect_riposte(
                        player_build_,
                        {
                            view.activation_sequences[
                                index],
                            true,
                            true,
                        });
            static_cast<void>(
                colossal_weapon_
                    .queue_next_attack_override(
                        riposte,
                        view.activation_sequences[
                            index]));
        }
    }
    return result;
}

void Game::clear_colossal_weapon_input() noexcept {
    pending_colossal_primary_release_ =
        pending_colossal_primary_release_ ||
        colossal_primary_held_;
    pending_colossal_guard_release_ =
        pending_colossal_guard_release_ ||
        colossal_guard_held_;
    colossal_primary_held_ = false;
    colossal_guard_held_ = false;
    pending_colossal_primary_press_ = false;
    pending_colossal_guard_press_ = false;
}

void Game::update_colossal_weapon(
    float dt,
    const PlayerInput& movement_input,
    bool gameplay_input_enabled,
    bool maritime_session_active) {
    const auto selected =
        selected_colossal_weapon_active();
    const auto before =
        colossal_weapon_.snapshot();
    if (!selected &&
        before.state ==
            ColossalWeaponState::Holstered) {
        colossal_weapon_was_selected_ = false;
        colossal_blade_pose_valid_ = false;
        clear_colossal_weapon_input();
        return;
    }

    const auto look =
        safe_horizontal_direction(
            player_.look_direction());
    auto right =
        glm::cross(
            look,
            glm::vec3 {0.0F, 1.0F, 0.0F});
    const auto right_length_squared =
        glm::dot(right, right);
    right =
        right_length_squared > 1.0e-6F
            ? right /
                  std::sqrt(
                      right_length_squared)
            : glm::vec3 {1.0F, 0.0F, 0.0F};
    constexpr auto kTunnelHalfProbe = 1.45F;
    const auto left_hit =
        world_.raycast_collidable(
            player_.eye_position(),
            -right,
            kTunnelHalfProbe);
    const auto right_hit =
        world_.raycast_collidable(
            player_.eye_position(),
            right,
            kTunnelHalfProbe);
    const auto horizontal_clearance =
        (left_hit.hit
             ? left_hit.distance
             : kTunnelHalfProbe) +
        (right_hit.hit
             ? right_hit.distance
             : kTunnelHalfProbe);

    const auto strength =
        player_attribute_value(
            player_build_.attributes,
            PlayerAttribute::Strength);
    const auto demonstration_strength =
        issou_scenario_.active()
            ? kLeviathanSpineDefinition
                  .demonstration_strength
            : strength;
    const auto level =
        static_cast<std::uint16_t>(
            std::min<std::uint32_t>(
                progression_.level(),
                (std::numeric_limits<
                    std::uint16_t>::max)()));
    const auto fully_immersed =
        player_.state().head_underwater &&
        player_.state().swimming;

    ColossalWeaponInput input {};
    input.toggle_draw_pressed =
        (selected &&
         (!colossal_weapon_was_selected_ ||
          (before.state ==
               ColossalWeaponState::Holstered &&
           before.last_rejection !=
               ColossalWeaponRejection::
                   RequirementsNotMet &&
           !fully_immersed))) ||
        (!selected &&
         before.state !=
             ColossalWeaponState::Holstered &&
         before.can_change_equipment);
    input.primary_pressed =
        gameplay_input_enabled &&
        selected &&
        std::exchange(
            pending_colossal_primary_press_,
            false);
    input.primary_held =
        gameplay_input_enabled &&
        selected &&
        colossal_primary_held_;
    input.primary_released =
        std::exchange(
            pending_colossal_primary_release_,
            false);
    input.guard_pressed =
        gameplay_input_enabled &&
        selected &&
        std::exchange(
            pending_colossal_guard_press_,
            false);
    input.guard_held =
        gameplay_input_enabled &&
        selected &&
        colossal_guard_held_;
    input.guard_released =
        std::exchange(
            pending_colossal_guard_release_,
            false);
    input.cancel_pressed =
        player_.is_dead() ||
        !gameplay_input_enabled;

    const ColossalWeaponUpdateContext context {
        level,
        demonstration_strength,
        progression_.attack_damage_multiplier(),
        player_build_.val_energy,
        issou_scenario_.active(),
        fully_immersed,
        movement_input.sprint,
        horizontal_clearance < 2.75F,
    };
    const auto events =
        colossal_weapon_.update(
            input,
            context,
            dt);
    for (const auto& event : events) {
        switch (event.type) {
        case ColossalWeaponEventType::
            ChargeResourceCommit: {
            const auto cost =
                std::max(
                    event.primary_value,
                    0.0F);
            if (std::isfinite(cost) &&
                player_build_.val_energy >= cost) {
                player_build_.val_energy -= cost;
                if (player_build_.revision !=
                    (std::numeric_limits<
                        std::uint64_t>::max)()) {
                    ++player_build_.revision;
                }
                mark_session_dirty();
            }
            break;
        }
        case ColossalWeaponEventType::AttackStarted:
            player_.trigger_primary_action();
            player_.cancel_block_breaking();
            colossal_hit_ledger_.begin_attack(
                event.attack_sequence);
            colossal_wall_impact_sequence_ = 0U;
            colossal_shockwave_sequence_ = 0U;
            colossal_blade_pose_valid_ = false;
            music_.play_sfx(
                GameSfxKind::HeavySwing,
                event.attack ==
                        ColossalAttackKind::
                            ChargedExecution
                    ? 1.0F
                    : 0.84F);
            break;
        case ColossalWeaponEventType::
            RunningAdvanceRequested: {
            auto velocity =
                player_.state().velocity;
            const auto duration =
                std::max(
                    event.secondary_value,
                    0.10F);
            const auto advance_speed =
                std::clamp(
                    event.primary_value /
                        duration,
                    0.0F,
                    7.0F);
            velocity.x +=
                look.x * advance_speed;
            velocity.z +=
                look.z * advance_speed;
            player_.set_velocity(
                velocity);
            break;
        }
        case ColossalWeaponEventType::DrawCompleted:
            issou_scenario_.notify_combat_event(
                IssouArenaCombatEvent::
                    WeaponDrawn);
            break;
        case ColossalWeaponEventType::AttackMissed:
            issou_scenario_.notify_combat_event(
                IssouArenaCombatEvent::
                    AttackMissed);
            break;
        case ColossalWeaponEventType::AutoSheathed:
            queue_gameplay_announcement(
                "ARME RANGEE",
                "L'ECHINE EST TROP LOURDE SOUS L'EAU",
                2.0F);
            break;
        case ColossalWeaponEventType::ActionRejected:
        case ColossalWeaponEventType::ChargeRejected:
            if (event.rejection ==
                ColossalWeaponRejection::
                    RequirementsNotMet) {
                queue_gameplay_announcement(
                    "ARME TROP LOURDE",
                    "NIVEAU 35 ET FORCE 4 REQUIS",
                    2.4F);
            } else if (
                event.rejection ==
                ColossalWeaponRejection::
                    InsufficientEnergy) {
                queue_gameplay_announcement(
                    "ENERGIE INSUFFISANTE",
                    "35 POINTS DE VAL REQUIS",
                    2.0F);
            }
            break;
        case ColossalWeaponEventType::StateChanged:
        case ColossalWeaponEventType::SheathCompleted:
        case ColossalWeaponEventType::AttackBecameActive:
        case ColossalWeaponEventType::AttackFinished:
        case ColossalWeaponEventType::AttackImpact:
        case ColossalWeaponEventType::MomentumChanged:
        case ColossalWeaponEventType::StabilityChanged:
        case ColossalWeaponEventType::GuardStarted:
        case ColossalWeaponEventType::GuardEnded:
        case ColossalWeaponEventType::PerfectGuard:
        case ColossalWeaponEventType::GuardBroken:
            break;
        }
    }

    const auto after =
        colossal_weapon_.snapshot();
    if (after.state ==
        ColossalWeaponState::Active) {
        resolve_colossal_weapon_sweep(
            maritime_session_active);
    } else if (
        after.attack_sequence !=
            before.attack_sequence) {
        colossal_blade_pose_valid_ = false;
    }

    if (selected ||
        after.state !=
            ColossalWeaponState::Holstered) {
        pending_break_block_ = false;
        pending_primary_attack_ = false;
        pending_place_block_ = false;
        player_.cancel_block_breaking();
    }
    colossal_weapon_was_selected_ =
        selected;
    sync_selected_hotbar_slot();
}

void Game::resolve_colossal_weapon_sweep(
    bool maritime_session_active) {
    const auto weapon =
        colossal_weapon_.snapshot();
    const auto* attack =
        colossal_weapon_
            .current_attack_definition();
    if (attack == nullptr ||
        weapon.state !=
            ColossalWeaponState::Active ||
        weapon.attack_sequence == 0U) {
        return;
    }
    const auto attack_synergy =
        leviathan_knight_synergy_
            .prepare_attack(
                player_build_,
                {
                    weapon.attack_sequence,
                    weapon.attack,
                    attack->maximum_targets,
                });
    const auto effective_range =
        attack->range_blocks;
    const auto effective_shockwave_radius =
        std::max(
            0.0F,
            attack->shockwave_radius_blocks +
                attack_synergy
                    .additional_shockwave_radius_blocks);
    const auto effective_arc =
        attack->arc_degrees;
    const auto effective_maximum_targets =
        std::max(
            attack_synergy.maximum_targets ==
                    0U
                ? attack->maximum_targets
                : attack_synergy
                      .maximum_targets,
            attack->maximum_targets);
    const auto* base_attack =
        colossal_attack_definition(
            weapon.attack);
    const auto attack_override_stagger_multiplier =
        base_attack != nullptr &&
                base_attack->stagger_power >
                    1.0e-6F
            ? attack->stagger_power /
                  base_attack->stagger_power
            : 1.0F;

    const auto awakening_level =
        issou_scenario_.active()
            ? issou_scenario_.state()
                  .awakening_override
            : static_cast<std::uint8_t>(
                  legendary_weapon_progression_
                      .state()
                      .awakening);
    const auto awakening =
        static_cast<LegendaryWeaponAwakening>(
            std::min<std::uint8_t>(
                awakening_level,
                static_cast<std::uint8_t>(
                    LegendaryWeaponAwakening::
                        Awakened)));
    const auto current_pose =
        colossal_blade_pose(
            player_,
            weapon,
            awakening);
    if (!colossal_blade_pose_valid_ ||
        colossal_hit_ledger_
                .attack_sequence() !=
            weapon.attack_sequence) {
        previous_colossal_blade_pose_ =
            current_pose;
        colossal_blade_pose_valid_ = true;
    }

    enum class TargetRouteKind : std::uint8_t {
        Creature = 0,
        LegendaryEnemy,
        Colossus,
        SeaLeviathan,
    };
    struct TargetRoute {
        ColossalCombatTargetId combat_id = 0U;
        TargetRouteKind kind =
            TargetRouteKind::Creature;
        std::uint64_t runtime_id = 0U;
        ColossalTargetWeight weight =
            ColossalTargetWeight::Light;
        bool corrupted = false;
    };

    std::array<
        ColossalSweepCandidate,
        kMaximumColossalSweepCandidates>
        candidates {};
    std::array<
        TargetRoute,
        kMaximumColossalSweepCandidates>
        routes {};
    auto candidate_count = std::size_t {0U};
    auto route_count = std::size_t {0U};
    const auto add_route =
        [&](TargetRouteKind kind,
            std::uint64_t runtime_id,
            ColossalTargetWeight weight,
            bool corrupted)
            -> ColossalCombatTargetId {
        if (route_count >= routes.size()) {
            return 0U;
        }
        constexpr auto kPayloadMask =
            (std::uint64_t {1U} << 60U) - 1U;
        const auto route_namespace =
            static_cast<std::uint64_t>(kind) + 1U;
        const auto payload =
            (kind == TargetRouteKind::Colossus ||
             kind == TargetRouteKind::SeaLeviathan)
                ? 1U
                : runtime_id & kPayloadMask;
        const auto id =
            (route_namespace << 60U) |
            std::max<std::uint64_t>(payload, 1U);
        routes[route_count++] = {
            id,
            kind,
            runtime_id,
            weight,
            corrupted,
        };
        return id;
    };
    const auto append_candidate =
        [&](ColossalSweepCandidate candidate) {
        if (candidate_count >=
                candidates.size() ||
            candidate.target_id == 0U) {
            return;
        }
        candidates[candidate_count++] =
            candidate;
    };

    if (issou_scenario_.state().phase ==
        IssouArenaPhase::Combat) {
        const auto colossus_id =
            add_route(
                TargetRouteKind::Colossus,
                0U,
                ColossalTargetWeight::Boss,
                false);
        const auto& state =
            chained_colossus_.state();
        const auto limbs =
            chained_colossus_.limb_views();
        const auto zone_is_severed =
            [&](DamageZoneId zone_id) {
            const auto found =
                std::find_if(
                    limbs.begin(),
                    limbs.end(),
                    [zone_id](
                        const ChainedColossusLimbView&
                            limb) {
                        return limb.zone_id ==
                               zone_id;
                    });
            return found != limbs.end() &&
                   found->part_state ==
                       DismembermentPartState::
                           Severed;
        };
        constexpr std::array<
            DamageZoneId,
            7U>
            zones {{
                kColossusTorsoZone,
                kColossusHeadZone,
                kColossusLeftArmZone,
                kColossusRightArmZone,
                kColossusLeftLegZone,
                kColossusRightLegZone,
                kColossusHornZone,
            }};
        for (const auto zone_id : zones) {
            const auto torso =
                zone_id ==
                kColossusTorsoZone;
            const auto head =
                zone_id ==
                kColossusHeadZone;
            const auto horn =
                zone_id ==
                kColossusHornZone;
            append_candidate({
                colossus_id,
                zone_id,
                colossus_zone_center(
                    state,
                    zone_id),
                torso
                    ? 0.95F
                    : (head
                           ? 0.52F
                           : (horn
                                  ? 0.30F
                                  : 0.58F)),
                static_cast<std::uint8_t>(
                    horn
                        ? 6U
                        : (head
                               ? 5U
                               : (torso
                                      ? 1U
                                      : 4U))),
                !zone_is_severed(zone_id),
                false,
            });
        }
    }

    std::optional<SeaLeviathanCombatSnapshot>
        sea_combat {};
    if (maritime_session_active &&
        sea_leviathan_.active()) {
        const auto& ship =
            sea_adventure_.ship_entity();
        const ShipLocalFrame frame {
            ship.world_origin(),
            ship.local_to_world_direction(
                {1.0F, 0.0F, 0.0F}),
            ship.local_to_world_direction(
                {0.0F, 1.0F, 0.0F}),
            ship.local_to_world_direction(
                {0.0F, 0.0F, 1.0F}),
        };
        sea_combat =
            sea_leviathan_.combat_snapshot(
                frame);
        if (sea_combat.has_value()) {
            const auto sea_leviathan_id =
                add_route(
                    TargetRouteKind::SeaLeviathan,
                    0U,
                    ColossalTargetWeight::Boss,
                    false);
            for (std::size_t index = 0U;
                 index <
                 sea_combat->hit_volume_count;
                 ++index) {
                const auto& volume =
                    sea_combat
                        ->hit_volumes[index];
                if (!volume.enabled) {
                    continue;
                }
                append_candidate({
                    sea_leviathan_id,
                    static_cast<
                        ColossalCombatZoneId>(
                        volume.part),
                    volume.center_world,
                    volume.radius,
                    static_cast<std::uint8_t>(
                        volume.sectionable
                            ? 5U
                            : 3U),
                    true,
                    false,
                });
            }
        }
    }

    std::array<
        LegendaryEnemyCombatSnapshot,
        kMaximumLegendaryEnemies>
        legendary_snapshots {};
    const auto legendary_count =
        legendary_enemies_.combat_snapshots(
            legendary_snapshots);
    for (std::size_t index = 0U;
         index < legendary_count;
         ++index) {
        const auto& enemy =
            legendary_snapshots[index];
        if (!enemy.damageable) {
            continue;
        }
        const auto id =
            add_route(
                TargetRouteKind::
                    LegendaryEnemy,
                enemy.id,
                colossal_target_weight(
                    enemy.weight),
                enemy.corrupted);
        append_candidate({
            id,
            0U,
            enemy.hit_center,
            enemy.hit_radius,
            static_cast<std::uint8_t>(
                enemy.weight ==
                        EntityWeight::Heavy
                    ? 3U
                    : 2U),
            true,
            enemy.faction ==
                Faction::Ally,
        });
    }

    for (const auto& creature :
         creatures_.active_creatures()) {
        if (candidate_count >=
            candidates.size()) {
            break;
        }
        const auto profile =
            creature_combat_profile(
                creature);
        const auto id =
            add_route(
                TargetRouteKind::Creature,
                creature_id_from_anchor(
                    creature.anchor),
                colossal_target_weight(
                    profile.weight),
                profile.faction ==
                    Faction::Hostile);
        append_candidate({
            id,
            0U,
            creature.position +
                glm::vec3 {
                    0.0F,
                    0.75F,
                    0.0F,
                },
            profile.weight ==
                    EntityWeight::Heavy
                ? 0.72F
                : 0.58F,
            static_cast<std::uint8_t>(
                profile.weight ==
                        EntityWeight::Heavy
                    ? 2U
                    : 1U),
            true,
            creature.anchor.species ==
                CreatureSpecies::Villager,
        });
    }

    const auto direction =
        safe_horizontal_direction(
            player_.look_direction());
    const auto sweep_previous_pose =
        previous_colossal_blade_pose_;
    const ColossalSweepQuery query {
        weapon.attack_sequence,
        sweep_previous_pose,
        current_pose,
        player_.position() +
            glm::vec3 {
                0.0F,
                0.90F,
                0.0F,
            },
        direction,
        0.18F,
        effective_range,
        effective_arc,
        effective_maximum_targets,
        weapon.attack_shape ==
                ColossalAttackShape::
                    HorizontalArc ||
            weapon.attack_shape ==
                ColossalAttackShape::
                    ReverseHorizontalArc ||
            weapon.attack_shape ==
                ColossalAttackShape::
                    DiagonalArc,
        false,
    };
    const ColossalSweepCallbacks callbacks {
        this,
        +[](void* user_data,
            const ColossalSweepOcclusionRequest&
                request) noexcept {
            const auto& game =
                *static_cast<Game*>(
                    user_data);
            const auto delta =
                request.target_center -
                request.origin;
            const auto distance =
                glm::length(delta);
            if (!std::isfinite(distance) ||
                distance <= 0.08F) {
                return false;
            }
            const auto ray = delta / distance;
            const auto maximum =
                std::max(
                    distance - 0.08F,
                    0.0F);
            if (game.world_
                    .raycast_collidable(
                        request.origin,
                        ray,
                        maximum)
                    .hit) {
                return true;
            }
            return game.sea_adventure_.active() &&
                   game.sea_adventure_
                       .ship_entity()
                       .raycast_collidable_distance(
                           request.origin,
                           ray,
                           maximum)
                       .has_value();
        },
    };
    const auto result =
        resolve_colossal_sweep(
            query,
            std::span<
                const ColossalSweepCandidate> {
                candidates.data(),
                candidate_count,
            },
            colossal_hit_ledger_,
            callbacks);
    struct ColossalResolvedContact {
        ColossalSweepHit hit {};
        bool shockwave_only = false;
    };
    std::array<
        ColossalResolvedContact,
        kMaximumColossalSweepHits * 2U>
        resolved_contacts {};
    auto resolved_contact_count = std::size_t {0U};
    for (const auto& hit : result.accepted_hits()) {
        resolved_contacts[resolved_contact_count++] = {
            hit,
            false,
        };
    }

    auto shockwave_triggered = false;
    if (effective_shockwave_radius > 0.0F &&
        colossal_shockwave_sequence_ !=
            weapon.attack_sequence &&
        weapon.state_progress >= 0.45F) {
        auto shockwave_origin =
            player_.position() +
            direction *
                std::min(effective_range * 0.78F, 2.75F);
        shockwave_origin.y += 0.15F;
        if (!result.accepted_hits().empty()) {
            shockwave_origin =
                result.accepted_hits().front().contact_point;
        }
        colossal_shockwave_sequence_ =
            weapon.attack_sequence;
        shockwave_triggered = true;
        const auto remaining_targets =
            effective_maximum_targets >
                    resolved_contact_count
                ? static_cast<std::uint8_t>(
                      effective_maximum_targets -
                      resolved_contact_count)
                : std::uint8_t {0U};
        if (remaining_targets > 0U) {
            const auto shockwave =
                resolve_colossal_shockwave(
                    {
                        weapon.attack_sequence,
                        shockwave_origin,
                        effective_shockwave_radius,
                        remaining_targets,
                        false,
                    },
                    std::span<
                        const ColossalSweepCandidate> {
                        candidates.data(),
                        candidate_count,
                    },
                    colossal_hit_ledger_,
                    callbacks);
            for (const auto& hit :
                 shockwave.accepted_hits()) {
                if (resolved_contact_count >=
                    resolved_contacts.size()) {
                    break;
                }
                resolved_contacts[
                    resolved_contact_count++] = {
                    hit,
                    true,
                };
            }
        }
    }
    auto titan_synergy =
        LeviathanTitanImpactResult {};
    if (resolved_contact_count > 0U &&
        (weapon.attack ==
             ColossalAttackKind::
                 ChargedExecution ||
         weapon.attack ==
             ColossalAttackKind::
                 Earthbreaker)) {
        const auto synergy_view =
            leviathan_knight_synergy_
                .view();
        const auto index =
            static_cast<std::size_t>(
                LeviathanKnightSynergyKind::
                    TitanJudgment);
        if (synergy_view.active[index]) {
            titan_synergy =
                leviathan_knight_synergy_
                    .prepare_titan_impact(
                        player_build_,
                        {
                            synergy_view
                                .activation_sequences[
                                    index],
                            true,
                        });
        }
    }
    previous_colossal_blade_pose_ =
        current_pose;

    const auto effective_strength =
        issou_scenario_.active()
            ? kLeviathanSpineDefinition
                  .demonstration_strength
            : player_attribute_value(
                  player_build_.attributes,
                  PlayerAttribute::Strength);
    const auto first_awakening =
        awakening_level >=
        static_cast<std::uint8_t>(
            LegendaryWeaponAwakening::
                Corrupted);
    const auto final_awakening =
        awakening_level >=
        static_cast<std::uint8_t>(
            LegendaryWeaponAwakening::
                Awakened);
    auto heaviest =
        ColossalTargetWeight::Light;
    auto accepted_hits =
        std::uint8_t {0U};
    auto severed_limb = false;
    auto total_damage = 0.0F;
    const auto collision_safe_knockback_distance =
        [&](const glm::vec3& origin,
            const glm::vec3& raw_direction,
            float requested_distance) {
        auto horizontal =
            glm::vec3 {
                raw_direction.x,
                0.0F,
                raw_direction.z,
            };
        const auto length_squared =
            glm::dot(horizontal, horizontal);
        if (!std::isfinite(length_squared) ||
            length_squared <= 1.0e-6F ||
            !std::isfinite(requested_distance) ||
            requested_distance <= 0.0F) {
            return 0.0F;
        }
        horizontal /=
            std::sqrt(length_squared);
        auto allowed =
            std::clamp(
                requested_distance,
                0.0F,
                4.0F);
        const auto world_hit =
            world_.raycast_collidable(
                origin,
                horizontal,
                allowed);
        if (world_hit.hit) {
            allowed =
                std::min(
                    allowed,
                    std::max(
                        world_hit.distance -
                            0.20F,
                        0.0F));
        }
        if (maritime_session_active) {
            const auto ship_hit =
                sea_adventure_.ship_entity()
                    .raycast_collidable_distance(
                        origin,
                        horizontal,
                        allowed);
            if (ship_hit.has_value()) {
                allowed =
                    std::min(
                        allowed,
                        std::max(
                            *ship_hit - 0.20F,
                            0.0F));
            }
        }
        return allowed;
    };

    for (std::size_t contact_index = 0U;
         contact_index < resolved_contact_count;
         ++contact_index) {
        const auto& contact =
            resolved_contacts[contact_index];
        const auto& hit = contact.hit;
        const auto route =
            std::find_if(
                routes.begin(),
                routes.begin() +
                    static_cast<
                        std::ptrdiff_t>(
                        route_count),
                [&](const TargetRoute& candidate) {
                    return candidate.combat_id ==
                           hit.target_id;
                });
        if (route ==
            routes.begin() +
                static_cast<std::ptrdiff_t>(
                    route_count)) {
            continue;
        }

        auto damage =
            resolve_colossal_damage({
                weapon.attack,
                route->weight,
                progression_
                    .attack_damage_multiplier(),
                effective_strength,
                1.0F,
                1.0F,
                1.0F,
                weapon.momentum,
                route->corrupted,
                first_awakening,
            });
        if (final_awakening &&
            route->weight ==
                ColossalTargetWeight::Boss) {
            damage.stagger_power *= 1.10F;
        }
        damage.stagger_power *=
            attack_override_stagger_multiplier *
            titan_synergy
                .stagger_multiplier;
        if (contact.shockwave_only) {
            damage.stagger_power *= 0.65F;
            damage.sever_power = 0.0F;
        }
        const auto health_damage =
            (contact.shockwave_only
                 ? damage.shockwave_damage
                 : damage.direct_damage +
                       damage.shockwave_damage) *
                titan_synergy
                    .damage_multiplier +
            attack_synergy
                .additional_shockwave_damage;
        auto hit_accepted = false;
        auto hit_severed = false;
        auto applied_health_damage = 0.0F;
        switch (route->kind) {
        case TargetRouteKind::Creature: {
            const auto creature_hit =
                creatures_.apply_damage(
                    route->runtime_id,
                    health_damage,
                    CreatureDamageSource::Player,
                    hit.target_center -
                        player_.position());
            hit_accepted = creature_hit.hit;
            applied_health_damage =
                creature_hit.damage;
            if (creature_hit.hit) {
                const auto stagger_seconds =
                    std::clamp(
                        damage.stagger_power /
                            100.0F,
                        0.15F,
                        1.20F);
                static_cast<void>(
                    creatures_.apply_stagger(
                        route->runtime_id,
                        stagger_seconds));
                if (!creature_hit.killed) {
                    static_cast<void>(
                        creatures_.apply_knockback(
                            route->runtime_id,
                            hit.target_center -
                                player_.position(),
                            collision_safe_knockback_distance(
                                hit.target_center,
                                hit.target_center -
                                    player_.position(),
                                3.25F *
                                    damage
                                        .knockback_multiplier *
                                    (contact.shockwave_only
                                         ? 0.75F
                                         : 1.0F))));
                }
            }
            if (creature_hit.killed) {
                grant_creature_kill_rewards(
                    creature_hit,
                    "leviathan_spine");
                if (route->corrupted &&
                    !issou_scenario_.active()) {
                    static_cast<void>(
                        legendary_weapon_progression_
                            .record_corrupted_kills());
                }
            }
            break;
        }
        case TargetRouteKind::LegendaryEnemy: {
            const auto enemy_hit =
                legendary_enemies_.apply_hit(
                    route->runtime_id,
                    {
                        health_damage,
                        damage.stagger_power,
                        true,
                        awakening_level,
                    });
            hit_accepted =
                enemy_hit.accepted;
            applied_health_damage =
                enemy_hit.applied_health_damage;
            if (enemy_hit.accepted &&
                !enemy_hit.killed_now) {
                static_cast<void>(
                    legendary_enemies_
                        .apply_knockback(
                            route->runtime_id,
                            hit.target_center -
                                player_.position(),
                            collision_safe_knockback_distance(
                                hit.target_center,
                                hit.target_center -
                                    player_.position(),
                                3.25F *
                                    damage
                                        .knockback_multiplier *
                                    (contact.shockwave_only
                                         ? 0.75F
                                         : 1.0F))));
            }
            if (enemy_hit.killed_now &&
                route->corrupted &&
                !issou_scenario_.active()) {
                static_cast<void>(
                    legendary_weapon_progression_
                        .record_corrupted_kills());
            }
            break;
        }
        case TargetRouteKind::Colossus: {
            const auto gore =
                issou_scenario_.state()
                            .gore_mode ==
                        IssouGoreMode::Disabled
                    ? GorePresentationMode::
                          Disabled
                    : (issou_scenario_.state()
                                   .gore_mode ==
                               IssouGoreMode::Reduced
                           ? GorePresentationMode::
                                 Reduced
                           : GorePresentationMode::
                                 Full);
            const auto execution =
                !contact.shockwave_only &&
                hit.zone_id ==
                    kColossusHeadZone &&
                weapon.attack ==
                    ColossalAttackKind::
                        ChargedExecution &&
                chained_colossus_
                    .can_execute();
            const auto boss_hit =
                chained_colossus_.apply_hit({
                    hit.zone_id,
                    health_damage,
                    damage.stagger_power,
                    damage.sever_power,
                    gore,
                    true,
                    execution,
                });
            hit_accepted = boss_hit.accepted;
            applied_health_damage =
                boss_hit.health_damage;
            hit_severed =
                boss_hit.limb_severed;
            if (boss_hit.accepted) {
                colossus_blood_traces_
                    .add_impact(
                        hit.contact_point,
                        -direction,
                        hit_severed
                            ? 1.0F
                            : 0.55F,
                        static_cast<
                            std::uint32_t>(
                            weapon.attack_sequence ^
                            hit.zone_id),
                        gore);
                if (boss_hit.armor_broken_now) {
                    issou_scenario_
                        .notify_combat_event(
                            IssouArenaCombatEvent::
                                ArmorBroken);
                }
                if (boss_hit.limb_severed) {
                    issou_scenario_
                        .notify_combat_event(
                            IssouArenaCombatEvent::
                                LimbSevered);
                }
                if (boss_hit.killed) {
                    issou_scenario_
                        .notify_combat_event(
                            boss_hit
                                    .execution_completed
                                ? IssouArenaCombatEvent::
                                      BossExecuted
                                : IssouArenaCombatEvent::
                                      BossKilled);
                }
            }
            break;
        }
        case TargetRouteKind::SeaLeviathan: {
            const auto sea_hit =
                sea_leviathan_.apply_hit({
                    static_cast<SeaLeviathanPart>(
                        hit.zone_id),
                    health_damage,
                    damage.stagger_power,
                    !contact.shockwave_only &&
                        weapon.attack ==
                        ColossalAttackKind::
                            ChargedExecution,
                    damage.sever_power > 0.0F,
                    awakening_level,
                });
            hit_accepted = sea_hit.accepted;
            applied_health_damage =
                sea_hit.applied_health_damage;
            hit_severed =
                sea_hit
                    .tentacle_severed_now;
            if (sea_hit.defeated_now &&
                !issou_scenario_.active()) {
                static_cast<void>(
                    legendary_weapon_progression_
                        .record_major_boss_defeat());
            }
            break;
        }
        }

        if (!hit_accepted) {
            continue;
        }
        ++accepted_hits;
        total_damage +=
            std::max(applied_health_damage, 0.0F);
        heaviest =
            static_cast<std::uint8_t>(
                route->weight) >
                    static_cast<std::uint8_t>(
                        heaviest)
                ? route->weight
                : heaviest;
        severed_limb =
            severed_limb ||
            hit_severed;
    }

    auto wall_hit = false;
    auto protected_surface = false;
    auto impact_material =
        ColossalImpactMaterial::Organic;
    auto block_hit = RaycastHit {};
    const auto probe_blade_segment =
        [&](const glm::vec3& from,
            const glm::vec3& to) {
        const auto delta = to - from;
        const auto length = glm::length(delta);
        if (!std::isfinite(length) ||
            length <= 0.02F) {
            return;
        }
        const auto candidate =
            world_.raycast_collidable(
                from,
                delta / length,
                length);
        if (candidate.hit &&
            (!block_hit.hit ||
             candidate.distance <
                 block_hit.distance)) {
            block_hit = candidate;
        }
    };
    // Je sonde le volume réellement balayé par la lame entre les deux poses,
    // pas seulement le rayon central du viseur.
    probe_blade_segment(
        sweep_previous_pose.hilt,
        sweep_previous_pose.tip);
    probe_blade_segment(
        current_pose.hilt,
        current_pose.tip);
    probe_blade_segment(
        sweep_previous_pose.tip,
        current_pose.tip);
    probe_blade_segment(
        sweep_previous_pose.hilt,
        current_pose.hilt);
    probe_blade_segment(
        (sweep_previous_pose.hilt +
         sweep_previous_pose.tip) *
            0.5F,
        (current_pose.hilt + current_pose.tip) *
            0.5F);
    if (block_hit.hit &&
        colossal_wall_impact_sequence_ !=
            weapon.attack_sequence) {
        wall_hit = true;
        const auto material =
            colossal_cell_material(
                world_.get_block(
                    block_hit.block.x,
                    block_hit.block.y,
                    block_hit.block.z));
        impact_material =
            colossal_cell_impact_material(
                material);
        const ColossalWorldCell cell {
            block_hit.block.x,
            block_hit.block.y,
            block_hit.block.z,
        };
        protected_surface =
            world_.was_player_placed(
                cell.x,
                cell.y,
                cell.z) ||
            colossal_world_protections_
                    .protection_at(cell) !=
                WorldProtectionFlag::None;
        colossal_wall_impact_sequence_ =
            weapon.attack_sequence;
    }
    if (maritime_session_active) {
        auto ship_hit = std::optional<float> {};
        const auto probe_ship_segment =
            [&](const glm::vec3& from,
                const glm::vec3& to) {
            const auto delta = to - from;
            const auto length = glm::length(delta);
            if (!std::isfinite(length) ||
                length <= 0.02F) {
                return;
            }
            const auto candidate =
                sea_adventure_.ship_entity()
                    .raycast_collidable_distance(
                        from,
                        delta / length,
                        length);
            if (candidate.has_value() &&
                (!ship_hit.has_value() ||
                 *candidate < *ship_hit)) {
                ship_hit = candidate;
            }
        };
        probe_ship_segment(
            sweep_previous_pose.hilt,
            sweep_previous_pose.tip);
        probe_ship_segment(
            current_pose.hilt,
            current_pose.tip);
        probe_ship_segment(
            sweep_previous_pose.tip,
            current_pose.tip);
        if (ship_hit.has_value()) {
            wall_hit =
                wall_hit ||
                colossal_wall_impact_sequence_ !=
                    weapon.attack_sequence;
            protected_surface = true;
            impact_material =
                ColossalImpactMaterial::Ship;
            colossal_wall_impact_sequence_ =
                weapon.attack_sequence;
        }
    }

    if (wall_hit &&
        weapon.attack ==
            ColossalAttackKind::
                ChargedExecution &&
        block_hit.hit) {
        std::array<
            ColossalFragileCellCandidate,
            kMaximumColossalCellCandidates>
            fragile_candidates {};
        auto fragile_count =
            std::size_t {0U};
        for (auto y = -2;
             y <= 2 &&
             fragile_count <
                 fragile_candidates.size();
             ++y) {
            for (auto x = -4;
                 x <= 4 &&
                 fragile_count <
                     fragile_candidates.size();
                 ++x) {
                for (auto z = -4;
                     z <= 4 &&
                     fragile_count <
                         fragile_candidates.size();
                     ++z) {
                    const ColossalWorldCell cell {
                        block_hit.block.x + x,
                        block_hit.block.y + y,
                        block_hit.block.z + z,
                    };
                    const auto block =
                        world_.get_block(
                            cell.x,
                            cell.y,
                            cell.z);
                    const auto material =
                        colossal_cell_material(
                            block);
                    if (!colossal_cell_is_fragile(
                            material)) {
                        continue;
                    }
                    const auto delta =
                        glm::vec3 {
                            static_cast<float>(x),
                            static_cast<float>(y),
                            static_cast<float>(z),
                        };
                    const auto protections =
                        colossal_world_protections_
                            .protection_at(cell);
                    fragile_candidates[
                        fragile_count++] = {
                        cell,
                        material,
                        glm::dot(delta, delta),
                        block,
                        true,
                        world_.was_player_placed(
                            cell.x,
                            cell.y,
                            cell.z),
                        false,
                        world_protection_contains(
                            protections,
                            WorldProtectionFlag::
                                ImportantStructure),
                        world_protection_contains(
                            protections,
                            WorldProtectionFlag::
                                QuestStructure),
                    };
                }
            }
        }
        const auto plan =
            build_colossal_fragile_impact_plan(
                {
                    weapon.attack_sequence,
                    kLeviathanSpineDefinition
                        .maximum_fragile_cells,
                    true,
                },
                std::span<
                    const ColossalFragileCellCandidate> {
                    fragile_candidates.data(),
                    fragile_count,
                },
                colossal_world_protections_);
        for (const auto& edit :
             plan.accepted_edits()) {
            const auto current =
                world_.get_block(
                    edit.cell.x,
                    edit.cell.y,
                    edit.cell.z);
            if (current !=
                static_cast<BlockId>(
                    edit.expected_block_token)) {
                continue;
            }
            world_.set_block(
                edit.cell.x,
                edit.cell.y,
                edit.cell.z,
                to_block_id(
                    BlockType::Air));
        }
    }

    if (accepted_hits > 0U ||
        wall_hit ||
        shockwave_triggered) {
        auto impact_origin =
            player_.position() +
            direction *
                std::min(effective_range * 0.78F, 2.75F);
        impact_origin.y += 0.15F;
        if (resolved_contact_count > 0U) {
            impact_origin =
                resolved_contacts[0U]
                    .hit.contact_point;
        } else if (block_hit.hit) {
            impact_origin = {
                static_cast<float>(block_hit.block.x) +
                    0.5F,
                static_cast<float>(block_hit.block.y) +
                    0.5F,
                static_cast<float>(block_hit.block.z) +
                    0.5F,
            };
        }
        auto surface =
            LeviathanImpactSurface::Flesh;
        switch (impact_material) {
        case ColossalImpactMaterial::Wood:
            surface = LeviathanImpactSurface::Wood;
            break;
        case ColossalImpactMaterial::Stone:
        case ColossalImpactMaterial::Earth:
        case ColossalImpactMaterial::ProtectedStructure:
            surface = LeviathanImpactSurface::Stone;
            break;
        case ColossalImpactMaterial::Metal:
        case ColossalImpactMaterial::Ship:
            surface = LeviathanImpactSurface::Metal;
            break;
        default:
            break;
        }
        const auto weight =
            severed_limb ||
                    heaviest ==
                        ColossalTargetWeight::Boss
                ? LeviathanImpactWeight::
                      BossOrSection
                : heaviest ==
                          ColossalTargetWeight::Heavy
                      ? LeviathanImpactWeight::Heavy
                      : LeviathanImpactWeight::Light;
        auto events =
            build_leviathan_visual_events({
                impact_origin,
                direction,
                weapon.attack,
                surface,
                weight,
                awakening,
                {},
                weapon.state_progress,
                true,
                severed_limb,
            });
        constexpr auto kMaximumQueuedVisualEvents =
            std::size_t {64U};
        for (const auto& event : events) {
            if (pending_leviathan_visual_events_
                    .size() >=
                kMaximumQueuedVisualEvents) {
                break;
            }
            pending_leviathan_visual_events_
                .push_back(event);
        }
    }

    if (wall_hit) {
        music_.play_sfx(
            impact_material ==
                        ColossalImpactMaterial::
                            Metal ||
                    impact_material ==
                        ColossalImpactMaterial::
                            Ship
                ? GameSfxKind::MetalImpact
                : GameSfxKind::BoneImpact,
            protected_surface
                ? 0.72F
                : 0.90F,
            0.0F,
            1.0F,
            static_cast<std::uint32_t>(
                weapon.attack_sequence));
    }

    if (accepted_hits > 0U) {
        record_legendary_quest_tutorial(
            weapon.attack ==
                    ColossalAttackKind::
                        ChargedExecution
                ? LegendaryQuestAction::
                      TutorialChargedHit
                : LegendaryQuestAction::
                      TutorialSweepHit,
            weapon.attack_sequence,
            total_damage);
        issou_scenario_
            .acknowledge_first_successful_action();
        issou_scenario_.notify_combat_event(
            weapon.attack ==
                    ColossalAttackKind::
                        ChargedExecution
                ? IssouArenaCombatEvent::
                      ChargedAttackHit
                : (weapon.attack ==
                           ColossalAttackKind::
                               Earthbreaker
                       ? IssouArenaCombatEvent::
                             ComboFinisherHit
                       : IssouArenaCombatEvent::
                             AttackHit),
            total_damage,
            accepted_hits);
        music_.play_sfx(
            heaviest ==
                    ColossalTargetWeight::Boss
                ? GameSfxKind::BoneImpact
                : GameSfxKind::CreatureHit,
            heaviest ==
                    ColossalTargetWeight::Boss
                ? 1.0F
                : 0.82F);
    }

    static_cast<void>(
        colossal_weapon_
            .notify_attack_resolution({
                accepted_hits,
                heaviest,
                impact_material,
                wall_hit,
                protected_surface,
                severed_limb,
            }));
}

void Game::configure_legendary_weapon_quest() {
    const auto configured =
        legendary_weapon_quest_.configure(
            static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(
                    world_.seed())),
            active_game_mode_);
    legendary_weapon_quest_
        .reset_transient_progress();
    legendary_quest_world_content_.reset();
    legendary_quest_world_scenes_applied_
        .fill(false);
    if (configured) {
        legendary_quest_world_content_ =
            generate_legendary_quest_world_content(
                legendary_weapon_quest_
                    .layout());
        static_cast<void>(
            apply_legendary_quest_world_content());
    }
    legendary_quest_guardian_id_ = 0U;
    legendary_quest_tutorial_spawned_ =
        false;
}

auto Game::apply_legendary_quest_world_content()
    -> bool {
    if (!legendary_quest_world_content_
             .has_value() ||
        !is_valid_legendary_quest_world_content(
            *legendary_quest_world_content_)) {
        legendary_quest_world_scenes_applied_
            .fill(false);
        return false;
    }

    const auto& plan =
        *legendary_quest_world_content_;
    for (const auto& cell : plan.edits()) {
        world_.ensure_chunk_loaded(
            world_.world_to_chunk(
                cell.coordinate.x,
                cell.coordinate.z));
    }

    WorldEditTransactionCallbacks callbacks {};
    callbacks.validate_cell =
        [this](const WorldEditCell& cell) {
            if (!is_world_y_valid(
                    cell.coordinate.y)) {
                return false;
            }
            const auto current =
                world_.get_block(
                    cell.coordinate.x,
                    cell.coordinate.y,
                    cell.coordinate.z);
            // La, je preserve une construction du joueur tout en acceptant
            // sans ambiguite un bloc identique deja place par une ancienne
            // version de la scene.
            return current ==
                       cell.block_id ||
                   !world_.was_player_placed(
                       cell.coordinate.x,
                       cell.coordinate.y,
                       cell.coordinate.z);
        };
    callbacks.cell_contains_player_or_creature =
        [](const BlockCoord&) {
            // Les scenes sont installees pendant l'ecran de chargement, avant
            // que les acteurs du nouveau monde deviennent actifs.
            return false;
        };
    callbacks.read_current =
        [this](const BlockCoord& coordinate)
        -> std::optional<WorldEditCellState> {
        const auto snapshot =
            world_.capture_cell_snapshot(
                coordinate.x,
                coordinate.y,
                coordinate.z);
        if (!snapshot.has_value()) {
            return std::nullopt;
        }
        return WorldEditCellState {
            snapshot->coordinate,
            snapshot->block,
            snapshot->water_state,
            snapshot->player_placed,
        };
    };
    callbacks.commit_cell =
        [this](const WorldEditCell& cell) {
        world_.set_block(
            cell.coordinate.x,
            cell.coordinate.y,
            cell.coordinate.z,
            cell.block_id);
        return world_.get_block(
                   cell.coordinate.x,
                   cell.coordinate.y,
                   cell.coordinate.z) ==
               cell.block_id;
    };
    callbacks.rollback_cell =
        [this](const WorldEditCellState& cell) {
        static_cast<void>(
            world_.restore_cell_snapshot({
                cell.coordinate,
                cell.block_id,
                cell.water_state,
                cell.player_placed,
            }));
    };
    callbacks.materials_available =
        [](BlockId, std::uint32_t) {
            return true;
        };
    callbacks.consume_materials =
        [](BlockId, std::uint32_t) {
            return true;
        };
    callbacks.refund_materials =
        [](BlockId, std::uint32_t) {};

    auto all_scenes_applied = true;
    for (std::size_t scene_index = 0U;
         scene_index < plan.scenes.size();
         ++scene_index) {
        const auto result =
            execute_legendary_quest_world_scene(
                plan,
                scene_index,
                callbacks);
        legendary_quest_world_scenes_applied_[
            scene_index] =
            result.succeeded();
        all_scenes_applied =
            all_scenes_applied &&
            result.succeeded();
    }
    return all_scenes_applied;
}

auto Game::process_legendary_quest_request(
    const LegendaryQuestRequest& request)
    -> LegendaryQuestProcessResult {
    const auto before =
        legendary_weapon_progression_
            .state();
    const auto inventory_count_before =
        inventory_legendary_weapon_count(
            inventory_menu_,
            hotbar_);
    LegendaryQuestCallbacks callbacks {};
    callbacks.commit_hear_rumor =
        [this] {
            return legendary_weapon_progression_
                .hear_rumor();
        };
    callbacks.commit_map_fragment =
        [this] {
            return legendary_weapon_progression_
                .collect_map_fragment();
        };
    callbacks.commit_forge_discovery =
        [this] {
            return legendary_weapon_progression_
                .discover_forge();
        };
    callbacks.commit_guardian_defeat =
        [this] {
            return legendary_weapon_progression_
                .defeat_guardian();
        };
    callbacks.try_commit_weapon_to_inventory =
        [this](std::uint64_t unique_weapon_id) {
            if (unique_weapon_id == 0U) {
                return false;
            }
            return inventory_try_grant_legendary_weapon(
                       inventory_menu_,
                       hotbar_)
                .has_value();
        };
    callbacks.commit_weapon_claim =
        [this](
            std::uint64_t unique_weapon_id,
            std::uint32_t player_level,
            std::uint8_t strength) {
            return legendary_weapon_progression_
                .claim_weapon(
                    unique_weapon_id,
                    player_level,
                    strength);
        };
    callbacks.rollback_weapon_from_inventory =
        [this](std::uint64_t unique_weapon_id) {
            return unique_weapon_id != 0U &&
                   inventory_remove_all_legendary_weapons(
                       inventory_menu_,
                       hotbar_) > 0U;
        };
    callbacks.commit_first_combat =
        [this] {
            return legendary_weapon_progression_
                .complete_first_combat();
        };

    const auto level =
        progression_.level();
    const auto strength =
        player_attribute_value(
            player_build_.attributes,
            PlayerAttribute::Strength);
    const auto result =
        legendary_weapon_quest_.process(
            request,
            {
                before,
                level,
                strength,
                scenario_session_.active() ||
                    issou_scenario_.active(),
            },
            callbacks);
    const auto after =
        legendary_weapon_progression_
            .state();
    const auto inventory_count_after =
        inventory_legendary_weapon_count(
            inventory_menu_,
            hotbar_);
    if (after != before ||
        inventory_count_after !=
            inventory_count_before) {
        normalize_inventory_state(
            inventory_menu_,
            hotbar_);
        sync_selected_hotbar_slot();
        mark_session_dirty();
    }
    consume_legendary_quest_events();
    return result;
}

auto Game::try_interact_legendary_weapon_quest()
    -> bool {
    if (!has_active_session_ ||
        scenario_session_.active() ||
        issou_scenario_.active() ||
        !legendary_weapon_quest_
             .configured()) {
        return false;
    }

    const auto progression =
        legendary_weapon_progression_
            .state();
    if (progression.weapon_owned &&
        progression.corrupted_kills >=
            kLegendaryWeaponFinalAwakeningKills &&
        progression.astral_boss_defeated &&
        progression.major_boss_defeated &&
        !progression.forge_ritual_complete) {
        const auto& forge_anchor =
            legendary_weapon_quest_
                .layout()
                .forge;
        auto forge_point =
            forge_anchor.position;
        auto forge_radius =
            std::max(
                forge_anchor.discovery_radius,
                4.0F);
        auto forge_vertical_tolerance = 24.0F;
        if (legendary_quest_world_content_
                .has_value()) {
            if (const auto placement =
                    legendary_quest_world_content_
                        ->anchor(forge_anchor.id);
                placement.has_value()) {
                forge_point =
                    placement->interaction_position;
                forge_radius =
                    std::max(
                        placement
                            ->horizontal_interaction_radius,
                        2.0F);
                forge_vertical_tolerance =
                    std::max(
                        placement
                            ->vertical_tolerance,
                        2.0F);
            }
        }
        if (is_legendary_quest_near_horizontal(
                {
                    player_.position().x,
                    player_.position().y,
                    player_.position().z,
                },
                legendary_quest_spatial_point(
                    forge_point),
                forge_radius,
                forge_vertical_tolerance) &&
            legendary_weapon_progression_
                .complete_forge_ritual()) {
            static_cast<void>(
                legendary_weapon_progression_
                    .unlock_upgrades(
                        kLegendaryWeaponKnownUpgradeMask));
            static_cast<void>(
                legendary_weapon_progression_
                    .set_cosmetic(
                        LegendaryWeaponCosmetic::
                            Sovereign));
            mark_session_dirty();
            queue_gameplay_announcement(
                "ECHINE EVEILLEE",
                "LA FORGE A REUNI LA CHAIR, L'ASTRAL ET LE TITAN",
                4.5F);
            music_.play_sfx(
                GameSfxKind::BoneImpact,
                1.0F);
            return true;
        }
    }
    const auto presentation =
        legendary_weapon_quest_
            .presentation_state(
                progression,
                progression_.level(),
                player_attribute_value(
                    player_build_
                        .attributes,
                    PlayerAttribute::
                        Strength));
    if (!presentation.valid ||
        presentation.completed) {
        return false;
    }
    auto interaction_point =
        presentation.target.position;
    auto interaction_radius =
        std::max(
            presentation.target
                .discovery_radius,
            3.5F);
    auto vertical_tolerance = 24.0F;
    if (legendary_quest_world_content_
            .has_value()) {
        if (const auto placement =
                legendary_quest_world_content_
                    ->anchor(
                        presentation.target.id);
            placement.has_value()) {
            interaction_point =
                placement
                    ->interaction_position;
            interaction_radius =
                std::max(
                    placement
                        ->horizontal_interaction_radius,
                    1.0F);
            vertical_tolerance =
                std::max(
                    placement
                        ->vertical_tolerance,
                    1.0F);
        }
    }
    if (!is_legendary_quest_near_horizontal(
            {
                player_.position().x,
                player_.position().y,
                player_.position().z,
            },
            legendary_quest_spatial_point(
                interaction_point),
            interaction_radius,
            vertical_tolerance)) {
        return false;
    }

    LegendaryQuestRequest request {};
    request.anchor_id =
        presentation.target.id;
    switch (progression.quest_stage) {
    case LegendaryWeaponQuestStage::
        NotStarted:
        request.action =
            LegendaryQuestAction::
                HearRumor;
        break;
    case LegendaryWeaponQuestStage::
        RumorHeard:
        request.action =
            LegendaryQuestAction::
                CollectMapFragment;
        request.fragment_index =
            progression
                .map_fragments_collected;
        break;
    case LegendaryWeaponQuestStage::
        MapFragmentsComplete:
        request.action =
            LegendaryQuestAction::
                DiscoverForge;
        break;
    case LegendaryWeaponQuestStage::
        GuardianDefeated:
        request.action =
            LegendaryQuestAction::
                InteractWithBlade;
        break;
    case LegendaryWeaponQuestStage::
        ForgeDiscovered:
        queue_gameplay_announcement(
            "LE GARDIEN BLOQUE LA FORGE",
            "BRISEZ SON ARMURE ET DESEQUILIBREZ-LE",
            2.6F);
        return true;
    case LegendaryWeaponQuestStage::
        WeaponClaimed:
    case LegendaryWeaponQuestStage::
        FirstCombatComplete:
    default:
        return false;
    }
    static_cast<void>(
        process_legendary_quest_request(
            request));
    return true;
}

void Game::record_legendary_quest_tutorial(
    LegendaryQuestAction action,
    std::uint64_t target_id,
    float combat_value) {
    if (legendary_weapon_progression_
            .state()
            .quest_stage !=
        LegendaryWeaponQuestStage::
            WeaponClaimed) {
        return;
    }
    static_cast<void>(
        process_legendary_quest_request({
            action,
            0U,
            0U,
            target_id == 0U
                ? 1U
                : target_id,
            std::max(
                combat_value,
                0.01F),
        }));
}

void Game::consume_legendary_quest_events() {
    std::array<
        LegendaryQuestEvent,
        kLegendaryQuestEventCapacity>
        events {};
    const auto count =
        legendary_weapon_quest_
            .drain_events(events);
    for (std::size_t index = 0U;
         index < count;
         ++index) {
        const auto& event =
            events[index];
        switch (event.type) {
        case LegendaryQuestEventType::
            RumorHeard:
            queue_gameplay_announcement(
                "UNE RUMEUR ANCIENNE",
                "RETROUVEZ LES TROIS FRAGMENTS DE CARTE",
                3.3F);
            break;
        case LegendaryQuestEventType::
            MapFragmentCollected:
            queue_gameplay_announcement(
                "FRAGMENT DE CARTE",
                "UN NOUVEL INDICE MENE A LA FORGE",
                2.6F);
            break;
        case LegendaryQuestEventType::
            MapCompleted:
            queue_gameplay_announcement(
                "CARTE RECONSTITUEE",
                "LA FORGE ABANDONNEE EST LOCALISEE",
                3.0F);
            break;
        case LegendaryQuestEventType::
            ForgeDiscovered:
            queue_gameplay_announcement(
                "LA FORGE INTERDITE",
                "UN GARDIEN LOURD PROTEGE LA LAME",
                3.1F);
            break;
        case LegendaryQuestEventType::
            GuardianDefeated:
            queue_gameplay_announcement(
                "LE GARDIEN EST TOMBE",
                "LA DERNIERE SALLE EST OUVERTE",
                2.8F);
            break;
        case LegendaryQuestEventType::
            BladeRefused:
            queue_gameplay_announcement(
                "LA LAME NE BOUGE PAS",
                "NIVEAU 35 ET FORCE 4 REQUIS",
                3.2F);
            music_.play_sfx(
                GameSfxKind::CreatureHit,
                0.78F);
            break;
        case LegendaryQuestEventType::
            BladeRequirementsMissing:
            queue_gameplay_announcement(
                "PUISSANCE INSUFFISANTE",
                "NIVEAU 35 ET FORCE 4 REQUIS",
                2.8F);
            break;
        case LegendaryQuestEventType::
            InventoryFull:
            queue_gameplay_announcement(
                "INVENTAIRE PLEIN",
                "LIBEREZ UNE PLACE POUR LA LAME",
                2.4F);
            break;
        case LegendaryQuestEventType::
            WeaponAcquired:
            queue_gameplay_announcement(
                "L'ECHINE DU LEVIATHAN",
                "LA LAME COLOSSALE VOUS RECONNAIT",
                4.0F);
            music_.play_sfx(
                GameSfxKind::CreatureHit,
                1.0F);
            break;
        case LegendaryQuestEventType::
            TutorialEncounterRequested:
            queue_gameplay_announcement(
                "PREMIER COMBAT",
                "BALAYAGE - GARDE - ATTAQUE CHARGEE",
                4.0F);
            break;
        case LegendaryQuestEventType::
            TutorialObjectiveCompleted:
            queue_gameplay_announcement(
                "MAITRISE PROGRESSE",
                "POURSUIVEZ L'EPREUVE DE LA LAME",
                1.9F);
            break;
        case LegendaryQuestEventType::
            QuestCompleted:
            queue_gameplay_announcement(
                "QUETE TERMINEE",
                "LE FER QUI N'AURAIT JAMAIS DU ETRE FORGE",
                4.5F);
            break;
        case LegendaryQuestEventType::
            TemporarySessionBlocked:
            queue_gameplay_announcement(
                "QUETE SUSPENDUE",
                "L'ARENE NE MODIFIE PAS VOTRE PARTIE",
                2.2F);
            break;
        default:
            break;
        }
    }
}

void Game::update_legendary_weapon_quest() {
    if (!has_active_session_ ||
        scenario_session_.active() ||
        issou_scenario_.active() ||
        !legendary_weapon_quest_
             .configured()) {
        return;
    }
    const auto progression =
        legendary_weapon_progression_
            .state();
    const auto& layout =
        legendary_weapon_quest_.layout();
    const auto quest_anchor_position =
        [this](const LegendaryQuestAnchor& anchor) {
            auto point = anchor.position;
            if (legendary_quest_world_content_
                    .has_value()) {
                if (const auto placement =
                        legendary_quest_world_content_
                            ->anchor(anchor.id);
                    placement.has_value()) {
                    point =
                        placement
                            ->interaction_position;
                }
            }
            return glm::vec3 {
                static_cast<float>(point.x) +
                    0.5F,
                static_cast<float>(point.y),
                static_cast<float>(point.z) +
                    0.5F,
            };
        };
    if (progression.quest_stage ==
            LegendaryWeaponQuestStage::
                MapFragmentsComplete) {
        auto target =
            layout.forge.position;
        auto radius =
            layout.forge
                .discovery_radius;
        auto vertical_tolerance = 40.0F;
        if (legendary_quest_world_content_
                .has_value()) {
            if (const auto placement =
                    legendary_quest_world_content_
                        ->anchor(
                            layout.forge.id);
                placement.has_value()) {
                target =
                    placement
                        ->interaction_position;
                radius =
                    placement
                        ->horizontal_interaction_radius;
                vertical_tolerance =
                    placement
                        ->vertical_tolerance;
            }
        }
        if (is_legendary_quest_near_horizontal(
                {
                    player_.position().x,
                    player_.position().y,
                    player_.position().z,
                },
                legendary_quest_spatial_point(
                    target),
                radius,
                vertical_tolerance)) {
            static_cast<void>(
                process_legendary_quest_request({
                    LegendaryQuestAction::
                        DiscoverForge,
                    layout.forge.id,
                }));
        }
    }

    const auto current =
        legendary_weapon_progression_
            .state();
    if (current.quest_stage ==
            LegendaryWeaponQuestStage::
                ForgeDiscovered &&
        legendary_quest_guardian_id_ ==
            0U) {
        const auto guardian_position =
            quest_anchor_position(
                layout.guardian);
        const auto spawned =
            legendary_enemies_.spawn({
                LegendaryEnemyArchetype::
                    ForgeGuardian,
                static_cast<std::uint32_t>(
                    layout.signature),
                guardian_position,
                0.0F,
            });
        if (spawned.spawned) {
            legendary_quest_guardian_id_ =
                spawned.id;
        }
    }
    if (current.quest_stage ==
            LegendaryWeaponQuestStage::
                WeaponClaimed &&
        !legendary_quest_tutorial_spawned_) {
        constexpr std::array<
            LegendaryEnemyArchetype,
            3U>
            kTutorialEnemies {{
                LegendaryEnemyArchetype::
                    CorruptedBrute,
                LegendaryEnemyArchetype::
                    SwiftHunter,
                LegendaryEnemyArchetype::
                    ArmoredGuard,
            }};
        const auto blade =
            quest_anchor_position(
                layout.blade);
        auto spawned_count =
            std::size_t {0U};
        for (std::size_t index = 0U;
             index <
             kTutorialEnemies.size();
             ++index) {
            const auto angle =
                static_cast<float>(index) *
                2.0943951F;
            const auto result =
                legendary_enemies_.spawn({
                    kTutorialEnemies[index],
                    static_cast<std::uint32_t>(
                        layout.signature ^
                        (index + 1U)),
                    blade +
                        glm::vec3 {
                            std::cos(angle) *
                                5.0F,
                            0.0F,
                            std::sin(angle) *
                                5.0F,
                        },
                    angle +
                        3.14159265F,
                });
            spawned_count +=
                result.spawned
                    ? 1U
                    : 0U;
        }
        legendary_quest_tutorial_spawned_ =
            spawned_count ==
            kTutorialEnemies.size();
    }

    if (current.weapon_owned &&
        current.corrupted_kills >=
            kLegendaryWeaponFirstAwakeningKills &&
        !current.astral_boss_defeated &&
        legendary_astral_boss_id_ == 0U) {
        const auto forge =
            quest_anchor_position(layout.forge);
        const auto delta =
            player_.position() - forge;
        const auto horizontal_distance_squared =
            delta.x * delta.x + delta.z * delta.z;
        if (horizontal_distance_squared <=
                18.0F * 18.0F &&
            std::abs(delta.y) <= 12.0F) {
            const auto spawned =
                legendary_enemies_.spawn({
                    LegendaryEnemyArchetype::
                        AstralBoss,
                    static_cast<std::uint32_t>(
                        layout.signature ^
                        0xA57A1B05ULL),
                    forge +
                        glm::vec3 {
                            8.0F,
                            0.0F,
                            0.0F,
                        },
                    -1.5707963F,
                });
            if (spawned.spawned) {
                legendary_astral_boss_id_ =
                    spawned.id;
                queue_gameplay_announcement(
                    "LE SOUVERAIN ASTRAL",
                    "L'ECHINE SOUILLEE PEUT FENDRE SON NOYAU",
                    3.4F);
            }
        }
    }
}

void Game::update_legendary_encounters(
    float dt,
    bool maritime_session_active) {
    if (issou_scenario_.active()) {
        update_issou_scenario(dt);
    }

    const auto weapon =
        colossal_weapon_.snapshot();
    static_cast<void>(
        legendary_enemies_.update(
            dt,
            {
                player_.position(),
                !player_.is_dead(),
                weapon.state ==
                        ColossalWeaponState::Windup ||
                    weapon.state ==
                        ColossalWeaponState::Charge,
                weapon.state ==
                    ColossalWeaponState::Recovery,
            }));

    std::array<
        LegendaryEnemyEvent,
        kMaximumLegendaryEnemyEvents>
        enemy_events {};
    const auto enemy_event_count =
        legendary_enemies_.consume_events(
            enemy_events);
    for (std::size_t index = 0U;
         index < enemy_event_count;
         ++index) {
        const auto& event =
            enemy_events[index];
        if (event.kind ==
                LegendaryEnemyEventKind::
                    AttackActive &&
            event.targets_player_only &&
            event.amount > 0.0F &&
            !player_.is_dead()) {
            auto to_attacker =
                event.local_position -
                player_.position();
            to_attacker.y = 0.0F;
            const auto direction =
                safe_horizontal_direction(
                    to_attacker,
                    -safe_horizontal_direction(
                        player_.look_direction()));
            const auto frontal_alignment =
                glm::dot(
                    safe_horizontal_direction(
                        player_.look_direction()),
                    direction);
            auto resulting_damage =
                event.amount;
            if (colossal_weapon_drawn()) {
                const auto guard =
                    intercept_colossal_guard({
                            event.amount,
                            1.0F,
                            frontal_alignment,
                            weapon.stability,
                            weapon.state ==
                                    ColossalWeaponState::Guard
                                ? weapon
                                      .state_elapsed_seconds
                                : 0.0F,
                            colossal_target_weight(
                                legendary_enemy_profile(
                                    event.archetype)
                                    .weight),
                            ColossalIncomingAttackKind::
                                Melee,
                            weapon.state ==
                                ColossalWeaponState::Guard,
                            false,
                        });
                resulting_damage =
                    guard.resulting_damage;
                if (guard.blocked) {
                    record_legendary_quest_tutorial(
                        LegendaryQuestAction::
                            TutorialGuardSucceeded,
                        event.enemy_id,
                        event.amount);
                }
                if (guard.perfect) {
                    static_cast<void>(
                        legendary_enemies_
                            .apply_hit(
                                event.enemy_id,
                                {
                                    0.0F,
                                    guard
                                        .attacker_stagger,
                                    true,
                                    static_cast<
                                        std::uint8_t>(
                                        legendary_weapon_progression_
                                            .state()
                                            .awakening),
                                }));
                    issou_scenario_
                        .notify_combat_event(
                            IssouArenaCombatEvent::
                                PerfectGuard);
                    music_.play_sfx(
                        GameSfxKind::
                            PerfectGuard,
                        1.0F);
                    queue_gameplay_announcement(
                        "GARDE PARFAITE",
                        "L'ENNEMI EST DESEQUILIBRE",
                        1.4F);
                }
            }
            const auto damage =
                player_
                    .apply_external_damage_report(
                        resulting_damage,
                        PlayerDeathCause::Zombie);
            if (damage.applied()) {
                auto velocity =
                    player_.state().velocity;
                velocity.x -=
                    direction.x * 1.7F;
                velocity.z -=
                    direction.z * 1.7F;
                player_.set_velocity(
                    velocity);
                issou_scenario_
                    .notify_combat_event(
                        IssouArenaCombatEvent::
                            PlayerHit);
                music_.play_sfx(
                    GameSfxKind::CreatureAttack,
                    0.72F);
            }
        }
        if (event.kind ==
                LegendaryEnemyEventKind::Died &&
            event.enemy_id != 0U &&
            event.enemy_id ==
                legendary_quest_guardian_id_ &&
            legendary_weapon_progression_
                    .state()
                    .quest_stage ==
                LegendaryWeaponQuestStage::
                    ForgeDiscovered) {
            static_cast<void>(
                process_legendary_quest_request({
                    LegendaryQuestAction::
                        DefeatGuardian,
                    legendary_weapon_quest_
                        .layout()
                        .guardian.id,
                    0U,
                    event.enemy_id,
                    1.0F,
                }));
        }
        if (event.kind ==
                LegendaryEnemyEventKind::Died &&
            event.enemy_id != 0U &&
            event.enemy_id ==
                legendary_astral_boss_id_ &&
            !issou_scenario_.active()) {
            legendary_astral_boss_id_ = 0U;
            if (legendary_weapon_progression_
                    .record_astral_boss_defeat()) {
                mark_session_dirty();
                queue_gameplay_announcement(
                    "LAME ASTRALE",
                    "LE NOYAU DU SOUVERAIN A EVEILLE LES RUNES",
                    4.0F);
            }
        }
        if (event.kind ==
                LegendaryEnemyEventKind::
                    RewardAvailable &&
            event.reward.experience_points >
                0U &&
            scenario_session_
                .permanent_rewards_allowed()) {
            award_player_experience(
                CombatExperienceEvent {
                    event.reward
                        .experience_points,
                    true,
                    maritime_session_active,
                    environment_
                        .current_creature_cycle()
                        .phase,
                },
                block_coord_from_position(
                    event.local_position),
                "legendary_enemy");
        }
    }

    if (!maritime_session_active ||
        !sea_adventure_.active()) {
        return;
    }

    constexpr auto kSeaLeviathanRouteTrigger =
        420.0F;
    const auto& sea_state =
        sea_adventure_.save_state();
    if (!sea_leviathan_started_for_session_ &&
        sea_state.voyage_phase ==
            SeaVoyagePhase::Underway &&
        sea_state.route_distance >=
            kSeaLeviathanRouteTrigger &&
        inventory_has_legendary_weapon(
            inventory_menu_,
            hotbar_)) {
        const auto started =
            sea_leviathan_.start({
                static_cast<std::uint32_t>(
                    world_.seed()) ^
                    0x1E71A7A5U,
                {0.0F, -1.5F, 7.0F},
            });
        sea_leviathan_started_for_session_ =
            started.started;
    }
    if (!sea_leviathan_.active()) {
        return;
    }

    const auto& ship =
        sea_adventure_.ship_entity();
    const ShipLocalFrame frame {
        ship.world_origin(),
        ship.local_to_world_direction(
            {1.0F, 0.0F, 0.0F}),
        ship.local_to_world_direction(
            {0.0F, 1.0F, 0.0F}),
        ship.local_to_world_direction(
            {0.0F, 0.0F, 1.0F}),
    };
    const auto current_weapon =
        colossal_weapon_.snapshot();
    static_cast<void>(
        sea_leviathan_.update(
            dt,
            {
                frame,
                player_.position(),
                !player_.is_dead(),
                current_weapon.state ==
                    ColossalWeaponState::Guard,
                current_weapon.state ==
                        ColossalWeaponState::Guard &&
                    current_weapon
                            .state_elapsed_seconds <=
                        kLeviathanSpineDefinition
                            .perfect_guard_window_seconds,
            }));
    std::array<
        SeaLeviathanEvent,
        kMaximumSeaLeviathanEvents>
        sea_events {};
    const auto sea_event_count =
        sea_leviathan_.consume_events(
            sea_events);
    for (std::size_t index = 0U;
         index < sea_event_count;
         ++index) {
        const auto& event =
            sea_events[index];
        if (event.damage.player_damage >
                0.0F &&
            !player_.is_dead()) {
            const auto damage =
                player_
                    .apply_external_damage_report(
                        event.damage
                            .player_damage,
                        PlayerDeathCause::Zombie);
            if (damage.applied()) {
                music_.play_sfx(
                    GameSfxKind::SeaLeviathan,
                    0.9F);
            }
        }
        switch (event.kind) {
        case SeaLeviathanEventKind::
            EncounterStarted:
            music_.play_sfx(
                GameSfxKind::SeaLeviathan,
                1.0F);
            queue_gameplay_announcement(
                "LA MER SE DECHIRE",
                "UN LEVIATHAN ATTAQUE L'AMELIE",
                3.4F);
            break;
        case SeaLeviathanEventKind::
            GuardWindowOpened:
            queue_gameplay_announcement(
                "TENIR LA LAME",
                "GARDEZ LE COUP DE PONT",
                2.2F);
            break;
        case SeaLeviathanEventKind::
            ChargedOpeningRequested:
            queue_gameplay_announcement(
                "CARAPACE FRAGILE",
                "PREPAREZ UNE ATTAQUE CHARGEE",
                2.4F);
            break;
        case SeaLeviathanEventKind::
            PerfectGuard:
            record_legendary_quest_tutorial(
                LegendaryQuestAction::
                    TutorialGuardSucceeded,
                event.simulation_tick == 0U
                    ? 1U
                    : event.simulation_tick,
                std::max(
                    event.amount,
                    1.0F));
            queue_gameplay_announcement(
                "GARDE PARFAITE",
                "LE NOYAU VA S'OUVRIR",
                1.8F);
            music_.play_sfx(
                GameSfxKind::PerfectGuard,
                1.0F);
            break;
        case SeaLeviathanEventKind::
            TentacleSevered:
            music_.play_sfx(
                GameSfxKind::BoneImpact,
                1.0F);
            queue_gameplay_announcement(
                "TENTACULE SECTIONNE",
                "LE LEVIATHAN RECULE",
                1.8F);
            break;
        case SeaLeviathanEventKind::Defeated:
            music_.play_sfx(
                GameSfxKind::SeaLeviathan,
                0.82F);
            queue_gameplay_announcement(
                "LEVIATHAN VAINCU",
                "L'AMELIE EST SAUVE",
                3.2F);
            break;
        default:
            break;
        }
    }
}

void Game::process_colossus_attack_events() {
    for (const auto& attack :
         chained_colossus_
             .consume_attack_events()) {
        if (player_.is_dead()) {
            continue;
        }
        const auto delta =
            player_.position() -
            attack.origin;
        const auto distance_squared =
            delta.x * delta.x +
            delta.z * delta.z;
        const auto contact_radius =
            std::max(
                attack.radius,
                0.0F) +
            0.55F;
        if (!std::isfinite(
                distance_squared) ||
            distance_squared >
                contact_radius *
                    contact_radius) {
            continue;
        }

        const auto to_attacker =
            safe_horizontal_direction(
                attack.origin -
                    player_.position(),
                -safe_horizontal_direction(
                    player_.look_direction()));
        const auto frontal_alignment =
            glm::dot(
                safe_horizontal_direction(
                    player_.look_direction()),
                to_attacker);
        auto resulting_damage =
            attack.damage;
        if (colossal_weapon_drawn()) {
            const auto weapon =
                colossal_weapon_.snapshot();
            const auto guard =
                intercept_colossal_guard({
                        attack.damage,
                        attack
                            .stability_coefficient,
                        frontal_alignment,
                        weapon.stability,
                        weapon.state ==
                                ColossalWeaponState::Guard
                            ? weapon
                                  .state_elapsed_seconds
                            : 0.0F,
                        ColossalTargetWeight::Boss,
                        attack.kind ==
                                ChainedColossusAttackKind::
                                    GroundShockwave
                            ? ColossalIncomingAttackKind::
                                  GroundHazard
                            : ColossalIncomingAttackKind::
                                  Melee,
                        weapon.state ==
                            ColossalWeaponState::Guard,
                        !attack
                             .frontally_guardable,
                    });
            resulting_damage =
                guard.resulting_damage;
            if (guard.blocked) {
                record_legendary_quest_tutorial(
                    LegendaryQuestAction::
                        TutorialGuardSucceeded,
                    attack.sequence,
                    attack.damage);
            }
            if (guard.perfect) {
                static_cast<void>(
                    chained_colossus_
                        .apply_hit({
                            kColossusTorsoZone,
                            0.0F,
                            guard
                                .attacker_stagger,
                            0.0F,
                            GorePresentationMode::
                                Disabled,
                            true,
                            false,
                        }));
                issou_scenario_
                    .notify_combat_event(
                        IssouArenaCombatEvent::
                            PerfectGuard);
                music_.play_sfx(
                    GameSfxKind::PerfectGuard,
                    1.0F);
                queue_gameplay_announcement(
                    "GARDE PARFAITE",
                    "LE COLOSSE VACILLE",
                    1.5F);
            }
        }
        const auto damage =
            player_
                .apply_external_damage_report(
                    resulting_damage,
                    PlayerDeathCause::Zombie);
        if (damage.applied()) {
            issou_scenario_
                .notify_combat_event(
                    IssouArenaCombatEvent::
                        PlayerHit);
            auto velocity =
                player_.state().velocity;
            velocity.x +=
                attack.direction.x *
                attack
                    .stability_coefficient *
                2.1F;
            velocity.z +=
                attack.direction.z *
                attack
                    .stability_coefficient *
                2.1F;
            player_.set_velocity(
                velocity);
            music_.play_sfx(
                GameSfxKind::CreatureAttack,
                0.95F);
        }
    }
}

void Game::update_issou_scenario(
    float dt) {
    const auto previous_phase =
        issou_scenario_.state().phase;
    issou_scenario_.update(dt);
    const auto phase =
        issou_scenario_.state().phase;
    if (previous_phase !=
            IssouArenaPhase::Combat &&
        phase ==
            IssouArenaPhase::Combat) {
        chained_colossus_.release();
        chained_colossus_.set_invulnerable(
            false);
    }

    chained_colossus_.update(
        dt,
        player_.position());
    process_colossus_attack_events();
    colossus_blood_traces_.update(dt);

    const auto health_ratio =
        chained_colossus_.state().health /
        kChainedColossusMaximumHealth;
    if (phase ==
            IssouArenaPhase::Combat &&
        health_ratio <= 0.75F &&
        !issou_arena_minions_spawned_) {
        const auto& layout =
            issou_scenario_.state()
                .layout;
        constexpr std::array<
            glm::vec3,
            4U>
            kOffsets {{
                {-5.0F, 0.0F, -1.5F},
                {-2.4F, 0.0F, -4.0F},
                {2.4F, 0.0F, -4.0F},
                {5.0F, 0.0F, -1.5F},
            }};
        auto spawned =
            std::size_t {0U};
        for (std::size_t index = 0U;
             index < kOffsets.size();
             ++index) {
            const auto result =
                legendary_enemies_.spawn({
                    LegendaryEnemyArchetype::
                        ArenaMinion,
                    static_cast<std::uint32_t>(
                        layout.seed) ^
                        static_cast<std::uint32_t>(
                            index * 0x9E37U),
                    layout.colossus_spawn +
                        kOffsets[index],
                    0.0F,
                });
            spawned +=
                result.spawned
                    ? 1U
                    : 0U;
        }
        issou_arena_minions_spawned_ =
            spawned == kOffsets.size();
        if (spawned > 0U) {
            queue_gameplay_announcement(
                "LES PORTES S'OUVRENT",
                "FAUCHEZ LE GROUPE",
                2.2F);
        }
    }
    if (phase ==
            IssouArenaPhase::Combat &&
        health_ratio < 0.25F) {
        issou_scenario_
            .notify_combat_event(
                IssouArenaCombatEvent::
                    BossBelowQuarterHealth);
    }
    if (phase ==
            IssouArenaPhase::Combat &&
        chained_colossus_.state()
                .phase ==
            ChainedColossusPhase::Dead) {
        issou_scenario_
            .notify_combat_event(
                chained_colossus_.state()
                        .executed
                    ? IssouArenaCombatEvent::
                          BossExecuted
                    : IssouArenaCombatEvent::
                          BossKilled);
    }
    consume_issou_scenario_events();
}

void Game::consume_issou_scenario_events() {
    for (const auto& event :
         issou_scenario_
             .consume_events()) {
        latest_issou_event_ =
            event.kind;
        switch (event.kind) {
        case IssouArenaEventKind::Horn:
            music_.play_sfx(
                GameSfxKind::Crowd,
                0.70F);
            break;
        case IssouArenaEventKind::
            WeaponTitle:
            queue_gameplay_announcement(
                "L'ECHINE DU LEVIATHAN",
                "ARME LEGENDAIRE COLOSSALE",
                2.8F);
            break;
        case IssouArenaEventKind::
            CountdownStarted:
            queue_gameplay_announcement(
                "PREPAREZ-VOUS",
                "LE COLOSSE SERA LIBERE DANS 10",
                2.4F);
            break;
        case IssouArenaEventKind::
            ChainStrain:
        case IssouArenaEventKind::
            ChainCrack:
            music_.play_sfx(
                GameSfxKind::ChainBreak,
                std::max(
                    event.intensity,
                    0.45F),
                0.0F,
                1.0F,
                event.sequence);
            break;
        case IssouArenaEventKind::
            ColossusRoar:
            music_.play_sfx(
                GameSfxKind::ColossusRoar,
                std::max(
                    event.intensity,
                    0.75F),
                0.0F,
                1.0F,
                event.sequence);
            break;
        case IssouArenaEventKind::
            CrowdMurmur:
        case IssouArenaEventKind::
            CrowdApplause:
        case IssouArenaEventKind::CrowdBoo:
        case IssouArenaEventKind::
            CrowdCheer:
        case IssouArenaEventKind::CrowdRoar:
            music_.play_sfx(
                GameSfxKind::Crowd,
                std::max(
                    event.intensity,
                    0.22F),
                0.0F,
                1.0F,
                event.sequence);
            break;
        case IssouArenaEventKind::
            ChainsBroken:
            chained_colossus_.release();
            chained_colossus_
                .set_invulnerable(
                    false);
            queue_gameplay_announcement(
                "LES CHAINES CEDENT",
                "COMBAT",
                1.8F);
            break;
        case IssouArenaEventKind::Victory:
            queue_gameplay_announcement(
                "LE COLOSSE EST TOMBE",
                "R POUR RECOMMENCER - /ISSOU EXIT POUR QUITTER",
                5.0F);
            break;
        case IssouArenaEventKind::Defeat:
            queue_gameplay_announcement(
                "DEFAITE",
                "RECOMMENCEZ POUR RELEVER LE DEFI",
                3.2F);
            break;
        default:
            break;
        }
    }
}

auto Game::enter_issou_scenario() -> bool {
    if (!has_active_session_ ||
        options_.smoke_test ||
        issou_scenario_.active() ||
        scenario_session_.active()) {
        return false;
    }

    if (!finish_pending_save(true)) {
        queue_gameplay_announcement(
            "SAUVEGARDE NON SECURISEE",
            "L'ARENE N'A PAS ETE OUVERTE POUR PROTEGER LA PARTIE",
            4.0F);
        return false;
    }
    const auto layout =
        IssouArenaLayoutGenerator {
            world_.seed() ^ 0x1550,
        }
            .build_layout();
    auto arena_world =
        World {
            layout.seed,
            options_.performance
                .stream_radius,
            WorldGenerationProfile::
                Continental,
            WorldGenerationVersion::Latest,
            options_.visual_pipeline,
        };
    IssouArenaLayoutGenerator {
        layout.seed,
    }
        .apply(
            arena_world,
            layout);

    auto runtime_restore =
        ScenarioLegendaryRuntimeRestore {};
    runtime_restore.environment =
        environment_;
    runtime_restore.player =
        player_;
    runtime_restore.player_musket =
        player_musket_;
    runtime_restore.player_musket_effects =
        player_musket_effects_;
    runtime_restore.progression =
        progression_;
    runtime_restore.weapon_progression =
        legendary_weapon_progression_;
    runtime_restore.experience_awards =
        experience_awards_;
    runtime_restore.player_build =
        player_build_;
    runtime_restore.ability_system =
        ability_system_;
    runtime_restore.ability_effects =
        player_ability_effects_;
    runtime_restore.creatures =
        creatures_;
    runtime_restore.item_drops =
        item_drops_;
    runtime_restore.sea_adventure =
        sea_adventure_;
    runtime_restore.hotbar =
        hotbar_;
    runtime_restore.inventory =
        inventory_menu_;
    runtime_restore.starting_village =
        starting_village_;
    runtime_restore.summoned_footmen =
        summoned_footmen_;
    runtime_restore
        .summoned_footman_ship_local_positions =
        summoned_footman_ship_local_positions_;
    runtime_restore
        .summoned_footman_far_seconds =
        summoned_footman_far_seconds_;
    runtime_restore
        .summoned_footman_cast_sequences =
        summoned_footman_cast_sequences_;
    runtime_restore.weapon =
        colossal_weapon_;
    runtime_restore.knight_synergy =
        leviathan_knight_synergy_;
    runtime_restore.hit_ledger =
        colossal_hit_ledger_;
    runtime_restore.previous_blade_pose =
        previous_colossal_blade_pose_;
    runtime_restore.protections =
        colossal_world_protections_;
    runtime_restore.blood_traces =
        colossus_blood_traces_;
    runtime_restore.enemies =
        legendary_enemies_;
    runtime_restore.sea_encounter =
        sea_leviathan_;
    runtime_restore.bound_musket_hotbar_slot =
        bound_musket_hotbar_slot_;
    runtime_restore.pending_ability_slot =
        pending_ability_slot_;
    runtime_restore.spawn_position =
        spawn_position_;
    runtime_restore.active_game_mode =
        active_game_mode_;
    runtime_restore.wall_impact_sequence =
        colossal_wall_impact_sequence_;
    runtime_restore.shockwave_sequence =
        colossal_shockwave_sequence_;
    runtime_restore.quest_guardian_id =
        legendary_quest_guardian_id_;
    runtime_restore.astral_boss_id =
        legendary_astral_boss_id_;
    runtime_restore
        .wind_acceleration_cast_sequence =
        wind_acceleration_cast_sequence_;
    runtime_restore
        .melee_attack_cooldown_remaining =
        melee_attack_cooldown_remaining_;
    runtime_restore.wind_acceleration_remaining =
        wind_acceleration_remaining_;
    runtime_restore.wind_movement_bonus =
        wind_movement_bonus_;
    runtime_restore.wind_recovery_bonus =
        wind_recovery_bonus_;
    runtime_restore.wind_dodge_remaining =
        wind_dodge_remaining_;
    runtime_restore
        .seconds_since_successful_shield_block =
        seconds_since_successful_shield_block_;
    runtime_restore.backrooms_elapsed_seconds =
        backrooms_elapsed_seconds_;
    runtime_restore.backrooms_flashlight =
        backrooms_flashlight_;
    runtime_restore.backrooms_jack =
        backrooms_jack_;
    runtime_restore.backrooms_jack_runtime =
        backrooms_jack_runtime_;
    runtime_restore.backrooms_jack_last_result =
        backrooms_jack_last_result_;
    runtime_restore
        .backrooms_jack_death_delay_seconds =
        backrooms_jack_death_delay_seconds_;
    runtime_restore
        .backrooms_jack_death_pending =
        backrooms_jack_death_pending_;
    runtime_restore.backrooms_marlow =
        backrooms_marlow_;
    runtime_restore.backrooms_marlow_runtime =
        backrooms_marlow_runtime_;
    runtime_restore.backrooms_marlow_last_result =
        backrooms_marlow_last_result_;
    runtime_restore.backrooms_threat_arbiter =
        backrooms_threat_arbiter_;
    runtime_restore.backrooms_threat_request_sequence =
        backrooms_threat_request_sequence_;
    runtime_restore.backrooms_marlow_previous_player_position =
        backrooms_marlow_previous_player_position_;
    runtime_restore.backrooms_marlow_death_delay_seconds =
        backrooms_marlow_death_delay_seconds_;
    runtime_restore.backrooms_marlow_death_pending =
        backrooms_marlow_death_pending_;
    runtime_restore.backrooms_marlow_previous_in_water =
        backrooms_marlow_previous_in_water_;
    runtime_restore.backrooms_marlow_previous_on_ground =
        backrooms_marlow_previous_on_ground_;
    runtime_restore.backrooms_marlow_previous_jump_input =
        backrooms_marlow_previous_jump_input_;
    runtime_restore.backrooms_marlow_has_previous_player_position =
        backrooms_marlow_has_previous_player_position_;
    runtime_restore.blade_pose_valid =
        colossal_blade_pose_valid_;
    runtime_restore.weapon_was_selected =
        colossal_weapon_was_selected_;
    runtime_restore.sea_encounter_started =
        sea_leviathan_started_for_session_;
    runtime_restore.wind_blade_available =
        wind_blade_available_;
    runtime_restore.super_vision_active =
        super_vision_active_;
    runtime_restore.starting_village_enabled =
        starting_village_enabled_;
    runtime_restore.quest_tutorial_spawned =
        legendary_quest_tutorial_spawned_;

    // Là, je capture les données dérivées avant de déplacer le monde actif.
    auto preserved_snapshot =
        make_world_snapshot();
    auto preserved_world =
        std::make_unique<World>(
            std::move(world_));
    if (!scenario_session_.capture(
            std::move(preserved_world),
            std::move(preserved_snapshot),
            session_save_state_,
            active_save_slot_)) {
        return false;
    }
    scenario_legendary_runtime_restore_ =
        std::move(runtime_restore);
    world_ = std::move(arena_world);

    ++issou_run_sequence_;
    if (issou_run_sequence_ == 0U) {
        issou_run_sequence_ = 1U;
    }
    if (!issou_scenario_.enter(
            layout,
            issou_run_sequence_)) {
        if (auto restore =
                scenario_session_.release();
            restore.has_value()) {
            restore_scenario_snapshot(
                std::move(*restore));
        }
        return false;
    }

    session_save_state_.reset_clean();

    renderer_.reset_world_resources();
    world_.enqueue_loaded_mesh_uploads();
    initialize_issou_run_state(
        layout,
        false);
    queue_gameplay_announcement(
        "L'ARENE DU COLOSSE",
        "L'ECHINE DU LEVIATHAN",
        3.8F);
    queue_gameplay_announcement(
        "COMMANDES DE L'ARME",
        "CLIC G COMBO - MAINTENIR EXECUTION - CLIC D GARDE PARFAITE",
        6.0F);
    return true;
}

void Game::initialize_issou_run_state(
    const IssouArenaLayout& layout,
    bool rebuild_world) {
    if (rebuild_world) {
        IssouArenaLayoutGenerator {
            layout.seed,
        }
            .apply(
                world_,
                layout);
    }

    // Je repars du meme socle a l'entree et apres chaque reset afin qu'aucun
    // effet, invocation, projectile ou objet du combat precedent ne survive.
    prepare_game_session();
    active_game_mode_ =
        GameMode::ClassicAdventure;
    sea_adventure_.load_state(
        {},
        layout.seed);
    sea_leviathan_.reset();
    sea_leviathan_started_for_session_ =
        false;
    legendary_enemies_.clear();
    legendary_quest_guardian_id_ = 0U;
    legendary_astral_boss_id_ = 0U;
    legendary_quest_tutorial_spawned_ =
        false;
    creatures_.clear();
    item_drops_.clear();
    item_drop_render_instances_.clear();
    progression_creature_render_instances_
        .clear();
    issou_arena_minions_spawned_ = false;
    latest_issou_event_ =
        IssouArenaEventKind::CrowdMurmur;

    for (auto& footman :
         summoned_footmen_) {
        footman.clear();
    }
    summoned_footman_ship_local_positions_
        .fill(std::nullopt);
    summoned_footman_far_seconds_.fill(
        0.0F);
    summoned_footman_cast_sequences_.fill(
        0U);

    hotbar_ = {};
    inventory_menu_ = {};
    static_cast<void>(
        inventory_try_grant_legendary_weapon(
            inventory_menu_,
            hotbar_));

    // Je reconstruis le build de demonstration depuis le build original :
    // chaque reset recharge donc aussi l'energie et toutes les capacites.
    if (scenario_legendary_runtime_restore_
            .has_value()) {
        player_build_ =
            scenario_legendary_runtime_restore_
                ->player_build;
    }
    player_build_.global_cooldown_remaining =
        0.0F;
    player_build_
        .energy_regeneration_delay_remaining =
        0.0F;
    player_build_.cooldowns_remaining.fill(
        0.0F);
    player_build_.charges.fill(0U);
    player_build_.successful_cast_sequence =
        0U;
    sanitize_player_build_state(
        player_build_,
        progression_.level());
    player_build_.attributes.values[
        player_attribute_index(
            PlayerAttribute::Strength)] =
        std::max<std::uint8_t>(
            player_build_.attributes.values[
                player_attribute_index(
                    PlayerAttribute::Strength)],
            kLeviathanSpineDefinition
                .demonstration_strength);
    player_build_.val_energy =
        player_max_val_energy(
            player_attribute_value(
                player_build_.attributes,
                PlayerAttribute::Wisdom));

    ability_system_ = {};
    player_ability_effects_.clear();
    pending_ability_slot_.reset();
    melee_attack_cooldown_remaining_ =
        0.0F;
    wind_acceleration_remaining_ = 0.0F;
    wind_movement_bonus_ = 0.0F;
    wind_recovery_bonus_ = 0.0F;
    wind_dodge_remaining_ = 0.0F;
    wind_blade_available_ = false;
    wind_acceleration_cast_sequence_ = 0U;
    seconds_since_successful_shield_block_ =
        -1.0F;
    super_vision_active_ = false;

    spawn_position_ =
        layout.player_spawn;
    environment_.set_time_of_day(
        19.25F);
    environment_.set_weather_seed(
        static_cast<std::uint32_t>(
            layout.seed));
    environment_.set_weather_time_seconds(
        0.0F);
    environment_.set_frozen(
        options_.freeze_time ||
        options_.smoke_test);
    starting_village_enabled_ = false;
    starting_village_ = {};

    chained_colossus_.reset(
        layout.colossus_spawn,
        static_cast<std::uint32_t>(
            layout.seed) ^
            issou_scenario_.state()
                .run_sequence);
    chained_colossus_.set_invulnerable(
        true);
    colossus_blood_traces_.clear();
    colossal_weapon_.reset();
    leviathan_knight_synergy_.reset();
    colossal_hit_ledger_.clear();
    previous_colossal_blade_pose_ = {};
    colossal_wall_impact_sequence_ = 0U;
    colossal_shockwave_sequence_ = 0U;
    colossal_blade_pose_valid_ = false;
    colossal_weapon_was_selected_ = false;
    clear_colossal_weapon_input();
    rebuild_colossal_world_protections();

    sync_selected_hotbar_slot();
    player_.respawn(
        layout.player_spawn);
    player_.set_velocity({});
    set_death_screen_visible(false);
    set_mouse_capture(true);
}

auto Game::reset_issou_scenario() -> bool {
    if (!issou_scenario_.reset()) {
        return false;
    }
    const auto& layout =
        issou_scenario_.state().layout;
    initialize_issou_run_state(
        layout,
        true);
    return true;
}

auto Game::exit_issou_scenario() -> bool {
    if (!issou_scenario_.active() ||
        !scenario_session_.active()) {
        return false;
    }
    static_cast<void>(
        issou_scenario_.request_exit());
    auto restore =
        scenario_session_.release();
    if (!restore.has_value()) {
        return false;
    }
    restore_scenario_snapshot(
        std::move(*restore));
    return true;
}

void Game::restore_scenario_snapshot(
    ScenarioSessionRestore restore) {
    if (restore.world == nullptr) {
        return;
    }
    auto runtime_restore =
        std::move(
            scenario_legendary_runtime_restore_);
    const auto snapshot =
        std::move(restore.snapshot);
    issou_scenario_ = {};
    chained_colossus_ = {};
    colossus_blood_traces_.clear();
    legendary_enemies_.clear();
    sea_leviathan_.reset();
    issou_arena_minions_spawned_ = false;
    latest_issou_event_ =
        IssouArenaEventKind::CrowdMurmur;

    renderer_.reset_world_resources();
    world_ = std::move(
        *restore.world);
    world_.enqueue_loaded_mesh_uploads();
    active_game_mode_ =
        is_known_game_mode(
            snapshot.metadata.game_mode)
            ? snapshot.metadata.game_mode
            : GameMode::ClassicAdventure;
    sea_adventure_.load_state(
        snapshot.sea_adventure,
        snapshot.metadata.seed);

    hotbar_ = snapshot.hotbar;
    inventory_menu_ =
        snapshot.inventory;
    normalize_inventory_state(
        inventory_menu_,
        hotbar_);
    inventory_menu_.visible = false;
    inventory_menu_.hovered_slot.reset();
    item_drops_.load_drops(
        snapshot.item_drops);
    progression_.load_state(
        snapshot.progression);
    legendary_weapon_progression_
        .load_state(
            snapshot.legendary_weapon);
    experience_awards_.load_state(
        snapshot.maritime_experience);
    player_build_ =
        snapshot.player_build;
    sanitize_player_build_state(
        player_build_,
        progression_.level());

    const auto robustness =
        player_attribute_value(
            player_build_.attributes,
            PlayerAttribute::Robustness);
    player_.set_max_health(
        player_base_max_health(
            progression_.level()) +
        static_cast<float>(robustness));
    player_.load_state(
        snapshot.player_state);
    environment_.set_time_of_day(
        snapshot.metadata.time_of_day);
    environment_.set_weather_seed(
        static_cast<std::uint32_t>(
            snapshot.metadata.seed));
    environment_.set_weather_time_seconds(
        snapshot.metadata
            .weather_time_seconds);
    environment_.set_frozen(
        options_.freeze_time ||
        options_.smoke_test);

    starting_village_enabled_ =
        active_game_mode_ ==
            GameMode::ClassicAdventure &&
        snapshot.metadata
            .has_starting_village;
    starting_village_ = {};
    if (starting_village_enabled_) {
        starting_village_ =
            StartingVillageGenerator {
                snapshot.metadata.seed,
            }
                .build_layout();
        creatures_
            .set_settlement_residents(
                starting_village_
                    .residents);
    } else {
        creatures_
            .set_settlement_residents(
                {});
    }
    creatures_.load_creatures(
        snapshot.creatures,
        environment_.current_state());
    spawn_position_ =
        finite_vec3_or(
            snapshot.spawn_position,
            {0.5F, 70.0F, 0.5F});
    if (active_game_mode_ ==
            GameMode::SeaAdventure &&
        sea_adventure_.active()) {
        spawn_position_ =
            sea_adventure_
                .deck_spawn_position();
    }

    prepare_game_session(
        snapshot
            .musket_shot_sequence);
    const auto ability_runtime =
        sanitize_player_ability_runtime_save_state(
            snapshot.player_ability_runtime);
    static_cast<void>(
        player_ability_effects_
            .load_state(
                ability_runtime
                    .player_effects));
    ability_system_
        .reserve_next_cast_sequence(
            ability_runtime
                .next_cast_sequence);
    reserve_next_summoned_unit_id(
        ability_runtime
            .next_summoned_unit_id);
    for (std::size_t index = 0U;
         index <
         summoned_footmen_.size();
         ++index) {
        summoned_footmen_[index].clear();
        summoned_footman_ship_local_positions_[
            index]
            .reset();
        summoned_footman_far_seconds_[
            index] = 0.0F;
        summoned_footman_cast_sequences_[
            index] = 0U;
        const auto& saved =
            ability_runtime
                .summoned_footmen[index];
        const auto loaded =
            summoned_footmen_[index]
                .load_state(
                    saved.runtime);
        if (!loaded.restored) {
            continue;
        }
        summoned_footman_ship_local_positions_[
            index] =
            active_game_mode_ ==
                    GameMode::SeaAdventure
                ? saved.ship_local_position
                : std::nullopt;
        summoned_footman_far_seconds_[index] =
            saved.far_seconds;
        summoned_footman_cast_sequences_[index] =
            saved.cast_sequence;
    }
    wind_acceleration_remaining_ =
        ability_runtime.wind
            .remaining_seconds;
    wind_movement_bonus_ =
        ability_runtime.wind
            .movement_bonus;
    wind_recovery_bonus_ =
        ability_runtime.wind
            .recovery_bonus;
    wind_dodge_remaining_ =
        ability_runtime.wind
            .dodge_remaining_seconds;
    wind_blade_available_ =
        ability_runtime.wind
            .blade_armed;
    wind_acceleration_cast_sequence_ =
        ability_runtime.wind
            .cast_sequence;

    if (runtime_restore.has_value()) {
        active_game_mode_ =
            runtime_restore
                ->active_game_mode;
        environment_ =
            runtime_restore
                ->environment;
        progression_ =
            std::move(
                runtime_restore
                    ->progression);
        legendary_weapon_progression_ =
            std::move(
                runtime_restore
                    ->weapon_progression);
        experience_awards_ =
            std::move(
                runtime_restore
                    ->experience_awards);
        player_build_ =
            std::move(
                runtime_restore
                    ->player_build);
        ability_system_ =
            std::move(
                runtime_restore
                    ->ability_system);
        player_ability_effects_ =
            std::move(
                runtime_restore
                    ->ability_effects);
        creatures_ =
            std::move(
                runtime_restore
                    ->creatures);
        item_drops_ =
            std::move(
                runtime_restore
                    ->item_drops);
        sea_adventure_ =
            std::move(
                runtime_restore
                    ->sea_adventure);
        hotbar_ =
            std::move(
                runtime_restore
                    ->hotbar);
        inventory_menu_ =
            std::move(
                runtime_restore
                    ->inventory);
        starting_village_ =
            std::move(
                runtime_restore
                    ->starting_village);
        starting_village_enabled_ =
            runtime_restore
                ->starting_village_enabled;
        summoned_footmen_ =
            std::move(
                runtime_restore
                    ->summoned_footmen);
        summoned_footman_ship_local_positions_ =
            std::move(
                runtime_restore
                    ->summoned_footman_ship_local_positions);
        summoned_footman_far_seconds_ =
            std::move(
                runtime_restore
                    ->summoned_footman_far_seconds);
        summoned_footman_cast_sequences_ =
            std::move(
                runtime_restore
                    ->summoned_footman_cast_sequences);
        spawn_position_ =
            runtime_restore
                ->spawn_position;
        bound_musket_hotbar_slot_ =
            runtime_restore
                ->bound_musket_hotbar_slot;
        pending_ability_slot_ =
            runtime_restore
                ->pending_ability_slot;
        melee_attack_cooldown_remaining_ =
            runtime_restore
                ->melee_attack_cooldown_remaining;
        wind_acceleration_remaining_ =
            runtime_restore
                ->wind_acceleration_remaining;
        wind_movement_bonus_ =
            runtime_restore
                ->wind_movement_bonus;
        wind_recovery_bonus_ =
            runtime_restore
                ->wind_recovery_bonus;
        wind_dodge_remaining_ =
            runtime_restore
                ->wind_dodge_remaining;
        wind_blade_available_ =
            runtime_restore
                ->wind_blade_available;
        wind_acceleration_cast_sequence_ =
            runtime_restore
                ->wind_acceleration_cast_sequence;
        seconds_since_successful_shield_block_ =
            runtime_restore
                ->seconds_since_successful_shield_block;
        backrooms_elapsed_seconds_ =
            runtime_restore
                ->backrooms_elapsed_seconds;
        backrooms_flashlight_ =
            sanitize_backrooms_flashlight_state(
                runtime_restore
                    ->backrooms_flashlight);
        backrooms_jack_ =
            sanitize_backrooms_jack_state(
                runtime_restore
                    ->backrooms_jack);
        backrooms_jack_runtime_ =
            std::move(
                runtime_restore
                    ->backrooms_jack_runtime);
        backrooms_jack_last_result_ =
            std::move(
                runtime_restore
                    ->backrooms_jack_last_result);
        backrooms_jack_death_delay_seconds_ =
            std::clamp(
                finite_or(
                    runtime_restore
                        ->backrooms_jack_death_delay_seconds,
                    0.0F),
                0.0F,
                kBackroomsJackScreamerHoldSeconds);
        backrooms_jack_death_pending_ =
            runtime_restore
                ->backrooms_jack_death_pending &&
            backrooms_jack_death_delay_seconds_ >
                0.0F &&
            backrooms_jack_.phase ==
                BackroomsJackPhase::Jumpscare;
        backrooms_marlow_ =
            sanitize_backrooms_marlow_state(
                runtime_restore->backrooms_marlow);
        backrooms_marlow_runtime_ = std::move(
            runtime_restore->backrooms_marlow_runtime);
        backrooms_marlow_last_result_ = std::move(
            runtime_restore->backrooms_marlow_last_result);
        backrooms_threat_arbiter_ =
            runtime_restore->backrooms_threat_arbiter;
        backrooms_threat_request_sequence_ =
            runtime_restore->backrooms_threat_request_sequence;
        backrooms_marlow_previous_player_position_ =
            finite_vec3_or(
                runtime_restore->backrooms_marlow_previous_player_position,
                player_.position());
        backrooms_marlow_death_delay_seconds_ = std::clamp(
            finite_or(
                runtime_restore->backrooms_marlow_death_delay_seconds,
                0.0F),
            0.0F,
            kBackroomsMarlowScreamerSeconds);
        backrooms_marlow_death_pending_ =
            runtime_restore->backrooms_marlow_death_pending &&
            backrooms_marlow_death_delay_seconds_ > 0.0F;
        backrooms_marlow_previous_in_water_ =
            runtime_restore->backrooms_marlow_previous_in_water;
        backrooms_marlow_previous_on_ground_ =
            runtime_restore->backrooms_marlow_previous_on_ground;
        backrooms_marlow_previous_jump_input_ =
            runtime_restore->backrooms_marlow_previous_jump_input;
        backrooms_marlow_has_previous_player_position_ =
            runtime_restore->backrooms_marlow_has_previous_player_position;
        super_vision_active_ =
            runtime_restore
                ->super_vision_active;
        colossal_weapon_ =
            std::move(
                runtime_restore->weapon);
        leviathan_knight_synergy_ =
            std::move(
                runtime_restore
                    ->knight_synergy);
        colossal_hit_ledger_ =
            std::move(
                runtime_restore
                    ->hit_ledger);
        previous_colossal_blade_pose_ =
            runtime_restore
                ->previous_blade_pose;
        colossal_world_protections_ =
            std::move(
                runtime_restore
                    ->protections);
        colossus_blood_traces_ =
            std::move(
                runtime_restore
                    ->blood_traces);
        legendary_enemies_ =
            std::move(
                runtime_restore
                    ->enemies);
        sea_leviathan_ =
            std::move(
                runtime_restore
                    ->sea_encounter);
        colossal_wall_impact_sequence_ =
            runtime_restore
                ->wall_impact_sequence;
        colossal_shockwave_sequence_ =
            runtime_restore
                ->shockwave_sequence;
        legendary_quest_guardian_id_ =
            runtime_restore
                ->quest_guardian_id;
        legendary_astral_boss_id_ =
            runtime_restore
                ->astral_boss_id;
        legendary_quest_tutorial_spawned_ =
            runtime_restore
                ->quest_tutorial_spawned;
        colossal_blade_pose_valid_ =
            runtime_restore
                ->blade_pose_valid;
        colossal_weapon_was_selected_ =
            runtime_restore
                ->weapon_was_selected;
        sea_leviathan_started_for_session_ =
            runtime_restore
                ->sea_encounter_started;
        player_ =
            std::move(
                runtime_restore
                    ->player);
        player_musket_ =
            std::move(
                runtime_restore
                    ->player_musket);
        player_musket_effects_ =
            std::move(
                runtime_restore
                    ->player_musket_effects);
    } else {
        reset_backrooms_jack_runtime();
        colossal_weapon_.reset();
        colossal_hit_ledger_.clear();
        colossal_wall_impact_sequence_ = 0U;
        colossal_blade_pose_valid_ = false;
        colossal_weapon_was_selected_ =
            false;
        sea_leviathan_started_for_session_ =
            sea_leviathan_.active();
        rebuild_colossal_world_protections();
    }
    scenario_legendary_runtime_restore_
        .reset();
    active_save_slot_ =
        restore.active_save_slot;
    session_save_state_ =
        restore.save_state;
    has_active_session_ = true;
    gameplay_announcements_.clear();
    if (!runtime_restore.has_value()) {
        super_vision_active_ = false;
        sync_selected_hotbar_slot();
    }
    menu_preview_time_of_day_ =
        environment_.time_of_day();
    preview_orbit_radians_ = 0.0F;
    update_menu_preview_camera(0.0F);
    static_cast<void>(
        world_.update_streaming(
            player_.position()));
    queue_gameplay_announcement(
        "RETOUR",
        "LA PARTIE PRECEDENTE EST RESTAUREE",
        2.8F);
}

void Game::rebuild_colossal_world_protections() noexcept {
    colossal_world_protections_.clear();
    if (issou_scenario_.active()) {
        const auto& bounds =
            issou_scenario_.state()
                .layout.protected_bounds;
        static_cast<void>(
            colossal_world_protections_
                .register_region({
                    kIssouArenaProtectionRegionId,
                    {
                        bounds.min_x,
                        bounds.min_y,
                        bounds.min_z,
                    },
                    {
                        bounds.max_x,
                        bounds.max_y,
                        bounds.max_z,
                    },
                    WorldProtectionFlag::
                        ArenaBoundary |
                        WorldProtectionFlag::
                            ImportantStructure,
                }));
        return;
    }
    if (starting_village_enabled_) {
        static_cast<void>(
            colossal_world_protections_
                .register_region({
                    kStartingVillageProtectionRegionId,
                    {
                        starting_village_.min_x,
                        starting_village_.base_y -
                            4,
                        starting_village_.min_z,
                    },
                    {
                        starting_village_.max_x,
                        starting_village_.base_y +
                            20,
                        starting_village_.max_z,
                    },
                    WorldProtectionFlag::
                        ImportantStructure,
                }));
    }
    if (legendary_quest_world_content_
            .has_value()) {
        const auto& volumes =
            legendary_quest_world_content_
                ->protection_volumes;
        for (std::size_t index = 0U;
             index < volumes.size();
             ++index) {
            if (!legendary_quest_world_scenes_applied_[
                    index]) {
                continue;
            }
            static_cast<void>(
                colossal_world_protections_
                    .register_region(
                        volumes[index].region));
        }
    }
}

void Game::reset_musket_interaction(
    bool forget_bound_slot) noexcept {
    musket_fire_held_ = false;
    pending_musket_fire_press_ = false;
    musket_aim_held_ = false;
    pending_musket_reload_ = false;
    player_musket_.cancel_transient_actions();
    player_musket_effects_.clear_flashes();
    if (forget_bound_slot) {
        bound_musket_hotbar_slot_.reset();
    }
}

auto Game::current_wind_velocity(
    const EnvironmentState& environment) const noexcept -> glm::vec3 {
    const auto direction =
        finite_vec3_or(
            {
                environment.wind_direction_xz.x,
                0.0F,
                environment.wind_direction_xz.y,
            },
            {0.0F, 0.0F, 1.0F});
    const auto strength =
        std::isfinite(environment.wind_strength)
            ? std::clamp(
                  environment.wind_strength,
                  0.0F,
                  1.0F)
            : 0.0F;
    return direction *
           (0.45F + strength * 3.2F);
}

void Game::resolve_player_musket_shot(
    const PlayerMusketEvents& shot,
    bool maritime_session_active) {

    if (!shot.fired ||
        !std::isfinite(shot.maximum_distance) ||
        shot.maximum_distance <= 0.0F ||
        !std::isfinite(shot.damage) ||
        shot.damage < 0.0F) {
        return;
    }

    const auto origin =
        player_.eye_position();
    const auto direction =
        safe_drop_direction(
            shot.shot_direction);
    std::array<MusketHit, 5U> candidates {};
    auto candidate_count =
        std::size_t {0U};
    const auto append_candidate =
        [&](MusketHit candidate) {
            if (candidate_count <
                candidates.size()) {
                candidates[candidate_count++] =
                    candidate;
            }
        };

    const auto world_hit =
        world_.raycast_collidable(
            origin,
            direction,
            shot.maximum_distance);
    if (world_hit.hit) {
        append_candidate({
            MusketHitKind::World,
            origin +
                direction *
                    world_hit.distance,
            world_hit.distance,
            0U,
        });
    }

    if (maritime_session_active) {
        if (const auto ship_hit =
                sea_adventure_.ship_entity()
                    .raycast_collidable_distance(
                        origin,
                        direction,
                        shot.maximum_distance);
            ship_hit.has_value()) {
            append_candidate({
                MusketHitKind::Ship,
                origin +
                    direction * *ship_hit,
                *ship_hit,
                0U,
            });
        }

        const auto guard_hit =
            sea_adventure_.intercept_old_guard(
                origin,
                direction,
                shot.maximum_distance);
        if (guard_hit.hit) {
            append_candidate({
                MusketHitKind::OldGuard,
                guard_hit.position,
                guard_hit.distance,
                guard_hit.guard_id,
            });
        }

        const auto crew_hit =
            sea_adventure_.raycast_crew(
                origin,
                direction,
                shot.maximum_distance);
        if (crew_hit.hit) {
            append_candidate({
                MusketHitKind::Crew,
                crew_hit.position,
                crew_hit.distance,
                crew_hit.member_id,
            });
        }
    }

    const auto creature_hit =
        creatures_.raycast_first_creature(
            origin,
            direction,
            shot.maximum_distance);
    if (creature_hit.hit) {
        append_candidate({
            MusketHitKind::Creature,
            origin +
                direction *
                    creature_hit.distance,
            creature_hit.distance,
            creature_hit.id,
        });
    }

    const auto hit =
        select_nearest_musket_hit(
            std::span<const MusketHit> {
                candidates.data(),
                candidate_count,
            },
            shot.maximum_distance);

    switch (hit.kind) {
    case MusketHitKind::Crew: {
        const auto result =
            sea_adventure_.apply_damage_crew(
                static_cast<std::uint8_t>(
                    hit.target_id),
                shot.damage,
                hit.distance);
        if (result.hit) {
            music_.play_sfx(
                GameSfxKind::CreatureHit,
                0.78F);
            if (result.knocked_out) {
                queue_gameplay_announcement(
                    "EQUIPAGE",
                    "MARIN ASSOMME",
                    2.4F);
            }
            record_audit_event(
                AuditEventCategory::Creatures,
                result.knocked_out
                    ? "ship_crew_knocked_out_by_musket"
                    : "ship_crew_damaged_by_musket",
                result.knocked_out
                    ? AuditSeverity::Warning
                    : AuditSeverity::Info,
                audit_json_object({
                    {
                        "member_id",
                        audit_json_number(
                            result.member_id),
                    },
                    {
                        "damage",
                        audit_json_number(
                            result.damage),
                    },
                    {
                        "remaining_health",
                        audit_json_number(
                            result.remaining_health),
                    },
                }),
                result.knocked_out
                    ? AuditPriority::High
                    : AuditPriority::Normal);
        }
        break;
    }
    case MusketHitKind::Creature: {
        const auto result =
            creatures_.apply_damage(
                static_cast<CreatureId>(
                    hit.target_id),
                shot.damage,
                CreatureDamageSource::Player,
                direction);
        if (result.hit) {
            music_.play_sfx(
                result.killed
                    ? GameSfxKind::CreatureDeath
                    : GameSfxKind::CreatureHit,
                result.killed
                    ? 0.92F
                    : 0.78F);
            if (result.killed) {
                grant_creature_kill_rewards(
                    result,
                    "creature_kill_musket");
            }
            record_audit_event(
                AuditEventCategory::Creatures,
                result.killed
                    ? "creature_killed_by_musket"
                    : "creature_damaged_by_musket",
                result.killed
                    ? AuditSeverity::Warning
                    : AuditSeverity::Info,
                audit_json_object({
                    {
                        "species",
                        audit_json_number(
                            static_cast<int>(
                                result.species)),
                    },
                    {
                        "damage",
                        audit_json_number(
                            result.damage),
                    },
                    {
                        "remaining_health",
                        audit_json_number(
                            result.remaining_health),
                    },
                }),
                result.killed
                    ? AuditPriority::High
                    : AuditPriority::Normal);
        }
        break;
    }
    case MusketHitKind::OldGuard:
        // Je garde la Vieille Garde invulnerable, mais son volume arrete
        // reellement la balle et protege tout ce qui se trouve derriere.
        music_.play_sfx(
            GameSfxKind::CreatureHit,
            0.48F);
        record_audit_event(
            AuditEventCategory::Creatures,
            "old_guard_intercepted_player_musket",
            AuditSeverity::Info,
            audit_json_object({
                {
                    "guard_id",
                    audit_json_number(
                        hit.target_id),
                },
                {
                    "distance",
                    audit_json_number(
                        hit.distance),
                },
            }),
            AuditPriority::Normal);
        break;
    case MusketHitKind::World:
    case MusketHitKind::Ship:
    case MusketHitKind::None:
    default:
        break;
    }

    record_audit_event(
        AuditEventCategory::InputAction,
        "player_musket_fired",
        AuditSeverity::Info,
        audit_json_object({
            {
                "sequence",
                audit_json_number(
                    shot.shot_sequence),
            },
            {
                "hit_kind",
                audit_json_number(
                    static_cast<int>(
                        hit.kind)),
            },
            {
                "distance",
                audit_json_number(
                    hit.hit()
                        ? hit.distance
                        : shot.maximum_distance),
            },
            {
                "damage",
                audit_json_number(
                    shot.damage),
            },
        }),
        AuditPriority::High);
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

    const auto level = progression_.level();
    const auto robustness =
        player_attribute_value(
            player_build_.attributes,
            PlayerAttribute::Robustness);
    const auto agility =
        player_attribute_value(
            player_build_.attributes,
            PlayerAttribute::Agility);
    const auto maximum_health =
        player_base_max_health(level) +
        static_cast<float>(robustness);
    player_.set_max_health(
        maximum_health);
    const auto temporary_effects =
        player_ability_effects_.aggregate(
            maximum_health);

    const auto armor_resistance =
        std::clamp(
            inventory_equipment_resistance_percent(
                inventory_menu_,
                colossal_weapon_drawn()) /
                100.0F,
            0.0F,
            0.99F);
    const auto combined_resistance =
        1.0F -
        (1.0F - armor_resistance) *
            (1.0F -
             player_level_damage_reduction(
                 level)) *
            (1.0F -
             player_robustness_damage_reduction(
                 robustness)) *
            (1.0F -
             temporary_effects
                 .damage_reduction);
    player_.set_damage_resistance_percent(
        std::clamp(
            combined_resistance,
            0.0F,
            0.80F) *
        100.0F);

    const auto apnea_duration_multiplier =
        player_total_apnea_duration_multiplier(
            level,
            robustness);
    player_.set_apnea_resistance_percent(
        (1.0F -
         1.0F /
             std::max(
                 apnea_duration_multiplier,
                 1.0F)) *
        100.0F);

    player_.set_fall_safety_multiplier(
        player_level_safe_fall_multiplier(
            level));

    player_.set_movement_speed_multiplier(
        player_total_movement_speed_multiplier(
            level,
            agility) *
        std::clamp(
            colossal_weapon_.snapshot()
                .movement_multiplier,
            0.0F,
            1.0F) *
        std::clamp(
            (1.0F +
             std::max(
                 wind_movement_bonus_,
                 0.0F)) *
                temporary_effects
                    .movement_speed_multiplier(),
            0.0F,
            1.0F +
                kMaximumTemporaryMovementSpeedBonus));

    player_.set_block_break_speed_multiplier(
        player_level_mining_speed_multiplier(
            level));

    if (selected_musket_active()) {
        const auto selected_index =
            normalize_hotbar_index(
                hotbar_.selected_index);
        if (!bound_musket_hotbar_slot_.has_value() ||
            *bound_musket_hotbar_slot_ !=
                selected_index) {
            player_musket_.cancel_transient_actions();
            player_musket_.synchronize_chamber(
                is_musket_loaded(
                    hotbar_.slots[selected_index]));
            bound_musket_hotbar_slot_ =
                selected_index;
        }
    } else if (bound_musket_hotbar_slot_.has_value()) {
        reset_musket_interaction();
    }
}

auto Game::selected_tool_break_speed_multiplier(BlockId target_block_id) const noexcept -> float {
    const auto& selected_slot = hotbar_.selected_slot();
    if (!inventory_slot_has_item(selected_slot)) {
        return 1.0F;
    }
    return tool_break_speed_multiplier(selected_slot.block_id, target_block_id);
}

void Game::select_hotbar_slot(std::size_t index) noexcept {
    if (!colossal_weapon_.snapshot()
             .can_change_equipment) {
        return;
    }
    const auto previous_index =
        normalize_hotbar_index(
            hotbar_.selected_index);
    valcraft::select_hotbar_index(hotbar_, index);
    if (previous_index !=
        normalize_hotbar_index(
            hotbar_.selected_index)) {
        reset_musket_interaction();
    }
    sync_selected_hotbar_slot();
}

void Game::cycle_hotbar_selection(int delta) noexcept {
    if (!colossal_weapon_.snapshot()
             .can_change_equipment) {
        return;
    }
    const auto previous_index =
        normalize_hotbar_index(
            hotbar_.selected_index);
    valcraft::cycle_hotbar_selection(hotbar_, delta);
    if (previous_index !=
        normalize_hotbar_index(
            hotbar_.selected_index)) {
        reset_musket_interaction();
    }
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
    const auto backrooms_respawn =
        active_game_mode_ == GameMode::Backrooms;

    if (maritime_respawn) {
        // Le voyage continue, mais les jauges transitoires doivent laisser au
        // joueur une fenetre reelle pour reprendre le controle apres sa mort.
        sea_adventure_.on_player_respawn();
    }

    spawn_position_ =
        maritime_respawn
            ? sea_adventure_.deck_spawn_position()
            : backrooms_respawn
                  ? backrooms_spawn_position(
                        world_,
                        world_.backrooms_level())
                  : find_initial_spawn_position();

    if (backrooms_respawn) {
        begin_loading_screen(
            LoadingScreenTheme::Standard,
            static_cast<std::uint32_t>(
                world_.seed()) ^
                static_cast<std::uint32_t>(
                    world_.backrooms_level()));
        update_loading_screen(
            "BACKROOMS",
            "RECOMPOSITION DU POINT DE REVEIL",
            LoadingPhase::Preparation,
            0.15F,
            true);
        if (!reset_renderer_world_resources_during_loading(
                "BACKROOMS")) {
            loading_active_ = false;
            return;
        }
    }

    player_.respawn(spawn_position_);
    player_.set_fly_mode_enabled(false);
    player_.set_water_movement_profile(
        backrooms_respawn &&
                world_.backrooms_theme_at_y(
                    spawn_position_.y) ==
                    BackroomsTheme::Poolrooms
            ? PlayerWaterMovementProfile::Poolrooms
            : PlayerWaterMovementProfile::Standard);
    const auto energy_parameters =
        player_ability_energy_parameters(
            player_build_);
    player_build_.val_energy =
        energy_parameters.maximum_energy *
        0.50F;
    player_ability_effects_.clear();
    wind_acceleration_remaining_ = 0.0F;
    wind_movement_bonus_ = 0.0F;
    wind_recovery_bonus_ = 0.0F;
    wind_dodge_remaining_ = 0.0F;
    wind_blade_available_ = false;
    wind_acceleration_cast_sequence_ = 0U;
    reset_musket_interaction();
    sync_selected_hotbar_slot();
    set_death_screen_visible(false);

    if (backrooms_respawn) {
        reset_backrooms_jack_runtime();
        creatures_.clear();
        item_drops_.clear();
        // Je reconstruis et transfère tout l'anneau visible sous le masque de
        // chargement : aucune salle du point d'origine ne réapparaît en jeu.
        prime_world_around(
            world_,
            player_.position(),
            "BACKROOMS",
            "ASSEMBLAGE DU POINT DE REVEIL HORS DE VUE");
        if (running_) {
            complete_loading_screen(
                "BACKROOMS",
                "VOUS VOUS REVEILLEZ AILLEURS");
            SDL_SetWindowTitle(
                window_,
                kGameWindowTitle.data());
        }
    } else {
        (void)world_.update_streaming(
            player_.position());
        creatures_.update(
            0.0F,
            world_,
            player_.position(),
            environment_.current_state(),
            environment_.current_creature_cycle());
    }

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
    if (progression_menu_.visible()) {
        return UiScreen::Progression;
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
           progression_menu_.visible() ||
           command_console_.visible() ||
           confirm_dialog_.visible ||
           front_end_visible();
}

auto Game::backrooms_active() const noexcept -> bool {
    return has_active_session_ &&
           active_game_mode_ == GameMode::Backrooms;
}

auto Game::current_backrooms_level() const noexcept -> int {
    // Je dérive l'étage du déplacement vertical réel : le niveau du générateur
    // reste uniquement l'ancre logique placée sur le plancher historique Y=40.
    return world_.backrooms_level_at_y(
        player_.position().y);
}

auto Game::session_backrooms_supports_jack() const noexcept
    -> bool {
    // Je fais vivre Jack sur l'etage actuellement occupe. Sa FSM detecte un
    // changement logique une seule fois et invalide alors sa grille 2D.
    return backrooms_active();
}

auto Game::backrooms_jack_jumpscare_active() const noexcept
    -> bool {
    return session_backrooms_supports_jack() &&
           backrooms_jack_.active &&
           backrooms_jack_.phase ==
               BackroomsJackPhase::Jumpscare;
}

auto Game::backrooms_marlow_cinematic_active() const noexcept
    -> bool {
    return backrooms_active() &&
           (backrooms_marlow_last_result_.capture.lock_player_controls ||
            backrooms_marlow_runtime_.phase ==
                BackroomsMarlowPhase::Screamer ||
            backrooms_marlow_death_pending_);
}

void Game::reset_backrooms_jack_runtime() noexcept {
    // Je crée ici l'état neuf des nouvelles sessions et des réapparitions.
    // Lors d'un chargement v17, l'état durable sauvegardé est réappliqué après
    // cette remise à zéro, tandis que ce runtime et le screamer restent neufs.
    const auto logical_level =
        world_.generation_profile() ==
                WorldGenerationProfile::Backrooms
            ? current_backrooms_level()
            : world_.backrooms_level();
    const auto seed =
        static_cast<std::uint32_t>(
            world_.seed()) ^
        (static_cast<std::uint32_t>(
             logical_level) *
         UINT32_C(0x9E3779B9)) ^
        UINT32_C(0x4A41434B);
    backrooms_jack_ =
        initialize_backrooms_jack(
            seed,
            logical_level);
    translate_backrooms_jack_state_y(
        backrooms_jack_,
        backrooms_runtime_anchor_y_offset(
            world_.seed(),
            world_.backrooms_level(),
            logical_level,
            world_.generation_version()));
    backrooms_jack_runtime_ = {};
    backrooms_jack_last_result_ = {};
    backrooms_jack_death_delay_seconds_ =
        0.0F;
    backrooms_jack_death_pending_ = false;
    backrooms_marlow_ = initialize_backrooms_marlow(
        seed ^ UINT32_C(0x4D524C57),
        logical_level);
    backrooms_marlow_runtime_ = {};
    backrooms_marlow_last_result_ = {};
    backrooms_threat_arbiter_ = {};
    backrooms_threat_request_sequence_ = 0U;
    backrooms_threat_arbiter_.random_state =
        seed ^ UINT32_C(0x41524254);
    if (backrooms_threat_arbiter_.random_state == 0U) {
        backrooms_threat_arbiter_.random_state = UINT32_C(0x41524254);
    }
    backrooms_marlow_previous_player_position_ = {};
    backrooms_marlow_death_delay_seconds_ = 0.0F;
    backrooms_marlow_death_pending_ = false;
    backrooms_marlow_previous_in_water_ = false;
    backrooms_marlow_previous_on_ground_ = true;
    backrooms_marlow_previous_jump_input_ = false;
    backrooms_marlow_has_previous_player_position_ = false;
    music_.set_backrooms_drowning_filter(0.0F);
}

auto Game::current_environment_state() const -> EnvironmentState {
    if (!backrooms_active()) {
        return environment_.current_state();
    }

    const auto& position = player_.position();
    return make_backrooms_environment_state(
        backrooms_elapsed_seconds_,
        world_.seed(),
        position.x,
        position.z,
        world_.backrooms_theme_at_y(position.y) ==
            BackroomsTheme::Poolrooms);
}

void Game::update_backrooms_simulation(float dt) {
    // Le mode n'expose que la locomotion. Je purge chaque requête discrète
    // afin qu'un clic ou une touche pressée juste avant le chargement ne puisse
    // ni casser une cloison, ni activer un pouvoir dans l'étage.
    pending_toggle_fly_ = false;
    pending_break_block_ = false;
    pending_primary_attack_ = false;
    pending_place_block_ = false;
    pending_fishing_ = false;
    pending_musket_fire_press_ = false;
    pending_musket_reload_ = false;
    pending_ability_slot_.reset();
    musket_fire_held_ = false;
    musket_aim_held_ = false;
    player_.cancel_block_breaking();
    player_.set_fly_mode_enabled(false);
    super_vision_active_ = false;

    const auto safe_dt =
        std::isfinite(dt)
            ? std::clamp(dt, 0.0F, 0.10F)
            : 0.0F;
    player_.set_water_movement_profile(
        world_.backrooms_theme_at_y(
            player_.position().y) ==
                BackroomsTheme::Poolrooms
            ? PlayerWaterMovementProfile::Poolrooms
            : PlayerWaterMovementProfile::Standard);
    static_cast<void>(
        update_backrooms_flashlight(
            backrooms_flashlight_,
            safe_dt));
    if (!std::isfinite(backrooms_elapsed_seconds_) ||
        backrooms_elapsed_seconds_ < 0.0F) {
        backrooms_elapsed_seconds_ = 0.0F;
    }
    // Le repli évite de perdre la précision des flottants après de très
    // longues parties, sans produire de rupture visible dans les cycles lents.
    constexpr auto kBackroomsClockPeriodSeconds = 86'400.0F;
    backrooms_elapsed_seconds_ =
        std::fmod(
            backrooms_elapsed_seconds_ + safe_dt,
            kBackroomsClockPeriodSeconds);

    const auto gameplay_input_enabled =
        !options_.smoke_test &&
        !gameplay_interaction_blocked() &&
        !backrooms_jack_jumpscare_active() &&
        !backrooms_marlow_cinematic_active();
    PlayerInput input {};
    if (gameplay_input_enabled) {
        input = read_player_movement_input(
            SDL_GetKeyboardState(nullptr));
    }
    input.toggle_fly = false;
    input.look_delta_x =
        gameplay_input_enabled &&
                mouse_captured_
            ? std::exchange(pending_look_x_, 0.0F)
            : 0.0F;
    input.look_delta_y =
        gameplay_input_enabled &&
                mouse_captured_
            ? std::exchange(pending_look_y_, 0.0F)
            : 0.0F;

    if (backrooms_jack_jumpscare_active() ||
        backrooms_marlow_cinematic_active()) {
        // Je consomme aussi le regard accumulé avant la capture. Il ne sera
        // pas réappliqué brutalement après le screamer ou au respawn.
        pending_look_x_ = 0.0F;
        pending_look_y_ = 0.0F;
    } else {
        player_.update(
            input,
            safe_dt,
            world_,
            nullptr,
            nullptr);
        // Je recalcule le profil après la physique : franchir une pente jusque
        // dans les Poolrooms adapte l'eau sans attendre la frame suivante.
        player_.set_water_movement_profile(
            world_.backrooms_theme_at_y(
                player_.position().y) ==
                    BackroomsTheme::Poolrooms
                ? PlayerWaterMovementProfile::Poolrooms
                : PlayerWaterMovementProfile::Standard);
    }

    const auto smoke_pose =
        options_.smoke_test
            ? requested_backrooms_jack_smoke_pose(
                  options_.smoke_backrooms_jack)
            : std::nullopt;
    const auto jack_logical_level =
        current_backrooms_level();
    const auto jack_vertical_offset =
        backrooms_runtime_anchor_y_offset(
            world_.seed(),
            world_.backrooms_level(),
            jack_logical_level,
            world_.generation_version());
    update_backrooms_threat_arbiter(
        backrooms_threat_arbiter_,
        safe_dt);
    const auto queue_threat_request =
        [this](BackroomsThreatOwner threat) {
            const auto already_pending =
                threat == BackroomsThreatOwner::Jack
                    ? backrooms_threat_arbiter_
                          .pending_jack_arrival !=
                          kBackroomsThreatNoArrival
                    : backrooms_threat_arbiter_
                          .pending_marlow_arrival !=
                          kBackroomsThreatNoArrival;
            if (threat == BackroomsThreatOwner::None ||
                already_pending ||
                backrooms_threat_arbiter_.owner == threat) {
                return;
            }
            ++backrooms_threat_request_sequence_;
            if (backrooms_threat_request_sequence_ == 0U) {
                ++backrooms_threat_request_sequence_;
            }
            request_backrooms_threat(
                backrooms_threat_arbiter_,
                threat,
                backrooms_threat_request_sequence_);
        };
    if (backrooms_jack_runtime_.pending_reveal) {
        queue_threat_request(BackroomsThreatOwner::Jack);
    }
    if (backrooms_marlow_runtime_.waiting_for_threat_slot) {
        queue_threat_request(BackroomsThreatOwner::Marlow);
    }
    static_cast<void>(
        resolve_backrooms_threat(
            backrooms_threat_arbiter_));
    auto caught_this_update = false;
    if (!session_backrooms_supports_jack()) {
        reset_backrooms_jack_runtime();
    } else if (smoke_pose.has_value()) {
        auto preview =
            make_backrooms_jack_smoke_preview(
                *smoke_pose,
                static_cast<std::uint32_t>(
                    world_.seed()) ^
                    UINT32_C(0x4A41434B));
        translate_backrooms_jack_state_y(
            preview.state,
            jack_vertical_offset);
        preview.render.position.y += jack_vertical_offset;
        preview.light_interference.position.y +=
            jack_vertical_offset;
        preview.state.logical_level =
            jack_logical_level;
        if (*smoke_pose !=
            BackroomsJackSmokePose::Jumpscare) {
            const auto forward =
                safe_horizontal_direction(
                    player_.look_direction());
            auto preview_direction = forward;
            auto preview_distance = 8.0F;
            if (options_.smoke_backrooms_jack ==
                BackroomsJackSmokeMode::CorridorStare) {
                preview_distance =
                    options_.smoke_backrooms_jack_distance;
            } else if (
                options_.smoke_backrooms_jack ==
                BackroomsJackSmokeMode::RearStare) {
                // Je cadre le moment juste apres le retournement : la camera
                // regarde desormais l'ancien arriere, ou Jack se tenait a 24 m.
                const auto pre_turn_yaw = glm::radians(
                    backrooms_smoke_camera_yaw_degrees_);
                const glm::vec3 pre_turn_forward {
                    std::cos(pre_turn_yaw),
                    0.0F,
                    std::sin(pre_turn_yaw),
                };
                preview_direction = -pre_turn_forward;
                preview_distance = 24.0F;
            }
            preview.state.position =
                player_.position() +
                preview_direction * preview_distance;
            preview.state.position.y =
                player_.position().y;
            preview.state.body_yaw_degrees =
                backrooms_jack_yaw_facing(
                    preview.state.position,
                    player_.position());
            preview.state =
                sanitize_backrooms_jack_state(
                    preview.state);
            preview.render =
                make_backrooms_jack_render_view(
                    preview.state,
                    jack_logical_level);
            preview.light_interference =
                make_backrooms_jack_light_interference_view(
                    preview.state,
                    jack_logical_level);
        }
        preview.state =
            sanitize_backrooms_jack_state(
                preview.state);
        preview.render =
            make_backrooms_jack_render_view(
                preview.state,
                jack_logical_level);
        preview.light_interference =
            make_backrooms_jack_light_interference_view(
                preview.state,
                jack_logical_level);
        backrooms_jack_ = preview.state;
        backrooms_jack_runtime_ = {};
        if (options_.smoke_backrooms_jack ==
            BackroomsJackSmokeMode::CorridorStare) {
            backrooms_jack_runtime_.encounter_mode =
                BackroomsJackEncounterMode::CorridorStare;
        } else if (
            options_.smoke_backrooms_jack ==
            BackroomsJackSmokeMode::RearStare) {
            backrooms_jack_runtime_.encounter_mode =
                BackroomsJackEncounterMode::RearStare;
        }
        backrooms_jack_last_result_ = {
            .render = preview.render,
            .light_interference =
                preview.light_interference,
        };
        backrooms_jack_death_delay_seconds_ =
            0.0F;
        backrooms_jack_death_pending_ = false;
    } else {
        const BackroomsGenerator generator {
            world_.seed(),
            jack_logical_level,
            kBackroomsSpatialConnectorDistrictModules,
            backrooms_runtime_pool_geometry_profile(
                world_.generation_version()),
        };
        auto simulated_jack = backrooms_jack_;
        translate_backrooms_jack_state_y(
            simulated_jack,
            -jack_vertical_offset);
        auto local_player_feet = player_.position();
        local_player_feet.y -= jack_vertical_offset;
        auto local_player_eye = player_.eye_position();
        local_player_eye.y -= jack_vertical_offset;
        const auto ui_blocks_simulation =
            gameplay_interaction_blocked();
        const BackroomsJackUpdateContext context {
            .player = {
                .feet_position =
                    local_player_feet,
                .eye_position =
                    local_player_eye,
                .look_direction =
                    player_.look_direction(),
                .maximum_sprint_speed = 7.2F,
            },
            .chunk_readiness =
                backrooms_jack_chunk_readiness(
                    world_,
                    renderer_,
                    player_.position()),
            .allow_spawn =
                !ui_blocks_simulation &&
                !player_.is_dead() &&
                backrooms_threat_arbiter_.owner !=
                    BackroomsThreatOwner::Marlow &&
                backrooms_threat_arbiter_.grace_seconds <= 0.0F,
            .simulation_frozen =
                ui_blocks_simulation ||
                backrooms_jack_jumpscare_active(),
            .player_alive =
                !player_.is_dead(),
            .spatial_world = &world_,
            .spatial_world_y_offset =
                jack_vertical_offset,
            .maximum_visible_distance =
                backrooms_jack_maximum_visible_distance(
                    renderer_.backrooms_terminal_fog_snapshot(),
                    world_.seed(),
                    jack_logical_level),
        };
        backrooms_jack_last_result_ =
            update_backrooms_jack(
                simulated_jack,
                backrooms_jack_runtime_,
                generator,
                context,
                safe_dt);
        backrooms_jack_ = simulated_jack;
        translate_backrooms_jack_state_y(
            backrooms_jack_,
            jack_vertical_offset);
        translate_backrooms_jack_result_y(
            backrooms_jack_last_result_,
            jack_vertical_offset);

        if (backrooms_jack_runtime_.pending_reveal) {
            queue_threat_request(
                BackroomsThreatOwner::Jack);
            static_cast<void>(
                resolve_backrooms_threat(
                    backrooms_threat_arbiter_));
        }

        const auto event_count =
            std::min(
                backrooms_jack_last_result_
                    .event_count,
                backrooms_jack_last_result_
                    .events.size());
        for (std::size_t index = 0U;
             index < event_count;
             ++index) {
            const auto& event =
                backrooms_jack_last_result_
                    .events[index];
            const auto kind =
                backrooms_jack_event_sfx(
                    event.kind);
            if (!kind.has_value()) {
                continue;
            }
            const auto spatial =
                backrooms_jack_spatial_audio(
                    player_.eye_position(),
                    player_.look_direction(),
                    event.position);
            auto deterministic_seed =
                static_cast<std::uint32_t>(
                    event.sequence) ^
                static_cast<std::uint32_t>(
                    event.sequence >> 32U) ^
                static_cast<std::uint32_t>(
                    world_.seed());
            if (deterministic_seed == 0U) {
                deterministic_seed =
                    UINT32_C(0x4A41434B);
            }
            music_.play_sfx(
                *kind,
                backrooms_jack_event_volume(
                    event.kind),
                spatial.pan,
                spatial.attenuation,
                deterministic_seed);
        }

        caught_this_update =
            backrooms_jack_last_result_
                .caught_player;
        if (caught_this_update &&
            !backrooms_jack_death_pending_) {
            player_.force_death(
                PlayerDeathCause::
                    JackThePirate);
            backrooms_jack_death_delay_seconds_ =
                kBackroomsJackScreamerHoldSeconds;
            backrooms_jack_death_pending_ = true;
            pending_look_x_ = 0.0F;
            pending_look_y_ = 0.0F;
        }
    }

    const auto jack_holds_threat =
        backrooms_jack_.active ||
        backrooms_jack_runtime_.pending_reveal ||
        backrooms_jack_last_result_.render.jumpscare;
    if (!jack_holds_threat &&
        backrooms_threat_arbiter_.owner ==
            BackroomsThreatOwner::Jack &&
        !smoke_pose.has_value()) {
        release_backrooms_threat(
            backrooms_threat_arbiter_,
            BackroomsThreatOwner::Jack);
    }

    const auto marlow_poolrooms_active =
        !options_.smoke_test &&
        session_backrooms_supports_jack() &&
        world_.backrooms_theme_at_y(player_.position().y) ==
            BackroomsTheme::Poolrooms;
    if (marlow_poolrooms_active) {
        const BackroomsGenerator marlow_generator {
            world_.seed(),
            jack_logical_level,
            kBackroomsSpatialConnectorDistrictModules,
            backrooms_runtime_pool_geometry_profile(
                world_.generation_version()),
        };
        auto local_player_feet = player_.position();
        local_player_feet.y -= jack_vertical_offset;
        auto local_player_eye = player_.eye_position();
        local_player_eye.y -= jack_vertical_offset;
        const auto column_x = static_cast<int>(
            std::floor(local_player_feet.x));
        const auto column_z = static_cast<int>(
            std::floor(local_player_feet.z));
        const auto player_column =
            marlow_generator.sample_column(
                column_x,
                column_z);
        const auto in_water =
            player_column.water_state != WaterState {0} &&
            local_player_feet.y <=
                static_cast<float>(
                    player_column.water_top_y + 1);
        auto travelled_distance = 0.0F;
        if (backrooms_marlow_has_previous_player_position_) {
            auto travelled = player_.position() -
                backrooms_marlow_previous_player_position_;
            travelled.y = 0.0F;
            const auto distance_squared =
                glm::dot(travelled, travelled);
            if (std::isfinite(distance_squared) &&
                distance_squared > 0.0F) {
                travelled_distance = std::sqrt(distance_squared);
            }
        }
        const auto jumped =
            input.jump &&
            !backrooms_marlow_previous_jump_input_ &&
            (backrooms_marlow_previous_on_ground_ ||
             backrooms_marlow_previous_in_water_) &&
            player_.state().velocity.y > 0.10F;
        const auto landed_in_water =
            in_water &&
            player_.state().on_ground &&
            !backrooms_marlow_previous_on_ground_;
        const auto flashlight_on_water =
            backrooms_flashlight_hits_water(
                world_,
                player_.eye_position(),
                player_.look_direction(),
                backrooms_flashlight_);
        const auto ui_blocks_simulation =
            gameplay_interaction_blocked();
        const auto threat_slot_available =
            backrooms_threat_arbiter_.owner ==
                BackroomsThreatOwner::None &&
            backrooms_threat_arbiter_.grace_seconds <= 0.0F;
        const auto threat_slot_owned =
            backrooms_threat_arbiter_.owner ==
                BackroomsThreatOwner::Marlow;
        const BackroomsMarlowUpdateContext context {
            .player = {
                .feet_position = local_player_feet,
                .eye_position = local_player_eye,
                .look_direction = player_.look_direction(),
                .travelled_horizontal_distance = travelled_distance,
                .water_depth = static_cast<float>(
                    player_column.water_depth_cells),
                .sprinting =
                    input.sprint &&
                    (std::abs(input.move_forward) > 0.01F ||
                     std::abs(input.move_right) > 0.01F),
                .in_water = in_water,
                .entered_water =
                    in_water &&
                    !backrooms_marlow_previous_in_water_,
                .jumped = jumped,
                .landed_in_water = landed_in_water,
                .flashlight_on_water = flashlight_on_water,
            },
            .chunk_readiness =
                backrooms_marlow_chunk_readiness(
                    world_,
                    renderer_,
                    player_.position()),
            .allow_manifestation =
                !ui_blocks_simulation &&
                !player_.is_dead(),
            .allow_capture =
                !ui_blocks_simulation &&
                !player_.is_dead(),
            .threat_slot_available =
                threat_slot_available,
            .threat_slot_owned =
                threat_slot_owned,
            .simulation_frozen =
                ui_blocks_simulation ||
                backrooms_jack_jumpscare_active(),
            .player_alive = !player_.is_dead(),
            .spatial_world = &world_,
            .spatial_world_y_offset =
                jack_vertical_offset,
        };
        backrooms_marlow_last_result_ =
            update_backrooms_marlow(
                backrooms_marlow_,
                backrooms_marlow_runtime_,
                marlow_generator,
                context,
                safe_dt);
        translate_backrooms_marlow_result_y(
            backrooms_marlow_last_result_,
            static_cast<float>(jack_vertical_offset));

        if (backrooms_marlow_last_result_.requests_threat_slot) {
            queue_threat_request(
                BackroomsThreatOwner::Marlow);
            static_cast<void>(
                resolve_backrooms_threat(
                    backrooms_threat_arbiter_));
        }
        if (backrooms_marlow_last_result_.cancels_threat_request) {
            cancel_backrooms_threat_request(
                backrooms_threat_arbiter_,
                BackroomsThreatOwner::Marlow);
        }
        if (backrooms_marlow_last_result_.releases_threat_slot) {
            release_backrooms_threat(
                backrooms_threat_arbiter_,
                BackroomsThreatOwner::Marlow);
        }

        const auto marlow_event_count = std::min(
            backrooms_marlow_last_result_.event_count,
            backrooms_marlow_last_result_.events.size());
        for (std::size_t index = 0U;
             index < marlow_event_count;
             ++index) {
            const auto& event =
                backrooms_marlow_last_result_.events[index];
            const auto kind =
                backrooms_marlow_event_sfx(event.kind);
            if (!kind.has_value()) {
                continue;
            }
            const auto spatial = backrooms_jack_spatial_audio(
                player_.eye_position(),
                player_.look_direction(),
                event.position);
            auto deterministic_seed =
                static_cast<std::uint32_t>(event.sequence) ^
                static_cast<std::uint32_t>(event.sequence >> 32U) ^
                static_cast<std::uint32_t>(world_.seed()) ^
                UINT32_C(0x4D524C57);
            if (deterministic_seed == 0U) {
                deterministic_seed = UINT32_C(0x4D524C57);
            }
            music_.play_sfx(
                *kind,
                backrooms_marlow_event_volume(event.kind),
                spatial.pan,
                spatial.attenuation,
                deterministic_seed);
        }

        if (backrooms_marlow_last_result_.capture.active) {
            const auto current = player_.position();
            const auto target = finite_vec3_or(
                backrooms_marlow_last_result_.capture.water_target,
                current);
            const auto drag_speed =
                5.5F +
                backrooms_marlow_last_result_.capture.drag_amount * 10.5F;
            const auto blend = std::clamp(
                safe_dt * drag_speed,
                0.0F,
                1.0F);
            player_.set_position(
                current + (target - current) * blend);
            player_.set_velocity(glm::vec3 {0.0F});
            pending_look_x_ = 0.0F;
            pending_look_y_ = 0.0F;
        }
        music_.set_backrooms_drowning_filter(
            backrooms_marlow_last_result_
                .capture.drowning_amount);

        if (backrooms_marlow_last_result_.kill_player &&
            !backrooms_marlow_death_pending_) {
            player_.force_death(
                PlayerDeathCause::MarlowTheDrowned);
            backrooms_marlow_death_delay_seconds_ =
                kBackroomsMarlowScreamerSeconds;
            backrooms_marlow_death_pending_ = true;
            pending_look_x_ = 0.0F;
            pending_look_y_ = 0.0F;
        }

        backrooms_marlow_previous_player_position_ =
            player_.position();
        backrooms_marlow_has_previous_player_position_ = true;
        backrooms_marlow_previous_in_water_ = in_water;
        backrooms_marlow_previous_on_ground_ =
            player_.state().on_ground;
        backrooms_marlow_previous_jump_input_ = input.jump;
    } else {
        backrooms_marlow_last_result_ = {};
        // Je retire toujours le corps en quittant les Poolrooms. Le directeur
        // durable garde sa pression, mais aucune phase de capture ne peut
        // reprendre au retour sur cet etage.
        backrooms_marlow_runtime_ = {};
        backrooms_marlow_.logical_level = jack_logical_level;
        backrooms_marlow_.cooldown_seconds = std::max(
            backrooms_marlow_.cooldown_seconds,
            kBackroomsMarlowInitialGraceSeconds);
        music_.set_backrooms_drowning_filter(0.0F);
        if (backrooms_threat_arbiter_.owner ==
            BackroomsThreatOwner::Marlow) {
            release_backrooms_threat(
                backrooms_threat_arbiter_,
                BackroomsThreatOwner::Marlow);
        }
        cancel_backrooms_threat_request(
            backrooms_threat_arbiter_,
            BackroomsThreatOwner::Marlow);
    }

    if (backrooms_jack_death_pending_) {
        if (!caught_this_update) {
            backrooms_jack_death_delay_seconds_ =
                std::max(
                    backrooms_jack_death_delay_seconds_ -
                        safe_dt,
                    0.0F);
        }
        if (backrooms_jack_death_delay_seconds_ <=
            0.0F) {
            // Je retire la surimpression avant d'ouvrir l'écran de mort :
            // les deux interfaces ne doivent jamais se superposer.
            reset_backrooms_jack_runtime();
            set_death_screen_visible(
                true,
                PlayerDeathCause::
                    JackThePirate);
        }
    } else if (backrooms_marlow_death_pending_) {
        backrooms_marlow_death_delay_seconds_ = std::max(
            backrooms_marlow_death_delay_seconds_ - safe_dt,
            0.0F);
        if (backrooms_marlow_death_delay_seconds_ <= 0.0F) {
            // Je laisse les 0,85 s du visage noyé se terminer avant d'afficher
            // la cause de mort, puis je purge le filtre audio subaquatique.
            reset_backrooms_jack_runtime();
            set_death_screen_visible(
                true,
                PlayerDeathCause::MarlowTheDrowned);
        }
    } else if (player_.is_dead() &&
               !death_screen_visible_) {
        const auto cause =
            player_.state().death_cause;
        reset_backrooms_jack_runtime();
        set_death_screen_visible(
            true,
            cause);
    }
    if (has_active_session_) {
        mark_session_dirty();
    }
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
    const auto& focus_player =
        options_.smoke_test &&
            options_.smoke_ship_view != SmokeShipView::None &&
            active_game_mode_ == GameMode::SeaAdventure &&
            sea_adventure_.active()
        ? preview_player_
        : (!options_.smoke_test && front_end_visible()
               ? preview_player_
               : player_);
    const auto base_focus =
        focus_player.position();
    if (active_game_mode_ == GameMode::SeaAdventure &&
        sea_adventure_.active()) {
        const auto& ship =
            sea_adventure_.ship_entity();
        const auto player_on_ship =
            sea_adventure_.hud_state(
                focus_player)
                .on_ship;
        // J'anticipe exactement un chunk dans le sens du navire uniquement
        // lorsqu'il porte ce joueur ; à terre, je charge autour du joueur.
        return resolve_sea_adventure_streaming_focus(
            base_focus,
            player_on_ship,
            ship.position(),
            ship.velocity());
    }
    return base_focus;
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
    const auto backrooms_mode =
        active_game_mode_ == GameMode::Backrooms;
    const auto save_active_ship =
        active_game_mode_ == GameMode::SeaAdventure &&
        sea_adventure_.active();
    const auto saved_backrooms_level =
        backrooms_mode
            ? current_backrooms_level()
            : 0;
    const auto saved_backrooms_y_offset =
        backrooms_mode
              ? backrooms_runtime_anchor_y_offset(
                  world_.seed(),
                  world_.backrooms_level(),
                  saved_backrooms_level,
                  world_.generation_version())
            : 0;

    snapshot.metadata.exists = true;
    snapshot.metadata.seed = world_.seed();
    snapshot.metadata.time_of_day =
        backrooms_mode ? 0.0F : environment_.time_of_day();
    // Le temps d'ambiance BackRooms est conservé dans le champ météo existant.
    // Le format reste compact et compatible sans créer un second compteur.
    snapshot.metadata.weather_time_seconds =
        backrooms_mode
            ? backrooms_elapsed_seconds_
            : environment_.weather_time_seconds();
    snapshot.metadata.has_starting_village =
        !backrooms_mode && starting_village_enabled_;
    snapshot.metadata.game_mode = active_game_mode_;
    snapshot.backrooms_level =
        saved_backrooms_level;
    snapshot.backrooms_flashlight =
        backrooms_mode
            ? sanitize_backrooms_flashlight_state(
                  backrooms_flashlight_)
            : BackroomsFlashlightState {};

    // Je sauvegarde le point de retour avec le navire courant, mais dans sa
    // pose neutre persistante afin qu'il reste exact apres le rechargement.
    snapshot.spawn_position =
        save_active_ship
            ? sea_adventure_.ship_entity().world_point_in_persisted_neutral_pose(
                  sea_adventure_.deck_spawn_position())
            : backrooms_mode
                  ? backrooms_spawn_position(
                        world_.seed(),
                        saved_backrooms_level,
                        world_.generation_version())
                  : spawn_position_;
    snapshot.player_state = player_.state();
    if (backrooms_mode) {
        // Je reancre le niveau courant sur le plan local Y=40 du fichier. Une
        // partie descendue en Poolrooms recharge ainsi au meme endroit sans
        // sauver un niveau Jack different du niveau monde.
        snapshot.player_state.position.y -=
            static_cast<float>(saved_backrooms_y_offset);
        snapshot.player_state.fall_start_y -=
            static_cast<float>(saved_backrooms_y_offset);
    }
    snapshot.progression = progression_.state();
    snapshot.player_build = player_build_;
    snapshot.legendary_weapon =
        legendary_weapon_progression_.state();
    snapshot.maritime_experience =
        experience_awards_.state();
    snapshot.hotbar = hotbar_;
    snapshot.inventory = inventory_menu_;
    snapshot.inventory.visible = false;
    snapshot.inventory.hovered_slot.reset();

    if (backrooms_mode) {
        // Le mode est volontairement sans objets, capacités ni entités. Cette
        // normalisation rend aussi une sauvegarde robuste face à un changement
        // de mode effectué depuis une ancienne version expérimentale.
        // Je persiste uniquement la FSM durable de Jack. Sa grille, son chemin
        // et ses événements audio/visuels seront reconstruits au chargement.
        auto persistent_jack = backrooms_jack_;
        translate_backrooms_jack_state_y(
            persistent_jack,
            -static_cast<float>(saved_backrooms_y_offset));
        snapshot.backrooms_jack =
            prepare_backrooms_jack_for_persistence(
                persistent_jack,
                backrooms_jack_runtime_);
        snapshot.backrooms_marlow =
            prepare_backrooms_marlow_for_persistence(
                backrooms_marlow_);
        snapshot.backrooms_marlow.logical_level =
            saved_backrooms_level;
        sanitize_backrooms_player_state(
            snapshot.player_state,
            world_.seed(),
            saved_backrooms_level,
            world_.generation_version());
        snapshot.progression = {};
        snapshot.player_build = {};
        snapshot.maritime_experience = {};
        snapshot.hotbar = {};
        snapshot.inventory = {};
        snapshot.sea_adventure = {};
        snapshot.player_ability_runtime = {};
        snapshot.musket_shot_sequence = 0U;
        snapshot.creatures.clear();
        snapshot.item_drops.clear();
        return snapshot;
    }

    snapshot.sea_adventure = sea_adventure_.save_state();
    snapshot.player_ability_runtime
        .player_effects =
        player_ability_effects_.snapshot();
    snapshot.player_ability_runtime.wind = {
        wind_acceleration_remaining_,
        wind_movement_bonus_,
        wind_recovery_bonus_,
        wind_dodge_remaining_,
        wind_blade_available_,
        wind_acceleration_cast_sequence_,
    };
    for (std::size_t index = 0U;
         index < summoned_footmen_.size();
         ++index) {
        auto& saved =
            snapshot.player_ability_runtime
                .summoned_footmen[index];
        saved.runtime =
            summoned_footmen_[index]
                .snapshot();
        saved.ship_local_position =
            summoned_footman_ship_local_positions_[
                index];
        saved.far_seconds =
            summoned_footman_far_seconds_[index];
        saved.cast_sequence =
            summoned_footman_cast_sequences_[
                index];
    }
    snapshot.player_ability_runtime
        .next_summoned_unit_id =
        next_summoned_unit_id();
    snapshot.player_ability_runtime
        .next_cast_sequence =
        ability_system_.next_cast_sequence();
    snapshot.musket_shot_sequence =
        player_musket_.view().shot_sequence;
    snapshot.creatures.assign(
        creatures_.active_creatures().begin(),
        creatures_.active_creatures().end());
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
        rebuild_colossal_world_protections();
        return;
    }

    StartingVillageGenerator generator(world_.seed());
    starting_village_ = generator.build_layout();
    if (apply_layout_to_world) {
        generator.apply(world_, starting_village_);
    }
    creatures_.set_settlement_residents(starting_village_.residents);
    rebuild_colossal_world_protections();
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
    struct PreparedShipMeshes {
        ChunkMeshData near_mesh {};
        ChunkMeshData far_mesh {};
        bool has_far_lod = false;
    };

    if (renderer_.ship_mesh_ready(ship)) {
        return true;
    }

    const auto dispatch_begin = clock::now();
    const auto* blueprint = ship.blueprint;
    auto visual_parts = ship.parts;
    if (options_.visual_pipeline ==
            VisualPipeline::LegacyVoxel &&
        blueprint != nullptr &&
        !blueprint->legacy_visual_parts.empty()) {
        // Je charge la photographie historique uniquement pour Legacy : la
        // physique et la sauvegarde continuent d'utiliser le blueprint moderne.
        visual_parts =
            blueprint->legacy_visual_parts;
    }
    auto parts =
        std::vector<ShipPart> {
            visual_parts.begin(),
            visual_parts.end()};
    const auto geometry_revision = ship.geometry_revision;
    const auto visual_pipeline = options_.visual_pipeline;
    auto mesh_future = std::async(
        std::launch::async,
        [parts = std::move(parts), blueprint, geometry_revision, visual_pipeline] {
            PreparedShipMeshes prepared {};
            if (visual_pipeline == VisualPipeline::ModernStylized &&
                blueprint != nullptr) {
                // Je construis les deux LOD hors du thread OpenGL pendant le
                // chargement afin qu'aucun seuil de distance ne provoque de
                // génération synchrone en jeu.
                auto local_blueprint = *blueprint;
                local_blueprint.parts = std::span<const ShipPart> {parts};
                local_blueprint.geometry_revision = geometry_revision;
                prepared.near_mesh =
                    build_stylized_ship_mesh(
                        local_blueprint,
                        StylizedShipLod::Near)
                        .mesh;
                prepared.far_mesh =
                    build_stylized_ship_mesh(
                        local_blueprint,
                        StylizedShipLod::Far)
                        .mesh;
                prepared.has_far_lod = true;
                return prepared;
            }
            prepared.near_mesh =
                build_ship_mesh_data(
                    std::span<const ShipPart> {
                        parts},
                    {},
                    ShipMeshLightingModel::
                        LegacyHistorical);
            return prepared;
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

    auto prepared_meshes = mesh_future.get();
    const auto upload_begin = clock::now();
    const auto ready =
        prepared_meshes.has_far_lod
            ? renderer_.upload_prepared_ship_mesh(
                  ship,
                  prepared_meshes.near_mesh,
                  prepared_meshes.far_mesh)
            : renderer_.upload_prepared_ship_mesh(
                  ship,
                  prepared_meshes.near_mesh);
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

auto Game::initial_preload_targets(const World& world, const glm::vec3& focus) const
    -> std::vector<ChunkCoord> {
    const auto center = world.world_to_chunk(
        static_cast<int>(std::floor(focus.x)),
        static_cast<int>(std::floor(focus.z)));
    const auto stream_radius = std::max(world.stream_radius(), 0);
    const auto maritime =
        world.generation_profile() == WorldGenerationProfile::OceanAdventure;
    const auto backrooms =
        world.generation_profile() == WorldGenerationProfile::Backrooms;
    auto neighborhood_radius = std::min(
        std::max(options_.performance.spawn_preload_radius, 1),
        stream_radius);
    if (maritime) {
        // Je charge deux anneaux autour du joueur en mer pour que le pont, la
        // surface et le fond immediat soient deja continus a la premiere frame.
        neighborhood_radius = std::min(
            std::max(neighborhood_radius, 2),
            stream_radius);
    }
    if (backrooms) {
        // Je termine aussi l'anneau de securite sur le GPU avant le gameplay.
        // Le joueur peut apparaitre a quelques centimetres d'une frontiere :
        // son premier pas ne doit donc jamais attendre une nouvelle salle.
        neighborhood_radius = std::min(
            std::max(
                neighborhood_radius,
                backrooms_initial_preload_radius(stream_radius)),
            stream_radius);
    }

    std::vector<ChunkCoord> targets {};
    const auto neighborhood_side =
        static_cast<std::size_t>(neighborhood_radius * 2 + 1);
    targets.reserve(neighborhood_side * neighborhood_side);
    for (int dz = -neighborhood_radius; dz <= neighborhood_radius; ++dz) {
        for (int dx = -neighborhood_radius; dx <= neighborhood_radius; ++dx) {
            targets.push_back({center.x + dx, center.z + dz});
        }
    }

    if (maritime && stream_radius > 0) {
        const auto port_layout =
            StartingPortGenerator(world.seed()).build_layout();
        const auto port_min = world.world_to_chunk(
            port_layout.min_x,
            port_layout.min_z);
        const auto port_max = world.world_to_chunk(
            port_layout.max_x,
            port_layout.max_z);
        const auto retained_min_x = center.x - stream_radius;
        const auto retained_max_x = center.x + stream_radius;
        const auto retained_min_z = center.z - stream_radius;
        const auto retained_max_z = center.z + stream_radius;
        const auto target_min_x = std::max(port_min.x, retained_min_x);
        const auto target_max_x = std::min(port_max.x, retained_max_x);
        const auto target_min_z = std::max(port_min.z, retained_min_z);
        const auto target_max_z = std::min(port_max.z, retained_max_z);

        // Je n'ajoute le port que lorsqu'il coupe la zone effectivement
        // conservable par ce monde. Une sauvegarde partie au large ne recharge
        // donc jamais le port a des kilometres du joueur.
        if (target_min_x <= target_max_x &&
            target_min_z <= target_max_z) {
            for (int z = target_min_z; z <= target_max_z; ++z) {
                for (int x = target_min_x; x <= target_max_x; ++x) {
                    targets.push_back({x, z});
                }
            }
        }
    }

    std::sort(
        targets.begin(),
        targets.end(),
        [](const ChunkCoord& lhs, const ChunkCoord& rhs) {
            return lhs.x < rhs.x ||
                   (lhs.x == rhs.x && lhs.z < rhs.z);
        });
    targets.erase(
        std::unique(targets.begin(), targets.end()),
        targets.end());
    return targets;
}

auto Game::preload_readiness(
    const World& world,
    std::span<const ChunkCoord> targets) const -> float {
    auto ready_chunks = std::size_t {0U};
    for (const auto& coord : targets) {
        const auto* chunk = world.find_chunk(coord);
        if (chunk == nullptr) {
            continue;
        }
        if (world.mesh_revision(coord) == 0 ||
            chunk->is_dirty() ||
            chunk->is_lighting_dirty()) {
            continue;
        }
        ++ready_chunks;
    }

    if (targets.empty()) {
        return 1.0F;
    }
    return static_cast<float>(ready_chunks) /
           static_cast<float>(targets.size());
}

auto Game::preload_gpu_readiness(
    const World& world,
    std::span<const ChunkCoord> targets) const -> float {
    auto ready_chunks = std::size_t {0U};
    for (const auto& coord : targets) {
        const auto* chunk = world.find_chunk(coord);
        const auto revision = world.mesh_revision(coord);
        if (chunk != nullptr &&
            !chunk->is_dirty() &&
            !chunk->is_lighting_dirty() &&
            revision != 0U &&
            renderer_.world_mesh_uploaded(coord, revision)) {
            ++ready_chunks;
        }
    }
    return targets.empty()
               ? 1.0F
               : static_cast<float>(ready_chunks) /
                     static_cast<float>(targets.size());
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
    const auto preload_targets = initial_preload_targets(world, focus);
    const auto target_count = preload_targets.size();
    auto preload_stream_radius = 0;
    for (const auto& coord : preload_targets) {
        preload_stream_radius = std::max(
            preload_stream_radius,
            std::max(
                std::abs(coord.x - center.x),
                std::abs(coord.z - center.z)));
    }
    const auto maritime = loading_theme_ == LoadingScreenTheme::Maritime;
    const auto backrooms =
        world.generation_profile() == WorldGenerationProfile::Backrooms;
    // Je prefere prolonger l'ecran de chargement sur une machine lente plutot
    // que de rendre une Backroom avant la fin de son anneau de securite.
    const auto loading_deadline =
        clock::now() +
        std::chrono::seconds(backrooms ? 120 : 60);

    const auto for_each_target = [&](const auto& visitor) {
        for (const auto& coord : preload_targets) {
            visitor(coord);
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

    // Je conserve des le depart le plus grand anneau contenant mes cibles. En
    // mer, cela empeche le petit voisinage d'apparition de decharger le port
    // que je viens de construire avant meme son premier maillage.
    (void)world.update_streaming(focus, preload_stream_radius);

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
    // Je ne recumule pas les fluides avec le maillage apres leur phase de
    // stabilisation dediee : le reliquat reprendra dans la boucle de jeu.
    meshing_budget.fluid_cell_budget = 0U;
    meshing_budget.light_node_budget = 4096U;
    // Je borne chaque tranche a une section lourde afin de garder l'ecran de
    // chargement reactif sur les deux pipelines visuels.
    meshing_budget.mesh_rebuild_budget = 1U;
    meshing_budget.max_fluid_ms = 0.0;
    meshing_budget.max_lighting_ms = 1.0;
    meshing_budget.max_meshing_ms = 3.0;
    auto meshing_ratio = preload_readiness(world, preload_targets);
    while (running_ && meshing_ratio < 1.0F) {
        check_loading_deadline();
        process_world_step(meshing_budget);
        meshing_ratio = preload_readiness(world, preload_targets);
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
        // plus les chunks indispensables a l'apparition du joueur et du port.
        const auto streaming_expand_begin = clock::now();
        (void)world.update_streaming(focus);
        record_loading_step("streaming_expansion", streaming_expand_begin);
    }
}

void Game::prepare_game_session(
    std::uint64_t musket_shot_sequence) {
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
    progression_menu_.set_visible(false);
    death_screen_visible_ = false;
    death_screen_.visible = false;
    death_screen_.cause = PlayerDeathCause::None;
    reset_backrooms_jack_runtime();
    pending_fishing_ = false;
    player_musket_.reset(
        true,
        musket_shot_sequence);
    player_musket_effects_.clear();
    bound_musket_hotbar_slot_.reset();
    musket_fire_held_ = false;
    pending_musket_fire_press_ = false;
    musket_aim_held_ = false;
    pending_musket_reload_ = false;
    clear_colossal_weapon_input();
    colossal_weapon_.reset();
    colossal_hit_ledger_.clear();
    colossal_blade_pose_valid_ = false;
    colossal_weapon_was_selected_ = false;
    pending_ability_slot_.reset();
    melee_attack_cooldown_remaining_ = 0.0F;
    wind_acceleration_remaining_ = 0.0F;
    wind_movement_bonus_ = 0.0F;
    wind_recovery_bonus_ = 0.0F;
    wind_dodge_remaining_ = 0.0F;
    wind_blade_available_ = false;
    wind_acceleration_cast_sequence_ = 0U;
    player_ability_effects_.clear();
    ability_system_.reset_timing();
    sync_selected_hotbar_slot();
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
    reset_backrooms_jack_runtime();
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
    reset_musket_interaction();
    player_musket_effects_.clear();
    reset_backrooms_jack_runtime();
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
    progression_menu_.set_visible(false);
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
    if (issou_scenario_.active() &&
        !exit_issou_scenario()) {
        queue_gameplay_announcement(
            "SORTIE IMPOSSIBLE",
            "LA SESSION NORMALE N'A PAS PU ETRE RESTAUREE",
            3.0F);
        return;
    }
    if (has_active_session_ && session_save_state_.dirty()) {
        set_confirm_dialog_visible(true, ConfirmDialogIntent::ReturnToMainMenu);
        return;
    }

    open_main_menu(true);
}

void Game::start_new_game_in_slot(std::size_t slot_index, GameMode game_mode) {
    if (issou_scenario_.active() &&
        !exit_issou_scenario()) {
        return;
    }
    using clock = std::chrono::steady_clock;

    const auto next_game_mode = is_known_game_mode(game_mode)
                                    ? game_mode
                                    : GameMode::ClassicAdventure;
    const auto sea_mode =
        next_game_mode == GameMode::SeaAdventure;
    const auto backrooms_mode =
        next_game_mode == GameMode::Backrooms;
    const auto backrooms_level =
        backrooms_mode &&
                options_.smoke_test &&
                options_.smoke_session ==
                    SmokeSessionMode::Backrooms
            ? options_.smoke_backrooms_level
            : 0;
    const auto generation_profile =
        backrooms_mode
            ? WorldGenerationProfile::Backrooms
            : sea_mode
                  ? WorldGenerationProfile::OceanAdventure
                  : WorldGenerationProfile::Continental;
    const auto loading_title =
        backrooms_mode
            ? std::string_view("BACKROOMS")
            : sea_mode
                  ? std::string_view("AVENTURE EN MER")
                  : std::string_view("NOUVELLE PARTIE");
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
    if (!(options_.smoke_test &&
          smoke_session_starts_gameplay(options_.smoke_session))) {
        seed = make_nonblocking_world_seed(slot_index);
    }
    backrooms_smoke_camera_pose_valid_ = false;
    if (backrooms_mode &&
        options_.smoke_test &&
        options_.smoke_session ==
            SmokeSessionMode::Backrooms) {
        const auto smoke_pose =
            backrooms_smoke_camera_pose(
                seed,
                options_
                    .smoke_backrooms_blackout,
                backrooms_level,
                options_.smoke_backrooms_jack,
                options_.smoke_backrooms_jack_distance);
        if (!smoke_pose.has_value()) {
            throw std::runtime_error(
                "Backrooms smoke could not find a deterministic camera pose");
        }
        backrooms_smoke_camera_position_ =
            smoke_pose->position;
        backrooms_smoke_camera_yaw_degrees_ =
            smoke_pose->yaw_degrees;
        backrooms_smoke_camera_pose_valid_ =
            true;
    }
    loading_quote_seed_ = static_cast<std::uint32_t>(seed);
    auto renderer_staged = false;
    auto session_committed = false;

    update_loading_screen(
        loading_title,
        backrooms_mode
            ? std::string_view("RECOMPOSITION DE L'ETAGE")
            : sea_mode
                  ? std::string_view("PREPARATION DU NAVIRE")
                  : std::string_view("CONSTRUCTION DU VILLAGE"),
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
            backrooms_mode
                ? backrooms_stream_radius(
                      options_.performance.stream_radius)
                : options_.performance.stream_radius,
            generation_profile,
            backrooms_mode
                ? WorldGenerationVersion::BackroomsV4
                : sea_mode
                      ? WorldGenerationVersion::LivingOceanV3
                      : WorldGenerationVersion::LegacyV1,
            options_.visual_pipeline,
            backrooms_level);
        SeaAdventureSystem prepared_sea_adventure {};
        StartingVillageLayout prepared_village {};
        CreatureSystem prepared_creatures {};
        const auto prepared_village_enabled =
            !sea_mode && !backrooms_mode;
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
        } else if (backrooms_mode) {
            // Aucun décor persistant n'est estampillé : chaque voxel vient du
            // profil procédural et peut être régénéré à partir de la graine.
            prepared_sea_adventure.load_state({}, seed);
            update_loading_screen(
                loading_title,
                "AGENCEMENT DES ESPACES IMPOSSIBLES",
                LoadingPhase::Preparation,
                0.70F,
                true);
        } else {
            prepared_sea_adventure.load_state({}, seed);
            StartingVillageGenerator village_generator(seed);
            prepared_village = village_generator.build_layout();
            village_generator.apply(prepared_world, prepared_village);
            prepared_creatures.set_settlement_residents(prepared_village.residents);
        }

        auto prepared_hotbar =
            backrooms_mode
                ? HotbarState {}
                : make_default_hotbar_state();
        auto prepared_inventory =
            backrooms_mode
                ? InventoryMenuState {}
                : make_default_inventory_menu_state();
        normalize_inventory_state(
            prepared_inventory,
            prepared_hotbar);
        const auto prepared_spawn =
            sea_mode
                ? prepared_sea_adventure.deck_spawn_position()
                : backrooms_mode
                      ? backrooms_smoke_camera_pose_valid_
                            ? backrooms_smoke_camera_position_
                            : backrooms_spawn_position(
                                  prepared_world,
                                  backrooms_level)
                      : find_initial_spawn_position(
                            prepared_world,
                            &prepared_village);
        PlayerController prepared_player {};
        prepared_player.respawn(prepared_spawn);
        prepared_player.set_fly_mode_enabled(false);
        prepared_player.set_water_movement_profile(
            backrooms_mode &&
                    prepared_world.backrooms_theme_at_y(
                        prepared_player.position().y) ==
                        BackroomsTheme::Poolrooms
                ? PlayerWaterMovementProfile::Poolrooms
                : PlayerWaterMovementProfile::Standard);
        EnvironmentClock prepared_environment {};
        prepared_environment.set_time_of_day(
            resolve_new_session_time_of_day(options_));
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
        if (!backrooms_mode) {
            const auto creature_sync_begin = clock::now();
            prepared_creatures.update(
                0.0F,
                prepared_world,
                prepared_player.position(),
                prepared_environment.current_state(),
                prepared_environment.current_creature_cycle());
            record_loading_step(
                "creature_initial_sync",
                creature_sync_begin);
        }

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
            sea_mode
                ? std::string_view("ASSEMBLAGE DU NAVIRE")
                : backrooms_mode
                      ? std::string_view("ALLUMAGE DES FLUORESCENTS")
                      : std::string_view("INITIALISATION DU RENDU"),
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
        legendary_weapon_progression_.reset();
        configure_legendary_weapon_quest();
        legendary_enemies_.clear();
        sea_leviathan_.reset();
        sea_leviathan_started_for_session_ =
            false;
        chained_colossus_ = {};
        colossus_blood_traces_.clear();
        issou_arena_minions_spawned_ =
            false;
        experience_awards_.reset();
        player_build_ = {};
        player_ability_effects_.clear();
        ability_system_.reset_timing();
        for (auto& footman :
             summoned_footmen_) {
            footman.clear();
        }
        summoned_footman_ship_local_positions_
            .fill(std::nullopt);
        summoned_footman_far_seconds_.fill(
            0.0F);
        summoned_footman_cast_sequences_.fill(
            0U);
        hotbar_ = std::move(prepared_hotbar);
        inventory_menu_ = std::move(prepared_inventory);
        player_ = std::move(prepared_player);
        environment_ = prepared_environment;
        backrooms_elapsed_seconds_ = 0.0F;
        backrooms_flashlight_ = {};
        starting_village_enabled_ = prepared_village_enabled;
        starting_village_ = std::move(prepared_village);
        rebuild_colossal_world_protections();
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
            backrooms_mode
                ? std::string_view("L'ETAGE T'ATTEND")
                : sea_mode
                      ? std::string_view("PRET A LARGUER LES AMARRES")
                      : std::string_view("AVENTURE PRETE"));
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
    if (issou_scenario_.active() &&
        !exit_issou_scenario()) {
        return false;
    }
    struct AsyncSaveProgress {
        std::atomic<std::uint8_t> phase {static_cast<std::uint8_t>(SaveLoadPhase::OpeningFile)};
        std::atomic<float> normalized {0.0F};
        std::atomic<bool> cancelled {false};
    };

    const auto metadata = slot_index < save_slot_menu_.slots.size()
                              ? save_slot_menu_.slots[slot_index]
                              : SaveSlotMetadata {};
    const auto expected_sea_mode =
        metadata.exists &&
        metadata.game_mode == GameMode::SeaAdventure;
    const auto expected_backrooms_mode =
        metadata.exists &&
        metadata.game_mode == GameMode::Backrooms;
    begin_loading_screen(
        expected_sea_mode
            ? LoadingScreenTheme::Maritime
            : LoadingScreenTheme::Standard,
        static_cast<std::uint32_t>(metadata.seed));
    const auto loading_title =
        expected_backrooms_mode
            ? std::string_view("BACKROOMS")
            : expected_sea_mode
                  ? std::string_view("AVENTURE EN MER")
                  : std::string_view("CHARGEMENT");
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

    const auto actual_sea_mode =
        snapshot->metadata.game_mode == GameMode::SeaAdventure;
    loading_theme_ =
        actual_sea_mode
            ? LoadingScreenTheme::Maritime
            : LoadingScreenTheme::Standard;
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
    if (issou_scenario_.active() &&
        !exit_issou_scenario()) {
        return false;
    }
    using clock = std::chrono::steady_clock;

    const auto next_game_mode = is_known_game_mode(snapshot.metadata.game_mode)
                                    ? snapshot.metadata.game_mode
                                    : GameMode::ClassicAdventure;
    const auto sea_mode =
        next_game_mode == GameMode::SeaAdventure;
    const auto backrooms_mode =
        next_game_mode == GameMode::Backrooms;
    const auto backrooms_level =
        backrooms_mode
            ? snapshot.backrooms_level
            : 0;
    const auto loading_title =
        backrooms_mode
            ? std::string_view("BACKROOMS")
            : sea_mode
                  ? std::string_view("AVENTURE EN MER")
                  : std::string_view("CHARGEMENT");
    const auto generation_profile =
        snapshot.world_save_plan.generation_profile;
    const auto generation_version =
        snapshot.world_save_plan.generation_version;
    const auto expected_profile =
        backrooms_mode
            ? WorldGenerationProfile::Backrooms
            : sea_mode
                  ? WorldGenerationProfile::OceanAdventure
                  : WorldGenerationProfile::Continental;
    const auto migrate_backrooms_to_v3 =
        backrooms_mode &&
        generation_profile == expected_profile &&
        (generation_version ==
             WorldGenerationVersion::BackroomsV1 ||
         generation_version ==
             WorldGenerationVersion::BackroomsV2) &&
        snapshot.world_save_plan.backrooms_level ==
            backrooms_level;
    const auto runtime_generation_version =
        backrooms_mode
            ? (migrate_backrooms_to_v3
                   ? WorldGenerationVersion::BackroomsV3
                   : generation_version)
            : generation_version;

    if (generation_profile != expected_profile ||
        (backrooms_mode &&
         !is_backrooms_generation_version(
             generation_version))) {
        throw std::invalid_argument(
            "Save generation profile is incompatible with its game mode");
    }

    if (migrate_backrooms_to_v3) {
        // Je projette chaque position depuis son étage historique avant de
        // reconstruire le monde. Les Poolrooms gagnent exactement un voxel,
        // tandis que les bureaux conservent leur hauteur V1/V2.
        migrate_backrooms_snapshot_to_v3(
            snapshot,
            generation_version,
            backrooms_level);
    }
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
            backrooms_mode
                ? backrooms_stream_radius(
                      options_.performance.stream_radius)
                : options_.performance.stream_radius,
            generation_profile,
            runtime_generation_version,
            options_.visual_pipeline,
            backrooms_level);
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
            sea_mode
                ? std::string_view("PREPARATION DU NAVIRE")
                : backrooms_mode
                      ? std::string_view("RECONSTRUCTION DE L'ETAGE")
                      : std::string_view("PREPARATION DU MONDE"),
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
        if (backrooms_mode) {
            sanitize_backrooms_player_state(
                snapshot.player_state,
                snapshot.metadata.seed,
                backrooms_level,
                runtime_generation_version);
            snapshot.progression = {};
            snapshot.player_build = {};
            snapshot.maritime_experience = {};
            snapshot.hotbar = {};
            snapshot.inventory = {};
            snapshot.item_drops.clear();
            snapshot.creatures.clear();
            snapshot.sea_adventure = {};
            snapshot.player_ability_runtime = {};
            snapshot.musket_shot_sequence = 0U;
        }

        auto prepared_hotbar = snapshot.hotbar;
        auto prepared_inventory = snapshot.inventory;
        normalize_inventory_state(prepared_inventory, prepared_hotbar);
        prepared_inventory.visible = false;
        prepared_inventory.hovered_slot.reset();
        ItemDropSystem prepared_item_drops {};
        if (!backrooms_mode) {
            prepared_item_drops.load_drops(
                snapshot.item_drops);
        }
        PlayerProgression prepared_progression {};
        prepared_progression.load_state(snapshot.progression);
        auto prepared_player_build =
            snapshot.player_build;
        sanitize_player_build_state(
            prepared_player_build,
            prepared_progression.level());
        ExperienceAwardService
            prepared_experience_awards {};
        prepared_experience_awards.load_state(
            snapshot.maritime_experience);
        const auto prepared_ability_runtime =
            backrooms_mode
                ? PlayerAbilityRuntimeSaveState {}
                : sanitize_player_ability_runtime_save_state(
                      snapshot.player_ability_runtime);
        const auto prepared_backrooms_jack =
            backrooms_mode
                ? sanitize_backrooms_jack_state(
                      snapshot.backrooms_jack)
                : initialize_backrooms_jack(
                      static_cast<std::uint32_t>(
                          snapshot.metadata.seed) ^
                      UINT32_C(0x4A41434B));
        const auto prepared_backrooms_marlow =
            backrooms_mode
                ? sanitize_backrooms_marlow_state(
                      snapshot.backrooms_marlow)
                : initialize_backrooms_marlow(
                      static_cast<std::uint32_t>(
                          snapshot.metadata.seed) ^
                          UINT32_C(0x4D524C57),
                      0);
        PlayerAbilityEffects
            prepared_player_ability_effects {};
        static_cast<void>(
            prepared_player_ability_effects
                .load_state(
                    prepared_ability_runtime
                        .player_effects));
        std::array<
            SummonedUnitSystem,
            kMaximumPlayerSummons>
            prepared_summoned_footmen {};
        std::array<
            std::optional<glm::vec3>,
            kMaximumPlayerSummons>
            prepared_summoned_ship_positions {};
        std::array<
            float,
            kMaximumPlayerSummons>
            prepared_summoned_far_seconds {};
        std::array<
            AbilityCastSequence,
            kMaximumPlayerSummons>
            prepared_summoned_cast_sequences {};
        for (std::size_t index = 0U;
             index <
                 prepared_summoned_footmen
                     .size();
             ++index) {
            const auto& saved =
                prepared_ability_runtime
                    .summoned_footmen[index];
            const auto restored =
                prepared_summoned_footmen[index]
                    .load_state(
                        saved.runtime);
            if (!restored.restored) {
                continue;
            }
            prepared_summoned_ship_positions[
                index] =
                sea_mode
                    ? saved.ship_local_position
                    : std::nullopt;
            prepared_summoned_far_seconds[index] =
                saved.far_seconds;
            prepared_summoned_cast_sequences[index] =
                saved.cast_sequence;
        }
        PlayerController prepared_player {};
        const auto prepared_robustness =
            player_attribute_value(
                prepared_player_build.attributes,
                PlayerAttribute::Robustness);
        prepared_player.set_max_health(
            player_base_max_health(
                prepared_progression.level()) +
            static_cast<float>(
                prepared_robustness));
        prepared_player.load_state(snapshot.player_state);
        prepared_player.set_water_movement_profile(
            backrooms_mode &&
                    prepared_world.backrooms_theme_at_y(
                        prepared_player.position().y) ==
                        BackroomsTheme::Poolrooms
                ? PlayerWaterMovementProfile::Poolrooms
                : PlayerWaterMovementProfile::Standard);
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
        if (!backrooms_mode) {
            prepared_creatures.load_creatures(
                snapshot.creatures,
                prepared_environment.current_state());
        }
        auto prepared_spawn =
            backrooms_mode
                ? backrooms_spawn_position(
                      prepared_world,
                      backrooms_level)
                : finite_vec3_or(
                      snapshot.spawn_position,
                      {0.5F, 70.0F, 0.5F});
        if (sea_mode && prepared_sea_adventure.active()) {
            prepared_spawn =
                prepared_sea_adventure
                    .deck_spawn_position();
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
            sea_mode
                ? std::string_view("ASSEMBLAGE DU NAVIRE")
                : backrooms_mode
                      ? std::string_view("ALLUMAGE DES FLUORESCENTS")
                      : std::string_view("INITIALISATION DU RENDU"),
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
        legendary_weapon_progression_.load_state(
            snapshot.legendary_weapon);
        configure_legendary_weapon_quest();
        legendary_enemies_.clear();
        sea_leviathan_.reset();
        sea_leviathan_started_for_session_ =
            false;
        chained_colossus_ = {};
        colossus_blood_traces_.clear();
        issou_arena_minions_spawned_ =
            false;
        player_build_ =
            std::move(prepared_player_build);
        player_ = std::move(prepared_player);
        environment_ = prepared_environment;
        backrooms_elapsed_seconds_ =
            backrooms_mode
                ? std::clamp(
                      finite_or(
                          snapshot.metadata
                              .weather_time_seconds,
                          0.0F),
                      0.0F,
                      86'400.0F)
                : 0.0F;
        backrooms_flashlight_ =
            backrooms_mode
                ? sanitize_backrooms_flashlight_state(
                      snapshot.backrooms_flashlight)
                : BackroomsFlashlightState {};
        creatures_ = std::move(prepared_creatures);
        starting_village_enabled_ = prepared_village_enabled;
        starting_village_ = std::move(prepared_village);
        rebuild_colossal_world_protections();
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
        prepare_game_session(
            snapshot.musket_shot_sequence);
        if (backrooms_mode) {
            // Je restaure l'état durable seulement après la préparation, qui
            // remet volontairement les runtimes de session à zéro. La grille
            // 5x5 et le chemin seront recalcules autour du joueur au prochain
            // pas, sans jamais réutiliser des pointeurs de chunks obsolètes.
            backrooms_jack_ =
                prepared_backrooms_jack;
            backrooms_jack_runtime_ = {};
            const auto restored_jack_level =
                current_backrooms_level();
            backrooms_jack_last_result_ = {
                .render =
                    make_backrooms_jack_render_view(
                        backrooms_jack_,
                        restored_jack_level),
                .light_interference =
                    make_backrooms_jack_light_interference_view(
                        backrooms_jack_,
                        restored_jack_level),
            };
            backrooms_jack_death_delay_seconds_ =
                0.0F;
            backrooms_jack_death_pending_ = false;
            backrooms_marlow_ =
                prepared_backrooms_marlow;
            backrooms_marlow_runtime_ = {};
            backrooms_marlow_last_result_ = {};
            backrooms_threat_arbiter_ = {};
            backrooms_threat_arbiter_.random_state =
                static_cast<std::uint32_t>(
                    snapshot.metadata.seed) ^
                UINT32_C(0x41524254);
            if (backrooms_threat_arbiter_.random_state == 0U) {
                backrooms_threat_arbiter_.random_state =
                    UINT32_C(0x41524254);
            }
            backrooms_threat_request_sequence_ = 0U;
            backrooms_marlow_previous_player_position_ =
                player_.position();
            backrooms_marlow_has_previous_player_position_ = false;
            backrooms_marlow_previous_in_water_ = false;
            backrooms_marlow_previous_on_ground_ =
                player_.state().on_ground;
            backrooms_marlow_previous_jump_input_ = false;
            backrooms_marlow_death_delay_seconds_ = 0.0F;
            backrooms_marlow_death_pending_ = false;
        }
        experience_awards_ =
            prepared_experience_awards;
        player_ability_effects_ =
            std::move(
                prepared_player_ability_effects);
        ability_system_
            .reserve_next_cast_sequence(
                prepared_ability_runtime
                    .next_cast_sequence);
        reserve_next_summoned_unit_id(
            prepared_ability_runtime
                .next_summoned_unit_id);
        summoned_footmen_ =
            std::move(
                prepared_summoned_footmen);
        summoned_footman_ship_local_positions_ =
            prepared_summoned_ship_positions;
        summoned_footman_far_seconds_ =
            prepared_summoned_far_seconds;
        summoned_footman_cast_sequences_ =
            prepared_summoned_cast_sequences;
        wind_acceleration_remaining_ =
            prepared_ability_runtime
                .wind.remaining_seconds;
        wind_movement_bonus_ =
            prepared_ability_runtime
                .wind.movement_bonus;
        wind_recovery_bonus_ =
            prepared_ability_runtime
                .wind.recovery_bonus;
        wind_dodge_remaining_ =
            prepared_ability_runtime
                .wind.dodge_remaining_seconds;
        wind_blade_available_ =
            prepared_ability_runtime
                .wind.blade_armed;
        wind_acceleration_cast_sequence_ =
            prepared_ability_runtime
                .wind.cast_sequence;
        sync_selected_hotbar_slot();
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
        if (migrate_backrooms_to_v3 &&
            slot_index.has_value()) {
            // Je réécris immédiatement le slot après le commit : la migration
            // ne sera ainsi jamais rejouée au prochain lancement.
            session_save_state_.mark_dirty();
            save_game_to_slot(*slot_index);
            if (!wait_for_pending_save_during_loading(
                    loading_title)) {
                if (!running_) {
                    return false;
                }
                // Je conserve la session migrée et son état sale si le disque
                // refuse l'écriture : le joueur pourra sécuriser le slot sans
                // perdre le monde déjà installé.
            }
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
            backrooms_mode
                ? std::string_view("L'ETAGE T'ATTEND")
                : sea_mode
                      ? std::string_view("PRET A LARGUER LES AMARRES")
                      : std::string_view("AVENTURE PRETE"));
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
    } else if (
        options_.smoke_session ==
        SmokeSessionMode::Backrooms) {
        start_new_game_in_slot(
            kSmokeSlot,
            GameMode::Backrooms);
    } else if (options_.smoke_session ==
               SmokeSessionMode::SeaOpen) {
        constexpr auto kOpenSeaRouteDistance =
            2'048.0F;

        begin_loading_screen(
            LoadingScreenTheme::Maritime,
            static_cast<std::uint32_t>(
                kSmokeSeed));
        update_loading_screen(
            "AVENTURE EN MER",
            "PREPARATION DE LA HAUTE MER",
            LoadingPhase::Preparation,
            1.0F,
            true);

        SeaAdventureSystem open_sea {};
        open_sea.reset(kSmokeSeed);
        auto sea_state =
            open_sea.save_state();
        sea_state.active = true;
        sea_state.voyage_phase =
            SeaVoyagePhase::Underway;
        sea_state.voyage_phase_elapsed = 0.0F;
        sea_state.ship_position.z =
            kOpenSeaRouteDistance + 0.5F;
        sea_state.route_distance =
            kOpenSeaRouteDistance;
        sea_state.has_stamped_ship = false;
        open_sea.load_state(
            sea_state,
            kSmokeSeed);

        SaveGameSnapshot fixture {};
        fixture.metadata.exists = true;
        fixture.metadata.seed = kSmokeSeed;
        fixture.metadata.time_of_day =
            options_.initial_time_of_day;
        fixture.metadata.weather_time_seconds =
            options_.initial_weather_time_seconds;
        fixture.metadata.game_mode =
            GameMode::SeaAdventure;
        fixture.sea_adventure =
            open_sea.save_state();
        fixture.spawn_position =
            open_sea.deck_spawn_position();
        fixture.player_state.position =
            fixture.spawn_position;
        fixture.player_state.velocity = {};
        fixture.player_state.fall_start_y =
            fixture.spawn_position.y;
        fixture.player_state.on_ground = true;
        fixture.hotbar =
            make_default_hotbar_state();
        fixture.inventory =
            make_default_inventory_menu_state();
        normalize_inventory_state(
            fixture.inventory,
            fixture.hotbar);

        WorldSavePlan fixture_plan {};
        fixture_plan.seed = kSmokeSeed;
        fixture_plan.generation_profile =
            WorldGenerationProfile::OceanAdventure;
        fixture_plan.generation_version =
            WorldGenerationVersion::LivingOceanV3;

        // Je passe par une vraie sauvegarde V3 afin que la haute mer suive
        // exactement le chargement, le streaming et le rendu d'une partie.
        write_save_slot(
            save_root_directory_,
            kSmokeSlot,
            fixture,
            fixture_plan);
        refresh_save_slots();
        if (!load_game_from_slot(
                kSmokeSlot)) {
            return false;
        }
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
    if (options_.smoke_session == SmokeSessionMode::Backrooms) {
        if (!backrooms_smoke_camera_pose_valid_) {
            throw std::runtime_error(
                "Backrooms smoke camera pose was not prepared before loading");
        }
        configure_backrooms_smoke_camera(
            player_,
            kSmokeSeed,
            backrooms_smoke_camera_position_,
            backrooms_jack_smoke_camera_yaw(
                backrooms_smoke_camera_yaw_degrees_,
                options_.smoke_backrooms_jack),
            options_.smoke_backrooms_level,
            options_.smoke_backrooms_ceiling_view);
        // Je reutilise l'horloge deterministe deja exposee par le smoke pour
        // comparer visuellement un ballast allume et sa courte panne.
        backrooms_elapsed_seconds_ =
            options_.initial_weather_time_seconds;
        if (options_.smoke_backrooms_flashlight) {
            backrooms_flashlight_ = {
                .battery_charge = 1.0F,
                .enabled = true,
            };
        }

        const BackroomsGenerator generator {
            kSmokeSeed,
            options_.smoke_backrooms_level,
        };
        const auto player_block_x =
            static_cast<int>(
                std::floor(
                    player_.position().x));
        const auto player_block_z =
            static_cast<int>(
                std::floor(
                    player_.position().z));
        const auto player_descriptor =
            generator.descriptor_at(
                player_block_x,
                player_block_z);
        auto luminous_fixture_near_blackout =
            false;
        auto maximum_actual_block_light =
            std::uint8_t {0U};
        auto blackout_light_chunks_loaded =
            true;
        auto jack_smoke_path_ready = true;
        if (options_.smoke_backrooms_blackout) {
            const auto clearance_squared =
                kBackroomsBlackoutFixtureClearance *
                kBackroomsBlackoutFixtureClearance;
            const auto player_block_y =
                static_cast<int>(
                    std::floor(
                        player_.position().y));
            for (auto delta_z =
                     -kBackroomsBlackoutFixtureClearance;
                 delta_z <=
                 kBackroomsBlackoutFixtureClearance;
                 ++delta_z) {
                for (auto delta_x =
                         -kBackroomsBlackoutFixtureClearance;
                     delta_x <=
                     kBackroomsBlackoutFixtureClearance;
                     ++delta_x) {
                    if (delta_x * delta_x +
                            delta_z * delta_z >
                        clearance_squared) {
                        continue;
                    }
                    const auto light_state =
                        generator
                            .sample_column(
                                player_block_x +
                                    delta_x,
                                player_block_z +
                                    delta_z)
                            .light_state;
                    luminous_fixture_near_blackout =
                        light_state ==
                            BackroomsLightState::Active ||
                        light_state ==
                            BackroomsLightState::Emergency;
                    if (luminous_fixture_near_blackout) {
                        break;
                    }

                }
                if (luminous_fixture_near_blackout) {
                    break;
                }
            }

            // Je valide la lumiere réellement reçue à la caméra. Le bord de
            // la zone de sécurité peut légitimement recevoir la propagation
            // d'une lampe située juste au-delà sans éclairer le joueur.
            const auto player_chunk =
                world_.world_to_chunk(
                    player_block_x,
                    player_block_z);
            if (world_.find_chunk(player_chunk) == nullptr) {
                blackout_light_chunks_loaded = false;
            } else {
                for (auto delta_y = 0;
                     delta_y <= 2;
                     ++delta_y) {
                    maximum_actual_block_light =
                        std::max(
                            maximum_actual_block_light,
                            world_.get_block_light(
                                player_block_x,
                                player_block_y +
                                    delta_y,
                                player_block_z));
                }
            }
        }
        const auto validates_jack_corridor =
            options_.smoke_backrooms_jack ==
                BackroomsJackSmokeMode::CorridorStare ||
            options_.smoke_backrooms_jack ==
                BackroomsJackSmokeMode::RearStare;
        if (validates_jack_corridor) {
            const auto distance =
                options_.smoke_backrooms_jack ==
                        BackroomsJackSmokeMode::RearStare
                    ? 24.0F
                    : options_.smoke_backrooms_jack_distance;
            const auto direction = safe_horizontal_direction(
                player_.look_direction());
            const auto feet_y = static_cast<int>(
                std::floor(player_.position().y));
            const auto last_step = static_cast<int>(
                std::ceil(distance));
            for (auto step = 0; step <= last_step; ++step) {
                const auto traveled = std::min(
                    static_cast<float>(step),
                    distance);
                const auto sample_x = static_cast<int>(std::floor(
                    player_.position().x + direction.x * traveled));
                const auto sample_z = static_cast<int>(std::floor(
                    player_.position().z + direction.z * traveled));
                const auto chunk = world_.world_to_chunk(
                    sample_x,
                    sample_z);
                if (world_.find_chunk(chunk) == nullptr ||
                    world_.mesh_revision(chunk) == 0U) {
                    jack_smoke_path_ready = false;
                    break;
                }
                const auto required_height =
                    step == last_step ? 5 : 3;
                for (auto height = 0;
                     height < required_height;
                     ++height) {
                    if (is_block_collidable(world_.get_block(
                            sample_x,
                            feet_y + height,
                            sample_z))) {
                        jack_smoke_path_ready = false;
                        break;
                    }
                }
                if (!jack_smoke_path_ready) {
                    break;
                }
            }
        }
        if (!has_active_session_ ||
            !backrooms_active() ||
            sea_adventure_.active() ||
            world_.seed() != kSmokeSeed ||
            world_.generation_profile() !=
                WorldGenerationProfile::Backrooms ||
            world_.generation_version() !=
                WorldGenerationVersion::BackroomsV4 ||
            world_.backrooms_level() !=
                options_.smoke_backrooms_level ||
            !loading_completed_ ||
            !loading_progress_.completed() ||
            loading_progress_.progress() != 1.0F ||
            loading_update_count_ < 2U ||
            pending_save_.valid() ||
            pending_world_release_.valid() ||
            session_save_state_.failed() ||
            session_save_state_.dirty() ||
            !generator.is_walkable(
                player_block_x,
                player_block_z) ||
            !jack_smoke_path_ready ||
            (options_.smoke_backrooms_blackout &&
             (player_descriptor.tension !=
                  BackroomsTension::Blackout ||
              luminous_fixture_near_blackout ||
              !blackout_light_chunks_loaded ||
              maximum_actual_block_light !=
                  std::uint8_t {0U}))) {
            std::ostringstream details;
            details
                << "Backrooms smoke loading did not complete a valid deterministic session"
                << " (position="
                << player_block_x << ',' << player_block_z
                << ", tension="
                << static_cast<int>(player_descriptor.tension)
                << ", luminous_fixture="
                << luminous_fixture_near_blackout
                << ", light_chunks_loaded="
                << blackout_light_chunks_loaded
                << ", maximum_block_light="
                << static_cast<int>(maximum_actual_block_light)
                << ", jack_path_ready="
                << jack_smoke_path_ready
                << ')';
            throw std::runtime_error(details.str());
        }

        // Je vérifie les chunks réellement nécessaires autour du joueur à
        // chaque frame de smoke via validate_smoke_frame(). Je ne reconstruis
        // pas ici une seconde liste de préchargement : elle peut légitimement
        // évoluer après l'initialisation du profil Backrooms et produire un
        // faux négatif alors que l'écran de chargement a bien rempli son contrat.
        return true;
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
        // Je verrouille ici les trois invariants propres à une nouvelle mer :
        // révision V3, navire encore amarré et port effectivement appliqué.
        if (world_.generation_version() != WorldGenerationVersion::LivingOceanV3 ||
            sea_adventure_.save_state().voyage_phase != SeaVoyagePhase::Moored ||
            !gangway_ready || !quay_ready) {
            throw std::runtime_error("New maritime smoke did not start moored in its V3 harbor");
        }
    } else if (
        options_.smoke_session ==
        SmokeSessionMode::SeaOpen) {
        const auto& sea_state =
            sea_adventure_.save_state();
        const auto player_on_ship =
            sea_adventure_
                .hud_state(player_)
                .on_ship;
        if (world_.generation_version() !=
                WorldGenerationVersion::
                    LivingOceanV3 ||
            sea_state.voyage_phase !=
                SeaVoyagePhase::Underway ||
            sea_state.ship_position.z <
                2'000.0F ||
            !player_on_ship) {
            throw std::runtime_error(
                "Open-sea smoke did not start underway on its V3 route");
        }
    }
    const auto smoke_preload_targets =
        initial_preload_targets(world_, player_.position());
    if (preload_readiness(world_, smoke_preload_targets) < 1.0F) {
        throw std::runtime_error("Maritime smoke loading entered gameplay before CPU chunks were ready");
    }
    if (preload_gpu_readiness(world_, smoke_preload_targets) < 1.0F) {
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
    if (!has_active_session_ ||
        !scenario_session_.saves_allowed() ||
        issou_scenario_.saving_suspended()) {
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
    if (!scenario_session_.saves_allowed() ||
        issou_scenario_.saving_suspended()) {
        return;
    }
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
    if (options_.smoke_session == SmokeSessionMode::Backrooms) {
        // Je garde la caméra du smoke dans le hub Backrooms : la trajectoire
        // terrestre générique cherche une hauteur de surface extérieure et
        // finirait au-dessus du plafond, avec uniquement le brouillard à l'écran.
        configure_backrooms_smoke_camera(
            player_,
            world_.seed(),
            backrooms_smoke_camera_pose_valid_
                ? backrooms_smoke_camera_position_
                : backrooms_spawn_position(
                      world_,
                      world_.backrooms_level()),
            backrooms_jack_smoke_camera_yaw(
                backrooms_smoke_camera_pose_valid_
                    ? backrooms_smoke_camera_yaw_degrees_
                    : -90.0F,
                options_.smoke_backrooms_jack),
            world_.backrooms_level(),
            options_.smoke_backrooms_ceiling_view);
        return;
    }

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
            local_center.z,
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
            blueprint.anchors.crew_quarters +
            glm::vec3 {
                0.15F,
                1.0F,
                6.0F,
            });
        break;

    case SmokeShipView::CaptainCabin:
        // Je cadre le lit depuis tribord pour contrôler en une image sa
        // literie, le tapis et la fermeture du bordé arrière.
        camera_position = to_world({
            4.20F,
            1.01F,
            -31.60F,
        });
        camera_focus = to_world({
            -4.50F,
            1.80F,
            -30.30F,
        });
        break;

    case SmokeShipView::CargoHold:
        // Je place ce contrôle dans l'axe exact où une rupture de bouchain ou
        // une fin de plancher révélerait la mer derrière les dernières caisses.
        // Je reste dans l'allée pour ne pas cadrer le quartier-maître de trop près.
        camera_position = to_world({
            0.25F,
            -4.99F,
            15.80F,
        });
        camera_focus = to_world({
            0.0F,
            -4.00F,
            22.50F,
        });
        break;

    case SmokeShipView::CrewDeck:
        // Je photographie les modules depuis leur porte avant : l'allée reste
        // entièrement visible et aucun marin au repos ne masque l'objectif.
        camera_position = to_world({
            0.25F,
            -1.99F,
            -10.70F,
        });
        camera_focus = to_world({
            0.0F,
            -0.62F,
            -17.40F,
        });
        break;

    case SmokeShipView::Infirmary:
        // Je cadre toute l'infirmerie depuis sa porte, avant les rideaux, pour
        // contrôler simultanément les deux lits et l'étanchéité du bordé.
        camera_position = to_world({
            0.25F,
            -1.99F,
            -19.70F,
        });
        camera_focus = to_world({
            0.0F,
            -0.80F,
            -26.20F,
        });
        break;

    case SmokeShipView::Mess:
        // Je cadre simultanément les pieds des tables et le passage vers les
        // pompes pour repérer les volumes pleins ou les collisions parasites.
        camera_position = to_world({
            0.0F,
            -4.99F,
            -8.5F,
        });
        camera_focus = to_world({
            3.20F,
            -3.88F,
            -1.0F,
        });
        break;

    case SmokeShipView::GunDeck:
        camera_position = to_world({
            0.0F,
            1.01F,
            -17.0F,
        });
        camera_focus = to_world({
            5.40F,
            2.12F,
            -8.0F,
        });
        break;

    case SmokeShipView::Underwater:
        // Je place cette capture sous la flottaison, hors de la coque, pour
        // contrôler simultanément le fond, la brume et la silhouette du navire.
        camera_position = to_world({
            extent.x * 1.15F,
            -15.0F,
            local_center.z - 2.0F,
        });
        camera_focus = to_world({
            0.0F,
            -20.0F,
            local_center.z + 18.0F,
        });
        break;

    case SmokeShipView::Wake:
        // Je décale la caméra derrière bâbord pour laisser visibles l'écume de
        // poupe et les deux rubans, sans que la coque ou le HUD les recouvrent.
        camera_position = to_world({
            -extent.x * 0.52F,
            2.4F,
            blueprint.bounds.min.z - 23.0F,
        });
        camera_focus = to_world({
            0.0F,
            0.25F,
            blueprint.bounds.min.z + 1.5F,
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
    const auto underwater_view =
        options_.smoke_ship_view ==
        SmokeShipView::Underwater;
    camera_state.head_underwater = underwater_view;
    camera_state.swimming = underwater_view;
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
    sample.gpu_water_resolve_ms =
        frame_stats.gpu_water_resolve_ms;
    sample.gpu_water_surface_ms =
        frame_stats.gpu_water_surface_ms;
    sample.gpu_transparent_weather_ms =
        frame_stats.gpu_transparent_weather_ms;
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
    case UiScreen::Progression:
        sample.ui_screen = "progression";
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
