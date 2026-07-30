#pragma once

#include "gameplay/progression/PlayerBuildState.h"
#include "world/Block.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace valcraft {

struct ConstructionPlanEditorCursor {
    std::int8_t x = 0;
    std::int8_t y = 0;
    std::int8_t z = 0;

    auto operator==(const ConstructionPlanEditorCursor&) const
        -> bool = default;
};

enum class ConstructionPlanEditorFailure : std::uint8_t {
    None = 0,
    Inactive,
    InvalidPlan,
    InvalidShape,
    InvalidMaterial,
    PlanFull,
    CellMissing,
    MirrorLocked,
    ConcurrentBuildMutation,
};

struct ConstructionPlanEditorResult {
    ConstructionPlanEditorFailure failure =
        ConstructionPlanEditorFailure::None;
    bool ui_changed = false;
    bool build_changed = false;

    [[nodiscard]] constexpr auto succeeded() const noexcept -> bool {
        return failure == ConstructionPlanEditorFailure::None;
    }

    [[nodiscard]] constexpr auto any_changed() const noexcept -> bool {
        return ui_changed || build_changed;
    }
};

struct ConstructionPlanEditorCellView {
    ConstructionPlanEditorCursor position {};
    std::uint16_t material_id = 0U;
    bool selected = false;
    bool on_selected_layer = false;
};

struct ConstructionPlanEditorViewModel {
    bool active = false;
    std::size_t selected_plan = 0U;
    std::size_t plan_count = kConstructionPlanCount;
    ConstructionPlanEditorCursor cursor {};
    std::int8_t selected_layer = 0;
    std::int8_t minimum_layer =
        static_cast<std::int8_t>(
            -kConstructionPlanCoordinateLimit);
    std::int8_t maximum_layer =
        kConstructionPlanCoordinateLimit;
    std::uint16_t selected_material_id =
        to_block_id(BlockType::Planks);
    ConstructionPlanShape shape =
        ConstructionPlanShape::Line;
    bool mirrored = false;
    bool mirror_unlocked = false;
    bool dirty = false;
    bool can_place = false;
    bool can_remove = false;
    bool can_commit = false;
    std::uint8_t cell_count = 0U;
    std::uint8_t cells_on_selected_layer = 0U;
    std::uint8_t maximum_cell_count =
        static_cast<std::uint8_t>(
            kConstructionPlanMaximumCellCount);
    std::array<
        ConstructionPlanEditorCellView,
        kConstructionPlanMaximumCellCount>
        cells {};
};

[[nodiscard]] inline auto construction_plan_editor_material_is_valid(
    std::uint16_t material_id) noexcept -> bool {
    if (material_id >
        std::numeric_limits<BlockId>::max()) {
        return false;
    }

    const auto block_id =
        block_item_id(
            static_cast<BlockId>(
                material_id));
    return is_placeable_item(block_id) &&
           is_block_solid(block_id);
}

class ConstructionPlanEditor {
public:
    [[nodiscard]] auto begin_editing(
        const PlayerBuildState& state) noexcept
        -> ConstructionPlanEditorResult {
        original_plans_ =
            state.construction_plans;
        draft_plans_ =
            original_plans_;
        original_selected_plan_ =
            bounded_plan_index(
                state.selected_construction_plan);
        selected_plan_ =
            original_selected_plan_;
        base_revision_ =
            state.revision;
        mirror_unlocked_ =
            player_ability_has_mastery(
                state,
                AbilityId::BuilderConstructionPlan);
        const auto rank =
            player_ability_rank(
                state,
                AbilityId::BuilderConstructionPlan);
        maximum_cell_count_ =
            rank == 1U
                ? 2U
                : rank == 2U
                      ? 3U
                      : rank >= 3U &&
                                !mirror_unlocked_
                            ? 5U
                            : static_cast<std::uint8_t>(
                                  kConstructionPlanMaximumCellCount);

        // Je nettoie uniquement la copie de travail : le build reste intact
        // jusqu'au commit, meme si une ancienne sauvegarde contient un plan
        // incomplet ou un materiau devenu invalide.
        for (auto& plan : draft_plans_) {
            normalize_plan(
                plan,
                mirror_unlocked_);
        }

        cursor_ = {};
        selected_material_id_ =
            to_block_id(
                BlockType::Planks);
        const auto& selected =
            draft_plans_[selected_plan_];
        if (selected.cell_count > 0U) {
            const auto& first =
                selected.cells[0U];
            cursor_ = {
                first.x,
                first.y,
                first.z,
            };
            selected_material_id_ =
                first.material_id;
        }
        active_ = true;
        return make_ui_result(true);
    }

