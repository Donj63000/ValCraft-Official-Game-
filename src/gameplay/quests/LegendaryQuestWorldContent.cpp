#include "gameplay/quests/LegendaryQuestWorldContent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

constexpr std::size_t kRumorEditCount = 4U;
constexpr std::size_t kFragmentEditCount = 4U;
constexpr std::size_t kForgeEditCount = 48U;
constexpr std::size_t kForgeShellEditCount = 4U;
constexpr std::uint64_t kContentSignatureSalt =
    0xD6E8FEB86659FD93ULL;
constexpr std::uint64_t kProtectionIdSalt =
    0xA5A3564E27F8862FULL;
constexpr std::uint64_t kProtectionIndexSalt =
    0x9E3779B97F4A7C15ULL;

struct LocalCell {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    BlockType block = BlockType::Air;
};

struct FeatureRecipe {
    LegendaryQuestForgeFeature feature =
        LegendaryQuestForgeFeature::GiantTools;
    std::span<const LocalCell> cells {};
    LegendaryQuestWorldPoint interaction_offset {};
    std::string_view feature_id {};
};

constexpr std::array<LocalCell, 6U> kGiantTools {
    LocalCell {-7, 0, 5, BlockType::PineWood},
    LocalCell {-7, 1, 5, BlockType::PineWood},
    LocalCell {-7, 2, 5, BlockType::PineWood},
    LocalCell {-7, 3, 5, BlockType::PineWood},
    LocalCell {-8, 3, 5, BlockType::IronOre},
    LocalCell {-6, 3, 5, BlockType::IronOre},
};

constexpr std::array<LocalCell, 5U> kBrokenChains {
    LocalCell {-4, 4, 5, BlockType::IronOre},
    LocalCell {-4, 3, 5, BlockType::IronOre},
    LocalCell {-3, 2, 5, BlockType::IronOre},
    LocalCell {-3, 1, 5, BlockType::IronOre},
    LocalCell {-2, 1, 5, BlockType::IronOre},
};

constexpr std::array<LocalCell, 6U> kGiantMoulds {
    LocalCell {0, 0, 5, BlockType::Cobblestone},
    LocalCell {1, 0, 5, BlockType::Cobblestone},
    LocalCell {2, 0, 5, BlockType::Cobblestone},
    LocalCell {0, 1, 5, BlockType::Cobblestone},
    LocalCell {2, 1, 5, BlockType::Cobblestone},
    LocalCell {2, 2, 5, BlockType::Cobblestone},
};

constexpr std::array<LocalCell, 4U> kBlackMetalBlocks {
    LocalCell {5, 0, 5, BlockType::CoalOre},
    LocalCell {6, 0, 5, BlockType::CoalOre},
    LocalCell {5, 1, 5, BlockType::CoalOre},
    LocalCell {6, 0, 4, BlockType::CoalOre},
};

constexpr std::array<LocalCell, 5U> kWallClawMarks {
    LocalCell {-7, 3, -5, BlockType::Gravel},
    LocalCell {-6, 2, -5, BlockType::Gravel},
    LocalCell {-5, 1, -5, BlockType::Gravel},
    LocalCell {-4, 3, -5, BlockType::Gravel},
    LocalCell {-3, 2, -5, BlockType::Gravel},
};

constexpr std::array<LocalCell, 8U> kAncientFurnace {
    LocalCell {0, 0, -4, BlockType::Cobblestone},
    LocalCell {1, 0, -4, BlockType::CoalOre},
    LocalCell {2, 0, -4, BlockType::Cobblestone},
    LocalCell {0, 1, -4, BlockType::Cobblestone},
    LocalCell {1, 1, -4, BlockType::Torch},
    LocalCell {2, 1, -4, BlockType::Cobblestone},
    LocalCell {0, 2, -4, BlockType::Cobblestone},
    LocalCell {2, 2, -4, BlockType::Cobblestone},
};

