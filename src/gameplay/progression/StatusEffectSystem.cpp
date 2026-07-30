#include "gameplay/progression/StatusEffectSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

inline constexpr double kStatusEffectTicksPerSecond = 60.0;
inline constexpr double kStatusEffectTickSnapTolerance = 1.0e-5;

static_assert(
    kStatusEffectMaximumDurationTicks ==
    static_cast<std::uint64_t>(
        kStatusEffectMaximumDurationSeconds *
        kStatusEffectTicksPerSecond));

[[nodiscard]] auto valid_kind(StatusEffectKind kind) noexcept -> bool {
    switch (kind) {
    case StatusEffectKind::DamageReduction:
    case StatusEffectKind::KnockbackResistance:
    case StatusEffectKind::FrontalProjectileReduction:
    case StatusEffectKind::MovementSpeedBonus:
    case StatusEffectKind::RecoverySpeedBonus:
    case StatusEffectKind::Slow:
    case StatusEffectKind::Shield:
    case StatusEffectKind::FirstAbsorption:
        return true;
    }
    return false;
}

[[nodiscard]] auto percentage_kind(StatusEffectKind kind) noexcept -> bool {
    switch (kind) {
    case StatusEffectKind::DamageReduction:
    case StatusEffectKind::KnockbackResistance:
    case StatusEffectKind::FrontalProjectileReduction:
    case StatusEffectKind::Slow:
        return true;
    case StatusEffectKind::MovementSpeedBonus:
    case StatusEffectKind::RecoverySpeedBonus:
    case StatusEffectKind::Shield:
    case StatusEffectKind::FirstAbsorption:
        return false;
    }
    return false;
}

[[nodiscard]] auto combine_reduction(
    double retained_fraction,
    float maximum_reduction) noexcept -> float {
    return std::clamp(
        static_cast<float>(1.0 - retained_fraction),
        0.0F,
        maximum_reduction);
}

} // namespace

auto StatusEffectAggregate::damage_multiplier(
    bool frontal_projectile) const noexcept -> float {
    auto multiplier = 1.0F - damage_reduction;
    if (frontal_projectile) {
        multiplier *= 1.0F - frontal_projectile_reduction;
    }
    return std::clamp(
        multiplier,
        1.0F - kMaximumDamageReduction,
        1.0F);
}

auto StatusEffectAggregate::knockback_multiplier() const noexcept -> float {
    return std::clamp(1.0F - knockback_resistance, 0.0F, 1.0F);
}

auto StatusEffectAggregate::movement_speed_multiplier() const noexcept
    -> float {
    return std::max(
        0.0F,
        (1.0F + movement_speed_bonus) * (1.0F - slow));
}

auto StatusEffectAggregate::recovery_speed_multiplier() const noexcept
    -> float {
    return 1.0F + recovery_speed_bonus;
}

auto StatusEffectSystem::find_entry(
    StatusEffectTargetId target_id,
    StatusEffectKind kind,
    StatusEffectStackTag stack_tag) noexcept -> Entry* {
    for (auto& entry : entries_) {
        if (entry.active &&
            entry.target_id == target_id &&
            entry.kind == kind &&
            entry.stack_tag == stack_tag) {
            return &entry;
        }
    }
    return nullptr;
}

auto StatusEffectSystem::find_entry(
    StatusEffectTargetId target_id,
    StatusEffectKind kind,
    StatusEffectStackTag stack_tag) const noexcept -> const Entry* {
    for (const auto& entry : entries_) {
        if (entry.active &&
            entry.target_id == target_id &&
            entry.kind == kind &&
            entry.stack_tag == stack_tag) {
            return &entry;
        }
    }
    return nullptr;
}

auto StatusEffectSystem::find_free_entry() noexcept -> Entry* {
    for (auto& entry : entries_) {
        if (!entry.active) {
            return &entry;
        }
    }
    return nullptr;
}

auto StatusEffectSystem::validate(
    const StatusEffectSpec& spec) const noexcept
    -> StatusEffectApplyError {
    if (spec.target_id == 0U) {
        return StatusEffectApplyError::InvalidTarget;
    }
    if (spec.stack_tag == 0U) {
        return StatusEffectApplyError::InvalidStackTag;
    }
    if (!valid_kind(spec.kind)) {
        return StatusEffectApplyError::InvalidKind;
    }
    if (!std::isfinite(spec.value) || spec.value <= 0.0F) {
        return StatusEffectApplyError::InvalidValue;
    }
    if (percentage_kind(spec.kind) && spec.value > 1.0F) {
        return StatusEffectApplyError::InvalidValue;
    }
    if (!std::isfinite(spec.duration_seconds) ||
        spec.duration_seconds <= 0.0F ||
        spec.duration_seconds > kStatusEffectMaximumDurationSeconds) {
        return StatusEffectApplyError::InvalidDuration;
    }
    return StatusEffectApplyError::None;
}

