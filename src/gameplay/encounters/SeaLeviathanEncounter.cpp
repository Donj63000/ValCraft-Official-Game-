#include "gameplay/encounters/SeaLeviathanEncounter.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace valcraft {

namespace {

constexpr std::array<glm::vec3, kSeaLeviathanTentacleCount>
    kTentacleOffsets {{
        {-4.2F, 1.2F, -1.8F},
        {4.2F, 1.2F, -1.8F},
        {-4.6F, 1.1F, 2.7F},
        {4.6F, 1.1F, 2.7F},
    }};

[[nodiscard]] auto finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto mixed_seed(std::uint32_t value) noexcept
    -> std::uint32_t {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] auto valid_part(SeaLeviathanPart part) noexcept -> bool {
    switch (part) {
    case SeaLeviathanPart::Core:
    case SeaLeviathanPart::Carapace:
    case SeaLeviathanPart::Tentacle0:
    case SeaLeviathanPart::Tentacle1:
    case SeaLeviathanPart::Tentacle2:
    case SeaLeviathanPart::Tentacle3:
        return true;
    }
    return false;
}

[[nodiscard]] auto squared(float value) noexcept -> float {
    return value * value;
}

[[nodiscard]] auto tentacle_part(std::size_t index) noexcept
    -> SeaLeviathanPart {
    return static_cast<SeaLeviathanPart>(
        static_cast<std::uint8_t>(SeaLeviathanPart::Tentacle0) +
        static_cast<std::uint8_t>(index));
}

} // namespace

auto valid_ship_local_frame(
    const ShipLocalFrame& frame) noexcept -> bool {
    if (!finite_vec3(frame.origin) ||
        !finite_vec3(frame.right) ||
        !finite_vec3(frame.up) ||
        !finite_vec3(frame.forward)) {
        return false;
    }
    constexpr auto kLengthTolerance = 0.03F;
    constexpr auto kOrthogonalTolerance = 0.03F;
    const auto unit =
        [](const glm::vec3& axis) noexcept {
            return std::abs(glm::dot(axis, axis) - 1.0F) <=
                   kLengthTolerance;
        };
    return unit(frame.right) &&
           unit(frame.up) &&
           unit(frame.forward) &&
           std::abs(glm::dot(frame.right, frame.up)) <=
               kOrthogonalTolerance &&
           std::abs(glm::dot(frame.right, frame.forward)) <=
               kOrthogonalTolerance &&
           std::abs(glm::dot(frame.up, frame.forward)) <=
               kOrthogonalTolerance;
}

auto ship_local_to_world(
    const ShipLocalFrame& frame,
    const glm::vec3& local_position) noexcept
    -> std::optional<glm::vec3> {
    if (!valid_ship_local_frame(frame) ||
        !finite_vec3(local_position)) {
        return std::nullopt;
    }
    return frame.origin +
           frame.right * local_position.x +
           frame.up * local_position.y +
           frame.forward * local_position.z;
}

auto ship_world_to_local(
    const ShipLocalFrame& frame,
    const glm::vec3& world_position) noexcept
    -> std::optional<glm::vec3> {
    if (!valid_ship_local_frame(frame) ||
        !finite_vec3(world_position)) {
        return std::nullopt;
    }
    const auto delta = world_position - frame.origin;
    return glm::vec3 {
        glm::dot(delta, frame.right),
        glm::dot(delta, frame.up),
        glm::dot(delta, frame.forward),
    };
}

void SeaLeviathanEncounter::push_event(
    SeaLeviathanEventKind kind,
    SeaLeviathanPart part,
    const glm::vec3& local_position,
    float amount,
    SeaLeviathanDamageDirective damage) noexcept {
    // Je neutralise ici les deux valeurs interdites : aucune branche d'IA ne
    // peut demander au raccord d'endommager le navire ou un allié.
    damage.ship_damage = 0.0F;
    damage.allied_damage = 0.0F;
    const auto event = SeaLeviathanEvent {
        kind,
        phase_,
        part,
        simulation_tick_,
        local_position,
        amount,
        damage,
    };
    if (event_count_ < events_.size()) {
        events_[event_count_] = event;
        ++event_count_;
        return;
    }
    std::move(events_.begin() + 1, events_.end(), events_.begin());
    events_.back() = event;
    ++dropped_event_count_;
}

