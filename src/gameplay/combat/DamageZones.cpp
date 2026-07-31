#include "gameplay/combat/DamageZones.h"

#include <algorithm>
#include <cmath>

namespace valcraft {

namespace {

[[nodiscard]] auto valid_kind(DamageZoneKind kind) noexcept -> bool {
    switch (kind) {
    case DamageZoneKind::Torso:
    case DamageZoneKind::Head:
    case DamageZoneKind::LeftArm:
    case DamageZoneKind::RightArm:
    case DamageZoneKind::LeftLeg:
    case DamageZoneKind::RightLeg:
    case DamageZoneKind::Horn:
    case DamageZoneKind::Tentacle:
        return true;
    }
    return false;
}

[[nodiscard]] auto valid_multiplier(float value) noexcept -> bool {
    return std::isfinite(value) &&
           value >= 0.0F &&
           value <= kMaximumDamageZoneMultiplier;
}

[[nodiscard]] auto scaled_value(
    float base_value,
    float multiplier) noexcept -> float {
    const auto scaled =
        static_cast<double>(base_value) *
        static_cast<double>(multiplier);
    return static_cast<float>(
        std::clamp(
            scaled,
            0.0,
            static_cast<double>(
                kMaximumDamageZoneInput *
                kMaximumDamageZoneMultiplier)));
}

} // namespace

auto DamageZones::configure(
    std::span<const DamageZoneDefinition> definitions) noexcept
    -> DamageZonesConfigureResult {
    DamageZonesConfigureResult result {};
    if (definitions.size() > kMaximumDamageZones) {
        result.error = DamageZonesConfigureError::CapacityExceeded;
        result.failing_definition_index = definitions.size();
        return result;
    }

    std::array<Entry, kMaximumDamageZones> staged {};
    for (std::size_t index = 0U;
         index < definitions.size();
         ++index) {
        const auto& definition = definitions[index];
        result.failing_definition_index = index;
        if (definition.id == 0U) {
            result.error = DamageZonesConfigureError::InvalidId;
            return result;
        }
        if (!valid_kind(definition.kind)) {
            result.error = DamageZonesConfigureError::InvalidKind;
            return result;
        }
        if (!std::isfinite(definition.maximum_local_resistance) ||
            definition.maximum_local_resistance <= 0.0F ||
            definition.maximum_local_resistance >
                kMaximumDamageZoneResistance) {
            result.error =
                DamageZonesConfigureError::InvalidResistance;
            return result;
        }
        if (!valid_multiplier(
                definition.health_damage_multiplier) ||
            !valid_multiplier(
                definition.local_damage_multiplier) ||
            !valid_multiplier(
                definition.stagger_multiplier)) {
            result.error =
                DamageZonesConfigureError::InvalidMultiplier;
            return result;
        }
        for (std::size_t previous = 0U;
             previous < index;
             ++previous) {
            if (staged[previous].definition.id ==
                definition.id) {
                result.error =
                    DamageZonesConfigureError::DuplicateId;
                return result;
            }
        }

        staged[index] = {
            definition,
            definition.maximum_local_resistance,
            true,
        };
    }

    // Je remplace la configuration en une seule fois pour conserver l'ancien
    // état intact si une définition du nouveau lot est invalide.
    entries_ = staged;
    zone_count_ = definitions.size();
    result.configured = true;
    result.configured_zone_count = zone_count_;
    result.failing_definition_index = definitions.size();
    return result;
}

auto DamageZones::find_entry(
    DamageZoneId zone_id) noexcept -> Entry* {
    for (auto& entry : entries_) {
        if (entry.active &&
            entry.definition.id == zone_id) {
            return &entry;
        }
    }
    return nullptr;
}

auto DamageZones::find_entry(
    DamageZoneId zone_id) const noexcept -> const Entry* {
    for (const auto& entry : entries_) {
        if (entry.active &&
            entry.definition.id == zone_id) {
            return &entry;
        }
    }
    return nullptr;
}

auto DamageZones::apply_hit(
    const DamageZoneHit& hit) noexcept -> DamageZoneHitResult {
    DamageZoneHitResult result {};
    result.zone_id = hit.zone_id;
    if (hit.zone_id == 0U) {
        result.error = DamageZoneHitError::InvalidId;
        return result;
    }
    if (!std::isfinite(hit.base_health_damage) ||
        hit.base_health_damage < 0.0F ||
        hit.base_health_damage > kMaximumDamageZoneInput) {
        result.error = DamageZoneHitError::InvalidDamage;
        return result;
    }
    if (!std::isfinite(hit.base_stagger_damage) ||
        hit.base_stagger_damage < 0.0F ||
        hit.base_stagger_damage > kMaximumDamageZoneInput) {
        result.error = DamageZoneHitError::InvalidStagger;
        return result;
    }

    auto* entry = find_entry(hit.zone_id);
    if (entry == nullptr) {
        result.error = DamageZoneHitError::UnknownZone;
        return result;
    }

    const auto& definition = entry->definition;
    result.accepted = true;
    result.kind = definition.kind;
    result.previous_local_resistance =
        entry->remaining_local_resistance;
    result.previous_condition =
        damage_zone_condition(
            entry->remaining_local_resistance,
            definition.maximum_local_resistance);
    result.health_damage =
        scaled_value(
            hit.base_health_damage,
            definition.health_damage_multiplier);
    result.stagger_damage =
        scaled_value(
            hit.base_stagger_damage,
            definition.stagger_multiplier);

    const auto requested_local_damage =
        scaled_value(
            hit.base_health_damage,
            definition.local_damage_multiplier);
    result.local_resistance_damage =
        std::min(
            entry->remaining_local_resistance,
            requested_local_damage);
    entry->remaining_local_resistance =
        std::max(
            0.0F,
            entry->remaining_local_resistance -
                result.local_resistance_damage);
    result.remaining_local_resistance =
        entry->remaining_local_resistance;
    result.condition =
        damage_zone_condition(
            entry->remaining_local_resistance,
            definition.maximum_local_resistance);
    result.depleted_now =
        result.previous_condition !=
            DamageZoneCondition::Depleted &&
        result.condition ==
            DamageZoneCondition::Depleted;
    return result;
}

auto DamageZones::zone(
    DamageZoneId zone_id) const noexcept
    -> std::optional<DamageZoneView> {
    const auto* entry = find_entry(zone_id);
    if (entry == nullptr) {
        return std::nullopt;
    }
    return DamageZoneView {
        entry->definition,
        entry->remaining_local_resistance,
        damage_zone_condition(
            entry->remaining_local_resistance,
            entry->definition.maximum_local_resistance),
    };
}

auto DamageZones::zone_count() const noexcept -> std::size_t {
    return zone_count_;
}

void DamageZones::reset() noexcept {
    for (auto& entry : entries_) {
        if (entry.active) {
            entry.remaining_local_resistance =
                entry.definition.maximum_local_resistance;
        }
    }
}

void DamageZones::clear() noexcept {
    entries_ = {};
    zone_count_ = 0U;
}

} // namespace valcraft
