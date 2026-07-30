#include "app/ProgressionMenu.h"

#include <algorithm>
#include <array>
#include <limits>

namespace valcraft {

namespace {

constexpr std::array<std::string_view, kAbilityPathCount> kPathNames {{
    "Chevalier",
    "Ninja",
    "Commandant",
    "Bâtisseur",
}};

constexpr std::array<std::string_view, kAbilityCount> kAbilityDisplayNames {{
    "Frappe d'avant-garde",
    "Garde de fer",
    "Charge du rempart",
    "Cri du champion",
    "Riposte parfaite",
    "Onde de choc",
    "Peau d'acier",
    "Fureur du colosse",
    "Forteresse vivante",
    "Jugement du titan",

    "Accélération du vent",
    "Bombe fumigène",
    "Bond shinobi",
    "Charge foudroyante",
    "Kunaï spectral",
    "Substitution",
    "Pas fantôme",
    "Danse des lames",
    "Marque de l'assassin",
    "Tempête des mille éclairs",

    "Invocation de fantassin",
    "Ordre : Assaut",
    "Tireur de la flotte",
    "Bannière de guerre",
    "Ordre : Formation du rempart",
    "Invocation de médecin",
    "Invocation de sapeur",
    "Capitaine d'élite",
    "Renforts immédiats",
    "Grande armée",

    "Plan de chantier",
    "Réparation express",
    "Mur déployable",
    "Pont modulaire",
    "Onde d'excavation",
    "Golem constructeur",
    "Tourelle automatisée",
    "Bastion modulaire",
    "Grand projet",
    "Architecte absolu",
}};

constexpr std::array<std::string_view, kPlayerAttributeCount>
    kAttributeNames {{
        "Force",
        "Sagesse",
        "Agilité",
        "Robustesse",
    }};

constexpr std::array<std::string_view, kEquippedAbilitySlotCount>
    kSlotNames {{
        "Actif 1",
        "Actif 2",
        "Actif 3",
        "Utilité",
        "Ultime",
    }};

[[nodiscard]] auto make_menu_failure(
    ProgressionMenuFailure failure) noexcept
    -> ProgressionMenuActionResult {
    return {
        failure,
        AbilityBuildFailure::None,
        false,
        false,
    };
}

[[nodiscard]] auto make_ability_failure(
    AbilityBuildFailure failure) noexcept
    -> ProgressionMenuActionResult {
    return {
        ProgressionMenuFailure::AbilityBuildRejected,
        failure,
        false,
        false,
    };
}

[[nodiscard]] auto make_ui_success(
    bool changed = true) noexcept
    -> ProgressionMenuActionResult {
    return {
        ProgressionMenuFailure::None,
        AbilityBuildFailure::None,
        changed,
        false,
    };
}

[[nodiscard]] auto make_build_success() noexcept
    -> ProgressionMenuActionResult {
    return {
        ProgressionMenuFailure::None,
        AbilityBuildFailure::None,
        false,
        true,
    };
}

void remember_successful_build_path(
    PlayerBuildState& state,
    AbilityPath path) noexcept {
    // Je rattache le choix valide a la voie affichee sans incrementer une
    // seconde fois la revision deja portee par l'action de build.
    state.last_dominant_path =
        ability_path_index(path) < kAbilityPathCount
            ? path
            : AbilityPath::Knight;
}

[[nodiscard]] auto compatible_slot(
    std::size_t slot,
    AbilityCategory category) noexcept -> bool {
    switch (progression_slot_kind(slot)) {
    case ProgressionMenuSlotKind::Active:
        return category == AbilityCategory::Active;
    case ProgressionMenuSlotKind::Utility:
        return category == AbilityCategory::Utility;
    case ProgressionMenuSlotKind::Ultimate:
        return category == AbilityCategory::Ultimate;
    }
    return false;
}

[[nodiscard]] auto allocation_failure(
    const PlayerBuildState& state,
    std::uint32_t level,
    PlayerAttribute attribute) noexcept -> ProgressionMenuFailure {
    const auto attribute_index =
        player_attribute_index(attribute);
    if (attribute_index >= state.attributes.values.size()) {
        return ProgressionMenuFailure::InvalidSelection;
    }
    if (state.attributes.values[attribute_index] >=
        kPlayerAttributeLevelCap) {
        return ProgressionMenuFailure::AttributeCapReached;
    }
    if (player_build_point_budget(
            state,
            level)
            .available_attribute_points == 0U) {
        return ProgressionMenuFailure::InsufficientAttributePoints;
    }
    return ProgressionMenuFailure::None;
}

[[nodiscard]] auto equipped_slot_for(
    const PlayerBuildState& state,
    AbilityId id) noexcept -> std::int8_t {
    for (std::size_t slot = 0U;
         slot < state.equipped_abilities.size();
         ++slot) {
        if (state.equipped_abilities[slot] == id) {
            return static_cast<std::int8_t>(slot);
        }
    }
    return -1;
}

[[nodiscard]] auto effective_rank_definition(
    const PlayerBuildState& state,
    AbilityId id) noexcept -> const AbilityRankDefinition* {
    const auto current_rank =
        player_ability_rank(
            state,
            id);
    return ability_rank_definition(
        id,
        current_rank == 0U ? 1U : current_rank);
}

} // namespace

auto progression_path_name(
    AbilityPath path) noexcept -> std::string_view {
    const auto index =
        ability_path_index(path);
    return index < kPathNames.size()
               ? kPathNames[index]
               : std::string_view {"Voie inconnue"};
}

auto progression_ability_display_name(
    AbilityId id) noexcept -> std::string_view {
    const auto index =
        ability_index(id);
    return index < kAbilityDisplayNames.size()
               ? kAbilityDisplayNames[index]
               : std::string_view {"Compétence inconnue"};
}

auto progression_ability_category_name(
    AbilityCategory category) noexcept -> std::string_view {
    switch (category) {
    case AbilityCategory::Active:
        return "Active";
    case AbilityCategory::Utility:
        return "Utilité";
    case AbilityCategory::Passive:
        return "Passive";
    case AbilityCategory::Ultimate:
        return "Ultime";
    }
    return "Catégorie inconnue";
}

auto progression_attribute_name(
    PlayerAttribute attribute) noexcept -> std::string_view {
    const auto index =
        player_attribute_index(attribute);
    return index < kAttributeNames.size()
               ? kAttributeNames[index]
               : std::string_view {"Attribut inconnu"};
}

auto progression_slot_kind(
    std::size_t slot) noexcept -> ProgressionMenuSlotKind {
    if (slot == 3U) {
        return ProgressionMenuSlotKind::Utility;
    }
    if (slot == 4U) {
        return ProgressionMenuSlotKind::Ultimate;
    }
    return ProgressionMenuSlotKind::Active;
}

auto progression_slot_name(
    std::size_t slot) noexcept -> std::string_view {
    return slot < kSlotNames.size()
               ? kSlotNames[slot]
               : std::string_view {"Slot inconnu"};
}

auto progression_ability_failure_text(
    AbilityBuildFailure failure) noexcept -> std::string_view {
    switch (failure) {
    case AbilityBuildFailure::None:
        return "Disponible";
    case AbilityBuildFailure::InvalidAbility:
        return "Compétence invalide";
    case AbilityBuildFailure::InvalidRank:
        return "Rang maximal atteint";
    case AbilityBuildFailure::RankOutOfSequence:
        return "Rangs à acheter dans l'ordre";
    case AbilityBuildFailure::RequiredLevel:
        return "Niveau requis non atteint";
    case AbilityBuildFailure::RequiredPathPoints:
        return "Points dépensés dans la voie insuffisants";
    case AbilityBuildFailure::MissingPrerequisite:
        return "Palier précédent requis";
    case AbilityBuildFailure::InsufficientSkillPoints:
        return "Points de compétence insuffisants";
    case AbilityBuildFailure::AlreadyMastered:
        return "Maîtrise déjà acquise";
    case AbilityBuildFailure::RankThreeRequired:
        return "Rang III requis";
    case AbilityBuildFailure::InsufficientMasteryPoints:
        return "Point de maîtrise insuffisant";
    case AbilityBuildFailure::InvalidSlot:
        return "Slot invalide";
    case AbilityBuildFailure::AbilityNotLearned:
        return "Compétence non apprise";
    case AbilityBuildFailure::PassiveNotEquippable:
        return "Compétence passive non équipable";
    case AbilityBuildFailure::UnimplementedAbility:
        return "Competence en preparation";
    case AbilityBuildFailure::IncompatibleSlot:
        return "Emplacement incompatible";
    case AbilityBuildFailure::DuplicateAbility:
        return "Compétence déjà équipée";
    }
    return "Action refusée";
}

auto progression_menu_failure_text(
    ProgressionMenuFailure failure) noexcept -> std::string_view {
    switch (failure) {
    case ProgressionMenuFailure::None:
        return "Action réussie";
    case ProgressionMenuFailure::Hidden:
        return "Menu fermé";
    case ProgressionMenuFailure::InvalidSelection:
        return "Sélection invalide";
    case ProgressionMenuFailure::IncompatibleSlot:
        return "Type de slot incompatible";
    case ProgressionMenuFailure::AttributeCapReached:
        return "Attribut au maximum";
    case ProgressionMenuFailure::InsufficientAttributePoints:
        return "Point d'attribut insuffisant";
    case ProgressionMenuFailure::AbilityBuildRejected:
        return "Action de compétence refusée";
    }
    return "Action refusée";
}

auto ProgressionMenu::visible() const noexcept -> bool {
    return visible_;
}

void ProgressionMenu::set_visible(
    bool visible) noexcept {
    visible_ = visible;
}

void ProgressionMenu::toggle_visibility() noexcept {
    visible_ = !visible_;
}

auto ProgressionMenu::selected_path() const noexcept -> AbilityPath {
    return selected_path_;
}

void ProgressionMenu::select_path(
    AbilityPath path) noexcept {
    selected_path_ =
        ability_path_index(path) < kAbilityPathCount
            ? path
            : AbilityPath::Knight;
}

auto ProgressionMenu::sync_selected_path_from_build(
    const PlayerBuildState& state,
    std::uint32_t level) noexcept -> bool {
    const auto synchronized_path =
        player_dominant_path(
            state,
            level);
    const auto changed =
        selected_path_ != synchronized_path;
    selected_path_ = synchronized_path;
    return changed;
}

auto ProgressionMenu::selected_tier() const noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(
        selected_tier_index_ + 1U);
}

