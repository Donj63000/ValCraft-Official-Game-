#include "gameplay/progression/AbilityEvents.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace valcraft {

namespace {

[[nodiscard]] auto payload_for(
    AbilityId ability_id,
    AbilityEventEntityId source_id = 7U) noexcept
    -> AbilityEventPayload {
    AbilityEventPayload payload {};
    payload.ability_id = ability_id;
    payload.source_id = source_id;
    payload.target_id = source_id + 1U;
    payload.position = {1.0F, 2.0F, 3.0F};
    payload.direction = {0.0F, 0.0F, -1.0F};
    payload.primary_value = 4.0F;
    payload.secondary_value = 5.0F;
    payload.duration_seconds = 6.0F;
    payload.detail_code = 9U;
    return payload;
}

void fill_with(
    AbilityEventBuffer& buffer,
    AbilityEventType type,
    AbilityCastSequence first_sequence = 1U) {
    for (auto index = std::size_t {0U};
         index < kAbilityLogicalEventCapacity;
         ++index) {
        const auto result = buffer.publish(
            type,
            first_sequence + index,
            payload_for(
                AbilityId::KnightVanguardStrike,
                index + 1U));
        REQUIRE(result.accepted());
    }
}

} // namespace

TEST_CASE("les huit événements logiques gardent une priorité explicite") {
    CHECK(
        ability_event_priority(AbilityEventType::Hit) ==
        AbilityEventPriority::Transient);
    CHECK(
        ability_event_priority(AbilityEventType::CastStarted) ==
        AbilityEventPriority::State);
    CHECK(
        ability_event_priority(AbilityEventType::Expired) ==
        AbilityEventPriority::State);

    constexpr std::array critical_types {
        AbilityEventType::CastSucceeded,
        AbilityEventType::Blocked,
        AbilityEventType::SummonSpawned,
        AbilityEventType::ConstructPlaced,
        AbilityEventType::ConstructDestroyed,
    };
    for (const auto type : critical_types) {
        CHECK(
            ability_event_priority(type) ==
            AbilityEventPriority::Critical);
    }
}

TEST_CASE("les casts et événements reçoivent des identifiants stables ordonnés") {
    AbilityEventBuffer buffer {};

    const auto first = buffer.start_cast(
        payload_for(AbilityId::KnightVanguardStrike));
    const auto second = buffer.start_cast(
        payload_for(AbilityId::NinjaWindAcceleration));
    const auto success = buffer.publish(
        AbilityEventType::CastSucceeded,
        first.cast_sequence,
        payload_for(AbilityId::KnightVanguardStrike));

    REQUIRE(first.publication.accepted());
    REQUIRE(second.publication.accepted());
    REQUIRE(success.accepted());
    CHECK(first.cast_sequence == 1U);
    CHECK(second.cast_sequence == 2U);
    CHECK(first.publication.event_id == 1U);
    CHECK(second.publication.event_id == 2U);
    CHECK(success.event_id == 3U);

    const auto events = buffer.peek();
    REQUIRE(events.size() == 3U);
    CHECK(events[0].cast_sequence == first.cast_sequence);
    CHECK(events[1].cast_sequence == second.cast_sequence);
    CHECK(events[2].cast_sequence == first.cast_sequence);
    CHECK(events[2].type == AbilityEventType::CastSucceeded);

    buffer.clear();
    const auto after_clear = buffer.start_cast(
        payload_for(AbilityId::CommanderFootman));
    CHECK(after_clear.cast_sequence == 3U);
    CHECK(after_clear.publication.event_id == 4U);
}

