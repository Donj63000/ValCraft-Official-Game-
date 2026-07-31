#pragma once

#include "gameplay/weapons/ColossalWeaponCombat.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace valcraft {

inline constexpr std::size_t kMaximumWorldProtectionRegions = 64U;
inline constexpr std::size_t kMaximumColossalCellCandidates = 128U;
inline constexpr std::size_t kMaximumColossalFragileCellEdits = 12U;

struct ColossalWorldCell {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;

    auto operator==(const ColossalWorldCell&) const -> bool = default;
};

enum class WorldProtectionFlag : std::uint32_t {
    None = 0U,
    ImportantStructure = 1U << 0U,
    QuestStructure = 1U << 1U,
    ArenaBoundary = 1U << 2U,
    Ship = 1U << 3U,
    PlayerConstruction = 1U << 4U,
};

[[nodiscard]] constexpr auto operator|(
    WorldProtectionFlag lhs,
    WorldProtectionFlag rhs) noexcept -> WorldProtectionFlag {
    return static_cast<WorldProtectionFlag>(
        static_cast<std::uint32_t>(lhs) |
        static_cast<std::uint32_t>(rhs));
}

constexpr auto operator|=(
    WorldProtectionFlag& lhs,
    WorldProtectionFlag rhs) noexcept
    -> WorldProtectionFlag& {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr auto world_protection_contains(
    WorldProtectionFlag flags,
    WorldProtectionFlag expected) noexcept -> bool {
    const auto bits =
        static_cast<std::uint32_t>(expected);
    return (
               static_cast<std::uint32_t>(flags) &
               bits) == bits;
}

struct WorldProtectionRegion {
    std::uint64_t id = 0U;
    ColossalWorldCell minimum {};
    ColossalWorldCell maximum {};
    WorldProtectionFlag flags =
        WorldProtectionFlag::None;
};

enum class WorldProtectionRegistrationError : std::uint8_t {
    None = 0,
    InvalidId,
    DuplicateId,
    InvalidBounds,
    MissingFlags,
    CapacityExceeded,
};

struct WorldProtectionRegistrationResult {
    bool registered = false;
    WorldProtectionRegistrationError error =
        WorldProtectionRegistrationError::None;
    std::size_t region_count = 0U;
};

class WorldProtectionRegistry {
public:
    [[nodiscard]] auto register_region(
        const WorldProtectionRegion& region) noexcept
        -> WorldProtectionRegistrationResult;
    [[nodiscard]] auto unregister_region(
        std::uint64_t region_id) noexcept -> bool;
    [[nodiscard]] auto protection_at(
        const ColossalWorldCell& cell) const noexcept
        -> WorldProtectionFlag;
    [[nodiscard]] auto region(
        std::uint64_t region_id) const noexcept
        -> std::optional<WorldProtectionRegion>;
    [[nodiscard]] auto region_count() const noexcept
        -> std::size_t;
    void clear() noexcept;

private:
    std::array<
        WorldProtectionRegion,
        kMaximumWorldProtectionRegions>
        regions_ {};
    std::size_t region_count_ = 0U;
};

enum class ColossalCellMaterial : std::uint8_t {
    Unknown = 0,
    FragileFlower,
    FragileGrass,
    FragileLeaves,
    FragileGlass,
    LightDecoration,
    Soil,
    Wood,
    Stone,
    Ore,
    Metal,
    Liquid,
};

enum class ColossalCellRejection : std::uint8_t {
    None = 0,
    InvalidQuery,
    CandidateCapacityExceeded,
    CellNotLoaded,
    NotFragile,
    PlayerPlaced,
    ShipSurface,
    ImportantStructure,
    QuestStructure,
    ArenaBoundary,
    DuplicateCell,
    EditLimitReached,
};

struct ColossalFragileCellCandidate {
    ColossalWorldCell cell {};
    ColossalCellMaterial material =
        ColossalCellMaterial::Unknown;
    float impact_distance_squared = 0.0F;
    std::uint16_t block_token = 0U;
    bool loaded = false;
    bool player_placed = false;
    bool ship_surface = false;
    bool important_structure = false;
    bool quest_structure = false;
};

struct ColossalFragileCellEdit {
    ColossalWorldCell cell {};
    ColossalCellMaterial material =
        ColossalCellMaterial::Unknown;
    std::uint16_t expected_block_token = 0U;
};

struct ColossalFragileImpactQuery {
    std::uint64_t attack_sequence = 0U;
    std::uint8_t maximum_cells = 12U;
    bool charged_execution = false;
};

struct ColossalFragileImpactPlan {
    ColossalCellRejection error =
        ColossalCellRejection::None;
    std::array<
        ColossalFragileCellEdit,
        kMaximumColossalFragileCellEdits>
        edits {};
    std::size_t edit_count = 0U;
    std::size_t unloaded_count = 0U;
    std::size_t non_fragile_count = 0U;
    std::size_t protected_count = 0U;
    std::size_t duplicate_count = 0U;
    bool edit_limit_reached = false;

    [[nodiscard]] auto accepted_edits() const noexcept
        -> std::span<const ColossalFragileCellEdit> {
        return {
            edits.data(),
            std::min(edit_count, edits.size()),
        };
    }
};

[[nodiscard]] constexpr auto colossal_cell_is_fragile(
    ColossalCellMaterial material) noexcept -> bool {
    switch (material) {
    case ColossalCellMaterial::FragileFlower:
    case ColossalCellMaterial::FragileGrass:
    case ColossalCellMaterial::FragileLeaves:
    case ColossalCellMaterial::FragileGlass:
    case ColossalCellMaterial::LightDecoration:
        return true;
    case ColossalCellMaterial::Unknown:
    case ColossalCellMaterial::Soil:
    case ColossalCellMaterial::Wood:
    case ColossalCellMaterial::Stone:
    case ColossalCellMaterial::Ore:
    case ColossalCellMaterial::Metal:
    case ColossalCellMaterial::Liquid:
        return false;
    }
    return false;
}

[[nodiscard]] auto build_colossal_fragile_impact_plan(
    const ColossalFragileImpactQuery& query,
    std::span<const ColossalFragileCellCandidate> candidates,
    const WorldProtectionRegistry& protections) noexcept
    -> ColossalFragileImpactPlan;

[[nodiscard]] constexpr auto colossal_cell_impact_material(
    ColossalCellMaterial material) noexcept
    -> ColossalImpactMaterial {
    switch (material) {
    case ColossalCellMaterial::FragileFlower:
    case ColossalCellMaterial::FragileGrass:
    case ColossalCellMaterial::FragileLeaves:
        return ColossalImpactMaterial::Organic;
    case ColossalCellMaterial::FragileGlass:
        return ColossalImpactMaterial::Glass;
    case ColossalCellMaterial::LightDecoration:
    case ColossalCellMaterial::Wood:
        return ColossalImpactMaterial::Wood;
    case ColossalCellMaterial::Soil:
        return ColossalImpactMaterial::Earth;
    case ColossalCellMaterial::Stone:
    case ColossalCellMaterial::Ore:
        return ColossalImpactMaterial::Stone;
    case ColossalCellMaterial::Metal:
        return ColossalImpactMaterial::Metal;
    case ColossalCellMaterial::Unknown:
    case ColossalCellMaterial::Liquid:
        return ColossalImpactMaterial::Unknown;
    }
    return ColossalImpactMaterial::Unknown;
}

} // namespace valcraft
