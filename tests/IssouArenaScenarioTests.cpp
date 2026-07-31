#include "gameplay/scenarios/IssouArenaLayout.h"
#include "gameplay/scenarios/IssouArenaScenario.h"
#include "gameplay/scenarios/ScenarioSessionState.h"

#include <doctest/doctest.h>

#include <cmath>

namespace valcraft {

TEST_CASE("l'arene Issou possede un layout deterministe aux dimensions du plan") {
    const auto first =
        IssouArenaLayoutGenerator(
            42,
            96,
            -48,
            72)
            .build_layout();
    const auto second =
        IssouArenaLayoutGenerator(
            42,
            96,
            -48,
            72)
            .build_layout();

    CHECK(first == second);
    CHECK(
        first.combat_bounds.max_x -
            first.combat_bounds.min_x +
            1 ==
        52);
    CHECK(
        first.combat_bounds.max_z -
            first.combat_bounds.min_z +
            1 ==
        40);
    CHECK(
        first.combat_bounds.max_y -
            first.floor_y ==
        12);
    CHECK(first.braziers.size() == 8U);
    CHECK(first.chain_anchors.size() == 4U);
    CHECK(first.gate_cells.size() == 42U);

    const auto distance =
        glm::length(
            first.player_spawn -
            first.colossus_spawn);
    CHECK(distance >= 18.0F);
    CHECK(distance <= 22.0F);
}

TEST_CASE("le generateur Issou construit une piste fermee et praticable") {
    constexpr int kSeed = 133742;
    World world(
        kSeed,
        4,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest);
    IssouArenaLayoutGenerator generator(
        kSeed,
        0,
        0,
        72);
    const auto layout =
        generator.build_layout();

    generator.apply(world, layout);

    CHECK(
        world.get_block(
            0,
            layout.floor_y,
            0) ==
        to_block_id(BlockType::Sand));
    CHECK(
        world.get_block(
            0,
            layout.floor_y + 1,
            0) ==
        to_block_id(BlockType::Air));
    CHECK(
        world.get_block(
            layout.combat_bounds.min_x,
            layout.floor_y + 8,
            0) !=
        to_block_id(BlockType::Air));
    CHECK(
        world.get_block(
            layout.combat_bounds.max_x,
            layout.floor_y + 8,
            0) !=
        to_block_id(BlockType::Air));
    CHECK(
        world.get_block(
            0,
            layout.floor_y + 3,
            layout.combat_bounds.min_z) ==
        to_block_id(BlockType::Planks));

    for (const auto& brazier : layout.braziers) {
        CHECK(
            world.get_block(
                brazier.x,
                brazier.y,
                brazier.z) ==
            to_block_id(BlockType::Torch));
    }
}

TEST_CASE("la hauteur de l'arene reste toujours dans les limites du monde") {
    const auto low =
        IssouArenaLayoutGenerator(
            1,
            0,
            0,
            -500)
            .build_layout();
    const auto high =
        IssouArenaLayoutGenerator(
            1,
            0,
            0,
            500)
            .build_layout();

    CHECK(low.protected_bounds.min_y >= kWorldMinY);
    CHECK(high.protected_bounds.max_y <= kWorldMaxY);
}

TEST_CASE("le scenario Issou libere le Colosse apres exactement dix secondes") {
    IssouArenaScenario scenario {};
    const auto layout =
        IssouArenaLayoutGenerator(77)
            .build_layout();
    REQUIRE(scenario.enter(layout, 4U));

    // Je fais avancer le pas borne comme le ferait la boucle fixe du jeu.
    for (auto index = 0; index < 8; ++index) {
        scenario.update(0.25F);
    }
    REQUIRE(
        scenario.state().phase ==
        IssouArenaPhase::Countdown);
    for (auto index = 0; index < 39; ++index) {
        scenario.update(0.25F);
    }
    CHECK(
        scenario.state().phase ==
        IssouArenaPhase::Countdown);
    CHECK(
        scenario.state().countdown_seconds ==
        doctest::Approx(0.25F));
    scenario.update(0.25F);

    CHECK(
        scenario.state().phase ==
        IssouArenaPhase::Combat);
    CHECK_FALSE(
        scenario.state().chains_visible);
    CHECK_FALSE(
        scenario.state()
            .colossus_invulnerable);
}

TEST_CASE("reset Issou conserve les options mais efface tout le combat") {
    IssouArenaScenario scenario {};
    const auto layout =
        IssouArenaLayoutGenerator(91)
            .build_layout();
    REQUIRE(scenario.enter(layout, 9U));
    REQUIRE(
        scenario.set_gore_mode(
            IssouGoreMode::Reduced));
    REQUIRE(
        scenario.set_awakening_override(3U));
    REQUIRE(scenario.skip_countdown());
    scenario.notify_combat_event(
        IssouArenaCombatEvent::AttackHit,
        32.0F,
        4U);
    scenario.notify_combat_event(
        IssouArenaCombatEvent::LimbSevered);
    scenario.notify_combat_event(
        IssouArenaCombatEvent::PerfectGuard);
    REQUIRE(scenario.reset());

    CHECK(
        scenario.state().phase ==
        IssouArenaPhase::Arrival);
    CHECK(
        scenario.state().statistics ==
        IssouArenaCombatStatistics {});
    CHECK(
        scenario.state().gore_mode ==
        IssouGoreMode::Reduced);
    CHECK(
        scenario.state()
            .awakening_override ==
        3U);
    CHECK(
        scenario.state().run_sequence ==
        10U);
}

TEST_CASE("Issou bloque toute mutation permanente et produit des resultats bornes") {
    IssouArenaScenario scenario {};
    REQUIRE(
        scenario.enter(
            IssouArenaLayoutGenerator(101)
                .build_layout()));
    REQUIRE(scenario.skip_countdown());

    CHECK(scenario.saving_suspended());
    CHECK_FALSE(
        scenario.permanent_rewards_allowed());
    for (auto index = 0; index < 1000; ++index) {
        scenario.notify_combat_event(
            IssouArenaCombatEvent::
                AttackMissed);
    }
    scenario.notify_combat_event(
        IssouArenaCombatEvent::
            BossExecuted);

    CHECK(
        scenario.state().phase ==
        IssouArenaPhase::Victory);
    CHECK(
        scenario.state().statistics
            .missed_attacks ==
        1000U);
    CHECK(
        scenario.state().statistics
            .executed);
    const auto hud = scenario.hud_view();
    CHECK(hud.visible);
    CHECK(hud.show_results);
    CHECK(
        hud.crowd_excitement >=
        0.0F);
    CHECK(
        hud.crowd_excitement <=
        1.0F);

    REQUIRE(scenario.request_exit());
    CHECK(
        scenario.state().phase ==
        IssouArenaPhase::ExitRequested);
}

TEST_CASE("les evenements Issou sont bornes et consommes une seule fois") {
    IssouArenaScenario scenario {};
    REQUIRE(
        scenario.enter(
            IssouArenaLayoutGenerator(202)
                .build_layout()));
    REQUIRE(scenario.skip_countdown());
    for (auto index = 0;
         index < 100;
         ++index) {
        scenario.notify_combat_event(
            IssouArenaCombatEvent::
                AttackHit,
            1.0F,
            1U);
    }

    const auto events =
        scenario.consume_events();
    CHECK(
        events.size() <=
        kIssouArenaEventCapacity);
    CHECK(
        scenario.consume_events()
            .empty());
}

TEST_CASE("le snapshot de scenario restitue le monde sans copie ni recapture") {
    ScenarioSessionState session {};
    auto world =
        std::make_unique<World>(
            4567,
            3,
            WorldGenerationProfile::Continental,
            WorldGenerationVersion::Latest);
    world->set_block(
        2,
        80,
        -3,
        to_block_id(BlockType::GoldOre));
    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 4567;
    snapshot.player_state.position = {
        9.5F,
        81.0F,
        -6.5F,
    };
    SessionSaveState save_state {};
    save_state.mark_dirty();

    REQUIRE(
        session.capture(
            std::move(world),
            snapshot,
            save_state,
            3U));
    CHECK(session.active());
    CHECK_FALSE(session.saves_allowed());
    CHECK_FALSE(
        session.permanent_rewards_allowed());
    CHECK_FALSE(
        session.capture(
            std::make_unique<World>(1),
            {},
            {},
            std::nullopt));

    auto restore = session.release();
    REQUIRE(restore.has_value());
    REQUIRE(restore->world != nullptr);
    CHECK(
        restore->world->seed() ==
        4567);
    CHECK(
        restore->world->get_block(
            2,
            80,
            -3) ==
        to_block_id(BlockType::GoldOre));
    CHECK(
        restore->snapshot.player_state
            .position ==
        snapshot.player_state.position);
    CHECK(
        restore->active_save_slot ==
        3U);
    CHECK(restore->save_state.dirty());
    CHECK_FALSE(session.active());
    CHECK(session.saves_allowed());
    CHECK_FALSE(session.release().has_value());
}

} // namespace valcraft