TEST_CASE("les charges utiles non finies sont assainies avant exposition") {
    AbilityEventBuffer buffer {};
    auto payload = payload_for(
        static_cast<AbilityId>(240U));
    payload.position = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -2.0F * kAbilityEventMaximumAbsolutePosition,
    };
    payload.direction = {
        -5.0F,
        std::numeric_limits<float>::quiet_NaN(),
        5.0F,
    };
    payload.primary_value =
        std::numeric_limits<float>::infinity();
    payload.secondary_value =
        -2.0F * kAbilityEventMaximumAbsoluteValue;
    payload.duration_seconds = -42.0F;

    REQUIRE(
        buffer.publish(
                  AbilityEventType::Hit,
                  1U,
                  payload)
            .accepted());
    const auto& event = buffer.peek().front();
    CHECK(event.payload.ability_id == AbilityId::None);
    CHECK(event.payload.position.x == 0.0F);
    CHECK(event.payload.position.y == 0.0F);
    CHECK(
        event.payload.position.z ==
        -kAbilityEventMaximumAbsolutePosition);
    CHECK(event.payload.direction.x == -1.0F);
    CHECK(event.payload.direction.y == 0.0F);
    CHECK(event.payload.direction.z == 1.0F);
    CHECK(event.payload.primary_value == 0.0F);
    CHECK(
        event.payload.secondary_value ==
        -kAbilityEventMaximumAbsoluteValue);
    CHECK(event.payload.duration_seconds == 0.0F);

    CHECK(std::isfinite(event.payload.position.x));
    CHECK(std::isfinite(event.payload.position.y));
    CHECK(std::isfinite(event.payload.position.z));
    CHECK(std::isfinite(event.payload.primary_value));
    CHECK(std::isfinite(event.payload.secondary_value));
    CHECK(std::isfinite(event.payload.duration_seconds));
}

TEST_CASE("peek drain et clear conservent l'ordre sans réallouer") {
    AbilityEventBuffer buffer {};
    for (auto index = std::uint64_t {1U}; index <= 5U; ++index) {
        REQUIRE(
            buffer.publish(
                      AbilityEventType::Hit,
                      index,
                      payload_for(
                          AbilityId::KnightVanguardStrike,
                          index))
                .accepted());
    }

    std::array<AbilityLogicalEvent, 2U> first_batch {};
    CHECK(buffer.drain(first_batch) == 2U);
    CHECK(first_batch[0].event_id == 1U);
    CHECK(first_batch[1].event_id == 2U);
    REQUIRE(buffer.peek().size() == 3U);
    CHECK(buffer.peek().front().event_id == 3U);

    std::array<AbilityLogicalEvent, 8U> second_batch {};
    CHECK(buffer.drain(second_batch) == 3U);
    CHECK(second_batch[0].event_id == 3U);
    CHECK(second_batch[2].event_id == 5U);
    CHECK(buffer.empty());
    CHECK(buffer.drain(std::span<AbilityLogicalEvent> {}) == 0U);

    REQUIRE(
        buffer.publish(
                  AbilityEventType::Expired,
                  6U)
            .accepted());
    buffer.clear();
    CHECK(buffer.empty());
    CHECK(buffer.size() == 0U);
    CHECK(buffer.capacity() == kAbilityLogicalEventCapacity);
}

TEST_CASE("un événement critique évince le plus ancien événement transitoire") {
    AbilityEventBuffer buffer {};
    fill_with(buffer, AbilityEventType::Hit);

    const auto critical = buffer.publish(
        AbilityEventType::ConstructPlaced,
        500U,
        payload_for(AbilityId::BuilderConstructionPlan));
    REQUIRE(critical.accepted());
    CHECK(critical.event_id == kAbilityLogicalEventCapacity + 1U);
    CHECK(critical.evicted_event_id == 1U);
    REQUIRE(buffer.size() == kAbilityLogicalEventCapacity);
    CHECK(buffer.peek().front().event_id == 2U);
    CHECK(buffer.peek().back().event_id == critical.event_id);
    CHECK(
        buffer.peek().back().type ==
        AbilityEventType::ConstructPlaced);
    CHECK(
        buffer.overflow_stats()
            .evicted_by_priority[static_cast<std::size_t>(
                AbilityEventPriority::Transient)] == 1U);

    const auto transient = buffer.publish(
        AbilityEventType::Hit,
        501U);
    CHECK_FALSE(transient.accepted());
    CHECK(
        transient.failure ==
        AbilityEventPublishFailure::Saturated);
    CHECK(transient.event_id == 0U);
    CHECK(transient.evicted_event_id == 0U);
    CHECK(
        buffer.overflow_stats()
            .rejected_by_priority[static_cast<std::size_t>(
                AbilityEventPriority::Transient)] == 1U);
}

