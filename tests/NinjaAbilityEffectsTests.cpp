#include "gameplay/progression/NinjaAbilityEffects.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace valcraft {

namespace {

[[nodiscard]] auto context(
    NinjaAbilityRank rank,
    bool mastered = false,
    NinjaActivationId activation_id = 1U)
    -> NinjaCastContext {
    return {
        1U,
        activation_id,
        rank,
        1U,
        0.0F,
        mastered,
    };
}

[[nodiscard]] auto entity(
    NinjaEntityId id,
    const glm::vec3& position,
    bool hostile = true) -> NinjaEntitySnapshot {
    NinjaEntitySnapshot result {};
    result.entity_id = id;
    result.position = position;
    result.hostile_to_owner = hostile;
    result.allied_with_owner = !hostile;
    return result;
}

[[nodiscard]] auto base_callbacks()
    -> NinjaWorldCallbacks {
    NinjaWorldCallbacks callbacks {};
    callbacks.validate_smoke_placement =
        [](const NinjaSmokePlacementRequest&) {
            return true;
        };
    callbacks.find_safe_movement =
        [](const NinjaSafeMovementRequest&
               request) {
            return NinjaSafeMovementResult {
                true,
                true,
                request.requested_end,
            };
        };
    callbacks.commit_movement =
        [](const NinjaMovementCommit&) {
            return true;
        };
    callbacks.query_entities =
        [](const NinjaEntityQuery&,
           std::span<NinjaEntitySnapshot>) {
            return std::size_t {0U};
        };
    callbacks.apply_damage =
        [](const NinjaDamageRequest& request) {
            return NinjaDamageResult {
                true,
                false,
                request.damage,
            };
        };
    callbacks.entity_snapshot =
        [](NinjaEntityId id)
            -> std::optional<NinjaEntitySnapshot> {
            if (id == 0U) {
                return std::nullopt;
            }
            return entity(
                id,
                glm::vec3 {
                    static_cast<float>(id),
                    0.0F,
                    0.0F,
                });
        };
    callbacks.select_kunai_target =
        [](const NinjaKunaiTargetQuery&)
            -> std::optional<NinjaEntitySnapshot> {
            return std::nullopt;
        };
    return callbacks;
}

void advance(
    NinjaAbilityEffects& effects,
    float dt,
    std::size_t frame_count,
    const NinjaWorldCallbacks& callbacks = {}) {
    for (std::size_t frame = 0U;
         frame < frame_count;
         ++frame) {
        const auto result =
            effects.update(dt, callbacks);
        REQUIRE(result.accepted);
    }
}

} // namespace

