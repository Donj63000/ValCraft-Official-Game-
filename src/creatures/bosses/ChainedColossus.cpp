#include "creatures/bosses/ChainedColossus.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

constexpr std::array<
    DamageZoneDefinition,
    7U>
    kColossusDamageZones {{
        {
            kColossusTorsoZone,
            DamageZoneKind::Torso,
            180.0F,
            1.0F,
            0.65F,
            1.0F,
        },
        {
            kColossusHeadZone,
            DamageZoneKind::Head,
            90.0F,
            1.30F,
            1.0F,
            1.25F,
        },
        {
            kColossusLeftArmZone,
            DamageZoneKind::LeftArm,
            65.0F,
            0.82F,
            1.0F,
            1.0F,
        },
        {
            kColossusRightArmZone,
            DamageZoneKind::RightArm,
            75.0F,
            0.86F,
            1.0F,
            1.0F,
        },
        {
            kColossusLeftLegZone,
            DamageZoneKind::LeftLeg,
            85.0F,
            0.75F,
            1.0F,
            1.1F,
        },
        {
            kColossusRightLegZone,
            DamageZoneKind::RightLeg,
            85.0F,
            0.75F,
            1.0F,
            1.1F,
        },
        {
            kColossusHornZone,
            DamageZoneKind::Horn,
            45.0F,
            0.55F,
            1.0F,
            0.8F,
        },
    }};

constexpr std::array<
    DismembermentPartDefinition,
    kChainedColossusLimbCount>
    kColossusDismemberment {{
        {
            kColossusLeftArmZone,
            40.0F,
            kColossusDisableLeftSweep,
            false,
            0.10F,
        },
        {
            kColossusRightArmZone,
            40.0F,
            kColossusDisableChainSlam |
                kColossusDisableFastRise,
            false,
            0.10F,
        },
        {
            kColossusLeftLegZone,
            40.0F,
            kColossusDisableCharge,
            false,
            0.10F,
        },
        {
            kColossusRightLegZone,
            40.0F,
            kColossusDisableCharge,
            false,
            0.10F,
        },
        {
            kColossusHornZone,
            35.0F,
            0U,
            false,
            0.10F,
        },
        {
            kColossusHeadZone,
            40.0F,
            0U,
            true,
            0.10F,
        },
    }};

constexpr std::array<DamageZoneId, 7U>
    kArmorZoneOrder {{
        kColossusTorsoZone,
        kColossusHeadZone,
        kColossusLeftArmZone,
        kColossusRightArmZone,
        kColossusLeftLegZone,
        kColossusRightLegZone,
        kColossusHornZone,
    }};

constexpr std::array<DamageZoneId, kChainedColossusLimbCount>
    kLimbZoneOrder {{
        kColossusLeftArmZone,
        kColossusRightArmZone,
        kColossusLeftLegZone,
        kColossusRightLegZone,
        kColossusHornZone,
        kColossusHeadZone,
    }};

auto finite_non_negative(float value) noexcept -> float {
    return std::isfinite(value)
               ? std::max(value, 0.0F)
               : 0.0F;
}

auto safe_horizontal_direction(
    const glm::vec3& from,
    const glm::vec3& to,
    const glm::vec3& fallback =
        {0.0F, 0.0F, 1.0F}) noexcept -> glm::vec3 {
    auto delta = to - from;
    delta.y = 0.0F;
    const auto length_squared =
        glm::dot(delta, delta);
    if (!std::isfinite(length_squared) ||
        length_squared <= 1.0e-6F) {
        return fallback;
    }
    return delta /
           std::sqrt(length_squared);
}

auto armor_index(
    DamageZoneId zone_id) noexcept
    -> std::optional<std::size_t> {
    for (std::size_t index = 0U;
         index < kArmorZoneOrder.size();
         ++index) {
        if (kArmorZoneOrder[index] ==
            zone_id) {
            return index;
        }
    }
    return std::nullopt;
}

auto zone_is_depleted(
    const DamageZones& zones,
    DamageZoneId zone_id) noexcept -> bool {
    const auto zone = zones.zone(zone_id);
    return zone.has_value() &&
           zone->condition ==
               DamageZoneCondition::Depleted;
}

