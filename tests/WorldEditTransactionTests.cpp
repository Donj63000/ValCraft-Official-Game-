#include "gameplay/progression/WorldEditTransaction.h"
#include "world/World.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace valcraft {
namespace {

struct CoordinateLess {
    auto operator()(
        const BlockCoord& lhs,
        const BlockCoord& rhs) const noexcept -> bool {
        if (lhs.x != rhs.x) {
            return lhs.x < rhs.x;
        }
        if (lhs.y != rhs.y) {
            return lhs.y < rhs.y;
        }
        return lhs.z < rhs.z;
    }
};

struct TransactionFixture {
    std::map<BlockCoord, BlockId, CoordinateLess> world {};
    std::array<std::uint32_t, 256U> materials {};
    std::vector<WorldEditCell> commits {};
    std::vector<WorldEditCell> rollbacks {};
    std::vector<std::pair<BlockId, std::uint32_t>> refunds {};
    std::optional<BlockCoord> occupied {};
    std::optional<BlockId> failed_consumption {};
    std::size_t validation_count = 0U;
    std::size_t availability_count = 0U;
    std::size_t commit_attempt_count = 0U;
    std::size_t fail_commit_at = std::numeric_limits<std::size_t>::max();

    [[nodiscard]] auto callbacks()
        -> WorldEditTransactionCallbacks {
        WorldEditTransactionCallbacks result {};
        result.validate_cell =
            [this](const WorldEditCell&) {
            CHECK(commits.empty());
            ++validation_count;
            return true;
        };
        result.cell_contains_player_or_creature =
            [this](const BlockCoord& coordinate) {
            return occupied.has_value() &&
                   *occupied == coordinate;
        };
        result.read_current =
            [this](const BlockCoord& coordinate)
                -> std::optional<WorldEditCellState> {
            const auto found =
                world.find(coordinate);
            return WorldEditCellState {
                coordinate,
                found == world.end()
                    ? to_block_id(BlockType::Air)
                    : found->second,
                0U,
                false,
            };
        };
        result.commit_cell =
            [this](const WorldEditCell& cell) {
            CHECK(validation_count > 0U);
            const auto attempt =
                commit_attempt_count;
            ++commit_attempt_count;
            if (attempt == fail_commit_at) {
                return false;
            }
            world[cell.coordinate] = cell.block_id;
            commits.push_back(cell);
            return true;
        };
        result.rollback_cell =
            [this](const WorldEditCellState& cell) {
            world[cell.coordinate] = cell.block_id;
            rollbacks.push_back({
                cell.coordinate,
                cell.block_id,
            });
        };
        result.materials_available =
            [this](
                BlockId block_id,
                std::uint32_t count) {
            CHECK(validation_count > 0U);
            CHECK(commits.empty());
            ++availability_count;
            return materials[block_id] >= count;
        };
        result.consume_materials =
            [this](
                BlockId block_id,
                std::uint32_t count) {
            if (failed_consumption.has_value() &&
                *failed_consumption == block_id) {
                return false;
            }
            if (materials[block_id] < count) {
                return false;
            }
            materials[block_id] -= count;
            return true;
        };
        result.refund_materials =
            [this](
                BlockId block_id,
                std::uint32_t count) {
            materials[block_id] += count;
            refunds.emplace_back(
                block_id,
                count);
        };
        return result;
    }
};

[[nodiscard]] auto has_coordinate(
    std::span<const WorldEditCell> cells,
    const BlockCoord& coordinate) -> bool {
    return std::any_of(
        cells.begin(),
        cells.end(),
        [&coordinate](const WorldEditCell& cell) {
            return cell.coordinate == coordinate;
        });
}

} // namespace

TEST_CASE("la transaction valide toutes les cellules et le cout avant toute mutation") {
    TransactionFixture fixture {};
    fixture.materials[
        to_block_id(BlockType::Stone)] = 4U;
    const std::array cells {
        WorldEditCell {
            {2, 4, 1},
            to_block_id(BlockType::Stone),
        },
        WorldEditCell {
            {-1, 4, 1},
            to_block_id(BlockType::Stone),
        },
    };

    const auto result =
        WorldEditTransaction::execute(
            cells,
            fixture.callbacks());

    CHECK(result.status ==
          WorldEditTransactionStatus::Success);
    CHECK(result.succeeded());
    CHECK(result.requested_cell_count == 2U);
    CHECK(result.unique_cell_count == 2U);
    CHECK(result.changed_cell_count == 2U);
    CHECK(result.commit_count == 2U);
    CHECK(result.rollback_count == 0U);
    CHECK(fixture.validation_count == 2U);
    CHECK(fixture.availability_count == 1U);
    REQUIRE(fixture.commits.size() == 2U);
    CHECK(fixture.commits[0].coordinate ==
          BlockCoord {-1, 4, 1});
    CHECK(fixture.commits[1].coordinate ==
          BlockCoord {2, 4, 1});
    CHECK(fixture.materials[
              to_block_id(BlockType::Stone)] ==
          2U);
}