auto StatusEffectSystem::duration_ticks(float seconds) noexcept
    -> std::uint64_t {
    const auto scaled =
        static_cast<double>(seconds) *
        kStatusEffectTicksPerSecond;
    const auto nearest = std::round(scaled);
    const auto rounded =
        std::abs(scaled - nearest) <=
                kStatusEffectTickSnapTolerance
            ? nearest
            : std::ceil(scaled);
    return std::max<std::uint64_t>(
        1U,
        static_cast<std::uint64_t>(rounded));
}

auto StatusEffectSystem::apply(
    const StatusEffectSpec& spec) noexcept -> StatusEffectApplyResult {
    StatusEffectApplyResult result {};
    result.error = validate(spec);
    if (result.error != StatusEffectApplyError::None) {
        return result;
    }

    const auto ticks = duration_ticks(spec.duration_seconds);
    const auto normalized_value =
        spec.kind == StatusEffectKind::FirstAbsorption
            ? 1.0F
            : spec.value;
    if (auto* existing =
            find_entry(spec.target_id, spec.kind, spec.stack_tag);
        existing != nullptr) {
        existing->value =
            std::max(existing->value, normalized_value);
        existing->remaining_ticks =
            std::max(existing->remaining_ticks, ticks);
        result.applied = true;
        result.refreshed = true;
        result.effective_value = existing->value;
        result.remaining_seconds =
            static_cast<double>(existing->remaining_ticks) *
            (1.0 / kStatusEffectTicksPerSecond);
        return result;
    }

    if (active_effect_count(spec.target_id) >=
        kMaximumStatusEffectsPerTarget) {
        result.error =
            StatusEffectApplyError::TargetCapacityReached;
        return result;
    }

    auto* entry = find_free_entry();
    if (entry == nullptr) {
        result.error =
            StatusEffectApplyError::GlobalCapacityReached;
        return result;
    }

    if (next_sequence_ ==
        std::numeric_limits<std::uint64_t>::max()) {
        std::array<Entry*, kMaximumStatusEffects>
            ordered_entries {};
        auto ordered_count =
            std::size_t {0U};
        for (auto& active_entry : entries_) {
            if (active_entry.active) {
                ordered_entries[ordered_count++] =
                    &active_entry;
            }
        }
        std::sort(
            ordered_entries.begin(),
            ordered_entries.begin() +
                static_cast<std::ptrdiff_t>(
                    ordered_count),
            [](const Entry* left,
               const Entry* right) {
                if (left->sequence !=
                    right->sequence) {
                    return left->sequence <
                           right->sequence;
                }
                return left < right;
            });
        for (std::size_t index = 0U;
             index < ordered_count;
             ++index) {
            ordered_entries[index]->sequence =
                static_cast<std::uint64_t>(
                    index + 1U);
        }
        // Je compacte uniquement à l'épuisement du compteur ; l'ordre relatif
        // reste identique et aucune future insertion ne réutilise un numéro.
        next_sequence_ =
            static_cast<std::uint64_t>(
                ordered_count + 1U);
    }

    *entry = {
        spec.target_id,
        spec.stack_tag,
        spec.kind,
        normalized_value,
        ticks,
        next_sequence_,
        true,
    };
    ++next_sequence_;

    result.applied = true;
    result.inserted = true;
    result.effective_value = normalized_value;
    result.remaining_seconds =
        static_cast<double>(ticks) *
        (1.0 / kStatusEffectTicksPerSecond);
    return result;
}

