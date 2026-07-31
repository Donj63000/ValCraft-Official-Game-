#include "gameplay/quests/LegendaryWeaponQuest.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace valcraft {

namespace {

struct LegendaryQuestFixture {
    explicit LegendaryQuestFixture(
        GameMode mode = GameMode::ClassicAdventure,
        std::uint64_t seed = 0xBADC0FFEEULL)
        : quest(seed, mode) {}

    [[nodiscard]] auto context(
        bool temporary_issou_session = false) const
        -> LegendaryQuestContext {
        return {
            progression.state(),
            player_level,
            player_strength,
            temporary_issou_session,
        };
    }

    [[nodiscard]] auto callbacks()
        -> LegendaryQuestCallbacks {
        LegendaryQuestCallbacks result {};
        result.commit_hear_rumor = [this] {
            calls.push_back("rumor");
            return progression.hear_rumor();
        };
        result.commit_map_fragment = [this] {
            calls.push_back("fragment");
            return progression.collect_map_fragment();
        };
        result.commit_forge_discovery = [this] {
            calls.push_back("forge");
            return progression.discover_forge();
        };
        result.commit_guardian_defeat = [this] {
            calls.push_back("guardian");
            return progression.defeat_guardian();
        };
        result.try_commit_weapon_to_inventory =
            [this](std::uint64_t weapon_id) {
                calls.push_back("inventory");
                if (!inventory_accepts_weapon ||
                    inventory_contains_weapon ||
                    weapon_id == 0ULL) {
                    return false;
                }
                inventory_contains_weapon = true;
                inventory_weapon_id = weapon_id;
                return true;
            };
        result.commit_weapon_claim =
            [this](
                std::uint64_t weapon_id,
                std::uint32_t level,
                std::uint8_t strength) {
                calls.push_back("progression");
                return progression_accepts_claim &&
                    progression.claim_weapon(
                        weapon_id,
                        level,
                        strength);
            };
        result.rollback_weapon_from_inventory =
            [this](std::uint64_t weapon_id) {
                calls.push_back("rollback");
                if (!rollback_succeeds ||
                    !inventory_contains_weapon ||
                    inventory_weapon_id != weapon_id) {
                    return false;
                }
                inventory_contains_weapon = false;
                inventory_weapon_id = 0ULL;
                return true;
            };
        result.commit_first_combat = [this] {
            calls.push_back("tutorial");
            return tutorial_commit_succeeds &&
                progression.complete_first_combat();
        };
        return result;
    }

    LegendaryWeaponQuest quest {};
    LegendaryWeaponProgression progression {};
    std::uint32_t player_level =
        kLegendaryWeaponRequiredPlayerLevel;
    std::uint8_t player_strength =
        kLegendaryWeaponRequiredStrength;
    bool inventory_accepts_weapon = true;
    bool progression_accepts_claim = true;
    bool rollback_succeeds = true;
    bool tutorial_commit_succeeds = true;
    bool inventory_contains_weapon = false;
    std::uint64_t inventory_weapon_id = 0ULL;
    std::vector<std::string_view> calls {};
};

[[nodiscard]] auto make_request(
    LegendaryQuestAction action,
    LegendaryQuestAnchorId anchor_id = 0ULL,
    std::uint8_t fragment_index = 0U,
    std::uint64_t combat_target_id = 0ULL,
    float combat_value = 0.0F) -> LegendaryQuestRequest {
    return {
        action,
        anchor_id,
        fragment_index,
        combat_target_id,
        combat_value,
    };
}

void advance_to_guardian_defeated(
    LegendaryQuestFixture& fixture) {
    auto callbacks = fixture.callbacks();
    const auto& layout = fixture.quest.layout();

    REQUIRE(
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::HearRumor,
                layout.rumor.id),
            fixture.context(),
            callbacks)
            .accepted());
    for (std::uint8_t index = 0U;
         index <
         static_cast<std::uint8_t>(
             kLegendaryQuestMapFragmentCount);
         ++index) {
        REQUIRE(
            fixture.quest.process(
                make_request(
                    LegendaryQuestAction::
                        CollectMapFragment,
                    layout.map_clues[
                        static_cast<std::size_t>(index)]
                        .source.id,
                    index),
                fixture.context(),
                callbacks)
                .accepted());
    }
    REQUIRE(
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::DiscoverForge,
                layout.forge.id),
            fixture.context(),
            callbacks)
            .accepted());
    REQUIRE(
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::DefeatGuardian,
                layout.guardian.id),
            fixture.context(),
            callbacks)
            .accepted());
    REQUIRE(
        fixture.progression.state().quest_stage ==
        LegendaryWeaponQuestStage::GuardianDefeated);
}

