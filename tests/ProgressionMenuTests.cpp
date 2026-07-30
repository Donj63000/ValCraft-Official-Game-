#include "app/ProgressionMenu.h"
#include "app/ConstructionPlanEditor.h"
#include "app/ProgressionMenuLayout.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string_view>

namespace valcraft {

TEST_CASE("le menu masque toute action de jeu tant qu'il est fermé") {
    ProgressionMenu menu {};
    PlayerBuildState state {};

    const auto hidden =
        menu.handle_input(
            ProgressionMenuInput::PurchaseRank,
            state,
            100U);
    CHECK(hidden.failure == ProgressionMenuFailure::Hidden);
    CHECK_FALSE(hidden.ui_changed);
    CHECK_FALSE(hidden.build_changed);
    CHECK(
        player_ability_rank(
            state,
            AbilityId::KnightVanguardStrike) ==
        0U);

    const auto opened =
        menu.handle_input(
            ProgressionMenuInput::ToggleVisibility,
            state,
            100U);
    CHECK(opened.succeeded());
    CHECK(opened.ui_changed);
    CHECK_FALSE(opened.build_changed);
    CHECK(menu.visible());

    const auto closed =
        menu.handle_input(
            ProgressionMenuInput::Close,
            state,
            100U);
    CHECK(closed.succeeded());
    CHECK(closed.ui_changed);
    CHECK_FALSE(closed.build_changed);
    CHECK_FALSE(menu.visible());
}

TEST_CASE("la navigation abstraite boucle sur les quatre voies dix paliers et cinq slots") {
    ProgressionMenu menu {};
    PlayerBuildState state {};
    state.last_dominant_path =
        AbilityPath::Ninja;
    state.revision = 41ULL;
    const auto build_before_navigation =
        state;
    menu.set_visible(true);

    CHECK(
        menu.handle_input(
                ProgressionMenuInput::PreviousPath,
                state,
                1U)
            .succeeded());
    CHECK(menu.selected_path() == AbilityPath::Builder);
    CHECK(
        menu.selected_ability() ==
        AbilityId::BuilderConstructionPlan);

    CHECK(
        menu.handle_input(
                ProgressionMenuInput::PreviousAbility,
                state,
                1U)
            .succeeded());
    CHECK(menu.selected_tier() == 10U);
    CHECK(
        menu.selected_ability() ==
        AbilityId::BuilderAbsoluteArchitect);

    CHECK(
        menu.handle_input(
                ProgressionMenuInput::PreviousSlot,
                state,
                1U)
            .succeeded());
    CHECK(menu.selected_slot() == 4U);

    CHECK(
        menu.handle_input(
                ProgressionMenuInput::NextPath,
                state,
                1U)
            .succeeded());
    CHECK(menu.selected_path() == AbilityPath::Knight);
    CHECK(
        menu.handle_input(
                ProgressionMenuInput::NextAbility,
                state,
                1U)
            .succeeded());
    CHECK(menu.selected_tier() == 1U);
    CHECK(
        menu.handle_input(
                ProgressionMenuInput::NextSlot,
                state,
                1U)
            .succeeded());
    CHECK(menu.selected_slot() == 0U);
    CHECK(state == build_before_navigation);
}

TEST_CASE("la sélection publique assainit les valeurs hors limites") {
    ProgressionMenu menu {};

    menu.select_path(
        static_cast<AbilityPath>(255U));
    menu.select_tier(0U);
    menu.select_slot(999U);
    menu.sanitize();

    CHECK(menu.selected_path() == AbilityPath::Knight);
    CHECK(menu.selected_tier() == 1U);
    CHECK(menu.selected_slot() == 4U);
    CHECK(
        menu.selected_ability() ==
        AbilityId::KnightVanguardStrike);

    menu.select_tier(999U);
    CHECK(menu.selected_tier() == 10U);
}

TEST_CASE("le menu délègue les achats de rangs et leurs refus au PlayerBuildState") {
    ProgressionMenu menu {};
    PlayerBuildState state {};
    menu.set_visible(true);

    const auto rank_one =
        menu.handle_input(
            ProgressionMenuInput::PurchaseRank,
            state,
            1U);
    CHECK(rank_one.succeeded());
    CHECK_FALSE(rank_one.ui_changed);
    CHECK(rank_one.build_changed);
    CHECK(
        player_ability_rank(
            state,
            AbilityId::KnightVanguardStrike) ==
        1U);

    const auto level_rejected =
        menu.handle_input(
            ProgressionMenuInput::PurchaseRank,
            state,
            1U);
    CHECK(
        level_rejected.failure ==
        ProgressionMenuFailure::AbilityBuildRejected);
    CHECK(
        level_rejected.ability_failure ==
        AbilityBuildFailure::RequiredLevel);
    CHECK_FALSE(level_rejected.ui_changed);
    CHECK_FALSE(level_rejected.build_changed);

    const auto rank_two =
        menu.handle_input(
            ProgressionMenuInput::PurchaseRank,
            state,
            100U);
    const auto rank_three =
        menu.handle_input(
            ProgressionMenuInput::PurchaseRank,
            state,
            100U);
    CHECK(rank_two.succeeded());
    CHECK(rank_three.succeeded());
    CHECK(
        player_ability_rank(
            state,
            AbilityId::KnightVanguardStrike) ==
        3U);
}

TEST_CASE("le menu achète une maîtrise uniquement avec le rang III et le budget requis") {
    ProgressionMenu menu {};
    PlayerBuildState state {};
    menu.set_visible(true);

    auto rejected =
        menu.handle_input(
            ProgressionMenuInput::PurchaseMastery,
            state,
            100U);
    CHECK(
        rejected.ability_failure ==
        AbilityBuildFailure::RankThreeRequired);

    CHECK(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::KnightVanguardStrike)
            .succeeded());
    CHECK(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::KnightVanguardStrike)
            .succeeded());
    CHECK(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::KnightVanguardStrike)
            .succeeded());

    const auto mastered =
        menu.handle_input(
            ProgressionMenuInput::PurchaseMastery,
            state,
            100U);
    CHECK(mastered.succeeded());
    CHECK_FALSE(mastered.ui_changed);
    CHECK(mastered.build_changed);
    CHECK(
        player_ability_has_mastery(
            state,
            AbilityId::KnightVanguardStrike));

    rejected =
        menu.handle_input(
            ProgressionMenuInput::PurchaseMastery,
            state,
            100U);
    CHECK(
        rejected.ability_failure ==
        AbilityBuildFailure::AlreadyMastered);
}