TEST_CASE("un echec de commit restaure exactement le monde et rembourse les materiaux") {
    TransactionFixture fixture {};
    const auto air =
        to_block_id(BlockType::Air);
    const auto wood =
        to_block_id(BlockType::Wood);
    fixture.world[{0, 2, 0}] =
        to_block_id(BlockType::Dirt);
    fixture.world[{1, 2, 0}] =
        to_block_id(BlockType::Grass);
    fixture.world[{2, 2, 0}] = air;
    const auto original = fixture.world;
    fixture.materials[wood] = 3U;
    fixture.fail_commit_at = 2U;
    const std::array cells {
        WorldEditCell {{2, 2, 0}, wood},
        WorldEditCell {{0, 2, 0}, wood},
        WorldEditCell {{1, 2, 0}, wood},
    };

    const auto result =
        WorldEditTransaction::execute(
            cells,
            fixture.callbacks());

    CHECK(result.status ==
          WorldEditTransactionStatus::CommitFailed);
    CHECK_FALSE(result.succeeded());
    CHECK(result.commit_count == 2U);
    CHECK(result.rollback_count == 2U);
    CHECK(fixture.world == original);
    CHECK(fixture.materials[wood] == 3U);
    REQUIRE(fixture.rollbacks.size() == 2U);
    CHECK(fixture.rollbacks[0].coordinate ==
          fixture.commits[1].coordinate);
    CHECK(fixture.rollbacks[1].coordinate ==
          fixture.commits[0].coordinate);
    REQUIRE(fixture.refunds.size() == 1U);
    CHECK(fixture.refunds.front() ==
          std::pair {wood, 3U});
}

TEST_CASE("une cible invalide ou occupee ne consomme rien et ne modifie rien") {
    TransactionFixture fixture {};
    const auto planks =
        to_block_id(BlockType::Planks);
    fixture.materials[planks] = 8U;
    fixture.occupied = BlockCoord {1, 3, 1};
    const std::array cells {
        WorldEditCell {{0, 3, 1}, planks},
        WorldEditCell {{1, 3, 1}, planks},
    };

    const auto result =
        WorldEditTransaction::execute(
            cells,
            fixture.callbacks());

    CHECK(result.status ==
          WorldEditTransactionStatus::InvalidTarget);
    CHECK(fixture.world.empty());
    CHECK(fixture.commits.empty());
    CHECK(fixture.refunds.empty());
    CHECK(fixture.materials[planks] == 8U);
    CHECK(fixture.availability_count == 0U);
}

TEST_CASE("un manque de materiaux est detecte avant tout commit") {
    TransactionFixture fixture {};
    const auto stone =
        to_block_id(BlockType::Stone);
    fixture.materials[stone] = 1U;
    const std::array cells {
        WorldEditCell {{0, 0, 0}, stone},
        WorldEditCell {{1, 0, 0}, stone},
    };

    const auto result =
        WorldEditTransaction::execute(
            cells,
            fixture.callbacks());

    CHECK(result.status ==
          WorldEditTransactionStatus::
              InsufficientMaterials);
    CHECK(fixture.commits.empty());
    CHECK(fixture.world.empty());
    CHECK(fixture.materials[stone] == 1U);
}

TEST_CASE("un refus de consommation rembourse les materiaux deja retires") {
    TransactionFixture fixture {};
    const auto stone =
        to_block_id(BlockType::Stone);
    const auto wood =
        to_block_id(BlockType::Wood);
    fixture.materials[stone] = 2U;
    fixture.materials[wood] = 2U;
    fixture.failed_consumption = wood;
    const std::array cells {
        WorldEditCell {{0, 0, 0}, stone},
        WorldEditCell {{1, 0, 0}, wood},
    };

    const auto result =
        WorldEditTransaction::execute(
            cells,
            fixture.callbacks());

    CHECK(result.status ==
          WorldEditTransactionStatus::
              InsufficientMaterials);
    CHECK(fixture.commits.empty());
    CHECK(fixture.world.empty());
    CHECK(fixture.materials[stone] == 2U);
    CHECK(fixture.materials[wood] == 2U);
    REQUIRE(fixture.refunds.size() == 1U);
    CHECK(fixture.refunds.front() ==
          std::pair {stone, 1U});
}