auto StatusEffectSystem::apply_iron_guard(
    const IronGuardStatusRequest& request) noexcept
    -> IronGuardStatusResult {
    std::array<StatusEffectSpec, 4U> specs {};
    auto count = std::size_t {0U};

    const auto add = [&](StatusEffectKind kind, float value) {
        if (value == 0.0F) {
            return;
        }
        specs[count++] = {
            request.target_id,
            request.stack_tag,
            kind,
            value,
            request.duration_seconds,
        };
    };

    add(
        StatusEffectKind::DamageReduction,
        request.damage_reduction);
    add(
        StatusEffectKind::KnockbackResistance,
        request.knockback_resistance);
    add(
        StatusEffectKind::FrontalProjectileReduction,
        request.frontal_projectile_reduction);
    if (request.grants_first_absorption) {
        add(StatusEffectKind::FirstAbsorption, 1.0F);
    }

    IronGuardStatusResult result {};
    if (count == 0U) {
        result.error = StatusEffectApplyError::InvalidValue;
        return result;
    }

    // Je prépare une copie complète pour garantir qu'une garde composée ne
    // laisse jamais un demi-effet si sa validation ou sa capacité échoue.
    auto staged = *this;
    for (std::size_t index = 0U; index < count; ++index) {
        const auto application = staged.apply(specs[index]);
        if (!application.applied) {
            result.error = application.error;
            return result;
        }
        result.inserted_effect_count +=
            application.inserted ? 1U : 0U;
        result.refreshed_effect_count +=
            application.refreshed ? 1U : 0U;
    }

    for (auto& entry : staged.entries_) {
        if (!entry.active ||
            entry.target_id != request.target_id ||
            entry.stack_tag != request.stack_tag) {
            continue;
        }
        const auto still_requested =
            std::any_of(
                specs.begin(),
                specs.begin() +
                    static_cast<std::ptrdiff_t>(count),
                [&entry](
                    const StatusEffectSpec& spec) {
                    return spec.kind == entry.kind;
                });
        if (!still_requested) {
            // Je retire les composantes absentes de la nouvelle activation :
            // un bouclier ôté ou une maîtrise inactive ne laisse aucun reliquat.
            entry = {};
        }
    }

    *this = staged;
    result.applied = true;
    return result;
}

auto StatusEffectSystem::update(float dt) noexcept
    -> StatusEffectUpdateResult {
    StatusEffectUpdateResult result {};
    if (!std::isfinite(dt) || dt < 0.0F) {
        return result;
    }
    result.accepted = true;
    if (dt == 0.0F) {
        return result;
    }

    const auto total_ticks =
        tick_accumulator_ +
        static_cast<double>(dt) *
            kStatusEffectTicksPerSecond;
    const auto nearest_tick = std::round(total_ticks);
    // Je ramène les erreurs usuelles du float au tick entier attendu afin
    // qu'une seconde entière et soixante pas produisent le même résultat.
    const auto snapped =
        std::abs(total_ticks - nearest_tick) <=
        kStatusEffectTickSnapTolerance;
    const auto tick_value =
        snapped
            ? nearest_tick
            : std::floor(total_ticks);
    if (tick_value <= 0.0) {
        tick_accumulator_ = total_ticks;
        return result;
    }

    const auto maximum_tick_value =
        static_cast<double>(
            std::numeric_limits<std::uint64_t>::max());
    if (tick_value >= maximum_tick_value) {
        result.advanced_ticks =
            std::numeric_limits<std::uint64_t>::max();
        tick_accumulator_ = 0.0;
    } else {
        result.advanced_ticks =
            static_cast<std::uint64_t>(tick_value);
        tick_accumulator_ =
            snapped
                ? 0.0
                : total_ticks -
                    static_cast<double>(
                        result.advanced_ticks);
        if (tick_accumulator_ < 0.0) {
            tick_accumulator_ = 0.0;
        } else if (tick_accumulator_ >= 1.0) {
            tick_accumulator_ =
                std::fmod(tick_accumulator_, 1.0);
        }
    }

    for (auto& entry : entries_) {
        if (!entry.active) {
            continue;
        }
        if (result.advanced_ticks >= entry.remaining_ticks) {
            entry = {};
            ++result.expired_effect_count;
        } else {
            entry.remaining_ticks -= result.advanced_ticks;
        }
    }
    return result;
}