TEST_CASE("les slots respectent les catégories et permettent le déséquipement") {
    ProgressionMenu menu {};
    PlayerBuildState state {};
    menu.set_visible(true);

    CHECK(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::KnightVanguardStrike)
            .succeeded());

    menu.select_slot(3U);
    const auto incompatible =
        menu.handle_input(
            ProgressionMenuInput::EquipOrUnequip,
            state,
            100U);
    CHECK(
        incompatible.failure ==
        ProgressionMenuFailure::IncompatibleSlot);
    CHECK(
        state.equipped_abilities[3] ==
        AbilityId::None);

    menu.select_slot(0U);
    const auto equipped =
        menu.handle_input(
            ProgressionMenuInput::EquipOrUnequip,
            state,
            100U);
    CHECK(equipped.succeeded());
    CHECK(
        state.equipped_abilities[0] ==
        AbilityId::KnightVanguardStrike);

    const auto unequipped =
        menu.handle_input(
            ProgressionMenuInput::EquipOrUnequip,
            state,
            100U);
    CHECK(unequipped.succeeded());
    CHECK(
        state.equipped_abilities[0] ==
        AbilityId::None);

    menu.select_path(AbilityPath::Ninja);
    CHECK(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::NinjaWindAcceleration)
            .succeeded());
    menu.select_slot(3U);
    CHECK(
        menu.handle_input(
                ProgressionMenuInput::EquipOrUnequip,
                state,
                100U)
            .succeeded());
    CHECK(
        state.equipped_abilities[3] ==
        AbilityId::NinjaWindAcceleration);
}

