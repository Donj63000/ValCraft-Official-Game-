#include "render/scenarios/IssouArenaPresentation.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto make_test_layout() -> IssouArenaLayout {
    IssouArenaLayout layout {};
    layout.seed = 1337;
    layout.floor_y = 72;
    layout.combat_bounds = {-25, 26, 72, 84, -19, 20};
    layout.protected_bounds = {-31, 32, 70, 88, -25, 26};
    layout.player_spawn = {0.5F, 73.01F, 15.5F};
    layout.colossus_spawn = {0.5F, 73.01F, -5.5F};
    layout.braziers = {
        {-20, 73, -16},
        {20, 73, 16},
    };
    layout.gate_cells = {
        {-1, 73, -20},
        {0, 73, -20},
    };
    layout.chain_anchors = {
        {-12.0F, 76.0F, -8.0F},
        {12.0F, 76.0F, -8.0F},
    };
    return layout;
}

[[nodiscard]] auto crowds_match(
    const std::vector<IssouCrowdInstance>& left,
    const std::vector<IssouCrowdInstance>& right) noexcept -> bool {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto& lhs = left[index];
        const auto& rhs = right[index];
        if (lhs.position != rhs.position ||
            lhs.yaw_radians != rhs.yaw_radians ||
            lhs.animation_phase != rhs.animation_phase ||
            lhs.reaction_amount != rhs.reaction_amount ||
            lhs.cloth_color != rhs.cloth_color ||
            lhs.reaction != rhs.reaction ||
            lhs.lod != rhs.lod ||
            lhs.instance_id != rhs.instance_id ||
            lhs.variant != rhs.variant) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto count_decor(
    const std::vector<IssouArenaDecorInstance>& decor,
    IssouArenaDecorKind kind) noexcept -> std::size_t {
    return static_cast<std::size_t>(std::count_if(
        decor.begin(), decor.end(),
        [kind](const IssouArenaDecorInstance& instance) {
            return instance.kind == kind;
        }));
}

} // namespace

TEST_CASE("la foule instanciee reste entre cent et cent quatre vingt") {
    const auto layout = make_test_layout();
    IssouCrowdRequest request {};
    request.desired_count = 1U;
    request.variant_count = 1U;
    const auto minimum = build_issou_crowd(layout, request);
    CHECK(minimum.size() == kIssouMinimumCrowdSize);
    CHECK(issou_crowd_variant_count(minimum) ==
          kIssouMinimumCrowdVariants);

    request.desired_count = 9'999U;
    request.variant_count = 255U;
    const auto maximum = build_issou_crowd(layout, request);
    CHECK(maximum.size() == kIssouMaximumCrowdSize);
    CHECK(issou_crowd_variant_count(maximum) ==
          kIssouMaximumCrowdVariants);
}

TEST_CASE("les huit a seize variantes de public sont deterministes") {
    const auto layout = make_test_layout();
    IssouCrowdRequest request {};
    request.seed = 0xC011055EU;
    request.desired_count = 156U;
    request.variant_count = 14U;
    request.animation_seconds = 3.25F;
    request.excitement = 0.82F;
    request.latest_event = IssouArenaEventKind::CrowdRoar;

    const auto first = build_issou_crowd(layout, request);
    const auto second = build_issou_crowd(layout, request);
    REQUIRE(crowds_match(first, second));
    CHECK(issou_crowd_variant_count(first) == 14U);
    CHECK(std::all_of(
        first.begin(), first.end(),
        [](const IssouCrowdInstance& instance) {
            return instance.variant < 14U &&
                   instance.reaction ==
                       IssouCrowdReaction::Roar &&
                   instance.reaction_amount >= 0.0F &&
                   instance.reaction_amount <= 1.0F;
        }));
}

TEST_CASE("la reduction de mouvement calme la foule sans changer son implantation") {
    const auto layout = make_test_layout();
    IssouCrowdRequest request {};
    request.seed = 72U;
    request.excitement = 1.0F;
    request.latest_event = IssouArenaEventKind::CrowdCheer;
    const auto animated = build_issou_crowd(layout, request);

    request.reduced_motion = true;
    const auto reduced = build_issou_crowd(layout, request);
    REQUIRE(animated.size() == reduced.size());
    for (std::size_t index = 0U; index < animated.size(); ++index) {
        CHECK(animated[index].position == reduced[index].position);
        CHECK(animated[index].variant == reduced[index].variant);
        CHECK(
            reduced[index].reaction_amount <=
            animated[index].reaction_amount * 0.181F);
    }
}

TEST_CASE("les niveaux de detail produisent six trois ou zero volumes") {
    IssouCrowdInstance instance {};
    instance.lod = IssouCrowdLod::Full;
    CHECK(build_issou_crowd_member_parts(instance).size() == 6U);

    instance.lod = IssouCrowdLod::Simplified;
    CHECK(build_issou_crowd_member_parts(instance).size() == 3U);
    instance.lod = IssouCrowdLod::Impostor;
    CHECK(build_issou_crowd_member_parts(instance).size() == 3U);
    instance.lod = IssouCrowdLod::Culled;
    CHECK(build_issou_crowd_member_parts(instance).empty());
}

