#pragma once

#include "app/Hotbar.h"
#include "creatures/CreatureTypes.h"
#include "gameplay/OldGuard.h"
#include "gameplay/ShipCrew.h"
#include "world/Block.h"
#include "world/Environment.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace valcraft {

class PlayerController;
class CreatureSystem;
class World;
struct ItemDrop;
struct PlayerState;

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
    OldGuardSaveState old_guard {};

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
    OldGuardFocusState old_guard_focus {};
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

struct ShipProtectionProfile {
    float stern_z = 0.0F;
    float bow_z = 0.0F;
    float maximum_half_width = 0.0F;
    float stern_width_loss = 0.0F;
    float bow_width_loss = 0.0F;
    float stern_taper_exponent = 1.0F;
    float bow_taper_exponent = 1.0F;
    float lower_hull_min_y = 0.0F;
    float middle_hull_min_y = 0.0F;
    float upper_hull_min_y = 0.0F;
    float main_deck_top_y = 0.0F;
    float lower_width_inset = 0.0F;
    float middle_width_inset = 0.0F;
    float lower_minimum_half_width = 0.0F;
    float middle_minimum_half_width = 0.0F;
    float boundary_margin = 0.0F;
    float sheltered_floor_y = 0.0F;

    // Je centralise la silhouette et les volumes proteges afin que la
    // geometrie, la physique et le rendu ne puissent pas diverger.
    [[nodiscard]] auto half_width_at(float local_z) const noexcept -> float;
    [[nodiscard]] auto excludes_ocean_local(const glm::vec3& local_point) const noexcept -> bool;
    [[nodiscard]] auto shelters_from_weather_local(const glm::vec3& local_point) const noexcept -> bool;

    auto operator==(const ShipProtectionProfile&) const -> bool = default;
};

struct ShipClimbContact {
    // L'AABB monde reste pratique pour les diagnostics et les tests historiques.
    // Les donnees locales et la base orientee permettent au controleur de suivre
    // exactement un filet incline par le tangage et le roulis du navire.
    ShipBounds bounds {};
    ShipBounds local_bounds {};
    glm::vec3 outward_normal {0.0F};
    glm::vec3 plane_point {0.0F};
    glm::vec3 up_direction {0.0F, 1.0F, 0.0F};
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
    ShipProtectionProfile protection_profile {};
    std::uint64_t geometry_revision = 0U;
    std::uint64_t navigation_revision = 0U;
};

[[nodiscard]] auto amelie_ship_blueprint() noexcept -> const ShipBlueprint&;
[[nodiscard]] auto legacy_ship_voxel_count() noexcept -> std::size_t;
[[nodiscard]] auto legacy_ship_blueprint_checksum() noexcept -> std::uint64_t;

struct ShipRenderState {
    bool visible = false;
    glm::vec3 world_origin {0.0F};
    glm::mat4 model_matrix {1.0F};
    const ShipBlueprint* blueprint = nullptr;
    std::span<const ShipPart> parts {};
    ShipBounds local_bounds {};
    ShipBounds world_bounds {};
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

    // La pose precedente est capturee une seule fois avant chaque pas physique.
    // Tous les occupants utilisent ensuite la meme transformation rigide.
    void begin_motion_step() noexcept;
    void synchronize_motion_history() noexcept;
    void set_ocean_pose(float heave,
                        float pitch_radians,
                        float roll_radians) noexcept;

    [[nodiscard]] auto position() const noexcept -> const glm::vec3&;
    [[nodiscard]] auto velocity() const noexcept -> const glm::vec3&;
    [[nodiscard]] auto orientation() const noexcept -> const glm::quat&;
    [[nodiscard]] auto world_origin() const noexcept -> glm::vec3;
    [[nodiscard]] auto previous_world_origin() const noexcept -> glm::vec3;
    [[nodiscard]] auto model_matrix() const noexcept -> glm::mat4;
    [[nodiscard]] auto previous_model_matrix() const noexcept -> glm::mat4;
    [[nodiscard]] auto world_bounds() const noexcept -> ShipBounds;

    [[nodiscard]] auto local_to_world_point(const glm::vec3& local_point) const noexcept -> glm::vec3;
    [[nodiscard]] auto world_to_local_point(const glm::vec3& world_point) const noexcept -> glm::vec3;
    [[nodiscard]] auto world_point_in_persisted_neutral_pose(
        const glm::vec3& current_world_point) const noexcept -> glm::vec3;
    [[nodiscard]] auto local_to_world_direction(const glm::vec3& local_direction) const noexcept -> glm::vec3;
    [[nodiscard]] auto world_to_local_direction(const glm::vec3& world_direction) const noexcept -> glm::vec3;
    [[nodiscard]] auto motion_delta_at(const glm::vec3& previous_world_point) const noexcept -> glm::vec3;
    [[nodiscard]] auto excludes_ocean_at(const glm::vec3& world_point) const noexcept -> bool;
    [[nodiscard]] auto is_weather_sheltered_at(const glm::vec3& world_point) const noexcept -> bool;