TEST_CASE("l'allocation d'attribut est bornée par les points et le plafond dix") {
    ProgressionMenu menu {};
    PlayerBuildState state {};
    menu.set_visible(true);

    auto rejected =
        menu.handle_input(
            ProgressionMenuInput::AllocateStrength,
            state,
            4U);
    CHECK(
        rejected.failure ==
        ProgressionMenuFailure::InsufficientAttributePoints);

    for (std::uint32_t point = 0U;
         point < 10U;
         ++point) {
        const auto allocated =
            menu.handle_input(
                ProgressionMenuInput::AllocateStrength,
                state,
                100U);
        CHECK(allocated.succeeded());
    }
    CHECK(
        state.attributes.values[
            player_attribute_index(
                PlayerAttribute::Strength)] ==
        10U);

    rejected =
        menu.handle_input(
            ProgressionMenuInput::AllocateStrength,
            state,
            100U);
    CHECK(
        rejected.failure ==
        ProgressionMenuFailure::AttributeCapReached);

    for (std::uint32_t point = 0U;
         point < 10U;
         ++point) {
        CHECK(
            menu.handle_input(
                    ProgressionMenuInput::AllocateWisdom,
                    state,
                    100U)
                .succeeded());
    }
    rejected =
        menu.handle_input(
            ProgressionMenuInput::AllocateAgility,
            state,
            100U);
    CHECK(
        rejected.failure ==
        ProgressionMenuFailure::InsufficientAttributePoints);
}

TEST_CASE("le view model expose les textes les budgets les prérequis et les valeurs EV CD") {
    ProgressionMenu menu {};
    PlayerBuildState state {};
    menu.set_visible(true);
    menu.select_path(AbilityPath::Commander);
    menu.select_tier(1U);
    menu.select_slot(0U);

    CHECK(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::CommanderFootman)
            .succeeded());
    CHECK(
        equip_player_ability(
            state,
            0U,
            AbilityId::CommanderFootman)
            .succeeded());
    state.attributes.values[
        player_attribute_index(
            PlayerAttribute::Wisdom)] = 3U;

    const auto view =
        menu.make_view_model(
            state,
            100U);
    CHECK(view.visible);
    CHECK(view.level == 100U);
    CHECK(view.selected_path_name == "Commandant");
    CHECK(view.selected_tier == 1U);
    CHECK(view.selected_slot_name == "Actif 1");
    CHECK(view.budget.earned_skill_points == 100U);
    CHECK(view.budget.spent_skill_points == 1U);
    CHECK(view.budget.available_skill_points == 99U);
    CHECK(view.budget.available_attribute_points == 17U);

    CHECK(
        view.ability.id ==
        AbilityId::CommanderFootman);
    CHECK(
        view.ability.stable_name ==
        "CommanderFootman");
    CHECK(
        view.ability.display_name ==
        "Invocation de fantassin");
    CHECK(
        view.ability.category_name ==
        "Active");
    CHECK(view.ability.current_rank == 1U);
    CHECK(view.ability.next_rank == 2U);
    CHECK(view.ability.next_rank_required_level == 3U);
    CHECK(view.ability.next_rank_skill_point_cost == 2U);
    CHECK(view.ability.energy_cost == doctest::Approx(25.0F));
    CHECK(view.ability.cooldown_seconds == doctest::Approx(20.0F));
    CHECK(view.ability.range_meters == doctest::Approx(8.0F));
    CHECK(view.ability.duration_seconds == doctest::Approx(20.0F));
    CHECK(view.ability.equipped);
    CHECK(view.ability.equipped_slot == 0);
    CHECK_FALSE(view.ability.mastered);
    CHECK(view.ability.rank_purchase_status == "Disponible");
    CHECK(view.ability.mastery_purchase_status == "Rang III requis");

    CHECK(view.slots[0].name == "Actif 1");
    CHECK(
        view.slots[0].ability ==
        AbilityId::CommanderFootman);
    CHECK(
        view.slots[0].ability_stable_name ==
        "CommanderFootman");
    CHECK(view.slots[3].kind == ProgressionMenuSlotKind::Utility);
    CHECK(view.slots[4].kind == ProgressionMenuSlotKind::Ultimate);

    CHECK(view.attributes[0].name == "Force");
    CHECK(view.attributes[1].name == "Sagesse");
    CHECK(view.attributes[1].allocated_value == 3U);
}

