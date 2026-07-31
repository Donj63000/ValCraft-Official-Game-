#pragma once

#include "gameplay/combat/WorldProtectionRegistry.h"
#include "gameplay/progression/WorldEditTransaction.h"
#include "gameplay/quests/LegendaryWeaponQuest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace valcraft {

inline constexpr std::size_t
    kLegendaryQuestWorldAnchorPlacementCount = 7U;
inline constexpr std::size_t
    kLegendaryQuestWorldSceneCount = 5U;
inline constexpr std::size_t
    kLegendaryQuestWorldProtectionVolumeCount =
        kLegendaryQuestWorldSceneCount;
inline constexpr std::size_t
    kLegendaryQuestWorldForgeFeatureCount =
        kLegendaryQuestForgeFeatures.size();
inline constexpr std::size_t
    kLegendaryQuestWorldMaximumBlockEditCount =
        kWorldEditMaximumCellCount;
inline constexpr std::int32_t
    kLegendaryQuestWorldHorizontalCoordinateLimit = 8'192;

using LegendaryQuestBlockEdit = WorldEditCell;

enum class LegendaryQuestWorldSceneKind : std::uint8_t {
    Rumor = 0,
    MapFragment = 1,
    Forge = 2,
};

struct LegendaryQuestWorldAnchorPlacement {
    LegendaryQuestAnchorId anchor_id = 0ULL;
    LegendaryQuestAnchorKind kind =
        LegendaryQuestAnchorKind::Rumor;
    LegendaryQuestWorldPoint authored_position {};
    LegendaryQuestWorldPoint interaction_position {};
    float interaction_radius = 0.0F;
    float horizontal_interaction_radius = 0.0F;
    float vertical_tolerance = 0.0F;

    auto operator==(
        const LegendaryQuestWorldAnchorPlacement&) const
        -> bool = default;
};

struct LegendaryQuestWorldScenePlacement {
    LegendaryQuestWorldSceneKind kind =
        LegendaryQuestWorldSceneKind::Rumor;
    LegendaryQuestAnchorId anchor_id = 0ULL;
    std::uint8_t fragment_index = 0U;
    std::size_t first_block_edit = 0U;
    std::size_t block_edit_count = 0U;
    std::size_t protection_volume_index = 0U;
    std::string_view scene_id {};

    auto operator==(
        const LegendaryQuestWorldScenePlacement&) const
        -> bool = default;
};

struct LegendaryQuestWorldForgeFeaturePlacement {
    LegendaryQuestForgeFeature feature =
        LegendaryQuestForgeFeature::GiantTools;
    LegendaryQuestWorldPoint interaction_position {};
    ColossalWorldCell minimum {};
    ColossalWorldCell maximum {};
    std::size_t first_block_edit = 0U;
    std::size_t block_edit_count = 0U;
    std::string_view feature_id {};

    auto operator==(
        const LegendaryQuestWorldForgeFeaturePlacement&) const
        -> bool = default;
};

struct LegendaryQuestWorldProtectionVolume {
    LegendaryQuestAnchorId source_anchor_id = 0ULL;
    WorldProtectionRegion region {};

    [[nodiscard]] auto operator==(
        const LegendaryQuestWorldProtectionVolume& other) const
        noexcept -> bool;
};

struct LegendaryQuestWorldContentPlan {
    std::uint64_t world_seed = 0ULL;
    GameMode game_mode = GameMode::ClassicAdventure;
    LegendaryQuestForgeSite forge_site =
        LegendaryQuestForgeSite::RemoteMountain;
    std::uint64_t layout_signature = 0ULL;
    std::uint64_t content_signature = 0ULL;
    std::array<
        LegendaryQuestWorldAnchorPlacement,
        kLegendaryQuestWorldAnchorPlacementCount>
        anchors {};
    std::array<
        LegendaryQuestWorldScenePlacement,
        kLegendaryQuestWorldSceneCount>
        scenes {};
    std::array<
        LegendaryQuestWorldForgeFeaturePlacement,
        kLegendaryQuestWorldForgeFeatureCount>
        forge_features {};
    std::array<
        LegendaryQuestWorldProtectionVolume,
        kLegendaryQuestWorldProtectionVolumeCount>
        protection_volumes {};
    std::array<
        LegendaryQuestBlockEdit,
        kLegendaryQuestWorldMaximumBlockEditCount>
        block_edits {};
    std::size_t block_edit_count = 0U;