TEST_CASE("les doublons sont dedupliques et tries independamment de l'ordre") {
    TransactionFixture first_fixture {};
    TransactionFixture second_fixture {};
    const auto stone =
        to_block_id(BlockType::Stone);
    const auto wood =
        to_block_id(BlockType::Wood);
    first_fixture.materials[stone] = 3U;
    second_fixture.materials[stone] = 3U;
    first_fixture.materials[wood] = 3U;
    second_fixture.materials[wood] = 3U;
    const std::array first {
        WorldEditCell {{4, 1, 2}, wood},
        WorldEditCell {{-2, 1, 2}, stone},
        WorldEditCell {{4, 1, 2}, stone},
        WorldEditCell {{0, 1, 2}, stone},
    };
    const std::array second {
        first[2],
        first[3],
        first[0],
        first[1],
    };

    const auto first_result =
        WorldEditTransaction::execute(
            first,
            first_fixture.callbacks());
    const auto second_result =
        WorldEditTransaction::execute(
            second,
            second_fixture.callbacks());

    REQUIRE(first_result.succeeded());
    REQUIRE(second_result.succeeded());
    CHECK(first_result.unique_cell_count == 3U);
    CHECK(second_result.unique_cell_count == 3U);
    CHECK(first_fixture.commits ==
          second_fixture.commits);
    REQUIRE(first_fixture.commits.size() == 3U);
    CHECK(first_fixture.commits.back().block_id ==
          stone);
    CHECK(first_fixture.materials[wood] == 3U);
    CHECK(first_fixture.materials[stone] == 0U);
}

TEST_CASE("la limite porte sur les cellules demandees avant deduplication") {
    TransactionFixture fixture {};
    std::array<
        WorldEditCell,
        kWorldEditMaximumCellCount + 1U>
        cells {};
    for (auto& cell : cells) {
        cell = {
            {0, 0, 0},
            to_block_id(BlockType::Stone),
        };
    }

    const auto result =
        WorldEditTransaction::execute(
            cells,
            fixture.callbacks());

    CHECK(result.status ==
          WorldEditTransactionStatus::TooManyCells);
    CHECK(result.requested_cell_count ==
          kWorldEditMaximumCellCount + 1U);
    CHECK(fixture.validation_count == 0U);
    CHECK(fixture.commits.empty());
}

TEST_CASE("le generateur produit les lignes contractuelles selon la face et l'axe") {
    const auto rank_one =
        generate_world_edit_shape({
            {10, 20, 30},
            to_block_id(BlockType::Stone),
            1U,
            WorldEditShape::Line,
            WorldEditFace::PositiveY,
            WorldEditAxis::X,
            WorldEditDirection::Positive,
            false,
        });
    REQUIRE(rank_one.valid);
    CHECK(rank_one.cell_count == 2U);
    CHECK(has_coordinate(
        rank_one.cell_span(),
        {10, 20, 30}));
    CHECK(has_coordinate(
        rank_one.cell_span(),
        {11, 20, 30}));

    const auto rank_two =
        generate_world_edit_shape({
            {10, 20, 30},
            to_block_id(BlockType::Stone),
            2U,
            WorldEditShape::Line,
            WorldEditFace::NegativeX,
            WorldEditAxis::Z,
            WorldEditDirection::Negative,
            false,
        });
    REQUIRE(rank_two.valid);
    CHECK(rank_two.cell_count == 3U);
    CHECK(has_coordinate(
        rank_two.cell_span(),
        {10, 20, 28}));

    const auto rank_three =
        generate_world_edit_shape({
            {10, 20, 30},
            to_block_id(BlockType::Stone),
            3U,
            WorldEditShape::Line,
            WorldEditFace::PositiveX,
            WorldEditAxis::Y,
            WorldEditDirection::Positive,
            false,
        });
    REQUIRE(rank_three.valid);
    CHECK(rank_three.cell_count == 5U);
    CHECK(has_coordinate(
        rank_three.cell_span(),
        {10, 24, 30}));
}

TEST_CASE("le rang trois produit une grille trois par trois dans le plan de la face") {
    const auto grid =
        generate_world_edit_shape({
            {5, 6, 7},
            to_block_id(BlockType::Planks),
            3U,
            WorldEditShape::Grid,
            WorldEditFace::PositiveY,
            WorldEditAxis::X,
            WorldEditDirection::Positive,
            false,
        });

    REQUIRE(grid.valid);
    CHECK(grid.cell_count == 9U);
    for (int x = 4; x <= 6; ++x) {
        for (int z = 6; z <= 8; ++z) {
            CHECK(has_coordinate(
                grid.cell_span(),
                {x, 6, z}));
        }
    }
}

