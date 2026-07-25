#include "gameplay/SeaAdventure.h"

#include "creatures/CreatureSystem.h"
#include "gameplay/ItemDropSystem.h"
#include "gameplay/PlayerController.h"
#include "world/World.h"
#include "world/OceanSimulation.h"
#include "world/WorldGenerator.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace valcraft {

auto ShipProtectionProfile::half_width_at(float local_z) const noexcept -> float {
    const auto finite_profile =
        std::isfinite(stern_z) &&
        std::isfinite(bow_z) &&
        std::isfinite(maximum_half_width) &&
        std::isfinite(stern_width_loss) &&
        std::isfinite(bow_width_loss) &&
        std::isfinite(stern_taper_exponent) &&
        std::isfinite(bow_taper_exponent);
    if (!finite_profile ||
        !std::isfinite(local_z) ||
        stern_z >= 0.0F ||
        bow_z <= 0.0F ||
        maximum_half_width <= 0.0F ||
        stern_width_loss < 0.0F ||
        bow_width_loss < 0.0F ||
        stern_width_loss > maximum_half_width ||
        bow_width_loss > maximum_half_width ||
        stern_taper_exponent <= 0.0F ||
        bow_taper_exponent <= 0.0F) {
        return 0.0F;
    }

    const auto bow_side = local_z >= 0.0F;
    const auto extent =
        bow_side
            ? bow_z
            : -stern_z;
    const auto width_loss =
        bow_side
            ? bow_width_loss
            : stern_width_loss;
    const auto exponent =
        bow_side
            ? bow_taper_exponent
            : stern_taper_exponent;
    const auto progression =
        std::clamp(
            std::abs(local_z) / extent,
            0.0F,
            1.0F);
    const auto minimum_half_width =
        maximum_half_width -
        width_loss;
    return std::max(
        minimum_half_width,
        maximum_half_width -
            width_loss *
                std::pow(
                    progression,
                    exponent));
}

auto ShipProtectionProfile::excludes_ocean_local(
    const glm::vec3& local_point) const noexcept
    -> bool {

    const auto finite_point =
        std::isfinite(local_point.x) &&
        std::isfinite(local_point.y) &&
        std::isfinite(local_point.z);
    const auto finite_bands =
        std::isfinite(lower_hull_min_y) &&
        std::isfinite(middle_hull_min_y) &&
        std::isfinite(upper_hull_min_y) &&
        std::isfinite(main_deck_top_y) &&
        std::isfinite(lower_width_inset) &&
        std::isfinite(middle_width_inset) &&
        std::isfinite(lower_minimum_half_width) &&
        std::isfinite(middle_minimum_half_width) &&
        std::isfinite(boundary_margin);
    if (!finite_point ||
        !finite_bands ||
        !(lower_hull_min_y <
          middle_hull_min_y &&
          middle_hull_min_y <
              upper_hull_min_y &&
          upper_hull_min_y <
              main_deck_top_y) ||
        lower_width_inset < 0.0F ||
        middle_width_inset < 0.0F ||
        lower_minimum_half_width < 0.0F ||
        middle_minimum_half_width < 0.0F ||
        boundary_margin < 0.0F ||
        local_point.z <
            stern_z -
                boundary_margin ||
        local_point.z >
            bow_z +
                boundary_margin ||
        local_point.y <
            lower_hull_min_y ||
        local_point.y >=
            main_deck_top_y) {
        return false;
    }

    const auto hull_half_width =
        half_width_at(local_point.z);
    if (hull_half_width <= 0.0F) {
        return false;
    }

    auto protected_half_width =
        hull_half_width;
    if (local_point.y <
        middle_hull_min_y) {
        protected_half_width =
            std::max(
                lower_minimum_half_width,
                hull_half_width -
                    lower_width_inset);
    } else if (local_point.y <
               upper_hull_min_y) {
        protected_half_width =
            std::max(
                middle_minimum_half_width,
                hull_half_width -
                    middle_width_inset);
    }

    return std::abs(local_point.x) <=
           protected_half_width +
               boundary_margin;
}

auto ShipProtectionProfile::shelters_from_weather_local(
    const glm::vec3& local_point) const noexcept
    -> bool {

    if (!std::isfinite(sheltered_floor_y) ||
        local_point.y <
            sheltered_floor_y ||
        !excludes_ocean_local(local_point) ||
        local_point.z < stern_z ||
        local_point.z > bow_z) {
        return false;
    }

    const auto hull_half_width =
        half_width_at(local_point.z);
    const auto sheltered_half_width =
        std::max(
            middle_minimum_half_width,
            hull_half_width -
                middle_width_inset) -
        boundary_margin;

    // Je garde la pluie hors du volume habitable, sans rendre la coque
    // exterieure artificiellement seche.
    return sheltered_half_width >= 0.0F &&
           std::abs(local_point.x) <=
               sheltered_half_width;
}

