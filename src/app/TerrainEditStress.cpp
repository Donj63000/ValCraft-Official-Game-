#include "app/TerrainEditStress.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace valcraft {

namespace {

constexpr std::array<int, 9> kPerpendicularOffsets {{
    0,
    -1,
    1,
    -2,
    2,
    -3,
    3,
    -4,
    4,
}};

[[nodiscard]] auto finite_floor_to_int(float value)
    -> std::optional<int> {
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    const auto floored = std::floor(static_cast<double>(value));
    if (floored <
            static_cast<double>((std::numeric_limits<int>::lowest)()) ||
        floored >
            static_cast<double>((std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }
    return static_cast<int>(floored);
}

[[nodiscard]] auto operation_action(
    BlockId next_block) noexcept -> TerrainEditStressAction {
    return next_block == to_block_id(BlockType::Air)
               ? TerrainEditStressAction::Break
               : TerrainEditStressAction::Place;
}

} // namespace

auto terrain_edit_stress_enabled(
    const GameOptions& options) noexcept -> bool {
    return options.smoke_test &&
           options.performance.perf_scenario ==
               "terrain_edit_stress";
}

auto TerrainEditStressScenario::update(
    World& world,
    const glm::vec3& focus,
    std::size_t frame_index,
    bool allow_new_pair)
    -> std::optional<TerrainEditStressOperation> {

    if (frame_index % kTerrainEditStressIntervalFrames != 0U ||
        last_update_frame_ == frame_index) {
        return std::nullopt;
    }
    last_update_frame_ = frame_index;

    if (pending_restore_.has_value()) {
        const auto pending = *pending_restore_;
        const auto previous =
            world.get_block(
                pending.block.x,
                pending.block.y,
                pending.block.z);
        world.set_block(
            pending.block.x,
            pending.block.y,
            pending.block.z,
            pending.original_block);
        if (world.get_block(
                pending.block.x,
                pending.block.y,
                pending.block.z) !=
            pending.original_block) {
            return std::nullopt;
        }

        pending_restore_.reset();
        ++completed_pair_count_;
        return TerrainEditStressOperation {
            pending.action,
            pending.block,
            previous,
            pending.original_block,
            pending.pair_index,
        };
    }

    if (!allow_new_pair) {
        return std::nullopt;
    }

    const auto target = select_target(world, focus);
    if (!target.has_value()) {
        return std::nullopt;
    }

    world.set_block(
        target->block.x,
        target->block.y,
        target->block.z,
        target->edited_block);
    if (world.get_block(
            target->block.x,
            target->block.y,
            target->block.z) !=
        target->edited_block) {
        return std::nullopt;
    }

    pending_restore_ = PendingRestore {
        target->block,
        target->original_block,
        target->restore_action,
        completed_pair_count_,
    };
    return TerrainEditStressOperation {
        target->edit_action,
        target->block,
        target->original_block,
        target->edited_block,
        completed_pair_count_,
    };
}

void TerrainEditStressScenario::reset() noexcept {
    pending_restore_.reset();
    last_update_frame_.reset();
    completed_pair_count_ = 0U;
}

auto TerrainEditStressScenario::has_pending_restore() const noexcept
    -> bool {
    return pending_restore_.has_value();
}

auto TerrainEditStressScenario::completed_pair_count() const noexcept
    -> std::size_t {
    return completed_pair_count_;
}

auto TerrainEditStressScenario::select_target(
    World& world,
    const glm::vec3& focus) const -> std::optional<Target> {

    const auto focus_x = finite_floor_to_int(focus.x);
    const auto focus_z = finite_floor_to_int(focus.z);
    if (!focus_x.has_value() || !focus_z.has_value()) {
        return std::nullopt;
    }

    const auto chunk_x =
        World::floor_div(*focus_x, kChunkSizeX);
    const auto chunk_z =
        World::floor_div(*focus_z, kChunkSizeZ);
    const auto chunk_min_x =
        static_cast<std::int64_t>(chunk_x) *
        static_cast<std::int64_t>(kChunkSizeX);
    const auto chunk_min_z =
        static_cast<std::int64_t>(chunk_z) *
        static_cast<std::int64_t>(kChunkSizeZ);

    // Je fais tourner les quatre cotes et leurs deux proprietaires. Les
    // invalidations couvrent ainsi x=0/x=15 puis z=0/z=15, y compris lorsque
    // les coordonnees monde deviennent negatives pendant un futur smoke.
    const auto target_side = completed_pair_count_ % 4U;
    const auto make_candidate =
        [&](int perpendicular_offset) -> std::optional<BlockCoord> {
        auto candidate_x = std::int64_t {0};
        auto candidate_z = std::int64_t {0};
        switch (target_side) {
        case 0U:
            candidate_x =
                chunk_min_x +
                static_cast<std::int64_t>(kChunkSizeX - 1);
            candidate_z =
                static_cast<std::int64_t>(*focus_z) +
                perpendicular_offset;
            break;
        case 1U:
            candidate_x = chunk_min_x;
            candidate_z =
                static_cast<std::int64_t>(*focus_z) +
                perpendicular_offset;
            break;
        case 2U:
            candidate_x =
                static_cast<std::int64_t>(*focus_x) +
                perpendicular_offset;
            candidate_z =
                chunk_min_z +
                static_cast<std::int64_t>(kChunkSizeZ - 1);
            break;
        case 3U:
        default:
            candidate_x =
                static_cast<std::int64_t>(*focus_x) +
                perpendicular_offset;
            candidate_z = chunk_min_z;
            break;
        }

        if (candidate_x <
                static_cast<std::int64_t>(
                    (std::numeric_limits<int>::lowest)()) +
                    4LL ||
            candidate_x >
                static_cast<std::int64_t>(
                    (std::numeric_limits<int>::max)()) -
                    4LL ||
            candidate_z <
                static_cast<std::int64_t>(
                    (std::numeric_limits<int>::lowest)()) +
                    4LL ||
            candidate_z >
                static_cast<std::int64_t>(
                    (std::numeric_limits<int>::max)()) -
                    4LL) {
            return std::nullopt;
        }

        return BlockCoord {
            static_cast<int>(candidate_x),
            0,
            static_cast<int>(candidate_z),
        };
    };

    for (const auto offset : kPerpendicularOffsets) {
        const auto candidate_coord = make_candidate(offset);
        if (!candidate_coord.has_value()) {
            continue;
        }
        auto candidate = *candidate_coord;
        candidate.y =
            world.surface_height(candidate.x, candidate.z);
        if (candidate.y <= kWorldMinY ||
            candidate.y >= kWorldMaxY ||
            world.water_level(
                candidate.x,
                candidate.y + 1,
                candidate.z) != 0U) {
            continue;
        }

        auto dry_neighborhood = true;
        for (int dz = -2; dz <= 2 && dry_neighborhood; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                for (int dy = -1; dy <= 2; ++dy) {
                    if (world.peek_water_level_or_generated(
                            candidate.x + dx,
                            candidate.y + dy,
                            candidate.z + dz) != 0U) {
                        dry_neighborhood = false;
                        break;
                    }
                }
                if (!dry_neighborhood) {
                    break;
                }
            }
        }
        if (!dry_neighborhood) {
            continue;
        }

        const auto original =
            world.get_block(
                candidate.x,
                candidate.y,
                candidate.z);
        if (!is_organic_terrain_block(original)) {
            continue;
        }
        return Target {
            candidate,
            original,
            to_block_id(BlockType::Air),
            TerrainEditStressAction::Break,
            TerrainEditStressAction::Place,
        };
    }

    // Une cote entierement maritime ou construite ne doit jamais neutraliser
    // le benchmark. Je place alors une pierre organique dans une cellule d'air
    // generee, puis je la retire a l'etape suivante.
    for (const auto offset : kPerpendicularOffsets) {
        const auto candidate_coord = make_candidate(offset);
        if (!candidate_coord.has_value()) {
            continue;
        }
        auto candidate = *candidate_coord;
        const auto surface_y =
            world.surface_height(candidate.x, candidate.z);
        const auto first_y =
            std::max(surface_y + 2, kSeaLevel + 2);
        for (int y = first_y;
             y <= std::min(first_y + 6, kWorldMaxY);
             ++y) {
            const auto original =
                world.get_block(candidate.x, y, candidate.z);
            if (original != to_block_id(BlockType::Air) ||
                world.water_level(candidate.x, y, candidate.z) != 0U) {
                continue;
            }
            candidate.y = y;
            const auto edited = to_block_id(BlockType::Stone);
            return Target {
                candidate,
                original,
                edited,
                operation_action(edited),
                operation_action(original),
            };
        }
    }

    return std::nullopt;
}

} // namespace valcraft