auto part_is_severed(
    const DismembermentSystem& dismemberment,
    DamageZoneId zone_id) noexcept -> bool {
    const auto part = dismemberment.part(zone_id);
    return part.has_value() &&
           part->state ==
               DismembermentPartState::Severed;
}

auto valid_gore_mode(
    GorePresentationMode gore_mode) noexcept -> bool {
    switch (gore_mode) {
    case GorePresentationMode::Full:
    case GorePresentationMode::Reduced:
    case GorePresentationMode::Disabled:
        return true;
    }
    return false;
}

auto attack_suitable_for_distance(
    ChainedColossusAttack attack,
    float player_distance) noexcept -> bool {
    if (!std::isfinite(player_distance) ||
        player_distance < 0.0F) {
        return false;
    }
    switch (attack) {
    case ChainedColossusAttack::ArmSweep:
        return player_distance >= 1.20F &&
               player_distance <= 3.80F;
    case ChainedColossusAttack::ChainSlam:
        return player_distance >= 1.35F &&
               player_distance <= 3.35F;
    case ChainedColossusAttack::Stomp:
        return player_distance <= 2.80F;
    case ChainedColossusAttack::SlowCharge:
        return player_distance >= 4.50F;
    case ChainedColossusAttack::ShoulderBash:
        return player_distance <= 2.0F;
    case ChainedColossusAttack::None:
    default:
        return false;
    }
}

auto crippled_locomotion(
    bool left_leg_depleted,
    bool right_leg_depleted) noexcept
    -> ChainedColossusLocomotion {
    if (left_leg_depleted &&
        right_leg_depleted) {
        return ChainedColossusLocomotion::
            BothLegsCrippled;
    }
    if (left_leg_depleted) {
        return ChainedColossusLocomotion::
            LeftLegLimp;
    }
    if (right_leg_depleted) {
        return ChainedColossusLocomotion::
            RightLegLimp;
    }
    return ChainedColossusLocomotion::Normal;
}

auto left_leg_fall_interval_seconds(
    std::uint32_t behavior_seed) noexcept -> float {
    return 2.25F +
           static_cast<float>(
               behavior_seed % 5U) *
               0.15F;
}

auto phase_four_aim_error(
    std::uint32_t behavior_seed,
    std::uint64_t next_attack_sequence,
    ChainedColossusAttack attack) noexcept -> float {
    auto sample =
        behavior_seed ^
        static_cast<std::uint32_t>(
            next_attack_sequence) ^
        (static_cast<std::uint32_t>(attack) *
         UINT32_C(0x9E3779B9));
    sample ^= sample >> 16U;
    sample *= UINT32_C(0x7FEB352D);
    sample ^= sample >> 15U;

    const auto fraction =
        static_cast<float>(
            (sample >> 1U) % 1'001U) /
        1'000.0F;
    const auto magnitude =
        kChainedColossusPhaseFourMaximumAimErrorRadians *
        (0.50F + fraction * 0.50F);
    return (sample & 1U) == 0U
               ? magnitude
               : -magnitude;
}

auto rotate_horizontal(
    const glm::vec3& direction,
    float angle_radians) noexcept -> glm::vec3 {
    const auto sine = std::sin(angle_radians);
    const auto cosine = std::cos(angle_radians);
    return {
        direction.x * cosine +
            direction.z * sine,
        0.0F,
        direction.z * cosine -
            direction.x * sine,
    };
}

} // namespace

ChainedColossus::ChainedColossus() noexcept {
    reset(glm::vec3 {0.0F}, 1U);
}

void ChainedColossus::reset(
    const glm::vec3& position,
    std::uint32_t behavior_seed) noexcept {
    state_ = {};
    state_.position = {
        std::isfinite(position.x)
            ? position.x
            : 0.0F,
        std::isfinite(position.y)
            ? position.y
            : 0.0F,
        std::isfinite(position.z)
            ? position.z
            : 0.0F,
    };
    state_.behavior_seed =
        behavior_seed == 0U
            ? 1U
            : behavior_seed;
    state_.health =
        kChainedColossusMaximumHealth;
    state_.phase =
        ChainedColossusPhase::Chained;
    state_.chained = true;
    state_.invulnerable = true;
    state_.armor_states.fill(
        ColossusArmorState::Intact);
    attack_event_count_ = 0U;

    static_cast<void>(
        damage_zones_.configure(
            kColossusDamageZones));
    static_cast<void>(
        dismemberment_.configure(
            kColossusDismemberment));
    static_cast<void>(
        stagger_.configure({
            kChainedColossusMaximumStagger,
            12.0F,
            1.5F,
            2.4F,
        }));
}

