#include "creatures/OldGuardAnimation.h"
#include "creatures/OldGuardGeometry.h"
#include "gameplay/OldGuard.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace valcraft {

namespace {

struct GuardFixture {
    OldGuardSystem guards {};
    std::vector<OldGuardTargetCandidate> targets {};
    std::vector<OldGuardOccupant> occupants {};
    OldGuardPlatformFrame platform {};

    explicit GuardFixture(bool loaded = true) {
        guards.reset(7411);
        auto state = guards.save_state();
        constexpr std::array<float, 5> kOtherZ {{-6.0F, -3.0F, 0.0F, 3.0F, 6.0F}};
        state.members[0].local_position = {0.0F, 4.0F, 0.0F};
        state.members[0].yaw_radians = 0.0F;
        state.members[0].action = loaded
                                      ? OldGuardAction::Watch
                                      : OldGuardAction::Reload;
        state.members[0].action_time = loaded ? 0.0F : 2.0F;
        state.members[0].musket_loaded = loaded;
        state.members[0].reload_remaining = loaded ? 0.0F : 3.0F;
        for (std::size_t index = 1; index < state.members.size(); ++index) {
            state.members[index].local_position = {
                -7.0F,
                4.0F,
                kOtherZ[index - 1U],
            };
            state.members[index].yaw_radians = 3.14159265358979323846F;
            state.members[index].action = OldGuardAction::Watch;
            state.members[index].action_time = 0.0F;
            state.members[index].musket_loaded = true;
            state.members[index].reload_remaining = 0.0F;
        }
        guards.load_state(state, 7411);
    }

    auto hostile_at(float eye_distance,
                    float horizontal_offset = 0.0F)
        -> OldGuardTargetCandidate {
        OldGuardTargetCandidate target {};
        target.position = {
            eye_distance,
            4.0F,
            horizontal_offset,
        };
        target.aim_position = {
            eye_distance,
            5.67F,
            horizontal_offset,
        };
        target.body_radius = 0.50F;
        target.morph_factor = 1.0F;
        target.health = 12.0F;
        target.stable_id = 913U;
        target.species = CreatureSpecies::Cow;
        target.phase = CreaturePhase::Night;
        return target;
    }