void claim_weapon(LegendaryQuestFixture& fixture) {
    auto callbacks = fixture.callbacks();
    const auto request =
        make_request(
            LegendaryQuestAction::InteractWithBlade,
            fixture.quest.layout().blade.id);
    REQUIRE_FALSE(
        fixture.quest.process(
            request,
            fixture.context(),
            callbacks)
            .accepted());
    REQUIRE(
        fixture.quest.process(
            request,
            fixture.context(),
            callbacks)
            .accepted());
    REQUIRE(
        fixture.progression.state().quest_stage ==
        LegendaryWeaponQuestStage::WeaponClaimed);
}

} // namespace

TEST_CASE(
    "la quete legendaire genere des ancres deterministes et distinctes") {
    constexpr std::array<std::uint64_t, 5U> kSeeds {
        0ULL,
        1ULL,
        42ULL,
        0xBADC0FFEEULL,
        std::numeric_limits<std::uint64_t>::max(),
    };

    for (const auto seed : kSeeds) {
        for (const auto mode : {
                 GameMode::ClassicAdventure,
                 GameMode::SeaAdventure,
             }) {
            const auto first =
                generate_legendary_weapon_quest_layout(
                    seed,
                    mode);
            const auto second =
                generate_legendary_weapon_quest_layout(
                    seed,
                    mode);
            REQUIRE(first.has_value());
            REQUIRE(second.has_value());
            CHECK(*first == *second);
            CHECK(
                is_valid_legendary_weapon_quest_layout(
                    *first));
            CHECK(first->signature != 0ULL);
            CHECK(first->unique_weapon_id != 0ULL);

            std::array<LegendaryQuestAnchorId, 7U> ids {
                first->rumor.id,
                first->map_clues[0].source.id,
                first->map_clues[1].source.id,
                first->map_clues[2].source.id,
                first->forge.id,
                first->guardian.id,
                first->blade.id,
            };
            for (std::size_t left = 0U;
                 left < ids.size();
                 ++left) {
                CHECK(ids[left] != 0ULL);
                for (std::size_t right = left + 1U;
                     right < ids.size();
                     ++right) {
                    CHECK(ids[left] != ids[right]);
                }
            }
        }
    }

    CHECK_FALSE(
        generate_legendary_weapon_quest_layout(
            10ULL,
            static_cast<GameMode>(255U))
            .has_value());

    auto damaged =
        *generate_legendary_weapon_quest_layout(
            42ULL,
            GameMode::ClassicAdventure);
    ++damaged.forge.position.x;
    CHECK_FALSE(
        is_valid_legendary_weapon_quest_layout(
            damaged));
}