    [[nodiscard]] auto render_state(bool visible) const noexcept -> ShipRenderState;
    [[nodiscard]] auto support_height(const glm::vec3& feet_position) const noexcept -> std::optional<float>;
    [[nodiscard]] auto previous_support_height(const glm::vec3& feet_position) const noexcept
        -> std::optional<float>;
    [[nodiscard]] auto support_height_in_range(const glm::vec3& feet_position,
                                               float min_height,
                                               float max_height) const noexcept -> std::optional<float>;
    [[nodiscard]] auto climb_contact(const glm::vec3& min_corner,
                                     const glm::vec3& max_corner) const noexcept
        -> std::optional<ShipClimbContact>;
    [[nodiscard]] auto intersects_aabb(const glm::vec3& min_corner,
                                       const glm::vec3& max_corner) const noexcept -> bool;
    [[nodiscard]] auto raycast_collidable_distance(const glm::vec3& origin,
                                                   const glm::vec3& direction,
                                                   float max_distance) const noexcept -> std::optional<float>;

private:
    glm::vec3 position_ {0.5F, 49.0F, 0.5F};
    glm::vec3 velocity_ {0.0F};
    float ocean_heave_ = 0.0F;
    glm::quat orientation_ {1.0F, 0.0F, 0.0F, 0.0F};

    glm::vec3 previous_position_ {0.5F, 49.0F, 0.5F};
    float previous_ocean_heave_ = 0.0F;
    glm::quat previous_orientation_ {1.0F, 0.0F, 0.0F, 0.0F};
    bool motion_history_valid_ = false;
};

// Je normalise uniquement les occupants dont les pieds reposent reellement
// sur le navire afin de conserver sans alteration les nageurs et les objets en vol.
[[nodiscard]] auto normalize_supported_player_for_ship_save(
    const ShipEntity& ship,
    PlayerState& player_state,
    bool player_is_climbing_ship) noexcept -> bool;
[[nodiscard]] auto normalize_supported_item_drop_for_ship_save(
    const ShipEntity& ship,
    ItemDrop& drop) noexcept -> bool;

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
    [[nodiscard]] auto old_guard_render_instances() const noexcept
        -> std::span<const OldGuardRenderInstance>;
    [[nodiscard]] auto old_guard_members() const noexcept
        -> std::span<const OldGuardMemberSaveState>;
    [[nodiscard]] auto old_guard_flashes() const noexcept
        -> std::span<const OldGuardMuzzleFlashInstance>;
    [[nodiscard]] auto old_guard_smoke() const noexcept
        -> std::span<const OldGuardSmokeInstance>;
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
    [[nodiscard]] auto update_old_guard_combat(World& world,
                                               CreatureSystem& creatures,
                                               const PlayerController& player,
                                               const EnvironmentState& environment,
                                               float dt) -> const OldGuardFrameEvents&;

    [[nodiscard]] auto collect_resource(BlockId block_id) noexcept -> bool;
    [[nodiscard]] auto record_hunt(CreatureSpecies species) noexcept -> bool;
    [[nodiscard]] auto try_damage_crew(const glm::vec3& origin,
                                       const glm::vec3& direction,
                                       float max_distance,
                                       float damage) noexcept -> ShipCrewDamageResult;
    [[nodiscard]] auto raycast_crew(const glm::vec3& origin,
                                    const glm::vec3& direction,
                                    float max_distance) const noexcept -> ShipCrewRayHit;
    [[nodiscard]] auto apply_damage_crew(std::uint8_t member_id,
                                         float damage,
                                         float hit_distance) noexcept -> ShipCrewDamageResult;
    [[nodiscard]] auto intercept_old_guard(const glm::vec3& origin,
                                           const glm::vec3& direction,
                                           float max_distance) const noexcept -> OldGuardRayHit;
    void cancel_fishing() noexcept;

    // Le respawn conserve la progression du voyage et les stocks, mais retire
    // les etats transitoires capables de provoquer une nouvelle mort immediate.
    void on_player_respawn() noexcept;

private:
    [[nodiscard]] auto player_should_ride_ship(const PlayerController& player) const noexcept -> bool;
    [[nodiscard]] auto player_on_ship(const glm::vec3& player_position) const noexcept -> bool;
    [[nodiscard]] auto player_ship_distance(const glm::vec3& player_position) const noexcept -> float;
    void consume_automatic_supplies(bool player_on_ship,
                                    SeaAdventureFrameResult& result) noexcept;
    struct LegacyShipMigrationState {
        int origin_x = 0;
        int origin_z = 0;
        std::size_t next_voxel = 0;
        std::size_t restored_cells = 0;
    };

    SeaAdventureSaveState state_ {};
    ShipEntity ship_ {};
    ShipCrewSystem crew_ {};
    OldGuardSystem old_guard_ {};
    // Je conserve les fractions de deplacement en double : au-dela de 524 km,
    // un float ne peut plus representer chaque petite avance d'une frame.
    double precise_ship_position_z_ = 0.5;
    double precise_route_distance_ = 0.0;
    std::uint32_t route_seed_ = 0U;
    std::optional<LegacyShipMigrationState> legacy_ship_migration_ {};

    // Ces valeurs sont volontairement transitoires : elles sont reconstruites
    // depuis la meteo et ne modifient pas le format des sauvegardes existantes.
    float ocean_heave_ = 0.0F;
    float ocean_heave_velocity_ = 0.0F;
    float ocean_pitch_ = 0.0F;
    float ocean_pitch_velocity_ = 0.0F;
    float ocean_roll_ = 0.0F;
    float ocean_roll_velocity_ = 0.0F;
};

} // namespace valcraft