namespace {

constexpr float kShipVisualY = static_cast<float>(kSeaLevel + 1);
constexpr float kShipBaseSpeed = 1.18F;
constexpr float kMooredBoardingSeconds = 8.0F;
constexpr float kDepartureAccelerationSeconds = 12.0F;
constexpr float kShipSupportSampleRadius = 0.28F;
constexpr float kShipSupportProbeDepth = 0.22F;
constexpr float kShipRideContactTolerance = 0.12F;
constexpr float kShipRideVerticalSpeedTolerance = 1.0e-4F;
constexpr float kShipDropSaveContactTolerance = 0.03F;
constexpr float kStrandedWarningDistance = 170.0F;
constexpr float kStrandedLossDistance = 245.0F;
constexpr float kFishingBaseSeconds = 6.5F;
constexpr float kFishingStormPenalty = 4.0F;
constexpr float kFishingNightBonus = 1.2F;
constexpr float kSurvivalDamageInterval = 1.75F;
constexpr float kStrandedWarningInterval = 8.0F;
constexpr float kAboardHungerLossPerSecond = 0.04F;
constexpr float kAboardThirstLossPerSecond = 0.05F;
constexpr float kAwayHungerLossPerSecond = 0.16F;
constexpr float kAwayThirstLossPerSecond = 0.20F;
constexpr float kAutomaticMealThreshold = 80.0F;
constexpr float kAutomaticDrinkThreshold = 80.0F;
constexpr float kRespawnMinimumHunger = 35.0F;
constexpr float kRespawnMinimumThirst = 35.0F;
constexpr float kRespawnStamina = 100.0F;
constexpr float kShipCoordinateLimit = 1'000'000.0F;
constexpr std::int64_t kLegacyShipOriginTolerance = 1;
constexpr std::size_t kLegacyShipV7VoxelCount = 2814U;
constexpr float kCollisionEpsilon = 0.001F;
constexpr float kLegacySupportTolerance = 0.45F;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

struct LegacyShipBlock {
    int x = 0;
    int y = 0;
    int z = 0;
    BlockId block_id = to_block_id(BlockType::Planks);
};

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

auto sanitize_sea_environment(
    const EnvironmentState& environment) noexcept -> EnvironmentState {

    const EnvironmentState defaults {};
    auto sanitized = environment;

    // EnvironmentClock produit deja des valeurs valides, mais cette frontiere
    // publique doit aussi resister aux tests, mods et futurs chargeurs externes.
    sanitized.time_of_day =
        EnvironmentClock::normalize_time_of_day(environment.time_of_day);

    sanitized.precipitation_intensity = std::clamp(
        finite_or(
            environment.precipitation_intensity,
            defaults.precipitation_intensity),
        0.0F,
        1.0F);

    sanitized.storm_intensity = std::clamp(
        finite_or(
            environment.storm_intensity,
            defaults.storm_intensity),
        0.0F,
        1.0F);

    sanitized.wind_strength = std::clamp(
        finite_or(
            environment.wind_strength,
            defaults.wind_strength),
        0.0F,
        1.0F);

    return sanitized;
}

auto smoothstep_primitive(float ratio) noexcept -> float {
    const auto clamped = std::clamp(ratio, 0.0F, 1.0F);
    const auto squared = clamped * clamped;
    return squared * clamped - 0.5F * squared * squared;
}

auto smoothstep_motion_seconds(float elapsed_before, float elapsed_after) noexcept -> float {
    const auto before_ratio = elapsed_before / kDepartureAccelerationSeconds;
    const auto after_ratio = elapsed_after / kDepartureAccelerationSeconds;
    return kDepartureAccelerationSeconds *
           (smoothstep_primitive(after_ratio) - smoothstep_primitive(before_ratio));
}

void integrate_critical_spring(float& value,
                               float& velocity,
                               float target,
                               float frequency_hz,
                               float dt) noexcept {
    if (dt <= 0.0F) {
        return;
    }

    constexpr float kTwoPi = 6.28318530717958647692F;
    const auto angular_frequency =
        kTwoPi * std::max(frequency_hz, 0.01F);
    const auto frequency_squared =
        angular_frequency * angular_frequency;
    const auto damping_term =
        1.0F + 2.0F * dt * angular_frequency;
    const auto position_term =
        dt * dt * frequency_squared;
    const auto inverse_determinant =
        1.0F / (damping_term + position_term);

    const auto previous_value = value;
    const auto previous_velocity = velocity;
    value =
        (damping_term * previous_value +
         dt * previous_velocity +
         position_term * target) *
        inverse_determinant;
    velocity =
        (previous_velocity +
         dt * frequency_squared *
             (target - previous_value)) *
        inverse_determinant;

    if (!std::isfinite(value)) {
        value = target;
    }
    if (!std::isfinite(velocity)) {
        velocity = 0.0F;
    }
}

void clamp_spring_state(
    float& value,
    float& velocity,
    float maximum_absolute_value) noexcept {

    const auto safe_limit =
        std::max(
            finite_or(
                maximum_absolute_value,
                0.0F),
            0.0F);
    const auto clamped_value =
        std::clamp(
            finite_or(value, 0.0F),
            -safe_limit,
            safe_limit);
    const auto position_was_clamped =
        !std::isfinite(value) ||
        clamped_value != value;
    value = clamped_value;
    if (position_was_clamped ||
        !std::isfinite(velocity)) {
        velocity = 0.0F;
    }
}

auto sea_motion_scale(const SeaAdventureSaveState& state) noexcept -> float {
    switch (state.voyage_phase) {
    case SeaVoyagePhase::Moored:
        // Les amarres absorbent une grande partie du roulis au port.
        return 0.38F;

    case SeaVoyagePhase::Departing:
        return std::clamp(
            0.38F +
                0.62F *
                    (state.voyage_phase_elapsed /
                     kDepartureAccelerationSeconds),
            0.38F,
            1.0F);

    case SeaVoyagePhase::Underway:
    default:
        return 1.0F;
    }
}

auto advance_voyage_phase(SeaAdventureSaveState& state,
                          float dt,
                          bool player_on_ship,
                          SeaAdventureFrameResult& result) noexcept -> float {
    auto remaining_dt = dt;
    auto motion_seconds = 0.0F;

    if (state.voyage_phase == SeaVoyagePhase::Moored) {
        if (!player_on_ship) {
            return 0.0F;
        }

        const auto waiting_remaining = kMooredBoardingSeconds - state.voyage_phase_elapsed;
        const auto waiting_step = std::min(remaining_dt, waiting_remaining);
        state.voyage_phase_elapsed += waiting_step;
        remaining_dt -= waiting_step;
        if (waiting_step < waiting_remaining) {
            return 0.0F;
        }

        state.voyage_phase = SeaVoyagePhase::Departing;
        state.voyage_phase_elapsed = 0.0F;
        result.departure_started = true;
    }

    if (state.voyage_phase == SeaVoyagePhase::Departing) {
        const auto acceleration_remaining = kDepartureAccelerationSeconds - state.voyage_phase_elapsed;
        const auto acceleration_step = std::min(remaining_dt, acceleration_remaining);
        const auto elapsed_before = state.voyage_phase_elapsed;
        state.voyage_phase_elapsed += acceleration_step;
        remaining_dt -= acceleration_step;
        motion_seconds += smoothstep_motion_seconds(elapsed_before, state.voyage_phase_elapsed);
        if (acceleration_step < acceleration_remaining) {
            return motion_seconds;
        }

        state.voyage_phase = SeaVoyagePhase::Underway;
        state.voyage_phase_elapsed = 0.0F;
        result.reached_open_sea = true;
    }

    // Je ne conditionne plus la navigation a la presence du joueur une fois
    // les amarres larguees : le navire poursuit alors sa route normalement.
    motion_seconds += remaining_dt;
    return motion_seconds;
}

auto is_sane_ship_world_position(const glm::vec3& position) noexcept -> bool {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
           std::abs(position.x) <= kShipCoordinateLimit &&
           std::abs(position.y) <= kShipCoordinateLimit &&
           std::abs(position.z) <= kShipCoordinateLimit;
}

auto hash_u32(std::uint32_t value) noexcept -> std::uint32_t {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

auto ship_origin_x(const SeaAdventureSaveState& state) noexcept -> int {
    return static_cast<int>(std::floor(state.ship_position.x));
}

auto ship_origin_z(const SeaAdventureSaveState& state) noexcept -> int {
    return static_cast<int>(std::floor(state.ship_position.z));
}

void append_legacy_block(std::vector<LegacyShipBlock>& blocks, int x, int y, int z, BlockType type) {
    blocks.push_back({x, y, z, to_block_id(type)});
}

auto legacy_ship_half_width_at_z(int z) noexcept -> int {
    const auto taper = std::abs(static_cast<float>(z)) / 31.0F;
    return std::max(2, static_cast<int>(std::round(8.0F - taper * 4.9F)));
}

auto build_legacy_ship_v7_blocks() -> const std::vector<LegacyShipBlock>& {
    static const auto blocks = [] {
        std::vector<LegacyShipBlock> output;
        output.reserve(4200U);

        for (int z = -31; z <= 29; ++z) {
            const auto half_width = legacy_ship_half_width_at_z(z);
            for (int x = -half_width; x <= half_width; ++x) {
                const auto side = std::abs(x) == half_width;
                const auto bow = z <= -29 || z >= 27;
                const auto inner = std::abs(x) <= half_width - 1 && !bow;
                if (inner) {
                    append_legacy_block(output, x, kSeaLevel + 2, z, BlockType::Planks);
                    append_legacy_block(output, x, kSeaLevel + 4, z, BlockType::Planks);
                }
                if (side || bow) {
                    append_legacy_block(output, x, kSeaLevel + 1, z, BlockType::Wood);
                    append_legacy_block(output, x, kSeaLevel + 2, z, BlockType::Wood);
                    append_legacy_block(output, x, kSeaLevel + 3, z, BlockType::Planks);
                    append_legacy_block(output, x, kSeaLevel + 5, z, BlockType::Wood);
                }
                if (inner && (z % 9 == 0) && std::abs(x) > 2) {
                    append_legacy_block(output, x, kSeaLevel + 3, z, BlockType::Wood);
                }
            }
        }

        for (int z = -26; z <= 24; z += 8) {
            const auto half_width = legacy_ship_half_width_at_z(z);
            append_legacy_block(output, -half_width - 1, kSeaLevel + 4, z, BlockType::Cobblestone);
            append_legacy_block(output, -half_width - 2, kSeaLevel + 4, z, BlockType::Stone);
            append_legacy_block(output, half_width + 1, kSeaLevel + 4, z, BlockType::Cobblestone);
            append_legacy_block(output, half_width + 2, kSeaLevel + 4, z, BlockType::Stone);
        }

        for (int z = 12; z <= 25; ++z) {
            for (int x = -5; x <= 5; ++x) {
                const auto wall = x == -5 || x == 5 || z == 12 || z == 25;
                append_legacy_block(output, x, kSeaLevel + 5, z, wall ? BlockType::Wood : BlockType::Planks);
                if (wall) {
                    append_legacy_block(output, x, kSeaLevel + 6, z, BlockType::Planks);
                    append_legacy_block(output, x, kSeaLevel + 7, z, ((x + z) % 5 == 0) ? BlockType::Glass : BlockType::Planks);
                    append_legacy_block(output, x, kSeaLevel + 8, z, BlockType::Wood);
                }
                append_legacy_block(output, x, kSeaLevel + 9, z, BlockType::Wood);
            }
        }

        for (int z = -27; z <= -18; ++z) {
            for (int x = -4; x <= 4; ++x) {
                append_legacy_block(output, x, kSeaLevel + 5, z, BlockType::Planks);
                if (x == -4 || x == 4 || z == -27 || z == -18) {
                    append_legacy_block(output, x, kSeaLevel + 6, z, BlockType::Wood);
                    append_legacy_block(output, x, kSeaLevel + 7, z, BlockType::Planks);
                }
            }
        }

        for (const int mast_z : {-17, 0, 15}) {
            for (int y = kSeaLevel + 5; y <= kSeaLevel + 22; ++y) {
                append_legacy_block(output, 0, y, mast_z, BlockType::Wood);
            }
            for (int x = -7; x <= 7; ++x) {
                append_legacy_block(output, x, kSeaLevel + 15, mast_z, BlockType::Wood);
            }
            for (int y = kSeaLevel + 10; y <= kSeaLevel + 18; ++y) {
                for (int x = -5; x <= 5; ++x) {
                    if (std::abs(x) <= 5 - (y - (kSeaLevel + 10)) / 3) {
                        append_legacy_block(output, x, y, mast_z + 1, BlockType::Snow);
                    }
                }
            }
        }

        for (int z = -23; z <= 21; z += 11) {
            append_legacy_block(output, -2, kSeaLevel + 5, z, BlockType::Torch);
            append_legacy_block(output, 2, kSeaLevel + 5, z, BlockType::Torch);
        }
        for (int z = 3; z <= 10; ++z) {
            append_legacy_block(output, -3, kSeaLevel + 5, z, BlockType::Cobblestone);
            append_legacy_block(output, -2, kSeaLevel + 5, z, BlockType::Stone);
            append_legacy_block(output, -1, kSeaLevel + 5, z, BlockType::Cobblestone);
        }
        for (int z = -8; z <= 6; z += 2) {
            append_legacy_block(output, 0, kSeaLevel + 3, z, BlockType::Planks);
            append_legacy_block(output, 0, kSeaLevel + 4, z + 1, BlockType::Planks);
        }

        return output;
    }();
    return blocks;
}

auto legacy_ship_v7_voxels() -> const std::vector<ShipVoxel>& {
    static const auto voxels = [] {
        // Je canonicalise le modele une seule fois : les anciens appels set_block
        // masquaient naturellement les doublons, alors qu'un mesh dynamique les
        // rendrait inutilement plusieurs fois.
        std::unordered_map<BlockCoord, BlockId, BlockCoordHash> unique_blocks;
        unique_blocks.reserve(build_legacy_ship_v7_blocks().size());
        for (const auto& block : build_legacy_ship_v7_blocks()) {
            unique_blocks[{block.x, block.y - static_cast<int>(kShipVisualY), block.z}] = block.block_id;
        }

        std::vector<ShipVoxel> output;
        output.reserve(unique_blocks.size());
        for (const auto& [local_block, block_id] : unique_blocks) {
            output.push_back({local_block, block_id});
        }
        std::sort(output.begin(), output.end(), [](const ShipVoxel& lhs, const ShipVoxel& rhs) {
            if (lhs.local_block.y != rhs.local_block.y) {
                return lhs.local_block.y < rhs.local_block.y;
            }
            if (lhs.local_block.z != rhs.local_block.z) {
                return lhs.local_block.z < rhs.local_block.z;
            }
            return lhs.local_block.x < rhs.local_block.x;
        });
        // Je rends toute divergence de l'empreinte v7 immediatement visible :
        // cette liste ne doit jamais suivre les evolutions visuelles du navire.
        if (output.size() != kLegacyShipV7VoxelCount) {
            std::terminate();
        }
        return output;
    }();
    return voxels;
}

struct LegacyShipSurfaceIndex {
    std::unordered_set<BlockCoord, BlockCoordHash> exposed_supports {};
};

auto legacy_ship_surface_index() -> const LegacyShipSurfaceIndex& {
    static const auto index = [] {
        LegacyShipSurfaceIndex output;
        std::unordered_set<BlockCoord, BlockCoordHash> occupied_cells;
        const auto& voxels = legacy_ship_v7_voxels();
        occupied_cells.reserve(voxels.size());
        output.exposed_supports.reserve(voxels.size());

        for (const auto& voxel : voxels) {
            if (is_block_collidable(voxel.block_id)) {
                occupied_cells.insert(voxel.local_block);
            }
        }
        for (const auto& voxel : voxels) {
            if (!is_block_surface_support(voxel.block_id)) {
                continue;
            }
            const BlockCoord surface {
                voxel.local_block.x,
                voxel.local_block.y + 1,
                voxel.local_block.z,
            };
            if (!occupied_cells.contains(surface)) {
                output.exposed_supports.insert(surface);
            }
        }
        return output;
    }();
    return index;
}

auto legacy_ship_supports_position(const glm::vec3& local_position, float half_width) noexcept -> bool {
    const auto sample_radius = std::clamp(half_width, 0.05F, kShipSupportSampleRadius);
    const std::array<glm::vec2, 5> samples {{
        {0.0F, 0.0F},
        {sample_radius, sample_radius},
        {-sample_radius, sample_radius},
        {sample_radius, -sample_radius},
        {-sample_radius, -sample_radius},
    }};
    const auto first_top = static_cast<int>(std::ceil(local_position.y - kLegacySupportTolerance - kCollisionEpsilon));
    const auto last_top = static_cast<int>(std::floor(local_position.y + kLegacySupportTolerance + kCollisionEpsilon));
    const auto& surfaces = legacy_ship_surface_index().exposed_supports;

    for (const auto& sample : samples) {
        const auto x = static_cast<int>(std::floor(local_position.x + sample.x));
        const auto z = static_cast<int>(std::floor(local_position.z + sample.y));
        for (int top = first_top; top <= last_top; ++top) {
            if (surfaces.contains({x, top, z})) {
                return true;
            }
        }
    }
    return false;
}

void add_box(std::vector<ShipPart>& parts,
             ShipMaterial material,
             const glm::vec3& min_corner,
             const glm::vec3& max_corner,
             bool collidable,
             bool supports_player = false,
             ShipPartShape shape = ShipPartShape::Box,
             const glm::vec3& orientation = {0.0F, 0.0F, 1.0F}) {
    parts.push_back({
        shape,
        material,
        min_corner,
        max_corner,
        orientation,
        0.0F,
        collidable,
        supports_player,
        U'\0',
    });
}

void add_segment(std::vector<ShipPart>& parts,
                 ShipMaterial material,
                 const glm::vec3& start,
                 const glm::vec3& end,
                 float thickness,
                 ShipPartShape shape = ShipPartShape::Segment) {
    parts.push_back({
        shape,
        material,
        start,
        end,
        {0.0F, 0.0F, 1.0F},
        thickness,
        false,
        false,
        U'\0',
    });
}

void add_panel(std::vector<ShipPart>& parts,
               ShipMaterial material,
               const glm::vec3& min_corner,
               const glm::vec3& max_corner,
               const glm::vec3& normal,
               float thickness = 0.08F,
               bool collidable = false) {
    parts.push_back({
        ShipPartShape::Panel,
        material,
        min_corner,
        max_corner,
        normal,
        thickness,
        collidable,
        false,
        U'\0',
    });
}

void add_glyph(
    std::vector<ShipPart>& parts,
    char32_t glyph,
    const glm::vec3& min_corner,
    const glm::vec3& max_corner,
    ShipMaterial material = ShipMaterial::Brass,
    const glm::vec3& orientation = {0.0F, 0.0F, -1.0F}) {

    parts.push_back({
        ShipPartShape::Glyph,
        material,
        min_corner,
        max_corner,
        orientation,
        0.06F,
        false,
        false,
        glyph,
    });
}

constexpr ShipProtectionProfile kAmelieProtectionProfile {
    -35.0F,
    36.0F,
    8.60F,
    2.25F,
    7.50F,
    1.35F,
    1.55F,
    -1.42F,
    -0.62F,
    0.82F,
    4.0F,
    1.25F,
    0.62F,
    0.70F,
    0.80F,
    0.04F,
    1.0F,
};
constexpr float kAmelieSternZ =
    kAmelieProtectionProfile.stern_z;
constexpr float kAmelieBowZ =
    kAmelieProtectionProfile.bow_z;
constexpr float kAmelieMainDeckUnderside = 3.65F;
constexpr float kAmelieMainDeckTop =
    kAmelieProtectionProfile.main_deck_top_y;
constexpr int kAmelieBoardingMinRowZ = -9;
constexpr int kAmelieBoardingMaxRowZ = -7;
constexpr float kAmelieBoardingOuterX = 9.0F;
constexpr float kAmelieBoardingNetX = 8.92F;
constexpr float kAmelieBoardingNetMinY = -1.30F;
constexpr float kAmelieBoardingNetMaxY = 4.48F;
constexpr float kAmelieBoardingNetMinZ = -9.0F;
constexpr float kAmelieBoardingNetMaxZ = -6.0F;
constexpr float kAmelieBoardingNetThickness = 0.06F;
constexpr float kAmelieBoardingNetGrabHalfDepth = 0.18F;
constexpr float kAmelieBoardingDeckExitX = 7.45F;
constexpr std::array<glm::vec3, 4> kAmelieInteriorLanterns {{
    {1.25F, 3.025F, -30.5F},
    {-1.55F, 3.025F, -6.0F},
    {1.45F, 3.025F, 5.0F},
    {-2.40F, 3.025F, 24.0F},
}};

void add_climbable_net(std::vector<ShipPart>& parts, float x, const glm::vec3& outward_normal) {
    parts.push_back({
        ShipPartShape::ClimbableNet,
        ShipMaterial::Rope,
        {x, kAmelieBoardingNetMinY, kAmelieBoardingNetMinZ},
        {x, kAmelieBoardingNetMaxY, kAmelieBoardingNetMaxZ},
        outward_normal,
        kAmelieBoardingNetThickness,
        false,
        false,
        U'\0',
    });
}

auto amelie_crew_navigation_nodes() -> const std::array<ShipCrewNavigationNode, 42>& {
    // Je place les stations dans les axes réellement dégagés du blueprint :
    // aucune route d'équipage ne dépend d'une coordonnée monde ou d'un chunk.
    static const std::array<ShipCrewNavigationNode, 42> nodes {{
        {ShipCrewStation::Helm, {0.0F, 4.51F, -30.55F}, true},
        {ShipCrewStation::ChartTable, {0.75F, 1.01F, -28.10F}, false},
        {ShipCrewStation::CaptainCabin, {0.0F, 1.01F, -22.0F}, false},
        {ShipCrewStation::AftWatch, {-3.50F, 4.51F, -32.25F}, true},
        {ShipCrewStation::PortFishing, {-7.55F, 4.01F, -6.0F}, true},
        {ShipCrewStation::StarboardFishing, {7.55F, 4.01F, -8.0F}, true},
        {ShipCrewStation::MainMast, {-1.35F, 4.01F, 0.0F}, true},
        {ShipCrewStation::ForeMast, {-1.35F, 4.01F, 18.0F}, true},
        {ShipCrewStation::MizzenMast, {-1.35F, 4.01F, -17.5F}, true},
        {ShipCrewStation::WaterStill, {3.35F, 1.01F, 5.55F}, false},
        {ShipCrewStation::Galley, {0.0F, 1.01F, 5.25F}, false},
        {ShipCrewStation::Capstan, {-2.70F, 4.01F, 15.60F}, true},
        {ShipCrewStation::AftDeck, {0.0F, 4.01F, -23.5F}, true},
        {ShipCrewStation::MidDeckPort, {-4.50F, 4.01F, 2.0F}, true},
        {ShipCrewStation::MidDeckStarboard, {3.80F, 4.01F, 10.0F}, true},
        {ShipCrewStation::ForeDeck, {0.0F, 4.51F, 29.0F}, true},
        {ShipCrewStation::AftStairsTop, {0.0F, 4.01F, -14.50F}, true},
        {ShipCrewStation::AftStairsMid, {0.0F, 2.51F, -11.50F}, false},
        {ShipCrewStation::AftStairsBottom, {0.0F, 1.01F, -8.50F}, false},
        {ShipCrewStation::ForeStairsTop, {0.0F, 4.01F, 13.50F}, true},
        {ShipCrewStation::ForeStairsMid, {0.0F, 2.51F, 10.50F}, false},
        {ShipCrewStation::ForeStairsBottom, {0.0F, 1.01F, 7.50F}, false},
        {ShipCrewStation::CargoFish, {-1.30F, 1.01F, 20.0F}, false},
        {ShipCrewStation::CargoWater, {1.30F, 1.01F, 20.0F}, false},
        {ShipCrewStation::CargoSort, {0.0F, 1.01F, 27.0F}, false},
        {ShipCrewStation::CrewBunks, {0.0F, 1.01F, -5.50F}, false},
        {ShipCrewStation::MessTable, {0.0F, 1.01F, -1.50F}, false},
        {ShipCrewStation::ForeHatchPortA, {-2.05F, 4.01F, 6.50F}, true},
        {ShipCrewStation::ForeHatchPortB, {-2.05F, 4.01F, 14.50F}, true},
        {ShipCrewStation::HelmBypassPort, {-1.55F, 4.51F, -30.55F}, true},
        {ShipCrewStation::QuarterdeckStepTop, {-1.55F, 4.51F, -24.50F}, true},
        {ShipCrewStation::QuarterdeckStepBottom, {-1.55F, 4.01F, -23.50F}, true},
        {ShipCrewStation::ForecastleStepBottom, {-1.35F, 4.01F, 23.50F}, true},
        {ShipCrewStation::ForecastleStepTop, {-1.35F, 4.51F, 25.50F}, true},
        {ShipCrewStation::ForeStairsExitCenter, {0.0F, 4.01F, 14.55F}, true},
        {ShipCrewStation::ForeStairsExitPort, {-2.70F, 4.01F, 14.55F}, true},
        {ShipCrewStation::AftCabinDoor, {0.0F, 1.01F, -17.50F}, false},
        {ShipCrewStation::AftLowerPortA, {-1.65F, 1.01F, -15.50F}, false},
        {ShipCrewStation::AftLowerPortB, {-1.65F, 1.01F, -8.50F}, false},
        {ShipCrewStation::ForeLowerPortA, {-1.65F, 1.01F, 7.50F}, false},
        {ShipCrewStation::ForeLowerPortB, {-1.65F, 1.01F, 14.75F}, false},
        {ShipCrewStation::WaterStillApproach, {2.20F, 1.01F, 5.65F}, false},
    }};
    return nodes;
}

auto amelie_crew_navigation_edges() -> const std::array<ShipCrewNavigationEdge, 47>& {
    // Je décompose les changements de niveau et les contournements de trappe
    // en segments courts : aucun marin ne coupe une hiloire, une marche ou un
    // meuble pour rejoindre son poste.
    static const std::array<ShipCrewNavigationEdge, 47> edges {{
        {ShipCrewStation::Helm, ShipCrewStation::AftWatch},
        {ShipCrewStation::Helm, ShipCrewStation::HelmBypassPort},
        {ShipCrewStation::HelmBypassPort, ShipCrewStation::QuarterdeckStepTop},
        {ShipCrewStation::QuarterdeckStepTop, ShipCrewStation::QuarterdeckStepBottom},
        {ShipCrewStation::QuarterdeckStepBottom, ShipCrewStation::AftDeck},
        {ShipCrewStation::AftDeck, ShipCrewStation::MizzenMast},
        {ShipCrewStation::MizzenMast, ShipCrewStation::AftStairsTop},
        {ShipCrewStation::AftDeck, ShipCrewStation::PortFishing},
        {ShipCrewStation::AftDeck, ShipCrewStation::StarboardFishing},
        {ShipCrewStation::PortFishing, ShipCrewStation::MidDeckPort},
        {ShipCrewStation::StarboardFishing, ShipCrewStation::MidDeckStarboard},
        {ShipCrewStation::MidDeckPort, ShipCrewStation::MainMast},
        {ShipCrewStation::MidDeckStarboard, ShipCrewStation::MainMast},
        {ShipCrewStation::MainMast, ShipCrewStation::ForeHatchPortA},
        {ShipCrewStation::ForeHatchPortA, ShipCrewStation::ForeHatchPortB},
        {ShipCrewStation::ForeHatchPortB, ShipCrewStation::Capstan},
        {ShipCrewStation::ForeHatchPortB, ShipCrewStation::ForeMast},
        {ShipCrewStation::ForeStairsTop, ShipCrewStation::ForeStairsExitCenter},
        {ShipCrewStation::ForeStairsExitCenter, ShipCrewStation::ForeStairsExitPort},
        {ShipCrewStation::ForeStairsExitPort, ShipCrewStation::Capstan},
        {ShipCrewStation::Capstan, ShipCrewStation::ForeMast},
        {ShipCrewStation::ForeMast, ShipCrewStation::ForecastleStepBottom},
        {ShipCrewStation::ForecastleStepBottom, ShipCrewStation::ForecastleStepTop},
        {ShipCrewStation::ForecastleStepTop, ShipCrewStation::ForeDeck},
        {ShipCrewStation::AftStairsTop, ShipCrewStation::AftStairsMid},
        {ShipCrewStation::AftStairsMid, ShipCrewStation::AftStairsBottom},
        {ShipCrewStation::AftStairsBottom, ShipCrewStation::CrewBunks},
        {ShipCrewStation::CrewBunks, ShipCrewStation::MessTable},
        {ShipCrewStation::MessTable, ShipCrewStation::Galley},
        {ShipCrewStation::CaptainCabin, ShipCrewStation::ChartTable},
        {ShipCrewStation::CaptainCabin, ShipCrewStation::AftCabinDoor},
        {ShipCrewStation::AftCabinDoor, ShipCrewStation::AftLowerPortA},
        {ShipCrewStation::AftLowerPortA, ShipCrewStation::AftLowerPortB},
        {ShipCrewStation::AftLowerPortB, ShipCrewStation::CrewBunks},
        {ShipCrewStation::Galley, ShipCrewStation::WaterStillApproach},
        {ShipCrewStation::WaterStillApproach, ShipCrewStation::WaterStill},
        {ShipCrewStation::Galley, ShipCrewStation::ForeStairsBottom},
        {ShipCrewStation::ForeStairsBottom, ShipCrewStation::ForeStairsMid},
        {ShipCrewStation::ForeStairsMid, ShipCrewStation::ForeStairsTop},
        {ShipCrewStation::Galley, ShipCrewStation::ForeLowerPortA},
        {ShipCrewStation::ForeLowerPortA, ShipCrewStation::ForeLowerPortB},
        {ShipCrewStation::ForeLowerPortB, ShipCrewStation::CargoFish},
        {ShipCrewStation::ForeLowerPortB, ShipCrewStation::CargoWater},
        {ShipCrewStation::ForeLowerPortB, ShipCrewStation::CargoSort},
        {ShipCrewStation::CargoFish, ShipCrewStation::CargoSort},
        {ShipCrewStation::CargoWater, ShipCrewStation::CargoSort},
        {ShipCrewStation::CargoFish, ShipCrewStation::CargoWater},
    }};
    return edges;
}

auto amelie_half_width(float z) noexcept -> float {
    // Je fais du profil partage l'unique source de verite de la coque afin
    // qu'une retouche de silhouette mette aussi a jour ses volumes proteges.
    return kAmelieProtectionProfile.half_width_at(z);
}

auto is_cabin_window_row(int z) noexcept -> bool {
    return z == -32 || z == -29 || z == -26 || z == -23 || z == -20;
}

auto is_main_deck_hatch_row(int z) noexcept -> bool {
    // Je prolonge chaque ouverture d'une rangee cote pont inferieur : le joueur
    // conserve ainsi ses 1,80 m de hauteur pendant l'approche de la demi-marche.
    return (z >= -15 && z <= -9) || (z >= 7 && z <= 13);
}

void add_hull_and_decks(std::vector<ShipPart>& parts) {
    // Je donne une vraie quille a la coque agrandie, sans modifier l'echelle des
    // portes, du mobilier ou des marches qui restent dimensionnes pour le joueur.
    add_box(parts,
            ShipMaterial::DarkHull,
            {-0.32F, -1.72F, kAmelieSternZ - 0.50F},
            {0.32F, 0.18F, kAmelieBowZ + 0.50F},
            true);

    for (int z = -35; z <= 35; ++z) {
        const auto row_min_z = static_cast<float>(z);
        const auto row_max_z = row_min_z + 1.0F;
        const auto half_width = amelie_half_width(row_min_z + 0.5F);
        const auto wall_thickness = std::min(0.48F, half_width * 0.24F);
        const auto inner_half_width = std::max(0.55F, half_width - wall_thickness);

        // Je construis trois bandes de coque : quille et bouchains etroits,
        // flanc intermediaire, puis muraille haute. La silhouette se referme
        // sous la flottaison au lieu de descendre verticalement jusqu'a la quille.
        const auto lower_half_width = std::max(0.70F, half_width - 1.25F);
        const auto middle_half_width = std::max(0.80F, half_width - 0.62F);
        add_box(parts,
                ShipMaterial::DarkHull,
                {-lower_half_width, -1.42F, row_min_z},
                {-lower_half_width + wall_thickness, -0.62F, row_max_z},
                true);
        add_box(parts,
                ShipMaterial::DarkHull,
                {lower_half_width - wall_thickness, -1.42F, row_min_z},
                {lower_half_width, -0.62F, row_max_z},
                true);
        add_box(parts,
                ShipMaterial::DarkHull,
                {-middle_half_width, -0.62F, row_min_z},
                {-middle_half_width + wall_thickness, 1.18F, row_max_z},
                true);
        add_box(parts,
                ShipMaterial::DarkHull,
                {middle_half_width - wall_thickness, -0.62F, row_min_z},
                {middle_half_width, 1.18F, row_max_z},
                true);

        // Je prolonge le pont inferieur jusque dans la derniere rangee de la
        // cabine. Il rejoint maintenant directement le tableau arriere : le
        // trou de trois metres de l'ancienne poupe ne peut plus exister.
        if (z >= -35 && z <= 33) {
            add_box(parts,
                    ShipMaterial::LightDeck,
                    {-inner_half_width, 0.0F, row_min_z},
                    {inner_half_width, 1.0F, row_max_z},
                    true,
                    true);
        }

        const auto add_shell_side = [&](float min_x, float max_x) {
            if (is_cabin_window_row(z)) {
                add_box(parts,
                        ShipMaterial::DarkHull,
                        {min_x, 0.82F, row_min_z},
                        {max_x, 1.52F, row_max_z},
                        true);
                add_box(parts,
                        ShipMaterial::DarkHull,
                        {min_x, 2.62F, row_min_z},
                        {max_x, 3.82F, row_max_z},
                        true);
                add_panel(parts,
                          ShipMaterial::Glass,
                          {min_x, 1.52F, row_min_z + 0.12F},
                          {max_x, 2.62F, row_max_z - 0.12F},
                          min_x < 0.0F ? glm::vec3 {-1.0F, 0.0F, 0.0F}
                                       : glm::vec3 {1.0F, 0.0F, 0.0F},
                          0.05F,
                          true);
                add_box(parts,
                        ShipMaterial::CleanBeam,
                        {min_x, 1.52F, row_min_z},
                        {max_x, 2.62F, row_min_z + 0.14F},
                        true);
                add_box(parts,
                        ShipMaterial::CleanBeam,
                        {min_x, 1.52F, row_max_z - 0.14F},
                        {max_x, 2.62F, row_max_z},
                        true);
                return;
            }
            add_box(parts,
                    ShipMaterial::DarkHull,
                    {min_x, 0.82F, row_min_z},
                    {max_x, 3.82F, row_max_z},
                    true);
        };
        add_shell_side(-half_width, -half_width + wall_thickness);
        add_shell_side(half_width - wall_thickness, half_width);

        if (z >= -35 && z <= 35) {
            const auto deck_half_width = std::max(0.55F, half_width - 0.12F);
            if (is_main_deck_hatch_row(z)) {
                add_box(parts,
                        ShipMaterial::LightDeck,
                        {-deck_half_width, kAmelieMainDeckUnderside, row_min_z},
                        {-1.22F, 4.0F, row_max_z},
                        true,
                        true);
                add_box(parts,
                        ShipMaterial::LightDeck,
                        {1.22F, kAmelieMainDeckUnderside, row_min_z},
                        {deck_half_width, 4.0F, row_max_z},
                        true,
                        true);
            } else {
                add_box(parts,
                        ShipMaterial::LightDeck,
                        {-deck_half_width, kAmelieMainDeckUnderside, row_min_z},
                        {deck_half_width, kAmelieMainDeckTop, row_max_z},
                        true,
                        true);
            }
        }

        const auto raised_deck = z <= -25 || z >= 25;
        if (!raised_deck) {
            const auto rail_x = std::max(0.6F, half_width - 0.10F);
            const auto boarding_row =
                z >= kAmelieBoardingMinRowZ && z <= kAmelieBoardingMaxRowZ;
            if (!boarding_row) {
                add_box(parts,
                        ShipMaterial::CleanBeam,
                        {-rail_x, 4.46F, row_min_z},
                        {-rail_x + 0.14F, 4.66F, row_max_z},
                        true);
                add_box(parts,
                        ShipMaterial::CleanBeam,
                        {rail_x - 0.14F, 4.46F, row_min_z},
                        {rail_x, 4.66F, row_max_z},
                        true);
            }
            if ((z + 35) % 3 == 0) {
                if (!boarding_row) {
                    add_box(parts,
                            ShipMaterial::CleanBeam,
                            {-rail_x, 4.0F, row_min_z},
                            {-rail_x + 0.16F, 5.02F, row_min_z + 0.16F},
                            true);
                    add_box(parts,
                            ShipMaterial::CleanBeam,
                            {rail_x - 0.16F, 4.0F, row_min_z},
                            {rail_x, 5.02F, row_min_z + 0.16F},
                            true);
                }
            }
        }
    }

    // Je prolonge le pont jusqu'aux deux filets. La levre babord conserve sa
    // jonction avec la passerelle et sa jumelle tribord securise la remontee.
    add_box(parts,
            ShipMaterial::LightDeck,
            {-kAmelieBoardingOuterX,
             kAmelieMainDeckUnderside,
             static_cast<float>(kAmelieBoardingMinRowZ)},
            {-7.75F,
             kAmelieMainDeckTop,
             static_cast<float>(kAmelieBoardingMaxRowZ + 1)},
            true,
            true,
            ShipPartShape::Slab);
    add_box(parts,
            ShipMaterial::LightDeck,
            {7.75F,
             kAmelieMainDeckUnderside,
             static_cast<float>(kAmelieBoardingMinRowZ)},
            {kAmelieBoardingOuterX,
             kAmelieMainDeckTop,
             static_cast<float>(kAmelieBoardingMaxRowZ + 1)},
            true,
            true,
            ShipPartShape::Slab);

    // Je garde une seule piece logique par cote : le rendu la decompose en
    // mailles, tandis que la physique conserve une surface de prise continue.
    add_climbable_net(parts, -kAmelieBoardingNetX, {-1.0F, 0.0F, 0.0F});
    add_climbable_net(parts, kAmelieBoardingNetX, {1.0F, 0.0F, 0.0F});

    // Je ferme l'etrave avec un taille-mer et je compose le tableau arriere en
    // plusieurs bandes. Les vitrages ferment reellement la cabine et remplacent
    // l'ancien grand cube sombre pose sur toute la poupe.
    add_box(parts,
            ShipMaterial::DarkHull,
            {-0.95F, -0.62F, kAmelieBowZ - 0.05F},
            {0.95F, 3.82F, kAmelieBowZ + 0.45F},
            true);
    constexpr float stern_inner_half_width = 6.30F;
    add_box(parts,
            ShipMaterial::DarkHull,
            {-stern_inner_half_width, -0.62F, kAmelieSternZ - 0.45F},
            {stern_inner_half_width, 1.30F, kAmelieSternZ},
            true);
    add_box(parts,
            ShipMaterial::DarkHull,
            {-stern_inner_half_width, 2.78F, kAmelieSternZ - 0.45F},
            {stern_inner_half_width, 3.82F, kAmelieSternZ},
            true);
    constexpr std::array<glm::vec2, 4> transom_windows {{
        {-4.90F, -2.75F},
        {-2.35F, -0.20F},
        {0.20F, 2.35F},
        {2.75F, 4.90F},
    }};
    add_box(parts,
            ShipMaterial::DarkHull,
            {-stern_inner_half_width, 1.30F, kAmelieSternZ - 0.45F},
            {-5.15F, 2.78F, kAmelieSternZ},
            true);
    add_box(parts,
            ShipMaterial::DarkHull,
            {5.15F, 1.30F, kAmelieSternZ - 0.45F},
            {stern_inner_half_width, 2.78F, kAmelieSternZ},
            true);
    for (const auto& window : transom_windows) {
        add_panel(parts,
                  ShipMaterial::Glass,
                  {window.x, 1.48F, kAmelieSternZ - 0.49F},
                  {window.y, 2.60F, kAmelieSternZ - 0.47F},
                  {0.0F, 0.0F, -1.0F},
                  0.06F,
                  true);
        add_box(parts,
                ShipMaterial::CleanBeam,
                {window.x - 0.12F, 1.30F, kAmelieSternZ - 0.50F},
                {window.x + 0.05F, 2.78F, kAmelieSternZ - 0.42F},
                true);
        add_box(parts,
                ShipMaterial::CleanBeam,
                {window.y - 0.05F, 1.30F, kAmelieSternZ - 0.50F},
                {window.y + 0.12F, 2.78F, kAmelieSternZ - 0.42F},
                true);
    }
    for (const auto& mullion : std::array<glm::vec2, 3> {{
             {-2.75F, -2.35F},
             {-0.20F, 0.20F},
             {2.35F, 2.75F},
         }}) {
        add_box(parts,
                ShipMaterial::CleanBeam,
                {mullion.x, 1.30F, kAmelieSternZ - 0.50F},
                {mullion.y, 2.78F, kAmelieSternZ - 0.42F},
                true);
    }
    add_box(parts,
            ShipMaterial::CleanBeam,
            {-5.15F, 1.30F, kAmelieSternZ - 0.50F},
            {5.15F, 1.48F, kAmelieSternZ - 0.42F},
            true);
    add_box(parts,
            ShipMaterial::CleanBeam,
            {-5.15F, 2.60F, kAmelieSternZ - 0.50F},
            {5.15F, 2.78F, kAmelieSternZ - 0.42F},
            true);

    // Je pose la dunette et le gaillard sur un vrai demi-niveau solidaire du
    // pont principal, puis j'adapte leur largeur a chaque tranche de coque.
    add_box(parts, ShipMaterial::LightDeck, {-1.10F, 4.0F, -25.0F}, {1.10F, 4.50F, -24.0F}, true, true, ShipPartShape::Stair,
            {0.0F, 0.0F, -1.0F});
    add_box(parts, ShipMaterial::LightDeck, {-1.10F, 4.0F, 24.0F}, {1.10F, 4.50F, 25.0F}, true, true, ShipPartShape::Stair,
            {0.0F, 0.0F, 1.0F});

    for (int z = 25; z <= 34; ++z) {
        const auto row_min_z = static_cast<float>(z);
        const auto deck_half_width = std::max(0.70F, amelie_half_width(row_min_z + 0.5F) - 0.12F);
        add_box(parts,
                ShipMaterial::LightDeck,
                {-deck_half_width, 4.0F, row_min_z},
                {deck_half_width, 4.50F, row_min_z + 1.0F},
                true,
                true,
                ShipPartShape::Slab);
        add_box(parts,
                ShipMaterial::CleanBeam,
                {-deck_half_width, 4.98F, row_min_z},
                {-deck_half_width + 0.15F, 5.18F, row_min_z + 1.0F},
                true);
        add_box(parts,
                ShipMaterial::CleanBeam,
                {deck_half_width - 0.15F, 4.98F, row_min_z},
                {deck_half_width, 5.18F, row_min_z + 1.0F},
                true);
        if ((z - 25) % 2 == 0) {
            add_box(parts,
                    ShipMaterial::CleanBeam,
                    {-deck_half_width, 4.50F, row_min_z},
                    {-deck_half_width + 0.15F, 5.50F, row_min_z + 0.16F},
                    true);
            add_box(parts,
                    ShipMaterial::CleanBeam,
                    {deck_half_width - 0.15F, 4.50F, row_min_z},
                    {deck_half_width, 5.50F, row_min_z + 0.16F},
                    true);
        }
    }

    for (int z = -35; z <= -25; ++z) {
        const auto row_min_z = static_cast<float>(z);
        const auto deck_half_width = amelie_half_width(row_min_z + 0.5F) - 0.12F;
        add_box(parts,
                ShipMaterial::LightDeck,
                {-deck_half_width, 4.0F, row_min_z},
                {deck_half_width, 4.50F, row_min_z + 1.0F},
                true,
                true,
                ShipPartShape::Slab);
        add_box(parts,
                ShipMaterial::CleanBeam,
                {-deck_half_width, 4.98F, row_min_z},
                {-deck_half_width + 0.15F, 5.18F, row_min_z + 1.0F},
                true);
        add_box(parts,
                ShipMaterial::CleanBeam,
                {deck_half_width - 0.15F, 4.98F, row_min_z},
                {deck_half_width, 5.18F, row_min_z + 1.0F},
                true);
        if ((z + 35) % 2 == 0) {
            add_box(parts,
                    ShipMaterial::CleanBeam,
                    {-deck_half_width, 4.50F, row_min_z},
                    {-deck_half_width + 0.15F, 5.50F, row_min_z + 0.16F},
                    true);
            add_box(parts,
                    ShipMaterial::CleanBeam,
                    {deck_half_width - 0.15F, 4.50F, row_min_z},
                    {deck_half_width, 5.50F, row_min_z + 0.16F},
                    true);
        }
    }

    // Je ferme réellement le couronnement de poupe : ce bastingage transversal
    // prolonge les deux rambardes latérales et empêche une chute au centre du
    // tableau, sans masquer la plaque nominative placée juste en dessous.
    const auto stern_rail_half_width = amelie_half_width(kAmelieSternZ + 0.5F) - 0.12F;
    add_box(parts,
            ShipMaterial::CleanBeam,
            {-stern_rail_half_width, 4.98F, kAmelieSternZ},
            {stern_rail_half_width, 5.18F, kAmelieSternZ + 0.16F},
            true);
    for (float x = -stern_rail_half_width; x <= stern_rail_half_width + 0.01F; x += 2.05F) {
        add_box(parts,
                ShipMaterial::CleanBeam,
                {x, 4.50F, kAmelieSternZ},
                {std::min(x + 0.15F, stern_rail_half_width), 5.50F, kAmelieSternZ + 0.16F},
                true);
    }

    // Je termine la poupe par une galerie et deux courbes decoratives. Ces
    // pieces donnent de la profondeur au tableau sans creer de sol accessible
    // hors de la coque ni de collision invisible pour le joueur.
    add_box(parts,
            ShipMaterial::LightDeck,
            {-5.55F, 3.84F, kAmelieSternZ - 0.78F},
            {5.55F, 4.08F, kAmelieSternZ - 0.42F},
            false,
            false,
            ShipPartShape::Slab);
    add_segment(parts,
                ShipMaterial::Brass,
                {-5.70F, 3.92F, kAmelieSternZ - 0.84F},
                {5.70F, 3.92F, kAmelieSternZ - 0.84F},
                0.10F);
    add_segment(parts,
                ShipMaterial::CleanBeam,
                {-6.12F, 1.10F, kAmelieSternZ - 0.52F},
                {-5.55F, 4.95F, kAmelieSternZ - 0.68F},
                0.18F);
    add_segment(parts,
                ShipMaterial::CleanBeam,
                {6.12F, 1.10F, kAmelieSternZ - 0.52F},
                {5.55F, 4.95F, kAmelieSternZ - 0.68F},
                0.18F);
}

void add_accesses_and_interior(std::vector<ShipPart>& parts) {
    const auto add_six_step_stair = [&](float first_z, bool ascends_positive_z) {
        for (int step = 0; step < 6; ++step) {
            const auto progression = ascends_positive_z ? step : 5 - step;
            const auto min_y = 1.0F;
            const auto max_y = 1.5F + static_cast<float>(progression) * 0.5F;
            const auto z = first_z + static_cast<float>(step);
            add_box(parts,
                    ShipMaterial::LightDeck,
                    {-1.05F, min_y, z},
                    {1.05F, max_y, z + 1.0F},
                    true,
                    true,
                    ShipPartShape::Stair,
                    {0.0F, 0.0F, ascends_positive_z ? 1.0F : -1.0F});
        }
    };
    add_six_step_stair(-15.0F, false);
    add_six_step_stair(8.0F, true);

    const auto add_partition_with_door = [&](float z, float half_width, float door_half_width = 1.15F) {
        add_box(parts,
                ShipMaterial::CleanBeam,
                {-half_width, 1.0F, z},
                {-door_half_width, kAmelieMainDeckUnderside, z + 0.22F},
                true);
        add_box(parts,
                ShipMaterial::CleanBeam,
                {door_half_width, 1.0F, z},
                {half_width, kAmelieMainDeckUnderside, z + 0.22F},
                true);
        add_box(parts,
                ShipMaterial::CleanBeam,
                {-door_half_width, 3.35F, z},
                {door_half_width, kAmelieMainDeckUnderside, z + 0.22F},
                true);
        add_box(parts,
                ShipMaterial::Brass,
                {-door_half_width - 0.08F, 1.0F, z - 0.04F},
                {-door_half_width + 0.08F, 3.35F, z + 0.26F},
                false);
        add_box(parts,
                ShipMaterial::Brass,
                {door_half_width - 0.08F, 1.0F, z - 0.04F},
                {door_half_width + 0.08F, 3.35F, z + 0.26F},
                false);
    };
    add_partition_with_door(-18.0F, amelie_half_width(-18.0F) - 0.52F);
    add_partition_with_door(2.0F, amelie_half_width(2.0F) - 0.52F);
    // Je place la cloison après la dernière marche : son linteau ne peut ainsi
    // plus couper le volume de tête du joueur pendant la montée avant.
    add_partition_with_door(14.35F, amelie_half_width(14.35F) - 0.52F, 2.10F);

    // Je meuble la cabine agrandie comme une vraie piece de commandement. Le
    // centre reste libre depuis la porte jusqu'aux fenetres du tableau arriere.
    add_box(parts,
            ShipMaterial::LightDeck,
            {-5.35F, 1.0F, -31.7F},
            {-2.05F, 1.58F, -26.4F},
            true,
            true);
    add_panel(parts,
              ShipMaterial::CreamCanvas,
              {-5.10F, 1.59F, -31.35F},
              {-2.30F, 1.63F, -26.75F},
              {0.0F, 1.0F, 0.0F},
              0.04F);
    add_box(parts,
            ShipMaterial::CleanBeam,
            {1.65F, 1.0F, -29.6F},
            {4.75F, 1.78F, -26.8F},
            true,
            true);
    add_panel(parts,
              ShipMaterial::LightDeck,
              {1.90F, 1.79F, -29.35F},
              {4.50F, 1.83F, -27.05F},
              {0.0F, 1.0F, 0.0F},
              0.04F);
    add_panel(parts,
              ShipMaterial::CreamCanvas,
              {2.10F, 1.84F, -29.10F},
              {4.30F, 1.88F, -27.30F},
              {0.0F, 1.0F, 0.0F},
              0.04F);
    add_box(parts,
            ShipMaterial::CleanBeam,
            {5.10F, 1.0F, -24.8F},
            {5.58F, 3.08F, -18.4F},
            true);
    for (int shelf = 0; shelf < 3; ++shelf) {
        const auto y = 1.34F + static_cast<float>(shelf) * 0.62F;
        add_box(parts,
                ShipMaterial::LightDeck,
                {4.88F, y, -24.65F},
                {5.62F, y + 0.12F, -18.55F},
                false);
    }
    add_box(parts,
            ShipMaterial::CleanBeam,
            {-5.20F, 1.0F, -24.4F},
            {-3.25F, 1.82F, -22.7F},
            true,
            true);
    add_box(parts,
            ShipMaterial::Brass,
            {-5.05F, 1.82F, -24.25F},
            {-3.40F, 1.94F, -22.85F},
            false);
    add_panel(parts,
              ShipMaterial::CreamCanvas,
              {-0.82F, 1.01F, -32.5F},
              {0.82F, 1.05F, -19.0F},
              {0.0F, 1.0F, 0.0F},
              0.04F);

    // Je distribue couchettes, cuisine et cargaison sur les flancs pour garder
    // un passage central continu d'au moins deux metres de haut.
    for (int bunk = 0; bunk < 3; ++bunk) {
        const auto z = -14.5F + static_cast<float>(bunk) * 4.2F;
        add_box(parts, ShipMaterial::LightDeck, {-6.35F, 1.0F, z}, {-2.55F, 1.45F, z + 2.3F}, true, true);
        add_box(parts, ShipMaterial::LightDeck, {2.55F, 1.0F, z}, {6.35F, 1.45F, z + 2.3F}, true, true);
        add_panel(parts,
                  ShipMaterial::CreamCanvas,
                  {-6.10F, 1.46F, z + 0.18F},
                  {-2.80F, 1.50F, z + 2.12F},
                  {0.0F, 1.0F, 0.0F},
                  0.04F);
        add_panel(parts,
                  ShipMaterial::CreamCanvas,
                  {2.80F, 1.46F, z + 0.18F},
                  {6.10F, 1.50F, z + 2.12F},
                  {0.0F, 1.0F, 0.0F},
                  0.04F);
    }
    add_box(parts, ShipMaterial::Iron, {-6.10F, 1.0F, 3.2F}, {-2.35F, 1.65F, 7.8F}, true, true);
    add_box(parts, ShipMaterial::CleanBeam, {-5.55F, 1.65F, 4.0F}, {-2.90F, 2.15F, 6.8F}, false);
    add_segment(parts, ShipMaterial::Iron, {-4.20F, 2.15F, 5.4F}, {-4.20F, 5.6F, 5.4F}, 0.38F);
    add_box(parts, ShipMaterial::LightDeck, {2.65F, 1.0F, 3.2F}, {5.85F, 1.45F, 5.2F}, true, true);
    add_box(parts, ShipMaterial::LightDeck, {2.65F, 1.0F, 6.0F}, {5.85F, 1.45F, 8.0F}, true, true);

    // Je materialise le dispositif d'eau par un reservoir et un condenseur en
    // cuivre, sans l'etirer avec la coque ni encombrer le passage central.
    add_box(parts,
            ShipMaterial::Brass,
            {5.10F, 1.45F, 3.55F},
            {5.72F, 2.55F, 4.75F},
            false);
    add_segment(parts,
                ShipMaterial::Brass,
                {5.40F, 2.50F, 4.10F},
                {4.35F, 2.82F, 6.85F},
                0.10F);
    add_box(parts,
            ShipMaterial::Iron,
            {4.00F, 1.45F, 6.15F},
            {4.75F, 2.05F, 7.55F},
            false);

    for (int z = 15; z <= 30; z += 4) {
        const auto center_z = static_cast<float>(z) + 1.25F;
        const auto outer_x = std::min(4.8F, amelie_half_width(center_z) - 0.55F);
        auto inner_x = std::max(0.85F, outer_x - 2.40F);
        if (z == 15) {
            // Je reserve la continuite du passage lateral jusqu'au bout de
            // l'escalier avant avant de refermer le couloir central de cale.
            inner_x = std::min(outer_x - 0.20F, std::max(inner_x, 2.05F));
        }
        add_box(parts, ShipMaterial::LightDeck, {-outer_x, 1.0F, static_cast<float>(z)}, {-inner_x, 2.25F, static_cast<float>(z) + 2.5F}, true, true);
        add_box(parts, ShipMaterial::LightDeck, {inner_x, 1.0F, static_cast<float>(z)}, {outer_x, 2.25F, static_cast<float>(z) + 2.5F}, true, true);
        const auto rope_x = (inner_x + outer_x) * 0.5F;
        add_segment(parts, ShipMaterial::Rope, {-rope_x, 2.28F, center_z}, {-rope_x, 2.75F, center_z}, 0.08F);
        add_segment(parts, ShipMaterial::Rope, {rope_x, 2.28F, center_z}, {rope_x, 2.75F, center_z}, 0.08F);
    }

    // Je remonte les barrots sous le pont aminci. Il reste 2,65 m en partie
    // courante et au moins 2,35 m sous chaque poutre, ce qui convient aussi aux
    // silhouettes d'equipage les plus grandes.
    for (int z = -32; z <= 32; z += 4) {
        const auto beam_z = static_cast<float>(z);
        const auto beam_half_width = std::max(1.20F, amelie_half_width(beam_z) - 0.60F);
        add_box(parts,
                ShipMaterial::CleanBeam,
                {-beam_half_width, 3.35F, beam_z},
                {beam_half_width, 3.55F, beam_z + 0.18F},
                false);
    }
    add_panel(parts, ShipMaterial::CreamCanvas, {-0.72F, 1.01F, -5.0F}, {0.72F, 1.05F, -1.0F}, {0.0F, 1.0F, 0.0F}, 0.04F);

    // Je borde les deux trappes avec des hiloires et des rambardes basses. Les
    // montants restent hors des 2,10 m utiles de l'escalier et ne ferment aucune
    // extremite du passage.
    const auto add_hatch_coaming = [&](float min_z, float max_z) {
        add_box(parts,
                ShipMaterial::CleanBeam,
                {-1.42F, 4.0F, min_z},
                {-1.22F, 4.34F, max_z},
                true);
        add_box(parts,
                ShipMaterial::CleanBeam,
                {1.22F, 4.0F, min_z},
                {1.42F, 4.34F, max_z},
                true);
        add_segment(parts,
                    ShipMaterial::CleanBeam,
                    {-1.34F, 4.88F, min_z + 0.12F},
                    {-1.34F, 4.88F, max_z - 0.12F},
                    0.12F);
        add_segment(parts,
                    ShipMaterial::CleanBeam,
                    {1.34F, 4.88F, min_z + 0.12F},
                    {1.34F, 4.88F, max_z - 0.12F},
                    0.12F);
        for (const auto z : {min_z + 0.12F, max_z - 0.12F}) {
            add_box(parts,
                    ShipMaterial::CleanBeam,
                    {-1.42F, 4.0F, z - 0.07F},
                    {-1.26F, 4.92F, z + 0.07F},
                    true);
            add_box(parts,
                    ShipMaterial::CleanBeam,
                    {1.26F, 4.0F, z - 0.07F},
                    {1.42F, 4.92F, z + 0.07F},
                    true);
        }
    };
    add_hatch_coaming(-15.0F, -8.0F);
    add_hatch_coaming(7.0F, 14.0F);

    // Je suspends les lanternes hors des volumes de passage et au-dessus du
    // mobilier : les ancres de chargement ne traversent plus leur geometrie.
    for (const auto& lantern : kAmelieInteriorLanterns) {
        add_segment(parts,
                    ShipMaterial::Rope,
                    {lantern.x, 3.20F, lantern.z},
                    {lantern.x, 3.43F, lantern.z},
                    0.04F);
        add_box(parts,
                ShipMaterial::Lantern,
                {lantern.x - 0.16F, 2.85F, lantern.z - 0.16F},
                {lantern.x + 0.16F, 3.20F, lantern.z + 0.16F},
                false);
    }
}

void add_deck_equipment(std::vector<ShipPart>& parts) {
    // Je place la barre, son pied et le compas sur la dunette.
    add_box(parts, ShipMaterial::CleanBeam, {-0.20F, 4.50F, -29.25F}, {0.20F, 5.85F, -28.85F}, true, true);
    parts.push_back({
        ShipPartShape::Wheel,
        ShipMaterial::CleanBeam,
        {-1.05F, 5.05F, -29.35F},
        {1.05F, 7.15F, -29.15F},
        {0.0F, 0.0F, 1.0F},
        0.13F,
        false,
        false,
        U'\0',
    });
    // Je construis un vrai habitacle de compas : pied en bois, cuvette de
    // laiton et rose vitrée, au lieu d'un gros cube doré sans fonction lisible.
    add_box(parts,
            ShipMaterial::CleanBeam,
            {1.90F, 4.50F, -29.88F},
            {2.30F, 4.92F, -29.42F},
            true,
            true);
    add_box(parts,
            ShipMaterial::Brass,
            {1.78F, 4.90F, -29.98F},
            {2.42F, 5.08F, -29.32F},
            true,
            true);
    add_panel(parts,
              ShipMaterial::Glass,
              {1.86F, 5.08F, -29.90F},
              {2.34F, 5.12F, -29.40F},
              {0.0F, 1.0F, 0.0F},
              0.04F);
    add_segment(parts, ShipMaterial::Iron, {2.10F, 5.13F, -29.86F}, {2.10F, 5.13F, -29.44F}, 0.025F);
    add_segment(parts, ShipMaterial::Iron, {1.90F, 5.13F, -29.65F}, {2.30F, 5.13F, -29.65F}, 0.025F);

    // Je garde le cabestan et les panneaux de cale hors des axes de circulation.
    add_box(parts, ShipMaterial::CleanBeam, {-1.15F, 4.0F, 15.0F}, {1.15F, 4.65F, 16.2F}, true, true);
    add_segment(parts, ShipMaterial::CleanBeam, {-2.15F, 4.48F, 15.6F}, {2.15F, 4.48F, 15.6F}, 0.16F);
    add_box(parts, ShipMaterial::CleanBeam, {-3.8F, 4.0F, -4.5F}, {-2.0F, 4.55F, -1.5F}, true, true);
    add_box(parts, ShipMaterial::CleanBeam, {2.0F, 4.0F, -4.5F}, {3.8F, 4.55F, -1.5F}, true, true);

    // Je pose une embarcation legere sur tribord et je laisse le centre libre.
    add_panel(parts, ShipMaterial::DarkHull, {4.20F, 4.18F, 0.5F}, {5.90F, 5.25F, 8.5F}, {1.0F, 0.0F, 0.0F}, 0.14F);
    add_segment(parts, ShipMaterial::CleanBeam, {4.45F, 4.35F, 0.9F}, {5.65F, 4.35F, 8.1F}, 0.10F);
    add_segment(parts, ShipMaterial::CleanBeam, {5.65F, 4.35F, 0.9F}, {4.45F, 4.35F, 8.1F}, 0.10F);

    for (const float z : {-22.0F, -4.0F, 13.5F, 34.0F}) {
        // Je suis la largeur reelle du pont au lieu de laisser les fanaux de
        // proue flotter a plusieurs metres hors de la coque effilee.
        const auto deck_half_width = std::max(0.70F, amelie_half_width(z + 0.5F) - 0.12F);
        const auto lantern_outer_x = std::max(0.45F, deck_half_width - 0.08F);
        const auto lantern_inner_x = std::max(0.15F, lantern_outer_x - 0.30F);
        add_box(parts,
                ShipMaterial::Lantern,
                {-lantern_outer_x, 4.35F, z - 0.14F},
                {-lantern_inner_x, 4.90F, z + 0.14F},
                false);
        add_box(parts,
                ShipMaterial::Lantern,
                {lantern_inner_x, 4.35F, z - 0.14F},
                {lantern_outer_x, 4.90F, z + 0.14F},
                false);
    }
}

constexpr float kBlackSailThickness = 0.07F;
constexpr float kGoldSailTrimDiameter = 0.045F;

void add_black_square_sail(
    std::vector<ShipPart>& parts,
    float mast_z,
    float bottom_y,
    float top_y,
    float bottom_half_width) {

    const auto top_half_width =
        bottom_half_width * 0.78F;

    const auto panel_min_z =
        mast_z + 0.16F;

    const auto panel_max_z =
        mast_z + 0.25F;

    const auto trim_z =
        mast_z + 0.255F;

    // Le panneau conserve exactement l'enveloppe de l'ancienne voile.
    // Le cadre doré se place légèrement devant la toile et reste décoratif.
    add_panel(
        parts,
        ShipMaterial::BlackCanvas,
        {
            -bottom_half_width,
            bottom_y,
            panel_min_z,
        },
        {
            bottom_half_width,
            top_y,
            panel_max_z,
        },
        {0.0F, 0.0F, 1.0F},
        kBlackSailThickness);

    // Bord inférieur.
    add_segment(
        parts,
        ShipMaterial::SolidGold,
        {
            -bottom_half_width,
            bottom_y,
            trim_z,
        },
        {
            bottom_half_width,
            bottom_y,
            trim_z,
        },
        kGoldSailTrimDiameter);

    // Bord supérieur.
    add_segment(
        parts,
        ShipMaterial::SolidGold,
        {
            -top_half_width,
            top_y,
            trim_z,
        },
        {
            top_half_width,
            top_y,
            trim_z,
        },
        kGoldSailTrimDiameter);

    // Bord latéral gauche.
    add_segment(
        parts,
        ShipMaterial::SolidGold,
        {
            -bottom_half_width,
            bottom_y,
            trim_z,
        },
        {
            -top_half_width,
            top_y,
            trim_z,
        },
        kGoldSailTrimDiameter);

    // Bord latéral droit.
    add_segment(
        parts,
        ShipMaterial::SolidGold,
        {
            bottom_half_width,
            bottom_y,
            trim_z,
        },
        {
            top_half_width,
            top_y,
            trim_z,
        },
        kGoldSailTrimDiameter);
}

void add_black_triangular_sail(
    std::vector<ShipPart>& parts,
    const glm::vec3& min_corner,
    const glm::vec3& max_corner,
    const glm::vec3& normal) {

    add_panel(
        parts,
        ShipMaterial::BlackCanvas,
        min_corner,
        max_corner,
        normal,
        kBlackSailThickness);

    // Je reproduis les trois sommets utilisés par le maillage afin que le
    // liseré suive réellement le triangle sans flotter à côté de la voile.
    const auto sign =
        normal.x >= 0.0F
            ? 1.0F
            : -1.0F;

    const auto center_x =
        (min_corner.x + max_corner.x) *
        0.5F;

    const auto trim_x =
        center_x +
        sign *
            (kBlackSailThickness * 0.5F + 0.01F);

    const auto apex_z =
        sign > 0.0F
            ? min_corner.z
            : max_corner.z;

    const auto first = glm::vec3 {
        trim_x,
        min_corner.y,
        min_corner.z,
    };

    const auto second = glm::vec3 {
        trim_x,
        min_corner.y,
        max_corner.z,
    };

    const auto apex = glm::vec3 {
        trim_x,
        max_corner.y,
        apex_z,
    };

    add_segment(
        parts,
        ShipMaterial::SolidGold,
        first,
        second,
        kGoldSailTrimDiameter);

    add_segment(
        parts,
        ShipMaterial::SolidGold,
        second,
        apex,
        kGoldSailTrimDiameter);

    add_segment(
        parts,
        ShipMaterial::SolidGold,
        apex,
        first,
        kGoldSailTrimDiameter);
}

void add_gold_mast_collar(
    std::vector<ShipPart>& parts,
    float mast_z,
    float center_y,
    float diameter) {

    // Une courte surépaisseur casse la silhouette trop uniforme du mât sans
    // modifier sa collision, son axe ou son emplacement historique.
    add_segment(
        parts,
        ShipMaterial::SolidGold,
        {
            0.0F,
            center_y - 0.10F,
            mast_z,
        },
        {
            0.0F,
            center_y + 0.10F,
            mast_z,
        },
        diameter);
}

void add_main_mast_engraving(
    std::vector<ShipPart>& parts) {

    constexpr std::array<char32_t, 8> name {
        U'L',
        U'\'',
        U'a',
        U'm',
        U'\u00E9',
        U'l',
        U'i',
        U'e',
    };

    constexpr float kTopY = 18.95F;
    constexpr float kGlyphHeight = 0.52F;
    constexpr float kGlyphAdvance = 0.63F;
    constexpr float kGlyphHalfWidth = 0.18F;

    // Les glyphes pénètrent légèrement la face arrière du grand mât.
    // Seule leur surface sombre reste visible, simulant une incision remplie
    // d'ombre plutôt que des lettres simplement collées sur l'or.
    for (std::size_t index = 0;
         index < name.size();
         ++index) {

        const auto max_y =
            kTopY -
            static_cast<float>(index) *
                kGlyphAdvance;

        const auto min_y =
            max_y -
            kGlyphHeight;

        add_glyph(
            parts,
            name[index],
            {
                -kGlyphHalfWidth,
                min_y,
                -0.232F,
            },
            {
                kGlyphHalfWidth,
                max_y,
                -0.205F,
            },
            ShipMaterial::Iron);
    }
}

void add_rigging(std::vector<ShipPart>& parts) {
    struct MastDefinition {
        float z = 0.0F;
        float top = 0.0F;
        float lower_yard = 0.0F;
        float upper_yard = 0.0F;
        float lower_half_width = 0.0F;
        float upper_half_width = 0.0F;
    };

    // Ces valeurs sont exactement celles du gréement historique.
    constexpr std::array<MastDefinition, 3> masts {{
        {
            -17.5F,
            20.0F,
            11.5F,
            16.2F,
            4.9F,
            3.6F,
        },
        {
            0.0F,
            26.0F,
            13.4F,
            19.5F,
            7.3F,
            5.2F,
        },
        {
            18.0F,
            22.5F,
            12.3F,
            17.4F,
            6.1F,
            4.3F,
        },
    }};

    for (const auto& mast : masts) {
        // Le pied collidable garde exactement son ancienne enveloppe.
        add_box(
            parts,
            ShipMaterial::SolidGold,
            {
                -0.30F,
                kAmelieMainDeckUnderside,
                mast.z - 0.30F,
            },
            {
                0.30F,
                8.0F,
                mast.z + 0.30F,
            },
            true);

        // Axe vertical du mât.
        add_segment(
            parts,
            ShipMaterial::SolidGold,
            {
                0.0F,
                4.0F,
                mast.z,
            },
            {
                0.0F,
                mast.top,
                mast.z,
            },
            0.42F);

        // Vergue inférieure.
        add_segment(
            parts,
            ShipMaterial::SolidGold,
            {
                -mast.lower_half_width,
                mast.lower_yard,
                mast.z,
            },
            {
                mast.lower_half_width,
                mast.lower_yard,
                mast.z,
            },
            0.28F);

        // Vergue supérieure.
        add_segment(
            parts,
            ShipMaterial::SolidGold,
            {
                -mast.upper_half_width,
                mast.upper_yard,
                mast.z,
            },
            {
                mast.upper_half_width,
                mast.upper_yard,
                mast.z,
            },
            0.23F);

        // Bagues décoratives de renfort.
        add_gold_mast_collar(
            parts,
            mast.z,
            8.0F,
            0.56F);

        add_gold_mast_collar(
            parts,
            mast.z,
            mast.lower_yard,
            0.52F);

        add_gold_mast_collar(
            parts,
            mast.z,
            mast.upper_yard,
            0.46F);

        // Voile carrée inférieure.
        add_black_square_sail(
            parts,
            mast.z,
            mast.lower_yard + 0.35F,
            mast.upper_yard - 0.35F,
            mast.lower_half_width - 0.35F);

        // Voile carrée supérieure.
        add_black_square_sail(
            parts,
            mast.z,
            mast.upper_yard + 0.30F,
            mast.top - 0.75F,
            mast.upper_half_width - 0.30F);

        const auto shroud_half_width =
            std::min(
                7.5F,
                amelie_half_width(
                    mast.z - 3.0F) -
                    0.35F);

        // Hauban gauche inchangé.
        add_segment(
            parts,
            ShipMaterial::Rope,
            {
                0.0F,
                mast.top,
                mast.z,
            },
            {
                -shroud_half_width,
                4.35F,
                mast.z - 3.0F,
            },
            0.055F);

        // Hauban droit inchangé.
        add_segment(
            parts,
            ShipMaterial::Rope,
            {
                0.0F,
                mast.top,
                mast.z,
            },
            {
                shroud_half_width,
                4.35F,
                mast.z - 3.0F,
            },
            0.055F);
    }

    // La gravure est uniquement posée sur le grand mât central.
    add_main_mast_engraving(parts);

    // Les étais conservent leurs coordonnées historiques.
    add_segment(
        parts,
        ShipMaterial::Rope,
        {
            0.0F,
            20.0F,
            -17.5F,
        },
        {
            0.0F,
            26.0F,
            0.0F,
        },
        0.055F);

    add_segment(
        parts,
        ShipMaterial::Rope,
        {
            0.0F,
            26.0F,
            0.0F,
        },
        {
            0.0F,
            22.5F,
            18.0F,
        },
        0.055F);

    // Voile d'étai arrière.
    add_black_triangular_sail(
        parts,
        {
            0.12F,
            8.6F,
            -15.4F,
        },
        {
            0.20F,
            22.4F,
            -1.8F,
        },
        {
            -1.0F,
            0.0F,
            0.0F,
        });

    // Voile d'étai centrale.
    add_black_triangular_sail(
        parts,
        {
            0.12F,
            8.4F,
            2.2F,
        },
        {
            0.20F,
            21.4F,
            16.2F,
        },
        {
            1.0F,
            0.0F,
            0.0F,
        });

    // Beaupré doré, avec coordonnées et longueur inchangées.
    add_segment(
        parts,
        ShipMaterial::SolidGold,
        {
            0.0F,
            4.50F,
            34.0F,
        },
        {
            0.0F,
            7.65F,
            46.0F,
        },
        0.38F);

    // Étai du beaupré inchangé.
    add_segment(
        parts,
        ShipMaterial::Rope,
        {
            0.0F,
            22.5F,
            18.0F,
        },
        {
            0.0F,
            7.65F,
            46.0F,
        },
        0.055F);

    // Voile triangulaire avant.
    add_black_triangular_sail(
        parts,
        {
            0.18F,
            7.80F,
            34.0F,
        },
        {
            0.27F,
            17.2F,
            44.4F,
        },
        {
            1.0F,
            0.0F,
            0.0F,
        });
}

void add_stern_identity_and_hardware(std::vector<ShipPart>& parts) {
    // Je conserve la plaque historique de poupe. La seconde gravure demandée
    // appartient au grand mât et ne modifie donc aucun élément de coque.
    add_panel(parts,
              ShipMaterial::CleanBeam,
              {-4.25F, 4.52F, kAmelieSternZ - 0.72F},
              {4.25F, 5.79F, kAmelieSternZ - 0.52F},
              {0.0F, 0.0F, -1.0F},
              0.12F);
    constexpr std::array<char32_t, 8> name {{U'L', U'\'', U'A', U'm', U'\u00E9', U'l', U'i', U'e'}};
    constexpr std::array<float, 8> widths {{0.62F, 0.30F, 0.72F, 0.78F, 0.62F, 0.28F, 0.28F, 0.62F}};
    auto cursor = 2.60F;
    for (std::size_t index = 0; index < name.size(); ++index) {
        add_glyph(parts,
                  name[index],
                  {cursor - widths[index], 4.79F, kAmelieSternZ - 0.82F},
                  {cursor, 5.52F, kAmelieSternZ - 0.74F});
        cursor -= widths[index] + 0.14F;
    }

    add_segment(parts,
                ShipMaterial::Brass,
                {-5.85F, 3.20F, kAmelieSternZ - 0.68F},
                {5.85F, 3.20F, kAmelieSternZ - 0.68F},
                0.11F);
    add_segment(parts,
                ShipMaterial::Brass,
                {-5.55F, 4.35F, kAmelieSternZ - 0.66F},
                {5.55F, 4.35F, kAmelieSternZ - 0.66F},
                0.09F);
    add_box(parts,
            ShipMaterial::Iron,
            {-0.65F, -1.30F, kAmelieSternZ - 1.05F},
            {0.65F, 1.30F, kAmelieSternZ - 0.35F},
            true);
    add_segment(parts, ShipMaterial::Iron, {-2.15F, 2.15F, 30.0F}, {-2.70F, -0.55F, 32.0F}, 0.24F);
    add_segment(parts, ShipMaterial::Iron, {2.15F, 2.15F, 30.0F}, {2.70F, -0.55F, 32.0F}, 0.24F);
    add_box(parts, ShipMaterial::Iron, {-3.05F, -0.85F, 31.7F}, {-2.35F, 0.15F, 32.35F}, false);
    add_box(parts, ShipMaterial::Iron, {2.35F, -0.85F, 31.7F}, {3.05F, 0.15F, 32.35F}, false);
}

auto part_bounds(const ShipPart& part) noexcept -> ShipBounds {
    auto bounds = ShipBounds {
        glm::min(part.local_start, part.local_end),
        glm::max(part.local_start, part.local_end),
    };
    if (part.shape == ShipPartShape::Segment ||
        part.shape == ShipPartShape::Panel ||
        part.shape == ShipPartShape::ClimbableNet) {
        // La bordure renforcee du filet atteint 1,65 fois le diametre nominal;
        // je l'inclus donc entierement dans les limites partagees avec le rendu.
        const auto padding_factor =
            part.shape == ShipPartShape::ClimbableNet ? 0.825F : 0.5F;
        const auto padding = glm::vec3 {std::max(0.0F, part.thickness) * padding_factor};
        bounds.min -= padding;
        bounds.max += padding;
    }
    return bounds;
}

auto calculate_ship_bounds(std::span<const ShipPart> parts) noexcept -> ShipBounds {
    if (parts.empty()) {
        return {};
    }
    auto bounds = part_bounds(parts.front());
    for (const auto& part : parts.subspan(1U)) {
        const auto current = part_bounds(part);
        bounds.min = glm::min(bounds.min, current.min);
        bounds.max = glm::max(bounds.max, current.max);
    }
    return bounds;
}

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_u32_value(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (int shift = 0; shift < 32; shift += 8) {
        hash_byte(hash, static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xFFU));
    }
}

void hash_vec3(std::uint64_t& hash, const glm::vec3& value) noexcept {
    hash_u32_value(hash, std::bit_cast<std::uint32_t>(value.x));
    hash_u32_value(hash, std::bit_cast<std::uint32_t>(value.y));
    hash_u32_value(hash, std::bit_cast<std::uint32_t>(value.z));
}

auto calculate_geometry_revision(std::span<const ShipPart> parts, const ShipBounds& bounds) noexcept -> std::uint64_t {
    auto hash = kFnvOffset;
    constexpr std::string_view name = "L'Am\xC3\xA9lie";
    for (const auto character : name) {
        hash_byte(hash, static_cast<std::uint8_t>(character));
    }
    hash_vec3(hash, bounds.min);
    hash_vec3(hash, bounds.max);
    for (const auto& part : parts) {
        hash_byte(hash, static_cast<std::uint8_t>(part.shape));
        hash_byte(hash, static_cast<std::uint8_t>(part.material));
        hash_vec3(hash, part.local_start);
        hash_vec3(hash, part.local_end);
        hash_vec3(hash, part.orientation);
        hash_u32_value(hash, std::bit_cast<std::uint32_t>(part.thickness));
        hash_byte(hash, part.collidable ? 1U : 0U);
        hash_byte(hash, part.supports_player ? 1U : 0U);
        hash_u32_value(hash, static_cast<std::uint32_t>(part.glyph));
    }
    return hash;
}

auto calculate_navigation_revision(std::span<const ShipCrewNavigationNode> nodes,
                                   std::span<const ShipCrewNavigationEdge> edges) noexcept -> std::uint64_t {
    auto hash = kFnvOffset;
    for (const auto& node : nodes) {
        hash_byte(hash, static_cast<std::uint8_t>(node.station));
        hash_vec3(hash, node.local_position);
        hash_byte(hash, node.exterior ? 1U : 0U);
    }
    for (const auto& edge : edges) {
        hash_byte(hash, static_cast<std::uint8_t>(edge.first));
        hash_byte(hash, static_cast<std::uint8_t>(edge.second));
    }
    return hash;
}

auto amelie_parts() -> const std::vector<ShipPart>& {
    static const auto parts = [] {
        std::vector<ShipPart> output;
        output.reserve(1'280U);
        add_hull_and_decks(output);
        add_accesses_and_interior(output);
        add_deck_equipment(output);
        add_rigging(output);
        add_stern_identity_and_hardware(output);
        return output;
    }();
    return parts;
}

struct ShipCollisionIndex {
    std::unordered_map<BlockCoord, std::vector<std::uint32_t>, BlockCoordHash> cells {};
    std::unordered_map<BlockCoord, std::vector<std::uint32_t>, BlockCoordHash> support_columns {};
};

auto ship_collision_index() -> const ShipCollisionIndex& {
    static const auto index = [] {
        ShipCollisionIndex output;
        const auto& parts = amelie_parts();
        for (std::size_t part_index = 0; part_index < parts.size(); ++part_index) {
            const auto& part = parts[part_index];
            const auto bounds = part_bounds(part);
            const auto min_x = static_cast<int>(std::floor(bounds.min.x));
            const auto min_y = static_cast<int>(std::floor(bounds.min.y));
            const auto min_z = static_cast<int>(std::floor(bounds.min.z));
            const auto max_x = static_cast<int>(std::floor(bounds.max.x - kCollisionEpsilon));
            const auto max_y = static_cast<int>(std::floor(bounds.max.y - kCollisionEpsilon));
            const auto max_z = static_cast<int>(std::floor(bounds.max.z - kCollisionEpsilon));

            if (part.collidable) {
                for (int y = min_y; y <= max_y; ++y) {
                    for (int z = min_z; z <= max_z; ++z) {
                        for (int x = min_x; x <= max_x; ++x) {
                            output.cells[{x, y, z}].push_back(
                                static_cast<std::uint32_t>(part_index));
                        }
                    }
                }
            }

            if (part.supports_player) {
                // Une colonne 2D suffit pour les faces de support. Cela evite
                // de reparcourir les quelque 1 280 pieces du blueprint a chaque
                // sondage des pieds du joueur ou d'un objet depose.
                for (int z = min_z; z <= max_z; ++z) {
                    for (int x = min_x; x <= max_x; ++x) {
                        output.support_columns[{x, 0, z}].push_back(
                            static_cast<std::uint32_t>(part_index));
                    }
                }
            }
        }
        return output;
    }();
    return index;
}

auto aabbs_overlap(const ShipBounds& lhs, const ShipBounds& rhs) noexcept -> bool {
    return lhs.min.x < rhs.max.x - kCollisionEpsilon && lhs.max.x > rhs.min.x + kCollisionEpsilon &&
           lhs.min.y < rhs.max.y - kCollisionEpsilon && lhs.max.y > rhs.min.y + kCollisionEpsilon &&
           lhs.min.z < rhs.max.z - kCollisionEpsilon && lhs.max.z > rhs.min.z + kCollisionEpsilon;
}

auto normalized_quaternion_or_identity(const glm::quat& value) noexcept -> glm::quat {
    const auto length_squared =
        value.w * value.w +
        value.x * value.x +
        value.y * value.y +
        value.z * value.z;

    if (!std::isfinite(value.w) ||
        !std::isfinite(value.x) ||
        !std::isfinite(value.y) ||
        !std::isfinite(value.z) ||
        !std::isfinite(length_squared) ||
        length_squared <= 1.0e-10F) {
        return {1.0F, 0.0F, 0.0F, 0.0F};
    }

    return glm::normalize(value);
}

auto transform_point(const glm::vec3& origin,
                     const glm::quat& orientation,
                     const glm::vec3& local_point) noexcept -> glm::vec3 {
    return origin + orientation * local_point;
}

auto inverse_transform_point(const glm::vec3& origin,
                             const glm::quat& orientation,
                             const glm::vec3& world_point) noexcept -> glm::vec3 {
    return glm::conjugate(orientation) * (world_point - origin);
}

auto transformed_bounds(const ShipBounds& local_bounds,
                        const glm::vec3& origin,
                        const glm::quat& orientation) noexcept -> ShipBounds {
    const std::array<glm::vec3, 8> corners {{
        {local_bounds.min.x, local_bounds.min.y, local_bounds.min.z},
        {local_bounds.max.x, local_bounds.min.y, local_bounds.min.z},
        {local_bounds.min.x, local_bounds.max.y, local_bounds.min.z},
        {local_bounds.max.x, local_bounds.max.y, local_bounds.min.z},
        {local_bounds.min.x, local_bounds.min.y, local_bounds.max.z},
        {local_bounds.max.x, local_bounds.min.y, local_bounds.max.z},
        {local_bounds.min.x, local_bounds.max.y, local_bounds.max.z},
        {local_bounds.max.x, local_bounds.max.y, local_bounds.max.z},
    }};

    auto result = ShipBounds {
        transform_point(origin, orientation, corners.front()),
        transform_point(origin, orientation, corners.front()),
    };
    for (const auto& corner : corners) {
        const auto world_corner =
            transform_point(origin, orientation, corner);
        result.min = glm::min(result.min, world_corner);
        result.max = glm::max(result.max, world_corner);
    }
    return result;
}

auto inverse_transformed_bounds(const ShipBounds& world_bounds,
                                const glm::vec3& origin,
                                const glm::quat& orientation) noexcept -> ShipBounds {
    const std::array<glm::vec3, 8> corners {{
        {world_bounds.min.x, world_bounds.min.y, world_bounds.min.z},
        {world_bounds.max.x, world_bounds.min.y, world_bounds.min.z},
        {world_bounds.min.x, world_bounds.max.y, world_bounds.min.z},
        {world_bounds.max.x, world_bounds.max.y, world_bounds.min.z},
        {world_bounds.min.x, world_bounds.min.y, world_bounds.max.z},
        {world_bounds.max.x, world_bounds.min.y, world_bounds.max.z},
        {world_bounds.min.x, world_bounds.max.y, world_bounds.max.z},
        {world_bounds.max.x, world_bounds.max.y, world_bounds.max.z},
    }};

    auto result = ShipBounds {
        inverse_transform_point(origin, orientation, corners.front()),
        inverse_transform_point(origin, orientation, corners.front()),
    };
    for (const auto& corner : corners) {
        const auto local_corner =
            inverse_transform_point(origin, orientation, corner);
        result.min = glm::min(result.min, local_corner);
        result.max = glm::max(result.max, local_corner);
    }
    return result;
}

auto oriented_box_intersects_aabb(const ShipBounds& local_box,
                                  const glm::vec3& ship_origin,
                                  const glm::quat& ship_orientation,
                                  const ShipBounds& world_query) noexcept -> bool {
    const auto query_center =
        (world_query.min + world_query.max) * 0.5F;
    const auto query_half_extents =
        glm::max(
            (world_query.max - world_query.min) * 0.5F,
            glm::vec3 {0.0F});

    const auto local_center =
        (local_box.min + local_box.max) * 0.5F;
    const auto box_half_extents =
        glm::max(
            (local_box.max - local_box.min) * 0.5F,
            glm::vec3 {0.0F});
    const auto box_center =
        transform_point(
            ship_origin,
            ship_orientation,
            local_center);

    const std::array<glm::vec3, 3> world_axes {{
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
    }};
    const std::array<glm::vec3, 3> box_axes {{
        ship_orientation * world_axes[0],
        ship_orientation * world_axes[1],
        ship_orientation * world_axes[2],
    }};
    const auto center_delta =
        box_center - query_center;

    const auto separates =
        [&](const glm::vec3& axis) noexcept {
            const auto axis_length_squared =
                glm::dot(axis, axis);
            if (axis_length_squared <= 1.0e-10F) {
                return false;
            }

            const auto query_radius =
                glm::dot(
                    query_half_extents,
                    glm::abs(axis));
            auto box_radius = 0.0F;
            for (int index = 0;
                 index < 3;
                 ++index) {
                box_radius +=
                    box_half_extents[index] *
                    std::abs(
                        glm::dot(
                            box_axes[static_cast<std::size_t>(index)],
                            axis));
            }

            const auto distance =
                std::abs(
                    glm::dot(
                        center_delta,
                        axis));
            const auto epsilon =
                kCollisionEpsilon *
                std::sqrt(axis_length_squared);

            // Un simple contact de surface ne compte pas comme penetration.
            return distance >=
                   query_radius +
                       box_radius -
                       epsilon;
        };

    for (const auto& axis : world_axes) {
        if (separates(axis)) {
            return false;
        }
    }
    for (const auto& axis : box_axes) {
        if (separates(axis)) {
            return false;
        }
    }
    for (const auto& world_axis : world_axes) {
        for (const auto& box_axis : box_axes) {
            if (separates(
                    glm::cross(
                        world_axis,
                        box_axis))) {
                return false;
            }
        }
    }
    return true;
}

auto support_height_for_pose(const glm::vec3& feet_position,
                             float min_height,
                             float max_height,
                             const glm::vec3& ship_origin,
                             const glm::quat& ship_orientation) noexcept
    -> std::optional<float> {
    if (!std::isfinite(feet_position.x) ||
        !std::isfinite(feet_position.y) ||
        !std::isfinite(feet_position.z) ||
        !std::isfinite(min_height) ||
        !std::isfinite(max_height) ||
        min_height > max_height) {
        return std::nullopt;
    }

    const auto up_normal =
        ship_orientation *
        glm::vec3 {0.0F, 1.0F, 0.0F};
    if (!std::isfinite(up_normal.y) ||
        up_normal.y <= 0.05F) {
        return std::nullopt;
    }

    const auto& blueprint =
        amelie_ship_blueprint();
    const auto& index =
        ship_collision_index();
    constexpr std::array<glm::vec2, 5> samples {{
        {0.0F, 0.0F},
        {kShipSupportSampleRadius, kShipSupportSampleRadius},
        {-kShipSupportSampleRadius, kShipSupportSampleRadius},
        {kShipSupportSampleRadius, -kShipSupportSampleRadius},
        {-kShipSupportSampleRadius, -kShipSupportSampleRadius},
    }};

    std::optional<float> best_height;
    for (const auto& sample : samples) {
        const auto sample_x =
            feet_position.x + sample.x;
        const auto sample_z =
            feet_position.z + sample.y;
        const auto local_low =
            inverse_transform_point(
                ship_origin,
                ship_orientation,
                {
                    sample_x,
                    min_height,
                    sample_z,
                });
        const auto local_high =
            inverse_transform_point(
                ship_origin,
                ship_orientation,
                {
                    sample_x,
                    max_height,
                    sample_z,
                });

        const auto local_min_x =
            std::max(
                std::min(local_low.x, local_high.x),
                blueprint.bounds.min.x);
        const auto local_max_x =
            std::min(
                std::max(local_low.x, local_high.x),
                blueprint.bounds.max.x);
        const auto local_min_z =
            std::max(
                std::min(local_low.z, local_high.z),
                blueprint.bounds.min.z);
        const auto local_max_z =
            std::min(
                std::max(local_low.z, local_high.z),
                blueprint.bounds.max.z);

        if (local_min_x >
                local_max_x +
                    kCollisionEpsilon ||
            local_min_z >
                local_max_z +
                    kCollisionEpsilon) {
            continue;
        }

        const auto first_x =
            static_cast<int>(
                std::floor(
                    local_min_x -
                    kCollisionEpsilon));
        const auto last_x =
            static_cast<int>(
                std::floor(
                    local_max_x +
                    kCollisionEpsilon));
        const auto first_z =
            static_cast<int>(
                std::floor(
                    local_min_z -
                    kCollisionEpsilon));
        const auto last_z =
            static_cast<int>(
                std::floor(
                    local_max_z +
                    kCollisionEpsilon));

        for (int z = first_z; z <= last_z; ++z) {
            for (int x = first_x; x <= last_x; ++x) {
                const auto column =
                    index.support_columns.find(
                        {x, 0, z});
                if (column ==
                    index.support_columns.end()) {
                    continue;
                }

                for (const auto part_index :
                     column->second) {
                    const auto& part =
                        blueprint.parts[part_index];
                    if (!part.supports_player) {
                        continue;
                    }

                    const auto bounds =
                        part_bounds(part);
                    const auto local_plane_point =
                        glm::vec3 {
                            (bounds.min.x +
                             bounds.max.x) *
                                0.5F,
                            bounds.max.y,
                            (bounds.min.z +
                             bounds.max.z) *
                                0.5F,
                        };
                    const auto world_plane_point =
                        transform_point(
                            ship_origin,
                            ship_orientation,
                            local_plane_point);
                    const auto candidate_height =
                        (
                            glm::dot(
                                up_normal,
                                world_plane_point) -
                            up_normal.x * sample_x -
                            up_normal.z * sample_z
                        ) /
                        up_normal.y;

                    if (candidate_height <
                            min_height -
                                kCollisionEpsilon ||
                        candidate_height >
                            max_height +
                                kCollisionEpsilon) {
                        continue;
                    }

                    const auto local_hit =
                        inverse_transform_point(
                            ship_origin,
                            ship_orientation,
                            {
                                sample_x,
                                candidate_height,
                                sample_z,
                            });
                    if (local_hit.x <
                            bounds.min.x -
                                kCollisionEpsilon ||
                        local_hit.x >
                            bounds.max.x +
                                kCollisionEpsilon ||
                        local_hit.z <
                            bounds.min.z -
                                kCollisionEpsilon ||
                        local_hit.z >
                            bounds.max.z +
                                kCollisionEpsilon) {
                        continue;
                    }

                    if (!best_height.has_value() ||
                        candidate_height >
                            *best_height) {
                        best_height =
                            candidate_height;
                    }
                }
            }
        }
    }
    return best_height;
}

auto ray_aabb_distance(const glm::vec3& origin,
                       const glm::vec3& direction,
                       const ShipBounds& bounds,
                       float max_distance) noexcept -> std::optional<float> {
    auto near_distance = 0.0F;
    auto far_distance = max_distance;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) <= 1.0e-7F) {
            if (origin[axis] < bounds.min[axis] || origin[axis] > bounds.max[axis]) {
                return std::nullopt;
            }
            continue;
        }
        auto first = (bounds.min[axis] - origin[axis]) / direction[axis];
        auto second = (bounds.max[axis] - origin[axis]) / direction[axis];
        if (first > second) {
            std::swap(first, second);
        }
        near_distance = std::max(near_distance, first);
        far_distance = std::min(far_distance, second);
        if (near_distance > far_distance) {
            return std::nullopt;
        }
    }
    return near_distance <= max_distance ? std::optional<float> {near_distance} : std::nullopt;
}