TEST_CASE("les quarante noms d'affichage et identifiants stables restent tous renseignés") {
    for (std::size_t index = 0U;
         index < kAbilityCount;
         ++index) {
        const auto id =
            ability_id_from_index(index);
        const auto* definition =
            ability_definition(id);
        REQUIRE(definition != nullptr);
        CHECK_FALSE(definition->stable_name.empty());
        CHECK_FALSE(
            progression_ability_display_name(id)
                .empty());
        CHECK(
            progression_ability_display_name(id) !=
            "Compétence inconnue");
    }

    CHECK(
        progression_ability_display_name(
            AbilityId::None) ==
        "Compétence inconnue");
    CHECK(
        progression_path_name(
            static_cast<AbilityPath>(255U)) ==
        "Voie inconnue");
    CHECK(
        progression_attribute_name(
            static_cast<PlayerAttribute>(255U)) ==
        "Attribut inconnu");
}

TEST_CASE("la voie affichee se synchronise explicitement sans reviser le build") {
    ProgressionMenu menu {};
    PlayerBuildState state {};
    state.last_dominant_path =
        AbilityPath::Builder;
    state.revision = 72ULL;

    CHECK(
        menu.sync_selected_path_from_build(
            state,
            100U));
    CHECK(
        menu.selected_path() ==
        AbilityPath::Builder);
    CHECK(state.revision == 72ULL);
    CHECK_FALSE(
        menu.sync_selected_path_from_build(
            state,
            100U));

    REQUIRE(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::KnightVanguardStrike)
            .succeeded());
    const auto revision_after_purchase =
        state.revision;
    CHECK(
        menu.sync_selected_path_from_build(
            state,
            100U));
    CHECK(
        menu.selected_path() ==
        AbilityPath::Knight);
    CHECK(
        state.revision ==
        revision_after_purchase);
}

TEST_CASE("chaque action de build reussie revise exactement une fois et memorise la voie") {
    ProgressionMenu menu {};
    PlayerBuildState state {};
    state.revision = 100ULL;
    menu.set_visible(true);
    menu.select_path(
        AbilityPath::Commander);

    for (std::uint8_t rank = 0U;
         rank <
         static_cast<std::uint8_t>(
             kAbilityRankCount);
         ++rank) {
        const auto revision_before =
            state.revision;
        const auto purchased =
            menu.handle_input(
                ProgressionMenuInput::PurchaseRank,
                state,
                100U);
        CHECK(purchased.build_changed);
        CHECK_FALSE(purchased.ui_changed);
        CHECK(
            state.revision ==
            revision_before + 1ULL);
        CHECK(
            state.last_dominant_path ==
            AbilityPath::Commander);
    }

    const auto mastery_revision =
        state.revision;
    const auto mastered =
        menu.handle_input(
            ProgressionMenuInput::PurchaseMastery,
            state,
            100U);
    CHECK(mastered.build_changed);
    CHECK(
        state.revision ==
        mastery_revision + 1ULL);
    CHECK(
        state.last_dominant_path ==
        AbilityPath::Commander);

    const auto equip_revision =
        state.revision;
    const auto equipped =
        menu.handle_input(
            ProgressionMenuInput::EquipOrUnequip,
            state,
            100U);
    CHECK(equipped.build_changed);
    CHECK(
        state.revision ==
        equip_revision + 1ULL);

    menu.select_path(
        AbilityPath::Ninja);
    const auto attribute_revision =
        state.revision;
    const auto allocated =
        menu.handle_input(
            ProgressionMenuInput::AllocateAgility,
            state,
            100U);
    CHECK(allocated.build_changed);
    CHECK(
        state.revision ==
        attribute_revision + 1ULL);
    CHECK(
        state.last_dominant_path ==
        AbilityPath::Ninja);
}

