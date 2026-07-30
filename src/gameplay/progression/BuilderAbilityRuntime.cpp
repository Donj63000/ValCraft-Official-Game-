#include "gameplay/progression/BuilderAbilityRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {
namespace {

[[nodiscard]] constexpr auto valid_rank(std::uint8_t rank) noexcept -> bool {
    return rank >= 1U && rank <= 3U;
}

[[nodiscard]] auto valid_energy(float energy) noexcept -> bool {
    return std::isfinite(energy) && energy >= 0.0F;
}

[[nodiscard]] constexpr auto safe_coordinate(
    const BlockCoord& coordinate) noexcept -> bool {
    constexpr auto limit = 1'000'000;
    return coordinate.x >= -limit && coordinate.x <= limit &&
           coordinate.y >= -limit && coordinate.y <= limit &&
           coordinate.z >= -limit && coordinate.z <= limit;
}

[[nodiscard]] auto common_callbacks_complete(
    const BuilderAbilityRuntimeCallbacks& callbacks) noexcept -> bool {
    return callbacks.is_chunk_loaded &&
           callbacks.is_protected &&
           callbacks.contains_dynamic_entity &&
           callbacks.energy_available &&
           callbacks.consume_energy &&
           callbacks.refund_energy &&
           callbacks.material_count &&
           callbacks.reserve_material &&
           callbacks.refund_material;
}

[[nodiscard]] auto placement_callbacks_complete(
    const BuilderAbilityRuntimeCallbacks& callbacks) noexcept -> bool {
    return common_callbacks_complete(callbacks) &&
           callbacks.is_replaceable &&
           callbacks.is_water &&
           callbacks.world_edit.validate_cell &&
           callbacks.world_edit.cell_contains_player_or_creature &&
           callbacks.world_edit.read_current &&
           callbacks.world_edit.commit_cell &&
           callbacks.world_edit.rollback_cell;
}

[[nodiscard]] auto cell_is_free_for_placement(
    const BuilderAbilityCell& cell,
    const BuilderAbilityRuntimeCallbacks& callbacks,
    bool reject_water) -> bool {
    if (!safe_coordinate(cell.coordinate) ||
        !callbacks.is_chunk_loaded(cell.coordinate) ||
        callbacks.is_protected(cell.coordinate) ||
        callbacks.contains_dynamic_entity(cell.coordinate) ||
        callbacks.world_edit.cell_contains_player_or_creature(
            cell.coordinate) ||
        !callbacks.is_replaceable(cell.coordinate) ||
        (reject_water && callbacks.is_water(cell.coordinate))) {
        return false;
    }
    return callbacks.world_edit.validate_cell({
        cell.coordinate,
        cell.block_id,
    });
}

[[nodiscard]] auto make_reserved_world_callbacks(
    const BuilderAbilityRuntimeCallbacks& callbacks)
    -> WorldEditTransactionCallbacks {
    auto result = callbacks.world_edit;
    result.materials_available =
        [](BlockId, std::uint32_t) {
            return true;
        };
    result.consume_materials =
        [](BlockId, std::uint32_t) {
            return true;
        };
    result.refund_materials =
        [](BlockId, std::uint32_t) {};
    return result;
}

[[nodiscard]] auto reserve_costs(
    std::span<const BuilderMaterialCost> costs,
    const BuilderAbilityRuntimeCallbacks& callbacks,
    std::size_t& reserved_count) -> bool {
    reserved_count = 0U;
    for (const auto& cost : costs) {
        if (cost.count == 0U) {
            continue;
        }
        if (!is_known_block_id(cost.block_id) ||
            callbacks.material_count(cost.block_id) < cost.count) {
            return false;
        }
    }
    for (const auto& cost : costs) {
        if (cost.count == 0U) {
            continue;
        }
        if (!callbacks.reserve_material(cost.block_id, cost.count)) {
            for (auto index = reserved_count; index > 0U; --index) {
                const auto& previous = costs[index - 1U];
                callbacks.refund_material(
                    previous.block_id,
                    previous.count);
            }
            reserved_count = 0U;
            return false;
        }
        ++reserved_count;
    }
    return true;
}

void refund_costs(
    std::span<const BuilderMaterialCost> costs,
    const BuilderAbilityRuntimeCallbacks& callbacks) {
    for (const auto& cost : costs) {
        if (cost.count != 0U) {
            callbacks.refund_material(cost.block_id, cost.count);
        }
    }
}

[[nodiscard]] auto pay_energy(
    float energy,
    const BuilderAbilityRuntimeCallbacks& callbacks) -> bool {
    return callbacks.energy_available(energy) &&
           callbacks.consume_energy(energy);
}

[[nodiscard]] auto append_material_cost(
    std::array<BuilderMaterialCost, kBuilderMaximumMaterialKinds>& costs,
    std::size_t& count,
    BlockId block_id,
    std::uint32_t amount) noexcept -> bool {
    if (amount == 0U) {
        return true;
    }
    for (std::size_t index = 0U; index < count; ++index) {
        if (costs[index].block_id == block_id) {
            if (amount >
                std::numeric_limits<std::uint32_t>::max() -
                    costs[index].count) {
                return false;
            }
            costs[index].count += amount;
            return true;
        }
    }
    if (count >= costs.size()) {
        return false;
    }
    costs[count++] = {block_id, amount};
    return true;
}

[[nodiscard]] auto next_nonzero_id(std::uint64_t& value) noexcept
    -> std::uint64_t {
    const auto result = value++;
    if (value == 0U) {
        value = 1U;
    }
    return result == 0U ? value++ : result;
}

} // namespace