auto legacy_blueprint_checksum() noexcept -> std::uint64_t {
    auto hash = kFnvOffset;
    for (const auto& voxel : legacy_ship_v7_voxels()) {
        hash_u32_value(hash, std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(voxel.local_block.x)));
        hash_u32_value(hash, std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(voxel.local_block.y)));
        hash_u32_value(hash, std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(voxel.local_block.z)));
        hash_byte(hash, voxel.block_id);
    }
    return hash;
}

auto clamp_ratio(float value) noexcept -> float {
    return std::clamp(value / 100.0F, 0.0F, 1.0F);
}

auto normalized_cycle_timer(float value, float interval) noexcept -> float {
    const auto safe_value = std::max(0.0F, finite_or(value, 0.0F));
    return interval > 0.0F ? std::fmod(safe_value, interval) : 0.0F;
}

void saturating_add(std::uint32_t& value, std::uint32_t increment) noexcept {
    value = increment > std::numeric_limits<std::uint32_t>::max() - value
                ? std::numeric_limits<std::uint32_t>::max()
                : value + increment;
}

auto route_roll_key(float route_distance) noexcept -> std::uint32_t {
    constexpr double kUint32Range = 4'294'967'296.0;
    const auto scaled = static_cast<double>(std::max(0.0F, finite_or(route_distance, 0.0F))) * 17.0;
    return static_cast<std::uint32_t>(std::fmod(scaled, kUint32Range));
}