TEST_CASE(
    "la forge change de biotope sans modifier la logique de quete") {
    bool found_volcanic = false;
    bool found_ruined = false;
    for (std::uint64_t seed = 0ULL;
         seed < 64ULL;
         ++seed) {
        const auto classic =
            generate_legendary_weapon_quest_layout(
                seed,
                GameMode::ClassicAdventure);
        const auto maritime =
            generate_legendary_weapon_quest_layout(
                seed,
                GameMode::SeaAdventure);
        REQUIRE(classic.has_value());
        REQUIRE(maritime.has_value());

        CHECK(
            classic->forge_site ==
            LegendaryQuestForgeSite::RemoteMountain);
        CHECK(classic->forge.position.y >= 104);
        CHECK(maritime->forge.position.x >= 2'200);
        CHECK(
            maritime->forge_site !=
            LegendaryQuestForgeSite::RemoteMountain);
        found_volcanic =
            found_volcanic ||
            maritime->forge_site ==
                LegendaryQuestForgeSite::VolcanicIsland;
        found_ruined =
            found_ruined ||
            maritime->forge_site ==
                LegendaryQuestForgeSite::RuinedIsland;

        for (std::size_t index = 0U;
             index < kLegendaryQuestMapFragmentCount;
             ++index) {
            CHECK(
                classic->map_clues[index].fragment_index ==
                static_cast<std::uint8_t>(index));
            CHECK(
                maritime->map_clues[index].fragment_index ==
                static_cast<std::uint8_t>(index));
            CHECK_FALSE(
                classic->map_clues[index].hint_id.empty());
            CHECK_FALSE(
                maritime->map_clues[index].hint_id.empty());
        }
        CHECK(
            classic->guardian_definition ==
            maritime->guardian_definition);
    }
    CHECK(found_volcanic);
    CHECK(found_ruined);
    CHECK(
        kLegendaryQuestForgeFeatures.front() ==
        LegendaryQuestForgeFeature::GiantTools);
    CHECK(
        kLegendaryQuestForgeFeatures.back() ==
        LegendaryQuestForgeFeature::SealedRoom);
}

TEST_CASE(
    "rumeur carte forge et gardien avancent uniquement apres commit") {
    LegendaryQuestFixture fixture {};
    auto callbacks = fixture.callbacks();
    const auto& layout = fixture.quest.layout();

    auto presentation =
        fixture.quest.presentation_state(
            fixture.progression.state(),
            fixture.player_level,
            fixture.player_strength);
    REQUIRE(presentation.valid);
    CHECK_FALSE(presentation.journal_visible);
    CHECK_FALSE(presentation.world_marker_visible);
    CHECK(
        presentation.target.id ==
        layout.rumor.id);
    CHECK(
        presentation.completion_ratio ==
        doctest::Approx(0.0F));

    const auto rumor =
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::HearRumor,
                layout.rumor.id),
            fixture.context(),
            callbacks);
    REQUIRE(rumor.accepted());
    CHECK(rumor.stage_advanced);
    CHECK(
        rumor.expected_stage_after ==
        LegendaryWeaponQuestStage::RumorHeard);

    const auto skipped_fragment =
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::CollectMapFragment,
                layout.map_clues[1].source.id,
                1U),
            fixture.context(),
            callbacks);
    CHECK(
        skipped_fragment.failure ==
        LegendaryQuestFailure::WrongMapFragment);
    CHECK(
        fixture.progression.state()
            .map_fragments_collected == 0U);

    for (std::uint8_t index = 0U;
         index <
         static_cast<std::uint8_t>(
             kLegendaryQuestMapFragmentCount);
         ++index) {
        const auto fragment =
            fixture.quest.process(
                make_request(
                    LegendaryQuestAction::
                        CollectMapFragment,
                    layout.map_clues[
                        static_cast<std::size_t>(index)]
                        .source.id,
                    index),
                fixture.context(),
                callbacks);
        REQUIRE(fragment.accepted());
        CHECK(
            fixture.progression.state()
                .map_fragments_collected ==
            static_cast<std::uint8_t>(index + 1U));
    }
    CHECK(
        fixture.progression.state().quest_stage ==
        LegendaryWeaponQuestStage::MapFragmentsComplete);

    REQUIRE(
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::DiscoverForge,
                layout.forge.id),
            fixture.context(),
            callbacks)
            .accepted());
    REQUIRE(
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::DefeatGuardian,
                layout.guardian.id),
            fixture.context(),
            callbacks)
            .accepted());

    presentation =
        fixture.quest.presentation_state(
            fixture.progression.state(),
            fixture.player_level,
            fixture.player_strength);
    CHECK(presentation.journal_visible);
    CHECK(
        presentation.stage ==
        LegendaryWeaponQuestStage::GuardianDefeated);
    CHECK(
        presentation.target.id ==
        layout.blade.id);
    CHECK(
        presentation.objective_id ==
        "quest.leviathan.objective.try_blade");
    CHECK(
        presentation.completion_ratio ==
        doctest::Approx(0.6F));

    CHECK(
        layout.guardian_definition.maximum_health ==
        doctest::Approx(300.0F));
    CHECK(
        layout.guardian_definition.armor ==
        doctest::Approx(42.0F));
    CHECK(layout.guardian_definition.teaches_armor);
    CHECK(layout.guardian_definition.teaches_stagger);
    CHECK(
        layout.guardian_definition
            .teaches_vulnerable_zones);
}

