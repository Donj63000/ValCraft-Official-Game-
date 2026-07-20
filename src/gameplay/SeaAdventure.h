#pragma once

#include "app/Hotbar.h"
#include "creatures/CreatureTypes.h"
#include "gameplay/ShipCrew.h"
#include "world/Block.h"
#include "world/Environment.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace valcraft {

class PlayerController;
class World;

enum class SeaVoyagePhase : std::uint8_t {
    Moored = 0,
    Departing,
    Underway,
};

struct SeaAdventureSaveState {
    bool active = false;
    SeaVoyagePhase voyage_phase = SeaVoyagePhase::Underway;
    float voyage_phase_elapsed = 0.0F;
    glm::vec3 ship_position {0.5F, 49.0F, 0.5F};
    float route_distance = 0.0F;
    float hunger = 100.0F;
    float thirst = 100.0F;
    float stamina = 100.0F;
    float fishing_progress = 0.0F;
    float fishing_target_seconds = 0.0F;
    float survival_damage_timer = 0.0F;
    float stranded_warning_timer = 0.0F;
    std::uint32_t food_rations = 6U;
    std::uint32_t water_flasks = 5U;
    std::uint32_t fish = 0U;
    std::uint32_t wood = 0U;
    std::uint32_t stone = 0U;
    std::uint32_t fiber = 0U;
    std::int32_t stamped_ship_x = 0;
    std::int32_t stamped_ship_z = 0;
    bool has_stamped_ship = false;
    bool fishing_active = false;
    ShipCrewSaveState crew {};

    auto operator==(const SeaAdventureSaveState&) const -> bool = default;
};

[[nodiscard]] auto sanitize_sea_adventure_save_state(const SeaAdventureSaveState& state) noexcept
    -> SeaAdventureSaveState;

struct SeaAdventureFrameResult {
    bool on_ship = false;
    bool ship_moved_player = false;
    bool departure_started = false;
    bool reached_open_sea = false;
    bool fishing_started = false;
    bool fish_caught = false;
    bool fishing_failed = false;
    bool consumed_food = false;
    bool consumed_water = false;
    bool crew_fish_delivered = false;
    bool crew_water_delivered = false;
    bool stranded = false;
    bool stranded_warning = false;
    bool starving = false;
    bool dehydrating = false;
    float ship_distance = 0.0F;
    float ship_speed = 0.0F;
    glm::vec3 ship_delta {0.0F};
};

struct SeaAdventureHudState {
    bool visible = false;
    bool on_ship = false;
    bool fishing_active = false;
    bool danger = false;
    SeaVoyagePhase phase = SeaVoyagePhase::Underway;
    float departure_ratio = 1.0F;
    float departure_seconds_remaining = 0.0F;
    float hunger_ratio = 1.0F;
    float thirst_ratio = 1.0F;
    float stamina_ratio = 1.0F;
    float fishing_ratio = 0.0F;
    float ship_distance = 0.0F;
    float ship_speed = 0.0F;
    std::uint32_t food_rations = 0U;
    std::uint32_t water_flasks = 0U;
    std::uint32_t fish = 0U;
    ShipCrewFocusState crew_focus {};
};

struct ShipVoxel {
    BlockCoord local_block {};
    BlockId block_id = to_block_id(BlockType::Planks);

    auto operator==(const ShipVoxel&) const -> bool = default;
};

enum class ShipMaterial : std::uint8_t {
    DarkHull = 0,
    LightDeck,
    CleanBeam,
    CreamCanvas,
    Rope,
    Iron,
    Brass,
    Lantern,
    Glass,

    // Je conserve les valeurs historiques et j'ajoute les nouveaux matériaux
    // à la fin pour ne casser aucune donnée utilisant encore les anciens index.
    BlackCanvas,
    SolidGold,
};

enum class ShipPartShape : std::uint8_t {
    Box = 0,
    Slab,
    Stair,
    Segment,
    Panel,
    Wheel,
    Glyph,
    ClimbableNet,
};

struct ShipPart {
    ShipPartShape shape = ShipPartShape::Box;
    ShipMaterial material = ShipMaterial::DarkHull;
    // Je stocke ici l'AABB exacte des boites et les deux extremites des
    // decorations orientees afin que rendu et physique partagent un modele.
    glm::vec3 local_start {0.0F};
    glm::vec3 local_end {0.0F};
    glm::vec3 orientation {0.0F, 0.0F, 1.0F};
    float thickness = 0.0F;
    bool collidable = false;
    bool supports_player = false;
    char32_t glyph = U'\0';