TEST_CASE("les quatre compétences Ninja exposent les valeurs exactes du plan") {
    const auto smoke_one =
        ninja_smoke_bomb_tuning(
            NinjaAbilityRank::RankOne);
    const auto smoke_two =
        ninja_smoke_bomb_tuning(
            NinjaAbilityRank::RankTwo);
    const auto smoke_three =
        ninja_smoke_bomb_tuning(
            NinjaAbilityRank::RankThree);
    CHECK(smoke_one.energy_cost == doctest::Approx(20.0F));
    CHECK(smoke_one.cooldown_seconds == doctest::Approx(15.0F));
    CHECK(smoke_one.radius == doctest::Approx(4.0F));
    CHECK(smoke_one.duration_seconds == doctest::Approx(5.0F));
    CHECK(smoke_one.ninja_speed_bonus == doctest::Approx(0.10F));
    CHECK(smoke_two.cooldown_seconds == doctest::Approx(13.0F));
    CHECK(smoke_two.radius == doctest::Approx(5.0F));
    CHECK(smoke_two.duration_seconds == doctest::Approx(6.0F));
    CHECK(smoke_two.ninja_speed_bonus == doctest::Approx(0.15F));
    CHECK(smoke_two.attack_bonus == doctest::Approx(0.30F));
    CHECK(smoke_three.cooldown_seconds == doctest::Approx(11.0F));
    CHECK(smoke_three.radius == doctest::Approx(6.0F));
    CHECK(smoke_three.duration_seconds == doctest::Approx(7.0F));
    CHECK(smoke_three.ninja_speed_bonus == doctest::Approx(0.20F));
    CHECK(smoke_three.attack_bonus == doctest::Approx(0.40F));
    CHECK(smoke_three.slow_fraction == doctest::Approx(0.20F));
    CHECK(smoke_three.slow_duration_seconds == doctest::Approx(2.0F));
    CHECK(smoke_three.allied_speed_bonus == doctest::Approx(0.10F));
    CHECK(smoke_three.lock_break_delay_seconds == doctest::Approx(1.0F));

    const auto leap_one =
        ninja_shinobi_leap_tuning(
            NinjaAbilityRank::RankOne);
    const auto leap_two =
        ninja_shinobi_leap_tuning(
            NinjaAbilityRank::RankTwo);
    const auto leap_three =
        ninja_shinobi_leap_tuning(
            NinjaAbilityRank::RankThree);
    CHECK(leap_one.energy_cost == doctest::Approx(15.0F));
    CHECK(leap_one.cooldown_seconds == doctest::Approx(8.0F));
    CHECK(leap_one.distance == doctest::Approx(4.0F));
    CHECK_FALSE(leap_one.aerial_use_allowed);
    CHECK(leap_two.cooldown_seconds == doctest::Approx(7.0F));
    CHECK(leap_two.distance == doctest::Approx(5.0F));
    CHECK(leap_two.aerial_use_allowed);
    CHECK_FALSE(leap_two.free_second_impulse_allowed);
    CHECK(leap_three.cooldown_seconds == doctest::Approx(6.0F));
    CHECK(leap_three.distance == doctest::Approx(6.0F));
    CHECK(leap_three.free_second_impulse_allowed);
    CHECK(leap_three.free_second_impulse_distance == doctest::Approx(3.0F));
    CHECK(leap_three.mastery_window_seconds == doctest::Approx(2.0F));
    CHECK(leap_three.mastery_wave_damage == doctest::Approx(5.0F));
    CHECK(leap_three.mastery_wave_radius == doctest::Approx(2.0F));

    const auto dash_one =
        ninja_lightning_dash_tuning(
            NinjaAbilityRank::RankOne);
    const auto dash_two =
        ninja_lightning_dash_tuning(
            NinjaAbilityRank::RankTwo);
    const auto dash_three =
        ninja_lightning_dash_tuning(
            NinjaAbilityRank::RankThree);
    CHECK(dash_one.energy_cost == doctest::Approx(22.0F));
    CHECK(dash_one.cooldown_seconds == doctest::Approx(11.0F));
    CHECK(dash_one.distance == doctest::Approx(6.0F));
    CHECK(dash_one.base_damage == doctest::Approx(7.0F));
    CHECK(dash_two.cooldown_seconds == doctest::Approx(9.5F));
    CHECK(dash_two.distance == doctest::Approx(7.0F));
    CHECK(dash_two.base_damage == doctest::Approx(9.0F));
    CHECK(dash_two.kill_cooldown_reduction == doctest::Approx(0.40F));
    CHECK(dash_three.cooldown_seconds == doctest::Approx(8.0F));
    CHECK(dash_three.distance == doctest::Approx(8.0F));
    CHECK(dash_three.base_damage == doctest::Approx(11.0F));
    CHECK(dash_three.kill_cooldown_reduction == doctest::Approx(0.50F));
    CHECK(dash_three.mastery_charge_count == 2U);
    CHECK(dash_three.mastery_charge_recharge_seconds == doctest::Approx(12.0F));
    CHECK(dash_three.mastery_repeat_target_interval_seconds == doctest::Approx(0.75F));

    const auto kunai_one =
        ninja_spectral_kunai_tuning(
            NinjaAbilityRank::RankOne);
    const auto kunai_two =
        ninja_spectral_kunai_tuning(
            NinjaAbilityRank::RankTwo);
    const auto kunai_three =
        ninja_spectral_kunai_tuning(
            NinjaAbilityRank::RankThree);
    CHECK(kunai_one.energy_cost == doctest::Approx(20.0F));
    CHECK(kunai_one.cooldown_seconds == doctest::Approx(8.0F));
    CHECK(kunai_one.base_damage == doctest::Approx(7.0F));
    CHECK(kunai_one.bounce_count == 1U);
    CHECK(kunai_one.mark_duration_seconds == doctest::Approx(6.0F));
    CHECK(kunai_one.mark_melee_bonus == doctest::Approx(0.30F));
    CHECK(kunai_one.bounce_power_loss == doctest::Approx(0.25F));
    CHECK(kunai_two.cooldown_seconds == doctest::Approx(7.0F));
    CHECK(kunai_two.base_damage == doctest::Approx(9.0F));
    CHECK(kunai_two.bounce_count == 2U);
    CHECK(kunai_two.mark_melee_bonus == doctest::Approx(0.40F));
    CHECK(kunai_two.bounce_power_loss == doctest::Approx(0.20F));
    CHECK(kunai_three.cooldown_seconds == doctest::Approx(6.0F));
    CHECK(kunai_three.base_damage == doctest::Approx(11.0F));
    CHECK(kunai_three.bounce_count == 3U);
    CHECK(kunai_three.mark_melee_bonus == doctest::Approx(0.50F));
    CHECK(kunai_three.bounce_power_loss == doctest::Approx(0.15F));
    CHECK(kunai_three.mastery_return_power == doctest::Approx(0.60F));
}

TEST_CASE("les dégâts Ninja appliquent le niveau et l'agilité avec leurs plafonds") {
    CHECK(
        ninja_spell_damage(10.0F, 1U, 0.0F) ==
        doctest::Approx(10.0F));
    CHECK(
        ninja_spell_damage(10.0F, 50U, 10.0F) ==
        doctest::Approx(13.225F));
    CHECK(
        ninja_spell_damage(10.0F, 500U, 100.0F) ==
        doctest::Approx(15.475F));
    CHECK(
        ninja_spell_damage(10.0F, 0U, -8.0F) ==
        doctest::Approx(10.0F));
    CHECK(
        ninja_spell_damage(
            std::numeric_limits<float>::infinity(),
            1U,
            0.0F) == 0.0F);
    CHECK(
        ninja_spell_damage(
            10.0F,
            1U,
            std::numeric_limits<float>::quiet_NaN()) == 0.0F);
}