TEST_CASE(
    "les entrees invalides sont rejetees sans mutation ni callback") {
    LegendaryWeaponQuest unconfigured {};
    LegendaryWeaponProgressionState default_state {};
    const auto not_configured =
        unconfigured.process(
            make_request(
                LegendaryQuestAction::HearRumor),
            {default_state, 35U, 4U, false});
    CHECK(
        not_configured.failure ==
        LegendaryQuestFailure::NotConfigured);

    LegendaryQuestFixture fixture {};
    const auto original_layout = fixture.quest.layout();
    CHECK_FALSE(
        fixture.quest.configure(
            99ULL,
            static_cast<GameMode>(255U)));
    CHECK(fixture.quest.layout() == original_layout);

    std::uint32_t callback_count = 0U;
    LegendaryQuestCallbacks callbacks {};
    callbacks.commit_hear_rumor =
        [&callback_count] {
            ++callback_count;
            return true;
        };

    const auto invalid_action =
        fixture.quest.process(
            make_request(
                static_cast<LegendaryQuestAction>(250U),
                fixture.quest.layout().rumor.id),
            fixture.context(),
            callbacks);
    CHECK(
        invalid_action.failure ==
        LegendaryQuestFailure::InvalidAction);

    auto invalid_state = fixture.progression.state();
    invalid_state.map_fragments_collected = 2U;
    const auto invalid_progression =
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::HearRumor,
                fixture.quest.layout().rumor.id),
            {
                invalid_state,
                fixture.player_level,
                fixture.player_strength,
                false,
            },
            callbacks);
    CHECK(
        invalid_progression.failure ==
        LegendaryQuestFailure::InvalidProgressionState);

    const auto temporary =
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::HearRumor,
                fixture.quest.layout().rumor.id),
            fixture.context(true),
            callbacks);
    CHECK(
        temporary.failure ==
        LegendaryQuestFailure::TemporaryIssouSession);
    CHECK(callback_count == 0U);
    CHECK(
        fixture.progression.state().quest_stage ==
        LegendaryWeaponQuestStage::NotStarted);
    CHECK_FALSE(
        fixture.quest.runtime_state()
            .blade_refusal_witnessed);

    const auto wrong_anchor =
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::HearRumor,
                fixture.quest.layout().forge.id),
            fixture.context(),
            callbacks);
    CHECK(
        wrong_anchor.failure ==
        LegendaryQuestFailure::WrongAnchor);
    CHECK(callback_count == 0U);

    callbacks.commit_hear_rumor = {};
    CHECK(
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::HearRumor,
                fixture.quest.layout().rumor.id),
            fixture.context(),
            callbacks)
            .failure ==
        LegendaryQuestFailure::MissingCallback);

    callbacks.commit_hear_rumor = []() -> bool {
        throw std::runtime_error("commit interrompu");
    };
    CHECK(
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::HearRumor,
                fixture.quest.layout().rumor.id),
            fixture.context(),
            callbacks)
            .failure ==
        LegendaryQuestFailure::CallbackThrew);
    CHECK(
        fixture.progression.state().quest_stage ==
        LegendaryWeaponQuestStage::NotStarted);
}