void ChainedColossus::release() noexcept {
    if (!state_.chained ||
        state_.phase ==
            ChainedColossusPhase::Dead) {
        return;
    }
    state_.chained = false;
    state_.invulnerable = false;
    state_.phase =
        ChainedColossusPhase::PhaseOne;
    state_.attack_cooldown_seconds = 1.0F;
}

void ChainedColossus::set_invulnerable(
    bool invulnerable) noexcept {
    state_.invulnerable =
        state_.chained || invulnerable;
}

void ChainedColossus::update(
    float dt,
    const glm::vec3& player_position) noexcept {
    const auto safe_dt =
        std::clamp(
            finite_non_negative(dt),
            0.0F,
            0.25F);
    if (safe_dt <= 0.0F) {
        return;
    }
    state_.animation_seconds += safe_dt;
    if (state_.phase ==
        ChainedColossusPhase::Dead) {
        // Je conserve une horloge propre à la mort pour piloter la chute et
        // l'exécution sans réactiver l'intelligence artificielle.
        state_.death_elapsed_seconds += safe_dt;
        state_.movement_amount = 0.0F;
        return;
    }
    if (state_.chained) {
        state_.movement_amount = 0.0F;
        return;
    }

    const auto stagger_update =
        stagger_.update(safe_dt);
    const auto stagger_state =
        stagger_.state();
    if (stagger_state.staggered) {
        state_.phase =
            ChainedColossusPhase::Kneeling;
        state_.attack =
            ChainedColossusAttack::None;
        state_.attack_stage =
            ChainedColossusAttackStage::Idle;
        state_.attack_elapsed_seconds = 0.0F;
        state_.attack_aim_error_radians = 0.0F;
        state_.movement_amount = 0.0F;
        return;
    }
    if (stagger_update.stagger_ended) {
        update_phase();
        state_.attack_cooldown_seconds =
            dismemberment_
                    .capability_is_disabled(
                        kColossusDisableFastRise)
                ? 1.6F
                : 0.85F;
    }

    if (state_.left_leg_fall_remaining_seconds >
        0.0F) {
        state_.left_leg_fall_remaining_seconds =
            std::max(
                0.0F,
                state_
                    .left_leg_fall_remaining_seconds -
                    safe_dt);
        state_.locomotion =
            state_.left_leg_fall_remaining_seconds >
                    0.0F
                ? ChainedColossusLocomotion::
                      LeftLegFall
                : crippled_locomotion(
                      zone_is_depleted(
                          damage_zones_,
                          kColossusLeftLegZone),
                      zone_is_depleted(
                          damage_zones_,
                          kColossusRightLegZone));
        state_.movement_amount = 0.0F;
        return;
    }

    if (state_.attack !=
        ChainedColossusAttack::None) {
        update_attack(safe_dt);
        return;
    }

    state_.attack_cooldown_seconds =
        std::max(
            0.0F,
            state_.attack_cooldown_seconds -
                safe_dt);
    const auto delta =
        player_position -
        state_.position;
    const auto distance_squared =
        delta.x * delta.x +
        delta.z * delta.z;
    const auto distance =
        std::isfinite(distance_squared) &&
                distance_squared > 0.0F
            ? std::sqrt(distance_squared)
            : 0.0F;
    if (state_.attack_cooldown_seconds <=
        0.0F) {
        const auto selected =
            choose_attack(distance);
        if (selected !=
            ChainedColossusAttack::None) {
            start_attack(
                selected,
                player_position);
            return;
        }
    }
    update_movement(
        safe_dt,
        player_position);
}