TEST_CASE("le Fumigène valide avant de remplacer un nuage actif") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    REQUIRE(
        effects.cast_smoke_bomb(
                   context(NinjaAbilityRank::RankOne),
                   {1.0F, 0.0F, 0.0F},
                   callbacks)
            .cast);
    const auto original =
        effects.smoke_state();

    callbacks.validate_smoke_placement =
        [](const NinjaSmokePlacementRequest&) {
            return false;
        };
    const auto refused =
        effects.cast_smoke_bomb(
            context(
                NinjaAbilityRank::RankThree,
                true,
                2U),
            {8.0F, 0.0F, 0.0F},
            callbacks);
    CHECK_FALSE(refused.cast);
    CHECK(refused.failure == NinjaEffectFailure::InvalidTarget);
    const auto preserved =
        effects.smoke_state();
    CHECK(preserved.activation_id == original.activation_id);
    CHECK(preserved.center.x == doctest::Approx(original.center.x));

    const auto invalid =
        effects.cast_smoke_bomb(
            context(NinjaAbilityRank::RankOne),
            {
                std::numeric_limits<float>::quiet_NaN(),
                0.0F,
                0.0F,
            },
            callbacks);
    CHECK_FALSE(invalid.cast);
    CHECK(invalid.failure == NinjaEffectFailure::NonFiniteInput);
}

TEST_CASE("la durée du Fumigène est identique à 30 60 et 144 images par seconde") {
    auto callbacks = base_callbacks();
    NinjaAbilityEffects at_thirty {};
    NinjaAbilityEffects at_sixty {};
    NinjaAbilityEffects at_144 {};
    REQUIRE(
        at_thirty.cast_smoke_bomb(
                     context(NinjaAbilityRank::RankOne),
                     {0.0F, 0.0F, 0.0F},
                     callbacks)
            .cast);
    REQUIRE(
        at_sixty.cast_smoke_bomb(
                    context(NinjaAbilityRank::RankOne),
                    {0.0F, 0.0F, 0.0F},
                    callbacks)
            .cast);
    REQUIRE(
        at_144.cast_smoke_bomb(
                  context(NinjaAbilityRank::RankOne),
                  {0.0F, 0.0F, 0.0F},
                  callbacks)
            .cast);

    advance(at_thirty, 1.0F / 30.0F, 150U);
    advance(at_sixty, 1.0F / 60.0F, 300U);
    advance(at_144, 1.0F / 144.0F, 720U);
    CHECK_FALSE(at_thirty.smoke_state().active);
    CHECK_FALSE(at_sixty.smoke_state().active);
    CHECK_FALSE(at_144.smoke_state().active);
}

TEST_CASE("le Fumigène brise la cible après une seconde continue et partage la maîtrise") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    callbacks.entity_snapshot =
        [](NinjaEntityId id)
            -> std::optional<NinjaEntitySnapshot> {
            if (id != 1U) {
                return std::nullopt;
            }
            auto owner =
                entity(
                    1U,
                    {0.0F, 0.0F, 0.0F},
                    false);
            owner.allied_with_owner = true;
            return owner;
        };
    callbacks.query_entities =
        [](const NinjaEntityQuery& query,
           std::span<NinjaEntitySnapshot> output) {
            if (query.kind ==
                NinjaEntityQueryKind::InsideSmoke) {
                output[0] =
                    entity(
                        2U,
                        {1.0F, 0.0F, 0.0F},
                        false);
                return std::size_t {1U};
            }
            if (query.kind ==
                    NinjaEntityQueryKind::LockingTarget &&
                query.target_id == 1U) {
                output[0] =
                    entity(
                        9U,
                        {10.0F, 0.0F, 0.0F});
                output[0].target_id = 1U;
                return std::size_t {1U};
            }
            return std::size_t {0U};
        };
    callbacks.smoke_occludes =
        [](const NinjaSmokeOcclusionQuery&) {
            return true;
        };
    std::vector<NinjaBreakTargetRequest> breaks {};
    callbacks.break_target_lock =
        [&](const NinjaBreakTargetRequest& request) {
            breaks.push_back(request);
        };
    std::vector<NinjaModifierRequest> modifiers {};
    callbacks.apply_modifier =
        [&](const NinjaModifierRequest& request) {
            modifiers.push_back(request);
        };

    REQUIRE(
        effects.cast_smoke_bomb(
                   context(
                       NinjaAbilityRank::RankThree,
                       true),
                   {0.0F, 0.0F, 0.0F},
                   callbacks)
            .cast);
    advance(
        effects,
        1.0F / 60.0F,
        59U,
        callbacks);
    CHECK(breaks.empty());
    const auto sixtieth =
        effects.update(
            1.0F / 60.0F,
            callbacks);
    REQUIRE(sixtieth.accepted);
    CHECK(sixtieth.lock_break_count == 1U);
    REQUIRE(breaks.size() == 1U);
    CHECK(breaks[0].enemy_id == 9U);
    CHECK(breaks[0].target_id == 1U);
    CHECK(modifiers.size() == 120U);
    CHECK(modifiers[0].magnitude == doctest::Approx(0.20F));
    CHECK(modifiers[1].target_id == 2U);
    CHECK(modifiers[1].magnitude == doctest::Approx(0.10F));
}

