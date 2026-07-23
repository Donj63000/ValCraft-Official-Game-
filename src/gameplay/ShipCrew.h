#pragma once

#include "creatures/CreatureGeometry.h"
#include "world/Environment.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace valcraft {

inline constexpr std::size_t kShipCrewMemberCount = 6U;
inline constexpr std::uint32_t kAutomaticFishTarget = 12U;
inline constexpr std::uint32_t kAutomaticWaterTarget = 10U;
inline constexpr float kAutomaticFishWorkSeconds = 180.0F;
inline constexpr float kAutomaticWaterWorkSeconds = 240.0F;
inline constexpr float kShipCrewKnockoutSeconds = 30.0F;

enum class ShipCrewRole : std::uint8_t {
    Captain = 0,
    Fisher = 1,
    Rigger = 2,
    WaterTender = 3,
    Deckhand = 4,
    Quartermaster = 5,
};

enum class ShipCrewActivity : std::uint8_t {
    Idle = 0,
    Steer = 1,
    Inspect = 2,
    Fish = 3,
    TendWater = 4,
    Carry = 5,
    HaulRope = 6,
    Scrub = 7,
    TurnCapstan = 8,
    SortCargo = 9,
    Socialize = 10,
    Rest = 11,
};

enum class ShipCrewCargo : std::uint8_t {
    None = 0,
    Fish = 1,
    Water = 2,
};

// Je garde ces identifiants stables et append-only : une sauvegarde ne depend
// jamais d'un index accidentel dans le tableau des points de navigation.
enum class ShipCrewStation : std::uint8_t {
    Helm = 0,
    ChartTable = 1,
    CaptainCabin = 2,
    AftWatch = 3,
    PortFishing = 4,
    StarboardFishing = 5,
    MainMast = 6,
    ForeMast = 7,
    MizzenMast = 8,
    WaterStill = 9,
    Galley = 10,
    Capstan = 11,
    AftDeck = 12,
    MidDeckPort = 13,
    MidDeckStarboard = 14,
    ForeDeck = 15,
    AftStairsTop = 16,
    AftStairsMid = 17,
    AftStairsBottom = 18,
    ForeStairsTop = 19,
    ForeStairsMid = 20,
    ForeStairsBottom = 21,
    CargoFish = 22,
    CargoWater = 23,
    CargoSort = 24,
    CrewBunks = 25,
    MessTable = 26,
    ForeHatchPortA = 27,
    ForeHatchPortB = 28,
    HelmBypassPort = 29,
    QuarterdeckStepTop = 30,
    QuarterdeckStepBottom = 31,
    ForecastleStepBottom = 32,
    ForecastleStepTop = 33,
    ForeStairsExitCenter = 34,
    ForeStairsExitPort = 35,
    AftCabinDoor = 36,
    AftLowerPortA = 37,
    AftLowerPortB = 38,
    ForeLowerPortA = 39,
    ForeLowerPortB = 40,
    WaterStillApproach = 41,
    Count = 42,
};

struct ShipCrewNavigationNode {
    ShipCrewStation station = ShipCrewStation::AftDeck;
    glm::vec3 local_position {0.0F};
    bool exterior = true;

    auto operator==(const ShipCrewNavigationNode&) const -> bool = default;
};

struct ShipCrewNavigationEdge {
    ShipCrewStation first = ShipCrewStation::AftDeck;
    ShipCrewStation second = ShipCrewStation::AftDeck;

    auto operator==(const ShipCrewNavigationEdge&) const -> bool = default;
};

struct ShipCrewMemberSaveState {
    glm::vec3 local_position {0.0F};
    float yaw_radians = 0.0F;
    float animation_time = 0.0F;
    float activity_timer = 0.0F;
    float work_progress = 0.0F;
    float health = 14.0F;
    float recovery_timer = 0.0F;
    float hurt_timer = 0.0F;
    std::uint8_t id = 0U;
    std::uint8_t routine_step = 0U;
    ShipCrewRole role = ShipCrewRole::Deckhand;
    ShipCrewActivity activity = ShipCrewActivity::Idle;
    ShipCrewCargo cargo = ShipCrewCargo::None;
    ShipCrewStation current_station = ShipCrewStation::AftDeck;
    ShipCrewStation next_station = ShipCrewStation::AftDeck;
    ShipCrewStation destination_station = ShipCrewStation::AftDeck;

    auto operator==(const ShipCrewMemberSaveState&) const -> bool = default;
};

struct ShipCrewSaveState {
    std::array<ShipCrewMemberSaveState, kShipCrewMemberCount> members {};
    std::uint64_t navigation_revision = 0U;
    bool initialized = false;

