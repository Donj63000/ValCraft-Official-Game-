#include "gameplay/progression/BuilderAbilityGeometry.h"

#include <algorithm>
#include <array>

namespace valcraft {

namespace {

[[nodiscard]] constexpr auto valid_rank(
    std::uint8_t rank) noexcept -> bool {
    return rank >= 1U && rank <= 3U;
}

[[nodiscard]] constexpr auto horizontal_axis(
    WorldEditAxis axis) noexcept -> bool {
    return axis == WorldEditAxis::X ||
           axis == WorldEditAxis::Z;
}

[[nodiscard]] constexpr auto valid_axis(
    WorldEditAxis axis) noexcept -> bool {
    return axis == WorldEditAxis::X ||
           axis == WorldEditAxis::Y ||
           axis == WorldEditAxis::Z;
}

[[nodiscard]] constexpr auto valid_face(
    WorldEditFace face) noexcept -> bool {
    return face >=
               WorldEditFace::NegativeX &&
           face <=
               WorldEditFace::PositiveZ;
}

[[nodiscard]] constexpr auto valid_direction(
    WorldEditDirection direction) noexcept -> bool {
    return direction ==
               WorldEditDirection::Negative ||
           direction ==
               WorldEditDirection::Positive;
}

[[nodiscard]] constexpr auto direction_sign(
    WorldEditDirection direction) noexcept -> int {
    return direction ==
                   WorldEditDirection::Negative
               ? -1
               : 1;
}

void offset_axis(
    BlockCoord& coordinate,
    WorldEditAxis axis,
    int amount) noexcept {
    switch (axis) {
    case WorldEditAxis::X:
        coordinate.x += amount;
        break;
    case WorldEditAxis::Y:
        coordinate.y += amount;
        break;
    case WorldEditAxis::Z:
        coordinate.z += amount;
        break;
    }
}

[[nodiscard]] constexpr auto perpendicular_axis(
    WorldEditAxis axis) noexcept -> WorldEditAxis {
    return axis == WorldEditAxis::X
               ? WorldEditAxis::Z
               : WorldEditAxis::X;
}

[[nodiscard]] constexpr auto face_normal_axis(
    WorldEditFace face) noexcept -> WorldEditAxis {
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
    }
    return WorldEditAxis::Y;
}

[[nodiscard]] constexpr auto face_normal_sign(
    WorldEditFace face) noexcept -> int {
    switch (face) {
    case WorldEditFace::NegativeX:
    case WorldEditFace::NegativeY:
    case WorldEditFace::NegativeZ:
        return -1;
    case WorldEditFace::PositiveX:
    case WorldEditFace::PositiveY:
    case WorldEditFace::PositiveZ:
        return 1;
    }
    return 1;
}

[[nodiscard]] constexpr auto plane_axes(
    WorldEditAxis normal) noexcept
    -> std::array<WorldEditAxis, 2U> {
    switch (normal) {
    case WorldEditAxis::X:
        return {
            WorldEditAxis::Y,
            WorldEditAxis::Z,
        };
    case WorldEditAxis::Y:
        return {
            WorldEditAxis::X,
            WorldEditAxis::Z,
        };
    case WorldEditAxis::Z:
        return {
            WorldEditAxis::X,
            WorldEditAxis::Y,
        };
    }
    return {
        WorldEditAxis::X,
        WorldEditAxis::Z,
    };
}

[[nodiscard]] auto append_cell(
    BuilderAbilityCellSet& result,
    const BlockCoord& coordinate,
    BlockId block_id,
    BuilderAbilityCellRole role) noexcept -> bool {
    if (result.cell_count >=
        result.cells.size()) {
        return false;
    }
    for (std::size_t index = 0U;
         index < result.cell_count;
         ++index) {
        if (result.cells[index]
                .coordinate ==
            coordinate) {
            return false;
        }
    }
    result.cells[result.cell_count++] = {
        coordinate,
        block_id,
        role,
    };
    return true;
}

} // namespace

auto generate_deployable_wall(
    const DeployableWallRequest& request) noexcept
    -> BuilderAbilityCellSet {
    BuilderAbilityCellSet result {};
    if (!valid_rank(request.rank) ||
        !horizontal_axis(
            request.width_axis) ||
        !is_known_block_id(
            request.material) ||
        !is_block_collidable(
            request.material) ||
        is_block_liquid(
            request.material)) {
        return result;
    }

    const auto width =
        request.rank >= 3U ? 5 : 3;
    const auto height =
        request.rank == 1U ? 2 : 3;
    const auto half_width =
        width / 2;
    for (auto y = 0;
         y < height;
         ++y) {
        for (auto lateral = -half_width;
             lateral <= half_width;
             ++lateral) {
            if (request.rank >= 3U &&
                request.central_opening &&
                lateral == 0 &&
                y < 2) {
                continue;
            }
            auto coordinate =
                request.bottom_center;
            coordinate.y += y;
            offset_axis(
                coordinate,
                request.width_axis,
                lateral);
            if (!append_cell(
                    result,
                    coordinate,
                    request.material,
                    BuilderAbilityCellRole::
                        Permanent)) {
                return {};
            }
        }
    }
    result.valid =
        result.cell_count != 0U;
    return result;
}