    [[nodiscard]] auto active() const noexcept -> bool {
        return active_;
    }

    [[nodiscard]] auto selected_plan() const noexcept
        -> std::size_t {
        return selected_plan_;
    }

    [[nodiscard]] auto cursor() const noexcept
        -> ConstructionPlanEditorCursor {
        return cursor_;
    }

    [[nodiscard]] auto selected_material() const noexcept
        -> std::uint16_t {
        return selected_material_id_;
    }

    [[nodiscard]] auto draft_plan() const noexcept
        -> const ConstructionPlan* {
        return active_
                   ? &draft_plans_[selected_plan_]
                   : nullptr;
    }

    [[nodiscard]] auto select_plan(
        std::size_t plan_index) noexcept
        -> ConstructionPlanEditorResult {
        if (!active_) {
            return make_failure(
                ConstructionPlanEditorFailure::Inactive);
        }
        if (plan_index >=
            draft_plans_.size()) {
            return make_failure(
                ConstructionPlanEditorFailure::InvalidPlan);
        }

        const auto changed =
            selected_plan_ != plan_index;
        selected_plan_ =
            plan_index;
        return make_ui_result(
            changed);
    }

    [[nodiscard]] auto set_cursor(
        int x,
        int y,
        int z) noexcept
        -> ConstructionPlanEditorResult {
        if (!active_) {
            return make_failure(
                ConstructionPlanEditorFailure::Inactive);
        }

        const auto next =
            ConstructionPlanEditorCursor {
                clamp_coordinate(x),
                clamp_coordinate(y),
                clamp_coordinate(z),
            };
        const auto changed =
            cursor_ != next;
        cursor_ = next;
        return make_ui_result(
            changed);
    }

    [[nodiscard]] auto move_cursor(
        int delta_x,
        int delta_y,
        int delta_z) noexcept
        -> ConstructionPlanEditorResult {
        const auto next_x =
            static_cast<std::int64_t>(
                cursor_.x) +
            static_cast<std::int64_t>(
                delta_x);
        const auto next_y =
            static_cast<std::int64_t>(
                cursor_.y) +
            static_cast<std::int64_t>(
                delta_y);
        const auto next_z =
            static_cast<std::int64_t>(
                cursor_.z) +
            static_cast<std::int64_t>(
                delta_z);
        return set_cursor(
            clamp_coordinate_64(next_x),
            clamp_coordinate_64(next_y),
            clamp_coordinate_64(next_z));
    }

    [[nodiscard]] auto select_layer(
        int layer) noexcept
        -> ConstructionPlanEditorResult {
        return set_cursor(
            cursor_.x,
            layer,
            cursor_.z);
    }

    [[nodiscard]] auto set_material(
        std::uint16_t material_id) noexcept
        -> ConstructionPlanEditorResult {
        if (!active_) {
            return make_failure(
                ConstructionPlanEditorFailure::Inactive);
        }
        if (!construction_plan_editor_material_is_valid(
                material_id)) {
            return make_failure(
                ConstructionPlanEditorFailure::InvalidMaterial);
        }

        const auto changed =
            selected_material_id_ !=
            material_id;
        selected_material_id_ =
            material_id;
        return make_ui_result(
            changed);
    }

    [[nodiscard]] auto set_shape(
        ConstructionPlanShape shape) noexcept
        -> ConstructionPlanEditorResult {
        if (!active_) {
            return make_failure(
                ConstructionPlanEditorFailure::Inactive);
        }
        if (!shape_is_valid(shape)) {
            return make_failure(
                ConstructionPlanEditorFailure::InvalidShape);
        }

        auto& plan =
            draft_plans_[selected_plan_];
        const auto changed =
            plan.shape != shape;
        plan.shape = shape;
        return make_ui_result(
            changed);
    }