TEST_CASE("une ligne de vue retrouvée remet le délai du Fumigène à zéro") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    callbacks.entity_snapshot =
        [](NinjaEntityId) {
            return std::optional<NinjaEntitySnapshot> {
                entity(
                    1U,
                    {0.0F, 0.0F, 0.0F},
                    false),
            };
        };
    callbacks.query_entities =
        [](const NinjaEntityQuery& query,
           std::span<NinjaEntitySnapshot> output) {
            if (query.kind !=
                NinjaEntityQueryKind::LockingTarget) {
                return std::size_t {0U};
            }
            output[0] =
                entity(
                    8U,
                    {8.0F, 0.0F, 0.0F});
            output[0].target_id = 1U;
            return std::size_t {1U};
        };
    bool obscured = true;
    callbacks.smoke_occludes =
        [&](const NinjaSmokeOcclusionQuery&) {
            return obscured;
        };
    std::size_t break_count = 0U;
    callbacks.break_target_lock =
        [&](const NinjaBreakTargetRequest&) {
            ++break_count;
        };
    REQUIRE(
        effects.cast_smoke_bomb(
                   context(NinjaAbilityRank::RankOne),
                   {0.0F, 0.0F, 0.0F},
                   callbacks)
            .cast);
    advance(effects, 1.0F / 60.0F, 30U, callbacks);
    obscured = false;
    advance(effects, 1.0F / 60.0F, 1U, callbacks);
    obscured = true;
    advance(effects, 1.0F / 60.0F, 59U, callbacks);
    CHECK(break_count == 0U);
    advance(effects, 1.0F / 60.0F, 1U, callbacks);
    CHECK(break_count == 1U);
}

TEST_CASE("la première attaque sortant de la fumée consomme son bonus exact") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    REQUIRE(
        effects.cast_smoke_bomb(
                   context(NinjaAbilityRank::RankThree),
                   {0.0F, 0.0F, 0.0F},
                   callbacks)
            .cast);
    const NinjaSmokeAttackRequest attack {
        1U,
        7U,
        {0.0F, 0.0F, 0.0F},
        {8.0F, 0.0F, 0.0F},
        true,
    };
    const auto first =
        effects.resolve_smoke_attack(attack);
    REQUIRE(first.empowered);
    CHECK(first.bonus_damage_fraction == doctest::Approx(0.40F));
    CHECK(first.slow_fraction == doctest::Approx(0.20F));
    CHECK(first.slow_duration_seconds == doctest::Approx(2.0F));
    CHECK_FALSE(
        effects.resolve_smoke_attack(attack)
            .empowered);

    NinjaAbilityEffects rank_one {};
    REQUIRE(
        rank_one.cast_smoke_bomb(
                    context(NinjaAbilityRank::RankOne),
                    {0.0F, 0.0F, 0.0F},
                    callbacks)
            .cast);
    CHECK_FALSE(
        rank_one.resolve_smoke_attack(attack)
            .empowered);
}

TEST_CASE("le Bond respecte les usages aériens et l'impulsion gratuite") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    std::vector<float> distances {};
    callbacks.find_safe_movement =
        [&](const NinjaSafeMovementRequest& request) {
            distances.push_back(
                request.maximum_distance);
            return NinjaSafeMovementResult {
                true,
                true,
                request.requested_end,
            };
        };
    const NinjaShinobiLeapCastRequest aerial {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        true,
        false,
    };
    const auto rank_one =
        effects.cast_shinobi_leap(
            context(NinjaAbilityRank::RankOne),
            aerial,
            callbacks);
    CHECK_FALSE(rank_one.moved);
    CHECK(rank_one.failure == NinjaEffectFailure::AirUseUnavailable);

    const auto first =
        effects.cast_shinobi_leap(
            context(
                NinjaAbilityRank::RankThree,
                false,
                2U),
            aerial,
            callbacks);
    REQUIRE(first.moved);
    CHECK(first.consumes_energy_and_cooldown);
    CHECK(first.armed_free_second_impulse);
    CHECK(effects.free_shinobi_impulse_available());
    CHECK(distances.back() == doctest::Approx(6.0F));

    auto free_request = aerial;
    free_request.use_free_second_impulse = true;
    const auto second =
        effects.cast_shinobi_leap(
            context(
                NinjaAbilityRank::RankThree,
                false,
                3U),
            free_request,
            callbacks);
    REQUIRE(second.moved);
    CHECK_FALSE(second.consumes_energy_and_cooldown);
    CHECK(second.requested_distance == doctest::Approx(3.0F));
    CHECK_FALSE(effects.free_shinobi_impulse_available());
    CHECK(
        effects.cast_shinobi_leap(
                   context(
                       NinjaAbilityRank::RankThree,
                       false,
                       4U),
                   free_request,
                   callbacks)
            .failure ==
        NinjaEffectFailure::FreeImpulseUnavailable);

    const auto contact_result =
        effects.handle_contact(
            {
                1U,
                {0.0F, 0.0F, 0.0F},
                NinjaContactKind::Water,
            },
            callbacks);
    CHECK_FALSE(contact_result.mastery_triggered);
    CHECK(
        effects.cast_shinobi_leap(
                   context(
                       NinjaAbilityRank::RankThree,
                       false,
                       5U),
                   aerial,
                   callbacks)
            .moved);
}

TEST_CASE("le Bond s'arrête à la dernière position sûre sans accepter un déplacement hors portée") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    callbacks.find_safe_movement =
        [](const NinjaSafeMovementRequest& request) {
            return NinjaSafeMovementResult {
                true,
                false,
                request.start +
                    glm::vec3 {
                        2.0F,
                        0.0F,
                        0.0F,
                    },
            };
        };
    const auto stopped =
        effects.cast_shinobi_leap(
            context(NinjaAbilityRank::RankOne),
            {
                {0.0F, 0.0F, 0.0F},
                {1.0F, 0.0F, 0.0F},
                false,
                false,
            },
            callbacks);
    REQUIRE(stopped.moved);
    CHECK(stopped.destination.x == doctest::Approx(2.0F));

    callbacks.find_safe_movement =
        [](const NinjaSafeMovementRequest&) {
            return NinjaSafeMovementResult {
                true,
                true,
                {50.0F, 0.0F, 0.0F},
            };
        };
    const auto malicious =
        effects.cast_shinobi_leap(
            context(
                NinjaAbilityRank::RankOne,
                false,
                2U),
            {
                {0.0F, 0.0F, 0.0F},
                {1.0F, 0.0F, 0.0F},
                false,
                false,
            },
            callbacks);
    CHECK_FALSE(malicious.moved);
    CHECK(
        malicious.failure ==
        NinjaEffectFailure::UnsafeDestination);
}