TEST_CASE("une camera lointaine bascule proprement le public en culling") {
    IssouCrowdRequest request {};
    request.camera_position = {2'000.0F, 2'000.0F, 2'000.0F};
    const auto crowd =
        build_issou_crowd(make_test_layout(), request);
    CHECK(std::all_of(
        crowd.begin(), crowd.end(),
        [](const IssouCrowdInstance& instance) {
            return instance.lod == IssouCrowdLod::Culled;
        }));
}

TEST_CASE("le decor contient gradins bannieres brasiers porte et chaines") {
    IssouArenaState state {};
    state.layout = make_test_layout();
    state.chains_visible = true;
    const auto chained = build_issou_arena_decor(state);

    CHECK(count_decor(chained, IssouArenaDecorKind::Stand) == 4U);
    CHECK(count_decor(chained, IssouArenaDecorKind::Banner) == 4U);
    CHECK(count_decor(chained, IssouArenaDecorKind::Brazier) == 2U);
    CHECK(count_decor(chained, IssouArenaDecorKind::Gate) == 2U);
    CHECK(
        count_decor(chained, IssouArenaDecorKind::ChainLink) == 24U);

    state.chains_visible = false;
    const auto freed = build_issou_arena_decor(state);
    CHECK(count_decor(freed, IssouArenaDecorKind::ChainLink) == 0U);
    CHECK(freed.size() + 24U == chained.size());
}

TEST_CASE("le hud de combat reste discret borne et lisible") {
    IssouArenaHudInput input {};
    input.phase = IssouArenaPhase::Combat;
    input.viewport_width = 1920.0F;
    input.viewport_height = 1080.0F;
    input.boss_health_ratio = 0.65F;
    input.boss_stagger_ratio = 0.40F;
    input.weapon_stability_ratio = 0.75F;
    input.charge_ratio = 0.50F;
    input.momentum = 3U;
    input.accessibility.high_contrast = true;
    const auto hud = build_issou_arena_hud(input);

    REQUIRE(hud.size() == 6U);
    CHECK(
        std::count_if(
            hud.begin(), hud.end(),
            [](const IssouHudElement& element) {
                return element.visible;
            }) == 5);
    for (const auto& element : hud) {
        CHECK(element.value >= 0.0F);
        CHECK(element.value <= 1.0F);
        CHECK(element.rect.x >= 0.0F);
        CHECK(element.rect.y >= 0.0F);
        CHECK(
            element.rect.x + element.rect.width <=
            input.viewport_width);
        CHECK(
            element.rect.y + element.rect.height <=
            input.viewport_height);
        CHECK(element.background.w >= 0.45F);
    }
}

TEST_CASE("le compte a rebours remplace seul les barres de combat") {
    IssouArenaHudInput input {};
    input.phase = IssouArenaPhase::Countdown;
    input.countdown_seconds = 7.5F;
    const auto hud = build_issou_arena_hud(input);
    const auto visible = std::count_if(
        hud.begin(), hud.end(),
        [](const IssouHudElement& element) {
            return element.visible;
        });
    CHECK(visible == 1);
    const auto countdown = std::find_if(
        hud.begin(), hud.end(),
        [](const IssouHudElement& element) {
            return element.kind ==
                   IssouHudElementKind::Countdown;
        });
    REQUIRE(countdown != hud.end());
    CHECK(countdown->visible);
    CHECK(countdown->secondary_value == doctest::Approx(7.5F));
}

TEST_CASE("l ecran de resultats restitue toutes les statistiques") {
    IssouArenaCombatStatistics statistics {};
    statistics.combat_seconds = 42.5F;
    statistics.damage_dealt = 420.0F;
    statistics.limbs_severed = 3U;
    statistics.perfect_guards = 2U;
    statistics.missed_attacks = 1U;
    statistics.maximum_momentum = 3U;
    statistics.maximum_targets_hit = 6U;
    statistics.executed = true;

    const auto results = build_issou_results(statistics, true);
    CHECK(results.victory);
    CHECK(results.executed);
    REQUIRE(results.lines.size() == 7U);
    CHECK(results.lines.front().value == doctest::Approx(42.5F));
    CHECK(std::count_if(
              results.lines.begin(), results.lines.end(),
              [](const IssouResultLine& line) {
                  return line.highlight;
              }) >= 4);
}

TEST_CASE("le hud assainit les dimensions et valeurs non finies") {
    IssouArenaHudInput input {};
    input.phase = IssouArenaPhase::Combat;
    input.viewport_width =
        std::numeric_limits<float>::quiet_NaN();
    input.viewport_height =
        std::numeric_limits<float>::infinity();
    input.boss_health_ratio =
        std::numeric_limits<float>::infinity();
    input.charge_ratio =
        std::numeric_limits<float>::quiet_NaN();
    input.accessibility.interface_scale =
        std::numeric_limits<float>::infinity();
    const auto hud = build_issou_arena_hud(input);

    CHECK(std::all_of(
        hud.begin(), hud.end(),
        [](const IssouHudElement& element) {
            return std::isfinite(element.rect.x) &&
                   std::isfinite(element.rect.y) &&
                   std::isfinite(element.rect.width) &&
                   std::isfinite(element.rect.height) &&
                   std::isfinite(element.value);
        }));

    input.viewport_width = 320.0F;
    input.viewport_height = 320.0F;
    input.accessibility.interface_scale = 1.60F;
    const auto compact = build_issou_arena_hud(input);
    CHECK(std::all_of(
        compact.begin(), compact.end(),
        [&input](const IssouHudElement& element) {
            return element.rect.x >= 0.0F &&
                   element.rect.y >= 0.0F &&
                   element.rect.x + element.rect.width <=
                       input.viewport_width &&
                   element.rect.y + element.rect.height <=
                       input.viewport_height;
        }));
}

} // namespace valcraft