constexpr std::array<LocalCell, 10U> kSealedRoom {
    LocalCell {5, 0, -5, BlockType::MossyStone},
    LocalCell {6, 0, -5, BlockType::IronOre},
    LocalCell {7, 0, -5, BlockType::MossyStone},
    LocalCell {5, 1, -5, BlockType::MossyStone},
    LocalCell {6, 1, -5, BlockType::IronOre},
    LocalCell {7, 1, -5, BlockType::MossyStone},
    LocalCell {5, 2, -5, BlockType::MossyStone},
    LocalCell {6, 2, -5, BlockType::MossyStone},
    LocalCell {7, 2, -5, BlockType::MossyStone},
    LocalCell {6, 3, -5, BlockType::Torch},
};

constexpr std::array<FeatureRecipe, 7U> kFeatureRecipes {
    FeatureRecipe {
        LegendaryQuestForgeFeature::GiantTools,
        kGiantTools,
        {-7, 1, 3},
        "quest.leviathan.world.giant_tools",
    },
    FeatureRecipe {
        LegendaryQuestForgeFeature::BrokenChains,
        kBrokenChains,
        {-3, 2, 3},
        "quest.leviathan.world.broken_chains",
    },
    FeatureRecipe {
        LegendaryQuestForgeFeature::GiantMoulds,
        kGiantMoulds,
        {1, 1, 3},
        "quest.leviathan.world.giant_moulds",
    },
    FeatureRecipe {
        LegendaryQuestForgeFeature::BlackMetalBlocks,
        kBlackMetalBlocks,
        {6, 1, 3},
        "quest.leviathan.world.black_metal",
    },
    FeatureRecipe {
        LegendaryQuestForgeFeature::WallClawMarks,
        kWallClawMarks,
        {-5, 2, -3},
        "quest.leviathan.world.claw_marks",
    },
    FeatureRecipe {
        LegendaryQuestForgeFeature::AncientFurnace,
        kAncientFurnace,
        {1, 1, -2},
        "quest.leviathan.world.ancient_furnace",
    },
    FeatureRecipe {
        LegendaryQuestForgeFeature::SealedRoom,
        kSealedRoom,
        {6, 1, -3},
        "quest.leviathan.world.sealed_room",
    },
};

static_assert(
    kRumorEditCount +
        (kLegendaryQuestMapFragmentCount *
         kFragmentEditCount) +
        kForgeEditCount ==
    kLegendaryQuestWorldMaximumBlockEditCount);
static_assert(
    kForgeShellEditCount +
        kGiantTools.size() +
        kBrokenChains.size() +
        kGiantMoulds.size() +
        kBlackMetalBlocks.size() +
        kWallClawMarks.size() +
        kAncientFurnace.size() +
        kSealedRoom.size() ==
    kForgeEditCount);

[[nodiscard]] constexpr auto mix64(std::uint64_t value) noexcept
    -> std::uint64_t {
    value += 0x9E3779B97F4A7C15ULL;
    value =
        (value ^ (value >> 30U)) *
        0xBF58476D1CE4E5B9ULL;
    value =
        (value ^ (value >> 27U)) *
        0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] constexpr auto non_zero(
    std::uint64_t value) noexcept -> std::uint64_t {
    return value == 0ULL ? 1ULL : value;
}

[[nodiscard]] constexpr auto projected_scene_position(
    const LegendaryQuestWorldPoint& authored,
    std::int32_t minimum_y,
    std::int32_t maximum_y) noexcept
    -> LegendaryQuestWorldPoint {
    return {
        authored.x,
        std::clamp(authored.y, minimum_y, maximum_y),
        authored.z,
    };
}

[[nodiscard]] constexpr auto offset_point(
    const LegendaryQuestWorldPoint& origin,
    const LegendaryQuestWorldPoint& offset) noexcept
    -> LegendaryQuestWorldPoint {
    return {
        origin.x + offset.x,
        origin.y + offset.y,
        origin.z + offset.z,
    };
}