    auto operator==(const ShipCrewSaveState&) const -> bool = default;
};

[[nodiscard]] auto sanitize_ship_crew_save_state(const ShipCrewSaveState& state) noexcept
    -> ShipCrewSaveState;
[[nodiscard]] auto ship_crew_max_health(ShipCrewRole role) noexcept -> float;

// Ces libelles sont centralises ici afin que le gameplay et le HUD decrivent
// toujours une meme activite avec les memes termes.
[[nodiscard]] auto ship_crew_role_label(ShipCrewRole role) noexcept -> std::string_view;
[[nodiscard]] auto ship_crew_activity_label(ShipCrewActivity activity) noexcept -> std::string_view;
[[nodiscard]] auto ship_crew_cargo_label(ShipCrewCargo cargo) noexcept -> std::string_view;
[[nodiscard]] auto ship_crew_station_label(ShipCrewStation station) noexcept -> std::string_view;

struct ShipCrewUpdateResult {
    bool fish_delivered = false;
    bool water_delivered = false;
};

struct ShipCrewDamageResult {
    bool hit = false;
    bool knocked_out = false;
    std::uint8_t member_id = 0U;
    glm::vec3 position {0.0F};
    float damage = 0.0F;
    float remaining_health = 0.0F;
    float distance = 0.0F;
};

// Vue purement runtime du marin actuellement vise. Elle ne fait volontairement
// pas partie de la sauvegarde : le viseur et les temporisations de blocage ne
// doivent jamais modifier le format v9 des parties existantes.
struct ShipCrewFocusState {
    bool visible = false;
    bool moving = false;
    bool blocked = false;
    bool has_progress = false;
    bool knocked_out = false;
    std::uint8_t member_id = 0U;
    ShipCrewRole role = ShipCrewRole::Deckhand;
    ShipCrewActivity activity = ShipCrewActivity::Idle;
    ShipCrewCargo cargo = ShipCrewCargo::None;
    ShipCrewStation destination_station = ShipCrewStation::AftDeck;
    float progress_ratio = 0.0F;
    float health_ratio = 1.0F;
    float distance = 0.0F;
};

class ShipEntity;
struct ShipBlueprint;

class ShipCrewSystem {
public:
    void reset(int world_seed, const ShipEntity& ship) noexcept;
    void load_state(const ShipCrewSaveState& state, int world_seed, const ShipEntity& ship) noexcept;

    [[nodiscard]] auto update(const ShipEntity& ship,
                              const EnvironmentState& environment,
                              float dt,
                              std::uint32_t& fish,
                              std::uint32_t& water_flasks,
                              std::optional<glm::vec3> player_world_position = std::nullopt) noexcept
        -> ShipCrewUpdateResult;
    [[nodiscard]] auto try_damage_from_player(const ShipEntity& ship,
                                               const glm::vec3& origin,
                                               const glm::vec3& direction,
                                               float max_distance,
                                               float damage) noexcept -> ShipCrewDamageResult;
    [[nodiscard]] auto focus_from_ray(const ShipEntity& ship,
                                      const glm::vec3& origin,
                                      const glm::vec3& direction,
                                      float max_distance) const noexcept -> ShipCrewFocusState;

    [[nodiscard]] auto save_state() const noexcept -> const ShipCrewSaveState&;
    [[nodiscard]] auto members() const noexcept -> std::span<const ShipCrewMemberSaveState>;
    [[nodiscard]] auto render_instances() const noexcept -> std::span<const CrewRenderInstance>;

private:
    struct MemberRuntime {
        // La vitesse et la distance parcourue pilotent la locomotion visuelle :
        // un marin arrete ne continue plus a faire glisser ses pieds.
        float current_speed = 0.0F;
        float locomotion_distance = 0.0F;
        float motion_amount = 0.0F;
        // Je reserve cette phase aux animations de tache et de recuperation ;
        // la locomotion est derivee separement de la distance parcourue.
        float activity_phase = 0.0F;
        float recover_timer = 0.0F;
        float blocked_timer = 0.0F;
        glm::vec3 visual_offset {0.0F};
        bool blocked = false;
    };

    void initialize_canonical_roster(int world_seed, const ShipEntity& ship) noexcept;
    void restore_runtime_routes(const ShipEntity& ship) noexcept;
    void rebuild_render_instances(const ShipEntity& ship, const EnvironmentState& environment) noexcept;

    ShipCrewSaveState state_ {};
    std::array<MemberRuntime, kShipCrewMemberCount> runtime_ {};
    std::array<CrewRenderInstance, kShipCrewMemberCount> render_instances_ {};
    std::uint32_t appearance_seed_ = 0U;
    int world_seed_ = 1337;
};

} // namespace valcraft