TEST_CASE("la maitrise miroir reste dedupliquee deterministe et bornee a dix cellules") {
    const WorldEditShapeRequest request {
        {0, 0, 0},
        to_block_id(BlockType::Cobblestone),
        3U,
        WorldEditShape::Line,
        WorldEditFace::PositiveY,
        WorldEditAxis::X,
        WorldEditDirection::Positive,
        true,
    };
    const auto first =
        generate_world_edit_shape(request);
    const auto second =
        generate_world_edit_shape(request);

    REQUIRE(first.valid);
    REQUIRE(second.valid);
    CHECK(first.cell_count == 9U);
    CHECK(first.cell_count <=
          kWorldEditShapeMaximumCellCount);
    CHECK(std::equal(
        first.cell_span().begin(),
        first.cell_span().end(),
        second.cell_span().begin(),
        second.cell_span().end()));
    for (int x = -4; x <= 4; ++x) {
        CHECK(has_coordinate(
            first.cell_span(),
            {x, 0, 0}));
    }
}

TEST_CASE("une orientation hors du plan de la face est rejetee sans debordement") {
    const auto invalid_axis =
        generate_world_edit_shape({
            {},
            to_block_id(BlockType::Stone),
            2U,
            WorldEditShape::Line,
            WorldEditFace::PositiveX,
            WorldEditAxis::X,
            WorldEditDirection::Positive,
            false,
        });
    CHECK_FALSE(invalid_axis.valid);
    CHECK(invalid_axis.cell_span().empty());

    const auto overflow =
        generate_world_edit_shape({
            {
                std::numeric_limits<int>::max(),
                0,
                0,
            },
            to_block_id(BlockType::Stone),
            3U,
            WorldEditShape::Line,
            WorldEditFace::PositiveY,
            WorldEditAxis::X,
            WorldEditDirection::Positive,
            false,
        });
    CHECK_FALSE(overflow.valid);
    CHECK(overflow.cell_span().empty());
}

TEST_CASE("un rollback réel restaure bloc eau et provenance joueur") {
    World world(
        77191,
        0,
        WorldGenerationProfile::OceanAdventure);
    world.ensure_chunk_loaded({0, 0});
    const auto surface =
        world.loaded_surface_height(
            2,
            2);
    REQUIRE(surface.has_value());
    const std::array requested {
        WorldEditCell {
            {2, *surface + 4, 2},
            to_block_id(BlockType::Planks),
        },
        WorldEditCell {
            {3, *surface + 4, 2},
            to_block_id(BlockType::Planks),
        },
    };
    const auto first_before =
        world.capture_cell_snapshot(
            requested[0].coordinate.x,
            requested[0].coordinate.y,
            requested[0].coordinate.z);
    const auto second_before =
        world.capture_cell_snapshot(
            requested[1].coordinate.x,
            requested[1].coordinate.y,
            requested[1].coordinate.z);
    REQUIRE(first_before.has_value());
    REQUIRE(second_before.has_value());

    auto commit_count = std::size_t {0U};
    WorldEditTransactionCallbacks callbacks {};
    callbacks.validate_cell =
        [](const WorldEditCell&) {
            return true;
        };
    callbacks.cell_contains_player_or_creature =
        [](const BlockCoord&) {
            return false;
        };
    callbacks.read_current =
        [&world](const BlockCoord& coordinate)
            -> std::optional<WorldEditCellState> {
        const auto snapshot =
            world.capture_cell_snapshot(
                coordinate.x,
                coordinate.y,
                coordinate.z);
        if (!snapshot.has_value()) {
            return std::nullopt;
        }
        return WorldEditCellState {
            snapshot->coordinate,
            snapshot->block,
            snapshot->water_state,
            snapshot->player_placed,
        };
    };
    callbacks.commit_cell =
        [&world, &commit_count](
            const WorldEditCell& cell) {
        if (commit_count++ == 1U) {
            return false;
        }
        return world.set_player_block(
            cell.coordinate.x,
            cell.coordinate.y,
            cell.coordinate.z,
            cell.block_id);
    };
    callbacks.rollback_cell =
        [&world](const WorldEditCellState& cell) {
        REQUIRE(
            world.restore_cell_snapshot({
                cell.coordinate,
                cell.block_id,
                cell.water_state,
                cell.player_placed,
            }));
    };
    callbacks.materials_available =
        [](BlockId, std::uint32_t) {
            return true;
        };
    callbacks.consume_materials =
        [](BlockId, std::uint32_t) {
            return true;
        };
    callbacks.refund_materials =
        [](BlockId, std::uint32_t) {};

    const auto result =
        WorldEditTransaction::execute(
            requested,
            callbacks);
    CHECK(
        result.status ==
        WorldEditTransactionStatus::CommitFailed);
    CHECK(result.commit_count == 1U);
    CHECK(result.rollback_count == 1U);
    CHECK(
        world.capture_cell_snapshot(
            requested[0].coordinate.x,
            requested[0].coordinate.y,
            requested[0].coordinate.z) ==
        first_before);
    CHECK(
        world.capture_cell_snapshot(
            requested[1].coordinate.x,
            requested[1].coordinate.y,
            requested[1].coordinate.z) ==
        second_before);
}

} // namespace valcraft