[[nodiscard]] constexpr auto to_cell(
    const LegendaryQuestWorldPoint& point) noexcept
    -> ColossalWorldCell {
    return {point.x, point.y, point.z};
}

[[nodiscard]] constexpr auto block_coord(
    const LegendaryQuestWorldPoint& origin,
    const LocalCell& local) noexcept -> BlockCoord {
    return {
        origin.x + local.x,
        origin.y + local.y,
        origin.z + local.z,
    };
}

[[nodiscard]] constexpr auto is_world_coordinate_valid(
    const BlockCoord& coordinate) noexcept -> bool {
    return
        coordinate.x >=
            -kLegendaryQuestWorldHorizontalCoordinateLimit &&
        coordinate.x <=
            kLegendaryQuestWorldHorizontalCoordinateLimit &&
        coordinate.y >= kWorldMinY &&
        coordinate.y <= kWorldMaxY &&
        coordinate.z >=
            -kLegendaryQuestWorldHorizontalCoordinateLimit &&
        coordinate.z <=
            kLegendaryQuestWorldHorizontalCoordinateLimit;
}

[[nodiscard]] constexpr auto protection_id(
    LegendaryQuestAnchorId anchor_id,
    std::size_t scene_index) noexcept -> std::uint64_t {
    return non_zero(
        mix64(
            anchor_id ^
            kProtectionIdSalt ^
            (static_cast<std::uint64_t>(scene_index + 1U) *
             kProtectionIndexSalt)));
}

[[nodiscard]] constexpr auto scene_marker_block(
    LegendaryQuestForgeSite forge_site) noexcept -> BlockType {
    switch (forge_site) {
    case LegendaryQuestForgeSite::RemoteMountain:
        return BlockType::IronOre;
    case LegendaryQuestForgeSite::VolcanicIsland:
        return BlockType::CoalOre;
    case LegendaryQuestForgeSite::RuinedIsland:
        return BlockType::MossyStone;
    }
    return BlockType::IronOre;
}

[[nodiscard]] constexpr auto forge_floor_block(
    LegendaryQuestForgeSite forge_site) noexcept -> BlockType {
    switch (forge_site) {
    case LegendaryQuestForgeSite::RemoteMountain:
        return BlockType::Stone;
    case LegendaryQuestForgeSite::VolcanicIsland:
        return BlockType::Cobblestone;
    case LegendaryQuestForgeSite::RuinedIsland:
        return BlockType::MossyStone;
    }
    return BlockType::Stone;
}

[[nodiscard]] auto region_contains(
    const WorldProtectionRegion& region,
    const BlockCoord& coordinate) noexcept -> bool {
    return
        coordinate.x >= region.minimum.x &&
        coordinate.x <= region.maximum.x &&
        coordinate.y >= region.minimum.y &&
        coordinate.y <= region.maximum.y &&
        coordinate.z >= region.minimum.z &&
        coordinate.z <= region.maximum.z;
}

[[nodiscard]] auto region_is_valid(
    const WorldProtectionRegion& region) noexcept -> bool {
    return
        region.id != 0ULL &&
        region.minimum.x <= region.maximum.x &&
        region.minimum.y <= region.maximum.y &&
        region.minimum.z <= region.maximum.z &&
        region.minimum.x >=
            -kLegendaryQuestWorldHorizontalCoordinateLimit &&
        region.maximum.x <=
            kLegendaryQuestWorldHorizontalCoordinateLimit &&
        region.minimum.y >= kWorldMinY &&
        region.maximum.y <= kWorldMaxY &&
        region.minimum.z >=
            -kLegendaryQuestWorldHorizontalCoordinateLimit &&
        region.maximum.z <=
            kLegendaryQuestWorldHorizontalCoordinateLimit &&
        region.flags == WorldProtectionFlag::QuestStructure;
}

[[nodiscard]] constexpr auto feature_name(
    LegendaryQuestForgeFeature feature) noexcept
    -> std::string_view {
    for (const auto& recipe : kFeatureRecipes) {
        if (recipe.feature == feature) {
            return recipe.feature_id;
        }
    }
    return {};
}