auto maximum_ship_position_z() noexcept -> float {
    static const auto limit = [] {
        const auto forward_extent_from_entity =
            std::max(0.0F, amelie_ship_blueprint().bounds.max.z - 0.5F);
        return std::max(0.5F, kShipCoordinateLimit - forward_extent_from_entity);
    }();
    return limit;
}

} // namespace

auto amelie_ship_blueprint() noexcept -> const ShipBlueprint& {
    static const auto blueprint = [] {
        const auto& parts = amelie_parts();
        const auto bounds = calculate_ship_bounds(parts);
        const auto& navigation_nodes = amelie_crew_navigation_nodes();
        const auto& navigation_edges = amelie_crew_navigation_edges();
        const ShipAnchors anchors {
            {0.5F, 4.10F, -7.5F},
            {0.0F, 1.01F, -7.5F},
            {0.0F, 1.01F, -22.0F},
            {0.0F, 1.01F, -5.0F},
            {0.0F, 1.01F, 5.0F},
            {0.0F, 1.01F, 23.0F},
            {-1.5F, 4.51F, -29.0F},
            {0.0F, 4.01F, -7.5F},
            {0.0F, 4.01F, 14.5F},
        };
        return ShipBlueprint {
            "L'Am\xC3\xA9lie",
            std::span<const ShipPart>(parts),
            std::span<const ShipCrewNavigationNode>(navigation_nodes),
            std::span<const ShipCrewNavigationEdge>(navigation_edges),
            std::span<const glm::vec3>(kAmelieInteriorLanterns),
            bounds,
            anchors,
            kAmelieProtectionProfile,
            calculate_geometry_revision(parts, bounds),
            calculate_navigation_revision(navigation_nodes, navigation_edges),
        };
    }();
    return blueprint;
}