    auto operator==(const ShipPart&) const -> bool = default;
};

struct ShipBounds {
    glm::vec3 min {0.0F};
    glm::vec3 max {0.0F};

    auto operator==(const ShipBounds&) const -> bool = default;
};

struct ShipClimbContact {
    // Je fournis toutes les donnees en coordonnees monde pour que le controleur
    // puisse s'accrocher au filet sans connaitre l'origine locale du navire.
    ShipBounds bounds {};
    glm::vec3 outward_normal {0.0F};
    glm::vec3 deck_exit {0.0F};
};

struct ShipAnchors {
    glm::vec3 safe_spawn {0.5F, 4.10F, -7.5F};
    glm::vec3 lower_deck {0.0F, 1.01F, -7.5F};
    glm::vec3 captain_cabin {0.0F, 1.01F, -22.0F};
    glm::vec3 crew_quarters {0.0F, 1.01F, -5.0F};
    glm::vec3 galley {0.0F, 1.01F, 5.0F};
    glm::vec3 cargo_hold {0.0F, 1.01F, 23.0F};
    glm::vec3 helm {-1.5F, 4.51F, -29.0F};
    glm::vec3 aft_hatch {0.0F, 4.01F, -7.5F};
    glm::vec3 fore_hatch {0.0F, 4.01F, 14.5F};

    auto operator==(const ShipAnchors&) const -> bool = default;
};

struct ShipBlueprint {
    std::string_view name {};
    std::span<const ShipPart> parts {};
    std::span<const ShipCrewNavigationNode> crew_navigation_nodes {};
    std::span<const ShipCrewNavigationEdge> crew_navigation_edges {};
    std::span<const glm::vec3> interior_lanterns {};
    ShipBounds bounds {};
    ShipAnchors anchors {};
    std::uint64_t geometry_revision = 0U;
    std::uint64_t navigation_revision = 0U;
};

[[nodiscard]] auto amelie_ship_blueprint() noexcept -> const ShipBlueprint&;
[[nodiscard]] auto legacy_ship_voxel_count() noexcept -> std::size_t;
[[nodiscard]] auto legacy_ship_blueprint_checksum() noexcept -> std::uint64_t;

struct ShipRenderState {
    bool visible = false;
    glm::vec3 world_origin {0.0F};
    const ShipBlueprint* blueprint = nullptr;
    std::span<const ShipPart> parts {};
    ShipBounds local_bounds {};
    std::uint64_t geometry_revision = 0U;
};

struct LegacyShipMigrationStats {
    std::size_t processed_cells = 0;
    std::size_t restored_cells = 0;
    std::size_t pending_cells = 0;
    float progress = 1.0F;
};

class ShipEntity {
public:
    void set_position(const glm::vec3& position) noexcept;
    void set_velocity(const glm::vec3& velocity) noexcept;

    [[nodiscard]] auto position() const noexcept -> const glm::vec3&;
    [[nodiscard]] auto velocity() const noexcept -> const glm::vec3&;
    [[nodiscard]] auto world_origin() const noexcept -> glm::vec3;
    [[nodiscard]] auto render_state(bool visible) const noexcept -> ShipRenderState;
    [[nodiscard]] auto support_height(const glm::vec3& feet_position) const noexcept -> std::optional<float>;
    [[nodiscard]] auto support_height_in_range(const glm::vec3& feet_position,
                                               float min_height,
                                               float max_height) const noexcept -> std::optional<float>;
    [[nodiscard]] auto climb_contact(const glm::vec3& min_corner,
                                     const glm::vec3& max_corner) const noexcept
        -> std::optional<ShipClimbContact>;
    [[nodiscard]] auto intersects_aabb(const glm::vec3& min_corner, const glm::vec3& max_corner) const noexcept -> bool;
    [[nodiscard]] auto raycast_collidable_distance(const glm::vec3& origin,
                                                   const glm::vec3& direction,
                                                   float max_distance) const noexcept -> std::optional<float>;

private:
    glm::vec3 position_ {0.5F, 49.0F, 0.5F};
    glm::vec3 velocity_ {0.0F};
};