auto ChainedColossus::apply_hit(
    const ColossusHitRequest& request) noexcept
    -> ColossusHitResult {
    ColossusHitResult result {};
    result.remaining_health = state_.health;
    if (state_.phase ==
        ChainedColossusPhase::Dead) {
        result.failure =
            ColossusHitFailure::Dead;
        return result;
    }
    if (state_.invulnerable ||
        state_.chained) {
        result.failure =
            ColossusHitFailure::Invulnerable;
        return result;
    }
    if (request.zone_id == 0U ||
        !std::isfinite(
            request.health_damage) ||
        request.health_damage < 0.0F ||
        request.health_damage >
            kMaximumDamageZoneInput ||
        !std::isfinite(
            request.stagger_power) ||
        request.stagger_power < 0.0F ||
        request.stagger_power >
            kMaximumDamageZoneInput ||
        !std::isfinite(
            request.severing_power) ||
        request.severing_power < 0.0F ||
        request.severing_power >
            kMaximumSeveringPower ||
        !valid_gore_mode(
            request.gore_mode)) {
        // Je rejette toute la requête avant de toucher aux jauges : une entrée
        // invalide ne peut donc jamais produire des dégâts partiels.
        result.failure =
            ColossusHitFailure::InvalidRequest;
        return result;
    }
    const auto zone_before =
        damage_zones_.zone(
            request.zone_id);
    if (!zone_before.has_value()) {
        result.failure =
            ColossusHitFailure::UnknownZone;
        return result;
    }

    const auto armor =
        armor_state_for(
            request.zone_id);
    const auto armor_damage_multiplier =
        armor == ColossusArmorState::Intact
            ? 0.76F
            : (armor ==
                       ColossusArmorState::Cracked
                   ? 0.90F
                   : 1.0F);
    result.zone =
        damage_zones_.apply_hit({
            request.zone_id,
            request.health_damage *
                armor_damage_multiplier,
            request.stagger_power,
        });
    if (!result.zone.accepted) {
        result.failure =
            ColossusHitFailure::UnknownZone;
        return result;
    }

    result.accepted = true;
    update_armor_state(
        result.zone,
        result.armor_broken_now);
    if (state_.left_leg_fall_remaining_seconds <=
        0.0F) {
        state_.locomotion =
            crippled_locomotion(
                zone_is_depleted(
                    damage_zones_,
                    kColossusLeftLegZone),
                zone_is_depleted(
                    damage_zones_,
                    kColossusRightLegZone));
    }
    result.health_damage =
        std::min(
            state_.health,
            result.zone.health_damage);
    state_.health =
        std::max(
            0.0F,
            state_.health -
                result.health_damage);
    result.stagger =
        stagger_.apply(
            result.zone.stagger_damage);
    result.stagger_triggered =
        result.stagger.triggered;
    update_phase();

    const auto health_ratio =
        state_.health /
        kChainedColossusMaximumHealth;
    result.dismemberment =
        dismemberment_.try_section({
            request.zone_id,
            result.zone.condition,
            request.severing_power,
            std::clamp(
                health_ratio,
                0.0F,
                1.0F),
            phase_allows_section(
                request.zone_id),
            armor_state_for(
                request.zone_id) !=
                ColossusArmorState::Broken,
            request.blade_crossed_zone,
            stagger_.state().staggered,
            request.execution_attack,
            request.gore_mode,
        });
    result.limb_severed =
        result.dismemberment.severed_now;
    if (result.limb_severed) {
        state_.bleeding_intensity =
            std::min(
                1.0F,
                state_.bleeding_intensity +
                    0.28F);
    }

    if (request.zone_id ==
            kColossusHeadZone &&
        result.limb_severed) {
        state_.health = 0.0F;
        state_.executed = true;
        result.execution_completed = true;
    }
    if (state_.health <= 0.0F) {
        state_.phase =
            ChainedColossusPhase::Dead;
        state_.attack =
            ChainedColossusAttack::None;
        state_.attack_stage =
            ChainedColossusAttackStage::Idle;
        state_.attack_aim_error_radians = 0.0F;
        state_.movement_amount = 0.0F;
        state_.death_elapsed_seconds = 0.0F;
        result.killed = true;
    }
    result.remaining_health = state_.health;
    return result;
}