[[nodiscard]] auto points_are_finite(
    const LegendaryQuestSpatialPoint& point) noexcept -> bool {
    return
        std::isfinite(point.x) &&
        std::isfinite(point.y) &&
        std::isfinite(point.z);
}

[[nodiscard]] auto make_invalid_transaction_result() noexcept
    -> WorldEditTransactionResult {
    WorldEditTransactionResult result {};
    result.status = WorldEditTransactionStatus::InvalidTarget;
    return result;
}

}

auto LegendaryQuestWorldProtectionVolume::operator==(
    const LegendaryQuestWorldProtectionVolume& other) const
    noexcept -> bool {
    return
        source_anchor_id == other.source_anchor_id &&
        region.id == other.region.id &&
        region.minimum == other.region.minimum &&
        region.maximum == other.region.maximum &&
        region.flags == other.region.flags;
}

auto LegendaryQuestWorldContentPlan::edits() const noexcept
    -> std::span<const LegendaryQuestBlockEdit> {
    return {
        block_edits.data(),
        std::min(block_edit_count, block_edits.size()),
    };
}

auto LegendaryQuestWorldContentPlan::scene_edits(
    std::size_t scene_index) const noexcept
    -> std::span<const LegendaryQuestBlockEdit> {
    if (scene_index >= scenes.size()) {
        return {};
    }
    const auto& scene = scenes[scene_index];
    if (scene.first_block_edit > block_edit_count ||
        scene.block_edit_count >
            block_edit_count - scene.first_block_edit) {
        return {};
    }
    return {
        block_edits.data() + scene.first_block_edit,
        scene.block_edit_count,
    };
}

auto LegendaryQuestWorldContentPlan::anchor(
    LegendaryQuestAnchorId anchor_id) const noexcept
    -> std::optional<LegendaryQuestWorldAnchorPlacement> {
    const auto found = std::find_if(
        anchors.begin(),
        anchors.end(),
        [anchor_id](
            const LegendaryQuestWorldAnchorPlacement& placement) {
            return placement.anchor_id == anchor_id;
        });
    return found == anchors.end()
        ? std::nullopt
        : std::optional {*found};
}

auto LegendaryQuestWorldContentPlan::forge_feature(
    LegendaryQuestForgeFeature feature) const noexcept
    -> std::optional<LegendaryQuestWorldForgeFeaturePlacement> {
    const auto found = std::find_if(
        forge_features.begin(),
        forge_features.end(),
        [feature](
            const LegendaryQuestWorldForgeFeaturePlacement& placement) {
            return placement.feature == feature;
        });
    return found == forge_features.end()
        ? std::nullopt
        : std::optional {*found};
}

auto LegendaryQuestWorldContentPlan::operator==(
    const LegendaryQuestWorldContentPlan& other) const noexcept
    -> bool {
    return
        world_seed == other.world_seed &&
        game_mode == other.game_mode &&
        forge_site == other.forge_site &&
        layout_signature == other.layout_signature &&
        content_signature == other.content_signature &&
        anchors == other.anchors &&
        scenes == other.scenes &&
        forge_features == other.forge_features &&
        protection_volumes == other.protection_volumes &&
        block_edits == other.block_edits &&
        block_edit_count == other.block_edit_count;
}