auto BuilderAbilityRuntime::activate_express_repair_device(
    const ExpressRepairDeviceRequest& request,
    const BuilderAbilityRuntimeCallbacks& callbacks)
    -> BuilderAbilityRuntimeResult {
    BuilderAbilityRuntimeResult result {};
    if (!valid_rank(request.rank) ||
        request.target_id == 0U ||
        !valid_energy(request.energy_cost) ||
        !common_callbacks_complete(callbacks) ||
        !callbacks.repair_target ||
        !callbacks.nearby_repair_targets ||
        !callbacks.apply_repair ||
        !callbacks.apply_shield) {
        return result;
    }

    const auto parameters =
        express_repair_parameters(request.rank, request.mastery_active);
    const auto primary = callbacks.repair_target(request.target_id);
    if (!primary.has_value() ||
        primary->target_id == 0U ||
        !std::isfinite(primary->current_durability) ||
        !std::isfinite(primary->maximum_durability) ||
        primary->maximum_durability <= 0.0F ||
        primary->current_durability < 0.0F ||
        primary->current_durability >= primary->maximum_durability) {
        result.status = primary.has_value()
                            ? BuilderAbilityRuntimeStatus::NoChange
                            : BuilderAbilityRuntimeStatus::InvalidTarget;
        return result;
    }

    std::array<BuilderRepairTarget, kBuilderMaximumRepairTargets> targets {};
    targets[0U] = *primary;
    auto target_count = std::size_t {1U};
    if (request.mastery_active) {
        std::array<BuilderRepairTarget, kBuilderMaximumRepairTargets - 1U>
            nearby {};
        const auto reported = callbacks.nearby_repair_targets(
            request.target_id,
            parameters.mastery_chain_radius,
            nearby);
        const auto bounded =
            std::min(reported, nearby.size());
        for (std::size_t index = 0U;
             index < bounded &&
             target_count < targets.size();
             ++index) {
            const auto& candidate = nearby[index];
            if (candidate.target_id == 0U ||
                candidate.target_id == request.target_id ||
                !std::isfinite(candidate.distance_from_primary) ||
                candidate.distance_from_primary < 0.0F ||
                candidate.distance_from_primary >
                    parameters.mastery_chain_radius ||
                !std::isfinite(candidate.current_durability) ||
                !std::isfinite(candidate.maximum_durability) ||
                candidate.maximum_durability <= 0.0F ||
                candidate.current_durability < 0.0F ||
                candidate.current_durability >= candidate.maximum_durability) {
                continue;
            }
            bool duplicate = false;
            for (std::size_t existing = 0U;
                 existing < target_count;
                 ++existing) {
                duplicate =
                    duplicate ||
                    targets[existing].target_id == candidate.target_id;
            }
            if (!duplicate) {
                targets[target_count++] = candidate;
            }
        }
    }

    std::array<BuilderMaterialCost, kBuilderMaximumMaterialKinds> costs {};
    auto cost_count = std::size_t {0U};
    for (std::size_t index = 0U; index < target_count; ++index) {
        const auto& target = targets[index];
        if (target.repair_material_count != 0U &&
            (!is_known_block_id(target.repair_material) ||
             !append_material_cost(
                 costs,
                 cost_count,
                 target.repair_material,
                 target.repair_material_count))) {
            result.status = BuilderAbilityRuntimeStatus::InvalidTarget;
            return result;
        }
    }

    std::size_t reserved_count = 0U;
    if (!reserve_costs(
            {costs.data(), cost_count},
            callbacks,
            reserved_count)) {
        result.status = BuilderAbilityRuntimeStatus::InsufficientMaterials;
        return result;
    }
    if (!pay_energy(request.energy_cost, callbacks)) {
        refund_costs({costs.data(), cost_count}, callbacks);
        result.status = BuilderAbilityRuntimeStatus::InsufficientEnergy;
        return result;
    }

    for (std::size_t index = 0U; index < target_count; ++index) {
        const auto& target = targets[index];
        const auto ratio =
            index == 0U
                ? parameters.durability_ratio
                : parameters.durability_ratio *
                      parameters.mastery_chain_ratio;
        callbacks.apply_repair(
            target.target_id,
            target.maximum_durability * ratio);
    }
    if (parameters.shield_ratio > 0.0F) {
        callbacks.apply_shield(
            primary->target_id,
            primary->maximum_durability * parameters.shield_ratio,
            static_cast<std::uint32_t>(
                parameters.shield_duration_seconds * 60.0F));
    }

    result.status = BuilderAbilityRuntimeStatus::Completed;
    result.accepted_cell_count = target_count;
    result.changed_cell_count = target_count;
    return result;
}

