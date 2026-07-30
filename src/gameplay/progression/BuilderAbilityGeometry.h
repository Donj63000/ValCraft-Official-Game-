#pragma once

#include "gameplay/progression/WorldEditTransaction.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace valcraft {

inline constexpr std::size_t
    kBuilderAbilityMaximumCellCount = 64U;

enum class BuilderAbilityCellRole : std::uint8_t {
    Permanent = 0,
    OptionalGuardRail = 1,
    Excavation = 2,
};

struct BuilderAbilityCell {
    BlockCoord coordinate {};
    BlockId block_id =
        to_block_id(BlockType::Air);
    BuilderAbilityCellRole role =
        BuilderAbilityCellRole::Permanent;

    auto operator==(const BuilderAbilityCell&) const -> bool = default;
};

struct BuilderAbilityCellSet {
    std::array<
        BuilderAbilityCell,
        kBuilderAbilityMaximumCellCount>
        cells {};
    std::size_t cell_count = 0U;
    bool valid = false;

    [[nodiscard]] auto cell_span() const noexcept
        -> std::span<const BuilderAbilityCell> {
        return {
            cells.data(),
            cell_count,
        };
    }
};

struct DeployableWallRequest {
    BlockCoord bottom_center {};
    BlockId material =
        to_block_id(BlockType::Planks);
    std::uint8_t rank = 1U;
    WorldEditAxis width_axis =
        WorldEditAxis::X;
    bool central_opening = false;
};

struct ModularBridgeRequest {
    BlockCoord origin {};
    BlockId material =
        to_block_id(BlockType::Planks);
    std::uint8_t rank = 1U;
    WorldEditAxis forward_axis =
        WorldEditAxis::X;
    WorldEditDirection direction =
        WorldEditDirection::Positive;
    WorldEditDirection width_side =
        WorldEditDirection::Positive;
    std::int8_t grade = 0;
    bool double_width = false;
    bool include_optional_guard_rails = false;
};

struct ExcavationWaveRequest {
    BlockCoord target {};
    std::uint8_t rank = 1U;
    WorldEditFace hit_face =
        WorldEditFace::PositiveY;
    WorldEditAxis line_axis =
        WorldEditAxis::X;
};

struct ExpressRepairParameters {
    float durability_ratio = 0.0F;
    std::uint8_t maximum_plan_cells = 0U;
    float shield_ratio = 0.0F;
    float shield_duration_seconds = 0.0F;
    std::uint8_t mastery_chain_targets = 0U;
    float mastery_chain_radius = 0.0F;
    float mastery_chain_ratio = 0.0F;
};

[[nodiscard]] auto generate_deployable_wall(
    const DeployableWallRequest& request) noexcept
    -> BuilderAbilityCellSet;

[[nodiscard]] auto generate_modular_bridge(
    const ModularBridgeRequest& request) noexcept
    -> BuilderAbilityCellSet;

[[nodiscard]] auto generate_excavation_wave(
    const ExcavationWaveRequest& request) noexcept
    -> BuilderAbilityCellSet;

[[nodiscard]] auto express_repair_parameters(
    std::uint8_t rank,
    bool mastery_active) noexcept
    -> ExpressRepairParameters;

[[nodiscard]] constexpr auto
    deployable_wall_minimum_placement_count(
        std::size_t requested_cell_count) noexcept
    -> std::size_t {
    // Je calcule le plafond de 60 % sans flottant pour garder la validation
    // identique sur toutes les plateformes.
    return (requested_cell_count * 3U +
            4U) /
           5U;
}

} // namespace valcraft