auto legacy_ship_voxel_count() noexcept -> std::size_t {
    return legacy_ship_v7_voxels().size();
}

auto legacy_ship_blueprint_checksum() noexcept -> std::uint64_t {
    static const auto checksum = legacy_blueprint_checksum();
    return checksum;
}

auto sanitize_sea_adventure_save_state(const SeaAdventureSaveState& state) noexcept -> SeaAdventureSaveState {
    auto sanitized = state;
    sanitized.voyage_phase_elapsed = std::max(0.0F, finite_or(sanitized.voyage_phase_elapsed, 0.0F));
    switch (sanitized.voyage_phase) {
    case SeaVoyagePhase::Moored:
        if (sanitized.voyage_phase_elapsed >= kMooredBoardingSeconds) {
            sanitized.voyage_phase_elapsed -= kMooredBoardingSeconds;
            sanitized.voyage_phase = SeaVoyagePhase::Departing;
            if (sanitized.voyage_phase_elapsed >= kDepartureAccelerationSeconds) {
                sanitized.voyage_phase = SeaVoyagePhase::Underway;
                sanitized.voyage_phase_elapsed = 0.0F;
            }
        }
        break;
    case SeaVoyagePhase::Departing:
        if (sanitized.voyage_phase_elapsed >= kDepartureAccelerationSeconds) {
            sanitized.voyage_phase = SeaVoyagePhase::Underway;
            sanitized.voyage_phase_elapsed = 0.0F;
        }
        break;
    case SeaVoyagePhase::Underway:
        sanitized.voyage_phase_elapsed = 0.0F;
        break;
    default:
        sanitized.voyage_phase = SeaVoyagePhase::Underway;
        sanitized.voyage_phase_elapsed = 0.0F;
        break;
    }
    sanitized.ship_position = finite_vec3_or(sanitized.ship_position, {0.5F, kShipVisualY, 0.5F});
    // Je recale toujours le navire sur l'axe de la voie oceanique : il ne
    // possede aucun pilotage lateral et une ancienne valeur X ne peut donc
    // pas representer une position atteignable dans la simulation actuelle.
    sanitized.ship_position.x = 0.5F;
    sanitized.ship_position.y = kShipVisualY;
    sanitized.ship_position.z = std::clamp(sanitized.ship_position.z, 0.5F, maximum_ship_position_z());
    sanitized.route_distance = std::clamp(finite_or(sanitized.route_distance, 0.0F), 0.0F, kShipCoordinateLimit);
    sanitized.hunger = std::clamp(finite_or(sanitized.hunger, 100.0F), 0.0F, 100.0F);
    sanitized.thirst = std::clamp(finite_or(sanitized.thirst, 100.0F), 0.0F, 100.0F);
    sanitized.stamina = std::clamp(finite_or(sanitized.stamina, 100.0F), 0.0F, 100.0F);
    sanitized.survival_damage_timer = normalized_cycle_timer(sanitized.survival_damage_timer, kSurvivalDamageInterval);
    sanitized.stranded_warning_timer = normalized_cycle_timer(sanitized.stranded_warning_timer, kStrandedWarningInterval);

    const auto target = std::clamp(finite_or(sanitized.fishing_target_seconds, 0.0F), 0.0F, 60.0F);
    const auto progress = std::clamp(finite_or(sanitized.fishing_progress, 0.0F), 0.0F, 60.0F);
    if (!sanitized.fishing_active || target <= 0.0F || progress >= target) {
        sanitized.fishing_active = false;
        sanitized.fishing_progress = 0.0F;
        sanitized.fishing_target_seconds = 0.0F;
    } else {
        sanitized.fishing_progress = progress;
        sanitized.fishing_target_seconds = target;
    }

    const auto current_origin_x = static_cast<std::int64_t>(ship_origin_x(sanitized));
    const auto current_origin_z = static_cast<std::int64_t>(ship_origin_z(sanitized));
    const auto stamped_origin_x = static_cast<std::int64_t>(sanitized.stamped_ship_x);
    const auto stamped_origin_z = static_cast<std::int64_t>(sanitized.stamped_ship_z);
    const auto legacy_origin_is_near_ship =
        stamped_origin_x >= current_origin_x - kLegacyShipOriginTolerance &&
        stamped_origin_x <= current_origin_x + kLegacyShipOriginTolerance &&
        stamped_origin_z >= current_origin_z - kLegacyShipOriginTolerance &&
        stamped_origin_z <= current_origin_z + kLegacyShipOriginTolerance;
    if (!sanitized.has_stamped_ship || !legacy_origin_is_near_ship) {
        // Je refuse de migrer un ancien navire a une coordonnee arbitraire :
        // cela evite de charger des chunks lointains depuis une save corrompue.
        sanitized.has_stamped_ship = false;
        sanitized.stamped_ship_x = static_cast<std::int32_t>(current_origin_x);
        sanitized.stamped_ship_z = static_cast<std::int32_t>(current_origin_z);
    }
    sanitized.crew = sanitize_ship_crew_save_state(sanitized.crew);
    sanitized.old_guard = sanitize_old_guard_save_state(sanitized.old_guard);
    return sanitized;
}