auto ChainedColossus::can_execute() const noexcept
    -> bool {
    if (state_.phase ==
            ChainedColossusPhase::Dead ||
        state_.chained ||
        state_.health >=
            kChainedColossusMaximumHealth *
                kChainedColossusExecutionHealthRatio ||
        !stagger_.state().staggered) {
        return false;
    }
    const auto head =
        damage_zones_.zone(
            kColossusHeadZone);
    return head.has_value() &&
           head->condition ==
               DamageZoneCondition::Depleted;
}

auto ChainedColossus::attack_available(
    ChainedColossusAttack attack) const noexcept
    -> bool {
    const auto disabled =
        dismemberment_.disabled_capabilities();
    switch (attack) {
    case ChainedColossusAttack::ArmSweep:
        return (disabled &
                kColossusDisableLeftSweep) ==
               0U;
    case ChainedColossusAttack::ChainSlam:
        return (disabled &
                kColossusDisableChainSlam) ==
               0U;
    case ChainedColossusAttack::SlowCharge:
        // Je retire la charge dès qu'une jambe est gravement blessée, même
        // avant son sectionnement, comme le demande le comportement du boss.
        return (disabled &
                kColossusDisableCharge) ==
                   0U &&
               !zone_is_depleted(
                   damage_zones_,
                   kColossusLeftLegZone) &&
               !zone_is_depleted(
                   damage_zones_,
                   kColossusRightLegZone);
    case ChainedColossusAttack::Stomp:
    case ChainedColossusAttack::ShoulderBash:
        return true;
    case ChainedColossusAttack::None:
    default:
        return false;
    }
}

auto ChainedColossus::state() const noexcept
    -> const ChainedColossusState& {
    return state_;
}

auto ChainedColossus::stagger_state() const noexcept
    -> StaggerState {
    return stagger_.state();
}

auto ChainedColossus::limb_views() const noexcept
    -> std::array<
        ChainedColossusLimbView,
        kChainedColossusLimbCount> {
    std::array<
        ChainedColossusLimbView,
        kChainedColossusLimbCount>
        views {};
    for (std::size_t index = 0U;
         index < kLimbZoneOrder.size();
         ++index) {
        const auto zone_id =
            kLimbZoneOrder[index];
        const auto zone =
            damage_zones_.zone(zone_id);
        const auto part =
            dismemberment_.part(zone_id);
        views[index].zone_id = zone_id;
        views[index].armor =
            armor_state_for(zone_id);
        if (zone.has_value()) {
            views[index].kind =
                zone->definition.kind;
            views[index].condition =
                zone->condition;
            views[index]
                .remaining_resistance =
                zone
                    ->remaining_local_resistance;
            views[index]
                .maximum_resistance =
                zone->definition
                    .maximum_local_resistance;
        }
        if (part.has_value()) {
            views[index].part_state =
                part->state;
        }
    }
    return views;
}

auto ChainedColossus::
    consume_attack_events() noexcept
    -> std::span<
        const ChainedColossusAttackEvent> {
    const auto result =
        std::span<
            const ChainedColossusAttackEvent> {
            attack_events_.data(),
            attack_event_count_,
        };
    attack_event_count_ = 0U;
    return result;
}

auto ChainedColossus::attack_definition(
    ChainedColossusAttack attack) noexcept
    -> AttackDefinition {
    switch (attack) {
    case ChainedColossusAttack::ArmSweep:
        return {
            3.0F,
            1.0F,
            0.20F,
            1.0F,
            3.6F,
            0.85F,
            ChainedColossusAttackKind::Melee,
            true,
        };
    case ChainedColossusAttack::ChainSlam:
        return {
            5.0F,
            1.4F,
            0.18F,
            1.25F,
            3.0F,
            1.35F,
            ChainedColossusAttackKind::Melee,
            true,
        };
    case ChainedColossusAttack::Stomp:
        return {
            2.0F,
            0.80F,
            0.25F,
            0.85F,
            2.7F,
            0.70F,
            ChainedColossusAttackKind::
                GroundShockwave,
            false,
        };
    case ChainedColossusAttack::SlowCharge:
        return {
            6.0F,
            1.55F,
            0.55F,
            1.35F,
            1.35F,
            1.50F,
            ChainedColossusAttackKind::Charge,
            true,
        };
    case ChainedColossusAttack::ShoulderBash:
        return {
            4.0F,
            0.65F,
            0.18F,
            0.90F,
            1.8F,
            1.0F,
            ChainedColossusAttackKind::Melee,
            true,
        };
    case ChainedColossusAttack::None:
    default:
        return {};
    }
}