auto BuilderAbilityRuntime::activate_express_repair_plan(
    const ExpressRepairPlanRequest& request,
    const BuilderAbilityRuntimeCallbacks& callbacks)
    -> BuilderAbilityRuntimeResult {
    BuilderAbilityRuntimeResult result {};
    const auto parameters =
        express_repair_parameters(request.rank, false);
    result.requested_cell_count = request.missing_cells.size();
    if (!valid_rank(request.rank) ||
        !valid_energy(request.energy_cost) ||
        request.missing_cells.empty() ||
        request.missing_cells.size() > parameters.maximum_plan_cells ||
        !placement_callbacks_complete(callbacks)) {
        return result;
    }

    std::array<BuilderMaterialCost, kBuilderMaximumMaterialKinds> costs {};
    auto cost_count = std::size_t {0U};
    for (const auto& cell : request.missing_cells) {
        const BuilderAbilityCell candidate {
            cell.coordinate,
            cell.block_id,
            BuilderAbilityCellRole::Permanent,
        };
        if (!cell_is_free_for_placement(candidate, callbacks, false) ||
            !append_material_cost(costs, cost_count, cell.block_id, 1U)) {
            result.status = BuilderAbilityRuntimeStatus::InvalidTarget;
            return result;
        }
    }

    std::size_t reserved_count = 0U;
    if (!reserve_costs(
            {costs.data(), cost_count},
            callbacks,
            reserved_count)) {
        result.status = BuilderAbilityRuntimeStatus::InsufficientMaterials;
        return result;
    }
    if (!pay_energy(request.energy_cost, callbacks)) {
        refund_costs({costs.data(), cost_count}, callbacks);
        result.status = BuilderAbilityRuntimeStatus::InsufficientEnergy;
        return result;
    }

    const auto transaction = WorldEditTransaction::execute(
        request.missing_cells,
        make_reserved_world_callbacks(callbacks));
    if (!transaction.succeeded()) {
        callbacks.refund_energy(request.energy_cost);
        refund_costs({costs.data(), cost_count}, callbacks);
        result.status = BuilderAbilityRuntimeStatus::CommitFailed;
        return result;
    }
    result.status = BuilderAbilityRuntimeStatus::Completed;
    result.accepted_cell_count = transaction.unique_cell_count;
    result.changed_cell_count = transaction.changed_cell_count;
    return result;
}

