#pragma once

#include "gameplay/combat/DamageZones.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace valcraft {

inline constexpr std::size_t kMaximumDismembermentParts = 16U;
inline constexpr float kMaximumSeveringPower = 1'000'000.0F;

enum class DismembermentPartState : std::uint8_t {
    Intact = 0,
    Wounded,
    Sectionable,
    Severed,
};

enum class GorePresentationMode : std::uint8_t {
    Full = 0,
    Reduced,
    Disabled,
};

enum class DismembermentVisualAction : std::uint8_t {
    None = 0,
    DetachWithBlood,
    HideWithMutedEffect,
    HideWithDarkEffect,
};

enum class DismembermentBlockReason : std::uint8_t {
    None = 0,
    InvalidRequest,
    UnknownPart,
    AlreadySevered,
    LocalResistanceRemaining,
    PhaseLocked,
    ArmorIntact,
    ExecutionHealthTooHigh,
    ExecutionRequiresStagger,
    ExecutionAttackRequired,
    BladeDidNotCross,
    InsufficientSeveringPower,
};

enum class DismembermentConfigureError : std::uint8_t {
    None = 0,
    CapacityExceeded,
    InvalidZoneId,
    DuplicateZoneId,
    InvalidSeveringPower,
    InvalidExecutionHealthRatio,
};

struct DismembermentPartDefinition {
    DamageZoneId zone_id = 0U;
    float minimum_severing_power = 1.0F;
    std::uint64_t disabled_capabilities = 0U;
    bool execution_only = false;
    float execution_maximum_health_ratio = 0.10F;

    auto operator==(const DismembermentPartDefinition&) const
        -> bool = default;
};

struct DismembermentConfigureResult {
    bool configured = false;
    DismembermentConfigureError error =
        DismembermentConfigureError::None;
    std::size_t failing_definition_index = 0U;
    std::size_t configured_part_count = 0U;
};

struct DismembermentRequest {
    DamageZoneId zone_id = 0U;
    DamageZoneCondition local_condition =
        DamageZoneCondition::Intact;
    float severing_power = 0.0F;
    float target_health_ratio = 1.0F;
    bool phase_allows_severing = false;
    bool armor_intact = true;
    bool blade_crossed_zone = false;
    bool target_staggered = false;
    bool execution_attack = false;
    GorePresentationMode gore_mode =
        GorePresentationMode::Full;
};

struct DismembermentPartView {
    DismembermentPartDefinition definition {};
    DismembermentPartState state =
        DismembermentPartState::Intact;

    auto operator==(const DismembermentPartView&) const
        -> bool = default;
};

struct DismembermentResult {
    bool accepted = false;
    bool sectionable = false;
    bool severed_now = false;
    bool gameplay_neutralized = false;
    DismembermentBlockReason reason =
        DismembermentBlockReason::None;
    DamageZoneId zone_id = 0U;
    DismembermentPartState previous_state =
        DismembermentPartState::Intact;
    DismembermentPartState state =
        DismembermentPartState::Intact;
    DismembermentVisualAction visual_action =
        DismembermentVisualAction::None;
    std::uint64_t disabled_capabilities = 0U;
};

class DismembermentSystem {
public:
    [[nodiscard]] auto configure(
        std::span<const DismembermentPartDefinition>
            definitions) noexcept
        -> DismembermentConfigureResult;
    [[nodiscard]] auto try_section(
        const DismembermentRequest& request) noexcept
        -> DismembermentResult;

    [[nodiscard]] auto part(
        DamageZoneId zone_id) const noexcept
        -> std::optional<DismembermentPartView>;
    [[nodiscard]] auto part_count() const noexcept
        -> std::size_t;
    [[nodiscard]] auto severed_part_count() const noexcept
        -> std::size_t;
    [[nodiscard]] auto disabled_capabilities() const noexcept
        -> std::uint64_t;
    [[nodiscard]] auto capability_is_disabled(
        std::uint64_t capability_mask) const noexcept -> bool;

    void reset() noexcept;
    void clear() noexcept;

private:
    struct Entry {
        DismembermentPartDefinition definition {};
        DismembermentPartState state =
            DismembermentPartState::Intact;
        bool active = false;
    };

    [[nodiscard]] auto find_entry(
        DamageZoneId zone_id) noexcept -> Entry*;
    [[nodiscard]] auto find_entry(
        DamageZoneId zone_id) const noexcept -> const Entry*;

    std::array<Entry, kMaximumDismembermentParts>
        entries_ {};
    std::size_t part_count_ = 0U;
    std::size_t severed_part_count_ = 0U;
    std::uint64_t disabled_capabilities_ = 0U;
};

} // namespace valcraft
