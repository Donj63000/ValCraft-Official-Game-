#include "gameplay/progression/AbilityEvents.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] constexpr auto priority_index(
    AbilityEventPriority priority) noexcept -> std::size_t {
    return static_cast<std::size_t>(priority);
}

[[nodiscard]] auto finite_bounded(
    float value,
    float minimum,
    float maximum) noexcept -> float {
    return std::isfinite(value)
               ? std::clamp(value, minimum, maximum)
               : 0.0F;
}

[[nodiscard]] auto sanitize_position(
    const glm::vec3& value) noexcept -> glm::vec3 {
    return {
        finite_bounded(
            value.x,
            -kAbilityEventMaximumAbsolutePosition,
            kAbilityEventMaximumAbsolutePosition),
        finite_bounded(
            value.y,
            -kAbilityEventMaximumAbsolutePosition,
            kAbilityEventMaximumAbsolutePosition),
        finite_bounded(
            value.z,
            -kAbilityEventMaximumAbsolutePosition,
            kAbilityEventMaximumAbsolutePosition),
    };
}

[[nodiscard]] auto sanitize_direction(
    const glm::vec3& value) noexcept -> glm::vec3 {
    return {
        finite_bounded(value.x, -1.0F, 1.0F),
        finite_bounded(value.y, -1.0F, 1.0F),
        finite_bounded(value.z, -1.0F, 1.0F),
    };
}

[[nodiscard]] constexpr auto sanitize_ability_id(
    AbilityId id) noexcept -> AbilityId {
    const auto raw_id = static_cast<std::uint8_t>(id);
    return raw_id < static_cast<std::uint8_t>(AbilityId::Count) ||
                   id == AbilityId::None
               ? id
               : AbilityId::None;
}

[[nodiscard]] auto sanitize_payload(
    const AbilityEventPayload& payload) noexcept -> AbilityEventPayload {
    auto sanitized = payload;
    sanitized.ability_id = sanitize_ability_id(payload.ability_id);
    sanitized.position = sanitize_position(payload.position);
    sanitized.direction = sanitize_direction(payload.direction);
    sanitized.primary_value = finite_bounded(
        payload.primary_value,
        -kAbilityEventMaximumAbsoluteValue,
        kAbilityEventMaximumAbsoluteValue);
    sanitized.secondary_value = finite_bounded(
        payload.secondary_value,
        -kAbilityEventMaximumAbsoluteValue,
        kAbilityEventMaximumAbsoluteValue);
    sanitized.duration_seconds = finite_bounded(
        payload.duration_seconds,
        0.0F,
        kAbilityEventMaximumAbsoluteValue);
    if (sanitized.visual_id.empty()) {
        sanitized.visual_id =
            resolved_ability_visual_id(
                sanitized.ability_id);
    }
    if (sanitized.sfx_id.empty()) {
        sanitized.sfx_id =
            resolved_ability_sfx_id(
                sanitized.ability_id);
    }
    return sanitized;
}

[[nodiscard]] constexpr auto valid_event_type(
    AbilityEventType type) noexcept -> bool {
    return static_cast<std::uint8_t>(type) <
           static_cast<std::uint8_t>(AbilityEventType::Count);
}

void saturating_increment(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

[[nodiscard]] auto take_nonzero_counter(
    std::uint64_t& next_value) noexcept -> std::uint64_t {
    if (next_value == 0U) {
        next_value = 1U;
    }

    const auto allocated = next_value;
    next_value =
        next_value == std::numeric_limits<std::uint64_t>::max()
            ? 1U
            : next_value + 1U;
    return allocated;
}

} // namespace

auto AbilityEventBuffer::start_cast(
    const AbilityEventPayload& payload) noexcept
    -> AbilityCastStartResult {
    const auto cast_sequence = allocate_cast_sequence();
    return {
        cast_sequence,
        publish(
            AbilityEventType::CastStarted,
            cast_sequence,
            payload),
    };
}