TEST_CASE("le budget isole les points jouables et la reserve du catalogue") {
    PlayerBuildState state {};
    const auto level_one_budget =
        player_build_point_budget(
            state,
            1U);
    CHECK(
        level_one_budget
            .implemented_skill_point_capacity ==
        4U);
    CHECK(
        level_one_budget
            .spendable_skill_points ==
        1U);
    CHECK(
        level_one_budget
            .reserved_skill_points ==
        0U);

    const auto level_ten_budget =
        player_build_point_budget(
            state,
            10U);
    CHECK(
        level_ten_budget
            .implemented_skill_point_capacity ==
        30U);
    CHECK(
        level_ten_budget
            .spendable_skill_points ==
        10U);
    CHECK(
        level_ten_budget
            .reserved_skill_points ==
        0U);

    const auto level_thirty_one_budget =
        player_build_point_budget(
            state,
            31U);
    CHECK(
        level_thirty_one_budget
            .implemented_skill_point_capacity ==
        30U);
    CHECK(
        level_thirty_one_budget
            .spendable_skill_points ==
        30U);
    CHECK(
        level_thirty_one_budget
            .reserved_skill_points ==
        1U);

    auto budget =
        player_build_point_budget(
            state,
            100U);
    CHECK(
        budget
            .implemented_skill_point_capacity ==
        30U);
    CHECK(
        budget
            .spent_implemented_skill_points ==
        0U);
    CHECK(
        budget.spendable_skill_points ==
        30U);
    CHECK(
        budget.reserved_skill_points ==
        70U);

    constexpr std::array<AbilityId, 5U>
        implemented_abilities {{
            AbilityId::KnightVanguardStrike,
            AbilityId::KnightIronGuard,
            AbilityId::NinjaWindAcceleration,
            AbilityId::CommanderFootman,
            AbilityId::BuilderConstructionPlan,
        }};
    for (const auto ability :
         implemented_abilities) {
        for (std::uint8_t rank = 0U;
             rank <
             static_cast<std::uint8_t>(
                 kAbilityRankCount);
             ++rank) {
            REQUIRE(
                purchase_player_ability_rank(
                    state,
                    100U,
                    ability)
                    .succeeded());
        }
    }

    budget =
        player_build_point_budget(
            state,
            100U);
    CHECK(
        budget.spent_skill_points ==
        30U);
    CHECK(
        budget
            .spent_implemented_skill_points ==
        30U);
    CHECK(
        budget.available_skill_points ==
        70U);
    CHECK(
        budget.spendable_skill_points ==
        0U);
    CHECK(
        budget.reserved_skill_points ==
        70U);
}

TEST_CASE("l'editeur refuse les materiaux non solides ou non placables") {
    CHECK(
        construction_plan_editor_material_is_valid(
            to_block_id(
                BlockType::Planks)));
    CHECK(
        construction_plan_editor_material_is_valid(
            to_block_id(
                BlockType::Stone)));
    CHECK_FALSE(
        construction_plan_editor_material_is_valid(
            to_block_id(
                BlockType::Air)));
    CHECK_FALSE(
        construction_plan_editor_material_is_valid(
            to_block_id(
                BlockType::Water)));
    CHECK_FALSE(
        construction_plan_editor_material_is_valid(
            to_block_id(
                BlockType::Torch)));
    CHECK_FALSE(
        construction_plan_editor_material_is_valid(
            to_block_id(
                BlockType::Sword)));
    CHECK_FALSE(
        construction_plan_editor_material_is_valid(
            std::numeric_limits<
                std::uint16_t>::max()));
}