auto ChainedColossus::choose_attack(
    float player_distance) const noexcept
    -> ChainedColossusAttack {
    constexpr std::array<
        ChainedColossusAttack,
        5U>
        kCandidates {{
            ChainedColossusAttack::ArmSweep,
            ChainedColossusAttack::ChainSlam,
            ChainedColossusAttack::Stomp,
            ChainedColossusAttack::SlowCharge,
            ChainedColossusAttack::ShoulderBash,
        }};
    const auto start =
        static_cast<std::size_t>(
            (state_.attack_sequence +
             state_.behavior_seed) %
            kCandidates.size());
    for (std::size_t offset = 0U;
         offset < kCandidates.size();
         ++offset) {
        const auto candidate =
            kCandidates[
                (start + offset) %
            kCandidates.size()];
        if (attack_available(candidate) &&
            attack_suitable_for_distance(
                candidate,
                player_distance)) {
            return candidate;
        }
    }
    return ChainedColossusAttack::None;
}

void ChainedColossus::start_attack(
    ChainedColossusAttack attack,
    const glm::vec3& player_position) noexcept {
    if (!attack_available(attack)) {
        return;
    }
    state_.attack = attack;
    state_.attack_stage =
        ChainedColossusAttackStage::Windup;
    state_.attack_elapsed_seconds = 0.0F;
    state_.attack_event_emitted = false;
    state_.locked_attack_direction =
        safe_horizontal_direction(
            state_.position,
            player_position,
            state_.locked_attack_direction);
    state_.attack_aim_error_radians = 0.0F;
    if (state_.phase ==
        ChainedColossusPhase::PhaseFour) {
        auto next_sequence =
            state_.attack_sequence + 1U;
        if (next_sequence == 0U) {
            next_sequence = 1U;
        }
        state_.attack_aim_error_radians =
            phase_four_aim_error(
                state_.behavior_seed,
                next_sequence,
                attack);
        // Je limite l'imprécision finale à quatre degrés environ : le joueur
        // lit toujours la télégraphie, mais un Colosse mourant vise moins bien.
        state_.locked_attack_direction =
            rotate_horizontal(
                state_.locked_attack_direction,
                state_.attack_aim_error_radians);
    }
    state_.yaw_radians =
        std::atan2(
            state_.locked_attack_direction.x,
            state_.locked_attack_direction.z);
    state_.movement_amount = 0.0F;
}

void ChainedColossus::update_attack(
    float dt) noexcept {
    if (state_.attack_stage ==
            ChainedColossusAttackStage::Windup &&
        !attack_available(state_.attack)) {
        // Je supprime proprement une attaque devenue impossible pendant sa
        // télégraphie, par exemple quand le joueur sectionne le bras armé.
        state_.attack = ChainedColossusAttack::None;
        state_.attack_stage =
            ChainedColossusAttackStage::Idle;
        state_.attack_elapsed_seconds = 0.0F;
        state_.attack_event_emitted = false;
        state_.attack_aim_error_radians = 0.0F;
        state_.attack_cooldown_seconds = 0.85F;
        state_.movement_amount = 0.0F;
        return;
    }
    const auto definition =
        attack_definition(state_.attack);
    state_.attack_elapsed_seconds += dt;
    if (state_.attack_elapsed_seconds <
        definition.windup_seconds) {
        state_.attack_stage =
            ChainedColossusAttackStage::Windup;
        return;
    }

    if (!state_.attack_event_emitted) {
        state_.attack_event_emitted = true;
        state_.attack_stage =
            ChainedColossusAttackStage::Active;
        queue_attack_event(definition);
    }
    const auto active_end =
        definition.windup_seconds +
        definition.active_seconds;
    if (state_.attack_elapsed_seconds <
        active_end) {
        state_.attack_stage =
            ChainedColossusAttackStage::Active;
        if (state_.attack ==
            ChainedColossusAttack::SlowCharge) {
            state_.position +=
                state_.locked_attack_direction *
                (kChainedColossusWalkSpeed *
                 2.8F * dt);
            state_.movement_amount = 1.0F;
        }
        return;
    }
    const auto recovery_end =
        active_end +
        definition.recovery_seconds;
    if (state_.attack_elapsed_seconds <
        recovery_end) {
        state_.attack_stage =
            ChainedColossusAttackStage::Recovery;
        state_.movement_amount = 0.0F;
        return;
    }

    state_.attack =
        ChainedColossusAttack::None;
    state_.attack_stage =
        ChainedColossusAttackStage::Idle;
    state_.attack_elapsed_seconds = 0.0F;
    state_.attack_event_emitted = false;
    state_.attack_aim_error_radians = 0.0F;
    state_.attack_cooldown_seconds = 2.2F;
    state_.movement_amount = 0.0F;
}

