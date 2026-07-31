#include "gameplay/combat/DismembermentSystem.h"

#include <cmath>

namespace valcraft {

namespace {

[[nodiscard]] auto valid_zone_condition(
    DamageZoneCondition condition) noexcept -> bool {
    switch (condition) {
    case DamageZoneCondition::Intact:
    case DamageZoneCondition::Wounded:
    case DamageZoneCondition::Depleted:
        return true;
    }
    return false;
}

[[nodiscard]] auto valid_gore_mode(
    GorePresentationMode mode) noexcept -> bool {
    switch (mode) {
    case GorePresentationMode::Full:
    case GorePresentationMode::Reduced:
    case GorePresentationMode::Disabled:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr auto visual_action_for(
    GorePresentationMode mode) noexcept
    -> DismembermentVisualAction {
    switch (mode) {
    case GorePresentationMode::Full:
        return DismembermentVisualAction::DetachWithBlood;
    case GorePresentationMode::Reduced:
        return DismembermentVisualAction::HideWithMutedEffect;
    case GorePresentationMode::Disabled:
    default:
        return DismembermentVisualAction::HideWithDarkEffect;
    }
}

} // namespace

auto DismembermentSystem::configure(
    std::span<const DismembermentPartDefinition>
        definitions) noexcept
    -> DismembermentConfigureResult {
    DismembermentConfigureResult result {};
    if (definitions.size() >
        kMaximumDismembermentParts) {
        result.error =
            DismembermentConfigureError::CapacityExceeded;
        result.failing_definition_index =
            definitions.size();
        return result;
    }

    std::array<Entry, kMaximumDismembermentParts>
        staged {};
    for (std::size_t index = 0U;
         index < definitions.size();
         ++index) {
        const auto& definition = definitions[index];
        result.failing_definition_index = index;
        if (definition.zone_id == 0U) {
            result.error =
                DismembermentConfigureError::InvalidZoneId;
            return result;
        }
        if (!std::isfinite(
                definition.minimum_severing_power) ||
            definition.minimum_severing_power <= 0.0F ||
            definition.minimum_severing_power >
                kMaximumSeveringPower) {
            result.error =
                DismembermentConfigureError::
                    InvalidSeveringPower;
            return result;
        }
        if (!std::isfinite(
                definition
                    .execution_maximum_health_ratio) ||
            definition
                    .execution_maximum_health_ratio <
                0.0F ||
            definition
                    .execution_maximum_health_ratio >
                1.0F) {
            result.error =
                DismembermentConfigureError::
                    InvalidExecutionHealthRatio;
            return result;
        }
        for (std::size_t previous = 0U;
             previous < index;
             ++previous) {
            if (staged[previous]
                    .definition.zone_id ==
                definition.zone_id) {
                result.error =
                    DismembermentConfigureError::
                        DuplicateZoneId;
                return result;
            }
        }
        staged[index] = {
            definition,
            DismembermentPartState::Intact,
            true,
        };
    }

    // Je valide le lot complet avant de remplacer les membres actifs afin
    // qu'une mauvaise configuration ne détruise jamais le combat courant.
    entries_ = staged;
    part_count_ = definitions.size();
    severed_part_count_ = 0U;
    disabled_capabilities_ = 0U;
    result.configured = true;
    result.configured_part_count = part_count_;
    result.failing_definition_index =
        definitions.size();
    return result;
}

auto DismembermentSystem::find_entry(
    DamageZoneId zone_id) noexcept -> Entry* {
    for (auto& entry : entries_) {
        if (entry.active &&
            entry.definition.zone_id == zone_id) {
            return &entry;
        }
    }
    return nullptr;
}

auto DismembermentSystem::find_entry(
    DamageZoneId zone_id) const noexcept -> const Entry* {
    for (const auto& entry : entries_) {
        if (entry.active &&
            entry.definition.zone_id == zone_id) {
            return &entry;
        }
    }
    return nullptr;
}

auto DismembermentSystem::try_section(
    const DismembermentRequest& request) noexcept
    -> DismembermentResult {
    DismembermentResult result {};
    result.zone_id = request.zone_id;
    if (request.zone_id == 0U ||
        !valid_zone_condition(
            request.local_condition) ||
        !std::isfinite(request.severing_power) ||
        request.severing_power < 0.0F ||
        request.severing_power >
            kMaximumSeveringPower ||
        !std::isfinite(
            request.target_health_ratio) ||
        request.target_health_ratio < 0.0F ||
        request.target_health_ratio > 1.0F ||
        !valid_gore_mode(request.gore_mode)) {
        result.reason =
            DismembermentBlockReason::InvalidRequest;
        return result;
    }

    auto* entry = find_entry(request.zone_id);
    if (entry == nullptr) {
        result.reason =
            DismembermentBlockReason::UnknownPart;
        return result;
    }

    result.accepted = true;
    result.previous_state = entry->state;
    result.state = entry->state;
    result.disabled_capabilities =
        disabled_capabilities_;
    if (entry->state ==
        DismembermentPartState::Severed) {
        result.reason =
            DismembermentBlockReason::AlreadySevered;
        result.gameplay_neutralized = true;
        return result;
    }

    if (request.local_condition ==
            DamageZoneCondition::Wounded ||
        request.local_condition ==
            DamageZoneCondition::Depleted) {
        if (entry->state ==
            DismembermentPartState::Intact) {
            entry->state =
                DismembermentPartState::Wounded;
        }
    }

    if (request.local_condition !=
        DamageZoneCondition::Depleted) {
        result.reason =
            DismembermentBlockReason::
                LocalResistanceRemaining;
        result.state = entry->state;
        return result;
    }
    if (!request.phase_allows_severing) {
        result.reason =
            DismembermentBlockReason::PhaseLocked;
        result.state = entry->state;
        return result;
    }
    if (request.armor_intact) {
        result.reason =
            DismembermentBlockReason::ArmorIntact;
        result.state = entry->state;
        return result;
    }

    const auto& definition = entry->definition;
    if (definition.execution_only) {
        if (request.target_health_ratio >
            definition
                .execution_maximum_health_ratio) {
            result.reason =
                DismembermentBlockReason::
                    ExecutionHealthTooHigh;
            result.state = entry->state;
            return result;
        }
        if (!request.target_staggered) {
            result.reason =
                DismembermentBlockReason::
                    ExecutionRequiresStagger;
            result.state = entry->state;
            return result;
        }
    }

    entry->state =
        DismembermentPartState::Sectionable;
    result.sectionable = true;
    result.state = entry->state;
    if (definition.execution_only &&
        !request.execution_attack) {
        result.reason =
            DismembermentBlockReason::
                ExecutionAttackRequired;
        return result;
    }
    if (!request.blade_crossed_zone) {
        result.reason =
            DismembermentBlockReason::
                BladeDidNotCross;
        return result;
    }
    if (request.severing_power <
        definition.minimum_severing_power) {
        result.reason =
            DismembermentBlockReason::
                InsufficientSeveringPower;
        return result;
    }

    entry->state =
        DismembermentPartState::Severed;
    ++severed_part_count_;
    disabled_capabilities_ |=
        definition.disabled_capabilities;
    result.severed_now = true;
    result.gameplay_neutralized = true;
    result.reason = DismembermentBlockReason::None;
    result.state = entry->state;
    result.visual_action =
        visual_action_for(request.gore_mode);
    result.disabled_capabilities =
        disabled_capabilities_;
    return result;
}

auto DismembermentSystem::part(
    DamageZoneId zone_id) const noexcept
    -> std::optional<DismembermentPartView> {
    const auto* entry = find_entry(zone_id);
    if (entry == nullptr) {
        return std::nullopt;
    }
    return DismembermentPartView {
        entry->definition,
        entry->state,
    };
}

auto DismembermentSystem::part_count() const noexcept
    -> std::size_t {
    return part_count_;
}

auto DismembermentSystem::severed_part_count() const noexcept
    -> std::size_t {
    return severed_part_count_;
}

auto DismembermentSystem::disabled_capabilities() const noexcept
    -> std::uint64_t {
    return disabled_capabilities_;
}

auto DismembermentSystem::capability_is_disabled(
    std::uint64_t capability_mask) const noexcept -> bool {
    return capability_mask != 0U &&
           (disabled_capabilities_ &
            capability_mask) ==
               capability_mask;
}

void DismembermentSystem::reset() noexcept {
    for (auto& entry : entries_) {
        if (entry.active) {
            entry.state =
                DismembermentPartState::Intact;
        }
    }
    severed_part_count_ = 0U;
    disabled_capabilities_ = 0U;
}

void DismembermentSystem::clear() noexcept {
    entries_ = {};
    part_count_ = 0U;
    severed_part_count_ = 0U;
    disabled_capabilities_ = 0U;
}

} // namespace valcraft
