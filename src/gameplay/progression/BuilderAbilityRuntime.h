#pragma once

#include "gameplay/progression/BuilderAbilityGeometry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace valcraft {

inline constexpr std::size_t kBuilderMaximumQueuedJobs = 8U;
inline constexpr std::size_t kBuilderMaximumRepairTargets = 4U;
inline constexpr std::size_t kBuilderMaximumMaterialKinds = 4U;
inline constexpr std::size_t kBuilderMaximumTemporaryPanels = 64U;
inline constexpr std::uint32_t kBuilderTemporaryPanelLifetimeTicks = 900U;

enum class BuilderAbilityRuntimeStatus : std::uint8_t {
    InvalidRequest = 0,
    InvalidTarget,
    NoChange,
    InsufficientEnergy,
    InsufficientMaterials,
    QueueFull,
    CommitFailed,
    Accepted,
    Completed,
};

enum class BuilderQueuedJobKind : std::uint8_t {
    ModularBridge = 0,
    ExcavationWave = 1,
};

struct BuilderMaterialCost {
    BlockId block_id = to_block_id(BlockType::Air);
    std::uint32_t count = 0U;
};

struct BuilderAbilityRuntimeResult {
    BuilderAbilityRuntimeStatus status =
        BuilderAbilityRuntimeStatus::InvalidRequest;
    std::uint64_t job_id = 0U;
    std::size_t requested_cell_count = 0U;
    std::size_t accepted_cell_count = 0U;
    std::size_t temporary_cell_count = 0U;
    std::size_t changed_cell_count = 0U;
    float mining_experience = 0.0F;

    [[nodiscard]] constexpr auto succeeded() const noexcept -> bool {
        return status == BuilderAbilityRuntimeStatus::Accepted ||
               status == BuilderAbilityRuntimeStatus::Completed;
    }
};

struct BuilderRepairTarget {
    std::uint64_t target_id = 0U;
    float distance_from_primary = 0.0F;
    float current_durability = 0.0F;
    float maximum_durability = 0.0F;
    BlockId repair_material = to_block_id(BlockType::Air);
    std::uint32_t repair_material_count = 0U;
};

struct BuilderExcavationReward {
    BlockId block_id = to_block_id(BlockType::Air);
    std::uint32_t count = 0U;
    float mining_experience = 0.0F;
};

struct BuilderAbilityRuntimeCallbacks {
    WorldEditTransactionCallbacks world_edit {};

    std::function<bool(const BlockCoord&)> is_chunk_loaded {};
    std::function<bool(const BlockCoord&)> is_protected {};
    std::function<bool(const BlockCoord&)> is_replaceable {};
    std::function<bool(const BlockCoord&)> is_water {};
    std::function<bool(const BlockCoord&)> contains_dynamic_entity {};

    std::function<bool(float)> energy_available {};
    std::function<bool(float)> consume_energy {};
    std::function<void(float)> refund_energy {};
    std::function<std::uint32_t(BlockId)> material_count {};
    std::function<bool(BlockId, std::uint32_t)> reserve_material {};
    std::function<void(BlockId, std::uint32_t)> refund_material {};

    std::function<std::optional<BuilderRepairTarget>(std::uint64_t)>
        repair_target {};
    std::function<std::size_t(
        std::uint64_t,
        float,
        std::span<BuilderRepairTarget>)>
        nearby_repair_targets {};
    std::function<void(std::uint64_t, float)> apply_repair {};
    std::function<void(std::uint64_t, float, std::uint32_t)> apply_shield {};

    std::function<void(const WorldEditCell&, std::uint64_t)>
        create_temporary_panel {};
    std::function<void(const BlockCoord&, std::uint64_t)>
        remove_temporary_panel {};