auto SeaLeviathanEncounter::start(
    const SeaLeviathanStartRequest& request) noexcept
    -> SeaLeviathanStartResult {
    SeaLeviathanStartResult result {};
    if (active()) {
        result.error = SeaLeviathanStartError::AlreadyActive;
        return result;
    }
    if (!finite_vec3(request.body_anchor_ship_local)) {
        result.error = SeaLeviathanStartError::InvalidAnchor;
        return result;
    }

    reset();
    const auto health_units =
        mixed_seed(request.deterministic_seed) % 1'501U;
    maximum_health_ =
        kSeaLeviathanMinimumHealth +
        static_cast<float>(health_units);
    health_ = maximum_health_;
    body_anchor_local_ = request.body_anchor_ship_local;
    carapace_ = maximum_carapace_;
    for (std::size_t index = 0U;
         index < tentacles_.size();
         ++index) {
        const auto resistance =
            145.0F +
            static_cast<float>(
                mixed_seed(
                    request.deterministic_seed +
                    static_cast<std::uint32_t>(index + 1U)) %
                36U);
        tentacles_[index] = {
            resistance,
            resistance,
            false,
        };
    }
    phase_ = SeaLeviathanPhase::Emerging;
    phase_ticks_remaining_ = 120U;
    attack_sequence_ = 2U;
    push_event(
        SeaLeviathanEventKind::EncounterStarted,
        SeaLeviathanPart::Core,
        body_anchor_local_);
    result.started = true;
    result.maximum_health = maximum_health_;
    return result;
}

void SeaLeviathanEncounter::change_phase(
    SeaLeviathanPhase phase) noexcept {
    if (phase_ == phase) {
        return;
    }
    phase_ = phase;
    push_event(
        SeaLeviathanEventKind::PhaseChanged,
        SeaLeviathanPart::Core,
        body_anchor_local_);
}

auto SeaLeviathanEncounter::base_combat_phase() const noexcept
    -> SeaLeviathanPhase {
    if (maximum_health_ > 0.0F &&
        health_ / maximum_health_ <= 0.35F) {
        return SeaLeviathanPhase::Frenzy;
    }
    return SeaLeviathanPhase::CarapaceAssault;
}

void SeaLeviathanEncounter::expose_core() noexcept {
    change_phase(SeaLeviathanPhase::ExposedCore);
    phase_ticks_remaining_ = 360U;
    push_event(
        SeaLeviathanEventKind::CoreExposed,
        SeaLeviathanPart::Core,
        body_anchor_local_ + glm::vec3 {0.0F, 2.4F, 0.0F});
}

auto SeaLeviathanEncounter::tentacle_index(
    SeaLeviathanPart part) const noexcept
    -> std::optional<std::size_t> {
    const auto raw = static_cast<std::uint8_t>(part);
    const auto first =
        static_cast<std::uint8_t>(SeaLeviathanPart::Tentacle0);
    const auto last =
        static_cast<std::uint8_t>(SeaLeviathanPart::Tentacle3);
    if (raw < first || raw > last) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(raw - first);
}

void SeaLeviathanEncounter::begin_attack() noexcept {
    const auto deck_attack = (attack_sequence_ % 3U) == 2U;
    ++attack_sequence_;
    if (deck_attack) {
        active_attack_ = SeaLeviathanAttack::DeckSmash;
        attack_ticks_remaining_ = 48U;
        telegraph_local_ = glm::vec3 {0.0F, 0.8F, 0.0F};
        change_phase(SeaLeviathanPhase::GuardWindow);
        push_event(
            SeaLeviathanEventKind::GuardWindowOpened,
            SeaLeviathanPart::Tentacle0,
            telegraph_local_,
            0.8F);
        push_event(
            SeaLeviathanEventKind::DeckStrikeTelegraphed,
            SeaLeviathanPart::Tentacle0,
            telegraph_local_,
            0.8F);
        return;
    }

    auto selected = tentacles_.size();
    for (std::size_t offset = 0U;
         offset < tentacles_.size();
         ++offset) {
        const auto candidate =
            (attacking_tentacle_index_ + offset) %
            tentacles_.size();
        if (!tentacles_[candidate].severed) {
            selected = candidate;
            break;
        }
    }
    if (selected >= tentacles_.size()) {
        active_attack_ = SeaLeviathanAttack::DeckSmash;
        attack_ticks_remaining_ = 54U;
        telegraph_local_ = glm::vec3 {0.0F, 0.8F, 0.0F};
        push_event(
            SeaLeviathanEventKind::DeckStrikeTelegraphed,
            SeaLeviathanPart::Core,
            telegraph_local_,
            0.9F);
        return;
    }

    attacking_tentacle_index_ = selected;
    active_attack_ = SeaLeviathanAttack::TentacleSweep;
    attack_ticks_remaining_ =
        phase_ == SeaLeviathanPhase::Frenzy ? 30U : 42U;
    telegraph_local_ = kTentacleOffsets[selected];
    push_event(
        SeaLeviathanEventKind::TentacleTelegraphed,
        tentacle_part(selected),
        telegraph_local_,
        static_cast<float>(attack_ticks_remaining_) / 60.0F);
}