TEST_CASE("le rang deux autorise un seul Bond aérien avant un contact valide") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    const NinjaShinobiLeapCastRequest aerial {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        true,
        false,
    };
    REQUIRE(
        effects.cast_shinobi_leap(
                   context(NinjaAbilityRank::RankTwo),
                   aerial,
                   callbacks)
            .moved);
    CHECK(
        effects.cast_shinobi_leap(
                   context(
                       NinjaAbilityRank::RankTwo,
                       false,
                       2U),
                   aerial,
                   callbacks)
            .failure ==
        NinjaEffectFailure::AirUseUnavailable);
    static_cast<void>(
        effects.handle_contact(
            {
                1U,
                {0.0F, 0.0F, 0.0F},
                NinjaContactKind::ShipDeck,
            },
            callbacks));
    CHECK(
        effects.cast_shinobi_leap(
                   context(
                       NinjaAbilityRank::RankTwo,
                       false,
                       3U),
                   aerial,
                   callbacks)
            .moved);
}

TEST_CASE("l'atterrissage tranchant frappe une fois et annule la chute") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    callbacks.query_entities =
        [](const NinjaEntityQuery& query,
           std::span<NinjaEntitySnapshot> output) {
            if (query.kind !=
                NinjaEntityQueryKind::LandingArea) {
                return std::size_t {0U};
            }
            output[0] =
                entity(
                    3U,
                    {1.0F, 0.0F, 0.0F});
            output[1] = output[0];
            output[2] =
                entity(
                    4U,
                    {3.0F, 0.0F, 0.0F});
            output[3] =
                entity(
                    5U,
                    {1.0F, 0.0F, 0.0F},
                    false);
            return std::size_t {4U};
        };
    std::vector<NinjaDamageRequest> damage {};
    callbacks.apply_damage =
        [&](const NinjaDamageRequest& request) {
            damage.push_back(request);
            return NinjaDamageResult {
                true,
                false,
                request.damage,
            };
        };
    auto mastery_context =
        context(
            NinjaAbilityRank::RankThree,
            true);
    mastery_context.player_level = 50U;
    mastery_context.agility = 10.0F;
    REQUIRE(
        effects.cast_shinobi_leap(
                   mastery_context,
                   {
                       {0.0F, 0.0F, 0.0F},
                       {1.0F, 0.0F, 0.0F},
                       false,
                       false,
                   },
                   callbacks)
            .moved);
    const auto landing =
        effects.handle_contact(
            {
                1U,
                {0.0F, 0.0F, 0.0F},
                NinjaContactKind::ShipDeck,
            },
            callbacks);
    REQUIRE(landing.mastery_triggered);
    CHECK(landing.cancel_fall_damage);
    CHECK(landing.hit_count == 1U);
    REQUIRE(damage.size() == 1U);
    CHECK(
        damage[0].damage ==
        doctest::Approx(
            ninja_spell_damage(
                5.0F,
                50U,
                10.0F)));
    CHECK_FALSE(
        effects.handle_contact(
                   {
                       1U,
                       {0.0F, 0.0F, 0.0F},
                       NinjaContactKind::Ground,
                   },
                   callbacks)
            .mastery_triggered);
}

TEST_CASE("la maîtrise du Bond expire exactement après deux secondes") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    REQUIRE(
        effects.cast_shinobi_leap(
                   context(
                       NinjaAbilityRank::RankThree,
                       true),
                   {
                       {0.0F, 0.0F, 0.0F},
                       {1.0F, 0.0F, 0.0F},
                       false,
                       false,
                   },
                   callbacks)
            .moved);
    advance(effects, 1.0F / 60.0F, 120U);
    CHECK_FALSE(
        effects.handle_contact(
                   {
                       1U,
                       {1.0F, 0.0F, 0.0F},
                       NinjaContactKind::Ground,
                   },
                   callbacks)
            .mastery_triggered);
}

TEST_CASE("la Ruée frappe chaque cible distincte et réduit la recharge sur élimination") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    callbacks.query_entities =
        [](const NinjaEntityQuery& query,
           std::span<NinjaEntitySnapshot> output) {
            REQUIRE(
                query.kind ==
                NinjaEntityQueryKind::DashPath);
            output[0] =
                entity(
                    5U,
                    {3.0F, 0.0F, 0.0F});
            output[1] =
                entity(
                    2U,
                    {2.0F, 0.0F, 0.0F});
            output[2] = output[1];
            output[3] =
                entity(
                    7U,
                    {2.0F, 0.0F, 3.0F});
            return std::size_t {4U};
        };
    callbacks.apply_damage =
        [](const NinjaDamageRequest& request) {
            return NinjaDamageResult {
                true,
                request.target_id == 2U,
                request.damage,
            };
        };
    std::vector<NinjaCooldownReductionRequest> reductions {};
    callbacks.reduce_cooldown =
        [&](const NinjaCooldownReductionRequest& request) {
            reductions.push_back(request);
        };
    const auto result =
        effects.cast_lightning_dash(
            context(NinjaAbilityRank::RankTwo),
            {
                {0.0F, 0.0F, 0.0F},
                {1.0F, 0.0F, 0.0F},
                std::nullopt,
            },
            callbacks);
    REQUIRE(result.moved);
    CHECK(result.hit_count == 2U);
    CHECK(result.kill_count == 1U);
    CHECK(result.hits[0].target_id == 2U);
    CHECK(result.hits[1].target_id == 5U);
    CHECK(result.hits[0].requested_damage == doctest::Approx(9.0F));
    REQUIRE(reductions.size() == 1U);
    CHECK(
        reductions[0].remaining_fraction_to_remove ==
        doctest::Approx(0.40F));
}