auto BuilderAbilityRuntime::activate_deployable_wall(
    const DeployableWallRuntimeRequest& request,
    const BuilderAbilityRuntimeCallbacks& callbacks)
    -> BuilderAbilityRuntimeResult {
    BuilderAbilityRuntimeResult result {};
    if (!valid_energy(request.energy_cost) ||
        !placement_callbacks_complete(callbacks)) {
        return result;
    }
    const auto generated = generate_deployable_wall(request.geometry);
    result.requested_cell_count = generated.cell_count;
    if (!generated.valid) {
        return result;
    }

    std::array<WorldEditCell, kBuilderAbilityMaximumCellCount> placeable {};
    auto placeable_count = std::size_t {0U};
    for (const auto& cell : generated.cell_span()) {
        if (cell_is_free_for_placement(cell, callbacks, false)) {
            placeable[placeable_count++] = {
                cell.coordinate,
                cell.block_id,
            };
        }
    }
    if (placeable_count <
        deployable_wall_minimum_placement_count(generated.cell_count)) {
        result.status = BuilderAbilityRuntimeStatus::InvalidTarget;
        return result;
    }

    const auto available = static_cast<std::size_t>(
        callbacks.material_count(request.geometry.material));
    auto permanent_count = std::min(placeable_count, available);
    const auto missing_materials = placeable_count - permanent_count;
    if (missing_materials != 0U &&
        (!request.mastery_active || missing_materials > 3U ||
         !callbacks.create_temporary_panel)) {
        result.status = BuilderAbilityRuntimeStatus::InsufficientMaterials;
        return result;
    }
    if (permanent_count == 0U && missing_materials == 0U) {
        result.status = BuilderAbilityRuntimeStatus::NoChange;
        return result;
    }
    if (missing_materials >
        temporary_panels_.size() - temporary_panel_count()) {
        result.status = BuilderAbilityRuntimeStatus::QueueFull;
        return result;
    }

    if (permanent_count != 0U &&
        !callbacks.reserve_material(
            request.geometry.material,
            static_cast<std::uint32_t>(permanent_count))) {
        result.status = BuilderAbilityRuntimeStatus::InsufficientMaterials;
        return result;
    }
    if (!pay_energy(request.energy_cost, callbacks)) {
        if (permanent_count != 0U) {
            callbacks.refund_material(
                request.geometry.material,
                static_cast<std::uint32_t>(permanent_count));
        }
        result.status = BuilderAbilityRuntimeStatus::InsufficientEnergy;
        return result;
    }

    WorldEditTransactionResult transaction {};
    transaction.status = WorldEditTransactionStatus::Success;
    if (permanent_count != 0U) {
        transaction = WorldEditTransaction::execute(
            {placeable.data(), permanent_count},
            make_reserved_world_callbacks(callbacks));
    }
    if (!transaction.succeeded()) {
        callbacks.refund_energy(request.energy_cost);
        if (permanent_count != 0U) {
            callbacks.refund_material(
                request.geometry.material,
                static_cast<std::uint32_t>(permanent_count));
        }
        result.status = BuilderAbilityRuntimeStatus::CommitFailed;
        return result;
    }

    for (std::size_t index = permanent_count;
         index < placeable_count;
         ++index) {
        const auto token = next_nonzero_id(next_temporary_token_);
        callbacks.create_temporary_panel(placeable[index], token);
        for (auto& panel : temporary_panels_) {
            if (!panel.active) {
                panel = {
                    true,
                    placeable[index].coordinate,
                    token,
                    simulation_tick_ +
                        kBuilderTemporaryPanelLifetimeTicks,
                };
                break;
            }
        }
    }

    result.status = BuilderAbilityRuntimeStatus::Completed;
    result.accepted_cell_count = placeable_count;
    result.temporary_cell_count = missing_materials;
    result.changed_cell_count =
        transaction.changed_cell_count + missing_materials;
    return result;
}