    [[nodiscard]] auto set_mirrored(
        bool mirrored) noexcept
        -> ConstructionPlanEditorResult {
        if (!active_) {
            return make_failure(
                ConstructionPlanEditorFailure::Inactive);
        }
        if (mirrored &&
            !mirror_unlocked_) {
            return make_failure(
                ConstructionPlanEditorFailure::MirrorLocked);
        }

        auto& plan =
            draft_plans_[selected_plan_];
        const auto changed =
            plan.mirrored != mirrored;
        plan.mirrored = mirrored;
        return make_ui_result(
            changed);
    }

    [[nodiscard]] auto toggle_mirrored() noexcept
        -> ConstructionPlanEditorResult {
        if (!active_) {
            return make_failure(
                ConstructionPlanEditorFailure::Inactive);
        }
        return set_mirrored(
            !draft_plans_[selected_plan_]
                 .mirrored);
    }

    [[nodiscard]] auto place_cell() noexcept
        -> ConstructionPlanEditorResult {
        if (!active_) {
            return make_failure(
                ConstructionPlanEditorFailure::Inactive);
        }
        if (!construction_plan_editor_material_is_valid(
                selected_material_id_)) {
            return make_failure(
                ConstructionPlanEditorFailure::InvalidMaterial);
        }

        auto& plan =
            draft_plans_[selected_plan_];
        const auto existing =
            find_cell_index(
                plan,
                cursor_);
        if (existing < plan.cell_count) {
            auto& cell =
                plan.cells[existing];
            const auto changed =
                cell.material_id !=
                selected_material_id_;
            cell.material_id =
                selected_material_id_;
            return make_ui_result(
                changed);
        }
        if (plan.cell_count >=
                plan.cells.size() ||
            plan.cell_count >=
                maximum_cell_count_) {
            return make_failure(
                ConstructionPlanEditorFailure::PlanFull);
        }

        plan.cells[plan.cell_count] = {
            cursor_.x,
            cursor_.y,
            cursor_.z,
            selected_material_id_,
        };
        ++plan.cell_count;
        return make_ui_result(true);
    }

    [[nodiscard]] auto remove_cell() noexcept
        -> ConstructionPlanEditorResult {
        if (!active_) {
            return make_failure(
                ConstructionPlanEditorFailure::Inactive);
        }

        auto& plan =
            draft_plans_[selected_plan_];
        const auto index =
            find_cell_index(
                plan,
                cursor_);
        if (index >= plan.cell_count) {
            return make_failure(
                ConstructionPlanEditorFailure::CellMissing);
        }

        for (auto source = index + 1U;
             source < plan.cell_count;
             ++source) {
            plan.cells[source - 1U] =
                plan.cells[source];
        }
        --plan.cell_count;
        plan.cells[plan.cell_count] = {};
        return make_ui_result(true);
    }

    [[nodiscard]] auto clear_plan() noexcept
        -> ConstructionPlanEditorResult {
        if (!active_) {
            return make_failure(
                ConstructionPlanEditorFailure::Inactive);
        }

        auto& plan =
            draft_plans_[selected_plan_];
        const auto changed =
            plan.cell_count != 0U;
        plan.cells = {};
        plan.cell_count = 0U;
        return make_ui_result(
            changed);
    }

    [[nodiscard]] auto commit(
        PlayerBuildState& state) noexcept
        -> ConstructionPlanEditorResult {
        if (!active_) {
            return make_failure(
                ConstructionPlanEditorFailure::Inactive);
        }
        if (state.revision !=
            base_revision_) {
            return make_failure(
                ConstructionPlanEditorFailure::
                    ConcurrentBuildMutation);
        }
        for (const auto& plan :
             draft_plans_) {
            if (!plan_is_valid(plan) ||
                plan.cell_count >
                    maximum_cell_count_) {
                return make_failure(
                    ConstructionPlanEditorFailure::InvalidPlan);
            }
        }

        const auto build_changed =
            state.construction_plans !=
                draft_plans_ ||
            state.selected_construction_plan !=
                static_cast<std::uint8_t>(
                    selected_plan_);
        if (build_changed) {
            // Je applique toutes les modifications sur une copie, puis je
            // remplace le build en une seule operation observable.
            auto next_state = state;
            next_state.construction_plans =
                draft_plans_;
            next_state.selected_construction_plan =
                static_cast<std::uint8_t>(
                    selected_plan_);
            next_state.last_dominant_path =
                AbilityPath::Builder;
            if (next_state.revision !=
                std::numeric_limits<
                    std::uint64_t>::max()) {
                ++next_state.revision;
            }
            state =
                next_state;
        }

        active_ = false;
        return {
            ConstructionPlanEditorFailure::None,
            true,
            build_changed,
        };
    }