void SeaLeviathanEncounter::resolve_attack(
    const SeaLeviathanUpdateInput& input) noexcept {
    if (active_attack_ == SeaLeviathanAttack::TentacleSweep) {
        const auto part = tentacle_part(attacking_tentacle_index_);
        const auto damage =
            phase_ == SeaLeviathanPhase::Frenzy ? 20.0F : 14.0F;
        push_event(
            SeaLeviathanEventKind::TentacleStrike,
            part,
            telegraph_local_,
            damage,
            SeaLeviathanDamageDirective {
                input.player_alive ? damage : 0.0F,
                18.0F,
                0.0F,
                0.0F,
            });
    } else if (active_attack_ == SeaLeviathanAttack::DeckSmash) {
        if (input.perfect_guard_active &&
            input.guarding_with_legendary_weapon) {
            push_event(
                SeaLeviathanEventKind::PerfectGuard,
                SeaLeviathanPart::Tentacle0,
                telegraph_local_,
                0.0F,
                SeaLeviathanDamageDirective {
                    0.0F,
                    5.0F,
                    0.0F,
                    0.0F,
                });
        } else if (input.guarding_with_legendary_weapon) {
            push_event(
                SeaLeviathanEventKind::DeckStrikeGuarded,
                SeaLeviathanPart::Tentacle0,
                telegraph_local_,
                4.0F,
                SeaLeviathanDamageDirective {
                    input.player_alive ? 4.0F : 0.0F,
                    32.0F,
                    0.0F,
                    0.0F,
                });
        } else {
            push_event(
                SeaLeviathanEventKind::PlayerHit,
                SeaLeviathanPart::Tentacle0,
                telegraph_local_,
                24.0F,
                SeaLeviathanDamageDirective {
                    input.player_alive ? 24.0F : 0.0F,
                    0.0F,
                    0.0F,
                    0.0F,
                });
        }
        change_phase(SeaLeviathanPhase::ChargedOpening);
        phase_ticks_remaining_ = 480U;
        push_event(
            SeaLeviathanEventKind::ChargedOpeningRequested,
            SeaLeviathanPart::Carapace,
            body_anchor_local_ + glm::vec3 {0.0F, 2.2F, -0.4F},
            8.0F);
    }
    active_attack_ = SeaLeviathanAttack::None;
    attack_cooldown_ticks_ =
        phase_ == SeaLeviathanPhase::Frenzy ? 90U : 150U;
}

void SeaLeviathanEncounter::update_tick(
    const SeaLeviathanUpdateInput& input) noexcept {
    if (phase_ == SeaLeviathanPhase::Dormant ||
        phase_ == SeaLeviathanPhase::Defeated) {
        return;
    }
    if (phase_ == SeaLeviathanPhase::Emerging) {
        if (phase_ticks_remaining_ > 0U) {
            --phase_ticks_remaining_;
        }
        if (phase_ticks_remaining_ == 0U) {
            change_phase(SeaLeviathanPhase::CarapaceAssault);
            attack_cooldown_ticks_ = 120U;
            push_event(
                SeaLeviathanEventKind::Emerged,
                SeaLeviathanPart::Core,
                body_anchor_local_);
        }
        return;
    }

    if (phase_ == SeaLeviathanPhase::ExposedCore ||
        phase_ == SeaLeviathanPhase::ChargedOpening) {
        if (phase_ticks_remaining_ > 0U) {
            --phase_ticks_remaining_;
        }
        if (phase_ticks_remaining_ == 0U) {
            if (phase_ == SeaLeviathanPhase::ExposedCore) {
                push_event(
                    SeaLeviathanEventKind::CoreClosed,
                    SeaLeviathanPart::Core,
                    body_anchor_local_);
            }
            change_phase(base_combat_phase());
            attack_cooldown_ticks_ = 90U;
        }
    }

    if (stagger_ > 0.0F) {
        stagger_ = std::max(0.0F, stagger_ - 6.0F / 60.0F);
    }
    if (attack_ticks_remaining_ > 0U) {
        --attack_ticks_remaining_;
        if (attack_ticks_remaining_ == 0U) {
            resolve_attack(input);
        }
        return;
    }
    if (attack_cooldown_ticks_ > 0U) {
        --attack_cooldown_ticks_;
        return;
    }
    if (phase_ != SeaLeviathanPhase::ChargedOpening) {
        begin_attack();
    }
}