void ProgressionMenu::select_tier(
    std::size_t tier) noexcept {
    // Je reçois ici un palier humain compris entre 1 et 10.
    const auto clamped =
        std::clamp<std::size_t>(
            tier,
            1U,
            kProgressionMenuAbilityPerPathCount);
    selected_tier_index_ =
        static_cast<std::uint8_t>(
            clamped - 1U);
}

auto ProgressionMenu::selected_ability() const noexcept -> AbilityId {
    const auto first =
        ability_path_first_id(
            selected_path_);
    const auto first_index =
        ability_index(first);
    if (first_index >= kAbilityCount) {
        return AbilityId::None;
    }
    return ability_id_from_index(
        first_index +
        selected_tier_index_);
}

auto ProgressionMenu::selected_slot() const noexcept -> std::size_t {
    return selected_slot_;
}

void ProgressionMenu::select_slot(
    std::size_t slot) noexcept {
    selected_slot_ =
        std::min<std::size_t>(
            slot,
            kEquippedAbilitySlotCount - 1U);
}

void ProgressionMenu::sanitize() noexcept {
    select_path(
        selected_path_);
    select_tier(
        selected_tier());
    select_slot(
        selected_slot_);
}

auto ProgressionMenu::purchase_selected_rank(
    PlayerBuildState& state,
    std::uint32_t level) noexcept -> ProgressionMenuActionResult {
    const auto id =
        selected_ability();
    if (!ability_id_is_valid(id)) {
        return make_menu_failure(
            ProgressionMenuFailure::InvalidSelection);
    }
    const auto result =
        purchase_player_ability_rank(
            state,
            level,
            id);
    if (!result.succeeded()) {
        return make_ability_failure(
            result.failure);
    }
    remember_successful_build_path(
        state,
        selected_path_);
    return make_build_success();
}

