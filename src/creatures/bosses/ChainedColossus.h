#pragma once

#include "creatures/bosses/ChainedColossusState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace valcraft {

inline constexpr std::uint64_t
    kColossusDisableLeftSweep = UINT64_C(1) << 0U;
inline constexpr std::uint64_t
    kColossusDisableChainSlam = UINT64_C(1) << 1U;
inline constexpr std::uint64_t
    kColossusDisableCharge = UINT64_C(1) << 2U;
inline constexpr std::uint64_t
    kColossusDisableFastRise = UINT64_C(1) << 3U;

enum class ColossusHitFailure : std::uint8_t {
    None = 0,
    Invulnerable,
    Dead,
    InvalidRequest,
    UnknownZone,
};

struct ColossusHitRequest {
    DamageZoneId zone_id =
        kColossusTorsoZone;
    float health_damage = 0.0F;
    float stagger_power = 0.0F;
    float severing_power = 0.0F;
    GorePresentationMode gore_mode =
        GorePresentationMode::Full;
    bool blade_crossed_zone = true;
    bool execution_attack = false;
};

struct ColossusHitResult {
    bool accepted = false;
    bool killed = false;
    bool armor_broken_now = false;
    bool stagger_triggered = false;
    bool limb_severed = false;
    bool execution_completed = false;
    ColossusHitFailure failure =
        ColossusHitFailure::None;
    float health_damage = 0.0F;
    float remaining_health =
        kChainedColossusMaximumHealth;
    DamageZoneHitResult zone {};
    StaggerApplyResult stagger {};
    DismembermentResult dismemberment {};
};

struct ChainedColossusLimbView {
    DamageZoneId zone_id = 0U;
    DamageZoneKind kind =
        DamageZoneKind::Torso;
    DamageZoneCondition condition =
        DamageZoneCondition::Intact;
    DismembermentPartState part_state =
        DismembermentPartState::Intact;
    ColossusArmorState armor =
        ColossusArmorState::Intact;
    float remaining_resistance = 0.0F;
    float maximum_resistance = 0.0F;
};

class ChainedColossus {
public:
    ChainedColossus() noexcept;

    void reset(
        const glm::vec3& position,
        std::uint32_t behavior_seed = 1U) noexcept;
    void release() noexcept;
    void set_invulnerable(bool invulnerable) noexcept;
    void update(
        float dt,
        const glm::vec3& player_position) noexcept;

    [[nodiscard]] auto apply_hit(
        const ColossusHitRequest& request) noexcept
        -> ColossusHitResult;
    [[nodiscard]] auto can_execute() const noexcept -> bool;
    [[nodiscard]] auto attack_available(
        ChainedColossusAttack attack) const noexcept -> bool;

    [[nodiscard]] auto state() const noexcept
        -> const ChainedColossusState&;
    [[nodiscard]] auto stagger_state() const noexcept
        -> StaggerState;
    [[nodiscard]] auto limb_views() const noexcept
        -> std::array<
            ChainedColossusLimbView,
            kChainedColossusLimbCount>;
    [[nodiscard]] auto consume_attack_events() noexcept
        -> std::span<
            const ChainedColossusAttackEvent>;

private:
    struct AttackDefinition {
        float damage = 0.0F;
        float windup_seconds = 0.0F;
        float active_seconds = 0.0F;
        float recovery_seconds = 0.0F;
        float radius = 0.0F;
        float stability_coefficient = 1.0F;
        ChainedColossusAttackKind kind =
            ChainedColossusAttackKind::Melee;
        bool frontally_guardable = true;
    };

    [[nodiscard]] static auto attack_definition(
        ChainedColossusAttack attack) noexcept
        -> AttackDefinition;
    [[nodiscard]] auto choose_attack(
        float player_distance) const noexcept
        -> ChainedColossusAttack;
    void start_attack(
        ChainedColossusAttack attack,
        const glm::vec3& player_position) noexcept;
    void update_attack(float dt) noexcept;
    void update_movement(
        float dt,
        const glm::vec3& player_position) noexcept;
    void update_phase() noexcept;
    void queue_attack_event(
        const AttackDefinition& definition) noexcept;
    [[nodiscard]] auto armor_state_for(
        DamageZoneId zone_id) const noexcept
        -> ColossusArmorState;
    void update_armor_state(
        const DamageZoneHitResult& zone,
        bool& broken_now) noexcept;
    [[nodiscard]] auto phase_allows_section(
        DamageZoneId zone_id) const noexcept -> bool;

    ChainedColossusState state_ {};
    DamageZones damage_zones_ {};
    DismembermentSystem dismemberment_ {};
    StaggerSystem stagger_ {};
    std::array<
        ChainedColossusAttackEvent,
        4U>
        attack_events_ {};
    std::size_t attack_event_count_ = 0U;
};

} // namespace valcraft