auto BuilderAbilityRuntime::queue_modular_bridge(
    const ModularBridgeRuntimeRequest& request,
    const BuilderAbilityRuntimeCallbacks& callbacks)
    -> BuilderAbilityRuntimeResult {
    BuilderAbilityRuntimeResult result {};
    if (!valid_energy(request.energy_cost) ||
        !placement_callbacks_complete(callbacks)) {
        return result;
    }
    const auto generated = generate_modular_bridge(request.geometry);
    result.requested_cell_count = generated.cell_count;
    if (!generated.valid) {
        return result;
    }
    auto* free_job = static_cast<QueuedJob*>(nullptr);
    for (auto& job : jobs_) {
        if (!job.active) {
            free_job = &job;
            break;
        }
    }
    if (free_job == nullptr) {
        result.status = BuilderAbilityRuntimeStatus::QueueFull;
        return result;
    }

    std::array<BuilderAbilityCell, kBuilderAbilityMaximumCellCount> accepted {};
    auto deck_count = std::size_t {0U};
    auto rail_count = std::size_t {0U};
    for (const auto& cell : generated.cell_span()) {
        if (cell.role == BuilderAbilityCellRole::OptionalGuardRail) {
            continue;
        }
        if (!cell_is_free_for_placement(cell, callbacks, true)) {
            break;
        }
        accepted[deck_count++] = cell;
    }
    if (deck_count == 0U) {
        result.status = BuilderAbilityRuntimeStatus::InvalidTarget;
        return result;
    }
    if (request.mastery_active &&
        request.geometry.include_optional_guard_rails) {
        for (const auto& cell : generated.cell_span()) {
            if (cell.role != BuilderAbilityCellRole::OptionalGuardRail ||
                !cell_is_free_for_placement(cell, callbacks, true)) {
                continue;
            }
            accepted[deck_count + rail_count++] = cell;
        }
    }

    const auto available = static_cast<std::size_t>(
        callbacks.material_count(request.geometry.material));
    if (available < deck_count) {
        result.status = BuilderAbilityRuntimeStatus::InsufficientMaterials;
        return result;
    }
    rail_count = std::min(rail_count, available - deck_count);
    const auto final_count = deck_count + rail_count;
    if (!callbacks.reserve_material(
            request.geometry.material,
            static_cast<std::uint32_t>(final_count))) {
        result.status = BuilderAbilityRuntimeStatus::InsufficientMaterials;
        return result;
    }
    if (!pay_energy(request.energy_cost, callbacks)) {
        callbacks.refund_material(
            request.geometry.material,
            static_cast<std::uint32_t>(final_count));
        result.status = BuilderAbilityRuntimeStatus::InsufficientEnergy;
        return result;
    }

    *free_job = {};
    free_job->active = true;
    free_job->kind = BuilderQueuedJobKind::ModularBridge;
    free_job->id = next_nonzero_id(next_job_id_);
    free_job->cell_count = final_count;
    free_job->paid_energy = request.energy_cost;
    free_job->reserved_material = {
        request.geometry.material,
        static_cast<std::uint32_t>(final_count),
    };
    std::copy_n(accepted.begin(), final_count, free_job->cells.begin());

    result.status = BuilderAbilityRuntimeStatus::Accepted;
    result.job_id = free_job->id;
    result.accepted_cell_count = final_count;
    return result;
}