auto SeaLeviathanEncounter::update(
    float dt,
    const SeaLeviathanUpdateInput& input) noexcept
    -> SeaLeviathanUpdateResult {
    SeaLeviathanUpdateResult result {};
    if (!std::isfinite(dt) ||
        dt < 0.0F ||
        dt > 10.0F ||
        !valid_ship_local_frame(input.ship_frame) ||
        !finite_vec3(input.player_world_position)) {
        return result;
    }
    result.accepted = true;
    const auto dropped_before = dropped_event_count_;
    tick_accumulator_ += static_cast<double>(dt) * 60.0;
    const auto nearest = std::round(tick_accumulator_);
    if (std::abs(tick_accumulator_ - nearest) <= 1.0e-5) {
        tick_accumulator_ = nearest;
    }
    const auto ticks =
        static_cast<std::uint64_t>(
            std::floor(tick_accumulator_));
    tick_accumulator_ -= static_cast<double>(ticks);
    result.advanced_ticks = ticks;
    for (std::uint64_t tick = 0U; tick < ticks; ++tick) {
        ++simulation_tick_;
        update_tick(input);
    }
    result.dropped_event_count =
        dropped_event_count_ - dropped_before;
    return result;
}

auto SeaLeviathanEncounter::apply_hit(
    const SeaLeviathanHitRequest& request) noexcept
    -> SeaLeviathanHitResult {
    SeaLeviathanHitResult result {};
    if (!active()) {
        result.error = SeaLeviathanHitError::Inactive;
        return result;
    }
    if (phase_ == SeaLeviathanPhase::Defeated) {
        result.error = SeaLeviathanHitError::Defeated;
        return result;
    }
    if (!valid_part(request.part)) {
        result.error = SeaLeviathanHitError::InvalidPart;
        return result;
    }
    if (!std::isfinite(request.physical_damage) ||
        request.physical_damage < 0.0F ||
        request.physical_damage > 1'000'000.0F) {
        result.error = SeaLeviathanHitError::InvalidDamage;
        return result;
    }
    if (!std::isfinite(request.stagger_power) ||
        request.stagger_power < 0.0F ||
        request.stagger_power > 1'000'000.0F) {
        result.error = SeaLeviathanHitError::InvalidStagger;
        return result;
    }

    result.accepted = true;
    const auto stagger_available =
        std::max(0.0F, maximum_stagger_ - stagger_);
    const auto stagger_multiplier =
        phase_ == SeaLeviathanPhase::ExposedCore ? 1.25F : 1.0F;
    result.applied_stagger =
        std::min(
            stagger_available,
            request.stagger_power * stagger_multiplier);
    stagger_ += result.applied_stagger;

    if (request.part == SeaLeviathanPart::Core) {
        if (request.weapon_awakening_level < 2U) {
            result.core_deflected = true;
            push_event(
                SeaLeviathanEventKind::AstralCoreDeflected,
                request.part,
                body_anchor_local_ + glm::vec3 {0.0F, 2.4F, 0.0F});
        } else {
            const auto multiplier =
                phase_ == SeaLeviathanPhase::ExposedCore
                    ? 1.55F
                    : 0.08F;
            result.applied_health_damage =
                std::min(
                    health_,
                    request.physical_damage * multiplier);
        }
    } else if (request.part == SeaLeviathanPart::Carapace) {
        const auto local_multiplier =
            request.charged_attack ? 1.0F : 0.18F;
        result.applied_local_damage =
            std::min(
                carapace_,
                request.physical_damage * local_multiplier);
        carapace_ -= result.applied_local_damage;
        if (carapace_ <= 0.0F) {
            carapace_ = 0.0F;
            result.carapace_broken_now = true;
            push_event(
                SeaLeviathanEventKind::CarapaceBroken,
                request.part,
                body_anchor_local_ + glm::vec3 {0.0F, 2.2F, -0.4F});
            if (phase_ != SeaLeviathanPhase::ExposedCore) {
                expose_core();
                result.core_exposed_now = true;
            }
        } else if (
            phase_ == SeaLeviathanPhase::ChargedOpening &&
            request.charged_attack &&
            request.physical_damage >= 20.0F) {
            expose_core();
            result.core_exposed_now = true;
        }
    } else {
        const auto index = tentacle_index(request.part);
        if (!index.has_value()) {
            result.accepted = false;
            result.error = SeaLeviathanHitError::InvalidPart;
            return result;
        }
        auto& tentacle = tentacles_[*index];
        if (tentacle.severed) {
            result.accepted = false;
            result.error = SeaLeviathanHitError::PartAlreadySevered;
            return result;
        }
        result.applied_local_damage =
            std::min(tentacle.resistance, request.physical_damage);
        const auto remaining =
            tentacle.resistance - result.applied_local_damage;
        if (remaining <= 0.0F && !request.sectioning_attack) {
            tentacle.resistance = 1.0F;
            result.applied_local_damage =
                std::max(0.0F, result.applied_local_damage - 1.0F);
        } else {
            tentacle.resistance = std::max(0.0F, remaining);
        }
        result.applied_health_damage =
            std::min(health_, result.applied_local_damage * 0.08F);
        if (tentacle.resistance <= 0.0F &&
            request.sectioning_attack) {
            tentacle.severed = true;
            result.tentacle_severed_now = true;
            push_event(
                SeaLeviathanEventKind::TentacleSevered,
                request.part,
                kTentacleOffsets[*index]);
            if (active_attack_ ==
                    SeaLeviathanAttack::TentacleSweep &&
                attacking_tentacle_index_ == *index) {
                active_attack_ = SeaLeviathanAttack::None;
                attack_ticks_remaining_ = 0U;
                attack_cooldown_ticks_ = 90U;
            }
        }
        result.remaining_local_resistance =
            tentacle.resistance;
    }

    health_ -= result.applied_health_damage;
    if (result.applied_health_damage > 0.0F) {
        push_event(
            SeaLeviathanEventKind::MonsterDamaged,
            request.part,
            body_anchor_local_,
            result.applied_health_damage);
    }
    if (health_ <= 0.0F) {
        health_ = 0.0F;
        change_phase(SeaLeviathanPhase::Defeated);
        active_attack_ = SeaLeviathanAttack::None;
        attack_ticks_remaining_ = 0U;
        attack_cooldown_ticks_ = 0U;
        result.defeated_now = true;
        push_event(
            SeaLeviathanEventKind::Defeated,
            SeaLeviathanPart::Core,
            body_anchor_local_);
    } else if (
        phase_ != SeaLeviathanPhase::ExposedCore &&
        phase_ != SeaLeviathanPhase::ChargedOpening &&
        phase_ != SeaLeviathanPhase::GuardWindow &&
        base_combat_phase() == SeaLeviathanPhase::Frenzy) {
        change_phase(SeaLeviathanPhase::Frenzy);
        attack_cooldown_ticks_ =
            std::min(attack_cooldown_ticks_, 90U);
    }

    result.remaining_health = health_;
    if (request.part == SeaLeviathanPart::Carapace) {
        result.remaining_local_resistance = carapace_;
    }
    return result;
}

