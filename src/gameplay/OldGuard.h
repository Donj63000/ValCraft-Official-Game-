#pragma once

#include "creatures/CreatureGeometry.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace valcraft {

inline constexpr std::size_t kOldGuardMemberCount = 6U;
inline constexpr std::size_t kOldGuardRoutePointCount = 4U;
inline constexpr std::size_t kOldGuardVisualPartBudget = 64U;
inline constexpr std::size_t kOldGuardSmokeCapacity = 128U;
inline constexpr std::size_t kOldGuardFlashCapacity = kOldGuardMemberCount;
inline constexpr float kOldGuardRenderDistance = 96.0F;
inline constexpr float kOldGuardWalkSpeed = 1.10F;
inline constexpr float kOldGuardMusketRange = 50.0F;
inline constexpr float kOldGuardMusketDamage = 20.0F;
inline constexpr float kOldGuardReloadSeconds = 5.0F;
inline constexpr float kOldGuardRaiseSeconds = 0.45F;
inline constexpr float kOldGuardAimSeconds = 0.30F;
inline constexpr float kOldGuardBayonetSeconds = 0.70F;
inline constexpr float kOldGuardBayonetHitTime = 0.28F;
inline constexpr float kOldGuardBayonetCooldownSeconds = 1.0F;
inline constexpr float kOldGuardBayonetRange = 1.80F;
inline constexpr float kOldGuardBayonetDamage = 4.0F;
inline constexpr float kOldGuardTargetMemorySeconds = 2.0F;
inline constexpr float kOldGuardPerceptionInterval = 0.10F;
inline constexpr std::uint64_t kOldGuardPatrolRevision = 0x4F4C444755415244ULL;

enum class OldGuardAction : std::uint8_t {
    Patrol = 0,
    Watch = 1,
    RaiseMusket = 2,
    StabilizeAim = 3,
    Fire = 4,
    Reload = 5,
    Bayonet = 6,
};

enum class OldGuardPatrolRoute : std::uint8_t {
    AftPort = 0,
    AftStarboard = 1,
    MainDeckPort = 2,
    MainDeckStarboard = 3,
    ForecastlePort = 4,
    ForecastleStarboard = 5,
};

struct OldGuardPatrol {
    OldGuardPatrolRoute route = OldGuardPatrolRoute::AftPort;
    std::array<glm::vec3, kOldGuardRoutePointCount> points {};

    auto operator==(const OldGuardPatrol&) const -> bool = default;
};

struct OldGuardMemberSaveState {
    glm::vec3 local_position {0.0F};
    float yaw_radians = 0.0F;
    float animation_time = 0.0F;
    float action_time = 0.0F;
    float reload_remaining = 0.0F;
    float bayonet_cooldown = 0.0F;
    std::uint8_t id = 0U;
    std::uint8_t route_index = 0U;
    std::uint8_t route_step = 0U;
    OldGuardAction action = OldGuardAction::Watch;
    bool musket_loaded = true;

    auto operator==(const OldGuardMemberSaveState&) const -> bool = default;
};

struct OldGuardSaveState {
    std::array<OldGuardMemberSaveState, kOldGuardMemberCount> members {};
    std::uint64_t patrol_revision = kOldGuardPatrolRevision;
    bool initialized = false;

    auto operator==(const OldGuardSaveState&) const -> bool = default;
};

struct OldGuardRenderInstance {
    glm::vec3 position {0.0F};
    glm::vec3 local_position {0.0F};
    glm::vec3 aim_direction {1.0F, 0.0F, 0.0F};
    glm::quat platform_orientation {1.0F, 0.0F, 0.0F, 0.0F};
    float yaw_radians = 0.0F;
    float animation_time = 0.0F;
    float action_time = 0.0F;
    float action_progress = 0.0F;
    float reload_remaining = 0.0F;
    float locomotion_phase = 0.0F;
    float motion_amount = 0.0F;
    float sky_light = 1.0F;
    float local_light = 0.0F;
    float precipitation_exposure = 1.0F;
    std::uint32_t appearance_seed = 0U;
    std::uint8_t id = 0U;
    OldGuardAction action = OldGuardAction::Watch;
    bool musket_loaded = true;
};

