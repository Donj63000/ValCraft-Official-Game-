#pragma once

#include "creatures/CreatureTypes.h"
#include "world/World.h"

#include <glm/vec3.hpp>

#include <optional>
#include <span>
#include <vector>

namespace valcraft {

inline constexpr int kCreatureActivationRadiusChunks = 3;
inline constexpr int kCreatureKeepAliveRadiusChunks = 4;
inline constexpr std::size_t kCreatureMaxActiveCount = 18;

class CreatureSystem {
public:
    void update(float dt,
                const World& world,
                const glm::vec3& player_position,
                const EnvironmentState& environment,
                const CreatureCycleState& cycle);

    [[nodiscard]] auto spawn_anchor_for_chunk(const World& world, const ChunkCoord& coord) const
        -> std::optional<CreatureSpawnAnchor>;
    [[nodiscard]] auto active_creatures() const noexcept -> std::span<const CreatureInstance>;
    [[nodiscard]] auto render_instances() const noexcept -> std::span<const CreatureRenderInstance>;

private:
    void sync_active_creatures(const World& world, const glm::vec3& player_position, const CreatureCycleState& cycle);
    void update_creature(CreatureInstance& creature,
                         float dt,
                         const World& world,
                         const glm::vec3& player_position,
                         const EnvironmentState& environment,
                         const CreatureCycleState& cycle) const;
    void rebuild_render_instances(const EnvironmentState& environment);

    [[nodiscard]] auto compute_spawn_anchor(const World& world, const ChunkCoord& coord) const
        -> std::optional<CreatureSpawnAnchor>;
    [[nodiscard]] auto find_creature(const ChunkCoord& coord) -> CreatureInstance*;
    [[nodiscard]] auto find_creature(const ChunkCoord& coord) const -> const CreatureInstance*;

    std::vector<CreatureInstance> creatures_ {};
    std::vector<CreatureRenderInstance> render_instances_ {};
};

} // namespace valcraft