TEST_CASE("la Ruée de rang trois privilégie une position sûre derrière la cible") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    NinjaSafeMovementKind observed_kind =
        NinjaSafeMovementKind::LightningDash;
    glm::vec3 observed_end {0.0F};
    callbacks.find_safe_movement =
        [&](const NinjaSafeMovementRequest& request) {
            observed_kind = request.kind;
            observed_end = request.requested_end;
            return NinjaSafeMovementResult {
                true,
                true,
                request.requested_end,
            };
        };
    auto target =
        entity(
            8U,
            {5.0F, 0.0F, 0.0F});
    target.forward = {1.0F, 0.0F, 0.0F};
    const auto result =
        effects.cast_lightning_dash(
            context(NinjaAbilityRank::RankThree),
            {
                {0.0F, 0.0F, 0.0F},
                {1.0F, 0.0F, 0.0F},
                target,
            },
            callbacks);
    REQUIRE(result.moved);
    CHECK(
        observed_kind ==
        NinjaSafeMovementKind::LightningDashBehindTarget);
    CHECK(observed_end.x == doctest::Approx(4.0F));
    CHECK(result.destination.x == doctest::Approx(4.0F));
}

TEST_CASE("Double éclair possède deux charges séparées et bloque un double impact pendant 0,75 seconde") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    callbacks.query_entities =
        [](const NinjaEntityQuery&,
           std::span<NinjaEntitySnapshot> output) {
            output[0] =
                entity(
                    9U,
                    {2.0F, 0.0F, 0.0F});
            return std::size_t {1U};
        };
    const auto mastered =
        context(
            NinjaAbilityRank::RankThree,
            true);
    const NinjaLightningDashCastRequest request {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        std::nullopt,
    };
    const auto first =
        effects.cast_lightning_dash(
            mastered,
            request,
            callbacks);
    REQUIRE(first.moved);
    CHECK(first.hit_count == 1U);
    CHECK(first.remaining_mastery_charges == 1U);
    const auto second =
        effects.cast_lightning_dash(
            context(
                NinjaAbilityRank::RankThree,
                true,
                2U),
            request,
            callbacks);
    REQUIRE(second.moved);
    CHECK(second.hit_count == 0U);
    CHECK(second.remaining_mastery_charges == 0U);
    CHECK(
        effects.cast_lightning_dash(
                   context(
                       NinjaAbilityRank::RankThree,
                       true,
                       3U),
                   request,
                   callbacks)
            .failure ==
        NinjaEffectFailure::NoCharge);

    advance(effects, 1.0F / 60.0F, 720U);
    CHECK(effects.available_lightning_dash_charges() == 2U);
    const auto after_recharge =
        effects.cast_lightning_dash(
            context(
                NinjaAbilityRank::RankThree,
                true,
                4U),
            request,
            callbacks);
    REQUIRE(after_recharge.moved);
    CHECK(after_recharge.hit_count == 1U);
}

TEST_CASE("une élimination avec Double éclair réduit de moitié le timer de sa charge") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    callbacks.query_entities =
        [](const NinjaEntityQuery&,
           std::span<NinjaEntitySnapshot> output) {
            output[0] =
                entity(
                    2U,
                    {2.0F, 0.0F, 0.0F});
            return std::size_t {1U};
        };
    callbacks.apply_damage =
        [](const NinjaDamageRequest& request) {
            return NinjaDamageResult {
                true,
                true,
                request.damage,
            };
        };
    REQUIRE(
        effects.cast_lightning_dash(
                   context(
                       NinjaAbilityRank::RankThree,
                       true),
                   {
                       {0.0F, 0.0F, 0.0F},
                       {1.0F, 0.0F, 0.0F},
                       std::nullopt,
                   },
                   callbacks)
            .moved);
    CHECK(effects.available_lightning_dash_charges() == 1U);
    advance(effects, 1.0F / 60.0F, 360U);
    CHECK(effects.available_lightning_dash_charges() == 2U);
}

TEST_CASE("un mouvement refusé ne consomme aucune charge de Ruée") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    callbacks.commit_movement =
        [](const NinjaMovementCommit&) {
            return false;
        };
    const auto result =
        effects.cast_lightning_dash(
            context(
                NinjaAbilityRank::RankThree,
                true),
            {
                {0.0F, 0.0F, 0.0F},
                {1.0F, 0.0F, 0.0F},
                std::nullopt,
            },
            callbacks);
    CHECK_FALSE(result.moved);
    CHECK(result.failure == NinjaEffectFailure::MovementRejected);
    CHECK(effects.available_lightning_dash_charges() == 2U);
}