struct OldGuardShotEvent {
    glm::vec3 muzzle_position {0.0F};
    glm::vec3 direction {1.0F, 0.0F, 0.0F};
    glm::vec3 target_position {0.0F};
    float maximum_distance = kOldGuardMusketRange;
    float damage = kOldGuardMusketDamage;
    std::uint64_t target_id = 0U;
    std::uint64_t sequence = 0U;
    std::uint8_t guard_id = 0U;
};

struct OldGuardBayonetEvent {
    glm::vec3 origin {0.0F};
    glm::vec3 tip_position {0.0F};
    glm::vec3 direction {1.0F, 0.0F, 0.0F};
    float range = kOldGuardBayonetRange;
    float damage = kOldGuardBayonetDamage;
    std::uint64_t target_id = 0U;
    std::uint64_t sequence = 0U;
    std::uint8_t guard_id = 0U;
};

struct OldGuardMuzzleFlashInstance {
    glm::vec3 position {0.0F};
    glm::vec3 direction {1.0F, 0.0F, 0.0F};
    float age = 0.0F;
    float lifetime = 0.065F;
    float size = 0.34F;
    float intensity = 1.0F;
    std::uint32_t seed = 0U;
};

struct OldGuardSmokeInstance {
    glm::vec3 position {0.0F};
    glm::vec3 velocity {0.0F};
    float age = 0.0F;
    float lifetime = 1.8F;
    float size = 0.18F;
    float rotation_radians = 0.0F;
    float angular_velocity = 0.0F;
    float opacity = 1.0F;
    std::uint32_t seed = 0U;
};

struct OldGuardPlatformFrame {
    glm::vec3 world_origin {0.0F};
    glm::vec3 velocity {0.0F};
    glm::quat orientation {1.0F, 0.0F, 0.0F, 0.0F};
};

struct OldGuardTargetCandidate {
    glm::vec3 position {0.0F};
    glm::vec3 aim_position {0.0F};
    float body_radius = 0.5F;
    float morph_factor = 0.0F;
    float health = 0.0F;
    std::uint64_t stable_id = 0U;
    CreatureSpecies species = CreatureSpecies::Pig;
    CreaturePhase phase = CreaturePhase::Day;
};

enum class OldGuardOccupantPriority : std::uint8_t {
    Guard = 0,
    CrewTask = 1,
    Player = 2,
};

struct OldGuardOccupant {
    glm::vec3 position {0.0F};
    float radius = 0.45F;
    OldGuardOccupantPriority priority = OldGuardOccupantPriority::Guard;
    bool blocks_shot = true;
};

using OldGuardLineQuery = std::function<bool(const glm::vec3& origin,
                                             const glm::vec3& target)>;
using OldGuardShotLineQuery = std::function<bool(const glm::vec3& origin,
                                                 const glm::vec3& direction,
                                                 float distance,
                                                 std::uint64_t intended_target)>;

struct OldGuardUpdateContext {
    OldGuardPlatformFrame platform {};
    std::span<const OldGuardTargetCandidate> targets {};
    std::span<const OldGuardOccupant> occupants {};
    OldGuardLineQuery visibility_clear {};
    OldGuardShotLineQuery shot_clear {};
    OldGuardLineQuery melee_clear {};
    std::function<void(const OldGuardShotEvent&)> on_shot {};
    std::function<void(const OldGuardBayonetEvent&)> on_bayonet {};
    glm::vec3 wind_velocity {0.0F};
    float sky_light = 1.0F;
    float local_light = 0.0F;
    float precipitation_exposure = 1.0F;
};

struct OldGuardFrameEvents {
    std::vector<OldGuardShotEvent> shots {};
    std::vector<OldGuardBayonetEvent> bayonet_hits {};

    void clear() {
        shots.clear();
        bayonet_hits.clear();
    }
};

struct OldGuardRayHit {
    bool hit = false;
    float distance = 0.0F;
    glm::vec3 position {0.0F};
    std::uint8_t guard_id = 0U;
};

struct OldGuardFocusState {
    bool visible = false;
    float distance = 0.0F;
    std::uint8_t guard_id = 0U;
};

[[nodiscard]] auto old_guard_patrols() noexcept
    -> const std::array<OldGuardPatrol, kOldGuardMemberCount>&;