TEST_CASE("l'editeur garde ses trois plans en brouillon et annule atomiquement") {
    PlayerBuildState state {};
    state.revision = 14ULL;
    state.construction_plans[0U]
        .cell_count = 1U;
    state.construction_plans[0U]
        .cells[0U] = {
        0,
        0,
        0,
        to_block_id(
            BlockType::Planks),
    };
    const auto original =
        state;

    ConstructionPlanEditor editor {};
    const auto opened =
        editor.begin_editing(
            state);
    CHECK(opened.ui_changed);
    CHECK_FALSE(opened.build_changed);
    CHECK(editor.active());
    REQUIRE(
        editor.select_plan(2U)
            .succeeded());
    REQUIRE(
        editor.set_shape(
                  ConstructionPlanShape::Grid)
            .succeeded());
    REQUIRE(
        editor.select_layer(4)
            .succeeded());
    REQUIRE(
        editor.set_cursor(2, 4, -3)
            .succeeded());
    REQUIRE(
        editor.set_material(
                  to_block_id(
                      BlockType::Stone))
            .succeeded());
    REQUIRE(
        editor.place_cell()
            .succeeded());

    const auto view =
        editor.make_view_model();
    CHECK(view.active);
    CHECK(view.selected_plan == 2U);
    CHECK(
        view.shape ==
        ConstructionPlanShape::Grid);
    CHECK(view.selected_layer == 4);
    CHECK(view.cell_count == 1U);
    CHECK(
        view.cells_on_selected_layer ==
        1U);
    CHECK(view.dirty);
    CHECK(view.can_commit);
    CHECK(state == original);

    const auto canceled =
        editor.cancel();
    CHECK(canceled.ui_changed);
    CHECK_FALSE(canceled.build_changed);
    CHECK_FALSE(editor.active());
    CHECK(state == original);
}

TEST_CASE("l'editeur borne la grille 3D et limite chaque plan a dix cellules") {
    PlayerBuildState state {};
    ConstructionPlanEditor editor {};
    REQUIRE(
        editor.begin_editing(
                  state)
            .succeeded());
    REQUIRE(
        editor.select_plan(1U)
            .succeeded());
    REQUIRE(
        editor.move_cursor(
                  std::numeric_limits<int>::max(),
                  std::numeric_limits<int>::min(),
                  std::numeric_limits<int>::max())
            .succeeded());
    CHECK(
        editor.cursor() ==
        (ConstructionPlanEditorCursor {
            32,
            -32,
            32,
        }));

    for (int index = 0;
         index < 10;
         ++index) {
        REQUIRE(
            editor.set_cursor(
                      index,
                      3,
                      0)
                .succeeded());
        REQUIRE(
            editor.place_cell()
                .succeeded());
    }
    REQUIRE(
        editor.set_cursor(
                  10,
                  3,
                  0)
            .succeeded());
    const auto full =
        editor.place_cell();
    CHECK(
        full.failure ==
        ConstructionPlanEditorFailure::PlanFull);
    CHECK_FALSE(full.any_changed());
    CHECK(
        editor.make_view_model()
            .cell_count ==
        10U);
    CHECK(
        editor.select_plan(3U)
            .failure ==
        ConstructionPlanEditorFailure::InvalidPlan);
}

TEST_CASE("le commit de plan est atomique conflictuel et ne revise qu'une fois") {
    PlayerBuildState state {};
    state.revision = 50ULL;
    state.ability_masteries[
        ability_index(
            AbilityId::BuilderConstructionPlan)] =
        1U;

    ConstructionPlanEditor editor {};
    REQUIRE(
        editor.begin_editing(
                  state)
            .succeeded());
    REQUIRE(
        editor.select_plan(1U)
            .succeeded());
    REQUIRE(
        editor.set_shape(
                  ConstructionPlanShape::Grid)
            .succeeded());
    REQUIRE(
        editor.set_mirrored(true)
            .succeeded());
    REQUIRE(
        editor.set_cursor(1, 2, 3)
            .succeeded());
    REQUIRE(
        editor.set_material(
                  to_block_id(
                      BlockType::Stone))
            .succeeded());
    REQUIRE(
        editor.place_cell()
            .succeeded());

    const auto committed =
        editor.commit(
            state);
    CHECK(committed.ui_changed);
    CHECK(committed.build_changed);
    CHECK(state.revision == 51ULL);
    CHECK(
        state.last_dominant_path ==
        AbilityPath::Builder);
    CHECK(
        state.selected_construction_plan ==
        1U);
    const auto& plan =
        state.construction_plans[1U];
    CHECK(
        plan.shape ==
        ConstructionPlanShape::Grid);
    CHECK(plan.mirrored);
    CHECK(plan.cell_count == 1U);
    CHECK(plan.cells[0U].x == 1);
    CHECK(plan.cells[0U].y == 2);
    CHECK(plan.cells[0U].z == 3);
    CHECK(
        plan.cells[0U].material_id ==
        to_block_id(
            BlockType::Stone));
    CHECK_FALSE(editor.active());

    ConstructionPlanEditor conflicting_editor {};
    REQUIRE(
        conflicting_editor
            .begin_editing(
                state)
            .succeeded());
    REQUIRE(
        conflicting_editor
            .set_cursor(4, 0, 0)
            .succeeded());
    REQUIRE(
        conflicting_editor
            .place_cell()
            .succeeded());
    ++state.revision;
    const auto state_after_external_change =
        state;
    const auto conflict =
        conflicting_editor.commit(
            state);
    CHECK(
        conflict.failure ==
        ConstructionPlanEditorFailure::
            ConcurrentBuildMutation);
    CHECK_FALSE(conflict.any_changed());
    CHECK(
        state ==
        state_after_external_change);
    CHECK(
        conflicting_editor.active());
}