auto AbilityEventBuffer::publish(
    AbilityEventType type,
    AbilityCastSequence cast_sequence,
    const AbilityEventPayload& payload) noexcept
    -> AbilityEventPublishResult {
    if (!valid_event_type(type)) {
        return {
            AbilityEventPublishFailure::InvalidType,
            0U,
            0U,
        };
    }
    if (cast_sequence == 0U) {
        return {
            AbilityEventPublishFailure::InvalidCastSequence,
            0U,
            0U,
        };
    }

    const auto incoming_priority = ability_event_priority(type);
    auto insertion_index = event_count_;
    auto evicted_event_id = AbilityLogicalEventId {0U};

    if (event_count_ == events_.size()) {
        auto evicted_priority = AbilityEventPriority::Count;
        insertion_index = events_.size();

        // Je cherche d'abord la priorité la plus faible, puis son événement
        // le plus ancien. Ce choix fixe rend l'overflow reproductible.
        for (auto index = std::size_t {0U};
             index < event_count_;
             ++index) {
            const auto stored_priority = events_[index].priority;
            if (stored_priority < incoming_priority &&
                stored_priority < evicted_priority) {
                evicted_priority = stored_priority;
                insertion_index = index;
            }
        }

        if (insertion_index == events_.size()) {
            saturating_increment(
                overflow_stats_
                    .rejected_by_priority[priority_index(incoming_priority)]);
            return {
                AbilityEventPublishFailure::Saturated,
                0U,
                0U,
            };
        }

        evicted_event_id = events_[insertion_index].event_id;
        saturating_increment(
            overflow_stats_
                .evicted_by_priority[priority_index(evicted_priority)]);
        std::move(
            events_.begin() +
                static_cast<std::ptrdiff_t>(insertion_index + 1U),
            events_.begin() +
                static_cast<std::ptrdiff_t>(event_count_),
            events_.begin() +
                static_cast<std::ptrdiff_t>(insertion_index));
        insertion_index = event_count_ - 1U;
    } else {
        ++event_count_;
    }

    const auto event_id = allocate_event_id();
    events_[insertion_index] = {
        event_id,
        cast_sequence,
        type,
        incoming_priority,
        sanitize_payload(payload),
    };
    return {
        AbilityEventPublishFailure::None,
        event_id,
        evicted_event_id,
    };
}

auto AbilityEventBuffer::peek() const noexcept
    -> std::span<const AbilityLogicalEvent> {
    return {
        events_.data(),
        event_count_,
    };
}

auto AbilityEventBuffer::drain(
    std::span<AbilityLogicalEvent> destination) noexcept
    -> std::size_t {
    const auto drained_count =
        std::min(destination.size(), event_count_);
    std::copy_n(
        events_.begin(),
        static_cast<std::ptrdiff_t>(drained_count),
        destination.begin());

    std::move(
        events_.begin() +
            static_cast<std::ptrdiff_t>(drained_count),
        events_.begin() +
            static_cast<std::ptrdiff_t>(event_count_),
        events_.begin());
    event_count_ -= drained_count;
    return drained_count;
}

void AbilityEventBuffer::clear() noexcept {
    event_count_ = 0U;
}

auto AbilityEventBuffer::size() const noexcept -> std::size_t {
    return event_count_;
}

auto AbilityEventBuffer::empty() const noexcept -> bool {
    return event_count_ == 0U;
}

auto AbilityEventBuffer::overflow_stats() const noexcept
    -> const AbilityEventOverflowStats& {
    return overflow_stats_;
}

auto AbilityEventBuffer::next_cast_sequence() const noexcept
    -> AbilityCastSequence {
    return next_cast_sequence_;
}

void AbilityEventBuffer::reserve_next_cast_sequence(
    AbilityCastSequence minimum_next_sequence) noexcept {
    if (minimum_next_sequence == 0U) {
        minimum_next_sequence = 1U;
    }
    next_cast_sequence_ =
        std::max(
            next_cast_sequence_,
            minimum_next_sequence);
}

auto AbilityEventBuffer::allocate_event_id() noexcept
    -> AbilityLogicalEventId {
    return take_nonzero_counter(next_event_id_);
}

auto AbilityEventBuffer::allocate_cast_sequence() noexcept
    -> AbilityCastSequence {
    return take_nonzero_counter(next_cast_sequence_);
}

} // namespace valcraft