auto BuilderAbilityRuntime::queue_excavation_wave(
    const ExcavationWaveRuntimeRequest& request,
    const BuilderAbilityRuntimeCallbacks& callbacks)
    -> BuilderAbilityRuntimeResult {
    BuilderAbilityRuntimeResult result {};
    if (!valid_energy(request.energy_cost) ||
        !common_callbacks_complete(callbacks) ||
        !callbacks.world_edit.read_current ||
        !callbacks.tool_can_break ||
        !callbacks.block_is_breakable ||
        !callbacks.excavate_cell ||
        !callbacks.drop_excavated_item ||
        !callbacks.grant_mining_experience) {
        return result;
    }
    const auto generated = generate_excavation_wave(request.geometry);
    result.requested_cell_count = generated.cell_count;
    if (!generated.valid) {
        return result;
    }
    auto* free_job = static_cast<QueuedJob*>(nullptr);
    for (auto& job : jobs_) {
        if (!job.active) {
            free_job = &job;
            break;
        }
    }
    if (free_job == nullptr) {
        result.status = BuilderAbilityRuntimeStatus::QueueFull;
        return result;
    }

    for (const auto& cell : generated.cell_span()) {
        if (!safe_coordinate(cell.coordinate) ||
            !callbacks.is_chunk_loaded(cell.coordinate) ||
            callbacks.is_protected(cell.coordinate) ||
            callbacks.contains_dynamic_entity(cell.coordinate)) {
            result.status = BuilderAbilityRuntimeStatus::InvalidTarget;
            return result;
        }
        const auto current =
            callbacks.world_edit.read_current(
                cell.coordinate);
        if (!current.has_value() ||
            !is_known_block_id(
                current->block_id) ||
            current->block_id ==
                to_block_id(BlockType::Air) ||
            !callbacks.block_is_breakable(
                current->block_id) ||
            !callbacks.tool_can_break(
                current->block_id)) {
            result.status = BuilderAbilityRuntimeStatus::InvalidTarget;
            return result;
        }
    }
    if (!pay_energy(request.energy_cost, callbacks)) {
        result.status = BuilderAbilityRuntimeStatus::InsufficientEnergy;
        return result;
    }

    *free_job = {};
    free_job->active = true;
    free_job->kind = BuilderQueuedJobKind::ExcavationWave;
    free_job->id = next_nonzero_id(next_job_id_);
    free_job->cell_count = generated.cell_count;
    free_job->paid_energy = request.energy_cost;
    free_job->excavation_mastery = request.mastery_active;
    std::copy_n(
        generated.cells.begin(),
        generated.cell_count,
        free_job->cells.begin());

    result.status = BuilderAbilityRuntimeStatus::Accepted;
    result.job_id = free_job->id;
    result.accepted_cell_count = generated.cell_count;
    return result;
}

