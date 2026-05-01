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

struct CreatureAuditStats {
    std::size_t spawned = 0;
    std::size_t despawned = 0;
    std::size_t attacks = 0;
    std::size_t active_creatures = 0;
};

struct CreatureDamageResult {
    bool hit = false;
    bool killed = false;
    CreatureSpecies species = CreatureSpecies::Pig;
    glm::vec3 position {0.0F};
    float damage = 0.0F;
    float remaining_health = 0.0F;
    float distance = 0.0F;
};

struct ResidentProfile {
    CreatureSpawnAnchor anchor {};
    CreatureResidentRole role = CreatureResidentRole::Artisan;
    glm::vec3 home_position {0.0F};
    glm::vec3 interior_position {0.0F};
    glm::vec3 work_position {0.0F};
    glm::vec3 social_position {0.0F};
    glm::vec3 garden_position {0.0F};
    float walk_radius = 3.5F;
    float home_radius = 1.8F;
    float roam_radius = 10.0F;
    std::uint32_t routine_seed = 0;
};

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
    [[nodiscard]] auto recent_attacks() const noexcept -> std::span<const CreatureAttackEvent>;
    [[nodiscard]] auto consume_audit_stats() noexcept -> CreatureAuditStats;
    auto try_damage_from_player(const glm::vec3& origin, const glm::vec3& direction, float max_distance, float damage)
        -> CreatureDamageResult;
    void set_settlement_residents(std::vector<CreatureSpawnAnchor> residents);
    void load_creatures(const std::vector<CreatureInstance>& creatures, const EnvironmentState& environment);
    void clear() noexcept;

private:
    void sync_active_creatures(const World& world, const glm::vec3& player_position, const CreatureCycleState& cycle);
    void update_creature(CreatureInstance& creature,
                         float dt,
                         const World& world,
                         const glm::vec3& player_position,
                         const EnvironmentState& environment,
                         const CreatureCycleState& cycle);
    void update_death_visuals(float dt) noexcept;
    void update_spawn_suppressions(float dt) noexcept;
    void rebuild_render_instances(const EnvironmentState& environment);

    [[nodiscard]] auto compute_spawn_anchor(const World& world, const ChunkCoord& coord) const
        -> std::optional<CreatureSpawnAnchor>;
    [[nodiscard]] auto find_creature(const ChunkCoord& coord) -> CreatureInstance*;
    [[nodiscard]] auto find_creature(const ChunkCoord& coord) const -> const CreatureInstance*;
    [[nodiscard]] auto find_resident(const CreatureSpawnAnchor& anchor) const -> const CreatureSpawnAnchor*;
    [[nodiscard]] auto find_resident_creature(const CreatureSpawnAnchor& anchor) -> CreatureInstance*;
    [[nodiscard]] auto find_resident_creature(const CreatureSpawnAnchor& anchor) const -> const CreatureInstance*;
    [[nodiscard]] auto find_resident_profile(const CreatureSpawnAnchor& anchor) -> ResidentProfile*;
    [[nodiscard]] auto find_resident_profile(const CreatureSpawnAnchor& anchor) const -> const ResidentProfile*;
    [[nodiscard]] auto is_session_dead_resident(const CreatureSpawnAnchor& anchor) const -> bool;
    [[nodiscard]] auto is_spawn_suppressed(const CreatureSpawnAnchor& anchor) const -> bool;
    void remember_session_dead_resident(const CreatureSpawnAnchor& anchor);
    void suppress_spawn_after_death(const CreatureSpawnAnchor& anchor);
    void spawn_death_visual(const CreatureInstance& creature, const glm::vec3& hit_direction);

    struct CreatureDeathVisual {
        CreatureSpawnAnchor anchor {};
        glm::vec3 position {0.0F};
        float yaw_radians = 0.0F;
        float animation_time = 0.0F;
        float morph_factor = 0.0F;
        float daylight_factor = 1.0F;
        float tension = 0.0F;
        std::uint32_t appearance_seed = 0;
        CreatureBehaviorState behavior_state = CreatureBehaviorState::Idle;
        CreaturePhase phase = CreaturePhase::Day;
        float motion_amount = 0.0F;
        float gaze_weight = 0.0F;
        float attack_amount = 0.0F;
        float age_seconds = 0.0F;
        float duration_seconds = 1.15F;
        glm::vec3 hit_direction {0.0F, 0.0F, 1.0F};
    };

    struct SpawnSuppression {
        CreatureSpawnAnchor anchor {};
        float remaining_seconds = 0.0F;
    };

    std::vector<CreatureInstance> creatures_ {};
    std::vector<CreatureRenderInstance> render_instances_ {};
    std::vector<CreatureAttackEvent> attacks_ {};
    std::vector<CreatureDeathVisual> death_visuals_ {};
    std::vector<SpawnSuppression> spawn_suppressions_ {};
    std::vector<CreatureSpawnAnchor> settlement_residents_ {};
    std::vector<ResidentProfile> resident_profiles_ {};
    std::vector<CreatureSpawnAnchor> session_dead_residents_ {};
    glm::vec3 settlement_center_ {0.0F};
    CreatureAuditStats audit_stats_ {};
};

} // namespace valcraft