auto generate_legendary_quest_world_content(
    const LegendaryWeaponQuestLayout& layout) noexcept
    -> std::optional<LegendaryQuestWorldContentPlan> {
    if (!is_valid_legendary_weapon_quest_layout(layout)) {
        return std::nullopt;
    }

    LegendaryQuestWorldContentPlan result {};
    result.world_seed = layout.world_seed;
    result.game_mode = layout.game_mode;
    result.forge_site = layout.forge_site;
    result.layout_signature = layout.signature;
    result.content_signature = non_zero(
        mix64(
            layout.signature ^
            layout.unique_weapon_id ^
            kContentSignatureSalt));

    const auto rumor_position =
        projected_scene_position(
            layout.rumor.position,
            2,
            kWorldMaxY - 4);
    std::array<LegendaryQuestWorldPoint, 3U>
        fragment_positions {};
    for (std::size_t index = 0U;
         index < fragment_positions.size();
         ++index) {
        fragment_positions[index] =
            projected_scene_position(
                layout.map_clues[index].source.position,
                2,
                kWorldMaxY - 4);
    }
    const auto forge_position =
        projected_scene_position(
            layout.forge.position,
            4,
            kWorldMaxY - 6);

    result.anchors[0] = {
        layout.rumor.id,
        layout.rumor.kind,
        layout.rumor.position,
        offset_point(rumor_position, {0, 0, 2}),
        layout.rumor.discovery_radius,
        layout.rumor.discovery_radius + 1.5F,
        4.0F,
    };
    for (std::size_t index = 0U;
         index < fragment_positions.size();
         ++index) {
        const auto& source =
            layout.map_clues[index].source;
        result.anchors[index + 1U] = {
            source.id,
            source.kind,
            source.position,
            offset_point(
                fragment_positions[index],
                {0, 0, 2}),
            source.discovery_radius,
            source.discovery_radius + 1.25F,
            3.5F,
        };
    }
    result.anchors[4] = {
        layout.forge.id,
        layout.forge.kind,
        layout.forge.position,
        offset_point(forge_position, {0, 0, 7}),
        layout.forge.discovery_radius,
        layout.forge.discovery_radius,
        8.0F,
    };
    result.anchors[5] = {
        layout.guardian.id,
        layout.guardian.kind,
        layout.guardian.position,
        offset_point(forge_position, {-7, 0, 2}),
        layout.guardian.discovery_radius,
        layout.guardian.discovery_radius,
        4.0F,
    };
    result.anchors[6] = {
        layout.blade.id,
        layout.blade.kind,
        layout.blade.position,
        offset_point(forge_position, {5, 1, -3}),
        layout.blade.discovery_radius,
        layout.blade.discovery_radius + 0.75F,
        3.0F,
    };

    const auto append =
        [&result](
            const LegendaryQuestWorldPoint& origin,
            const LocalCell& local) noexcept -> bool {
            if (result.block_edit_count >=
                result.block_edits.size()) {
                return false;
            }
            const auto coordinate =
                block_coord(origin, local);
            if (!is_world_coordinate_valid(coordinate)) {
                return false;
            }
            result.block_edits[result.block_edit_count] = {
                coordinate,
                to_block_id(local.block),
            };
            ++result.block_edit_count;
            return true;
        };

    const std::array<LocalCell, kRumorEditCount>
        rumor_cells {
            LocalCell {0, -1, 0, BlockType::Cobblestone},
            LocalCell {0, 0, 0, BlockType::Planks},
            LocalCell {0, 1, 0, BlockType::Planks},
            LocalCell {0, 2, 0, BlockType::Torch},
        };
    const auto rumor_first = result.block_edit_count;
    for (const auto& cell : rumor_cells) {
        if (!append(rumor_position, cell)) {
            return std::nullopt;
        }
    }
    result.scenes[0] = {
        LegendaryQuestWorldSceneKind::Rumor,
        layout.rumor.id,
        0U,
        rumor_first,
        kRumorEditCount,
        0U,
        "quest.leviathan.world.rumor_scene",
    };

    constexpr std::array<BlockType, 3U>
        kFragmentTokens {
            BlockType::IronOre,
            BlockType::CoalOre,
            BlockType::GoldOre,
        };
    for (std::size_t index = 0U;
         index < fragment_positions.size();
         ++index) {
        const std::array<LocalCell, kFragmentEditCount>
            fragment_cells {
                LocalCell {
                    0,
                    -1,
                    0,
                    BlockType::MossyStone,
                },
                LocalCell {0, 0, 0, BlockType::Planks},
                LocalCell {
                    0,
                    1,
                    0,
                    kFragmentTokens[index],
                },
                LocalCell {1, 0, 0, BlockType::Torch},
            };
        const auto first = result.block_edit_count;
        for (const auto& cell : fragment_cells) {
            if (!append(fragment_positions[index], cell)) {
                return std::nullopt;
            }
        }
        result.scenes[index + 1U] = {
            LegendaryQuestWorldSceneKind::MapFragment,
            layout.map_clues[index].source.id,
            static_cast<std::uint8_t>(index),
            first,
            kFragmentEditCount,
            index + 1U,
            "quest.leviathan.world.fragment_scene",
        };
    }

    const auto forge_first = result.block_edit_count;
    const std::array<LocalCell, kForgeShellEditCount>
        forge_shell {
            LocalCell {
                -1,
                -1,
                7,
                forge_floor_block(layout.forge_site),
            },
            LocalCell {
                0,
                -1,
                7,
                forge_floor_block(layout.forge_site),
            },
            LocalCell {
                1,
                -1,
                7,
                forge_floor_block(layout.forge_site),
            },
            LocalCell {
                2,
                0,
                7,
                scene_marker_block(layout.forge_site),
            },
        };
    for (const auto& cell : forge_shell) {
        if (!append(forge_position, cell)) {
            return std::nullopt;
        }
    }

    for (std::size_t feature_index = 0U;
         feature_index < kFeatureRecipes.size();
         ++feature_index) {
        const auto& recipe = kFeatureRecipes[feature_index];
        const auto feature_first = result.block_edit_count;
        auto minimum = ColossalWorldCell {
            std::numeric_limits<std::int32_t>::max(),
            std::numeric_limits<std::int32_t>::max(),
            std::numeric_limits<std::int32_t>::max(),
        };
        auto maximum = ColossalWorldCell {
            std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::min(),
        };
        for (const auto& cell : recipe.cells) {
            if (!append(forge_position, cell)) {
                return std::nullopt;
            }
            const auto coordinate =
                block_coord(forge_position, cell);
            minimum.x = std::min(minimum.x, coordinate.x);
            minimum.y = std::min(minimum.y, coordinate.y);
            minimum.z = std::min(minimum.z, coordinate.z);
            maximum.x = std::max(maximum.x, coordinate.x);
            maximum.y = std::max(maximum.y, coordinate.y);
            maximum.z = std::max(maximum.z, coordinate.z);
        }
        result.forge_features[feature_index] = {
            recipe.feature,
            offset_point(
                forge_position,
                recipe.interaction_offset),
            minimum,
            maximum,
            feature_first,
            recipe.cells.size(),
            recipe.feature_id,
        };
    }

    result.scenes[4] = {
        LegendaryQuestWorldSceneKind::Forge,
        layout.forge.id,
        0U,
        forge_first,
        kForgeEditCount,
        4U,
        "quest.leviathan.world.forge_scene",
    };

    const auto make_protection =
        [&result](
            std::size_t index,
            LegendaryQuestAnchorId anchor_id,
            ColossalWorldCell minimum,
            ColossalWorldCell maximum) noexcept {
            result.protection_volumes[index] = {
                anchor_id,
                {
                    protection_id(anchor_id, index),
                    minimum,
                    maximum,
                    WorldProtectionFlag::QuestStructure,
                },
            };
        };
    make_protection(
        0U,
        layout.rumor.id,
        {
            rumor_position.x - 2,
            rumor_position.y - 2,
            rumor_position.z - 2,
        },
        {
            rumor_position.x + 2,
            rumor_position.y + 4,
            rumor_position.z + 3,
        });
    for (std::size_t index = 0U;
         index < fragment_positions.size();
         ++index) {
        const auto& point = fragment_positions[index];
        make_protection(
            index + 1U,
            layout.map_clues[index].source.id,
            {point.x - 2, point.y - 2, point.z - 2},
            {point.x + 2, point.y + 3, point.z + 3});
    }
    make_protection(
        4U,
        layout.forge.id,
        {
            forge_position.x - 10,
            forge_position.y - 2,
            forge_position.z - 7,
        },
        {
            forge_position.x + 10,
            forge_position.y + 6,
            forge_position.z + 9,
        });

    if (result.block_edit_count !=
        kLegendaryQuestWorldMaximumBlockEditCount) {
        return std::nullopt;
    }
    return result;
}