TEST_CASE(
    "la premiere interaction echoue puis l'inventaire commit avant la progression") {
    LegendaryQuestFixture fixture {};
    advance_to_guardian_defeated(fixture);
    fixture.calls.clear();
    auto callbacks = fixture.callbacks();
    const auto blade_request =
        make_request(
            LegendaryQuestAction::InteractWithBlade,
            fixture.quest.layout().blade.id);

    const auto first =
        fixture.quest.process(
            blade_request,
            fixture.context(),
            callbacks);
    CHECK(
        first.failure ==
        LegendaryQuestFailure::FirstInteractionMustFail);
    CHECK(fixture.calls.empty());
    CHECK(
        fixture.quest.runtime_state()
            .blade_refusal_witnessed);
    REQUIRE_FALSE(fixture.quest.events().empty());
    CHECK(
        fixture.quest.events().back().type ==
        LegendaryQuestEventType::BladeRefused);

    fixture.player_level =
        kLegendaryWeaponRequiredPlayerLevel - 1U;
    fixture.player_strength =
        kLegendaryWeaponRequiredStrength - 1U;
    const auto too_weak =
        fixture.quest.process(
            blade_request,
            fixture.context(),
            callbacks);
    CHECK(
        too_weak.failure ==
        LegendaryQuestFailure::RequirementsNotMet);
    CHECK(fixture.calls.empty());

    auto presentation =
        fixture.quest.presentation_state(
            fixture.progression.state(),
            fixture.player_level,
            fixture.player_strength);
    CHECK(presentation.requirements_visible);
    CHECK_FALSE(presentation.level_requirement_met);
    CHECK_FALSE(presentation.strength_requirement_met);
    CHECK(
        presentation.objective_id ==
        "quest.leviathan.objective.gain_required_power");

    fixture.player_level =
        kLegendaryWeaponRequiredPlayerLevel;
    fixture.player_strength =
        kLegendaryWeaponRequiredStrength;
    fixture.inventory_accepts_weapon = false;
    const auto inventory_full =
        fixture.quest.process(
            blade_request,
            fixture.context(),
            callbacks);
    CHECK(
        inventory_full.failure ==
        LegendaryQuestFailure::InventoryRejected);
    REQUIRE(fixture.calls.size() == 1U);
    CHECK(fixture.calls[0] == "inventory");
    CHECK_FALSE(fixture.inventory_contains_weapon);
    CHECK_FALSE(fixture.progression.state().weapon_owned);

    fixture.calls.clear();
    fixture.inventory_accepts_weapon = true;
    fixture.progression_accepts_claim = false;
    const auto progression_rejected =
        fixture.quest.process(
            blade_request,
            fixture.context(),
            callbacks);
    CHECK(
        progression_rejected.failure ==
        LegendaryQuestFailure::ProgressionCommitRejected);
    CHECK(progression_rejected.inventory_committed);
    CHECK(progression_rejected.inventory_rolled_back);
    REQUIRE(fixture.calls.size() == 3U);
    CHECK(fixture.calls[0] == "inventory");
    CHECK(fixture.calls[1] == "progression");
    CHECK(fixture.calls[2] == "rollback");
    CHECK_FALSE(fixture.inventory_contains_weapon);
    CHECK_FALSE(fixture.progression.state().weapon_owned);

    fixture.calls.clear();
    fixture.progression_accepts_claim = true;
    fixture.quest.clear_events();
    const auto acquired =
        fixture.quest.process(
            blade_request,
            fixture.context(),
            callbacks);
    REQUIRE(acquired.accepted());
    CHECK(acquired.inventory_committed);
    CHECK_FALSE(acquired.inventory_rolled_back);
    REQUIRE(fixture.calls.size() == 2U);
    CHECK(fixture.calls[0] == "inventory");
    CHECK(fixture.calls[1] == "progression");
    CHECK(fixture.inventory_contains_weapon);
    CHECK(
        fixture.inventory_weapon_id ==
        fixture.quest.layout().unique_weapon_id);
    CHECK(fixture.progression.state().weapon_owned);
    CHECK(
        fixture.progression.state().unique_weapon_id ==
        fixture.quest.layout().unique_weapon_id);
    REQUIRE(fixture.quest.events().size() == 2U);
    CHECK(
        fixture.quest.events()[0].type ==
        LegendaryQuestEventType::WeaponAcquired);
    CHECK(
        fixture.quest.events()[1].type ==
        LegendaryQuestEventType::
            TutorialEncounterRequested);
}