TEST_CASE("le Kunaï applique les pertes de puissance et une seule marque") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    callbacks.entity_snapshot =
        [](NinjaEntityId id)
            -> std::optional<NinjaEntitySnapshot> {
            return entity(
                id,
                {
                    static_cast<float>(id),
                    0.0F,
                    0.0F,
                });
        };
    callbacks.select_kunai_target =
        [](const NinjaKunaiTargetQuery& query)
            -> std::optional<NinjaEntitySnapshot> {
            const auto candidate =
                static_cast<NinjaEntityId>(
                    query.bounce_index + 2U);
            return entity(
                candidate,
                {
                    static_cast<float>(candidate),
                    0.0F,
                    0.0F,
                });
        };
    const auto result =
        effects.cast_spectral_kunai(
            context(NinjaAbilityRank::RankThree),
            {2U},
            callbacks);
    REQUIRE(result.cast);
    CHECK(result.outward_hit_count == 4U);
    CHECK(result.return_hit_count == 0U);
    CHECK(result.mark_applied);
    CHECK(result.hits[0].requested_damage == doctest::Approx(11.0F));
    CHECK(result.hits[1].requested_damage == doctest::Approx(9.35F));
    CHECK(result.hits[2].requested_damage == doctest::Approx(7.9475F));
    CHECK(result.hits[3].requested_damage == doctest::Approx(6.755375F));
    CHECK(effects.active_mark_count() == 1U);

    const auto mark =
        effects.consume_spectral_mark({
            1U,
            2U,
            20.0F,
            true,
        });
    REQUIRE(mark.consumed);
    CHECK(mark.bonus_damage_fraction == doctest::Approx(0.50F));
    CHECK(mark.bonus_damage == doctest::Approx(10.0F));
    CHECK_FALSE(
        effects.consume_spectral_mark({
                   1U,
                   2U,
                   20.0F,
                   true,
               })
            .consumed);
}

TEST_CASE("chaque rang du Kunaï réalise exactement son nombre de rebonds") {
    for (const auto rank : {
             NinjaAbilityRank::RankOne,
             NinjaAbilityRank::RankTwo,
             NinjaAbilityRank::RankThree,
         }) {
        NinjaAbilityEffects effects {};
        auto callbacks = base_callbacks();
        callbacks.entity_snapshot =
            [](NinjaEntityId id)
                -> std::optional<NinjaEntitySnapshot> {
                return entity(
                    id,
                    {
                        static_cast<float>(id),
                        0.0F,
                        0.0F,
                    });
            };
        callbacks.select_kunai_target =
            [](const NinjaKunaiTargetQuery& query)
                -> std::optional<NinjaEntitySnapshot> {
                const auto candidate =
                    static_cast<NinjaEntityId>(
                        query.bounce_index + 2U);
                return entity(
                    candidate,
                    {
                        static_cast<float>(
                            candidate),
                        0.0F,
                        0.0F,
                    });
            };
        const auto tuning =
            ninja_spectral_kunai_tuning(rank);
        const auto result =
            effects.cast_spectral_kunai(
                context(rank),
                {2U},
                callbacks);
        REQUIRE(result.cast);
        REQUIRE(
            result.outward_hit_count ==
            static_cast<std::size_t>(
                tuning.bounce_count) +
                1U);
        CHECK(
            result.hits[0].requested_damage ==
            doctest::Approx(
                tuning.base_damage));
        if (result.outward_hit_count > 1U) {
            CHECK(
                result.hits[1]
                    .requested_damage ==
                doctest::Approx(
                    tuning.base_damage *
                    (1.0F -
                     tuning.bounce_power_loss)));
        }
    }
}

TEST_CASE("Retour spectral reparcourt la chaîne à 60 pour cent sans nouvelle marque") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    callbacks.entity_snapshot =
        [](NinjaEntityId id)
            -> std::optional<NinjaEntitySnapshot> {
            return entity(
                id,
                {
                    static_cast<float>(id),
                    0.0F,
                    0.0F,
                });
        };
    callbacks.select_kunai_target =
        [](const NinjaKunaiTargetQuery& query)
            -> std::optional<NinjaEntitySnapshot> {
            const auto candidate =
                static_cast<NinjaEntityId>(
                    query.bounce_index + 2U);
            return entity(
                candidate,
                {
                    static_cast<float>(candidate),
                    0.0F,
                    0.0F,
                });
        };
    const auto result =
        effects.cast_spectral_kunai(
            context(
                NinjaAbilityRank::RankThree,
                true),
            {2U},
            callbacks);
    REQUIRE(result.cast);
    REQUIRE(result.hit_count == 8U);
    CHECK(result.outward_hit_count == 4U);
    CHECK(result.return_hit_count == 4U);
    CHECK(result.hits[4].target_id == 5U);
    CHECK(
        result.hits[4].requested_damage ==
        doctest::Approx(
            result.hits[3].requested_damage *
            0.60F));
    CHECK(result.hits[7].target_id == 2U);
    CHECK(
        result.hits[7].requested_damage ==
        doctest::Approx(6.60F));
    CHECK(effects.active_mark_count() == 1U);
}

TEST_CASE("le trajet aller du Kunaï refuse une cible déjà touchée") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    callbacks.select_kunai_target =
        [](const NinjaKunaiTargetQuery&)
            -> std::optional<NinjaEntitySnapshot> {
            return entity(
                2U,
                {2.0F, 0.0F, 0.0F});
        };
    const auto result =
        effects.cast_spectral_kunai(
            context(NinjaAbilityRank::RankThree),
            {2U},
            callbacks);
    REQUIRE(result.cast);
    CHECK(result.outward_hit_count == 1U);
}