TEST_CASE("la saturation choisit toujours la priorité la plus faible puis la plus ancienne") {
    AbilityEventBuffer buffer {};
    REQUIRE(
        buffer.publish(
                  AbilityEventType::CastStarted,
                  1U)
            .accepted());
    REQUIRE(
        buffer.publish(
                  AbilityEventType::Hit,
                  2U)
            .accepted());
    REQUIRE(
        buffer.publish(
                  AbilityEventType::Hit,
                  3U)
            .accepted());

    for (auto index = std::size_t {3U};
         index < kAbilityLogicalEventCapacity;
         ++index) {
        REQUIRE(
            buffer.publish(
                      AbilityEventType::CastStarted,
                      index + 1U)
                .accepted());
    }

    const auto result = buffer.publish(
        AbilityEventType::Blocked,
        999U);
    REQUIRE(result.accepted());
    CHECK(result.evicted_event_id == 2U);
    REQUIRE(buffer.peek().size() == kAbilityLogicalEventCapacity);
    CHECK(buffer.peek()[0].event_id == 1U);
    CHECK(buffer.peek()[1].event_id == 3U);
}

TEST_CASE("une file entièrement critique conserve les événements déjà acceptés") {
    AbilityEventBuffer buffer {};
    fill_with(buffer, AbilityEventType::CastSucceeded);
    const auto snapshot = buffer.peek().back();

    const auto rejected = buffer.publish(
        AbilityEventType::SummonSpawned,
        700U,
        payload_for(AbilityId::CommanderFootman));
    CHECK_FALSE(rejected.accepted());
    CHECK(
        rejected.failure ==
        AbilityEventPublishFailure::Saturated);
    CHECK(buffer.peek().back() == snapshot);
    CHECK(
        buffer.overflow_stats()
            .rejected_by_priority[static_cast<std::size_t>(
                AbilityEventPriority::Critical)] == 1U);
}

TEST_CASE("les types inconnus et séquences nulles sont refusés sans mutation") {
    AbilityEventBuffer buffer {};
    const auto invalid_type = buffer.publish(
        static_cast<AbilityEventType>(250U),
        1U);
    const auto invalid_sequence = buffer.publish(
        AbilityEventType::Hit,
        0U);

    CHECK_FALSE(invalid_type.accepted());
    CHECK(
        invalid_type.failure ==
        AbilityEventPublishFailure::InvalidType);
    CHECK_FALSE(invalid_sequence.accepted());
    CHECK(
        invalid_sequence.failure ==
        AbilityEventPublishFailure::InvalidCastSequence);
    CHECK(buffer.empty());
    CHECK(
        buffer.overflow_stats() ==
        AbilityEventOverflowStats {});
}

TEST_CASE("deux buffers alimentés pareil produisent exactement le même flux") {
    static_assert(
        std::is_nothrow_destructible_v<AbilityEventBuffer>);
    static_assert(kAbilityLogicalEventCapacity == 256U);

    AbilityEventBuffer first {};
    AbilityEventBuffer second {};
    for (auto index = std::uint64_t {0U}; index < 400U; ++index) {
        const auto type =
            index % 11U == 0U
                ? AbilityEventType::CastSucceeded
                : AbilityEventType::Hit;
        const auto payload = payload_for(
            AbilityId::KnightVanguardStrike,
            index + 1U);
        const auto first_result =
            first.publish(type, index + 1U, payload);
        const auto second_result =
            second.publish(type, index + 1U, payload);
        CHECK(
            first_result.failure ==
            second_result.failure);
        CHECK(first_result.event_id == second_result.event_id);
        CHECK(
            first_result.evicted_event_id ==
            second_result.evicted_event_id);
    }

    REQUIRE(first.peek().size() == second.peek().size());
    for (auto index = std::size_t {0U};
         index < first.peek().size();
         ++index) {
        CHECK(first.peek()[index] == second.peek()[index]);
    }
    CHECK(first.overflow_stats() == second.overflow_stats());
}

} // namespace valcraft
