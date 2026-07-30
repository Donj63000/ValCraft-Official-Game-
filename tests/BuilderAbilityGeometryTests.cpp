#include "gameplay/progression/BuilderAbilityGeometry.h"
#include "gameplay/progression/AbilityCatalog.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <set>
#include <tuple>

namespace valcraft {

namespace {

[[nodiscard]] auto unique_coordinates(
    const BuilderAbilityCellSet& set) {
    std::set<
        std::tuple<int, int, int>>
        coordinates {};
    for (const auto& cell :
         set.cell_span()) {
        coordinates.emplace(
            cell.coordinate.x,
            cell.coordinate.y,
            cell.coordinate.z);
    }
    return coordinates;
}

} // namespace

TEST_CASE("les trois murs gardent leurs dimensions et leur seuil de soixante pour cent") {
    constexpr std::array expected_counts {
        std::size_t {6U},
        std::size_t {9U},
        std::size_t {15U},
    };
    for (auto rank = std::uint8_t {1U};
         rank <= 3U;
         ++rank) {
        const auto wall =
            generate_deployable_wall({
                {10, 20, 30},
                to_block_id(
                    BlockType::Stone),
                rank,
                WorldEditAxis::X,
                false,
            });
        REQUIRE(wall.valid);
        CHECK(
            wall.cell_count ==
            expected_counts[
                static_cast<std::size_t>(
                    rank - 1U)]);
        CHECK(
            unique_coordinates(wall)
                .size() ==
            wall.cell_count);
        CHECK(
            deployable_wall_minimum_placement_count(
                wall.cell_count) ==
            (rank == 1U
                 ? 4U
                 : (rank == 2U
                        ? 6U
                        : 9U)));
    }
}

TEST_CASE("le catalogue des opérations bâtisseur correspond aux générateurs") {
    const auto* repair =
        ability_definition(
            AbilityId::BuilderExpressRepair);
    const auto* wall =
        ability_definition(
            AbilityId::BuilderDeployableWall);
    const auto* bridge =
        ability_definition(
            AbilityId::BuilderModularBridge);
    const auto* excavation =
        ability_definition(
            AbilityId::BuilderExcavationWave);
    REQUIRE(repair != nullptr);
    REQUIRE(wall != nullptr);
    REQUIRE(bridge != nullptr);
    REQUIRE(excavation != nullptr);
    CHECK_FALSE(repair->implemented);
    CHECK_FALSE(wall->implemented);
    CHECK_FALSE(bridge->implemented);
    CHECK_FALSE(excavation->implemented);
    CHECK(repair->ranks[2U].values[0U] == doctest::Approx(0.50F));
    CHECK(repair->ranks[2U].values[1U] == doctest::Approx(8.0F));
    CHECK(repair->ranks[2U].values[2U] == doctest::Approx(0.20F));
    CHECK(wall->ranks[0U].values[2U] == doctest::Approx(6.0F));
    CHECK(wall->ranks[2U].values[2U] == doctest::Approx(15.0F));
    CHECK(bridge->ranks[0U].values[0U] == doctest::Approx(5.0F));
    CHECK(bridge->ranks[2U].values[0U] == doctest::Approx(9.0F));
    CHECK(bridge->ranks[2U].values[1U] == doctest::Approx(2.0F));
    CHECK(excavation->ranks[0U].values[3U] == doctest::Approx(3.0F));
    CHECK(excavation->ranks[1U].values[3U] == doctest::Approx(9.0F));
    CHECK(excavation->ranks[2U].values[3U] == doctest::Approx(18.0F));
    CHECK(excavation->ranks[2U].range_meters == doctest::Approx(5.0F));
}

TEST_CASE("le mur rang trois peut réserver une vraie ouverture centrale") {
    const auto full =
        generate_deployable_wall({
            {0, 0, 0},
            to_block_id(
                BlockType::Planks),
            3U,
            WorldEditAxis::Z,
            false,
        });
    const auto opened =
        generate_deployable_wall({
            {0, 0, 0},
            to_block_id(
                BlockType::Planks),
            3U,
            WorldEditAxis::Z,
            true,
        });
    REQUIRE(full.valid);
    REQUIRE(opened.valid);
    CHECK(full.cell_count == 15U);
    CHECK(opened.cell_count == 13U);
    CHECK(
        std::none_of(
            opened.cell_span().begin(),
            opened.cell_span().end(),
            [](const BuilderAbilityCell& cell) {
                return cell.coordinate.x == 0 &&
                       cell.coordinate.z == 0 &&
                       cell.coordinate.y < 2;
            }));
}

TEST_CASE("les ponts respectent longueur pente largeur et garde-corps optionnels") {
    const auto rank_one =
        generate_modular_bridge({
            {0, 4, 0},
            to_block_id(
                BlockType::Planks),
            1U,
            WorldEditAxis::X,
            WorldEditDirection::Positive,
            WorldEditDirection::Positive,
            0,
            false,
            false,
        });
    REQUIRE(rank_one.valid);
    CHECK(rank_one.cell_count == 5U);
    CHECK(rank_one.cells[4U].coordinate.x == 4);
    CHECK(rank_one.cells[4U].coordinate.y == 4);

    const auto rank_two =
        generate_modular_bridge({
            {0, 4, 0},
            to_block_id(
                BlockType::Planks),
            2U,
            WorldEditAxis::Z,
            WorldEditDirection::Negative,
            WorldEditDirection::Positive,
            1,
            false,
            false,
        });
    REQUIRE(rank_two.valid);
    CHECK(rank_two.cell_count == 7U);
    CHECK(rank_two.cells[6U].coordinate.z == -6);
    CHECK(rank_two.cells[6U].coordinate.y == 7);

    const auto rank_three =
        generate_modular_bridge({
            {0, 4, 0},
            to_block_id(
                BlockType::Planks),
            3U,
            WorldEditAxis::X,
            WorldEditDirection::Positive,
            WorldEditDirection::Negative,
            -1,
            true,
            true,
        });
    REQUIRE(rank_three.valid);
    CHECK(rank_three.cell_count == 36U);
    CHECK(
        std::count_if(
            rank_three.cell_span().begin(),
            rank_three.cell_span().end(),
            [](const BuilderAbilityCell& cell) {
                return cell.role ==
                       BuilderAbilityCellRole::
                           Permanent;
            }) == 18);
    CHECK(
        std::count_if(
            rank_three.cell_span().begin(),
            rank_three.cell_span().end(),
            [](const BuilderAbilityCell& cell) {
                return cell.role ==
                       BuilderAbilityCellRole::
                           OptionalGuardRail;
            }) == 18);
    CHECK(
        unique_coordinates(rank_three)
            .size() ==
        rank_three.cell_count);
}

TEST_CASE("l'onde d'excavation produit exactement trois neuf et dix-huit cellules") {
    constexpr std::array expected_counts {
        std::size_t {3U},
        std::size_t {9U},
        std::size_t {18U},
    };
    for (auto rank = std::uint8_t {1U};
         rank <= 3U;
         ++rank) {
        const auto excavation =
            generate_excavation_wave({
                {5, 6, 7},
                rank,
                WorldEditFace::PositiveZ,
                WorldEditAxis::X,
            });
        REQUIRE(excavation.valid);
        CHECK(
            excavation.cell_count ==
            expected_counts[
                static_cast<std::size_t>(
                    rank - 1U)]);
        CHECK(
            unique_coordinates(
                excavation)
                .size() ==
            excavation.cell_count);
        CHECK(
            std::all_of(
                excavation.cell_span()
                    .begin(),
                excavation.cell_span()
                    .end(),
                [](const BuilderAbilityCell& cell) {
                    return cell.role ==
                               BuilderAbilityCellRole::
                                   Excavation &&
                           cell.block_id ==
                               to_block_id(
                                   BlockType::Air);
                }));
    }
}

TEST_CASE("la seconde couche d'excavation entre dans la face touchée") {
    const auto positive =
        generate_excavation_wave({
            {0, 0, 10},
            3U,
            WorldEditFace::PositiveZ,
            WorldEditAxis::X,
        });
    const auto negative =
        generate_excavation_wave({
            {0, 0, 10},
            3U,
            WorldEditFace::NegativeZ,
            WorldEditAxis::X,
        });
    REQUIRE(positive.valid);
    REQUIRE(negative.valid);
    CHECK(
        std::count_if(
            positive.cell_span().begin(),
            positive.cell_span().end(),
            [](const BuilderAbilityCell& cell) {
                return cell.coordinate.z == 9;
            }) == 9);
    CHECK(
        std::count_if(
            negative.cell_span().begin(),
            negative.cell_span().end(),
            [](const BuilderAbilityCell& cell) {
                return cell.coordinate.z == 11;
            }) == 9);
}

TEST_CASE("réparation express expose les valeurs exactes et sa chaîne maîtrisée") {
    constexpr std::array ratios {
        0.25F,
        0.35F,
        0.50F,
    };
    constexpr std::array cells {
        std::uint8_t {4U},
        std::uint8_t {6U},
        std::uint8_t {8U},
    };
    for (auto rank = std::uint8_t {1U};
         rank <= 3U;
         ++rank) {
        const auto ordinary =
            express_repair_parameters(
                rank,
                false);
        const auto mastered =
            express_repair_parameters(
                rank,
                true);
        const auto index =
            static_cast<std::size_t>(
                rank - 1U);
        CHECK(
            ordinary.durability_ratio ==
            doctest::Approx(
                ratios[index]));
        CHECK(
            ordinary.maximum_plan_cells ==
            cells[index]);
        CHECK(
            ordinary.shield_ratio ==
            doctest::Approx(
                rank == 3U
                    ? 0.20F
                    : 0.0F));
        CHECK(
            mastered.mastery_chain_targets ==
            3U);
        CHECK(
            mastered.mastery_chain_radius ==
            doctest::Approx(4.0F));
        CHECK(
            mastered.mastery_chain_ratio ==
            doctest::Approx(0.50F));
    }
}

TEST_CASE("les requêtes invalides ne produisent jamais de géométrie partielle") {
    CHECK_FALSE(
        generate_deployable_wall({
            {},
            to_block_id(
                BlockType::Water),
            1U,
            WorldEditAxis::X,
            false,
        })
            .valid);
    CHECK_FALSE(
        generate_modular_bridge({
            {},
            to_block_id(
                BlockType::Planks),
            1U,
            WorldEditAxis::X,
            WorldEditDirection::Positive,
            WorldEditDirection::Positive,
            1,
            false,
            false,
        })
            .valid);
    CHECK_FALSE(
        generate_excavation_wave({
            {},
            4U,
            WorldEditFace::PositiveY,
            WorldEditAxis::X,
        })
            .valid);
    CHECK_FALSE(
        generate_excavation_wave({
            {},
            1U,
            static_cast<WorldEditFace>(
                255U),
            WorldEditAxis::X,
        })
            .valid);
    CHECK(
        express_repair_parameters(
            0U,
            true)
            .maximum_plan_cells == 0U);
}

} // namespace valcraft