    [[nodiscard]] auto edits() const noexcept
        -> std::span<const LegendaryQuestBlockEdit>;
    [[nodiscard]] auto scene_edits(std::size_t scene_index) const
        noexcept -> std::span<const LegendaryQuestBlockEdit>;
    [[nodiscard]] auto anchor(
        LegendaryQuestAnchorId anchor_id) const noexcept
        -> std::optional<LegendaryQuestWorldAnchorPlacement>;
    [[nodiscard]] auto forge_feature(
        LegendaryQuestForgeFeature feature) const noexcept
        -> std::optional<LegendaryQuestWorldForgeFeaturePlacement>;

    [[nodiscard]] auto operator==(
        const LegendaryQuestWorldContentPlan& other) const
        noexcept -> bool;
};

[[nodiscard]] auto generate_legendary_quest_world_content(
    const LegendaryWeaponQuestLayout& layout) noexcept
    -> std::optional<LegendaryQuestWorldContentPlan>;
[[nodiscard]] auto generate_legendary_quest_world_content(
    std::uint64_t world_seed,
    GameMode game_mode) noexcept
    -> std::optional<LegendaryQuestWorldContentPlan>;
[[nodiscard]] auto is_valid_legendary_quest_world_content(
    const LegendaryQuestWorldContentPlan& plan) noexcept -> bool;

// Je laisse la mutation au moteur transactionnel existant : ce module ne
// connait jamais World et ne peut donc pas contourner validation ou rollback.
[[nodiscard]] auto execute_legendary_quest_world_content(
    const LegendaryQuestWorldContentPlan& plan,
    const WorldEditTransactionCallbacks& callbacks)
    -> WorldEditTransactionResult;
[[nodiscard]] auto execute_legendary_quest_world_scene(
    const LegendaryQuestWorldContentPlan& plan,
    std::size_t scene_index,
    const WorldEditTransactionCallbacks& callbacks)
    -> WorldEditTransactionResult;

struct LegendaryQuestSpatialPoint {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    auto operator==(const LegendaryQuestSpatialPoint&) const
        -> bool = default;
};

enum class LegendaryQuestProximityMetric : std::uint8_t {
    ThreeDimensional = 0,
    Horizontal = 1,
};

struct LegendaryQuestProximityQuery {
    LegendaryQuestSpatialPoint observer {};
    LegendaryQuestSpatialPoint target {};
    float radius = 0.0F;
    LegendaryQuestProximityMetric metric =
        LegendaryQuestProximityMetric::ThreeDimensional;
    // Je n'applique cette garde verticale qu'au calcul horizontal.
    std::optional<float> maximum_vertical_distance {};
};

struct LegendaryQuestProximityResult {
    bool valid = false;
    bool within = false;
    double distance_squared = 0.0;
    double horizontal_distance_squared = 0.0;
    double vertical_distance = 0.0;

    auto operator==(const LegendaryQuestProximityResult&) const
        -> bool = default;
};

[[nodiscard]] auto evaluate_legendary_quest_proximity(
    const LegendaryQuestProximityQuery& query) noexcept
    -> LegendaryQuestProximityResult;
[[nodiscard]] auto is_legendary_quest_near_3d(
    const LegendaryQuestSpatialPoint& observer,
    const LegendaryQuestSpatialPoint& target,
    float radius) noexcept -> bool;
[[nodiscard]] auto is_legendary_quest_near_horizontal(
    const LegendaryQuestSpatialPoint& observer,
    const LegendaryQuestSpatialPoint& target,
    float radius,
    std::optional<float> maximum_vertical_distance =
        std::nullopt) noexcept -> bool;
[[nodiscard]] constexpr auto legendary_quest_spatial_point(
    const LegendaryQuestWorldPoint& point) noexcept
    -> LegendaryQuestSpatialPoint {
    return {
        static_cast<float>(point.x),
        static_cast<float>(point.y),
        static_cast<float>(point.z),
    };
}

}