struct ShipOccupantReconciliation {
    glm::vec3 position {0.0F};
    bool relocated = false;
};

enum class ShipInvalidPositionPolicy : std::uint8_t {
    Relocate = 0,
    Preserve,
};

[[nodiscard]] auto reconcile_loaded_ship_occupant(const ShipEntity& ship,
                                                   const glm::vec3& saved_position,
                                                   float half_width,
                                                   float height,
                                                   bool legacy_layout_present,
                                                   ShipInvalidPositionPolicy invalid_position_policy =
                                                       ShipInvalidPositionPolicy::Relocate,
                                                   std::optional<glm::vec3> legacy_world_origin = std::nullopt) noexcept
    -> ShipOccupantReconciliation;

class SeaAdventureSystem {
public:
    void reset(int seed) noexcept;
    void load_state(const SeaAdventureSaveState& state, int world_seed = 1337) noexcept;
    [[nodiscard]] auto save_state() const noexcept -> const SeaAdventureSaveState&;
    [[nodiscard]] auto active() const noexcept -> bool;
    [[nodiscard]] auto ship_position() const noexcept -> glm::vec3;
    [[nodiscard]] auto deck_spawn_position() const noexcept -> glm::vec3;
    [[nodiscard]] auto ship_entity() const noexcept -> const ShipEntity&;
    [[nodiscard]] auto ship_render_state() const noexcept -> ShipRenderState;
    [[nodiscard]] auto crew_render_instances() const noexcept -> std::span<const CrewRenderInstance>;
    [[nodiscard]] auto crew_members() const noexcept -> std::span<const ShipCrewMemberSaveState>;
    [[nodiscard]] auto hud_state(const PlayerController& player) const noexcept -> SeaAdventureHudState;

    // Je conserve ce point d'entree pour migrer les anciennes sauvegardes qui
    // contenaient encore le navire grave dans les chunks.
    void stamp_ship(World& world);
    void begin_legacy_ship_migration(World& world);
    [[nodiscard]] auto migrate_legacy_ship_step(World& world, std::size_t cell_budget, double max_ms)
        -> LegacyShipMigrationStats;
    [[nodiscard]] auto has_pending_legacy_ship_migration() const noexcept -> bool;
    [[nodiscard]] auto legacy_ship_migration_progress() const noexcept -> float;
    [[nodiscard]] auto update(World& world,
                              PlayerController& player,
                              const EnvironmentState& environment,
                              float dt,
                              bool request_fishing) -> SeaAdventureFrameResult;

    [[nodiscard]] auto collect_resource(BlockId block_id) noexcept -> bool;
    [[nodiscard]] auto record_hunt(CreatureSpecies species) noexcept -> bool;
    [[nodiscard]] auto try_damage_crew(const glm::vec3& origin,
                                       const glm::vec3& direction,
                                       float max_distance,
                                       float damage) noexcept -> ShipCrewDamageResult;
    void cancel_fishing() noexcept;

    // Le respawn conserve la progression du voyage et les stocks, mais retire
    // les etats transitoires capables de provoquer une nouvelle mort immediate.
    void on_player_respawn() noexcept;

private:
    [[nodiscard]] auto player_should_ride_ship(const PlayerController& player) const noexcept -> bool;
    [[nodiscard]] auto player_on_ship(const glm::vec3& player_position) const noexcept -> bool;
    [[nodiscard]] auto player_ship_distance(const glm::vec3& player_position) const noexcept -> float;
    void consume_automatic_supplies(SeaAdventureFrameResult& result) noexcept;
    struct LegacyShipMigrationState {
        int origin_x = 0;
        int origin_z = 0;
        std::size_t next_voxel = 0;
        std::size_t restored_cells = 0;
    };

    SeaAdventureSaveState state_ {};
    ShipEntity ship_ {};
    ShipCrewSystem crew_ {};
    // Je conserve les fractions de deplacement en double : au-dela de 524 km,
    // un float ne peut plus representer chaque petite avance d'une frame.
    double precise_ship_position_z_ = 0.5;
    double precise_route_distance_ = 0.0;
    std::uint32_t route_seed_ = 0U;
    std::optional<LegacyShipMigrationState> legacy_ship_migration_ {};
};

} // namespace valcraft