    std::function<bool(BlockId)> tool_can_break {};
    std::function<bool(BlockId)> block_is_breakable {};
    std::function<std::optional<BuilderExcavationReward>(const BlockCoord&)>
        excavate_cell {};
    std::function<bool(BlockId, std::uint32_t)> store_excavated_item {};
    std::function<void(const BlockCoord&, BlockId, std::uint32_t)>
        drop_excavated_item {};
    std::function<void(float)> grant_mining_experience {};
};

struct ExpressRepairDeviceRequest {
    std::uint64_t target_id = 0U;
    std::uint8_t rank = 1U;
    bool mastery_active = false;
    float energy_cost = 18.0F;
};

struct ExpressRepairPlanRequest {
    std::span<const WorldEditCell> missing_cells {};
    std::uint8_t rank = 1U;
    float energy_cost = 18.0F;
};

struct DeployableWallRuntimeRequest {
    DeployableWallRequest geometry {};
    bool mastery_active = false;
    float energy_cost = 15.0F;
};

struct ModularBridgeRuntimeRequest {
    ModularBridgeRequest geometry {};
    bool mastery_active = false;
    float energy_cost = 20.0F;
};

struct ExcavationWaveRuntimeRequest {
    ExcavationWaveRequest geometry {};
    bool mastery_active = false;
    float energy_cost = 25.0F;
};

class BuilderAbilityRuntime {
public:
    [[nodiscard]] auto activate_express_repair_device(
        const ExpressRepairDeviceRequest& request,
        const BuilderAbilityRuntimeCallbacks& callbacks)
        -> BuilderAbilityRuntimeResult;

    [[nodiscard]] auto activate_express_repair_plan(
        const ExpressRepairPlanRequest& request,
        const BuilderAbilityRuntimeCallbacks& callbacks)
        -> BuilderAbilityRuntimeResult;

    [[nodiscard]] auto activate_deployable_wall(
        const DeployableWallRuntimeRequest& request,
        const BuilderAbilityRuntimeCallbacks& callbacks)
        -> BuilderAbilityRuntimeResult;

    [[nodiscard]] auto queue_modular_bridge(
        const ModularBridgeRuntimeRequest& request,
        const BuilderAbilityRuntimeCallbacks& callbacks)
        -> BuilderAbilityRuntimeResult;

    [[nodiscard]] auto queue_excavation_wave(
        const ExcavationWaveRuntimeRequest& request,
        const BuilderAbilityRuntimeCallbacks& callbacks)
        -> BuilderAbilityRuntimeResult;

    [[nodiscard]] auto update_fixed_tick(
        std::size_t maximum_world_modifications,
        const BuilderAbilityRuntimeCallbacks& callbacks)
        -> BuilderAbilityRuntimeResult;

    void clear(const BuilderAbilityRuntimeCallbacks& callbacks) noexcept;

    [[nodiscard]] auto queued_job_count() const noexcept -> std::size_t;
    [[nodiscard]] auto temporary_panel_count() const noexcept -> std::size_t;
    [[nodiscard]] auto simulation_tick() const noexcept -> std::uint64_t;

private:
    struct QueuedJob {
        bool active = false;
        BuilderQueuedJobKind kind = BuilderQueuedJobKind::ModularBridge;
        std::uint64_t id = 0U;
        std::array<BuilderAbilityCell, kBuilderAbilityMaximumCellCount> cells {};
        std::size_t cell_count = 0U;
        std::size_t next_cell = 0U;
        float paid_energy = 0.0F;
        BuilderMaterialCost reserved_material {};
        bool excavation_mastery = false;
    };

    struct TemporaryPanel {
        bool active = false;
        BlockCoord coordinate {};
        std::uint64_t token = 0U;
        std::uint64_t expires_at_tick = 0U;
    };

    std::array<QueuedJob, kBuilderMaximumQueuedJobs> jobs_ {};
    std::array<TemporaryPanel, kBuilderMaximumTemporaryPanels>
        temporary_panels_ {};
    std::uint64_t next_job_id_ = 1U;
    std::uint64_t next_temporary_token_ = 1U;
    std::uint64_t simulation_tick_ = 0U;
};

} // namespace valcraft
