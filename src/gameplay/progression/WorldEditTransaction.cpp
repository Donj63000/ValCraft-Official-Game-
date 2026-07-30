#include "gameplay/progression/WorldEditTransaction.h"

#include <algorithm>
#include <array>
#include <limits>

namespace valcraft {
namespace {

struct WorldEditMaterialCost {
    BlockId block_id = to_block_id(BlockType::Air);
    std::uint32_t count = 0U;
};

[[nodiscard]] auto coordinate_less(
    const BlockCoord& lhs,
    const BlockCoord& rhs) noexcept -> bool {
    if (lhs.x != rhs.x) {
        return lhs.x < rhs.x;
    }
    if (lhs.y != rhs.y) {
        return lhs.y < rhs.y;
    }
    return lhs.z < rhs.z;
}

[[nodiscard]] auto cell_less(
    const WorldEditCell& lhs,
    const WorldEditCell& rhs) noexcept -> bool {
    if (coordinate_less(
            lhs.coordinate,
            rhs.coordinate)) {
        return true;
    }
    if (coordinate_less(
            rhs.coordinate,
            lhs.coordinate)) {
        return false;
    }
    return lhs.block_id < rhs.block_id;
}

[[nodiscard]] auto same_coordinate(
    const WorldEditCell& lhs,
    const WorldEditCell& rhs) noexcept -> bool {
    return lhs.coordinate == rhs.coordinate;
}

[[nodiscard]] auto callbacks_are_complete(
    const WorldEditTransactionCallbacks& callbacks) noexcept -> bool {
    return static_cast<bool>(callbacks.validate_cell) &&
           static_cast<bool>(
               callbacks.cell_contains_player_or_creature) &&
           static_cast<bool>(callbacks.read_current) &&
           static_cast<bool>(callbacks.commit_cell) &&
           static_cast<bool>(callbacks.rollback_cell) &&
           static_cast<bool>(callbacks.materials_available) &&
           static_cast<bool>(callbacks.consume_materials) &&
           static_cast<bool>(callbacks.refund_materials);
}

auto normalize_cells(
    std::span<const WorldEditCell> requested_cells,
    std::array<WorldEditCell, kWorldEditMaximumCellCount>& normalized)
    -> std::size_t {
    std::copy(
        requested_cells.begin(),
        requested_cells.end(),
        normalized.begin());
    std::sort(
        normalized.begin(),
        normalized.begin() +
            static_cast<std::ptrdiff_t>(requested_cells.size()),
        cell_less);

    // Je tranche les doublons de position par le plus petit identifiant
    // afin que le résultat ne dépende jamais de l'ordre d'entrée.
    const auto unique_end =
        std::unique(
            normalized.begin(),
            normalized.begin() +
                static_cast<std::ptrdiff_t>(requested_cells.size()),
            same_coordinate);
    return static_cast<std::size_t>(
        std::distance(
            normalized.begin(),
            unique_end));
}

auto build_material_costs(
    std::span<const WorldEditCell> changed_cells,
    std::array<WorldEditMaterialCost, kWorldEditMaximumCellCount>& costs)
    noexcept -> std::size_t {
    std::array<std::uint32_t, 256U> counts {};
    for (const auto& cell : changed_cells) {
        if (cell.block_id !=
            to_block_id(BlockType::Air)) {
            ++counts[cell.block_id];
        }
    }

    auto cost_count = std::size_t {0U};
    for (std::size_t block_index = 0U;
         block_index < counts.size();
         ++block_index) {
        if (counts[block_index] == 0U) {
            continue;
        }
        costs[cost_count] = {
            static_cast<BlockId>(block_index),
            counts[block_index],
        };
        ++cost_count;
    }
    return cost_count;
}

void refund_costs(
    std::span<const WorldEditMaterialCost> costs,
    const WorldEditTransactionCallbacks& callbacks) {
    for (auto index = costs.size();
         index > 0U;
         --index) {
        const auto& cost =
            costs[index - 1U];
        callbacks.refund_materials(
            cost.block_id,
            cost.count);
    }
}

[[nodiscard]] auto face_axis(
    WorldEditFace face) noexcept
    -> std::optional<WorldEditAxis> {
    switch (face) {
    case WorldEditFace::NegativeX:
    case WorldEditFace::PositiveX:
        return WorldEditAxis::X;
    case WorldEditFace::NegativeY:
    case WorldEditFace::PositiveY:
        return WorldEditAxis::Y;
    case WorldEditFace::NegativeZ:
    case WorldEditFace::PositiveZ:
        return WorldEditAxis::Z;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] auto secondary_axis(
    WorldEditAxis normal,
    WorldEditAxis primary) noexcept
    -> std::optional<WorldEditAxis> {
    if (normal == primary) {
        return std::nullopt;
    }
    for (const auto candidate : {
             WorldEditAxis::X,
             WorldEditAxis::Y,
             WorldEditAxis::Z,
         }) {
        if (candidate != normal &&
            candidate != primary) {
            return candidate;
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto offset_coordinate(
    const BlockCoord& origin,
    WorldEditAxis axis,
    int offset) noexcept
    -> std::optional<BlockCoord> {
    auto result = origin;
    auto* component = &result.x;
    switch (axis) {
    case WorldEditAxis::X:
        component = &result.x;
        break;
    case WorldEditAxis::Y:
        component = &result.y;
        break;
    case WorldEditAxis::Z:
        component = &result.z;
        break;
    default:
        return std::nullopt;
    }

    const auto value =
        static_cast<long long>(*component) +
        static_cast<long long>(offset);
    if (value <
            static_cast<long long>(
                std::numeric_limits<int>::min()) ||
        value >
            static_cast<long long>(
                std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    *component = static_cast<int>(value);
    return result;
}

[[nodiscard]] auto offset_coordinate(
    const BlockCoord& origin,
    WorldEditAxis first_axis,
    int first_offset,
    WorldEditAxis second_axis,
    int second_offset) noexcept
    -> std::optional<BlockCoord> {
    const auto first =
        offset_coordinate(
            origin,
            first_axis,
            first_offset);
    if (!first.has_value()) {
        return std::nullopt;
    }
    return offset_coordinate(
        *first,
        second_axis,
        second_offset);
}

[[nodiscard]] auto reflected_coordinate(
    const BlockCoord& coordinate,
    const BlockCoord& origin,
    WorldEditAxis mirror_axis) noexcept
    -> std::optional<BlockCoord> {
    int coordinate_component = 0;
    int origin_component = 0;
    switch (mirror_axis) {
    case WorldEditAxis::X:
        coordinate_component = coordinate.x;
        origin_component = origin.x;
        break;
    case WorldEditAxis::Y:
        coordinate_component = coordinate.y;
        origin_component = origin.y;
        break;
    case WorldEditAxis::Z:
        coordinate_component = coordinate.z;
        origin_component = origin.z;
        break;
    default:
        return std::nullopt;
    }

    const auto reflected =
        static_cast<long long>(origin_component) * 2LL -
        static_cast<long long>(coordinate_component);
    if (reflected <
            static_cast<long long>(
                std::numeric_limits<int>::min()) ||
        reflected >
            static_cast<long long>(
                std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    auto result = coordinate;
    switch (mirror_axis) {
    case WorldEditAxis::X:
        result.x = static_cast<int>(reflected);
        break;
    case WorldEditAxis::Y:
        result.y = static_cast<int>(reflected);
        break;
    case WorldEditAxis::Z:
        result.z = static_cast<int>(reflected);
        break;
    default:
        return std::nullopt;
    }
    return result;
}

} // namespace

auto WorldEditTransaction::execute(
    std::span<const WorldEditCell> requested_cells,
    const WorldEditTransactionCallbacks& callbacks)
    -> WorldEditTransactionResult {
    WorldEditTransactionResult result {};
    result.requested_cell_count =
        requested_cells.size();
    if (requested_cells.size() >
        kWorldEditMaximumCellCount) {
        result.status =
            WorldEditTransactionStatus::TooManyCells;
        return result;
    }
    if (requested_cells.empty() ||
        !callbacks_are_complete(callbacks)) {
        return result;
    }

    std::array<
        WorldEditCell,
        kWorldEditMaximumCellCount>
        normalized {};
    result.unique_cell_count =
        normalize_cells(
            requested_cells,
            normalized);

    std::array<
        WorldEditCell,
        kWorldEditMaximumCellCount>
        changed {};
    std::array<
        WorldEditCellState,
        kWorldEditMaximumCellCount>
        previous {};
    for (std::size_t index = 0U;
         index < result.unique_cell_count;
         ++index) {
        const auto& cell = normalized[index];
        if (!callbacks.validate_cell(cell) ||
            callbacks.cell_contains_player_or_creature(
                cell.coordinate)) {
            return result;
        }

        const auto current =
            callbacks.read_current(
                cell.coordinate);
        if (!current.has_value()) {
            return result;
        }
        if (current->block_id == cell.block_id &&
            current->water_state == 0U) {
            continue;
        }

        changed[result.changed_cell_count] = cell;
        previous[result.changed_cell_count] =
            *current;
        ++result.changed_cell_count;
    }

    std::array<
        WorldEditMaterialCost,
        kWorldEditMaximumCellCount>
        material_costs {};
    const auto material_cost_count =
        build_material_costs(
            {
                changed.data(),
                result.changed_cell_count,
            },
            material_costs);
    for (std::size_t index = 0U;
         index < material_cost_count;
         ++index) {
        const auto& cost = material_costs[index];
        if (!callbacks.materials_available(
                cost.block_id,
                cost.count)) {
            result.status =
                WorldEditTransactionStatus::
                    InsufficientMaterials;
            return result;
        }
    }

    auto consumed_cost_count =
        std::size_t {0U};
    for (; consumed_cost_count <
           material_cost_count;
         ++consumed_cost_count) {
        const auto& cost =
            material_costs[consumed_cost_count];
        // Je considère qu'un refus de consommation n'a retiré aucun
        // matériau pour l'élément refusé.
        if (!callbacks.consume_materials(
                cost.block_id,
                cost.count)) {
            refund_costs(
                {
                    material_costs.data(),
                    consumed_cost_count,
                },
                callbacks);
            result.status =
                WorldEditTransactionStatus::
                    InsufficientMaterials;
            return result;
        }
    }

    for (std::size_t index = 0U;
         index < result.changed_cell_count;
         ++index) {
        // Je considère qu'un commit refusé n'a pas modifié sa cellule.
        if (callbacks.commit_cell(changed[index])) {
            ++result.commit_count;
            continue;
        }

        for (auto rollback_index =
                 result.commit_count;
             rollback_index > 0U;
             --rollback_index) {
            callbacks.rollback_cell(
                previous[rollback_index - 1U]);
            ++result.rollback_count;
        }
        refund_costs(
            {
                material_costs.data(),
                consumed_cost_count,
            },
            callbacks);
        result.status =
            WorldEditTransactionStatus::CommitFailed;
        return result;
    }

    result.status =
        WorldEditTransactionStatus::Success;
    return result;
}

auto generate_world_edit_shape(
    const WorldEditShapeRequest& request) noexcept
    -> WorldEditShapeResult {
    WorldEditShapeResult result {};
    const auto normal_axis =
        face_axis(request.support_face);
    const auto second_axis =
        normal_axis.has_value()
            ? secondary_axis(
                  *normal_axis,
                  request.primary_axis)
            : std::nullopt;
    if (!normal_axis.has_value() ||
        !second_axis.has_value() ||
        request.rank == 0U ||
        request.rank > 3U ||
        (request.rank < 3U &&
         request.shape != WorldEditShape::Line)) {
        return result;
    }

    std::size_t base_count = 0U;
    if (request.shape == WorldEditShape::Line) {
        base_count =
            request.rank == 1U
                ? 2U
                : request.rank == 2U
                      ? 3U
                      : 5U;
    } else if (request.shape ==
                   WorldEditShape::Grid &&
               request.rank == 3U) {
        base_count = 9U;
    } else {
        return result;
    }

    std::array<WorldEditCell, 18U> candidates {};
    auto candidate_count = std::size_t {0U};
    const auto direction =
        static_cast<int>(request.direction);
    if (direction != -1 &&
        direction != 1) {
        return result;
    }

    if (request.shape == WorldEditShape::Line) {
        for (std::size_t index = 0U;
             index < base_count;
             ++index) {
            const auto coordinate =
                offset_coordinate(
                    request.origin,
                    request.primary_axis,
                    static_cast<int>(index) *
                        direction);
            if (!coordinate.has_value()) {
                return result;
            }
            candidates[candidate_count] = {
                *coordinate,
                request.block_id,
            };
            ++candidate_count;
        }
    } else {
        for (auto primary_offset = -1;
             primary_offset <= 1;
             ++primary_offset) {
            for (auto secondary_offset = -1;
                 secondary_offset <= 1;
                 ++secondary_offset) {
                const auto coordinate =
                    offset_coordinate(
                        request.origin,
                        request.primary_axis,
                        primary_offset *
                            direction,
                        *second_axis,
                        secondary_offset);
                if (!coordinate.has_value()) {
                    return result;
                }
                candidates[candidate_count] = {
                    *coordinate,
                    request.block_id,
                };
                ++candidate_count;
            }
        }
    }

    if (request.mastery_mirror) {
        const auto unmirrored_count =
            candidate_count;
        for (std::size_t index = 0U;
             index < unmirrored_count;
             ++index) {
            const auto coordinate =
                reflected_coordinate(
                    candidates[index].coordinate,
                    request.origin,
                    request.primary_axis);
            if (!coordinate.has_value()) {
                return result;
            }
            candidates[candidate_count] = {
                *coordinate,
                request.block_id,
            };
            ++candidate_count;
        }
    }

    std::sort(
        candidates.begin(),
        candidates.begin() +
            static_cast<std::ptrdiff_t>(candidate_count),
        cell_less);
    const auto unique_end =
        std::unique(
            candidates.begin(),
            candidates.begin() +
                static_cast<std::ptrdiff_t>(candidate_count),
            same_coordinate);
    const auto unique_count =
        static_cast<std::size_t>(
            std::distance(
                candidates.begin(),
                unique_end));
    result.cell_count =
        std::min(
            unique_count,
            kWorldEditShapeMaximumCellCount);
    std::copy_n(
        candidates.begin(),
        result.cell_count,
        result.cells.begin());
    result.valid = true;
    return result;
}

} // namespace valcraft