auto generate_legendary_quest_world_content(
    std::uint64_t world_seed,
    GameMode game_mode) noexcept
    -> std::optional<LegendaryQuestWorldContentPlan> {
    const auto layout =
        generate_legendary_weapon_quest_layout(
            world_seed,
            game_mode);
    if (!layout.has_value()) {
        return std::nullopt;
    }
    return generate_legendary_quest_world_content(*layout);
}

auto is_valid_legendary_quest_world_content(
    const LegendaryQuestWorldContentPlan& plan) noexcept -> bool {
    if (!is_known_game_mode(plan.game_mode) ||
        plan.block_edit_count == 0U ||
        plan.block_edit_count > plan.block_edits.size()) {
        return false;
    }

    const auto expected =
        generate_legendary_quest_world_content(
            plan.world_seed,
            plan.game_mode);
    if (!expected.has_value() || *expected != plan) {
        return false;
    }

    std::size_t covered_edits = 0U;
    for (std::size_t scene_index = 0U;
         scene_index < plan.scenes.size();
         ++scene_index) {
        const auto& scene = plan.scenes[scene_index];
        if (scene.first_block_edit != covered_edits ||
            scene.block_edit_count == 0U ||
            scene.block_edit_count >
                kWorldEditMaximumCellCount ||
            scene.protection_volume_index >=
                plan.protection_volumes.size()) {
            return false;
        }
        const auto& volume =
            plan.protection_volumes[
                scene.protection_volume_index];
        if (!region_is_valid(volume.region) ||
            volume.source_anchor_id != scene.anchor_id) {
            return false;
        }
        for (const auto& edit :
             plan.scene_edits(scene_index)) {
            if (!is_world_coordinate_valid(
                    edit.coordinate) ||
                !is_known_block_id(edit.block_id) ||
                !region_contains(
                    volume.region,
                    edit.coordinate)) {
                return false;
            }
        }
        covered_edits += scene.block_edit_count;
    }
    if (covered_edits != plan.block_edit_count) {
        return false;
    }

    for (std::size_t index = 0U;
         index < plan.block_edit_count;
         ++index) {
        for (std::size_t other = index + 1U;
             other < plan.block_edit_count;
             ++other) {
            if (plan.block_edits[index].coordinate ==
                plan.block_edits[other].coordinate) {
                return false;
            }
        }
    }

    for (std::size_t index = 0U;
         index < plan.protection_volumes.size();
         ++index) {
        for (std::size_t other = index + 1U;
             other < plan.protection_volumes.size();
             ++other) {
            if (plan.protection_volumes[index].region.id ==
                plan.protection_volumes[other].region.id) {
                return false;
            }
        }
    }

    const auto& forge_scene = plan.scenes.back();
    std::size_t previous_end =
        forge_scene.first_block_edit +
        kForgeShellEditCount;
    for (std::size_t index = 0U;
         index < plan.forge_features.size();
         ++index) {
        const auto& feature = plan.forge_features[index];
        if (feature.feature !=
                kLegendaryQuestForgeFeatures[index] ||
            feature.feature_id !=
                feature_name(feature.feature) ||
            feature.first_block_edit != previous_end ||
            feature.block_edit_count == 0U ||
            feature.minimum.x > feature.maximum.x ||
            feature.minimum.y > feature.maximum.y ||
            feature.minimum.z > feature.maximum.z) {
            return false;
        }
        previous_end += feature.block_edit_count;
    }
    return
        previous_end ==
        forge_scene.first_block_edit +
            forge_scene.block_edit_count;
}

