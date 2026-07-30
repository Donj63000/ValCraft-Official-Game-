#pragma once

#include "gameplay/progression/AbilityCatalog.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace valcraft {

using AbilityLogicalEventId = std::uint64_t;
using AbilityCastSequence = std::uint64_t;
using AbilityEventEntityId = std::uint64_t;

inline constexpr std::size_t kAbilityLogicalEventCapacity = 256U;
inline constexpr float kAbilityEventMaximumAbsolutePosition = 1'000'000.0F;
inline constexpr float kAbilityEventMaximumAbsoluteValue = 1'000'000.0F;

enum class AbilityEventType : std::uint8_t {
    CastStarted = 0,
    CastSucceeded = 1,
    Hit = 2,
    Blocked = 3,
    Expired = 4,
    SummonSpawned = 5,
    ConstructPlaced = 6,
    ConstructDestroyed = 7,
    Count = 8,
};

enum class AbilityEventPriority : std::uint8_t {
    Transient = 0,
    State = 1,
    Critical = 2,
    Count = 3,
};

[[nodiscard]] constexpr auto ability_event_priority(
    AbilityEventType type) noexcept -> AbilityEventPriority {
    switch (type) {
    case AbilityEventType::Hit:
        return AbilityEventPriority::Transient;
    case AbilityEventType::CastStarted:
    case AbilityEventType::Expired:
        return AbilityEventPriority::State;
    case AbilityEventType::CastSucceeded:
    case AbilityEventType::Blocked:
    case AbilityEventType::SummonSpawned:
    case AbilityEventType::ConstructPlaced:
    case AbilityEventType::ConstructDestroyed:
        return AbilityEventPriority::Critical;
    case AbilityEventType::Count:
    default:
        return AbilityEventPriority::State;
    }
}

struct AbilityEventPayload {
    AbilityId ability_id = AbilityId::None;
    AbilityEventEntityId source_id = 0U;
    AbilityEventEntityId target_id = 0U;
    glm::vec3 position {0.0F};
    glm::vec3 direction {0.0F};
    float primary_value = 0.0F;
    float secondary_value = 0.0F;
    float duration_seconds = 0.0F;
    std::uint32_t detail_code = 0U;
    std::string_view visual_id {};
    std::string_view sfx_id {};

    auto operator==(const AbilityEventPayload&) const -> bool = default;
};

struct AbilityLogicalEvent {
    AbilityLogicalEventId event_id = 0U;
    AbilityCastSequence cast_sequence = 0U;
    AbilityEventType type = AbilityEventType::CastStarted;
    AbilityEventPriority priority = AbilityEventPriority::State;
    AbilityEventPayload payload {};

    auto operator==(const AbilityLogicalEvent&) const -> bool = default;
};

enum class AbilityEventPublishFailure : std::uint8_t {
    None = 0,
    InvalidType,
    InvalidCastSequence,
    Saturated,
};

struct AbilityEventPublishResult {
    AbilityEventPublishFailure failure = AbilityEventPublishFailure::None;
    AbilityLogicalEventId event_id = 0U;
    AbilityLogicalEventId evicted_event_id = 0U;

    [[nodiscard]] constexpr auto accepted() const noexcept -> bool {
        return failure == AbilityEventPublishFailure::None;
    }
};

struct AbilityCastStartResult {
    AbilityCastSequence cast_sequence = 0U;
    AbilityEventPublishResult publication {};
};

struct AbilityEventOverflowStats {
    std::array<
        std::uint64_t,
        static_cast<std::size_t>(AbilityEventPriority::Count)>
        rejected_by_priority {};
    std::array<
        std::uint64_t,
        static_cast<std::size_t>(AbilityEventPriority::Count)>
        evicted_by_priority {};

    auto operator==(const AbilityEventOverflowStats&) const -> bool = default;
};

class AbilityEventBuffer {
public:
    // Je crée toujours une séquence non nulle, même si la file saturée ne
    // peut plus recevoir le CastStarted. Les événements ultérieurs gardent
    // ainsi une identité de cast stable et indépendante du rendu.
    [[nodiscard]] auto start_cast(
        const AbilityEventPayload& payload) noexcept
        -> AbilityCastStartResult;

    // Je protège une priorité déjà stockée contre toute publication de
    // priorité égale ou inférieure. Une priorité supérieure remplace le plus
    // ancien événement de la priorité disponible la plus faible ; si toute
    // la file est aussi importante, je refuse la publication sans mutation.
    [[nodiscard]] auto publish(
        AbilityEventType type,
        AbilityCastSequence cast_sequence,
        const AbilityEventPayload& payload = {}) noexcept
        -> AbilityEventPublishResult;

    // Je n'expose au rendu et à l'audio que des observations constantes ou
    // des copies drainées : aucun consommateur ne peut décider le gameplay.
    [[nodiscard]] auto peek() const noexcept
        -> std::span<const AbilityLogicalEvent>;

    [[nodiscard]] auto drain(
        std::span<AbilityLogicalEvent> destination) noexcept
        -> std::size_t;

    // Je vide seulement les événements consommables : je ne réutilise ni
    // leurs identifiants ni les séquences déjà attribuées.
    void clear() noexcept;

    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] constexpr auto capacity() const noexcept -> std::size_t {
        return kAbilityLogicalEventCapacity;
    }
    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto overflow_stats() const noexcept
        -> const AbilityEventOverflowStats&;
    [[nodiscard]] auto next_cast_sequence() const noexcept
        -> AbilityCastSequence;
    void reserve_next_cast_sequence(
        AbilityCastSequence minimum_next_sequence) noexcept;

private:
    [[nodiscard]] auto allocate_event_id() noexcept
        -> AbilityLogicalEventId;
    [[nodiscard]] auto allocate_cast_sequence() noexcept
        -> AbilityCastSequence;

    std::array<AbilityLogicalEvent, kAbilityLogicalEventCapacity> events_ {};
    std::size_t event_count_ = 0U;
    AbilityLogicalEventId next_event_id_ = 1U;
    AbilityCastSequence next_cast_sequence_ = 1U;
    AbilityEventOverflowStats overflow_stats_ {};
};

} // namespace valcraft