TEST_CASE("la marque spectrale expire après six secondes de simulation fixe") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    REQUIRE(
        effects.cast_spectral_kunai(
                   context(NinjaAbilityRank::RankOne),
                   {2U},
                   callbacks)
            .mark_applied);
    CHECK(effects.active_mark_count() == 1U);
    advance(effects, 1.0F / 60.0F, 359U);
    CHECK(effects.active_mark_count() == 1U);
    advance(effects, 1.0F / 60.0F, 1U);
    CHECK(effects.active_mark_count() == 0U);
}

TEST_CASE("les charges de Ruée se rechargent pareil à 30 60 et 144 images par seconde") {
    auto callbacks = base_callbacks();
    const NinjaLightningDashCastRequest request {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        std::nullopt,
    };
    const auto mastered =
        context(
            NinjaAbilityRank::RankThree,
            true);
    NinjaAbilityEffects at_thirty {};
    NinjaAbilityEffects at_sixty {};
    NinjaAbilityEffects at_144 {};
    REQUIRE(
        at_thirty.cast_lightning_dash(
                      mastered,
                      request,
                      callbacks)
            .moved);
    REQUIRE(
        at_sixty.cast_lightning_dash(
                     mastered,
                     request,
                     callbacks)
            .moved);
    REQUIRE(
        at_144.cast_lightning_dash(
                   mastered,
                   request,
                   callbacks)
            .moved);
    advance(at_thirty, 1.0F / 30.0F, 360U);
    advance(at_sixty, 1.0F / 60.0F, 720U);
    advance(at_144, 1.0F / 144.0F, 1728U);
    CHECK(
        at_thirty
            .available_lightning_dash_charges() ==
        2U);
    CHECK(
        at_sixty
            .available_lightning_dash_charges() ==
        2U);
    CHECK(
        at_144
            .available_lightning_dash_charges() ==
        2U);
}

TEST_CASE("les timers de Double éclair se restaurent transactionnellement") {
    NinjaAbilityEffects effects {};
    NinjaLightningDashChargeState saved {};
    saved.remaining_recharge_seconds = {
        3.25F,
        12.0F,
    };
    REQUIRE(
        effects.restore_lightning_dash_charge_state(
            saved));
    const auto restored =
        effects.lightning_dash_charge_state();
    CHECK(
        restored.remaining_recharge_seconds[0] ==
        doctest::Approx(3.25F));
    CHECK(
        restored.remaining_recharge_seconds[1] ==
        doctest::Approx(12.0F));
    CHECK(effects.available_lightning_dash_charges() == 0U);

    auto invalid = saved;
    invalid.remaining_recharge_seconds[0] =
        std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(
        effects.restore_lightning_dash_charge_state(
            invalid));
    const auto preserved =
        effects.lightning_dash_charge_state();
    CHECK(
        preserved.remaining_recharge_seconds[0] ==
        doctest::Approx(3.25F));
    CHECK(
        preserved.remaining_recharge_seconds[1] ==
        doctest::Approx(12.0F));
}

TEST_CASE("les entrées non finies et les grands retards restent bornés") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    const auto invalid_context =
        NinjaCastContext {
            1U,
            1U,
            NinjaAbilityRank::RankOne,
            1U,
            std::numeric_limits<float>::quiet_NaN(),
            false,
        };
    CHECK_FALSE(
        effects.cast_smoke_bomb(
                   invalid_context,
                   {0.0F, 0.0F, 0.0F},
                   callbacks)
            .cast);
    CHECK_FALSE(
        effects.cast_shinobi_leap(
                   context(NinjaAbilityRank::RankOne),
                   {
                       {0.0F, 0.0F, 0.0F},
                       {
                           std::numeric_limits<float>::infinity(),
                           0.0F,
                           0.0F,
                       },
                       false,
                       false,
                   },
                   callbacks)
            .moved);
    CHECK_FALSE(
        effects.update(
                   std::numeric_limits<float>::quiet_NaN(),
                   callbacks)
            .accepted);
    const auto bounded =
        effects.update(1000.0F, callbacks);
    CHECK(bounded.accepted);
    CHECK(bounded.saturated);
    CHECK(
        bounded.simulated_ticks ==
        kNinjaMaximumTicksPerUpdate);
}

TEST_CASE("une requête de Ruée surchargée est tronquée à sa capacité fixe") {
    NinjaAbilityEffects effects {};
    auto callbacks = base_callbacks();
    callbacks.query_entities =
        [](const NinjaEntityQuery&,
           std::span<NinjaEntitySnapshot> output) {
            for (std::size_t index = 0U;
                 index < output.size();
                 ++index) {
                output[index] =
                    entity(
                        static_cast<NinjaEntityId>(
                            index + 2U),
                        {
                            1.0F +
                                static_cast<float>(
                                    index % 5U),
                            0.0F,
                            0.0F,
                        });
            }
            return output.size() + 500U;
        };
    const auto result =
        effects.cast_lightning_dash(
            context(NinjaAbilityRank::RankOne),
            {
                {0.0F, 0.0F, 0.0F},
                {1.0F, 0.0F, 0.0F},
                std::nullopt,
            },
            callbacks);
    REQUIRE(result.moved);
    CHECK(result.query_saturated);
    CHECK(result.hit_count == kNinjaMaximumQueryEntities);
}

} // namespace valcraft