void ShipEntity::set_position(const glm::vec3& position) noexcept {
    position_ = finite_vec3_or(position, position_);
}

void ShipEntity::set_velocity(const glm::vec3& velocity) noexcept {
    velocity_ = finite_vec3_or(velocity, {});
}

void ShipEntity::begin_motion_step() noexcept {
    // Tous les passagers, objets et marins doivent comparer exactement les
    // memes deux poses pendant une frame. La copie est donc centralisee ici.
    previous_position_ = position_;
    previous_ocean_heave_ = ocean_heave_;
    previous_orientation_ = orientation_;
    motion_history_valid_ = true;
}

void ShipEntity::synchronize_motion_history() noexcept {
    // Apres un chargement ou un reset, la pose precedente doit etre identique
    // a la pose courante afin de ne jamais produire un faux bond de plateforme.
    previous_position_ = position_;
    previous_ocean_heave_ = ocean_heave_;
    previous_orientation_ = orientation_;
    motion_history_valid_ = true;
}

void ShipEntity::set_ocean_pose(float heave,
                                float pitch_radians,
                                float roll_radians) noexcept {
    ocean_heave_ = std::clamp(
        finite_or(heave, ocean_heave_),
        -4.0F,
        4.0F);

    const auto pitch = std::clamp(
        finite_or(pitch_radians, 0.0F),
        -0.55F,
        0.55F);
    const auto roll = std::clamp(
        finite_or(roll_radians, 0.0F),
        -0.65F,
        0.65F);

    // Le navire n'a pas encore de lacet pilotable : le tangage s'applique
    // autour de son axe local X et le roulis autour de son axe local Z.
    orientation_ = normalized_quaternion_or_identity(
        glm::angleAxis(
            pitch,
            glm::vec3 {1.0F, 0.0F, 0.0F}) *
        glm::angleAxis(
            roll,
            glm::vec3 {0.0F, 0.0F, 1.0F}));
}

auto ShipEntity::position() const noexcept -> const glm::vec3& {
    return position_;
}

auto ShipEntity::velocity() const noexcept -> const glm::vec3& {
    return velocity_;
}

auto ShipEntity::orientation() const noexcept -> const glm::quat& {
    return orientation_;
}

auto ShipEntity::world_origin() const noexcept -> glm::vec3 {
    return position_ -
           glm::vec3 {0.5F, 0.0F, 0.5F} +
           glm::vec3 {0.0F, ocean_heave_, 0.0F};
}

auto ShipEntity::previous_world_origin() const noexcept -> glm::vec3 {
    if (!motion_history_valid_) {
        return world_origin();
    }

    return previous_position_ -
           glm::vec3 {0.5F, 0.0F, 0.5F} +
           glm::vec3 {0.0F, previous_ocean_heave_, 0.0F};
}

auto ShipEntity::model_matrix() const noexcept -> glm::mat4 {
    return glm::translate(
               glm::mat4 {1.0F},
               world_origin()) *
           glm::mat4_cast(orientation_);
}

auto ShipEntity::previous_model_matrix() const noexcept -> glm::mat4 {
    const auto previous_orientation =
        motion_history_valid_
            ? previous_orientation_
            : orientation_;

    return glm::translate(
               glm::mat4 {1.0F},
               previous_world_origin()) *
           glm::mat4_cast(previous_orientation);
}

auto ShipEntity::world_bounds() const noexcept -> ShipBounds {
    return transformed_bounds(
        amelie_ship_blueprint().bounds,
        world_origin(),
        orientation_);
}

auto ShipEntity::local_to_world_point(
    const glm::vec3& local_point) const noexcept -> glm::vec3 {

    return transform_point(
        world_origin(),
        orientation_,
        finite_vec3_or(local_point, {}));
}

auto ShipEntity::world_to_local_point(
    const glm::vec3& world_point) const noexcept -> glm::vec3 {

    return inverse_transform_point(
        world_origin(),
        orientation_,
        finite_vec3_or(world_point, world_origin()));
}

auto ShipEntity::world_point_in_persisted_neutral_pose(
    const glm::vec3& current_world_point) const noexcept -> glm::vec3 {

    if (!std::isfinite(current_world_point.x) ||
        !std::isfinite(current_world_point.y) ||
        !std::isfinite(current_world_point.z)) {
        return current_world_point;
    }

    // Je conserve la translation logique du navire dans la sauvegarde, tandis
    // que je reconstruis la houle et son orientation a zero au chargement.
    const auto persisted_world_origin =
        position_ -
        glm::vec3 {0.5F, 0.0F, 0.5F};
    return persisted_world_origin +
           world_to_local_point(current_world_point);
}

auto ShipEntity::local_to_world_direction(
    const glm::vec3& local_direction) const noexcept -> glm::vec3 {

    return orientation_ *
           finite_vec3_or(local_direction, {});
}

auto ShipEntity::world_to_local_direction(
    const glm::vec3& world_direction) const noexcept -> glm::vec3 {

    return glm::conjugate(orientation_) *
           finite_vec3_or(world_direction, {});
}

auto ShipEntity::motion_delta_at(
    const glm::vec3& previous_world_point) const noexcept -> glm::vec3 {

    if (!motion_history_valid_ ||
        !std::isfinite(previous_world_point.x) ||
        !std::isfinite(previous_world_point.y) ||
        !std::isfinite(previous_world_point.z)) {
        return {};
    }

    const auto previous_local_point =
        inverse_transform_point(
            previous_world_origin(),
            previous_orientation_,
            previous_world_point);
    const auto current_world_point =
        transform_point(
            world_origin(),
            orientation_,
            previous_local_point);

    return current_world_point -
           previous_world_point;
}

auto ShipEntity::excludes_ocean_at(
    const glm::vec3& world_point) const noexcept
    -> bool {

    // Je refuse les non-finis avant world_to_local_point(), car cette API
    // remplace volontairement une position corrompue par l'origine du navire.
    if (!std::isfinite(world_point.x) ||
        !std::isfinite(world_point.y) ||
        !std::isfinite(world_point.z)) {
        return false;
    }

    return amelie_ship_blueprint()
        .protection_profile
        .excludes_ocean_local(
            world_to_local_point(
                world_point));
}

auto ShipEntity::is_weather_sheltered_at(
    const glm::vec3& world_point) const noexcept
    -> bool {

    if (!std::isfinite(world_point.x) ||
        !std::isfinite(world_point.y) ||
        !std::isfinite(world_point.z)) {
        return false;
    }

    return amelie_ship_blueprint()
        .protection_profile
        .shelters_from_weather_local(
            world_to_local_point(
                world_point));
}

auto ShipEntity::render_state(bool visible) const noexcept -> ShipRenderState {
    const auto& blueprint = amelie_ship_blueprint();

    return {
        visible,
        world_origin(),
        model_matrix(),
        &blueprint,
        blueprint.parts,
        blueprint.bounds,
        world_bounds(),
        blueprint.geometry_revision,
    };
}

auto ShipEntity::support_height(
    const glm::vec3& feet_position) const noexcept
    -> std::optional<float> {

    return support_height_in_range(
        feet_position,
        feet_position.y - kShipSupportProbeDepth,
        feet_position.y + kShipSupportProbeDepth);
}

auto ShipEntity::previous_support_height(
    const glm::vec3& feet_position) const noexcept
    -> std::optional<float> {

    return support_height_for_pose(
        feet_position,
        feet_position.y - kShipSupportProbeDepth,
        feet_position.y + kShipSupportProbeDepth,
        previous_world_origin(),
        motion_history_valid_
            ? previous_orientation_
            : orientation_);
}

auto ShipEntity::support_height_in_range(
    const glm::vec3& feet_position,
    float min_height,
    float max_height) const noexcept -> std::optional<float> {

    return support_height_for_pose(
        feet_position,
        min_height,
        max_height,
        world_origin(),
        orientation_);
}

auto ShipEntity::climb_contact(
    const glm::vec3& min_corner,
    const glm::vec3& max_corner) const noexcept
    -> std::optional<ShipClimbContact> {

    if (!std::isfinite(min_corner.x) ||
        !std::isfinite(min_corner.y) ||
        !std::isfinite(min_corner.z) ||
        !std::isfinite(max_corner.x) ||
        !std::isfinite(max_corner.y) ||
        !std::isfinite(max_corner.z) ||
        min_corner.x >= max_corner.x ||
        min_corner.y >= max_corner.y ||
        min_corner.z >= max_corner.z) {
        return std::nullopt;
    }

    const ShipBounds world_query {
        min_corner,
        max_corner,
    };
    const auto ship_origin = world_origin();

    for (const auto& part :
         amelie_ship_blueprint().parts) {
        if (part.shape !=
            ShipPartShape::ClimbableNet) {
            continue;
        }

        auto local_bounds = ShipBounds {
            glm::min(
                part.local_start,
                part.local_end),
            glm::max(
                part.local_start,
                part.local_end),
        };

        const auto raw_normal =
            finite_vec3_or(
                part.orientation,
                {1.0F, 0.0F, 0.0F});
        const auto local_normal =
            glm::dot(raw_normal, raw_normal) >
                    1.0e-8F
                ? glm::normalize(raw_normal)
                : glm::vec3 {1.0F, 0.0F, 0.0F};

        // La prise est plus genereuse que la corde visible, tout en restant
        // une boite orientee avec le navire et non une AABB monde grossiere.
        const auto grab_padding =
            glm::abs(local_normal) *
            std::max(
                kAmelieBoardingNetGrabHalfDepth,
                part.thickness * 0.5F);
        local_bounds.min -= grab_padding;
        local_bounds.max += grab_padding;

        if (!oriented_box_intersects_aabb(
                local_bounds,
                ship_origin,
                orientation_,
                world_query)) {
            continue;
        }

        const auto local_center =
            (local_bounds.min +
             local_bounds.max) *
            0.5F;
        const auto local_half_extents =
            (local_bounds.max -
             local_bounds.min) *
            0.5F;
        const auto local_plane_point =
            local_center +
            local_normal *
                glm::dot(
                    glm::abs(local_normal),
                    local_half_extents);

        auto deck_exit = local_to_world_point({
            local_normal.x *
                kAmelieBoardingDeckExitX,
            kAmelieMainDeckTop + 0.01F,
            (kAmelieBoardingNetMinZ +
             kAmelieBoardingNetMaxZ) *
                0.5F,
        });

        // Le point de sortie est pose sur la face de pont reelle. Cette
        // correction evite un decalage vertical quand le pont est incline.
        if (const auto support =
                support_height_in_range(
                    deck_exit,
                    deck_exit.y - 0.75F,
                    deck_exit.y + 0.75F);
            support.has_value()) {
            deck_exit.y = *support + 0.01F;
        }

        return ShipClimbContact {
            transformed_bounds(
                local_bounds,
                ship_origin,
                orientation_),
            local_bounds,
            local_to_world_direction(local_normal),
            local_to_world_point(
                local_plane_point),
            local_to_world_direction(
                {0.0F, 1.0F, 0.0F}),
            deck_exit,
        };
    }

    return std::nullopt;
}

