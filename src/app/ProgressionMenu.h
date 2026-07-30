#pragma once

#include "gameplay/progression/PlayerBuildState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace valcraft {

inline constexpr std::size_t kProgressionMenuAbilityPerPathCount = 10U;

enum class ProgressionMenuInput : std::uint8_t {
    ToggleVisibility = 0,
    Close,
    PreviousPath,
    NextPath,
    PreviousAbility,
    NextAbility,
    PreviousSlot,
    NextSlot,
    PurchaseRank,
    PurchaseMastery,
    EquipOrUnequip,
    AllocateStrength,
    AllocateWisdom,
    AllocateAgility,
    AllocateRobustness,
};

enum class ProgressionMenuSlotKind : std::uint8_t {
    Active = 0,
    Utility,
    Ultimate,
};

enum class ProgressionMenuFailure : std::uint8_t {
    None = 0,
    Hidden,
    InvalidSelection,
    IncompatibleSlot,
    AttributeCapReached,
    InsufficientAttributePoints,
    AbilityBuildRejected,
};

struct ProgressionMenuActionResult {
    ProgressionMenuFailure failure = ProgressionMenuFailure::None;
    AbilityBuildFailure ability_failure = AbilityBuildFailure::None;
    // Je separe l'etat local du menu de l'etat persistant du personnage afin
    // qu'une simple navigation ne puisse jamais salir ou reviser le build.
    bool ui_changed = false;
    bool build_changed = false;

    [[nodiscard]] constexpr auto succeeded() const noexcept -> bool {
        return failure == ProgressionMenuFailure::None;
    }

    [[nodiscard]] constexpr auto any_changed() const noexcept -> bool {
        return ui_changed || build_changed;
    }
};

struct ProgressionMenuAttributeView {
    PlayerAttribute attribute = PlayerAttribute::Strength;
    std::string_view name {};
    std::uint8_t allocated_value = 0U;
    std::uint8_t allocation_cap = kPlayerAttributeLevelCap;
    ProgressionMenuFailure allocation_failure =
        ProgressionMenuFailure::None;
};

struct ProgressionMenuSlotView {
    std::size_t index = 0U;
    ProgressionMenuSlotKind kind = ProgressionMenuSlotKind::Active;
    std::string_view name {};
    AbilityId ability = AbilityId::None;
    std::string_view ability_stable_name {};
    std::string_view ability_display_name {};
};

struct ProgressionMenuAbilityView {
    AbilityId id = AbilityId::None;
    std::string_view stable_name {};
    std::string_view display_name {};
    AbilityPath path = AbilityPath::Knight;
    std::string_view path_name {};
    AbilityCategory category = AbilityCategory::Active;
    std::string_view category_name {};
    std::uint8_t tier = 1U;
    std::uint8_t current_rank = 0U;
    std::uint8_t maximum_rank =
        static_cast<std::uint8_t>(kAbilityRankCount);
    bool implemented = false;
    bool mastered = false;
    bool equipped = false;
    std::int8_t equipped_slot = -1;

    std::uint8_t required_level = 1U;
    std::uint8_t required_path_points = 0U;
    AbilityId prerequisite = AbilityId::None;
    std::string_view prerequisite_stable_name {};
    std::string_view prerequisite_display_name {};

    std::uint8_t next_rank = 0U;
    std::uint8_t next_rank_required_level = 0U;
    std::uint8_t next_rank_skill_point_cost = 0U;
    AbilityBuildFailure rank_purchase_failure =
        AbilityBuildFailure::None;
    std::string_view rank_purchase_status {};

    AbilityBuildFailure mastery_purchase_failure =
        AbilityBuildFailure::None;
    std::string_view mastery_purchase_status {};
    std::uint8_t mastery_point_cost = 1U;

    float energy_cost = 0.0F;
    float cooldown_seconds = 0.0F;
    float range_meters = 0.0F;
    float duration_seconds = 0.0F;
};

struct ProgressionMenuViewModel {
    bool visible = false;
    std::uint32_t level = 1U;
    AbilityPath selected_path = AbilityPath::Knight;
    std::string_view selected_path_name {};
    std::uint8_t selected_tier = 1U;
    AbilityId selected_ability = AbilityId::None;
    std::size_t selected_slot = 0U;
    std::string_view selected_slot_name {};
    PlayerBuildPointBudget budget {};
    ProgressionMenuAbilityView ability {};
    std::array<
        ProgressionMenuAttributeView,
        kPlayerAttributeCount> attributes {};
    std::array<
        ProgressionMenuSlotView,
        kEquippedAbilitySlotCount> slots {};
};

[[nodiscard]] auto progression_path_name(
    AbilityPath path) noexcept -> std::string_view;

[[nodiscard]] auto progression_ability_display_name(
    AbilityId id) noexcept -> std::string_view;

[[nodiscard]] auto progression_ability_category_name(
    AbilityCategory category) noexcept -> std::string_view;

[[nodiscard]] auto progression_attribute_name(
    PlayerAttribute attribute) noexcept -> std::string_view;

[[nodiscard]] auto progression_slot_kind(
    std::size_t slot) noexcept -> ProgressionMenuSlotKind;

[[nodiscard]] auto progression_slot_name(
    std::size_t slot) noexcept -> std::string_view;

[[nodiscard]] auto progression_ability_failure_text(
    AbilityBuildFailure failure) noexcept -> std::string_view;

[[nodiscard]] auto progression_menu_failure_text(
    ProgressionMenuFailure failure) noexcept -> std::string_view;

class ProgressionMenu {
public:
    [[nodiscard]] auto visible() const noexcept -> bool;
    void set_visible(bool visible) noexcept;
    void toggle_visibility() noexcept;

    [[nodiscard]] auto selected_path() const noexcept -> AbilityPath;
    void select_path(AbilityPath path) noexcept;
    [[nodiscard]] auto sync_selected_path_from_build(
        const PlayerBuildState& state,
        std::uint32_t level) noexcept -> bool;

    [[nodiscard]] auto selected_tier() const noexcept -> std::uint8_t;
    void select_tier(std::size_t tier) noexcept;

    [[nodiscard]] auto selected_ability() const noexcept -> AbilityId;

    [[nodiscard]] auto selected_slot() const noexcept -> std::size_t;
    void select_slot(std::size_t slot) noexcept;

    void sanitize() noexcept;

    [[nodiscard]] auto purchase_selected_rank(
        PlayerBuildState& state,
        std::uint32_t level) noexcept -> ProgressionMenuActionResult;

    [[nodiscard]] auto purchase_selected_mastery(
        PlayerBuildState& state,
        std::uint32_t level) noexcept -> ProgressionMenuActionResult;

    [[nodiscard]] auto equip_or_unequip_selected(
        PlayerBuildState& state) noexcept -> ProgressionMenuActionResult;

    [[nodiscard]] auto allocate_attribute(
        PlayerBuildState& state,
        std::uint32_t level,
        PlayerAttribute attribute) noexcept -> ProgressionMenuActionResult;

    [[nodiscard]] auto handle_input(
        ProgressionMenuInput input,
        PlayerBuildState& state,
        std::uint32_t level) noexcept -> ProgressionMenuActionResult;

    [[nodiscard]] auto make_view_model(
        const PlayerBuildState& state,
        std::uint32_t level) const noexcept -> ProgressionMenuViewModel;

private:
    bool visible_ = false;
    AbilityPath selected_path_ = AbilityPath::Knight;
    std::uint8_t selected_tier_index_ = 0U;
    std::size_t selected_slot_ = 0U;
};

} // namespace valcraft