auto generate_modular_bridge(
    const ModularBridgeRequest& request) noexcept
    -> BuilderAbilityCellSet {
    BuilderAbilityCellSet result {};
    if (!valid_rank(request.rank) ||
        !horizontal_axis(
            request.forward_axis) ||
        !is_known_block_id(
            request.material) ||
        !is_block_collidable(
            request.material) ||
        is_block_liquid(
            request.material) ||
        !valid_direction(
            request.direction) ||
        !valid_direction(
            request.width_side) ||
        request.grade < -1 ||
        request.grade > 1 ||
        (request.rank == 1U &&
         request.grade != 0) ||
        (request.rank < 3U &&
         request.double_width)) {
        return result;
    }

    const auto length =
        request.rank == 1U
            ? 5
            : (request.rank == 2U
                   ? 7
                   : 9);
    const auto width =
        request.rank >= 3U &&
                request.double_width
            ? 2
            : 1;
    const auto forward_sign =
        direction_sign(
            request.direction);
    const auto side_sign =
        direction_sign(
            request.width_side);
    const auto side_axis =
        perpendicular_axis(
            request.forward_axis);

    for (auto step = 0;
         step < length;
         ++step) {
        auto center =
            request.origin;
        offset_axis(
            center,
            request.forward_axis,
            step * forward_sign);
        center.y +=
            (step / 2) *
            static_cast<int>(
                request.grade);

        for (auto side = 0;
             side < width;
             ++side) {
            auto deck = center;
            offset_axis(
                deck,
                side_axis,
                side * side_sign);
            if (!append_cell(
                    result,
                    deck,
                    request.material,
                    BuilderAbilityCellRole::
                        Permanent)) {
                return {};
            }
        }

        if (request.include_optional_guard_rails) {
            for (const auto side :
                 std::array {
                     -side_sign,
                     width * side_sign,
                 }) {
                auto rail = center;
                offset_axis(
                    rail,
                    side_axis,
                    side);
                ++rail.y;
                if (!append_cell(
                        result,
                        rail,
                        request.material,
                        BuilderAbilityCellRole::
                            OptionalGuardRail)) {
                    return {};
                }
            }
        }
    }
    result.valid =
        result.cell_count != 0U;
    return result;
}

auto generate_excavation_wave(
    const ExcavationWaveRequest& request) noexcept
    -> BuilderAbilityCellSet {
    BuilderAbilityCellSet result {};
    if (!valid_rank(request.rank) ||
        !valid_face(
            request.hit_face) ||
        !valid_axis(
            request.line_axis)) {
        return result;
    }

    const auto normal_axis =
        face_normal_axis(
            request.hit_face);
    const auto axes =
        plane_axes(
            normal_axis);
    auto line_axis =
        request.line_axis;
    if (line_axis == normal_axis) {
        line_axis = axes[0U];
    }
    const auto second_axis =
        line_axis == axes[0U]
            ? axes[1U]
            : axes[0U];
    const auto layer_count =
        request.rank >= 3U ? 2 : 1;
    const auto second_minimum =
        request.rank >= 2U ? -1 : 0;
    const auto second_maximum =
        request.rank >= 2U ? 1 : 0;

    for (auto depth = 0;
         depth < layer_count;
         ++depth) {
        for (auto second = second_minimum;
             second <= second_maximum;
             ++second) {
            for (auto line = -1;
                 line <= 1;
                 ++line) {
                auto coordinate =
                    request.target;
                offset_axis(
                    coordinate,
                    line_axis,
                    line);
                offset_axis(
                    coordinate,
                    second_axis,
                    second);
                // Je creuse la seconde couche derrière la face touchée, jamais
                // vers le joueur qui a lancé le rayon.
                offset_axis(
                    coordinate,
                    normal_axis,
                    -depth *
                        face_normal_sign(
                            request.hit_face));
                if (!append_cell(
                        result,
                        coordinate,
                        to_block_id(
                            BlockType::Air),
                        BuilderAbilityCellRole::
                            Excavation)) {
                    return {};
                }
            }
        }
    }
    result.valid =
        result.cell_count ==
        (request.rank == 1U
             ? 3U
             : (request.rank == 2U
                    ? 9U
                    : 18U));
    return result;
}

auto express_repair_parameters(
    std::uint8_t rank,
    bool mastery_active) noexcept
    -> ExpressRepairParameters {
    if (!valid_rank(rank)) {
        return {};
    }
    const auto index =
        static_cast<std::size_t>(
            rank - 1U);
    constexpr std::array ratios {
        0.25F,
        0.35F,
        0.50F,
    };
    constexpr std::array maximum_cells {
        std::uint8_t {4U},
        std::uint8_t {6U},
        std::uint8_t {8U},
    };
    return {
        ratios[index],
        maximum_cells[index],
        rank >= 3U ? 0.20F : 0.0F,
        rank >= 3U ? 5.0F : 0.0F,
        mastery_active
            ? std::uint8_t {3U}
            : std::uint8_t {0U},
        mastery_active ? 4.0F : 0.0F,
        mastery_active ? 0.50F : 0.0F,
    };
}

} // namespace valcraft