auto ShipEntity::intersects_aabb(
    const glm::vec3& min_corner,
    const glm::vec3& max_corner) const noexcept -> bool {

    if (!std::isfinite(min_corner.x) ||
        !std::isfinite(min_corner.y) ||
        !std::isfinite(min_corner.z) ||
        !std::isfinite(max_corner.x) ||
        !std::isfinite(max_corner.y) ||
        !std::isfinite(max_corner.z) ||
        min_corner.x >= max_corner.x ||
        min_corner.y >= max_corner.y ||
        min_corner.z >= max_corner.z) {
        return false;
    }

    const ShipBounds world_query {
        min_corner,
        max_corner,
    };
    if (!aabbs_overlap(
            world_query,
            world_bounds())) {
        return false;
    }

    const auto ship_origin = world_origin();
    const auto& blueprint =
        amelie_ship_blueprint();
    const auto local_query =
        inverse_transformed_bounds(
            world_query,
            ship_origin,
            orientation_);
    if (!aabbs_overlap(
            local_query,
            blueprint.bounds)) {
        return false;
    }

    const ShipBounds clipped {
        glm::max(
            local_query.min,
            blueprint.bounds.min),
        glm::min(
            local_query.max,
            blueprint.bounds.max),
    };
    const auto min_x =
        static_cast<int>(
            std::floor(clipped.min.x));
    const auto min_y =
        static_cast<int>(
            std::floor(clipped.min.y));
    const auto min_z =
        static_cast<int>(
            std::floor(clipped.min.z));
    const auto max_x =
        static_cast<int>(
            std::floor(
                clipped.max.x -
                kCollisionEpsilon));
    const auto max_y =
        static_cast<int>(
            std::floor(
                clipped.max.y -
                kCollisionEpsilon));
    const auto max_z =
        static_cast<int>(
            std::floor(
                clipped.max.z -
                kCollisionEpsilon));
    const auto& index =
        ship_collision_index();

    // Une piece peut appartenir a plusieurs cellules. Ce tableau de generation
    // evite les tests SAT dupliques sans allocation dynamique par requete.
    thread_local std::vector<std::uint32_t>
        visit_generations;
    thread_local std::uint32_t visit_generation = 0U;
    if (visit_generations.size() <
        blueprint.parts.size()) {
        visit_generations.assign(
            blueprint.parts.size(),
            0U);
        visit_generation = 0U;
    }
    ++visit_generation;
    if (visit_generation == 0U) {
        std::fill(
            visit_generations.begin(),
            visit_generations.end(),
            0U);
        visit_generation = 1U;
    }

    for (int y = min_y;
         y <= max_y;
         ++y) {
        for (int z = min_z;
             z <= max_z;
             ++z) {
            for (int x = min_x;
                 x <= max_x;
                 ++x) {
                const auto cell =
                    index.cells.find(
                        {x, y, z});
                if (cell ==
                    index.cells.end()) {
                    continue;
                }

                for (const auto part_index :
                     cell->second) {
                    if (part_index >=
                        visit_generations.size() ||
                        visit_generations[part_index] ==
                            visit_generation) {
                        continue;
                    }
                    visit_generations[part_index] =
                        visit_generation;

                    if (oriented_box_intersects_aabb(
                            part_bounds(
                                blueprint.parts[
                                    part_index]),
                            ship_origin,
                            orientation_,
                            world_query)) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

auto ShipEntity::raycast_collidable_distance(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float max_distance) const noexcept
    -> std::optional<float> {

    if (!std::isfinite(origin.x) ||
        !std::isfinite(origin.y) ||
        !std::isfinite(origin.z) ||
        !std::isfinite(direction.x) ||
        !std::isfinite(direction.y) ||
        !std::isfinite(direction.z) ||
        !std::isfinite(max_distance) ||
        max_distance <= 0.0F ||
        glm::dot(direction, direction) <=
            1.0e-6F) {
        return std::nullopt;
    }

    // Une rotation rigide conserve les distances : le parametre t obtenu
    // dans l'espace local est directement reutilisable dans l'espace monde.
    const auto ray_direction =
        glm::normalize(direction);
    const auto local_origin =
        world_to_local_point(origin);
    const auto local_direction =
        world_to_local_direction(
            ray_direction);

    auto closest =
        std::optional<float> {};
    for (const auto& part :
         amelie_ship_blueprint().parts) {
        if (!part.collidable) {
            continue;
        }

        const auto hit =
            ray_aabb_distance(
                local_origin,
                local_direction,
                part_bounds(part),
                max_distance);
        if (hit.has_value() &&
            (!closest.has_value() ||
             *hit < *closest)) {
            closest = hit;
        }
    }

    return closest;
}

namespace {

auto supported_world_point_in_persisted_neutral_pose(
    const ShipEntity& ship,
    const glm::vec3& current_world_point,
    float current_support_height) noexcept
    -> std::optional<glm::vec3> {

    auto persisted_position =
        ship.world_point_in_persisted_neutral_pose(
            current_world_point);
    const auto persisted_origin =
        ship.position() -
        glm::vec3 {0.5F, 0.0F, 0.5F};

    // Je tiens compte du point le plus haut sous l'empreinte runtime, puis je
    // recale Y sur le pont plat sans perdre le petit ecart de contact.
    const auto persisted_support =
        support_height_for_pose(
            persisted_position,
            persisted_position.y - 0.50F,
            persisted_position.y +
                kShipSupportProbeDepth,
            persisted_origin,
            glm::quat {1.0F, 0.0F, 0.0F, 0.0F});
    if (!persisted_support.has_value()) {
        return std::nullopt;
    }

    persisted_position.y =
        *persisted_support +
        (current_world_point.y -
         current_support_height);
    return persisted_position;
}

} // namespace

auto normalize_supported_player_for_ship_save(
    const ShipEntity& ship,
    PlayerState& player_state,
    bool player_is_climbing_ship) noexcept -> bool {

    if (!player_state.on_ground ||
        player_state.dead ||
        player_state.fly_mode ||
        player_is_climbing_ship ||
        player_state.head_underwater ||
        player_state.swimming ||
        !std::isfinite(player_state.velocity.y) ||
        std::abs(player_state.velocity.y) >
            kShipRideVerticalSpeedTolerance) {
        return false;
    }

    const auto support =
        ship.support_height(player_state.position);
    if (!support.has_value() ||
        std::abs(player_state.position.y - *support) >
            kShipRideContactTolerance) {
        return false;
    }

    const auto persisted_position =
        supported_world_point_in_persisted_neutral_pose(
            ship,
            player_state.position,
            *support);
    if (!persisted_position.has_value()) {
        return false;
    }

    player_state.position = *persisted_position;
    // Je conserve l'impulsion horizontale du joueur et retire uniquement la
    // composante verticale incompatible avec un chargement pose sur le pont.
    player_state.velocity.y = 0.0F;
    player_state.fall_start_y = player_state.position.y;
    player_state.airborne_time = 0.0F;
    player_state.on_ground = true;
    return true;
}

auto normalize_supported_item_drop_for_ship_save(
    const ShipEntity& ship,
    ItemDrop& drop) noexcept -> bool {

    if (!drop.grounded) {
        return false;
    }

    const auto support =
        ship.support_height(drop.position);
    if (!support.has_value() ||
        std::abs(drop.position.y - *support) >
            kShipDropSaveContactTolerance) {
        return false;
    }

    const auto persisted_position =
        supported_world_point_in_persisted_neutral_pose(
            ship,
            drop.position,
            *support);
    if (!persisted_position.has_value()) {
        return false;
    }

    drop.position = *persisted_position;
    drop.velocity = {};
    drop.grounded = true;
    drop.sleeping = false;
    drop.sleep_support_valid = false;
    drop.sleep_candidate_seconds = 0.0F;
    drop.sleep_support_check_timer = 0.0F;
    drop.sleep_support_block = {};
    return true;
}

auto reconcile_loaded_ship_occupant(const ShipEntity& ship,
                                    const glm::vec3& saved_position,
                                    float half_width,
                                    float height,
                                    bool legacy_layout_present,
                                    ShipInvalidPositionPolicy invalid_position_policy,
                                    std::optional<glm::vec3> legacy_world_origin) noexcept
    -> ShipOccupantReconciliation {
    const auto sane_position = is_sane_ship_world_position(saved_position);
    if (!sane_position && invalid_position_policy == ShipInvalidPositionPolicy::Preserve) {
        // Je laisse le sanitizer du type concerne supprimer une entite corrompue;
        // seul le joueur utilise la politique de secours vers une ancre sure.
        return {saved_position, false};
    }
    const auto safe_half_width = std::clamp(finite_or(half_width, 0.30F), 0.05F, 2.0F);
    const auto safe_height = std::clamp(finite_or(height, 1.80F), 0.10F, 4.0F);
    const auto origin = ship.world_origin();
    const auto local =
        sane_position
            ? ship.world_to_local_point(
                  saved_position)
            : glm::vec3 {0.0F};
    const auto legacy_local =
        sane_position
            ? saved_position -
                  legacy_world_origin.value_or(
                      origin)
            : glm::vec3 {0.0F};

    // Je ne recale que les occupants plausiblement poses dans le navire. Un
    // nageur sous la coque ou un joueur deja eloigne conserve donc sa position.
    const auto in_habitable_height = sane_position && local.y >= 0.70F && local.y <= 6.50F;
    const auto overlaps_new_layout = in_habitable_height && ship.intersects_aabb(
        saved_position + glm::vec3 {-safe_half_width, 0.0F, -safe_half_width},
        saved_position + glm::vec3 {safe_half_width, safe_height, safe_half_width});
    const auto stood_on_legacy_surface =
        legacy_layout_present && sane_position && legacy_ship_supports_position(legacy_local, safe_half_width);
    const auto has_new_support = sane_position && ship.support_height(saved_position).has_value();
    if (sane_position && !overlaps_new_layout && !(stood_on_legacy_surface && !has_new_support)) {
        return {saved_position, false};
    }

    const auto& anchors = amelie_ship_blueprint().anchors;
    const std::array<glm::vec3, 9> candidates {{
        anchors.safe_spawn,
        anchors.lower_deck,
        anchors.captain_cabin,
        anchors.crew_quarters,
        anchors.galley,
        anchors.cargo_hold,
        anchors.helm,
        anchors.aft_hatch,
        anchors.fore_hatch,
    }};
    auto best_position =
        ship.local_to_world_point(
            anchors.safe_spawn);
    auto best_distance_squared = std::numeric_limits<float>::max();
    for (const auto& local_candidate : candidates) {
        const auto candidate =
            ship.local_to_world_point(
                local_candidate);
        const auto candidate_min = candidate + glm::vec3 {-safe_half_width, 0.0F, -safe_half_width};
        const auto candidate_max = candidate + glm::vec3 {safe_half_width, safe_height, safe_half_width};
        const auto support = ship.support_height_in_range(candidate, candidate.y - 0.25F, candidate.y + 0.25F);
        if (!support.has_value() || ship.intersects_aabb(candidate_min, candidate_max)) {
            continue;
        }
        const auto offset = sane_position ? candidate - saved_position : local_candidate - anchors.safe_spawn;
        const auto distance_squared = glm::dot(offset, offset);
        if (distance_squared < best_distance_squared) {
            best_distance_squared = distance_squared;
            best_position = candidate;
        }
    }
    return {best_position, true};
}

void SeaAdventureSystem::reset(int seed) noexcept {
    route_seed_ = hash_u32(static_cast<std::uint32_t>(seed));
    state_ = {};
    state_.active = true;
    state_.voyage_phase = SeaVoyagePhase::Moored;
    state_.voyage_phase_elapsed = 0.0F;
    state_.ship_position = {
        0.5F,
        kShipVisualY,
        0.5F,
    };
    state_.stamped_ship_x = ship_origin_x(state_);
    state_.stamped_ship_z = ship_origin_z(state_);
    state_.has_stamped_ship = false;
    precise_ship_position_z_ = static_cast<double>(state_.ship_position.z);
    precise_route_distance_ = static_cast<double>(state_.route_distance);
    ship_.set_position(state_.ship_position);
    ship_.set_velocity({});
    ocean_heave_ = 0.0F;
    ocean_heave_velocity_ = 0.0F;
    ocean_pitch_ = 0.0F;
    ocean_pitch_velocity_ = 0.0F;
    ocean_roll_ = 0.0F;
    ocean_roll_velocity_ = 0.0F;
    ship_.set_ocean_pose(0.0F, 0.0F, 0.0F);
    ship_.synchronize_motion_history();
    crew_.reset(seed, ship_);
    state_.crew = crew_.save_state();
    old_guard_.reset(seed);
    state_.old_guard = old_guard_.save_state();
    legacy_ship_migration_.reset();
}

void SeaAdventureSystem::load_state(const SeaAdventureSaveState& state, int world_seed) noexcept {
    state_ = sanitize_sea_adventure_save_state(state);
    // Je reconstruis la meme graine de route qu'une partie continue afin que
    // recharger une sauvegarde ne change jamais le prochain tirage de peche.
    route_seed_ = hash_u32(static_cast<std::uint32_t>(world_seed));
    precise_ship_position_z_ = static_cast<double>(state_.ship_position.z);
    precise_route_distance_ = static_cast<double>(state_.route_distance);
    ship_.set_position(state_.ship_position);
    ship_.set_velocity({});
    ocean_heave_ = 0.0F;
    ocean_heave_velocity_ = 0.0F;
    ocean_pitch_ = 0.0F;
    ocean_pitch_velocity_ = 0.0F;
    ocean_roll_ = 0.0F;
    ocean_roll_velocity_ = 0.0F;
    ship_.set_ocean_pose(0.0F, 0.0F, 0.0F);
    ship_.synchronize_motion_history();
    crew_.load_state(state_.crew, world_seed, ship_);
    state_.crew = crew_.save_state();
    old_guard_.load_state(state_.old_guard, world_seed);
    state_.old_guard = old_guard_.save_state();
    legacy_ship_migration_.reset();
}

auto SeaAdventureSystem::save_state() const noexcept -> const SeaAdventureSaveState& {
    return state_;
}

auto SeaAdventureSystem::active() const noexcept -> bool {
    return state_.active;
}

auto SeaAdventureSystem::ship_position() const noexcept -> glm::vec3 {
    return ship_.position();
}

auto SeaAdventureSystem::deck_spawn_position() const noexcept -> glm::vec3 {
    return ship_.local_to_world_point(
        amelie_ship_blueprint().anchors.safe_spawn);
}

auto SeaAdventureSystem::ship_entity() const noexcept -> const ShipEntity& {
    return ship_;
}

auto SeaAdventureSystem::ship_render_state() const noexcept -> ShipRenderState {
    return ship_.render_state(state_.active);
}

auto SeaAdventureSystem::crew_render_instances() const noexcept -> std::span<const CrewRenderInstance> {
    return state_.active ? crew_.render_instances() : std::span<const CrewRenderInstance> {};
}

auto SeaAdventureSystem::crew_members() const noexcept -> std::span<const ShipCrewMemberSaveState> {
    return state_.active ? crew_.members() : std::span<const ShipCrewMemberSaveState> {};
}

auto SeaAdventureSystem::old_guard_render_instances() const noexcept
    -> std::span<const OldGuardRenderInstance> {
    return state_.active
               ? old_guard_.render_instances()
               : std::span<const OldGuardRenderInstance> {};
}

auto SeaAdventureSystem::old_guard_members() const noexcept
    -> std::span<const OldGuardMemberSaveState> {
    return state_.active
               ? old_guard_.members()
               : std::span<const OldGuardMemberSaveState> {};
}

auto SeaAdventureSystem::old_guard_flashes() const noexcept
    -> std::span<const OldGuardMuzzleFlashInstance> {
    return state_.active
               ? old_guard_.flashes()
               : std::span<const OldGuardMuzzleFlashInstance> {};
}

auto SeaAdventureSystem::old_guard_smoke() const noexcept
    -> std::span<const OldGuardSmokeInstance> {
    return state_.active
               ? old_guard_.smoke()
               : std::span<const OldGuardSmokeInstance> {};
}

auto SeaAdventureSystem::hud_state(const PlayerController& player) const noexcept -> SeaAdventureHudState {
    SeaAdventureHudState hud {};
    hud.visible = state_.active;
    hud.on_ship = player_should_ride_ship(player);
    hud.fishing_active = state_.fishing_active;
    hud.phase = state_.voyage_phase;
    switch (state_.voyage_phase) {
    case SeaVoyagePhase::Moored:
        hud.departure_ratio = std::clamp(state_.voyage_phase_elapsed / kMooredBoardingSeconds, 0.0F, 1.0F);
        hud.departure_seconds_remaining = std::max(0.0F, kMooredBoardingSeconds - state_.voyage_phase_elapsed);
        break;
    case SeaVoyagePhase::Departing:
        hud.departure_ratio =
            std::clamp(state_.voyage_phase_elapsed / kDepartureAccelerationSeconds, 0.0F, 1.0F);
        hud.departure_seconds_remaining =
            std::max(0.0F, kDepartureAccelerationSeconds - state_.voyage_phase_elapsed);
        break;
    case SeaVoyagePhase::Underway:
        hud.departure_ratio = 1.0F;
        hud.departure_seconds_remaining = 0.0F;
        break;
    }
    hud.danger = player_ship_distance(player.position()) >= kStrandedWarningDistance ||
                 state_.hunger <= 18.0F ||
                 state_.thirst <= 18.0F;
    hud.hunger_ratio = clamp_ratio(state_.hunger);
    hud.thirst_ratio = clamp_ratio(state_.thirst);
    hud.stamina_ratio = clamp_ratio(state_.stamina);
    hud.fishing_ratio = state_.fishing_target_seconds > 0.0F
                            ? std::clamp(state_.fishing_progress / state_.fishing_target_seconds, 0.0F, 1.0F)
                            : 0.0F;
    hud.ship_distance = player_ship_distance(player.position());
    const auto velocity = ship_.velocity();
    hud.ship_speed = glm::length(glm::vec2 {velocity.x, velocity.z});
    hud.food_rations = state_.food_rations;
    hud.water_flasks = state_.water_flasks;
    hud.fish = state_.fish;
    if (!player.is_dead()) {
        // Le panneau n'apparait que sur une cible reellement visible ; le
        // raycast de l'equipage tient compte des cloisons du navire.
        hud.crew_focus = crew_.focus_from_ray(
            ship_,
            player.eye_position(),
            player.look_direction(),
            6.5F);
        hud.old_guard_focus = old_guard_.focus_from_ray(
            player.eye_position(),
            player.look_direction(),
            6.5F);
    }
    return hud;
}

void SeaAdventureSystem::stamp_ship(World& world) {
    begin_legacy_ship_migration(world);
    while (has_pending_legacy_ship_migration()) {
        (void)migrate_legacy_ship_step(
            world,
            std::numeric_limits<std::size_t>::max(),
            std::numeric_limits<double>::infinity());
    }
}

void SeaAdventureSystem::begin_legacy_ship_migration(World& world) {
    // Je garde le monde dans la signature pour rendre explicite la destination
    // de la migration, mais je ne le touche pas avant la premiere tranche.
    (void)world;
    if (legacy_ship_migration_.has_value() || !state_.active) {
        return;
    }

    if (state_.has_stamped_ship) {
        legacy_ship_migration_ = LegacyShipMigrationState {
            state_.stamped_ship_x,
            state_.stamped_ship_z,
            0U,
            0U,
        };
        return;
    }

    state_.stamped_ship_x = ship_origin_x(state_);
    state_.stamped_ship_z = ship_origin_z(state_);
    state_.has_stamped_ship = false;
}

auto SeaAdventureSystem::migrate_legacy_ship_step(World& world, std::size_t cell_budget, double max_ms)
    -> LegacyShipMigrationStats {
    using clock = std::chrono::steady_clock;
    LegacyShipMigrationStats stats {};
    if (!legacy_ship_migration_.has_value()) {
        return stats;
    }

    auto& migration = *legacy_ship_migration_;
    const auto& voxels = legacy_ship_v7_voxels();
    const auto time_limited = std::isfinite(max_ms);
    const auto deadline = time_limited
                              ? clock::now() + std::chrono::duration<double, std::milli>(std::max(0.0, max_ms))
                              : (clock::time_point::max)();
    while (migration.next_voxel < voxels.size() && stats.processed_cells < cell_budget) {
        if (time_limited && clock::now() >= deadline) {
            break;
        }

        const auto& voxel = voxels[migration.next_voxel++];
        const auto world_x = migration.origin_x + voxel.local_block.x;
        const auto world_y = static_cast<int>(kShipVisualY) + voxel.local_block.y;
        const auto world_z = migration.origin_z + voxel.local_block.z;
        if (world.restore_generated_cell(world_x, world_y, world_z)) {
            ++migration.restored_cells;
            ++stats.restored_cells;
        }
        ++stats.processed_cells;
    }

    if (migration.next_voxel == voxels.size()) {
        state_.stamped_ship_x = ship_origin_x(state_);
        state_.stamped_ship_z = ship_origin_z(state_);
        state_.has_stamped_ship = false;
        legacy_ship_migration_.reset();
        stats.pending_cells = 0U;
        stats.progress = 1.0F;
        return stats;
    }

    stats.pending_cells = voxels.size() - migration.next_voxel;
    stats.progress = voxels.empty()
                         ? 1.0F
                         : std::clamp(
                               static_cast<float>(migration.next_voxel) / static_cast<float>(voxels.size()),
                               0.0F,
                               1.0F);
    return stats;
}

auto SeaAdventureSystem::has_pending_legacy_ship_migration() const noexcept -> bool {
    return legacy_ship_migration_.has_value();
}

auto SeaAdventureSystem::legacy_ship_migration_progress() const noexcept -> float {
    if (!legacy_ship_migration_.has_value()) {
        return 1.0F;
    }
    const auto total = legacy_ship_v7_voxels().size();
    return total == 0U
               ? 1.0F
               : std::clamp(
                     static_cast<float>(legacy_ship_migration_->next_voxel) / static_cast<float>(total),
                     0.0F,
                     1.0F);
}

auto SeaAdventureSystem::update(
    World& world,
    PlayerController& player,
    const EnvironmentState& environment,
    float dt,
    bool request_fishing) -> SeaAdventureFrameResult {

    SeaAdventureFrameResult result {};
    if (!state_.active) {
        return result;
    }

    const auto safe_environment =
        sanitize_sea_environment(environment);
    const auto ocean =
        OceanSimulation::evaluate(
            safe_environment,
            OceanSimulation::surface_profile_for_world(
                world.generation_profile()));
    dt = std::clamp(
        finite_or(dt, 0.0F),
        0.0F,
        0.25F);

    // Je repare aussi les invariants purement runtime. Une valeur invalide ne
    // doit jamais survivre assez longtemps pour contaminer une sauvegarde.
    state_.hunger = std::clamp(
        finite_or(state_.hunger, 100.0F),
        0.0F,
        100.0F);
    state_.thirst = std::clamp(
        finite_or(state_.thirst, 100.0F),
        0.0F,
        100.0F);
    state_.stamina = std::clamp(
        finite_or(state_.stamina, 100.0F),
        0.0F,
        100.0F);

    if (!std::isfinite(
            state_.fishing_progress) ||
        !std::isfinite(
            state_.fishing_target_seconds) ||
        (state_.fishing_active &&
         state_.fishing_target_seconds <=
             0.0F)) {
        cancel_fishing();
    }

    // Ces trois tests sont effectues avant de modifier la pose. Le joueur est
    // ensuite transporte par la transformation rigide correspondant au point
    // exact ou se trouvent ses pieds, et non par le delta du centre du bateau.
    const auto player_was_on_ship =
        player_should_ride_ship(player);
    const auto player_was_climbing_ship =
        !player_was_on_ship &&
        !player.is_dead() &&
        !player.state().fly_mode &&
        player.is_climbing_dynamic_obstacle();
    const auto player_intersected_ship_before_move =
        player.overlaps_dynamic_obstacle(
            ship_);

    const auto requested_speed =
        kShipBaseSpeed *
        std::clamp(
            0.92F +
                safe_environment.wind_strength *
                    0.24F -
                safe_environment.storm_intensity *
                    0.16F,
            0.72F,
            1.28F);
    const auto motion_seconds =
        advance_voyage_phase(
            state_,
            dt,
            player_was_on_ship,
            result);

    const auto previous_ship_position =
        ship_.position();
    const auto maximum_position_z =
        static_cast<double>(
            maximum_ship_position_z());

    // Les doubles prives conservent les fractions de mouvement a grande
    // distance. Je les recale sur l'etat public si une ancienne execution les
    // a contamines avec NaN ou l'infini.
    precise_ship_position_z_ = std::clamp(
        std::isfinite(
            precise_ship_position_z_)
            ? precise_ship_position_z_
            : static_cast<double>(
                  previous_ship_position.z),
        0.5,
        maximum_position_z);
    const auto previous_precise_z =
        precise_ship_position_z_;
    const auto requested_position_z =
        previous_precise_z +
        static_cast<double>(
            requested_speed) *
            static_cast<double>(
                motion_seconds);
    precise_ship_position_z_ = std::clamp(
        std::isfinite(
            requested_position_z)
            ? requested_position_z
            : previous_precise_z,
        0.5,
        maximum_position_z);

    auto next_ship_position =
        previous_ship_position;
    next_ship_position.z =
        static_cast<float>(
            precise_ship_position_z_);

    // La pose precedente doit etre capturee juste avant la premiere mutation.
    // Elle reste valable jusqu'au prochain appel de update().
    ship_.begin_motion_step();
    ship_.set_position(
        next_ship_position);

    const auto& ship_bounds =
        amelie_ship_blueprint().bounds;
    const auto local_center_x =
        (ship_bounds.min.x +
         ship_bounds.max.x) *
        0.5F;
    const auto local_center_z =
        (ship_bounds.min.z +
         ship_bounds.max.z) *
        0.5F;
    const auto longitudinal_half_span =
        std::max(
            4.0F,
            (ship_bounds.max.z -
             ship_bounds.min.z) *
                0.32F);
    const auto lateral_half_span =
        std::max(
            2.0F,
            (ship_bounds.max.x -
             ship_bounds.min.x) *
                0.34F);

    // Le navire n'a actuellement aucun lacet : les cinq sondes peuvent donc
    // etre placees directement autour de son origine horizontale. Cette
    // methode evite une boucle de retroaction avec l'inclinaison precedente.
    const auto base_origin =
        next_ship_position -
        glm::vec3 {
            0.5F,
            0.0F,
            0.5F,
        };
    const auto sample_ocean =
        [&](float local_x,
            float local_z) noexcept {
            return OceanSimulation::sample(
                ocean,
                {
                    base_origin.x + local_x,
                    base_origin.z + local_z,
                },
                kOceanBuoyancyWaveCount);
        };

    const auto center_sample =
        sample_ocean(
            local_center_x,
            local_center_z);
    const auto bow_sample =
        sample_ocean(
            local_center_x,
            local_center_z +
                longitudinal_half_span);
    const auto stern_sample =
        sample_ocean(
            local_center_x,
            local_center_z -
                longitudinal_half_span);
    const auto starboard_sample =
        sample_ocean(
            local_center_x +
                lateral_half_span,
            local_center_z);
    const auto port_sample =
        sample_ocean(
            local_center_x -
                lateral_half_span,
            local_center_z);

    const auto motion_scale =
        sea_motion_scale(state_);
    auto target_heave =
        (
            center_sample.height * 0.44F +
            bow_sample.height * 0.14F +
            stern_sample.height * 0.14F +
            starboard_sample.height * 0.14F +
            port_sample.height * 0.14F
        ) *
        motion_scale;
    auto target_pitch =
        -std::atan2(
            bow_sample.height -
                stern_sample.height,
            longitudinal_half_span *
                2.0F) *
        motion_scale;
    auto target_roll =
        std::atan2(
            starboard_sample.height -
                port_sample.height,
            lateral_half_span *
                2.0F) *
        motion_scale;

    constexpr float kRadiansPerDegree =
        0.01745329251994329577F;
    const auto maximum_heave =
        std::clamp(
            ocean.maximum_displacement *
                    0.92F +
                0.05F,
            0.08F,
            3.60F) *
        motion_scale;
    const auto maximum_pitch =
        (2.5F +
         ocean.severity * 8.0F +
         ocean.tempest_factor * 3.5F) *
        kRadiansPerDegree *
        motion_scale;
    const auto maximum_roll =
        (4.0F +
         ocean.severity * 12.0F +
         ocean.tempest_factor * 6.0F) *
        kRadiansPerDegree *
        motion_scale;

    target_heave = std::clamp(
        target_heave,
        -maximum_heave,
        maximum_heave);
    target_pitch = std::clamp(
        target_pitch,
        -maximum_pitch,
        maximum_pitch);
    target_roll = std::clamp(
        target_roll,
        -maximum_roll,
        maximum_roll);

    // Les ressorts critiques filtrent les hautes frequences qui ne peuvent pas
    // deplacer instantanement une coque lourde et restent stables quel que soit
    // le framerate dans l'intervalle de dt autorise.
    integrate_critical_spring(
        ocean_heave_,
        ocean_heave_velocity_,
        target_heave,
        0.50F +
            ocean.severity * 0.08F,
        dt);
    integrate_critical_spring(
        ocean_pitch_,
        ocean_pitch_velocity_,
        target_pitch,
        0.38F +
            ocean.severity * 0.06F,
        dt);
    integrate_critical_spring(
        ocean_roll_,
        ocean_roll_velocity_,
        target_roll,
        0.31F +
            ocean.severity * 0.06F,
        dt);
    // Je borne aussi l'état mémorisé du ressort. Un outil ou un mod peut
    // remplacer instantanément une Tempest par du calme ; le navire doit alors
    // rester dans l'enveloppe de la surface effectivement rendue.
    clamp_spring_state(
        ocean_heave_,
        ocean_heave_velocity_,
        maximum_heave);
    clamp_spring_state(
        ocean_pitch_,
        ocean_pitch_velocity_,
        maximum_pitch);
    clamp_spring_state(
        ocean_roll_,
        ocean_roll_velocity_,
        maximum_roll);
    ship_.set_ocean_pose(
        ocean_heave_,
        ocean_pitch_,
        ocean_roll_);

    // Je conserve le contrat historique de ship_delta : il decrit uniquement
    // la translation de route persistante. Le pilonnement et la rotation sont
    // transitoires et les occupants les recuperent via motion_delta_at().
    result.ship_delta =
        next_ship_position -
        previous_ship_position;
    const auto pose_origin_delta =
        ship_.world_origin() -
        ship_.previous_world_origin();

    const auto precise_delta_z =
        precise_ship_position_z_ -
        previous_precise_z;
    result.ship_speed =
        dt > 0.0F
            ? static_cast<float>(
                  std::abs(
                      precise_delta_z) /
                  static_cast<double>(dt))
            : 0.0F;

    ship_.set_velocity(
        dt > 0.0F
            ? pose_origin_delta / dt
            : glm::vec3 {});
    state_.ship_position =
        ship_.position();

    precise_route_distance_ = std::clamp(
        std::isfinite(
            precise_route_distance_)
            ? precise_route_distance_
            : static_cast<double>(
                  state_.route_distance),
        0.0,
        static_cast<double>(
            kShipCoordinateLimit));
    const auto requested_route_distance =
        precise_route_distance_ +
        std::abs(precise_delta_z);
    precise_route_distance_ = std::clamp(
        std::isfinite(
            requested_route_distance)
            ? requested_route_distance
            : precise_route_distance_,
        0.0,
        static_cast<double>(
            kShipCoordinateLimit));
    state_.route_distance =
        static_cast<float>(
            precise_route_distance_);

    const auto move_player_with_ship =
        [&](bool resolve_support) {
            const auto platform_delta =
                ship_.motion_delta_at(
                    player.position());
            const auto moved =
                glm::dot(
                    platform_delta,
                    platform_delta) >
                1.0e-12F;

            if (moved) {
                player.translate_platform_delta(
                    platform_delta);
            }

            if (resolve_support) {
                if (const auto support =
                        ship_.support_height(
                            player.position());
                    support.has_value()) {
                    player.resolve_dynamic_platform_support(
                        *support);
                }
            }

            result.ship_moved_player =
                result.ship_moved_player ||
                moved;
        };

    if (player_was_on_ship) {
        move_player_with_ship(true);
    } else if (player_was_climbing_ship) {
        // Le grimpeur suit le point du filet auquel il est accroche, sans etre
        // considere comme embarque tant que ses pieds ne touchent pas le pont.
        move_player_with_ship(false);
    } else if (
        !player_intersected_ship_before_move &&
        player.overlaps_dynamic_obstacle(
            ship_)) {
        // Si la coque penetre le joueur durant ce pas, on applique le mouvement
        // local de la coque. La resolution AABB du controleur terminera le
        // depoussage au prochain sous-pas sans coller le joueur au navire.
        move_player_with_ship(false);
    }

    state_.stamped_ship_x = ship_origin_x(state_);
    state_.stamped_ship_z = ship_origin_z(state_);
    state_.has_stamped_ship = false;

    const auto player_now_on_ship =
        player_should_ride_ship(player);

    result.on_ship = player_now_on_ship;
    result.ship_distance =
        player_ship_distance(player.position());

    const auto swimming = player.state().swimming;

    const auto weather_pressure =
        1.0F +
        safe_environment.precipitation_intensity * 0.20F +
        safe_environment.storm_intensity * 0.35F;

    state_.hunger = std::max(
        0.0F,
        state_.hunger -
            dt *
                (player_now_on_ship
                     ? kAboardHungerLossPerSecond
                     : kAwayHungerLossPerSecond) *
                weather_pressure);

    state_.thirst = std::max(
        0.0F,
        state_.thirst -
            dt *
                (player_now_on_ship
                     ? kAboardThirstLossPerSecond
                     : kAwayThirstLossPerSecond) *
                weather_pressure);

    if (swimming) {
        state_.stamina = std::max(
            0.0F,
            state_.stamina -
                dt *
                    (7.0F +
                     safe_environment.storm_intensity * 9.0F));
    } else {
        state_.stamina = std::min(
            100.0F,
            state_.stamina +
                dt *
                    (player_now_on_ship ? 8.5F : 5.2F));
    }

    // Je credite les livraisons avant le service et les degats de survie :
    // une ressource rapportee sur cette frame doit pouvoir sauver le joueur.
    const auto crew_result =
        crew_.update(
            ship_,
            safe_environment,
            dt,
            state_.fish,
            state_.water_flasks,
            player.position());

    result.crew_fish_delivered =
        crew_result.fish_delivered;

    result.crew_water_delivered =
        crew_result.water_delivered;

    state_.crew = crew_.save_state();

    consume_automatic_supplies(
        player_now_on_ship,
        result);

    // Ces drapeaux decrivent l'etat de la frame, pas seulement la frame exacte
    // ou un tick de degat est applique.
    result.starving = state_.hunger <= 0.0F;
    result.dehydrating = state_.thirst <= 0.0F;

    const auto exhausted_at_sea =
        swimming && state_.stamina <= 0.0F;

    if (result.starving ||
        result.dehydrating ||
        exhausted_at_sea) {

        state_.survival_damage_timer += dt;

        while (
            state_.survival_damage_timer >=
                kSurvivalDamageInterval &&
            !player.is_dead()) {

            const auto cause =
                exhausted_at_sea
                    ? PlayerDeathCause::Drowning
                    : (result.dehydrating
                           ? PlayerDeathCause::Thirst
                           : PlayerDeathCause::Starvation);

            player.apply_environmental_damage(
                exhausted_at_sea ? 3.0F : 2.0F,
                cause);

            state_.survival_damage_timer -=
                kSurvivalDamageInterval;
        }
    } else {
        state_.survival_damage_timer = 0.0F;
    }

    if (state_.fishing_active &&
        !player_now_on_ship) {

        // Je coupe la partie de peche des que je quitte le pont : sa progression
        // ne doit jamais continuer a distance pendant que le navire s'eloigne.
        cancel_fishing();
        result.fishing_failed = true;
    }

    if (request_fishing &&
        player_now_on_ship &&
        !state_.fishing_active &&
        !player.is_dead()) {

        state_.fishing_active = true;
        state_.fishing_progress = 0.0F;

        const auto night_factor =
            (safe_environment.time_of_day >= 19.0F ||
             safe_environment.time_of_day < 5.0F)
                ? kFishingNightBonus
                : 0.0F;

        const auto route_roll =
            static_cast<float>(
                hash_u32(
                    route_seed_ ^
                    route_roll_key(state_.route_distance)) %
                120U) /
            120.0F;

        state_.fishing_target_seconds =
            kFishingBaseSeconds +
            safe_environment.storm_intensity *
                kFishingStormPenalty -
            night_factor +
            route_roll * 2.0F;

        result.fishing_started = true;
    } else if (
        request_fishing &&
        (!player_now_on_ship || player.is_dead())) {

        result.fishing_failed = true;
    }

    if (state_.fishing_active &&
        !player.is_dead()) {

        state_.fishing_progress +=
            dt *
            std::clamp(
                1.0F -
                    safe_environment.storm_intensity *
                        0.22F,
                0.42F,
                1.0F);

        if (state_.fishing_progress >=
            state_.fishing_target_seconds) {

            state_.fishing_active = false;
            state_.fishing_progress = 0.0F;
            state_.fishing_target_seconds = 0.0F;

            saturating_add(state_.fish, 1U);

            state_.hunger = std::min(
                100.0F,
                state_.hunger + 18.0F);

            result.fish_caught = true;
        }
    }

    if (result.ship_distance >=
        kStrandedLossDistance) {

        state_.stranded_warning_timer = 0.0F;

        if (!player.is_dead()) {
            // "Perdu en mer" est une condition de defaite, pas une attaque :
            // aucune armure ni invulnerabilite ne doit pouvoir l'annuler.
            player.force_death(
                PlayerDeathCause::Stranded);

            result.stranded = true;
        }
    } else if (
        result.ship_distance >=
        kStrandedWarningDistance) {

        state_.stranded_warning_timer += dt;

        if (state_.stranded_warning_timer >=
            kStrandedWarningInterval) {

            state_.stranded_warning_timer = 0.0F;
            result.stranded_warning = true;
        }
    } else {
        state_.stranded_warning_timer = 0.0F;
    }

    return result;
}

auto SeaAdventureSystem::update_old_guard_combat(
    World& world,
    CreatureSystem& creatures,
    const PlayerController& player,
    const EnvironmentState& environment,
    float dt) -> const OldGuardFrameEvents& {
    static const OldGuardFrameEvents kNoEvents {};
    if (!state_.active) {
        old_guard_.clear_transient_effects();
        return kNoEvents;
    }

    std::array<OldGuardTargetCandidate, kCreatureMaxActiveCount> targets {};
    auto target_count = std::size_t {0U};
    for (const auto& creature : creatures.active_creatures()) {
        if (target_count >= targets.size() ||
            !std::isfinite(creature.position.x) ||
            !std::isfinite(creature.position.y) ||
            !std::isfinite(creature.position.z) ||
            !std::isfinite(creature.health) ||
            creature.health <= 0.0F) {
            continue;
        }

        auto aim_height = 0.92F;
        auto body_radius = 0.48F;
        switch (creature.anchor.species) {
        case CreatureSpecies::Cow:
            aim_height = 1.12F;
            body_radius = 0.62F;
            break;
        case CreatureSpecies::Villager:
            aim_height = 1.35F;
            body_radius = 0.42F;
            break;
        case CreatureSpecies::Sheep:
            aim_height = 0.90F;
            body_radius = 0.52F;
            break;
        case CreatureSpecies::Pig:
        default:
            break;
        }

        auto& candidate = targets[target_count++];
        candidate.position = creature.position;
        candidate.aim_position =
            creature.position +
            glm::vec3 {0.0F, aim_height, 0.0F};
        candidate.body_radius = body_radius;
        candidate.morph_factor = creature.morph_factor;
        candidate.health = creature.health;
        candidate.stable_id =
            creature_id_from_anchor(creature.anchor);
        candidate.species = creature.anchor.species;
        candidate.phase = creature.phase;
    }

    std::array<OldGuardOccupant,
               1U + kShipCrewMemberCount + kOldGuardMemberCount> occupants {};
    auto occupant_count = std::size_t {0U};
    if (!player.is_dead()) {
        occupants[occupant_count++] = {
            .position =
                player.position() +
                glm::vec3 {0.0F, 0.90F, 0.0F},
            .radius = 0.42F,
            .priority = OldGuardOccupantPriority::Player,
            .blocks_shot = true,
        };
    }
    for (const auto& member : crew_.render_instances()) {
        occupants[occupant_count++] = {
            .position =
                member.position +
                glm::vec3 {0.0F, 0.88F, 0.0F},
            .radius = 0.40F,
            .priority = OldGuardOccupantPriority::CrewTask,
            .blocks_shot = true,
        };
    }
    for (const auto& guard : old_guard_.render_instances()) {
        occupants[occupant_count++] = {
            .position =
                guard.position +
                glm::vec3 {0.0F, 0.92F, 0.0F},
            .radius = 0.42F,
            .priority = OldGuardOccupantPriority::Guard,
            .blocks_shot = true,
        };
    }

    const auto unobstructed_to =
        [&](const glm::vec3& origin,
            const glm::vec3& target,
            bool collidable) {
            const auto delta = target - origin;
            const auto distance_squared = glm::dot(delta, delta);
            if (!std::isfinite(distance_squared) ||
                distance_squared <= 1.0e-8F) {
                return false;
            }
            const auto distance = std::sqrt(distance_squared);
            const auto direction = delta / distance;
            const auto world_hit =
                collidable
                    ? world.raycast_collidable(
                          origin,
                          direction,
                          distance)
                    : world.raycast_visibility(
                          origin,
                          direction,
                          distance);
            constexpr auto kLineTolerance = 0.025F;
            if (world_hit.hit &&
                world_hit.distance <
                    distance - kLineTolerance) {
                return false;
            }
            const auto ship_hit =
                ship_.raycast_collidable_distance(
                    origin,
                    direction,
                    distance);
            return !ship_hit.has_value() ||
                   *ship_hit >=
                       distance - kLineTolerance;
        };

    const auto wind_direction =
        glm::vec3 {
            environment.wind_direction_xz.x,
            0.0F,
            environment.wind_direction_xz.y,
        };
    const auto safe_wind_strength =
        std::isfinite(environment.wind_strength)
            ? std::clamp(environment.wind_strength, 0.0F, 1.0F)
            : 0.0F;
    const auto wind_velocity =
        wind_direction * (0.45F + safe_wind_strength * 3.2F);
    old_guard_.update_effects(dt, wind_velocity);

    OldGuardUpdateContext context {};
    context.platform = {
        .world_origin = ship_.world_origin(),
        .velocity = ship_.velocity(),
        .orientation = ship_.orientation(),
    };
    context.targets = std::span<const OldGuardTargetCandidate> {
        targets.data(),
        target_count,
    };
    context.occupants = std::span<const OldGuardOccupant> {
        occupants.data(),
        occupant_count,
    };
    context.visibility_clear =
        [&](const glm::vec3& origin,
            const glm::vec3& target) {
            return unobstructed_to(
                origin,
                target,
                false);
        };
    context.melee_clear =
        [&](const glm::vec3& origin,
            const glm::vec3& target) {
            return unobstructed_to(
                origin,
                target,
                true);
        };
    context.shot_clear =
        [&](const glm::vec3& origin,
            const glm::vec3& direction,
            float maximum_distance,
            std::uint64_t intended_target) {
            if (!std::isfinite(maximum_distance) ||
                maximum_distance <= 0.0F ||
                maximum_distance >
                    kOldGuardMusketRange + 0.05F) {
                return false;
            }
            const auto first_creature =
                creatures.raycast_first_creature(
                    origin,
                    direction,
                    maximum_distance);
            if (!first_creature.hit ||
                first_creature.id != intended_target) {
                return false;
            }

            constexpr auto kImpactTolerance = 0.025F;
            const auto world_hit =
                world.raycast_collidable(
                    origin,
                    direction,
                    maximum_distance);
            if (world_hit.hit &&
                world_hit.distance <
                    first_creature.distance -
                        kImpactTolerance) {
                return false;
            }
            const auto ship_hit =
                ship_.raycast_collidable_distance(
                    origin,
                    direction,
                    maximum_distance);
            return !ship_hit.has_value() ||
                   *ship_hit >=
                       first_creature.distance -
                           kImpactTolerance;
        };
    context.wind_velocity = wind_velocity;
    context.sky_light =
        std::clamp(
            std::isfinite(environment.daylight_factor)
                ? environment.daylight_factor
                : 1.0F,
            0.0F,
            1.0F);
    context.local_light =
        std::clamp(
            0.18F +
                (1.0F - context.sky_light) * 0.30F,
            0.0F,
            1.0F);
    context.precipitation_exposure = 1.0F;

    const auto& events =
        old_guard_.update(context, dt);
    for (const auto& shot : events.shots) {
        (void)creatures.apply_damage(
            shot.target_id,
            shot.damage,
            CreatureDamageSource::OldGuard,
            shot.direction);
    }
    for (const auto& bayonet : events.bayonet_hits) {
        (void)creatures.apply_damage(
            bayonet.target_id,
            bayonet.damage,
            CreatureDamageSource::OldGuard,
            bayonet.direction);
    }
    state_.old_guard = old_guard_.save_state();
    return events;
}

auto SeaAdventureSystem::collect_resource(
    BlockId block_id) noexcept -> bool {

    if (!state_.active) {
        return false;
    }

    block_id = block_item_id(block_id);

    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Wood:
    case BlockType::PineWood:
    case BlockType::Planks:
        saturating_add(state_.wood, 1U);
        return true;

    case BlockType::Stone:
    case BlockType::Cobblestone:
    case BlockType::MossyStone:
    case BlockType::Gravel:
        saturating_add(state_.stone, 1U);
        return true;

    case BlockType::Leaves:
    case BlockType::PineLeaves:
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::Cactus:
        saturating_add(state_.fiber, 1U);
        state_.hunger = std::min(
            100.0F,
            state_.hunger + 4.0F);
        return true;

    case BlockType::Water:
        saturating_add(state_.water_flasks, 1U);
        state_.thirst = std::min(
            100.0F,
            state_.thirst + 10.0F);
        return true;

    default:
        return false;
    }
}

auto SeaAdventureSystem::record_hunt(
    CreatureSpecies species) noexcept -> bool {

    if (!state_.active) {
        return false;
    }

    switch (species) {
    case CreatureSpecies::Pig:
    case CreatureSpecies::Cow:
    case CreatureSpecies::Sheep:
        saturating_add(
            state_.food_rations,
            species == CreatureSpecies::Cow ? 3U : 2U);

        state_.hunger = std::min(
            100.0F,
            state_.hunger +
                (species == CreatureSpecies::Cow
                     ? 22.0F
                     : 16.0F));

        return true;

    case CreatureSpecies::Villager:
    default:
        return false;
    }
}

auto SeaAdventureSystem::try_damage_crew(const glm::vec3& origin,
                                         const glm::vec3& direction,
                                         float max_distance,
                                         float damage) noexcept -> ShipCrewDamageResult {
    if (!state_.active) {
        return {};
    }
    auto result = crew_.try_damage_from_player(ship_, origin, direction, max_distance, damage);
    if (result.hit) {
        state_.crew = crew_.save_state();
    }
    return result;
}

auto SeaAdventureSystem::intercept_old_guard(const glm::vec3& origin,
                                             const glm::vec3& direction,
                                             float max_distance) const noexcept -> OldGuardRayHit {
    if (!state_.active) {
        return {};
    }
    return old_guard_.intercept_ray(
        origin,
        direction,
        max_distance);
}

void SeaAdventureSystem::cancel_fishing() noexcept {
    state_.fishing_active = false;
    state_.fishing_progress = 0.0F;
    state_.fishing_target_seconds = 0.0F;
}

void SeaAdventureSystem::on_player_respawn() noexcept {
    if (!state_.active) {
        return;
    }

    // Le voyage, l'equipage et les stocks restent inchanges. Seuls les etats
    // transitoires pouvant tuer a nouveau le joueur sont remis en securite.
    cancel_fishing();

    state_.survival_damage_timer = 0.0F;
    state_.stranded_warning_timer = 0.0F;

    state_.hunger = std::clamp(
        finite_or(
            state_.hunger,
            kRespawnMinimumHunger),
        kRespawnMinimumHunger,
        100.0F);

    state_.thirst = std::clamp(
        finite_or(
            state_.thirst,
            kRespawnMinimumThirst),
        kRespawnMinimumThirst,
        100.0F);

    state_.stamina = kRespawnStamina;
}

auto SeaAdventureSystem::player_should_ride_ship(const PlayerController& player) const noexcept -> bool {
    return state_.active &&
           !player.is_dead() &&
           !player.state().fly_mode &&
           std::abs(player.state().velocity.y) <= kShipRideVerticalSpeedTolerance &&
           player_on_ship(player.position());
}

auto SeaAdventureSystem::player_on_ship(const glm::vec3& player_position) const noexcept -> bool {
    const auto support_height = ship_.support_height(player_position);
    return support_height.has_value() &&
           std::abs(player_position.y - *support_height) <= kShipRideContactTolerance;
}

auto SeaAdventureSystem::player_ship_distance(const glm::vec3& player_position) const noexcept -> float {
    const glm::vec2 delta {
        player_position.x - state_.ship_position.x,
        player_position.z - state_.ship_position.z,
    };
    return glm::length(delta);
}

void SeaAdventureSystem::consume_automatic_supplies(
    bool player_on_ship,
    SeaAdventureFrameResult& result) noexcept {

    if (!player_on_ship) {
        return;
    }

    // Je laisse la jauge atteindre un seuil de confort avant le service pour
    // ne jamais gaspiller une unite a chaque frame, puis le repas la remplit.
    if (state_.hunger <=
        kAutomaticMealThreshold) {

        if (state_.fish > 0U) {
            --state_.fish;
            result.consumed_food = true;
        } else if (state_.food_rations > 0U) {
            --state_.food_rations;
            result.consumed_food = true;
        }

        if (result.consumed_food) {
            state_.hunger = 100.0F;
        }
    }

    if (state_.thirst <=
            kAutomaticDrinkThreshold &&
        state_.water_flasks > 0U) {

        --state_.water_flasks;
        state_.thirst = 100.0F;
        result.consumed_water = true;
    }
}

} // namespace valcraft