void ChainedColossus::update_movement(
    float dt,
    const glm::vec3& player_position) noexcept {
    const auto target_direction =
        safe_horizontal_direction(
            state_.position,
            player_position,
            state_.locked_attack_direction);
    state_.locked_attack_direction =
        target_direction;
    state_.yaw_radians =
        std::atan2(
            target_direction.x,
            target_direction.z);
    const auto delta =
        player_position -
        state_.position;
    const auto distance_squared =
        delta.x * delta.x +
        delta.z * delta.z;
    const auto left_leg_depleted =
        zone_is_depleted(
            damage_zones_,
            kColossusLeftLegZone);
    const auto right_leg_depleted =
        zone_is_depleted(
            damage_zones_,
            kColossusRightLegZone);
    state_.locomotion =
        crippled_locomotion(
            left_leg_depleted,
            right_leg_depleted);
    if (!std::isfinite(distance_squared) ||
        distance_squared <=
            2.8F * 2.8F) {
        state_.movement_amount = 0.0F;
        return;
    }

    state_.locomotion_cycle_seconds += dt;
    if (left_leg_depleted) {
        state_.left_leg_fall_progress_seconds +=
            dt;
        const auto fall_interval =
            left_leg_fall_interval_seconds(
                state_.behavior_seed);
        if (state_
                .left_leg_fall_progress_seconds >=
            fall_interval) {
            state_.left_leg_fall_progress_seconds =
                std::max(
                    0.0F,
                    state_
                        .left_leg_fall_progress_seconds -
                        fall_interval);
            state_.left_leg_fall_remaining_seconds =
                kChainedColossusLeftLegFallDurationSeconds;
            if (state_.left_leg_fall_count <
                std::numeric_limits<
                    std::uint32_t>::max()) {
                ++state_.left_leg_fall_count;
            }
            state_.locomotion =
                ChainedColossusLocomotion::
                    LeftLegFall;
            state_.movement_amount = 0.0F;
            return;
        }
    } else {
        state_.left_leg_fall_progress_seconds =
            0.0F;
    }

    const auto phase_speed =
        state_.phase ==
                    ChainedColossusPhase::
                        PhaseThree
            ? 1.10F
            : 1.0F;
    const auto limb_speed =
        left_leg_depleted &&
                right_leg_depleted
            ? 0.20F
            : (left_leg_depleted
                   ? 0.35F
                   : (right_leg_depleted
                          ? 0.42F
                          : 1.0F));

    auto travel_direction =
        target_direction;
    if (right_leg_depleted) {
        // Je donne à la jambe droite une boiterie latérale distincte, bornée
        // et déterministe, au lieu de réutiliser la chute de la jambe gauche.
        const auto sway_amplitude =
            left_leg_depleted ? 0.07F : 0.14F;
        const auto sway =
            std::sin(
                state_.locomotion_cycle_seconds *
                4.5F) *
            sway_amplitude;
        const glm::vec3 lateral {
            target_direction.z,
            0.0F,
            -target_direction.x,
        };
        travel_direction =
            safe_horizontal_direction(
                glm::vec3 {0.0F},
                target_direction +
                    lateral * sway,
                target_direction);
        state_.yaw_radians =
            std::atan2(
                travel_direction.x,
                travel_direction.z);
    }

    state_.position +=
        travel_direction *
        (kChainedColossusWalkSpeed *
         phase_speed *
         limb_speed *
         dt);
    state_.movement_amount =
        limb_speed;
}