auto StatusEffectSystem::aggregate(
    StatusEffectTargetId target_id,
    float maximum_health) const noexcept -> StatusEffectAggregate {
    StatusEffectAggregate result {};
    if (target_id == 0U) {
        return result;
    }

    auto damage_retained = 1.0;
    auto knockback_retained = 1.0;
    auto projectile_retained = 1.0;
    auto slow_retained = 1.0;
    auto movement_bonus = 0.0;
    auto recovery_bonus = 0.0;
    auto shield = 0.0;

    for (const auto& entry : entries_) {
        if (!entry.active || entry.target_id != target_id) {
            continue;
        }
        const auto value =
            static_cast<double>(entry.value);
        switch (entry.kind) {
        case StatusEffectKind::DamageReduction:
            damage_retained *= 1.0 - value;
            break;
        case StatusEffectKind::KnockbackResistance:
            knockback_retained *= 1.0 - value;
            break;
        case StatusEffectKind::FrontalProjectileReduction:
            projectile_retained *= 1.0 - value;
            break;
        case StatusEffectKind::MovementSpeedBonus:
            movement_bonus += value;
            break;
        case StatusEffectKind::RecoverySpeedBonus:
            recovery_bonus += value;
            break;
        case StatusEffectKind::Slow:
            slow_retained *= 1.0 - value;
            break;
        case StatusEffectKind::Shield:
            shield += value;
            break;
        case StatusEffectKind::FirstAbsorption:
            result.first_absorption_available = true;
            break;
        }
    }

    result.damage_reduction =
        combine_reduction(
            damage_retained,
            kMaximumDamageReduction);
    result.knockback_resistance =
        combine_reduction(
            knockback_retained,
            kMaximumKnockbackResistance);
    result.frontal_projectile_reduction =
        combine_reduction(
            projectile_retained,
            kMaximumDamageReduction);
    result.movement_speed_bonus =
        std::clamp(
            static_cast<float>(movement_bonus),
            0.0F,
            kMaximumTemporaryMovementSpeedBonus);
    result.recovery_speed_bonus =
        std::clamp(
            static_cast<float>(recovery_bonus),
            0.0F,
            kMaximumTemporaryRecoverySpeedBonus);
    result.slow =
        combine_reduction(slow_retained, kMaximumSlow);

    if (std::isfinite(maximum_health) &&
        maximum_health > 0.0F) {
        result.shield_health =
            std::clamp(
                static_cast<float>(shield),
                0.0F,
                maximum_health *
                    kMaximumShieldHealthRatio);
    }
    return result;
}

auto StatusEffectSystem::consume_first_absorption(
    StatusEffectTargetId target_id,
    std::optional<StatusEffectStackTag> stack_tag) noexcept
    -> FirstAbsorptionConsumeResult {
    FirstAbsorptionConsumeResult result {};
    if (target_id == 0U ||
        (stack_tag.has_value() && *stack_tag == 0U)) {
        return result;
    }

    Entry* selected = nullptr;
    for (auto& entry : entries_) {
        if (!entry.active ||
            entry.target_id != target_id ||
            entry.kind != StatusEffectKind::FirstAbsorption ||
            (stack_tag.has_value() &&
             entry.stack_tag != *stack_tag)) {
            continue;
        }
        if (selected == nullptr ||
            entry.sequence < selected->sequence) {
            selected = &entry;
        }
    }

    if (selected == nullptr) {
        return result;
    }
    result.consumed = true;
    result.stack_tag = selected->stack_tag;
    *selected = {};
    return result;
}

auto StatusEffectSystem::absorb_first_hit(
    StatusEffectTargetId target_id,
    float incoming_damage,
    std::optional<StatusEffectStackTag> stack_tag) noexcept
    -> FirstHitAbsorptionResult {
    FirstHitAbsorptionResult result {};
    result.requested_damage = incoming_damage;
    if (target_id == 0U ||
        (stack_tag.has_value() && *stack_tag == 0U) ||
        !std::isfinite(incoming_damage) ||
        incoming_damage < 0.0F) {
        return result;
    }

    result.accepted = true;
    result.remaining_damage = incoming_damage;
    if (incoming_damage == 0.0F) {
        return result;
    }

    const auto consumed =
        consume_first_absorption(
            target_id,
            stack_tag);
    if (!consumed.consumed) {
        return result;
    }

    result.absorbed = true;
    result.stack_tag = consumed.stack_tag;
    result.absorbed_damage = incoming_damage;
    result.remaining_damage = 0.0F;
    return result;
}