    auto context() -> OldGuardUpdateContext {
        OldGuardUpdateContext result {};
        result.platform = platform;
        result.targets = targets;
        result.occupants = occupants;
        result.visibility_clear = [](const glm::vec3&, const glm::vec3&) {
            return true;
        };
        result.shot_clear = [](const glm::vec3&,
                               const glm::vec3&,
                               float,
                               std::uint64_t) {
            return true;
        };
        result.melee_clear = [](const glm::vec3&, const glm::vec3&) {
            return true;
        };
        return result;
    }
};

auto matrix_is_finite(const glm::mat4& matrix) -> bool {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

auto same_vector(const glm::vec3& first,
                 const glm::vec3& second,
                 float epsilon = 1.0e-4F) -> bool {
    return glm::length(first - second) <= epsilon;
}

auto shot_from(const OldGuardFrameEvents& events,
               std::uint8_t guard_id) -> const OldGuardShotEvent* {
    const auto iterator = std::ranges::find_if(
        events.shots,
        [guard_id](const auto& shot) {
            return shot.guard_id == guard_id;
        });
    return iterator != events.shots.end()
               ? &*iterator
               : nullptr;
}

auto bayonet_from(const OldGuardFrameEvents& events,
                  std::uint8_t guard_id) -> const OldGuardBayonetEvent* {
    const auto iterator = std::ranges::find_if(
        events.bayonet_hits,
        [guard_id](const auto& hit) {
            return hit.guard_id == guard_id;
        });
    return iterator != events.bayonet_hits.end()
               ? &*iterator
               : nullptr;
}

} // namespace

TEST_CASE("la Vieille Garde cree exactement six soldats et six rondes distinctes") {
    OldGuardSystem guards {};
    guards.reset(9182);
    REQUIRE(guards.members().size() == kOldGuardMemberCount);
    REQUIRE(guards.render_instances().size() == kOldGuardMemberCount);
    REQUIRE(old_guard_patrols().size() == kOldGuardMemberCount);

    std::array<bool, kOldGuardMemberCount> ids {};
    std::array<bool, kOldGuardMemberCount> routes {};
    for (std::size_t index = 0; index < guards.members().size(); ++index) {
        const auto& member = guards.members()[index];
        REQUIRE(member.id < ids.size());
        REQUIRE(member.route_index < routes.size());
        CHECK_FALSE(ids[member.id]);
        CHECK_FALSE(routes[member.route_index]);
        ids[member.id] = true;
        routes[member.route_index] = true;
        CHECK(member.local_position == old_guard_patrols()[index].points[0]);
        for (const auto& point : old_guard_patrols()[index].points) {
            CHECK(std::isfinite(point.x));
            CHECK(std::isfinite(point.y));
            CHECK(std::isfinite(point.z));
        }
    }
}

TEST_CASE("seuls les animaux totalement transformes pendant la nuit sont hostiles") {
    OldGuardTargetCandidate target {};
    target.position = {1.0F, 2.0F, 3.0F};
    target.aim_position = {1.0F, 3.0F, 3.0F};
    target.health = 8.0F;
    target.morph_factor = 1.0F;
    target.phase = CreaturePhase::Night;

    for (const auto species : {
             CreatureSpecies::Pig,
             CreatureSpecies::Cow,
             CreatureSpecies::Sheep,
         }) {
        target.species = species;
        CHECK(old_guard_is_hostile(target));
    }
    target.species = CreatureSpecies::Villager;
    CHECK_FALSE(old_guard_is_hostile(target));
    target.species = CreatureSpecies::Pig;
    target.phase = CreaturePhase::DuskMorph;
    CHECK_FALSE(old_guard_is_hostile(target));
    target.phase = CreaturePhase::DawnRecover;
    CHECK_FALSE(old_guard_is_hostile(target));
    target.phase = CreaturePhase::Day;
    CHECK_FALSE(old_guard_is_hostile(target));
    target.phase = CreaturePhase::Night;
    target.morph_factor = 0.998F;
    CHECK_FALSE(old_guard_is_hostile(target));
    target.morph_factor = 1.0F;
    target.health = 0.0F;
    CHECK_FALSE(old_guard_is_hostile(target));
}

TEST_CASE("les soldats suivent exactement la translation le roulis et le tangage") {
    OldGuardSystem guards {};
    guards.reset(114);
    OldGuardUpdateContext context {};
    context.platform.world_origin = {18.0F, 42.0F, -7.0F};
    context.platform.orientation =
        glm::normalize(
            glm::angleAxis(0.21F, glm::vec3 {1.0F, 0.0F, 0.0F}) *
            glm::angleAxis(-0.17F, glm::vec3 {0.0F, 0.0F, 1.0F}));
    (void)guards.update(context, 0.0F);

    for (std::size_t index = 0; index < guards.members().size(); ++index) {
        const auto expected =
            context.platform.world_origin +
            context.platform.orientation *
                guards.members()[index].local_position;
        CHECK(same_vector(guards.render_instances()[index].position, expected));
        CHECK(
            guards.render_instances()[index].platform_orientation ==
            context.platform.orientation);
    }
}

TEST_CASE("la portee de perception est inclusive a cinquante metres") {
    for (const auto [distance, expected_shot] :
         std::array<std::pair<float, bool>, 3> {{
             {49.99F, true},
             {50.00F, true},
             {50.01F, false},
         }}) {
        GuardFixture fixture {};
        fixture.targets.push_back(fixture.hostile_at(distance));
        auto context = fixture.context();
        const auto& events = fixture.guards.update(context, 0.75F);
        CAPTURE(distance);
        CHECK((shot_from(events, 0U) != nullptr) == expected_shot);
    }
}

TEST_CASE("le cone le terrain et les lignes alliees interdisent un tir dangereux") {
    SUBCASE("une cible derriere le soldat reste hors du cone") {
        GuardFixture fixture {};
        fixture.targets.push_back(fixture.hostile_at(-12.0F));
        auto context = fixture.context();
        const auto& events = fixture.guards.update(context, 0.75F);
        CHECK(std::ranges::none_of(events.shots, [](const auto& shot) {
            return shot.guard_id == 0U;
        }));
    }

    SUBCASE("un terrain opaque retire la cible de la perception") {
        GuardFixture fixture {};
        fixture.targets.push_back(fixture.hostile_at(12.0F));
        auto context = fixture.context();
        context.visibility_clear = [](const glm::vec3&, const glm::vec3&) {
            return false;
        };
        CHECK(
            shot_from(
                fixture.guards.update(context, 0.75F),
                0U) == nullptr);
    }

    SUBCASE("un joueur ou un marin dans l'axe bloque le coup") {
        GuardFixture fixture {};
        fixture.targets.push_back(fixture.hostile_at(12.0F));
        fixture.occupants.push_back({
            {6.0F, 5.40F, 0.0F},
            0.55F,
            OldGuardOccupantPriority::Player,
            true,
        });
        auto context = fixture.context();
        CHECK(
            shot_from(
                fixture.guards.update(context, 0.75F),
                0U) == nullptr);
    }

    SUBCASE("une creature neutre dans l'axe bloque le coup") {
        GuardFixture fixture {};
        fixture.targets.push_back(fixture.hostile_at(12.0F));
        auto neutral = fixture.hostile_at(6.0F);
        neutral.stable_id = 125U;
        neutral.phase = CreaturePhase::Day;
        neutral.morph_factor = 0.0F;
        fixture.targets.push_back(neutral);
        auto context = fixture.context();
        CHECK(
            shot_from(
                fixture.guards.update(context, 0.75F),
                0U) == nullptr);
    }

    SUBCASE("la coque collidable peut bloquer une ligne pourtant visible") {
        GuardFixture fixture {};
        fixture.targets.push_back(fixture.hostile_at(12.0F));
        auto context = fixture.context();
        context.shot_clear = [](const glm::vec3&,
                                const glm::vec3&,
                                float,
                                std::uint64_t) {
            return false;
        };
        CHECK(
            shot_from(
                fixture.guards.update(context, 0.75F),
                0U) == nullptr);
    }
}

TEST_CASE("la mise en joue tire une seule fois a 0,75 seconde") {
    GuardFixture fixture {};
    fixture.targets.push_back(fixture.hostile_at(16.0F));
    auto context = fixture.context();

    CHECK(
        shot_from(
            fixture.guards.update(context, 0.449F),
            0U) == nullptr);
    CHECK(
        shot_from(
            fixture.guards.update(context, 0.300F),
            0U) == nullptr);
    const auto& first = fixture.guards.update(context, 0.001F);
    const auto* first_shot = shot_from(first, 0U);
    REQUIRE(first_shot != nullptr);
    CHECK(first_shot->damage == doctest::Approx(kOldGuardMusketDamage));
    CHECK(first_shot->maximum_distance == doctest::Approx(kOldGuardMusketRange));
    const auto first_sequence = first_shot->sequence;

    CHECK(
        shot_from(
            fixture.guards.update(context, 4.999F),
            0U) == nullptr);
    const auto& second = fixture.guards.update(context, 0.001F);
    const auto* second_shot = shot_from(second, 0U);
    REQUIRE(second_shot != nullptr);
    CHECK(second_shot->sequence > first_sequence);
}

TEST_CASE("le decoupage du dt ne duplique jamais les evenements de mousquet") {
    GuardFixture single {};
    GuardFixture sliced {};
    single.targets.push_back(single.hostile_at(18.0F));
    sliced.targets.push_back(sliced.hostile_at(18.0F));
    auto single_context = single.context();
    auto sliced_context = sliced.context();

    std::vector<std::uint64_t> single_sequences {};
    for (const auto& shot : single.guards.update(single_context, 5.75F).shots) {
        if (shot.guard_id == 0U) {
            single_sequences.push_back(shot.sequence);
        }
    }
    std::vector<std::uint64_t> sliced_sequences {};
    for (int step = 0; step < 575; ++step) {
        for (const auto& shot : sliced.guards.update(sliced_context, 0.01F).shots) {
            if (shot.guard_id == 0U) {
                sliced_sequences.push_back(shot.sequence);
            }
        }
    }
    CHECK(single_sequences.size() == 2U);
    CHECK(sliced_sequences.size() == single_sequences.size());
}

TEST_CASE("la perception echantillonne chaque frontiere de 0,10 s quel que soit le dt") {
    GuardFixture single {};
    GuardFixture sliced {};
    single.targets.push_back(single.hostile_at(18.0F));
    sliced.targets.push_back(sliced.hostile_at(18.0F));
    auto single_context = single.context();
    auto sliced_context = sliced.context();
    auto single_checks = 0;
    auto sliced_checks = 0;
    single_context.visibility_clear =
        [&](const glm::vec3&, const glm::vec3&) {
            ++single_checks;
            return single_checks % 3 != 0;
        };
    sliced_context.visibility_clear =
        [&](const glm::vec3&, const glm::vec3&) {
            ++sliced_checks;
            return sliced_checks % 3 != 0;
        };

    const auto& single_events =
        single.guards.update(single_context, 0.75F);
    const auto single_fired =
        shot_from(single_events, 0U) != nullptr;
    auto sliced_fired = false;
    for (int step = 0; step < 75; ++step) {
        sliced_fired =
            sliced_fired ||
            shot_from(
                sliced.guards.update(sliced_context, 0.01F),
                0U) != nullptr;
    }

    CHECK(single_checks == 8);
    CHECK(sliced_checks == single_checks);
    CHECK(single_fired);
    CHECK(sliced_fired == single_fired);
    CHECK(
        sliced.guards.members()[0].action ==
        single.guards.members()[0].action);
    CHECK(
        sliced.guards.members()[0].reload_remaining ==
        doctest::Approx(
            single.guards.members()[0].reload_remaining)
            .epsilon(1.0e-5));
    CHECK(
        sliced.guards.members()[0].animation_time ==
        doctest::Approx(
            single.guards.members()[0].animation_time)
            .epsilon(1.0e-5));
}

TEST_CASE("le mousquet charge reste prioritaire au contact") {
    GuardFixture fixture {};
    fixture.targets.push_back(fixture.hostile_at(1.20F));
    auto context = fixture.context();
    const auto& events = fixture.guards.update(context, 0.75F);
    CHECK(shot_from(events, 0U) != nullptr);
    CHECK(bayonet_from(events, 0U) == nullptr);
}

TEST_CASE("un tir charge mais dangereux cede la place a la baionnette au contact") {
    GuardFixture fixture {};
    fixture.targets.push_back(fixture.hostile_at(1.20F));
    auto context = fixture.context();
    context.shot_clear = [](const glm::vec3&,
                            const glm::vec3&,
                            float,
                            std::uint64_t) {
        return false;
    };

    const auto& refused_shot = fixture.guards.update(context, 0.75F);
    CHECK(shot_from(refused_shot, 0U) == nullptr);
    CHECK(bayonet_from(refused_shot, 0U) == nullptr);
    CHECK(
        fixture.guards.members()[0].action ==
        OldGuardAction::Bayonet);

    const auto& thrust =
        fixture.guards.update(context, kOldGuardBayonetHitTime);
    const auto* thrust_hit = bayonet_from(thrust, 0U);
    REQUIRE(thrust_hit != nullptr);
    CHECK(
        thrust_hit->damage ==
        doctest::Approx(kOldGuardBayonetDamage));
}

TEST_CASE("la baionnette interrompt puis reprend exactement le rechargement") {
    GuardFixture fixture {false};
    fixture.targets.push_back(fixture.hostile_at(1.20F));
    auto context = fixture.context();

    CHECK(
        bayonet_from(
            fixture.guards.update(context, 0.279F),
            0U) == nullptr);
    const auto& impact = fixture.guards.update(context, 0.001F);
    const auto* impact_hit = bayonet_from(impact, 0U);
    REQUIRE(impact_hit != nullptr);
    CHECK(
        impact_hit->damage ==
        doctest::Approx(kOldGuardBayonetDamage));
    CHECK(
        fixture.guards.members()[0].reload_remaining ==
        doctest::Approx(3.0F));

    (void)fixture.guards.update(context, 0.42F);
    CHECK(
        fixture.guards.members()[0].action ==
        OldGuardAction::Reload);
    CHECK(
        fixture.guards.members()[0].reload_remaining ==
        doctest::Approx(3.0F));
    CHECK(
        fixture.guards.members()[0].bayonet_cooldown ==
        doctest::Approx(kOldGuardBayonetCooldownSeconds));
}

TEST_CASE("le sampler partage les sockets de feu et une geometrie bornee") {
    GuardFixture fixture {};
    fixture.targets.push_back(fixture.hostile_at(14.0F));
    auto context = fixture.context();
    const auto& events = fixture.guards.update(context, 0.75F);
    const auto* shot = shot_from(events, 0U);
    REQUIRE(shot != nullptr);

    const auto& render = fixture.guards.render_instances()[0];
    const auto pose = sample_old_guard_pose(render);
    CHECK(same_vector(pose.muzzle_position, shot->muzzle_position, 2.0e-3F));
    CHECK(same_vector(pose.muzzle_forward, shot->direction, 2.0e-3F));
    CHECK(glm::length(pose.bayonet_tip - pose.bayonet_base) > 0.60F);

    const auto first_parts = build_old_guard_parts(render);
    const auto second_parts = build_old_guard_parts(render);
    REQUIRE(first_parts.size() <= kOldGuardVisualPartBudget);
    REQUIRE(first_parts.size() == second_parts.size());
    CHECK(first_parts.size() >= 48U);
    for (std::size_t index = 0; index < first_parts.size(); ++index) {
        CHECK(matrix_is_finite(first_parts[index].transform));
        CHECK(first_parts[index].transform == second_parts[index].transform);
    }
    const auto mesh = build_old_guard_mesh(render);
    CHECK(mesh.part_count == first_parts.size());
    CHECK_FALSE(mesh.empty());
}

TEST_CASE("les variations d'apparence sont deterministes mais distinguent le roster") {
    OldGuardSystem first {};
    OldGuardSystem second {};
    first.reset(419);
    second.reset(419);
    OldGuardUpdateContext context {};
    (void)first.update(context, 0.0F);
    (void)second.update(context, 0.0F);

    std::array<std::uint32_t, kOldGuardMemberCount> seeds {};
    for (std::size_t index = 0; index < seeds.size(); ++index) {
        seeds[index] = first.render_instances()[index].appearance_seed;
        CHECK(
            seeds[index] ==
            second.render_instances()[index].appearance_seed);
    }
    std::sort(seeds.begin(), seeds.end());
    CHECK(std::adjacent_find(seeds.begin(), seeds.end()) == seeds.end());
}

TEST_CASE("la fumee et le flash sont deterministes et strictement bornes") {
    GuardFixture first {};
    GuardFixture second {};
    first.targets.push_back(first.hostile_at(12.0F));
    second.targets.push_back(second.hostile_at(12.0F));
    auto first_context = first.context();
    auto second_context = second.context();
    first_context.platform.velocity = {0.7F, 0.0F, -0.3F};
    second_context.platform.velocity = first_context.platform.velocity;
    first_context.wind_velocity = {1.2F, 0.0F, 0.4F};
    second_context.wind_velocity = first_context.wind_velocity;

    (void)first.guards.update(first_context, 0.75F);
    (void)second.guards.update(second_context, 0.75F);
    REQUIRE(first.guards.flashes().size() == 1U);
    REQUIRE(first.guards.smoke().size() >= 10U);
    REQUIRE(first.guards.smoke().size() <= 18U);
    CHECK(first.guards.smoke().size() == second.guards.smoke().size());
    for (std::size_t index = 0; index < first.guards.smoke().size(); ++index) {
        CHECK(first.guards.smoke()[index].seed == second.guards.smoke()[index].seed);
        CHECK(same_vector(
            first.guards.smoke()[index].velocity,
            second.guards.smoke()[index].velocity));
    }

    for (int cycle = 0; cycle < 20; ++cycle) {
        (void)first.guards.update(first_context, 5.0F);
    }
    CHECK(first.guards.smoke().size() <= kOldGuardSmokeCapacity);
    first.guards.update_effects(3.0F, first_context.wind_velocity);
    CHECK(first.guards.smoke().empty());
    CHECK(first.guards.flashes().empty());
}

TEST_CASE("la revision de ronde invalide replace sans perdre un rechargement sain") {
    OldGuardSaveState corrupt {};
    corrupt.initialized = true;
    corrupt.patrol_revision = 7U;
    corrupt.members[0].id = 250U;
    corrupt.members[0].local_position = {
        std::numeric_limits<float>::quiet_NaN(),
        999.0F,
        -999.0F,
    };
    corrupt.members[0].musket_loaded = false;
    corrupt.members[0].reload_remaining = 2.75F;
    corrupt.members[0].action =
        static_cast<OldGuardAction>(255U);
    corrupt.members[0].bayonet_cooldown =
        std::numeric_limits<float>::infinity();

    const auto safe = sanitize_old_guard_save_state(corrupt);
    CHECK(safe.initialized);
    CHECK(safe.patrol_revision == kOldGuardPatrolRevision);
    CHECK(safe.members[0].id == 0U);
    CHECK(safe.members[0].route_index == 0U);
    CHECK(safe.members[0].local_position == old_guard_patrols()[0].points[0]);
    CHECK_FALSE(safe.members[0].musket_loaded);
    CHECK(safe.members[0].reload_remaining == doctest::Approx(2.75F));
    CHECK(safe.members[0].action == OldGuardAction::Reload);
    CHECK(std::isfinite(safe.members[0].bayonet_cooldown));

    OldGuardSystem restored {};
    restored.load_state(safe, 19);
    CHECK(restored.save_state() == safe);
}

TEST_CASE("la sanitization preserve un estoc charge et recale le rechargement") {
    OldGuardSystem guards {};
    guards.reset(83);
    auto state = guards.save_state();
    state.members[0].action = OldGuardAction::Bayonet;
    state.members[0].action_time = 0.33F;
    state.members[0].musket_loaded = true;
    state.members[0].reload_remaining = 0.0F;
    state.members[1].action = OldGuardAction::Reload;
    state.members[1].action_time = 4.90F;
    state.members[1].musket_loaded = false;
    state.members[1].reload_remaining = 3.0F;

    const auto safe = sanitize_old_guard_save_state(state);
    CHECK(safe.members[0].action == OldGuardAction::Bayonet);
    CHECK(safe.members[0].action_time == doctest::Approx(0.33F));
    CHECK(safe.members[0].musket_loaded);
    CHECK(safe.members[1].action == OldGuardAction::Reload);
    CHECK_FALSE(safe.members[1].musket_loaded);
    CHECK(safe.members[1].reload_remaining == doctest::Approx(3.0F));
    CHECK(safe.members[1].action_time == doctest::Approx(2.0F));
}

TEST_CASE("une simulation longue reste finie locale et sans teleportation") {
    OldGuardSystem guards {};
    guards.reset(101);
    OldGuardUpdateContext context {};
    auto previous = guards.members();
    std::array<glm::vec3, kOldGuardMemberCount> previous_positions {};
    for (std::size_t index = 0; index < previous.size(); ++index) {
        previous_positions[index] = previous[index].local_position;
    }

    for (int step = 0; step < 12000; ++step) {
        (void)guards.update(context, 0.05F);
        for (std::size_t index = 0; index < guards.members().size(); ++index) {
            const auto position = guards.members()[index].local_position;
            CAPTURE(step);
            CAPTURE(index);
            CHECK(std::isfinite(position.x));
            CHECK(std::isfinite(position.y));
            CHECK(std::isfinite(position.z));
            CHECK(glm::length(position - previous_positions[index]) <= 0.056F);
            previous_positions[index] = position;
        }
    }
}

TEST_CASE("les gardes invulnerables interceptent le rayon du joueur") {
    OldGuardSystem guards {};
    guards.reset(72);
    OldGuardUpdateContext context {};
    (void)guards.update(context, 0.0F);
    const auto guard = guards.render_instances()[0];
    const auto origin = guard.position + glm::vec3 {-4.0F, 1.05F, 0.0F};
    const auto hit = guards.intercept_ray(
        origin,
        {1.0F, 0.0F, 0.0F},
        8.0F);
    CHECK(hit.hit);
    CHECK(hit.guard_id == 0U);
    CHECK(hit.distance < 4.0F);
    const auto focus = guards.focus_from_ray(
        origin,
        {1.0F, 0.0F, 0.0F},
        8.0F);
    CHECK(focus.visible);
    CHECK(focus.guard_id == hit.guard_id);
}

} // namespace valcraft