auto BuilderAbilityRuntime::update_fixed_tick(
    std::size_t maximum_world_modifications,
    const BuilderAbilityRuntimeCallbacks& callbacks)
    -> BuilderAbilityRuntimeResult {
    BuilderAbilityRuntimeResult result {};
    ++simulation_tick_;

    for (auto& panel : temporary_panels_) {
        if (!panel.active || panel.expires_at_tick > simulation_tick_) {
            continue;
        }
        if (callbacks.remove_temporary_panel) {
            callbacks.remove_temporary_panel(panel.coordinate, panel.token);
        }
        panel = {};
    }

    if (maximum_world_modifications == 0U) {
        result.status = BuilderAbilityRuntimeStatus::NoChange;
        return result;
    }
    const auto budget = std::min(
        maximum_world_modifications,
        kBuilderAbilityMaximumCellCount);

    auto* job = static_cast<QueuedJob*>(nullptr);
    for (auto& candidate : jobs_) {
        if (candidate.active) {
            job = &candidate;
            break;
        }
    }
    if (job == nullptr) {
        result.status = BuilderAbilityRuntimeStatus::NoChange;
        return result;
    }

    result.job_id = job->id;
    result.requested_cell_count = job->cell_count;
    auto processed = std::size_t {0U};
    while (processed < budget && job->next_cell < job->cell_count) {
        const auto& cell = job->cells[job->next_cell];
        if (job->kind == BuilderQueuedJobKind::ModularBridge) {
            if (!placement_callbacks_complete(callbacks) ||
                !cell_is_free_for_placement(cell, callbacks, true)) {
                const auto remaining =
                    job->cell_count - job->next_cell;
                callbacks.refund_material(
                    job->reserved_material.block_id,
                    static_cast<std::uint32_t>(remaining));
                if (job->next_cell == 0U) {
                    callbacks.refund_energy(job->paid_energy);
                }
                job->active = false;
                result.status = BuilderAbilityRuntimeStatus::CommitFailed;
                return result;
            }
            const WorldEditCell edit {
                cell.coordinate,
                cell.block_id,
            };
            const auto transaction = WorldEditTransaction::execute(
                {&edit, 1U},
                make_reserved_world_callbacks(callbacks));
            if (!transaction.succeeded()) {
                const auto remaining =
                    job->cell_count - job->next_cell;
                callbacks.refund_material(
                    job->reserved_material.block_id,
                    static_cast<std::uint32_t>(remaining));
                if (job->next_cell == 0U) {
                    callbacks.refund_energy(job->paid_energy);
                }
                job->active = false;
                result.status = BuilderAbilityRuntimeStatus::CommitFailed;
                return result;
            }
        } else {
            if (!callbacks.excavate_cell ||
                !callbacks.drop_excavated_item ||
                !callbacks.grant_mining_experience) {
                if (job->next_cell == 0U) {
                    callbacks.refund_energy(job->paid_energy);
                }
                job->active = false;
                result.status = BuilderAbilityRuntimeStatus::CommitFailed;
                return result;
            }
            const auto reward = callbacks.excavate_cell(cell.coordinate);
            if (!reward.has_value() ||
                !is_known_block_id(reward->block_id) ||
                reward->count == 0U ||
                !std::isfinite(reward->mining_experience) ||
                reward->mining_experience < 0.0F) {
                if (job->next_cell == 0U) {
                    callbacks.refund_energy(job->paid_energy);
                }
                job->active = false;
                result.status = BuilderAbilityRuntimeStatus::CommitFailed;
                return result;
            }
            const auto stored =
                job->excavation_mastery &&
                callbacks.store_excavated_item &&
                callbacks.store_excavated_item(
                    reward->block_id,
                    reward->count);
            if (!stored) {
                callbacks.drop_excavated_item(
                    cell.coordinate,
                    reward->block_id,
                    reward->count);
            }
            const auto experience = reward->mining_experience * 0.40F;
            callbacks.grant_mining_experience(experience);
            result.mining_experience += experience;
        }
        ++job->next_cell;
        ++processed;
        ++result.changed_cell_count;
    }

    result.accepted_cell_count = job->next_cell;
    if (job->next_cell == job->cell_count) {
        job->active = false;
        result.status = BuilderAbilityRuntimeStatus::Completed;
    } else {
        result.status = BuilderAbilityRuntimeStatus::Accepted;
    }
    return result;
}

void BuilderAbilityRuntime::clear(
    const BuilderAbilityRuntimeCallbacks& callbacks) noexcept {
    for (auto& job : jobs_) {
        if (!job.active) {
            continue;
        }
        if (job.kind == BuilderQueuedJobKind::ModularBridge &&
            callbacks.refund_material) {
            const auto remaining = job.cell_count - job.next_cell;
            callbacks.refund_material(
                job.reserved_material.block_id,
                static_cast<std::uint32_t>(remaining));
        }
        if (job.next_cell == 0U && callbacks.refund_energy) {
            callbacks.refund_energy(job.paid_energy);
        }
        job = {};
    }
    for (auto& panel : temporary_panels_) {
        if (panel.active && callbacks.remove_temporary_panel) {
            callbacks.remove_temporary_panel(panel.coordinate, panel.token);
        }
        panel = {};
    }
}

auto BuilderAbilityRuntime::queued_job_count() const noexcept -> std::size_t {
    return static_cast<std::size_t>(std::count_if(
        jobs_.begin(),
        jobs_.end(),
        [](const QueuedJob& job) {
            return job.active;
        }));
}

auto BuilderAbilityRuntime::temporary_panel_count() const noexcept
    -> std::size_t {
    return static_cast<std::size_t>(std::count_if(
        temporary_panels_.begin(),
        temporary_panels_.end(),
        [](const TemporaryPanel& panel) {
            return panel.active;
        }));
}

auto BuilderAbilityRuntime::simulation_tick() const noexcept -> std::uint64_t {
    return simulation_tick_;
}

} // namespace valcraft