void ChainedColossus::update_phase() noexcept {
    if (state_.health <= 0.0F) {
        state_.phase =
            ChainedColossusPhase::Dead;
        state_.blood_level = 3U;
        return;
    }
    if (state_.chained) {
        state_.phase =
            ChainedColossusPhase::Chained;
        return;
    }
    if (stagger_.state().staggered) {
        state_.phase =
            ChainedColossusPhase::Kneeling;
        return;
    }

    const auto ratio =
        state_.health /
        kChainedColossusMaximumHealth;
    if (ratio > 0.75F) {
        state_.phase =
            ChainedColossusPhase::PhaseOne;
        state_.blood_level = 0U;
    } else if (ratio > 0.50F) {
        state_.phase =
            ChainedColossusPhase::PhaseTwo;
        state_.blood_level = 1U;
    } else if (ratio > 0.25F) {
        state_.phase =
            ChainedColossusPhase::PhaseThree;
        state_.blood_level = 2U;
    } else {
        state_.phase =
            ChainedColossusPhase::PhaseFour;
        state_.blood_level = 3U;
    }
    state_.bleeding_intensity =
        std::max(
            state_.bleeding_intensity,
            static_cast<float>(
                state_.blood_level) /
                3.0F);
}

void ChainedColossus::queue_attack_event(
    const AttackDefinition& definition) noexcept {
    if (attack_event_count_ >=
        attack_events_.size()) {
        return;
    }
    ++state_.attack_sequence;
    if (state_.attack_sequence == 0U) {
        state_.attack_sequence = 1U;
    }
    // Je répercute la perte du bras droit sur toute la puissance restante du
    // Colosse, et pas uniquement sur la disponibilité de la frappe de chaîne.
    const auto outgoing_damage_multiplier =
        part_is_severed(
            dismemberment_,
            kColossusRightArmZone)
            ? kChainedColossusRightArmDamageMultiplier
            : 1.0F;
    attack_events_[attack_event_count_++] = {
        state_.attack,
        definition.kind,
        state_.position +
            glm::vec3 {0.0F, 1.4F, 0.0F},
        state_.locked_attack_direction,
        definition.damage *
            outgoing_damage_multiplier,
        definition.radius,
        definition.stability_coefficient,
        state_.attack_sequence,
        definition.frontally_guardable,
    };
}

auto ChainedColossus::armor_state_for(
    DamageZoneId zone_id) const noexcept
    -> ColossusArmorState {
    const auto index =
        armor_index(zone_id);
    if (!index.has_value()) {
        return ColossusArmorState::Broken;
    }
    return state_.armor_states[*index];
}

void ChainedColossus::update_armor_state(
    const DamageZoneHitResult& zone,
    bool& broken_now) noexcept {
    const auto index =
        armor_index(zone.zone_id);
    if (!index.has_value()) {
        return;
    }
    const auto maximum =
        damage_zones_.zone(zone.zone_id)
            ->definition
            .maximum_local_resistance;
    const auto ratio =
        maximum > 0.0F
            ? zone.remaining_local_resistance /
                  maximum
            : 0.0F;
    const auto previous =
        state_.armor_states[*index];
    const auto next =
        ratio <= 0.20F
            ? ColossusArmorState::Broken
            : (ratio <= 0.60F
                   ? ColossusArmorState::Cracked
                   : ColossusArmorState::Intact);
    state_.armor_states[*index] = next;
    broken_now =
        previous !=
            ColossusArmorState::Broken &&
        next ==
            ColossusArmorState::Broken;
}

auto ChainedColossus::phase_allows_section(
    DamageZoneId zone_id) const noexcept -> bool {
    const auto ratio =
        state_.health /
        kChainedColossusMaximumHealth;
    if (zone_id == kColossusHeadZone) {
        return ratio <
               kChainedColossusExecutionHealthRatio;
    }
    if (zone_id ==
        kColossusHornZone) {
        return ratio <= 0.75F;
    }
    if (zone_id ==
            kColossusLeftArmZone ||
        zone_id ==
            kColossusRightArmZone) {
        return ratio <= 0.75F;
    }
    if (zone_id ==
            kColossusLeftLegZone ||
        zone_id ==
            kColossusRightLegZone) {
        return ratio <= 0.50F;
    }
    return false;
}

} // namespace valcraft