auto StatusEffectSystem::absorb_with_shield(
    StatusEffectTargetId target_id,
    float incoming_damage,
    float maximum_health) noexcept -> ShieldAbsorptionResult {
    ShieldAbsorptionResult result {};
    result.requested_damage = incoming_damage;
    if (target_id == 0U ||
        !std::isfinite(incoming_damage) ||
        incoming_damage < 0.0F ||
        !std::isfinite(maximum_health) ||
        maximum_health <= 0.0F) {
        return result;
    }
    result.accepted = true;

    const auto shield_cap =
        maximum_health * kMaximumShieldHealthRatio;
    auto total_shield = 0.0;
    for (const auto& entry : entries_) {
        if (entry.active &&
            entry.target_id == target_id &&
            entry.kind == StatusEffectKind::Shield) {
            total_shield += static_cast<double>(entry.value);
        }
    }

    // Je calcule en double pour que plusieurs grandes valeurs finies ne
    // débordent jamais avant l'application du plafond de PV.
    auto excess =
        std::max(
            0.0,
            total_shield - static_cast<double>(shield_cap));
    while (excess > 0.0) {
        Entry* newest = nullptr;
        for (auto& entry : entries_) {
            if (entry.active &&
                entry.target_id == target_id &&
                entry.kind == StatusEffectKind::Shield &&
                entry.value > 0.0F &&
                (newest == nullptr ||
                 entry.sequence > newest->sequence)) {
                newest = &entry;
            }
        }
        if (newest == nullptr) {
            break;
        }
        const auto removed =
            std::min(
                excess,
                static_cast<double>(newest->value));
        newest->value -= static_cast<float>(removed);
        excess -= removed;
        total_shield -= removed;
        if (newest->value <= 0.0F) {
            *newest = {};
        }
    }

    auto remaining_damage =
        std::min(
            static_cast<double>(incoming_damage),
            total_shield);
    result.absorbed_damage =
        static_cast<float>(remaining_damage);
    while (remaining_damage > 0.0) {
        Entry* oldest = nullptr;
        for (auto& entry : entries_) {
            if (entry.active &&
                entry.target_id == target_id &&
                entry.kind == StatusEffectKind::Shield &&
                entry.value > 0.0F &&
                (oldest == nullptr ||
                 entry.sequence < oldest->sequence)) {
                oldest = &entry;
            }
        }
        if (oldest == nullptr) {
            break;
        }
        const auto consumed =
            std::min(
                remaining_damage,
                static_cast<double>(oldest->value));
        oldest->value -= static_cast<float>(consumed);
        remaining_damage -= consumed;
        total_shield -= consumed;
        if (oldest->value <= 0.0F) {
            *oldest = {};
        }
    }
    result.remaining_shield =
        static_cast<float>(
            std::clamp(
                total_shield,
                0.0,
                static_cast<double>(shield_cap)));
    return result;
}

auto StatusEffectSystem::has_effect(
    StatusEffectTargetId target_id,
    StatusEffectKind kind,
    StatusEffectStackTag stack_tag) const noexcept -> bool {
    if (target_id == 0U ||
        stack_tag == 0U ||
        !valid_kind(kind)) {
        return false;
    }
    return find_entry(target_id, kind, stack_tag) != nullptr;
}

auto StatusEffectSystem::remaining_seconds(
    StatusEffectTargetId target_id,
    StatusEffectKind kind,
    StatusEffectStackTag stack_tag) const noexcept
    -> std::optional<double> {
    if (target_id == 0U ||
        stack_tag == 0U ||
        !valid_kind(kind)) {
        return std::nullopt;
    }
    const auto* entry =
        find_entry(target_id, kind, stack_tag);
    if (entry == nullptr) {
        return std::nullopt;
    }
    return
        static_cast<double>(entry->remaining_ticks) *
        (1.0 / kStatusEffectTicksPerSecond);
}

auto StatusEffectSystem::active_effect_count() const noexcept
    -> std::size_t {
    return static_cast<std::size_t>(
        std::count_if(
            entries_.begin(),
            entries_.end(),
            [](const Entry& entry) {
                return entry.active;
            }));
}

auto StatusEffectSystem::active_effect_count(
    StatusEffectTargetId target_id) const noexcept -> std::size_t {
    if (target_id == 0U) {
        return 0U;
    }
    return static_cast<std::size_t>(
        std::count_if(
            entries_.begin(),
            entries_.end(),
            [target_id](const Entry& entry) {
                return entry.active &&
                    entry.target_id == target_id;
            }));
}

auto StatusEffectSystem::snapshot() const noexcept
    -> StatusEffectSystemSnapshot {
    StatusEffectSystemSnapshot result {};
    for (std::size_t index = 0U;
         index < entries_.size();
         ++index) {
        const auto& entry = entries_[index];
        result.entries[index] = {
            entry.target_id,
            entry.stack_tag,
            entry.kind,
            entry.value,
            entry.remaining_ticks,
            entry.sequence,
            entry.active,
        };
    }
    result.fractional_tick_accumulator =
        tick_accumulator_;
    result.next_sequence = next_sequence_;
    return result;
}

