#include "render/weapons/LeviathanWeaponPresentation.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto matrix_distance(
    const glm::mat4& left,
    const glm::mat4& right) noexcept -> float {
    auto result = 0.0F;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result += std::abs(
                left[column][row] - right[column][row]);
        }
    }
    return result;
}

[[nodiscard]] auto all_parts_are_finite(
    const std::vector<LeviathanWeaponPartInstance>& parts) noexcept
    -> bool {
    return std::all_of(
        parts.begin(), parts.end(),
        [](const LeviathanWeaponPartInstance& part) noexcept {
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    if (!std::isfinite(part.transform[column][row])) {
                        return false;
                    }
                }
            }
            return std::isfinite(part.roughness) &&
                   std::isfinite(part.emissive_strength);
        });
}

} // namespace

TEST_CASE("l echine conserve exactement sa longueur monumentale de 2.2 blocs") {
    const auto parts = build_leviathan_weapon_model(
        LegendaryWeaponAwakening::Dormant);
    const auto bounds = leviathan_weapon_local_bounds(parts);

    REQUIRE_FALSE(parts.empty());
    CHECK(bounds.minimum.y == doctest::Approx(0.0F).epsilon(0.0001));
    CHECK(
        bounds.maximum.y - bounds.minimum.y ==
        doctest::Approx(kLeviathanWeaponVisualLengthBlocks)
            .epsilon(0.0001));
    CHECK((bounds.maximum.x - bounds.minimum.x) > 0.60F);
    CHECK(all_parts_are_finite(parts));
}

TEST_CASE("chaque eveil ajoute une matiere lisible sans changer la silhouette") {
    const auto dormant = build_leviathan_weapon_model(
        LegendaryWeaponAwakening::Dormant);
    const auto corrupted = build_leviathan_weapon_model(
        LegendaryWeaponAwakening::Corrupted);
    const auto astral = build_leviathan_weapon_model(
        LegendaryWeaponAwakening::Astral);
    const auto awakened = build_leviathan_weapon_model(
        LegendaryWeaponAwakening::Awakened);

    CHECK(corrupted.size() == dormant.size() + 5U);
    CHECK(astral.size() == corrupted.size());
    CHECK(awakened.size() == corrupted.size());
    const auto count_material =
        [](const auto& parts, LeviathanVisualMaterial material) {
            return std::count_if(
                parts.begin(), parts.end(),
                [material](const LeviathanWeaponPartInstance& part) {
                    return part.material == material &&
                           part.emissive_strength > 0.0F;
                });
        };
    CHECK(
        count_material(
            corrupted, LeviathanVisualMaterial::CorruptedVein) == 5);
    CHECK(
        count_material(
            astral, LeviathanVisualMaterial::AstralRune) == 5);
    CHECK(
        count_material(
            awakened, LeviathanVisualMaterial::SovereignCore) == 5);

    const auto dormant_bounds =
        leviathan_weapon_local_bounds(dormant);
    const auto awakened_bounds =
        leviathan_weapon_local_bounds(awakened);
    CHECK(
        awakened_bounds.maximum.y - awakened_bounds.minimum.y ==
        doctest::Approx(
            dormant_bounds.maximum.y - dormant_bounds.minimum.y));
}

TEST_CASE("les poses premiere personne dos garde et attaques restent distinctes") {
    LeviathanWeaponVisualInput input {};
    input.state = ColossalWeaponState::Idle;
    input.view_mode = LeviathanViewMode::FirstPerson;
    const auto first_person = solve_leviathan_weapon_pose(input);

    input.view_mode = LeviathanViewMode::ThirdPerson;
    const auto third_person = solve_leviathan_weapon_pose(input);
    CHECK(matrix_distance(
              first_person.root_transform,
              third_person.root_transform) > 0.5F);

    input.state = ColossalWeaponState::Holstered;
    const auto on_back = solve_leviathan_weapon_pose(input);
    CHECK(on_back.carried_on_back);
    CHECK(on_back.visible);

    input.state = ColossalWeaponState::Guard;
    const auto guard = solve_leviathan_weapon_pose(input);
    CHECK_FALSE(guard.carried_on_back);
    CHECK(matrix_distance(
              guard.root_transform,
              third_person.root_transform) > 0.5F);

    auto previous = guard.root_transform;
    for (const auto attack : {
             ColossalAttackKind::FirstSweep,
             ColossalAttackKind::SecondSweep,
             ColossalAttackKind::Earthbreaker,
             ColossalAttackKind::RunningCleave,
             ColossalAttackKind::ChargedExecution,
         }) {
        input.state = ColossalWeaponState::Active;
        input.attack = attack;
        input.state_progress = 0.65F;
        const auto attack_pose = solve_leviathan_weapon_pose(input);
        CAPTURE(static_cast<int>(attack));
        CHECK(matrix_distance(
                  attack_pose.root_transform, previous) > 0.08F);
        CHECK(
            glm::distance(
                attack_pose.primary_hand_anchor,
                attack_pose.secondary_hand_anchor) ==
            doctest::Approx(0.26F).epsilon(0.001));
        CHECK(all_parts_are_finite(attack_pose.parts));
        previous = attack_pose.root_transform;
    }
}