TEST_CASE("un commit de plan sans changement ferme l'editeur sans toucher au build") {
    PlayerBuildState state {};
    state.revision = 8ULL;
    state.last_dominant_path =
        AbilityPath::Knight;
    const auto original =
        state;

    ConstructionPlanEditor editor {};
    REQUIRE(
        editor.begin_editing(
                  state)
            .succeeded());
    const auto committed =
        editor.commit(
            state);
    CHECK(committed.ui_changed);
    CHECK_FALSE(committed.build_changed);
    CHECK(state == original);
}

TEST_CASE("le layout pur reste contenu et sans chevauchement aux resolutions supportees") {
    constexpr std::array<
        std::array<int, 2U>,
        3U>
        viewports {{
            {640, 360},
            {1280, 720},
            {1600, 900},
        }};

    for (const auto& viewport_size :
         viewports) {
        const auto width =
            viewport_size[0U];
        const auto height =
            viewport_size[1U];
        const auto layout =
            make_progression_menu_layout(
                width,
                height,
                ProgressionMenuPage::
                    ConstructionPlan);
        CAPTURE(width);
        CAPTURE(height);
        REQUIRE(layout.valid());
        CHECK(
            layout.page ==
            ProgressionMenuPage::
                ConstructionPlan);

        const auto viewport =
            ProgressionMenuRect {
                0.0F,
                0.0F,
                static_cast<float>(
                    width),
                static_cast<float>(
                    height),
            };
        CHECK(
            progression_menu_rect_contains(
                viewport,
                layout.panel));
        CHECK(
            progression_menu_rect_contains(
                layout.panel,
                layout.title));
        CHECK(
            progression_menu_rect_contains(
                layout.panel,
                layout.summary));
        CHECK(
            progression_menu_rect_contains(
                layout.panel,
                layout.navigation));
        CHECK(
            progression_menu_rect_contains(
                layout.panel,
                layout.primary_content));
        CHECK(
            progression_menu_rect_contains(
                layout.panel,
                layout.footer));
        CHECK_FALSE(
            progression_menu_rects_overlap(
                layout.title,
                layout.summary));
        CHECK_FALSE(
            progression_menu_rects_overlap(
                layout.summary,
                layout.navigation));
        CHECK_FALSE(
            progression_menu_rects_overlap(
                layout.navigation,
                layout.primary_content));
        CHECK_FALSE(
            progression_menu_rects_overlap(
                layout.primary_content,
                layout.footer));

        if (layout.mode ==
            ProgressionMenuLayoutMode::
                TwoColumns) {
            CHECK(
                progression_menu_rect_contains(
                    layout.panel,
                    layout.secondary_content));
            CHECK_FALSE(
                progression_menu_rects_overlap(
                    layout.primary_content,
                    layout.secondary_content));
            CHECK_FALSE(
                progression_menu_rects_overlap(
                    layout.secondary_content,
                    layout.footer));
        } else {
            CHECK(
                layout.secondary_content
                    .empty());
        }
    }
}

} // namespace valcraft