auto execute_legendary_quest_world_content(
    const LegendaryQuestWorldContentPlan& plan,
    const WorldEditTransactionCallbacks& callbacks)
    -> WorldEditTransactionResult {
    if (!is_valid_legendary_quest_world_content(plan)) {
        return make_invalid_transaction_result();
    }
    return WorldEditTransaction::execute(
        plan.edits(),
        callbacks);
}

auto execute_legendary_quest_world_scene(
    const LegendaryQuestWorldContentPlan& plan,
    std::size_t scene_index,
    const WorldEditTransactionCallbacks& callbacks)
    -> WorldEditTransactionResult {
    if (!is_valid_legendary_quest_world_content(plan) ||
        scene_index >= plan.scenes.size()) {
        return make_invalid_transaction_result();
    }
    return WorldEditTransaction::execute(
        plan.scene_edits(scene_index),
        callbacks);
}

auto evaluate_legendary_quest_proximity(
    const LegendaryQuestProximityQuery& query) noexcept
    -> LegendaryQuestProximityResult {
    LegendaryQuestProximityResult result {};
    const auto known_metric =
        query.metric ==
            LegendaryQuestProximityMetric::ThreeDimensional ||
        query.metric ==
            LegendaryQuestProximityMetric::Horizontal;
    if (!known_metric ||
        !points_are_finite(query.observer) ||
        !points_are_finite(query.target) ||
        !std::isfinite(query.radius) ||
        query.radius < 0.0F ||
        (query.maximum_vertical_distance.has_value() &&
         (!std::isfinite(
              *query.maximum_vertical_distance) ||
          *query.maximum_vertical_distance < 0.0F))) {
        return result;
    }

    const auto dx =
        static_cast<double>(query.observer.x) -
        static_cast<double>(query.target.x);
    const auto dy =
        static_cast<double>(query.observer.y) -
        static_cast<double>(query.target.y);
    const auto dz =
        static_cast<double>(query.observer.z) -
        static_cast<double>(query.target.z);
    const auto radius =
        static_cast<double>(query.radius);
    result.horizontal_distance_squared =
        dx * dx + dz * dz;
    result.vertical_distance = std::abs(dy);
    result.distance_squared =
        query.metric ==
            LegendaryQuestProximityMetric::ThreeDimensional
        ? result.horizontal_distance_squared + dy * dy
        : result.horizontal_distance_squared;
    result.valid =
        std::isfinite(result.distance_squared) &&
        std::isfinite(result.horizontal_distance_squared) &&
        std::isfinite(result.vertical_distance);
    if (!result.valid) {
        return result;
    }

    const auto vertical_gate_satisfied =
        query.metric !=
            LegendaryQuestProximityMetric::Horizontal ||
        !query.maximum_vertical_distance.has_value() ||
        result.vertical_distance <=
            static_cast<double>(
                *query.maximum_vertical_distance);
    result.within =
        result.distance_squared <= radius * radius &&
        vertical_gate_satisfied;
    return result;
}

auto is_legendary_quest_near_3d(
    const LegendaryQuestSpatialPoint& observer,
    const LegendaryQuestSpatialPoint& target,
    float radius) noexcept -> bool {
    return evaluate_legendary_quest_proximity(
               {
                   observer,
                   target,
                   radius,
                   LegendaryQuestProximityMetric::
                       ThreeDimensional,
                   std::nullopt,
               })
        .within;
}

auto is_legendary_quest_near_horizontal(
    const LegendaryQuestSpatialPoint& observer,
    const LegendaryQuestSpatialPoint& target,
    float radius,
    std::optional<float> maximum_vertical_distance) noexcept
    -> bool {
    return evaluate_legendary_quest_proximity(
               {
                   observer,
                   target,
                   radius,
                   LegendaryQuestProximityMetric::Horizontal,
                   maximum_vertical_distance,
               })
        .within;
}

}