TEST_CASE(
    "un echec de rollback est signale sans valider la quete") {
    LegendaryQuestFixture fixture {};
    advance_to_guardian_defeated(fixture);
    auto callbacks = fixture.callbacks();
    const auto request =
        make_request(
            LegendaryQuestAction::InteractWithBlade,
            fixture.quest.layout().blade.id);
    REQUIRE(
        fixture.quest.process(
            request,
            fixture.context(),
            callbacks)
            .failure ==
        LegendaryQuestFailure::FirstInteractionMustFail);

    fixture.progression_accepts_claim = false;
    fixture.rollback_succeeds = false;
    fixture.calls.clear();
    const auto result =
        fixture.quest.process(
            request,
            fixture.context(),
            callbacks);
    CHECK(
        result.failure ==
        LegendaryQuestFailure::InventoryRollbackFailed);
    CHECK(result.inventory_committed);
    CHECK_FALSE(result.inventory_rolled_back);
    CHECK(fixture.inventory_contains_weapon);
    CHECK_FALSE(fixture.progression.state().weapon_owned);
    CHECK(
        fixture.quest.events().back().type ==
        LegendaryQuestEventType::
            TransactionRollbackFailed);
}

TEST_CASE(
    "le premier combat exige un balayage une garde reussie et une charge") {
    LegendaryQuestFixture fixture {};
    advance_to_guardian_defeated(fixture);
    claim_weapon(fixture);
    fixture.calls.clear();
    auto callbacks = fixture.callbacks();

    const auto invalid_evidence =
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::TutorialSweepHit,
                0ULL,
                0U,
                0ULL,
                std::numeric_limits<float>::quiet_NaN()),
            fixture.context(),
            callbacks);
    CHECK(
        invalid_evidence.failure ==
        LegendaryQuestFailure::InvalidCombatEvidence);

    const auto guard =
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::
                    TutorialGuardSucceeded,
                0ULL,
                0U,
                1001ULL,
                18.0F),
            fixture.context(),
            callbacks);
    REQUIRE(guard.accepted());
    CHECK_FALSE(guard.stage_advanced);
    CHECK(
        guard.tutorial_completion_mask ==
        kLegendaryQuestTutorialGuardBit);
    CHECK(fixture.calls.empty());

    const auto duplicate_guard =
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::
                    TutorialGuardSucceeded,
                0ULL,
                0U,
                1001ULL,
                18.0F),
            fixture.context(),
            callbacks);
    CHECK(
        duplicate_guard.failure ==
        LegendaryQuestFailure::
            TutorialObjectiveAlreadyComplete);

    auto presentation =
        fixture.quest.presentation_state(
            fixture.progression.state(),
            fixture.player_level,
            fixture.player_strength);
    CHECK_FALSE(
        presentation.tutorial_objectives[0].complete);
    CHECK(presentation.tutorial_objectives[1].complete);
    CHECK(
        presentation.objective_id ==
        "quest.leviathan.tutorial.sweep");

    REQUIRE(
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::TutorialSweepHit,
                0ULL,
                0U,
                1002ULL,
                14.0F),
            fixture.context(),
            callbacks)
            .accepted());

    fixture.tutorial_commit_succeeds = false;
    const auto rejected_completion =
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::TutorialChargedHit,
                0ULL,
                0U,
                1003ULL,
                42.0F),
            fixture.context(),
            callbacks);
    CHECK(
        rejected_completion.failure ==
        LegendaryQuestFailure::ProgressionCommitRejected);
    CHECK(
        rejected_completion.tutorial_completion_mask ==
        static_cast<std::uint8_t>(
            kLegendaryQuestTutorialSweepBit |
            kLegendaryQuestTutorialGuardBit));
    CHECK(
        fixture.progression.state().quest_stage ==
        LegendaryWeaponQuestStage::WeaponClaimed);

    fixture.tutorial_commit_succeeds = true;
    const auto completed =
        fixture.quest.process(
            make_request(
                LegendaryQuestAction::TutorialChargedHit,
                0ULL,
                0U,
                1003ULL,
                42.0F),
            fixture.context(),
            callbacks);
    REQUIRE(completed.accepted());
    CHECK(completed.stage_advanced);
    CHECK(
        completed.expected_stage_after ==
        LegendaryWeaponQuestStage::FirstCombatComplete);
    CHECK(
        completed.tutorial_completion_mask ==
        kLegendaryQuestTutorialCompleteMask);
    CHECK(
        fixture.progression.state().quest_stage ==
        LegendaryWeaponQuestStage::FirstCombatComplete);

    presentation =
        fixture.quest.presentation_state(
            fixture.progression.state(),
            fixture.player_level,
            fixture.player_strength);
    CHECK(presentation.completed);
    CHECK_FALSE(presentation.world_marker_visible);
    CHECK(
        presentation.completion_ratio ==
        doctest::Approx(1.0F));
    CHECK(presentation.tutorial_objectives[0].complete);
    CHECK(presentation.tutorial_objectives[1].complete);
    CHECK(presentation.tutorial_objectives[2].complete);
}