auto StatusEffectSystem::load_state(
    const StatusEffectSystemSnapshot& requested) noexcept
    -> StatusEffectLoadResult {
    StatusEffectSystem staged {};
    StatusEffectLoadResult result {};
    auto maximum_sequence = std::uint64_t {0U};

    for (std::size_t index = 0U;
         index < requested.entries.size();
         ++index) {
        const auto& source =
            requested.entries[index];
        if (!source.active) {
            if (source != StatusEffectSnapshotEntry {}) {
                result.sanitized = true;
            }
            continue;
        }

        const auto valid_value =
            std::isfinite(source.value) &&
            source.value > 0.0F &&
            (!percentage_kind(source.kind) ||
             source.value <= 1.0F);
        const auto duplicate =
            valid_kind(source.kind) &&
            staged.find_entry(
                source.target_id,
                source.kind,
                source.stack_tag) != nullptr;
        const auto duplicate_sequence =
            source.sequence != 0U &&
            std::any_of(
                staged.entries_.begin(),
                staged.entries_.end(),
                [&source](
                    const Entry& entry) {
                    return entry.active &&
                           entry.sequence ==
                               source.sequence;
                });
        if (source.target_id == 0U ||
            source.stack_tag == 0U ||
            !valid_kind(source.kind) ||
            !valid_value ||
            source.remaining_ticks == 0U ||
            source.remaining_ticks >
                kStatusEffectMaximumDurationTicks ||
            source.sequence == 0U ||
            duplicate ||
            duplicate_sequence ||
            staged.active_effect_count(
                source.target_id) >=
                kMaximumStatusEffectsPerTarget) {
            ++result.discarded_effect_count;
            result.sanitized = true;
            continue;
        }

        auto normalized = source;
        if (normalized.kind ==
                StatusEffectKind::FirstAbsorption &&
            normalized.value != 1.0F) {
            normalized.value = 1.0F;
            result.sanitized = true;
        }
        staged.entries_[index] = {
            normalized.target_id,
            normalized.stack_tag,
            normalized.kind,
            normalized.value,
            normalized.remaining_ticks,
            normalized.sequence,
            true,
        };
        maximum_sequence =
            std::max(
                maximum_sequence,
                normalized.sequence);
        ++result.restored_effect_count;
    }

    if (std::isfinite(
            requested.fractional_tick_accumulator) &&
        requested.fractional_tick_accumulator >= 0.0 &&
        requested.fractional_tick_accumulator < 1.0) {
        staged.tick_accumulator_ =
            requested.fractional_tick_accumulator;
    } else {
        result.sanitized = true;
    }

    const auto minimum_next_sequence =
        maximum_sequence ==
                std::numeric_limits<std::uint64_t>::max()
            ? maximum_sequence
            : maximum_sequence + 1U;
    if (requested.next_sequence == 0U ||
        requested.next_sequence <
            minimum_next_sequence) {
        staged.next_sequence_ =
            std::max<std::uint64_t>(
                minimum_next_sequence,
                1U);
        result.sanitized = true;
    } else {
        staged.next_sequence_ =
            requested.next_sequence;
    }

    *this = staged;
    return result;
}

void StatusEffectSystem::clear_target(
    StatusEffectTargetId target_id) noexcept {
    if (target_id == 0U) {
        return;
    }
    for (auto& entry : entries_) {
        if (entry.active && entry.target_id == target_id) {
            entry = {};
        }
    }
}

void StatusEffectSystem::clear_stack(
    StatusEffectTargetId target_id,
    StatusEffectStackTag stack_tag) noexcept {
    if (target_id == 0U ||
        stack_tag == 0U) {
        return;
    }
    for (auto& entry : entries_) {
        if (entry.active &&
            entry.target_id == target_id &&
            entry.stack_tag == stack_tag) {
            entry = {};
        }
    }
}

auto StatusEffectSystem::clear_kind(
    StatusEffectTargetId target_id,
    StatusEffectKind kind) noexcept -> std::size_t {
    if (target_id == 0U ||
        static_cast<std::uint8_t>(kind) >
            static_cast<std::uint8_t>(
                StatusEffectKind::FirstAbsorption)) {
        return 0U;
    }
    auto cleared = std::size_t {0U};
    for (auto& entry : entries_) {
        if (!entry.active ||
            entry.target_id != target_id ||
            entry.kind != kind) {
            continue;
        }
        entry = {};
        ++cleared;
    }
    return cleared;
}

void StatusEffectSystem::clear() noexcept {
    entries_ = {};
    tick_accumulator_ = 0.0;
    next_sequence_ = 1U;
}

} // namespace valcraft