[[nodiscard]] auto old_guard_is_hostile(const OldGuardTargetCandidate& target) noexcept -> bool;
[[nodiscard]] auto old_guard_action_duration(OldGuardAction action) noexcept -> float;
[[nodiscard]] auto sanitize_old_guard_save_state(const OldGuardSaveState& state) noexcept
    -> OldGuardSaveState;

class OldGuardSystem {
public:
    void reset(int world_seed) noexcept;
    void load_state(const OldGuardSaveState& state, int world_seed) noexcept;

    [[nodiscard]] auto update(const OldGuardUpdateContext& context, float dt)
        -> const OldGuardFrameEvents&;
    void update_effects(float dt, const glm::vec3& wind_velocity) noexcept;
    void clear_transient_effects() noexcept;

    [[nodiscard]] auto save_state() const noexcept -> const OldGuardSaveState&;
    [[nodiscard]] auto members() const noexcept -> std::span<const OldGuardMemberSaveState>;
    [[nodiscard]] auto render_instances() const noexcept -> std::span<const OldGuardRenderInstance>;
    [[nodiscard]] auto flashes() const noexcept -> std::span<const OldGuardMuzzleFlashInstance>;
    [[nodiscard]] auto smoke() const noexcept -> std::span<const OldGuardSmokeInstance>;
    [[nodiscard]] auto last_events() const noexcept -> const OldGuardFrameEvents&;

    [[nodiscard]] auto intercept_ray(const glm::vec3& origin,
                                     const glm::vec3& direction,
                                     float maximum_distance) const noexcept -> OldGuardRayHit;
    [[nodiscard]] auto focus_from_ray(const glm::vec3& origin,
                                      const glm::vec3& direction,
                                      float maximum_distance) const noexcept -> OldGuardFocusState;

private:
    struct MemberRuntime {
        glm::vec3 last_seen_position {0.0F};
        std::optional<std::uint64_t> target_id {};
        float target_memory = 0.0F;
        float perception_accumulator = kOldGuardPerceptionInterval;
        float scan_time = 0.0F;
        float locomotion_distance = 0.0F;
        float current_speed = 0.0F;
        float paused_reload_remaining = 0.0F;
        glm::vec3 weapon_aim_direction {1.0F, 0.0F, 0.0F};
        bool target_visible = false;
        bool bayonet_hit_emitted = false;
        bool weapon_aim_override = false;
    };

    void initialize_canonical_roster() noexcept;
    void refresh_perception(std::size_t member_index,
                            const OldGuardUpdateContext& context);
    void update_member(std::size_t member_index,
                       const OldGuardUpdateContext& context,
                       float dt);
    void rebuild_render_instances(const OldGuardUpdateContext& context) noexcept;
    void emit_shot(std::size_t member_index,
                   const OldGuardUpdateContext& context,
                   const OldGuardTargetCandidate& target);
    void emit_bayonet(std::size_t member_index,
                      const OldGuardUpdateContext& context,
                      const OldGuardTargetCandidate& target);
    void spawn_muzzle_effects(const OldGuardShotEvent& shot,
                              const OldGuardUpdateContext& context) noexcept;

    [[nodiscard]] auto target_for(std::size_t member_index,
                                  std::span<const OldGuardTargetCandidate> targets) const noexcept
        -> const OldGuardTargetCandidate*;
    [[nodiscard]] auto safe_line_of_fire(std::size_t member_index,
                                         const OldGuardUpdateContext& context,
                                         const OldGuardTargetCandidate& target,
                                         const glm::vec3& origin,
                                         const glm::vec3& direction,
                                         float distance) const -> bool;

    OldGuardSaveState state_ {};
    std::array<MemberRuntime, kOldGuardMemberCount> runtime_ {};
    std::array<OldGuardRenderInstance, kOldGuardMemberCount> render_instances_ {};
    std::array<OldGuardMuzzleFlashInstance, kOldGuardFlashCapacity> flashes_ {};
    std::array<OldGuardSmokeInstance, kOldGuardSmokeCapacity> smoke_ {};
    std::size_t flash_count_ = 0U;
    std::size_t smoke_count_ = 0U;
    OldGuardFrameEvents events_ {};
    OldGuardPlatformFrame last_platform_ {};
    std::uint64_t event_sequence_ = 0U;
    std::uint32_t appearance_seed_ = 0U;
};

} // namespace valcraft
