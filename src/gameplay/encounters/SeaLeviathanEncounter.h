#pragma once

#include "creatures/legendary/LegendaryEnemySystem.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace valcraft {

inline constexpr std::size_t kSeaLeviathanTentacleCount = 4U;
inline constexpr std::size_t kMaximumSeaLeviathanEvents = 96U;
inline constexpr float kSeaLeviathanMinimumHealth = 1'000.0F;
inline constexpr float kSeaLeviathanMaximumHealth = 2'500.0F;

struct ShipLocalFrame {
    glm::vec3 origin {};
    glm::vec3 right {1.0F, 0.0F, 0.0F};
    glm::vec3 up {0.0F, 1.0F, 0.0F};
    glm::vec3 forward {0.0F, 0.0F, 1.0F};

    auto operator==(const ShipLocalFrame&) const -> bool = default;
};

[[nodiscard]] auto valid_ship_local_frame(
    const ShipLocalFrame& frame) noexcept -> bool;
[[nodiscard]] auto ship_local_to_world(
    const ShipLocalFrame& frame,
    const glm::vec3& local_position) noexcept
    -> std::optional<glm::vec3>;
[[nodiscard]] auto ship_world_to_local(
    const ShipLocalFrame& frame,
    const glm::vec3& world_position) noexcept
    -> std::optional<glm::vec3>;

enum class SeaLeviathanPhase : std::uint8_t {
    Dormant = 0,
    Emerging,
    CarapaceAssault,
    GuardWindow,
    ChargedOpening,
    ExposedCore,
    Frenzy,
    Defeated,
};

enum class SeaLeviathanPart : std::uint8_t {
    Core = 0,
    Carapace,
    Tentacle0,
    Tentacle1,
    Tentacle2,
    Tentacle3,
};

enum class SeaLeviathanAttack : std::uint8_t {
    None = 0,
    TentacleSweep,
    DeckSmash,
};

enum class SeaLeviathanEventKind : std::uint8_t {
    EncounterStarted = 0,
    Emerged,
    PhaseChanged,
    TentacleTelegraphed,
    TentacleStrike,
    DeckStrikeTelegraphed,
    GuardWindowOpened,
    DeckStrikeGuarded,
    PerfectGuard,
    PlayerHit,
    ChargedOpeningRequested,
    CoreExposed,
    CoreClosed,
    CarapaceBroken,
    TentacleSevered,
    AstralCoreDeflected,
    MonsterDamaged,
    Defeated,
};

struct SeaLeviathanDamageDirective {
    float player_damage = 0.0F;
    float player_stability_damage = 0.0F;
    float ship_damage = 0.0F;
    float allied_damage = 0.0F;

    auto operator==(const SeaLeviathanDamageDirective&) const
        -> bool = default;
};

struct SeaLeviathanEvent {
    SeaLeviathanEventKind kind =
        SeaLeviathanEventKind::EncounterStarted;
    SeaLeviathanPhase phase = SeaLeviathanPhase::Dormant;
    SeaLeviathanPart part = SeaLeviathanPart::Core;
    std::uint64_t simulation_tick = 0U;
    glm::vec3 ship_local_position {};
    float amount = 0.0F;
    SeaLeviathanDamageDirective damage {};
};

struct SeaLeviathanStartRequest {
    std::uint32_t deterministic_seed = 0U;
    glm::vec3 body_anchor_ship_local {0.0F, -1.5F, 7.0F};
};

enum class SeaLeviathanStartError : std::uint8_t {
    None = 0,
    AlreadyActive,
    InvalidAnchor,
};

struct SeaLeviathanStartResult {
    bool started = false;
    SeaLeviathanStartError error = SeaLeviathanStartError::None;
    float maximum_health = 0.0F;
};

struct SeaLeviathanUpdateInput {
    ShipLocalFrame ship_frame {};
    glm::vec3 player_world_position {};
    bool player_alive = true;
    bool guarding_with_legendary_weapon = false;
    bool perfect_guard_active = false;
};

struct SeaLeviathanUpdateResult {
    bool accepted = false;
    std::uint64_t advanced_ticks = 0U;
    std::size_t dropped_event_count = 0U;
};

struct SeaLeviathanHitRequest {
    SeaLeviathanPart part = SeaLeviathanPart::Core;
    float physical_damage = 0.0F;
    float stagger_power = 0.0F;
    bool charged_attack = false;
    bool sectioning_attack = false;
    std::uint8_t weapon_awakening_level = 0U;
};

enum class SeaLeviathanHitError : std::uint8_t {
    None = 0,
    Inactive,
    Defeated,
    InvalidPart,
    InvalidDamage,
    InvalidStagger,
    PartAlreadySevered,
};

struct SeaLeviathanHitResult {
    bool accepted = false;
    SeaLeviathanHitError error =
        SeaLeviathanHitError::None;
    float applied_health_damage = 0.0F;
    float applied_local_damage = 0.0F;
    float applied_stagger = 0.0F;
    float remaining_health = 0.0F;
    float remaining_local_resistance = 0.0F;
    bool core_deflected = false;
    bool core_exposed_now = false;
    bool carapace_broken_now = false;
    bool tentacle_severed_now = false;
    bool defeated_now = false;
};

