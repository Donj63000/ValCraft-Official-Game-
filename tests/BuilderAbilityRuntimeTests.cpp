#include "gameplay/progression/BuilderAbilityRuntime.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

namespace valcraft {
namespace {

struct CoordinateLess {
    auto operator()(const BlockCoord& lhs, const BlockCoord& rhs) const
        noexcept -> bool {
        return std::tie(lhs.x, lhs.y, lhs.z) <
               std::tie(rhs.x, rhs.y, rhs.z);
    }
};

struct BuilderRuntimeFixture {
    float energy = 1000.0F;
    float refunded_energy = 0.0F;
    std::map<BlockId, std::uint32_t> materials {};
    std::map<BlockCoord, BlockId, CoordinateLess> world {};
    std::map<BlockCoord, bool, CoordinateLess> unloaded {};
    std::map<BlockCoord, bool, CoordinateLess> protected_cells {};
    std::map<BlockCoord, bool, CoordinateLess> dynamic_cells {};
    std::map<BlockCoord, bool, CoordinateLess> player_cells {};
    std::map<std::uint64_t, BuilderRepairTarget> repair_targets {};
    std::vector<std::pair<std::uint64_t, float>> applied_repairs {};
    std::vector<std::tuple<std::uint64_t, float, std::uint32_t>> shields {};
    std::vector<std::pair<BlockCoord, std::uint64_t>> temporary_created {};
    std::vector<std::pair<BlockCoord, std::uint64_t>> temporary_removed {};
    std::vector<BlockCoord> excavated {};
    std::vector<std::tuple<BlockCoord, BlockId, std::uint32_t>> drops {};
    float mining_experience = 0.0F;
    bool store_rewards = false;
    bool fail_commit = false;
    bool fail_reservation = false;
    bool invalid_excavation_reward = false;

    [[nodiscard]] auto block_at(const BlockCoord& coordinate) const -> BlockId {
        const auto found = world.find(coordinate);
        return found == world.end()
                   ? to_block_id(BlockType::Air)
                   : found->second;
    }