TEST_CASE(
    "l'etat transitoire se restaure seulement pour la bonne quete") {
    LegendaryQuestFixture fixture {};
    advance_to_guardian_defeated(fixture);
    auto callbacks = fixture.callbacks();
    const auto blade_request =
        make_request(
            LegendaryQuestAction::InteractWithBlade,
            fixture.quest.layout().blade.id);
    REQUIRE_FALSE(
        fixture.quest.process(
            blade_request,
            fixture.context(),
            callbacks)
            .accepted());
    const auto snapshot =
        fixture.quest.runtime_state();
    REQUIRE(snapshot.blade_refusal_witnessed);

    LegendaryWeaponQuest restored(
        fixture.quest.layout().world_seed,
        fixture.quest.layout().game_mode);
    REQUIRE(
        restored.restore_runtime_state(
            snapshot,
            fixture.progression.state()));
    CHECK(restored.runtime_state() == snapshot);

    auto wrong_signature = snapshot;
    ++wrong_signature.layout_signature;
    CHECK_FALSE(
        restored.restore_runtime_state(
            wrong_signature,
            fixture.progression.state()));
    CHECK(restored.runtime_state() == snapshot);

    auto bad_mask = snapshot;
    bad_mask.tutorial_completion_mask = 0x80U;
    CHECK_FALSE(
        restored.restore_runtime_state(
            bad_mask,
            fixture.progression.state()));

    LegendaryWeaponProgression early_progression {};
    CHECK_FALSE(
        restored.restore_runtime_state(
            snapshot,
            early_progression.state()));
}

TEST_CASE(
    "la file d'evenements reste bornee et se draine dans l'ordre") {
    LegendaryQuestFixture fixture {};
    LegendaryQuestCallbacks callbacks {};

    for (std::size_t attempt = 0U;
         attempt < kLegendaryQuestEventCapacity + 50U;
         ++attempt) {
        const auto result =
            fixture.quest.process(
                make_request(
                    LegendaryQuestAction::HearRumor,
                    fixture.quest.layout().forge.id),
                fixture.context(),
                callbacks);
        CHECK(
            result.failure ==
            LegendaryQuestFailure::WrongAnchor);
        CHECK(
            fixture.quest.events().size() <=
            kLegendaryQuestEventCapacity);
    }

    REQUIRE(
        fixture.quest.events().size() ==
        kLegendaryQuestEventCapacity);
    CHECK(
        fixture.quest.event_overflow_stats().evicted ==
        50ULL);
    CHECK(
        fixture.quest.event_overflow_stats().rejected ==
        0ULL);

    std::array<LegendaryQuestEvent, 7U> first_batch {};
    const auto drained =
        fixture.quest.drain_events(first_batch);
    CHECK(drained == first_batch.size());
    for (std::size_t index = 1U;
         index < drained;
         ++index) {
        CHECK(
            first_batch[index - 1U].event_id <
            first_batch[index].event_id);
    }
    CHECK(
        fixture.quest.events().size() ==
        kLegendaryQuestEventCapacity -
            first_batch.size());

    fixture.quest.clear_events();
    CHECK(fixture.quest.events().empty());
}

} // namespace valcraft