struct SeaTentacleRenderSnapshot {
    SeaLeviathanPart part = SeaLeviathanPart::Tentacle0;
    glm::vec3 anchor_ship_local {};
    glm::vec3 anchor_world {};
    float resistance_ratio = 0.0F;
    bool severed = false;
    bool attacking = false;
};

struct SeaLeviathanRenderSnapshot {
    SeaLeviathanPhase phase = SeaLeviathanPhase::Dormant;
    SeaLeviathanAttack active_attack = SeaLeviathanAttack::None;
    glm::vec3 body_anchor_ship_local {};
    glm::vec3 body_anchor_world {};
    glm::vec3 core_world {};
    glm::vec3 telegraph_world {};
    float health_ratio = 0.0F;
    float carapace_ratio = 0.0F;
    float stagger_ratio = 0.0F;
    float core_exposure_ratio = 0.0F;
    bool core_exposed = false;
    bool active = false;
    std::array<SeaTentacleRenderSnapshot, kSeaLeviathanTentacleCount>
        tentacles {};
};

struct SeaLeviathanHitVolume {
    SeaLeviathanPart part = SeaLeviathanPart::Core;
    glm::vec3 center_world {};
    float radius = 0.0F;
    bool enabled = false;
    bool sectionable = false;
};

struct SeaLeviathanCombatSnapshot {
    std::array<SeaLeviathanHitVolume, 6U> hit_volumes {};
    std::size_t hit_volume_count = 0U;
    EntityWeight weight = EntityWeight::Boss;
    Faction faction = Faction::Hostile;
    bool can_damage_ship = false;
    bool can_damage_allies = false;
};

class SeaLeviathanEncounter {
public:
    [[nodiscard]] auto start(
        const SeaLeviathanStartRequest& request) noexcept
        -> SeaLeviathanStartResult;
    [[nodiscard]] auto update(
        float dt,
        const SeaLeviathanUpdateInput& input) noexcept
        -> SeaLeviathanUpdateResult;
    [[nodiscard]] auto apply_hit(
        const SeaLeviathanHitRequest& request) noexcept
        -> SeaLeviathanHitResult;

    [[nodiscard]] auto render_snapshot(
        const ShipLocalFrame& frame) const noexcept
        -> std::optional<SeaLeviathanRenderSnapshot>;
    [[nodiscard]] auto combat_snapshot(
        const ShipLocalFrame& frame) const noexcept
        -> std::optional<SeaLeviathanCombatSnapshot>;
    [[nodiscard]] auto consume_events(
        std::span<SeaLeviathanEvent> output) noexcept
        -> std::size_t;

    [[nodiscard]] auto phase() const noexcept -> SeaLeviathanPhase;
    [[nodiscard]] auto health() const noexcept -> float;
    [[nodiscard]] auto maximum_health() const noexcept -> float;
    [[nodiscard]] auto active() const noexcept -> bool;
    [[nodiscard]] static constexpr auto persistence_policy() noexcept
        -> TemporaryPersistencePolicy {
        return TemporaryPersistencePolicy::NeverSaved;
    }

    void reset() noexcept;

private:
    struct TentacleState {
        float resistance = 0.0F;
        float maximum_resistance = 0.0F;
        bool severed = false;
    };

    void update_tick(
        const SeaLeviathanUpdateInput& input) noexcept;
    void begin_attack() noexcept;
    void resolve_attack(
        const SeaLeviathanUpdateInput& input) noexcept;
    void change_phase(SeaLeviathanPhase phase) noexcept;
    void expose_core() noexcept;
    void push_event(
        SeaLeviathanEventKind kind,
        SeaLeviathanPart part,
        const glm::vec3& local_position,
        float amount = 0.0F,
        SeaLeviathanDamageDirective damage = {}) noexcept;
    [[nodiscard]] auto tentacle_index(
        SeaLeviathanPart part) const noexcept
        -> std::optional<std::size_t>;
    [[nodiscard]] auto base_combat_phase() const noexcept
        -> SeaLeviathanPhase;

    SeaLeviathanPhase phase_ = SeaLeviathanPhase::Dormant;
    SeaLeviathanAttack active_attack_ =
        SeaLeviathanAttack::None;
    glm::vec3 body_anchor_local_ {0.0F, -1.5F, 7.0F};
    glm::vec3 telegraph_local_ {};
    float health_ = 0.0F;
    float maximum_health_ = 0.0F;
    float carapace_ = 0.0F;
    float maximum_carapace_ = 320.0F;
    float stagger_ = 0.0F;
    float maximum_stagger_ = 220.0F;
    std::array<TentacleState, kSeaLeviathanTentacleCount>
        tentacles_ {};
    std::uint32_t phase_ticks_remaining_ = 0U;
    std::uint32_t attack_ticks_remaining_ = 0U;
    std::uint32_t attack_cooldown_ticks_ = 0U;
    std::uint32_t attack_sequence_ = 0U;
    std::size_t attacking_tentacle_index_ = 0U;
    std::uint64_t simulation_tick_ = 0U;
    double tick_accumulator_ = 0.0;
    std::array<SeaLeviathanEvent, kMaximumSeaLeviathanEvents>
        events_ {};
    std::size_t event_count_ = 0U;
    std::size_t dropped_event_count_ = 0U;
};

} // namespace valcraft