auto SeaLeviathanEncounter::render_snapshot(
    const ShipLocalFrame& frame) const noexcept
    -> std::optional<SeaLeviathanRenderSnapshot> {
    const auto body_world =
        ship_local_to_world(frame, body_anchor_local_);
    const auto core_world =
        ship_local_to_world(
            frame,
            body_anchor_local_ + glm::vec3 {0.0F, 2.4F, 0.0F});
    const auto telegraph_world =
        ship_local_to_world(frame, telegraph_local_);
    if (!body_world.has_value() ||
        !core_world.has_value() ||
        !telegraph_world.has_value()) {
        return std::nullopt;
    }

    SeaLeviathanRenderSnapshot snapshot {};
    snapshot.phase = phase_;
    snapshot.active_attack = active_attack_;
    snapshot.body_anchor_ship_local = body_anchor_local_;
    snapshot.body_anchor_world = *body_world;
    snapshot.core_world = *core_world;
    snapshot.telegraph_world = *telegraph_world;
    snapshot.health_ratio =
        maximum_health_ > 0.0F ? health_ / maximum_health_ : 0.0F;
    snapshot.carapace_ratio =
        maximum_carapace_ > 0.0F ? carapace_ / maximum_carapace_ : 0.0F;
    snapshot.stagger_ratio =
        maximum_stagger_ > 0.0F ? stagger_ / maximum_stagger_ : 0.0F;
    snapshot.core_exposure_ratio =
        phase_ == SeaLeviathanPhase::ExposedCore
            ? static_cast<float>(phase_ticks_remaining_) / 360.0F
            : 0.0F;
    snapshot.core_exposed =
        phase_ == SeaLeviathanPhase::ExposedCore;
    snapshot.active = active();
    for (std::size_t index = 0U;
         index < tentacles_.size();
         ++index) {
        const auto anchor_world =
            ship_local_to_world(frame, kTentacleOffsets[index]);
        if (!anchor_world.has_value()) {
            return std::nullopt;
        }
        const auto& state = tentacles_[index];
        snapshot.tentacles[index] = {
            tentacle_part(index),
            kTentacleOffsets[index],
            *anchor_world,
            state.maximum_resistance > 0.0F
                ? state.resistance / state.maximum_resistance
                : 0.0F,
            state.severed,
            active_attack_ == SeaLeviathanAttack::TentacleSweep &&
                attacking_tentacle_index_ == index,
        };
    }
    return snapshot;
}