    [[nodiscard]] auto cancel() noexcept
        -> ConstructionPlanEditorResult {
        if (!active_) {
            return make_failure(
                ConstructionPlanEditorFailure::Inactive);
        }
        active_ = false;
        return make_ui_result(true);
    }

    [[nodiscard]] auto make_view_model() const noexcept
        -> ConstructionPlanEditorViewModel {
        ConstructionPlanEditorViewModel view {};
        view.active =
            active_;
        if (!active_) {
            return view;
        }

        view.selected_plan =
            selected_plan_;
        view.cursor =
            cursor_;
        view.selected_layer =
            cursor_.y;
        view.selected_material_id =
            selected_material_id_;
        view.mirror_unlocked =
            mirror_unlocked_;
        view.dirty =
            draft_plans_ !=
                original_plans_ ||
            selected_plan_ !=
                original_selected_plan_;

        const auto& plan =
            draft_plans_[selected_plan_];
        view.shape =
            plan.shape;
        view.mirrored =
            plan.mirrored;
        view.cell_count =
            plan.cell_count;
        view.maximum_cell_count =
            maximum_cell_count_;
        const auto selected_cell =
            find_cell_index(
                plan,
                cursor_);
        view.can_place =
            construction_plan_editor_material_is_valid(
                selected_material_id_) &&
            (selected_cell <
                 plan.cell_count ||
             plan.cell_count <
                 plan.cells.size()) &&
            (selected_cell <
                 plan.cell_count ||
             plan.cell_count <
                 maximum_cell_count_);
        view.can_remove =
            selected_cell <
            plan.cell_count;
        view.can_commit = true;
        for (const auto& candidate :
             draft_plans_) {
            view.can_commit =
                view.can_commit &&
                plan_is_valid(candidate) &&
                candidate.cell_count <=
                    maximum_cell_count_;
        }

        for (std::size_t index = 0U;
             index <
             static_cast<std::size_t>(
                 plan.cell_count);
             ++index) {
            const auto& cell =
                plan.cells[index];
            auto& cell_view =
                view.cells[index];
            cell_view.position = {
                cell.x,
                cell.y,
                cell.z,
            };
            cell_view.material_id =
                cell.material_id;
            cell_view.selected =
                cell_view.position ==
                cursor_;
            cell_view.on_selected_layer =
                cell.y ==
                cursor_.y;
            if (cell_view.on_selected_layer) {
                ++view
                     .cells_on_selected_layer;
            }
        }
        return view;
    }

private:
    [[nodiscard]] static auto make_failure(
        ConstructionPlanEditorFailure failure) noexcept
        -> ConstructionPlanEditorResult {
        return {
            failure,
            false,
            false,
        };
    }

    [[nodiscard]] static auto make_ui_result(
        bool changed) noexcept
        -> ConstructionPlanEditorResult {
        return {
            ConstructionPlanEditorFailure::None,
            changed,
            false,
        };
    }

    [[nodiscard]] static auto bounded_plan_index(
        std::uint8_t plan_index) noexcept
        -> std::size_t {
        return std::min<std::size_t>(
            plan_index,
            kConstructionPlanCount - 1U);
    }

    [[nodiscard]] static auto shape_is_valid(
        ConstructionPlanShape shape) noexcept
        -> bool {
        return shape ==
                   ConstructionPlanShape::Line ||
               shape ==
                   ConstructionPlanShape::Grid;
    }

    [[nodiscard]] static auto clamp_coordinate(
        int value) noexcept -> std::int8_t {
        return static_cast<std::int8_t>(
            std::clamp(
                value,
                -static_cast<int>(
                    kConstructionPlanCoordinateLimit),
                static_cast<int>(
                    kConstructionPlanCoordinateLimit)));
    }

    [[nodiscard]] static auto clamp_coordinate_64(
        std::int64_t value) noexcept -> int {
        return static_cast<int>(
            std::clamp<std::int64_t>(
                value,
                -static_cast<std::int64_t>(
                    kConstructionPlanCoordinateLimit),
                static_cast<std::int64_t>(
                    kConstructionPlanCoordinateLimit)));
    }

