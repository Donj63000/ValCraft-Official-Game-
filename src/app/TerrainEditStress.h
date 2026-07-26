#pragma once

#include "app/GameOptions.h"
#include "world/World.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace valcraft {

inline constexpr std::size_t kTerrainEditStressIntervalFrames = 8U;

enum class TerrainEditStressAction : std::uint8_t {
    Break = 0,
    Place = 1,
};

struct TerrainEditStressOperation {
    TerrainEditStressAction action = TerrainEditStressAction::Break;
    BlockCoord block {};
    BlockId previous_block = to_block_id(BlockType::Air);
    BlockId next_block = to_block_id(BlockType::Air);
    std::size_t pair_index = 0U;

    auto operator==(const TerrainEditStressOperation&) const -> bool = default;
};

[[nodiscard]] auto terrain_edit_stress_enabled(
    const GameOptions& options) noexcept -> bool;

class TerrainEditStressScenario {
public:
    // Je n'applique au plus qu'une edition par frame. Chaque premiere operation
    // est suivie de son inverse afin que le monde logique revienne a son etat
    // genere et ne laisse aucun override persistant dans le smoke de mesure.
    [[nodiscard]] auto update(
        World& world,
        const glm::vec3& focus,
        std::size_t frame_index,
        bool allow_new_pair = true)
        -> std::optional<TerrainEditStressOperation>;

    void reset() noexcept;

    [[nodiscard]] auto has_pending_restore() const noexcept -> bool;
    [[nodiscard]] auto completed_pair_count() const noexcept -> std::size_t;

private:
    struct PendingRestore {
        BlockCoord block {};
        BlockId original_block = to_block_id(BlockType::Air);
        TerrainEditStressAction action = TerrainEditStressAction::Place;
        std::size_t pair_index = 0U;
    };

    struct Target {
        BlockCoord block {};
        BlockId original_block = to_block_id(BlockType::Air);
        BlockId edited_block = to_block_id(BlockType::Air);
        TerrainEditStressAction edit_action =
            TerrainEditStressAction::Break;
        TerrainEditStressAction restore_action =
            TerrainEditStressAction::Place;
    };

    [[nodiscard]] auto select_target(
        World& world,
        const glm::vec3& focus) const -> std::optional<Target>;

    std::optional<PendingRestore> pending_restore_ {};
    std::optional<std::size_t> last_update_frame_ {};
    std::size_t completed_pair_count_ = 0U;
};

} // namespace valcraft
