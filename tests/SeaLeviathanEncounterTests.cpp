#include "gameplay/encounters/SeaLeviathanEncounter.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>

namespace valcraft {

namespace {

constexpr ShipLocalFrame kIdentityFrame {};

void advance(
    SeaLeviathanEncounter& encounter,
    float seconds,
    bool guarding = false,
    bool perfect = false) {
    const auto ticks =
        static_cast<std::uint32_t>(seconds * 60.0F + 0.5F);
    for (std::uint32_t tick = 0U; tick < ticks; ++tick) {
        const auto result = encounter.update(
            1.0F / 60.0F,
            {
                kIdentityFrame,
                glm::vec3 {0.0F, 1.0F, 0.0F},
                true,
                guarding,
                perfect,
            });
        REQUIRE(result.accepted);
    }
}

[[nodiscard]] auto drain_events(
    SeaLeviathanEncounter& encounter) {
    std::array<SeaLeviathanEvent, kMaximumSeaLeviathanEvents>
        events {};
    const auto count = encounter.consume_events(events);
    return std::pair {events, count};
}

void reach_charged_opening(
    SeaLeviathanEncounter& encounter,
    bool guarding,
    bool perfect) {
    advance(encounter, 4.0F, guarding, perfect);
    advance(encounter, 1.0F, guarding, perfect);
    REQUIRE(
        encounter.phase() ==
        SeaLeviathanPhase::ChargedOpening);
}

} // namespace

TEST_CASE("le repere local suit translation tangage et rotation du navire") {
    const ShipLocalFrame frame {
        glm::vec3 {10.0F, 20.0F, 30.0F},
        glm::vec3 {0.0F, 0.0F, -1.0F},
        glm::vec3 {0.0F, 1.0F, 0.0F},
        glm::vec3 {1.0F, 0.0F, 0.0F},
    };
    REQUIRE(valid_ship_local_frame(frame));
    const auto world =
        ship_local_to_world(frame, glm::vec3 {2.0F, 3.0F, 4.0F});
    REQUIRE(world.has_value());
    CHECK(world->x == doctest::Approx(14.0F));
    CHECK(world->y == doctest::Approx(23.0F));
    CHECK(world->z == doctest::Approx(28.0F));
    const auto local = ship_world_to_local(frame, *world);
    REQUIRE(local.has_value());
    CHECK(local->x == doctest::Approx(2.0F));
    CHECK(local->y == doctest::Approx(3.0F));
    CHECK(local->z == doctest::Approx(4.0F));

    auto invalid = frame;
    invalid.forward = invalid.right;
    CHECK_FALSE(valid_ship_local_frame(invalid));
    CHECK_FALSE(
        ship_local_to_world(invalid, glm::vec3 {}).has_value());
}

TEST_CASE("la rencontre choisit entre mille et deux mille cinq cents points de vie") {
    SeaLeviathanEncounter first {};
    SeaLeviathanEncounter second {};
    const auto a = first.start({0x51EAU, {0.0F, -1.5F, 7.0F}});
    const auto b = second.start({0x51EAU, {0.0F, -1.5F, 7.0F}});
    REQUIRE(a.started);
    REQUIRE(b.started);
    CHECK(a.maximum_health >= kSeaLeviathanMinimumHealth);
    CHECK(a.maximum_health <= kSeaLeviathanMaximumHealth);
    CHECK(a.maximum_health == doctest::Approx(b.maximum_health));
    CHECK(first.phase() == SeaLeviathanPhase::Emerging);
    CHECK(
        SeaLeviathanEncounter::persistence_policy() ==
        TemporaryPersistencePolicy::NeverSaved);
}

TEST_CASE("les instantanes restent ancres au navire mobile sans derive monde") {
    SeaLeviathanEncounter encounter {};
    REQUIRE(encounter.start({12U, {0.0F, -1.5F, 7.0F}}).started);
    const auto initial = encounter.render_snapshot(kIdentityFrame);
    REQUIRE(initial.has_value());

    const ShipLocalFrame moved {
        glm::vec3 {50.0F, 4.0F, -30.0F},
        glm::vec3 {0.0F, 0.0F, -1.0F},
        glm::vec3 {0.0F, 1.0F, 0.0F},
        glm::vec3 {1.0F, 0.0F, 0.0F},
    };
    const auto after = encounter.render_snapshot(moved);
    REQUIRE(after.has_value());
    CHECK(
        after->body_anchor_ship_local.x ==
        doctest::Approx(initial->body_anchor_ship_local.x));
    CHECK(
        after->body_anchor_ship_local.z ==
        doctest::Approx(initial->body_anchor_ship_local.z));
    CHECK(after->body_anchor_world.x == doctest::Approx(57.0F));
    CHECK(after->body_anchor_world.y == doctest::Approx(2.5F));
    CHECK(after->body_anchor_world.z == doctest::Approx(-30.0F));
}

TEST_CASE("une tentacule exige un coup sectionnant et quitte les volumes actifs") {
    SeaLeviathanEncounter encounter {};
    REQUIRE(encounter.start({9U, {}}).started);
    const auto blunt = encounter.apply_hit({
        SeaLeviathanPart::Tentacle0,
        1'000.0F,
        0.0F,
        false,
        false,
        0U,
    });
    REQUIRE(blunt.accepted);
    CHECK_FALSE(blunt.tentacle_severed_now);
    CHECK(blunt.remaining_local_resistance == doctest::Approx(1.0F));

    const auto sever = encounter.apply_hit({
        SeaLeviathanPart::Tentacle0,
        2.0F,
        30.0F,
        true,
        true,
        2U,
    });
    REQUIRE(sever.accepted);
    CHECK(sever.tentacle_severed_now);
    CHECK(sever.remaining_local_resistance == doctest::Approx(0.0F));

    const auto combat = encounter.combat_snapshot(kIdentityFrame);
    REQUIRE(combat.has_value());
    CHECK(combat->hit_volumes[2].part == SeaLeviathanPart::Tentacle0);
    CHECK_FALSE(combat->hit_volumes[2].enabled);
    CHECK(combat->hit_volumes[2].sectionable);
}

TEST_CASE("la frappe de pont offre garde normale garde parfaite et aucun degat structurel") {
    for (const auto mode : std::array<int, 3U> {0, 1, 2}) {
        SeaLeviathanEncounter encounter {};
        REQUIRE(encounter.start({static_cast<std::uint32_t>(mode), {}}).started);
        static_cast<void>(drain_events(encounter));
        reach_charged_opening(
            encounter,
            mode != 0,
            mode == 2);

        const auto [events, count] = drain_events(encounter);
        auto resolved = false;
        for (std::size_t index = 0U; index < count; ++index) {
            const auto& event = events[index];
            CHECK(event.damage.ship_damage == doctest::Approx(0.0F));
            CHECK(event.damage.allied_damage == doctest::Approx(0.0F));
            if (mode == 0 &&
                event.kind == SeaLeviathanEventKind::PlayerHit) {
                CHECK(event.damage.player_damage == doctest::Approx(24.0F));
                resolved = true;
            }
            if (mode == 1 &&
                event.kind ==
                    SeaLeviathanEventKind::DeckStrikeGuarded) {
                CHECK(event.damage.player_damage == doctest::Approx(4.0F));
                CHECK(
                    event.damage.player_stability_damage ==
                    doctest::Approx(32.0F));
                resolved = true;
            }
            if (mode == 2 &&
                event.kind == SeaLeviathanEventKind::PerfectGuard) {
                CHECK(event.damage.player_damage == doctest::Approx(0.0F));
                CHECK(
                    event.damage.player_stability_damage ==
                    doctest::Approx(5.0F));
                resolved = true;
            }
        }
        CHECK(resolved);
        const auto combat = encounter.combat_snapshot(kIdentityFrame);
        REQUIRE(combat.has_value());
        CHECK_FALSE(combat->can_damage_ship);
        CHECK_FALSE(combat->can_damage_allies);
    }
}

TEST_CASE("la charge ouvre le noyau mais l eveil deux reste obligatoire") {
    SeaLeviathanEncounter encounter {};
    REQUIRE(encounter.start({44U, {}}).started);
    reach_charged_opening(encounter, true, true);
    const auto opening = encounter.apply_hit({
        SeaLeviathanPart::Carapace,
        32.0F,
        10.0F,
        true,
        false,
        1U,
    });
    REQUIRE(opening.accepted);
    CHECK(opening.core_exposed_now);
    CHECK(encounter.phase() == SeaLeviathanPhase::ExposedCore);

    const auto dormant_core = encounter.apply_hit({
        SeaLeviathanPart::Core,
        40.0F,
        0.0F,
        true,
        false,
        1U,
    });
    REQUIRE(dormant_core.accepted);
    CHECK(dormant_core.core_deflected);
    CHECK(dormant_core.applied_health_damage == doctest::Approx(0.0F));

    const auto astral_core = encounter.apply_hit({
        SeaLeviathanPart::Core,
        40.0F,
        0.0F,
        true,
        false,
        2U,
    });
    REQUIRE(astral_core.accepted);
    CHECK_FALSE(astral_core.core_deflected);
    CHECK(astral_core.applied_health_damage == doctest::Approx(62.0F));
    const auto combat = encounter.combat_snapshot(kIdentityFrame);
    REQUIRE(combat.has_value());
    CHECK(combat->hit_volumes[0].enabled);
}

TEST_CASE("la perte de vie conduit a la frenesie puis a la defaite") {
    SeaLeviathanEncounter encounter {};
    REQUIRE(encounter.start({78U, {}}).started);
    reach_charged_opening(encounter, true, true);
    REQUIRE(encounter.apply_hit({
        SeaLeviathanPart::Carapace,
        32.0F,
        0.0F,
        true,
        false,
        2U,
    }).core_exposed_now);

    const auto heavy = encounter.apply_hit({
        SeaLeviathanPart::Core,
        encounter.maximum_health() * 0.45F,
        0.0F,
        true,
        false,
        2U,
    });
    REQUIRE(heavy.accepted);
    CHECK(encounter.health() < encounter.maximum_health() * 0.35F);
    advance(encounter, 6.0F);
    CHECK(encounter.phase() == SeaLeviathanPhase::Frenzy);

    reach_charged_opening(encounter, true, true);
    REQUIRE(encounter.apply_hit({
        SeaLeviathanPart::Carapace,
        32.0F,
        0.0F,
        true,
        false,
        2U,
    }).core_exposed_now);
    const auto final_hit = encounter.apply_hit({
        SeaLeviathanPart::Core,
        1'000'000.0F,
        200.0F,
        true,
        false,
        3U,
    });
    REQUIRE(final_hit.accepted);
    CHECK(final_hit.defeated_now);
    CHECK(encounter.phase() == SeaLeviathanPhase::Defeated);
    CHECK(encounter.health() == doctest::Approx(0.0F));
}

TEST_CASE("les entrees non finies sont transactionnelles et les evenements bornes") {
    SeaLeviathanEncounter encounter {};
    REQUIRE(encounter.start({3U, {}}).started);
    const auto health_before = encounter.health();
    const auto invalid_hit = encounter.apply_hit({
        SeaLeviathanPart::Core,
        std::numeric_limits<float>::quiet_NaN(),
        0.0F,
        false,
        false,
        2U,
    });
    CHECK_FALSE(invalid_hit.accepted);
    CHECK(invalid_hit.error == SeaLeviathanHitError::InvalidDamage);
    CHECK(encounter.health() == doctest::Approx(health_before));

    auto invalid_frame = kIdentityFrame;
    invalid_frame.up.x =
        std::numeric_limits<float>::infinity();
    const auto invalid_update = encounter.update(
        1.0F / 60.0F,
        {
            invalid_frame,
            {},
            true,
            false,
            false,
        });
    CHECK_FALSE(invalid_update.accepted);
    CHECK_FALSE(encounter.render_snapshot(invalid_frame).has_value());

    std::array<SeaLeviathanEvent, 1U> one {};
    const auto consumed = encounter.consume_events(one);
    CHECK(consumed == 1U);
    std::array<SeaLeviathanEvent, kMaximumSeaLeviathanEvents> rest {};
    CHECK(encounter.consume_events(rest) <= rest.size());
}

} // namespace valcraft
