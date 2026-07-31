#pragma once

#include "gameplay/scenarios/IssouArenaLayout.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace valcraft {

inline constexpr float kIssouArenaCountdownSeconds = 10.0F;
inline constexpr std::size_t kIssouArenaEventCapacity = 24U;

enum class IssouArenaPhase : std::uint8_t {
    Inactive = 0,
    Arrival,
    Countdown,
    Combat,
    Victory,
    Defeat,
    ExitRequested,
};

enum class IssouGoreMode : std::uint8_t {
    Disabled = 0,
    Reduced = 1,
    Full = 2,
};

enum class IssouArenaEventKind : std::uint8_t {
    Entered = 0,
    Horn,
    CameraCrowd,
    CameraColossus,
    CameraPlayer,
    WeaponTitle,
    CountdownStarted,
    CrowdMurmur,
    ChainStrain,
    MusicStarted,
    ColossusRoar,
    ChainCrack,
    BriefSilence,
    ChainsBroken,
    CombatStarted,
    CrowdApplause,
    CrowdBoo,
    CrowdCheer,
    CrowdRoar,
    CrowdSilence,
    Victory,
    Defeat,
    Reset,
    ExitRequested,
};

enum class IssouArenaCombatEvent : std::uint8_t {
    WeaponDrawn = 0,
    AttackHit,
    AttackMissed,
    ComboFinisherHit,
    ChargedAttackHit,
    PerfectGuard,
    PlayerHit,
    ArmorBroken,
    LimbSevered,
    BossBelowQuarterHealth,
    ExecutionStarted,
    BossKilled,
    BossExecuted,
};

struct IssouArenaEvent {
    IssouArenaEventKind kind =
        IssouArenaEventKind::Entered;
    float intensity = 0.0F;
    std::uint32_t sequence = 0U;

    auto operator==(const IssouArenaEvent&) const -> bool = default;
};

struct IssouArenaCombatStatistics {
    float combat_seconds = 0.0F;
    float damage_dealt = 0.0F;
    std::uint32_t limbs_severed = 0U;
    std::uint32_t perfect_guards = 0U;
    std::uint32_t missed_attacks = 0U;
    std::uint8_t maximum_momentum = 0U;
    std::uint8_t maximum_targets_hit = 0U;
    bool executed = false;

    auto operator==(const IssouArenaCombatStatistics&) const -> bool = default;
};

struct IssouArenaState {
    IssouArenaPhase phase = IssouArenaPhase::Inactive;
    IssouGoreMode gore_mode = IssouGoreMode::Full;
    IssouArenaLayout layout {};
    IssouArenaCombatStatistics statistics {};
    float phase_seconds = 0.0F;
    float countdown_seconds = kIssouArenaCountdownSeconds;
    float crowd_excitement = 0.0F;
    std::uint32_t run_sequence = 0U;
    std::uint8_t awakening_override = 2U;
    bool chains_visible = true;
    bool colossus_invulnerable = true;
    bool tips_visible = true;

    auto operator==(const IssouArenaState&) const -> bool = default;
};

struct IssouArenaHudView {
    IssouArenaPhase phase = IssouArenaPhase::Inactive;
    float countdown_seconds = 0.0F;
    float crowd_excitement = 0.0F;
    bool visible = false;
    bool show_countdown = false;
    bool show_tips = false;
    bool show_results = false;
    IssouArenaCombatStatistics statistics {};
};

class IssouArenaScenario {
public:
    [[nodiscard]] auto enter(
        const IssouArenaLayout& layout,
        std::uint32_t run_sequence = 1U) noexcept -> bool;
    [[nodiscard]] auto reset() noexcept -> bool;
    [[nodiscard]] auto request_exit() noexcept -> bool;
    [[nodiscard]] auto skip_countdown() noexcept -> bool;
    [[nodiscard]] auto set_gore_mode(
        IssouGoreMode mode) noexcept -> bool;
    [[nodiscard]] auto set_awakening_override(
        std::uint8_t awakening) noexcept -> bool;

    void update(float dt) noexcept;
    void notify_combat_event(
        IssouArenaCombatEvent event,
        float value = 0.0F,
        std::uint8_t count = 0U) noexcept;
    void notify_player_death() noexcept;
    void acknowledge_first_successful_action() noexcept;

    [[nodiscard]] auto state() const noexcept
        -> const IssouArenaState&;
    [[nodiscard]] auto active() const noexcept -> bool;
    [[nodiscard]] auto saving_suspended() const noexcept -> bool;
    [[nodiscard]] auto permanent_rewards_allowed() const noexcept -> bool;
    [[nodiscard]] auto hud_view() const noexcept -> IssouArenaHudView;
    [[nodiscard]] auto consume_events() noexcept
        -> std::span<const IssouArenaEvent>;

private:
    void begin_countdown() noexcept;
    void begin_combat() noexcept;
    void queue_event(
        IssouArenaEventKind kind,
        float intensity = 0.0F) noexcept;
    void update_arrival(float previous_seconds) noexcept;
    void update_countdown(
        float previous_countdown) noexcept;

    IssouArenaState state_ {};
    std::array<
        IssouArenaEvent,
        kIssouArenaEventCapacity>
        events_ {};
    std::size_t event_count_ = 0U;
    std::uint32_t next_event_sequence_ = 1U;
};

} // namespace valcraft
