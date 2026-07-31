#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace valcraft {

using DamageZoneId = std::uint16_t;

inline constexpr std::size_t kMaximumDamageZones = 16U;
inline constexpr float kMaximumDamageZoneResistance = 1'000'000.0F;
inline constexpr float kMaximumDamageZoneInput = 1'000'000.0F;
inline constexpr float kMaximumDamageZoneMultiplier = 16.0F;

enum class DamageZoneKind : std::uint8_t {
    Torso = 0,
    Head,
    LeftArm,
    RightArm,
    LeftLeg,
    RightLeg,
    Horn,
    Tentacle,
};

enum class DamageZoneCondition : std::uint8_t {
    Intact = 0,
    Wounded,
    Depleted,
};

enum class DamageZonesConfigureError : std::uint8_t {
    None = 0,
    CapacityExceeded,
    InvalidId,
    DuplicateId,
    InvalidKind,
    InvalidResistance,
    InvalidMultiplier,
};

enum class DamageZoneHitError : std::uint8_t {
    None = 0,
    InvalidId,
    UnknownZone,
    InvalidDamage,
    InvalidStagger,
};

struct DamageZoneDefinition {
    DamageZoneId id = 0U;
    DamageZoneKind kind = DamageZoneKind::Torso;
    float maximum_local_resistance = 1.0F;
    float health_damage_multiplier = 1.0F;
    float local_damage_multiplier = 1.0F;
    float stagger_multiplier = 1.0F;

    auto operator==(const DamageZoneDefinition&) const -> bool = default;
};

struct DamageZonesConfigureResult {
    bool configured = false;
    DamageZonesConfigureError error = DamageZonesConfigureError::None;
    std::size_t failing_definition_index = 0U;
    std::size_t configured_zone_count = 0U;
};

struct DamageZoneHit {
    DamageZoneId zone_id = 0U;
    float base_health_damage = 0.0F;
    float base_stagger_damage = 0.0F;
};

struct DamageZoneView {
    DamageZoneDefinition definition {};
    float remaining_local_resistance = 0.0F;
    DamageZoneCondition condition = DamageZoneCondition::Intact;

    auto operator==(const DamageZoneView&) const -> bool = default;
};

struct DamageZoneHitResult {
    bool accepted = false;
    bool depleted_now = false;
    DamageZoneHitError error = DamageZoneHitError::None;
    DamageZoneId zone_id = 0U;
    DamageZoneKind kind = DamageZoneKind::Torso;
    float health_damage = 0.0F;
    float local_resistance_damage = 0.0F;
    float stagger_damage = 0.0F;
    float previous_local_resistance = 0.0F;
    float remaining_local_resistance = 0.0F;
    DamageZoneCondition previous_condition = DamageZoneCondition::Intact;
    DamageZoneCondition condition = DamageZoneCondition::Intact;
};

class DamageZones {
public:
    [[nodiscard]] auto configure(
        std::span<const DamageZoneDefinition> definitions) noexcept
        -> DamageZonesConfigureResult;

    [[nodiscard]] auto apply_hit(
        const DamageZoneHit& hit) noexcept -> DamageZoneHitResult;

    [[nodiscard]] auto zone(
        DamageZoneId zone_id) const noexcept
        -> std::optional<DamageZoneView>;
    [[nodiscard]] auto zone_count() const noexcept -> std::size_t;

    void reset() noexcept;
    void clear() noexcept;

private:
    struct Entry {
        DamageZoneDefinition definition {};
        float remaining_local_resistance = 0.0F;
        bool active = false;
    };

    [[nodiscard]] auto find_entry(
        DamageZoneId zone_id) noexcept -> Entry*;
    [[nodiscard]] auto find_entry(
        DamageZoneId zone_id) const noexcept -> const Entry*;

    std::array<Entry, kMaximumDamageZones> entries_ {};
    std::size_t zone_count_ = 0U;
};

[[nodiscard]] constexpr auto damage_zone_condition(
    float remaining_local_resistance,
    float maximum_local_resistance) noexcept -> DamageZoneCondition {
    if (remaining_local_resistance <= 0.0F) {
        return DamageZoneCondition::Depleted;
    }
    if (remaining_local_resistance < maximum_local_resistance) {
        return DamageZoneCondition::Wounded;
    }
    return DamageZoneCondition::Intact;
}

} // namespace valcraft