auto ProgressionMenu::purchase_selected_mastery(
    PlayerBuildState& state,
    std::uint32_t level) noexcept -> ProgressionMenuActionResult {
    const auto id =
        selected_ability();
    if (!ability_id_is_valid(id)) {
        return make_menu_failure(
            ProgressionMenuFailure::InvalidSelection);
    }
    const auto result =
        purchase_player_ability_mastery(
            state,
            level,
            id);
    if (!result.succeeded()) {
        return make_ability_failure(
            result.failure);
    }
    remember_successful_build_path(
        state,
        selected_path_);
    return make_build_success();
}

auto ProgressionMenu::equip_or_unequip_selected(
    PlayerBuildState& state) noexcept -> ProgressionMenuActionResult {
    const auto id =
        selected_ability();
    const auto* definition =
        ability_definition(id);
    if (definition == nullptr) {
        return make_menu_failure(
            ProgressionMenuFailure::InvalidSelection);
    }
    if (state.equipped_abilities[selected_slot_] == id) {
        const auto result =
            equip_player_ability(
                state,
                selected_slot_,
                AbilityId::None);
        if (!result.succeeded()) {
            return make_ability_failure(
                result.failure);
        }
        remember_successful_build_path(
            state,
            selected_path_);
        return make_build_success();
    }
    if (!compatible_slot(
            selected_slot_,
            definition->category)) {
        return make_menu_failure(
            ProgressionMenuFailure::IncompatibleSlot);
    }
    const auto result =
        equip_player_ability(
            state,
            selected_slot_,
            id);
    if (!result.succeeded()) {
        return make_ability_failure(
            result.failure);
    }
    remember_successful_build_path(
        state,
        selected_path_);
    return make_build_success();
}

