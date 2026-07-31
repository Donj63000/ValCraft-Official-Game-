#pragma once

#include "gameplay/combat/DamageZones.h"
#include "gameplay/combat/DismembermentSystem.h"
#include "gameplay/combat/StaggerSystem.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace valcraft {

inline constexpr float kChainedColossusMaximumHealth = 420.0F;
inline constexpr float kChainedColossusMaximumStagger = 120.0F;
inline constexpr float kChainedColossusWalkSpeed = 2.2F;
inline constexpr float kChainedColossusExecutionHealthRatio = 0.10F;
inline constexpr float kChainedColossusRightArmDamageMultiplier = 0.80F;
inline constexpr float kChainedColossusPhaseFourMaximumAimErrorRadians =
    0.07F;
inline constexpr float kChainedColossusLeftLegFallDurationSeconds = 0.55F;
inline constexpr std::size_t kChainedColossusLimbCount = 6U;

inline constexpr DamageZoneId kColossusTorsoZone = 1U;
inline constexpr DamageZoneId kColossusHeadZone = 2U;
inline constexpr DamageZoneId kColossusLeftArmZone = 3U;
inline constexpr DamageZoneId kColossusRightArmZone = 4U;
inline constexpr DamageZoneId kColossusLeftLegZone = 5U;
inline constexpr DamageZoneId kColossusRightLegZone = 6U;
inline constexpr DamageZoneId kColossusHornZone = 7U;

enum class ChainedColossusPhase : std::uint8_t {
    Chained = 0,
    PhaseOne,
    PhaseTwo,
    PhaseThree,
    PhaseFour,
    Kneeling,
    Dead,
};

enum class ChainedColossusAttack : std::uint8_t {
    None = 0,
    ArmSweep,
    ChainSlam,
    Stomp,
    SlowCharge,
    ShoulderBash,
};

enum class ChainedColossusAttackStage : std::uint8_t {
    Idle = 0,
    Windup,
    Active,
    Recovery,
};

enum class ChainedColossusAttackKind : std::uint8_t {
    Melee = 0,
    GroundShockwave,
    Charge,
};

enum class ColossusArmorState : std::uint8_t {
    Intact = 0,
    Cracked,
    Broken,
};

enum class ChainedColossusLocomotion : std::uint8_t {
    Normal = 0,
    LeftLegLimp,
    RightLegLimp,
    BothLegsCrippled,
    LeftLegFall,
};

struct ChainedColossusAttackEvent {
    ChainedColossusAttack attack =
        ChainedColossusAttack::None;
    ChainedColossusAttackKind kind =
        ChainedColossusAttackKind::Melee;
    glm::vec3 origin {0.0F};
    glm::vec3 direction {0.0F, 0.0F, 1.0F};
    float damage = 0.0F;
    float radius = 0.0F;
    float stability_coefficient = 1.0F;
    std::uint64_t sequence = 0U;
    bool frontally_guardable = true;
};

struct ChainedColossusState {
    glm::vec3 position {0.0F};
    glm::vec3 locked_attack_direction {0.0F, 0.0F, 1.0F};
    float yaw_radians = 0.0F;
    float health = kChainedColossusMaximumHealth;
    float attack_elapsed_seconds = 0.0F;
    float attack_cooldown_seconds = 0.0F;
    float animation_seconds = 0.0F;
    float death_elapsed_seconds = 0.0F;
    float movement_amount = 0.0F;
    float bleeding_intensity = 0.0F;
    float locomotion_cycle_seconds = 0.0F;
    float left_leg_fall_progress_seconds = 0.0F;
    float left_leg_fall_remaining_seconds = 0.0F;
    float attack_aim_error_radians = 0.0F;
    ChainedColossusPhase phase =
        ChainedColossusPhase::Chained;
    ChainedColossusAttack attack =
        ChainedColossusAttack::None;
    ChainedColossusAttackStage attack_stage =
        ChainedColossusAttackStage::Idle;
    ChainedColossusLocomotion locomotion =
        ChainedColossusLocomotion::Normal;
    std::array<ColossusArmorState, 7U>
        armor_states {};
    std::uint32_t behavior_seed = 1U;
    std::uint32_t left_leg_fall_count = 0U;
    std::uint64_t attack_sequence = 0U;
    std::uint8_t blood_level = 0U;
    bool chained = true;
    bool invulnerable = true;
    bool attack_event_emitted = false;
    bool executed = false;

    auto operator==(const ChainedColossusState&) const -> bool = default;
};

} // namespace valcraft