    [[nodiscard]] static auto cell_matches(
        const ConstructionPlanCell& cell,
        const ConstructionPlanEditorCursor& cursor) noexcept
        -> bool {
        return cell.x == cursor.x &&
               cell.y == cursor.y &&
               cell.z == cursor.z;
    }

    [[nodiscard]] static auto find_cell_index(
        const ConstructionPlan& plan,
        const ConstructionPlanEditorCursor& cursor) noexcept
        -> std::size_t {
        const auto count =
            std::min<std::size_t>(
                plan.cell_count,
                plan.cells.size());
        for (std::size_t index = 0U;
             index < count;
             ++index) {
            if (cell_matches(
                    plan.cells[index],
                    cursor)) {
                return index;
            }
        }
        return count;
    }

    [[nodiscard]] static auto plan_is_valid(
        const ConstructionPlan& plan) noexcept
        -> bool {
        if (!shape_is_valid(
                plan.shape) ||
            plan.cell_count >
                plan.cells.size()) {
            return false;
        }

        for (std::size_t index = 0U;
             index <
             static_cast<std::size_t>(
                 plan.cell_count);
             ++index) {
            const auto& cell =
                plan.cells[index];
            if (!construction_plan_editor_material_is_valid(
                    cell.material_id) ||
                cell.x <
                    -kConstructionPlanCoordinateLimit ||
                cell.x >
                    kConstructionPlanCoordinateLimit ||
                cell.y <
                    -kConstructionPlanCoordinateLimit ||
                cell.y >
                    kConstructionPlanCoordinateLimit ||
                cell.z <
                    -kConstructionPlanCoordinateLimit ||
                cell.z >
                    kConstructionPlanCoordinateLimit) {
                return false;
            }
            for (std::size_t previous = 0U;
                 previous < index;
                 ++previous) {
                if (cell.x ==
                        plan.cells[previous].x &&
                    cell.y ==
                        plan.cells[previous].y &&
                    cell.z ==
                        plan.cells[previous].z) {
                    return false;
                }
            }
        }
        return true;
    }

    static void normalize_plan(
        ConstructionPlan& plan,
        bool mirror_unlocked) noexcept {
        if (!shape_is_valid(
                plan.shape)) {
            plan.shape =
                ConstructionPlanShape::Line;
        }
        plan.mirrored =
            plan.mirrored &&
            mirror_unlocked;

        const auto requested_count =
            std::min<std::size_t>(
                plan.cell_count,
                plan.cells.size());
        auto normalized =
            std::array<
                ConstructionPlanCell,
                kConstructionPlanMaximumCellCount> {};
        auto normalized_count =
            std::size_t {0U};
        for (std::size_t index = 0U;
             index < requested_count;
             ++index) {
            auto cell =
                plan.cells[index];
            if (!construction_plan_editor_material_is_valid(
                    cell.material_id)) {
                continue;
            }
            cell.x =
                clamp_coordinate(
                    cell.x);
            cell.y =
                clamp_coordinate(
                    cell.y);
            cell.z =
                clamp_coordinate(
                    cell.z);

            const auto cursor =
                ConstructionPlanEditorCursor {
                    cell.x,
                    cell.y,
                    cell.z,
                };
            auto duplicate = false;
            for (std::size_t previous = 0U;
                 previous <
                 normalized_count;
                 ++previous) {
                duplicate =
                    duplicate ||
                    cell_matches(
                        normalized[previous],
                        cursor);
            }
            if (!duplicate) {
                normalized[
                    normalized_count++] =
                    cell;
            }
        }

        plan.cells =
            normalized;
        plan.cell_count =
            static_cast<std::uint8_t>(
                normalized_count);
    }

    bool active_ = false;
    bool mirror_unlocked_ = false;
    std::uint64_t base_revision_ = 0U;
    std::array<
        ConstructionPlan,
        kConstructionPlanCount>
        original_plans_ {};
    std::array<
        ConstructionPlan,
        kConstructionPlanCount>
        draft_plans_ {};
    std::size_t original_selected_plan_ = 0U;
    std::size_t selected_plan_ = 0U;
    ConstructionPlanEditorCursor cursor_ {};
    std::uint16_t selected_material_id_ =
        to_block_id(
            BlockType::Planks);
    std::uint8_t maximum_cell_count_ =
        static_cast<std::uint8_t>(
            kConstructionPlanMaximumCellCount);
};

} // namespace valcraft