auto ProgressionMenu::allocate_attribute(
    PlayerBuildState& state,
    std::uint32_t level,
    PlayerAttribute attribute) noexcept -> ProgressionMenuActionResult {
    const auto failure =
        allocation_failure(
            state,
            level,
            attribute);
    if (failure != ProgressionMenuFailure::None) {
        return make_menu_failure(
            failure);
    }

    ++state.attributes.values[
        player_attribute_index(attribute)];
    if (state.revision !=
        std::numeric_limits<std::uint64_t>::max()) {
        ++state.revision;
    }
    remember_successful_build_path(
        state,
        selected_path_);
    return make_build_success();
}

auto ProgressionMenu::handle_input(
    ProgressionMenuInput input,
    PlayerBuildState& state,
    std::uint32_t level) noexcept -> ProgressionMenuActionResult {
    if (input == ProgressionMenuInput::ToggleVisibility) {
        toggle_visibility();
        return make_ui_success();
    }
    if (input == ProgressionMenuInput::Close) {
        const auto changed =
            visible_;
        visible_ = false;
        return make_ui_success(
            changed);
    }
    if (!visible_) {
        return make_menu_failure(
            ProgressionMenuFailure::Hidden);
    }

    switch (input) {
    case ProgressionMenuInput::PreviousPath: {
        const auto index =
            ability_path_index(
                selected_path_);
        const auto previous =
            (index + kAbilityPathCount - 1U) %
            kAbilityPathCount;
        selected_path_ =
            static_cast<AbilityPath>(
                previous);
        break;
    }
    case ProgressionMenuInput::NextPath: {
        const auto index =
            ability_path_index(
                selected_path_);
        selected_path_ =
            static_cast<AbilityPath>(
                (index + 1U) %
                kAbilityPathCount);
        break;
    }
    case ProgressionMenuInput::PreviousAbility:
        selected_tier_index_ =
            static_cast<std::uint8_t>(
                (selected_tier_index_ +
                 kProgressionMenuAbilityPerPathCount -
                 1U) %
                kProgressionMenuAbilityPerPathCount);
        break;
    case ProgressionMenuInput::NextAbility:
        selected_tier_index_ =
            static_cast<std::uint8_t>(
                (selected_tier_index_ + 1U) %
                kProgressionMenuAbilityPerPathCount);
        break;
    case ProgressionMenuInput::PreviousSlot:
        selected_slot_ =
            (selected_slot_ +
             kEquippedAbilitySlotCount -
             1U) %
            kEquippedAbilitySlotCount;
        break;
    case ProgressionMenuInput::NextSlot:
        selected_slot_ =
            (selected_slot_ + 1U) %
            kEquippedAbilitySlotCount;
        break;
    case ProgressionMenuInput::PurchaseRank:
        return purchase_selected_rank(
            state,
            level);
    case ProgressionMenuInput::PurchaseMastery:
        return purchase_selected_mastery(
            state,
            level);
    case ProgressionMenuInput::EquipOrUnequip:
        return equip_or_unequip_selected(
            state);
    case ProgressionMenuInput::AllocateStrength:
        return allocate_attribute(
            state,
            level,
            PlayerAttribute::Strength);
    case ProgressionMenuInput::AllocateWisdom:
        return allocate_attribute(
            state,
            level,
            PlayerAttribute::Wisdom);
    case ProgressionMenuInput::AllocateAgility:
        return allocate_attribute(
            state,
            level,
            PlayerAttribute::Agility);
    case ProgressionMenuInput::AllocateRobustness:
        return allocate_attribute(
            state,
            level,
            PlayerAttribute::Robustness);
    case ProgressionMenuInput::ToggleVisibility:
    case ProgressionMenuInput::Close:
        break;
    }
    return make_ui_success();
}