    [[nodiscard]] auto callbacks() -> BuilderAbilityRuntimeCallbacks {
        BuilderAbilityRuntimeCallbacks result {};
        result.world_edit.validate_cell =
            [this](const WorldEditCell& cell) {
                return !protected_cells[cell.coordinate];
            };
        result.world_edit.cell_contains_player_or_creature =
            [this](const BlockCoord& coordinate) {
                return player_cells[coordinate];
            };
        result.world_edit.read_current =
            [this](const BlockCoord& coordinate)
                -> std::optional<WorldEditCellState> {
                if (unloaded[coordinate]) {
                    return std::nullopt;
                }
                return WorldEditCellState {
                    coordinate,
                    block_at(coordinate),
                    0U,
                    false,
                };
            };
        result.world_edit.commit_cell =
            [this](const WorldEditCell& cell) {
                if (fail_commit) {
                    return false;
                }
                world[cell.coordinate] = cell.block_id;
                return true;
            };
        result.world_edit.rollback_cell =
            [this](const WorldEditCellState& cell) {
                world[cell.coordinate] = cell.block_id;
            };
        result.world_edit.materials_available =
            [](BlockId, std::uint32_t) {
                return true;
            };
        result.world_edit.consume_materials =
            [](BlockId, std::uint32_t) {
                return true;
            };
        result.world_edit.refund_materials =
            [](BlockId, std::uint32_t) {};

        result.is_chunk_loaded =
            [this](const BlockCoord& coordinate) {
                return !unloaded[coordinate];
            };
        result.is_protected =
            [this](const BlockCoord& coordinate) {
                return protected_cells[coordinate];
            };
        result.is_replaceable =
            [this](const BlockCoord& coordinate) {
                return block_at(coordinate) ==
                       to_block_id(BlockType::Air);
            };
        result.is_water =
            [this](const BlockCoord& coordinate) {
                return is_block_liquid(block_at(coordinate));
            };
        result.contains_dynamic_entity =
            [this](const BlockCoord& coordinate) {
                return dynamic_cells[coordinate];
            };
        result.energy_available =
            [this](float amount) {
                return energy >= amount;
            };
        result.consume_energy =
            [this](float amount) {
                if (energy < amount) {
                    return false;
                }
                energy -= amount;
                return true;
            };
        result.refund_energy =
            [this](float amount) {
                energy += amount;
                refunded_energy += amount;
            };
        result.material_count =
            [this](BlockId block) {
                return materials[block];
            };
        result.reserve_material =
            [this](BlockId block, std::uint32_t count) {
                if (fail_reservation || materials[block] < count) {
                    return false;
                }
                materials[block] -= count;
                return true;
            };
        result.refund_material =
            [this](BlockId block, std::uint32_t count) {
                materials[block] += count;
            };

        result.repair_target =
            [this](std::uint64_t id)
            -> std::optional<BuilderRepairTarget> {
                const auto found = repair_targets.find(id);
                if (found == repair_targets.end()) {
                    return std::nullopt;
                }
                return found->second;
            };
        result.nearby_repair_targets =
            [this](
                std::uint64_t primary,
                float radius,
                std::span<BuilderRepairTarget> output) {
                auto count = std::size_t {0U};
                for (const auto& [id, target] : repair_targets) {
                    if (id == primary ||
                        target.distance_from_primary > radius ||
                        count >= output.size()) {
                        continue;
                    }
                    output[count++] = target;
                }
                return count;
            };
        result.apply_repair =
            [this](std::uint64_t id, float amount) {
                applied_repairs.emplace_back(id, amount);
            };
        result.apply_shield =
            [this](std::uint64_t id, float amount, std::uint32_t ticks) {
                shields.emplace_back(id, amount, ticks);
            };
        result.create_temporary_panel =
            [this](const WorldEditCell& cell, std::uint64_t token) {
                temporary_created.emplace_back(cell.coordinate, token);
            };
        result.remove_temporary_panel =
            [this](const BlockCoord& coordinate, std::uint64_t token) {
                temporary_removed.emplace_back(coordinate, token);
            };

        result.tool_can_break =
            [](BlockId block) {
                return block != to_block_id(BlockType::Air);
            };
        result.block_is_breakable =
            [](BlockId block) {
                return block != to_block_id(BlockType::Air);
            };
        result.excavate_cell =
            [this](const BlockCoord& coordinate)
            -> std::optional<BuilderExcavationReward> {
                const auto block = block_at(coordinate);
                if (block == to_block_id(BlockType::Air)) {
                    return std::nullopt;
                }
                world[coordinate] = to_block_id(BlockType::Air);
                excavated.push_back(coordinate);
                if (invalid_excavation_reward) {
                    return BuilderExcavationReward {
                        block,
                        1U,
                        std::numeric_limits<float>::quiet_NaN(),
                    };
                }
                return BuilderExcavationReward {block, 1U, 10.0F};
            };
        result.store_excavated_item =
            [this](BlockId, std::uint32_t) {
                return store_rewards;
            };
        result.drop_excavated_item =
            [this](
                const BlockCoord& coordinate,
                BlockId block,
                std::uint32_t count) {
                drops.emplace_back(coordinate, block, count);
            };
        result.grant_mining_experience =
            [this](float amount) {
                mining_experience += amount;
            };
        return result;
    }
};

constexpr auto kPlanks = to_block_id(BlockType::Planks);
constexpr auto kStone = to_block_id(BlockType::Stone);

void fill_excavation_targets(
    BuilderRuntimeFixture& fixture,
    const ExcavationWaveRequest& request) {
    const auto cells = generate_excavation_wave(request);
    REQUIRE(cells.valid);
    for (const auto& cell : cells.cell_span()) {
        fixture.world[cell.coordinate] = kStone;
    }
}

} // namespace

TEST_CASE("La réparation express applique les valeurs exactes et sa maîtrise") {
    BuilderRuntimeFixture fixture {};
    fixture.materials[kPlanks] = 10U;
    fixture.repair_targets.emplace(
        1U,
        BuilderRepairTarget {1U, 0.0F, 20.0F, 100.0F, kPlanks, 2U});
    fixture.repair_targets.emplace(
        2U,
        BuilderRepairTarget {2U, 2.0F, 40.0F, 100.0F, kPlanks, 1U});
    fixture.repair_targets.emplace(
        3U,
        BuilderRepairTarget {3U, 5.0F, 40.0F, 100.0F, kPlanks, 1U});
    BuilderAbilityRuntime runtime {};
    const auto result = runtime.activate_express_repair_device(
        {1U, 3U, true, 18.0F},
        fixture.callbacks());

    REQUIRE(result.status == BuilderAbilityRuntimeStatus::Completed);
    REQUIRE(result.changed_cell_count == 2U);
    REQUIRE(fixture.energy == 982.0F);
    REQUIRE(fixture.materials[kPlanks] == 7U);
    REQUIRE(fixture.applied_repairs.size() == 2U);
    CHECK(fixture.applied_repairs[0U].first == 1U);
    CHECK(fixture.applied_repairs[0U].second == 50.0F);
    CHECK(fixture.applied_repairs[1U].first == 2U);
    CHECK(fixture.applied_repairs[1U].second == 25.0F);
    REQUIRE(fixture.shields.size() == 1U);
    CHECK(std::get<1>(fixture.shields[0U]) == 20.0F);
    CHECK(std::get<2>(fixture.shields[0U]) == 300U);
}

TEST_CASE("La réparation refuse une cible saine ou un coût non fini sans payer") {
    BuilderRuntimeFixture fixture {};
    fixture.materials[kPlanks] = 10U;
    fixture.repair_targets.emplace(
        1U,
        BuilderRepairTarget {1U, 0.0F, 100.0F, 100.0F, kPlanks, 2U});
    BuilderAbilityRuntime runtime {};

    const auto healthy = runtime.activate_express_repair_device(
        {1U, 1U, false, 18.0F},
        fixture.callbacks());
    CHECK(healthy.status == BuilderAbilityRuntimeStatus::NoChange);
    CHECK(fixture.energy == 1000.0F);
    CHECK(fixture.materials[kPlanks] == 10U);

    const auto invalid = runtime.activate_express_repair_device(
        {
            1U,
            1U,
            false,
            std::numeric_limits<float>::infinity(),
        },
        fixture.callbacks());
    CHECK(invalid.status == BuilderAbilityRuntimeStatus::InvalidRequest);
    CHECK(fixture.energy == 1000.0F);
}

TEST_CASE("La réparation d'un plan est atomique et rembourse un commit refusé") {
    BuilderRuntimeFixture fixture {};
    fixture.materials[kPlanks] = 4U;
    fixture.fail_commit = true;
    const std::array cells {
        WorldEditCell {{0, 0, 0}, kPlanks},
        WorldEditCell {{1, 0, 0}, kPlanks},
    };
    BuilderAbilityRuntime runtime {};
    const auto result = runtime.activate_express_repair_plan(
        {{cells.data(), cells.size()}, 1U, 18.0F},
        fixture.callbacks());

    CHECK(result.status == BuilderAbilityRuntimeStatus::CommitFailed);
    CHECK(fixture.energy == 1000.0F);
    CHECK(fixture.materials[kPlanks] == 4U);
    CHECK(fixture.block_at({0, 0, 0}) == to_block_id(BlockType::Air));
}

TEST_CASE("Le mur exige exactement soixante pour cent de cellules valides") {
    BuilderRuntimeFixture fixture {};
    fixture.materials[kPlanks] = 20U;
    fixture.world[{0, 0, 0}] = kStone;
    fixture.world[{1, 0, 0}] = kStone;
    BuilderAbilityRuntime runtime {};
    const auto accepted = runtime.activate_deployable_wall(
        {
            DeployableWallRequest {
                {0, 0, 0},
                kPlanks,
                1U,
                WorldEditAxis::X,
                false,
            },
            false,
            15.0F,
        },
        fixture.callbacks());
    CHECK(accepted.status == BuilderAbilityRuntimeStatus::Completed);
    CHECK(accepted.accepted_cell_count == 4U);

    BuilderRuntimeFixture refused_fixture {};
    refused_fixture.materials[kPlanks] = 20U;
    refused_fixture.world[{-1, 0, 0}] = kStone;
    refused_fixture.world[{0, 0, 0}] = kStone;
    refused_fixture.world[{1, 0, 0}] = kStone;
    BuilderAbilityRuntime refused_runtime {};
    const auto refused = refused_runtime.activate_deployable_wall(
        {
            DeployableWallRequest {
                {0, 0, 0},
                kPlanks,
                1U,
                WorldEditAxis::X,
                false,
            },
            false,
            15.0F,
        },
        refused_fixture.callbacks());
    CHECK(refused.status == BuilderAbilityRuntimeStatus::InvalidTarget);
    CHECK(refused_fixture.energy == 1000.0F);
}

TEST_CASE("Le rempart d'urgence crée au plus trois panneaux et les nettoie") {
    BuilderRuntimeFixture fixture {};
    fixture.materials[kPlanks] = 3U;
    BuilderAbilityRuntime runtime {};
    const auto result = runtime.activate_deployable_wall(
        {
            DeployableWallRequest {
                {0, 0, 0},
                kPlanks,
                1U,
                WorldEditAxis::X,
                false,
            },
            true,
            15.0F,
        },
        fixture.callbacks());
    REQUIRE(result.status == BuilderAbilityRuntimeStatus::Completed);
    CHECK(result.temporary_cell_count == 3U);
    CHECK(runtime.temporary_panel_count() == 3U);

    for (std::uint32_t tick = 0U;
         tick < kBuilderTemporaryPanelLifetimeTicks;
         ++tick) {
        static_cast<void>(runtime.update_fixed_tick(0U, fixture.callbacks()));
    }
    CHECK(runtime.temporary_panel_count() == 0U);
    CHECK(fixture.temporary_removed.size() == 3U);
}

TEST_CASE("Le pont est tronqué à l'obstacle et construit sous budget fixe") {
    BuilderRuntimeFixture fixture {};
    fixture.materials[kPlanks] = 20U;
    fixture.world[{3, 0, 0}] = kStone;
    BuilderAbilityRuntime runtime {};
    const auto queued = runtime.queue_modular_bridge(
        {
            ModularBridgeRequest {
                {0, 0, 0},
                kPlanks,
                1U,
                WorldEditAxis::X,
                WorldEditDirection::Positive,
                WorldEditDirection::Positive,
                0,
                false,
                false,
            },
            false,
            20.0F,
        },
        fixture.callbacks());
    REQUIRE(queued.status == BuilderAbilityRuntimeStatus::Accepted);
    CHECK(queued.accepted_cell_count == 3U);
    CHECK(fixture.materials[kPlanks] == 17U);

    const auto first = runtime.update_fixed_tick(2U, fixture.callbacks());
    CHECK(first.status == BuilderAbilityRuntimeStatus::Accepted);
    CHECK(first.changed_cell_count == 2U);
    const auto second = runtime.update_fixed_tick(2U, fixture.callbacks());
    CHECK(second.status == BuilderAbilityRuntimeStatus::Completed);
    CHECK(second.changed_cell_count == 1U);
    CHECK(runtime.queued_job_count() == 0U);
}

TEST_CASE("Le pont ne traverse ni eau ni chunk absent et rembourse l'annulation") {
    BuilderRuntimeFixture fixture {};
    fixture.materials[kPlanks] = 20U;
    fixture.world[{1, 0, 0}] = to_block_id(BlockType::Water);
    BuilderAbilityRuntime runtime {};
    const auto queued = runtime.queue_modular_bridge(
        {
            ModularBridgeRequest {{0, 0, 0}, kPlanks, 1U},
            false,
            20.0F,
        },
        fixture.callbacks());
    REQUIRE(queued.status == BuilderAbilityRuntimeStatus::Accepted);
    CHECK(queued.accepted_cell_count == 1U);
    runtime.clear(fixture.callbacks());
    CHECK(fixture.energy == 1000.0F);
    CHECK(fixture.materials[kPlanks] == 20U);

    BuilderRuntimeFixture absent_fixture {};
    absent_fixture.materials[kPlanks] = 20U;
    absent_fixture.unloaded[{0, 0, 0}] = true;
    BuilderAbilityRuntime absent_runtime {};
    const auto absent = absent_runtime.queue_modular_bridge(
        {
            ModularBridgeRequest {{0, 0, 0}, kPlanks, 1U},
            false,
            20.0F,
        },
        absent_fixture.callbacks());
    CHECK(absent.status == BuilderAbilityRuntimeStatus::InvalidTarget);
}

TEST_CASE("L'onde valide outil et protection avant de consommer l'énergie") {
    BuilderRuntimeFixture fixture {};
    const ExcavationWaveRequest geometry {
        {0, 0, 0},
        1U,
        WorldEditFace::PositiveY,
        WorldEditAxis::X,
    };
    fill_excavation_targets(fixture, geometry);
    fixture.protected_cells[{0, 0, 0}] = true;
    BuilderAbilityRuntime runtime {};
    const auto protected_result = runtime.queue_excavation_wave(
        {geometry, false, 25.0F},
        fixture.callbacks());
    CHECK(protected_result.status == BuilderAbilityRuntimeStatus::InvalidTarget);
    CHECK(fixture.energy == 1000.0F);

    fixture.protected_cells[{0, 0, 0}] = false;
    auto callbacks = fixture.callbacks();
    callbacks.tool_can_break = [](BlockId) {
        return false;
    };
    const auto wrong_tool = runtime.queue_excavation_wave(
        {geometry, false, 25.0F},
        callbacks);
    CHECK(wrong_tool.status == BuilderAbilityRuntimeStatus::InvalidTarget);
    CHECK(fixture.energy == 1000.0F);
}

TEST_CASE("L'onde donne quarante pour cent d'expérience et maîtrise les objets") {
    BuilderRuntimeFixture fixture {};
    fixture.store_rewards = true;
    const ExcavationWaveRequest geometry {
        {0, 0, 0},
        2U,
        WorldEditFace::PositiveY,
        WorldEditAxis::X,
    };
    fill_excavation_targets(fixture, geometry);
    BuilderAbilityRuntime runtime {};
    const auto queued = runtime.queue_excavation_wave(
        {geometry, true, 25.0F},
        fixture.callbacks());
    REQUIRE(queued.status == BuilderAbilityRuntimeStatus::Accepted);
    CHECK(queued.accepted_cell_count == 9U);

    const auto first = runtime.update_fixed_tick(4U, fixture.callbacks());
    CHECK(first.status == BuilderAbilityRuntimeStatus::Accepted);
    CHECK(first.mining_experience == 16.0F);
    const auto second = runtime.update_fixed_tick(64U, fixture.callbacks());
    CHECK(second.status == BuilderAbilityRuntimeStatus::Completed);
    CHECK(second.mining_experience == 20.0F);
    CHECK(fixture.mining_experience == 36.0F);
    CHECK(fixture.excavated.size() == 9U);
    CHECK(fixture.drops.empty());
}

TEST_CASE("Les files restent bornées et le nettoyage rembourse les travaux vierges") {
    BuilderRuntimeFixture fixture {};
    fixture.materials[kPlanks] = 1000U;
    BuilderAbilityRuntime runtime {};
    for (std::size_t index = 0U;
         index < kBuilderMaximumQueuedJobs;
         ++index) {
        const auto origin = BlockCoord {
            static_cast<int>(index * 16U),
            0,
            0,
        };
        const auto result = runtime.queue_modular_bridge(
            {
                ModularBridgeRequest {origin, kPlanks, 1U},
                false,
                20.0F,
            },
            fixture.callbacks());
        REQUIRE(result.status == BuilderAbilityRuntimeStatus::Accepted);
        REQUIRE(result.job_id != 0U);
    }
    const auto overflow = runtime.queue_modular_bridge(
        {
            ModularBridgeRequest {{500, 0, 0}, kPlanks, 1U},
            false,
            20.0F,
        },
        fixture.callbacks());
    CHECK(overflow.status == BuilderAbilityRuntimeStatus::QueueFull);

    runtime.clear(fixture.callbacks());
    CHECK(runtime.queued_job_count() == 0U);
    CHECK(fixture.energy == 1000.0F);
    CHECK(fixture.materials[kPlanks] == 1000U);
}

} // namespace valcraft