auto SeaLeviathanEncounter::combat_snapshot(
    const ShipLocalFrame& frame) const noexcept
    -> std::optional<SeaLeviathanCombatSnapshot> {
    const auto render = render_snapshot(frame);
    if (!render.has_value()) {
        return std::nullopt;
    }
    SeaLeviathanCombatSnapshot snapshot {};
    snapshot.hit_volume_count = snapshot.hit_volumes.size();
    snapshot.hit_volumes[0] = {
        SeaLeviathanPart::Core,
        render->core_world,
        1.15F,
        render->core_exposed && phase_ != SeaLeviathanPhase::Defeated,
        false,
    };
    const auto carapace_world =
        ship_local_to_world(
            frame,
            body_anchor_local_ + glm::vec3 {0.0F, 2.2F, -0.4F});
    if (!carapace_world.has_value()) {
        return std::nullopt;
    }
    snapshot.hit_volumes[1] = {
        SeaLeviathanPart::Carapace,
        *carapace_world,
        2.25F,
        phase_ != SeaLeviathanPhase::Defeated,
        false,
    };
    for (std::size_t index = 0U;
         index < tentacles_.size();
         ++index) {
        snapshot.hit_volumes[index + 2U] = {
            tentacle_part(index),
            render->tentacles[index].anchor_world,
            0.9F,
            !tentacles_[index].severed &&
                phase_ != SeaLeviathanPhase::Defeated,
            true,
        };
    }
    snapshot.weight = EntityWeight::Boss;
    snapshot.faction = Faction::Hostile;
    snapshot.can_damage_ship = false;
    snapshot.can_damage_allies = false;
    return snapshot;
}

auto SeaLeviathanEncounter::consume_events(
    std::span<SeaLeviathanEvent> output) noexcept
    -> std::size_t {
    const auto count = std::min(output.size(), event_count_);
    std::copy_n(events_.begin(), count, output.begin());
    std::move(
        events_.begin() + static_cast<std::ptrdiff_t>(count),
        events_.begin() + static_cast<std::ptrdiff_t>(event_count_),
        events_.begin());
    event_count_ -= count;
    return count;
}

auto SeaLeviathanEncounter::phase() const noexcept
    -> SeaLeviathanPhase {
    return phase_;
}

auto SeaLeviathanEncounter::health() const noexcept -> float {
    return health_;
}

auto SeaLeviathanEncounter::maximum_health() const noexcept -> float {
    return maximum_health_;
}

auto SeaLeviathanEncounter::active() const noexcept -> bool {
    return phase_ != SeaLeviathanPhase::Dormant;
}

void SeaLeviathanEncounter::reset() noexcept {
    phase_ = SeaLeviathanPhase::Dormant;
    active_attack_ = SeaLeviathanAttack::None;
    body_anchor_local_ = glm::vec3 {0.0F, -1.5F, 7.0F};
    telegraph_local_ = {};
    health_ = 0.0F;
    maximum_health_ = 0.0F;
    carapace_ = 0.0F;
    stagger_ = 0.0F;
    tentacles_ = {};
    phase_ticks_remaining_ = 0U;
    attack_ticks_remaining_ = 0U;
    attack_cooldown_ticks_ = 0U;
    attack_sequence_ = 0U;
    attacking_tentacle_index_ = 0U;
    simulation_tick_ = 0U;
    tick_accumulator_ = 0.0;
    events_ = {};
    event_count_ = 0U;
    dropped_event_count_ = 0U;
}

} // namespace valcraft