auto ProgressionMenu::make_view_model(
    const PlayerBuildState& state,
    std::uint32_t level) const noexcept -> ProgressionMenuViewModel {
    ProgressionMenuViewModel view {};
    view.visible = visible_;
    view.level = level;
    view.selected_path = selected_path_;
    view.selected_path_name =
        progression_path_name(
            selected_path_);
    view.selected_tier =
        selected_tier();
    view.selected_ability =
        selected_ability();
    view.selected_slot =
        selected_slot_;
    view.selected_slot_name =
        progression_slot_name(
            selected_slot_);
    view.budget =
        player_build_point_budget(
            state,
            level);

    for (std::size_t index = 0U;
         index < view.attributes.size();
         ++index) {
        const auto attribute =
            static_cast<PlayerAttribute>(
                index);
        auto& attribute_view =
            view.attributes[index];
        attribute_view.attribute = attribute;
        attribute_view.name =
            progression_attribute_name(
                attribute);
        attribute_view.allocated_value =
            state.attributes.values[index];
        attribute_view.allocation_failure =
            allocation_failure(
                state,
                level,
                attribute);
    }

    for (std::size_t slot = 0U;
         slot < view.slots.size();
         ++slot) {
        auto& slot_view =
            view.slots[slot];
        slot_view.index = slot;
        slot_view.kind =
            progression_slot_kind(
                slot);
        slot_view.name =
            progression_slot_name(
                slot);
        slot_view.ability =
            state.equipped_abilities[slot];
        const auto* equipped_definition =
            ability_definition(
                slot_view.ability);
        if (equipped_definition != nullptr) {
            slot_view.ability_stable_name =
                equipped_definition->stable_name;
            slot_view.ability_display_name =
                progression_ability_display_name(
                    slot_view.ability);
        }
    }

    auto& ability_view =
        view.ability;
    const auto* definition =
        ability_definition(
            view.selected_ability);
    if (definition == nullptr) {
        return view;
    }

    ability_view.id = definition->id;
    ability_view.stable_name =
        definition->stable_name;
    ability_view.display_name =
        progression_ability_display_name(
            definition->id);
    ability_view.path = definition->path;
    ability_view.path_name =
        progression_path_name(
            definition->path);
    ability_view.category =
        definition->category;
    ability_view.category_name =
        progression_ability_category_name(
            definition->category);
    ability_view.tier = definition->tier;
    ability_view.current_rank =
        player_ability_rank(
            state,
            definition->id);
    ability_view.implemented =
        definition->implemented;
    ability_view.mastered =
        player_ability_has_mastery(
            state,
            definition->id);
    ability_view.equipped_slot =
        equipped_slot_for(
            state,
            definition->id);
    ability_view.equipped =
        ability_view.equipped_slot >= 0;
    ability_view.required_level =
        definition->required_level;
    ability_view.required_path_points =
        definition->required_path_points;
    ability_view.prerequisite =
        definition->prerequisite;
    if (const auto* prerequisite =
            ability_definition(
                definition->prerequisite);
        prerequisite != nullptr) {
        ability_view.prerequisite_stable_name =
            prerequisite->stable_name;
        ability_view.prerequisite_display_name =
            progression_ability_display_name(
                prerequisite->id);
    }

    if (ability_view.current_rank <
        kAbilityRankCount) {
        ability_view.next_rank =
            static_cast<std::uint8_t>(
                ability_view.current_rank + 1U);
        if (const auto* next_rank =
                ability_rank_definition(
                    definition->id,
                    ability_view.next_rank);
            next_rank != nullptr) {
            ability_view.next_rank_required_level =
                next_rank->required_level;
            ability_view.next_rank_skill_point_cost =
                next_rank->skill_point_cost;
        }
    }
    ability_view.rank_purchase_failure =
        player_ability_rank_purchase_failure(
            state,
            level,
            definition->id);
    ability_view.rank_purchase_status =
        progression_ability_failure_text(
            ability_view.rank_purchase_failure);
    ability_view.mastery_purchase_failure =
        player_ability_mastery_purchase_failure(
            state,
            level,
            definition->id);
    ability_view.mastery_purchase_status =
        progression_ability_failure_text(
            ability_view.mastery_purchase_failure);

    if (const auto* rank =
            effective_rank_definition(
                state,
                definition->id);
        rank != nullptr) {
        ability_view.energy_cost =
            rank->energy_cost;
        ability_view.cooldown_seconds =
            rank->cooldown_seconds;
        ability_view.range_meters =
            rank->range_meters;
        ability_view.duration_seconds =
            rank->duration_seconds;
    }
    return view;
}

} // namespace valcraft