TEST_CASE("le rangement masque uniquement la vue subjective") {
    LeviathanWeaponVisualInput input {};
    input.state = ColossalWeaponState::Holstered;
    input.view_mode = LeviathanViewMode::FirstPerson;
    CHECK_FALSE(solve_leviathan_weapon_pose(input).visible);

    input.view_mode = LeviathanViewMode::ThirdPerson;
    CHECK(solve_leviathan_weapon_pose(input).visible);
}

TEST_CASE("les impacts produisent des gels strictement visuels dans les bornes du plan") {
    CHECK(
        leviathan_visual_hit_stop_seconds(
            LeviathanImpactWeight::Light) >= 0.030F);
    CHECK(
        leviathan_visual_hit_stop_seconds(
            LeviathanImpactWeight::Light) <= 0.045F);
    CHECK(
        leviathan_visual_hit_stop_seconds(
            LeviathanImpactWeight::Heavy) >= 0.050F);
    CHECK(
        leviathan_visual_hit_stop_seconds(
            LeviathanImpactWeight::Heavy) <= 0.070F);
    CHECK(
        leviathan_visual_hit_stop_seconds(
            LeviathanImpactWeight::BossOrSection) >= 0.070F);
    CHECK(
        leviathan_visual_hit_stop_seconds(
            LeviathanImpactWeight::BossOrSection) <= 0.090F);

    LeviathanCombatVisualRequest request {};
    request.attack = ColossalAttackKind::ChargedExecution;
    request.attack_progress = 0.80F;
    request.landed = true;
    request.sectioned = true;
    request.weight = LeviathanImpactWeight::Heavy;
    const auto events = build_leviathan_visual_events(request);
    CHECK(events.size() == 5U);
    CHECK(std::all_of(
        events.begin(), events.end(),
        [](const LeviathanVisualEvent& event) {
            return event.visual_only &&
                   std::isfinite(event.duration_seconds) &&
                   event.duration_seconds >= 0.0F;
        }));
    const auto stop = std::find_if(
        events.begin(), events.end(),
        [](const LeviathanVisualEvent& event) {
            return event.kind ==
                   LeviathanVisualEventKind::VisualHitStop;
        });
    REQUIRE(stop != events.end());
    CHECK(stop->duration_seconds == doctest::Approx(0.082F));
}

TEST_CASE("l accessibilite supprime toute impulsion camera sans supprimer l impact") {
    LeviathanCombatVisualRequest request {};
    request.attack = ColossalAttackKind::Earthbreaker;
    request.attack_progress = 1.0F;
    request.landed = true;
    request.accessibility.reduced_motion = true;
    request.accessibility.reduced_flashes = true;
    const auto events = build_leviathan_visual_events(request);

    CHECK(std::none_of(
        events.begin(), events.end(),
        [](const LeviathanVisualEvent& event) {
            return event.kind ==
                   LeviathanVisualEventKind::CameraImpulse;
        }));
    CHECK(std::any_of(
        events.begin(), events.end(),
        [](const LeviathanVisualEvent& event) {
            return event.kind ==
                   LeviathanVisualEventKind::ImpactBurst;
        }));
    CHECK(std::any_of(
        events.begin(), events.end(),
        [](const LeviathanVisualEvent& event) {
            return event.kind ==
                   LeviathanVisualEventKind::VisualHitStop;
        }));
}

TEST_CASE("les entrees visuelles non finies sont assainies") {
    LeviathanWeaponVisualInput input {};
    input.state = ColossalWeaponState::Charge;
    input.state_progress =
        std::numeric_limits<float>::quiet_NaN();
    input.charge_progress =
        std::numeric_limits<float>::infinity();
    input.animation_time_seconds =
        -std::numeric_limits<float>::infinity();
    input.awakening =
        static_cast<LegendaryWeaponAwakening>(255U);
    input.actor_transform[0][0] =
        std::numeric_limits<float>::quiet_NaN();

    const auto pose = solve_leviathan_weapon_pose(input);
    CHECK(all_parts_are_finite(pose.parts));
    CHECK(std::isfinite(pose.primary_hand_anchor.x));
    CHECK(std::isfinite(pose.secondary_hand_anchor.y));

    LeviathanCombatVisualRequest request {};
    request.attack = ColossalAttackKind::FirstSweep;
    request.attack_progress = 1.0F;
    request.landed = true;
    request.origin = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        0.0F,
    };
    const auto events = build_leviathan_visual_events(request);
    CHECK(std::all_of(
        events.begin(), events.end(),
        [](const LeviathanVisualEvent& event) {
            return std::isfinite(event.position.x) &&
                   std::isfinite(event.position.y) &&
                   std::isfinite(event.position.z);
        }));
}

} // namespace valcraft
