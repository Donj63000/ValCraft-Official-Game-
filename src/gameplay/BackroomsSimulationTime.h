#pragma once

#include <algorithm>
#include <cmath>

namespace valcraft {

inline constexpr float kBackroomsMaximumSimulationDeltaSeconds = 0.10F;

struct BackroomsFrameTime {
    float real_delta_seconds = 0.0F;
    float simulation_delta_seconds = 0.0F;
    bool simulation_frozen = false;
};

struct BackroomsGameplayBlockers {
    bool death_screen = false;
    bool pause_menu = false;
    bool inventory = false;
    bool progression = false;
    bool command_console = false;
    bool confirmation_dialog = false;
    bool front_end = false;
};

[[nodiscard]] inline constexpr auto backrooms_gameplay_interaction_blocked(
    const BackroomsGameplayBlockers& blockers) noexcept -> bool {
    return blockers.death_screen ||
           blockers.pause_menu ||
           blockers.inventory ||
           blockers.progression ||
           blockers.command_console ||
           blockers.confirmation_dialog ||
           blockers.front_end;
}

struct BackroomsResumeState {
    bool simulation_was_frozen = false;
    bool suppress_jump_until_release = false;
};

struct BackroomsResumeDecision {
    bool synchronize_latches = false;
    bool suppress_jump = false;
};

// Je traite la sortie du front-end comme une reprise, meme si le chargement a
// eu lieu sans tick fixe intermediaire. Le smoke conserve son pilotage direct.
[[nodiscard]] inline constexpr auto initialize_backrooms_resume_state(
    bool smoke_test) noexcept -> BackroomsResumeState {
    return {
        .simulation_was_frozen = !smoke_test,
        .suppress_jump_until_release = false,
    };
}

// Je marque la transition au moment exact ou l'interface s'ouvre. Ainsi, meme
// deux evenements open/close consommes dans le meme lot SDL restent observables.
inline constexpr void note_backrooms_interaction_boundary(
    BackroomsResumeState& state,
    bool backrooms_active,
    bool smoke_test) noexcept {
    if (backrooms_active && !smoke_test) {
        state.simulation_was_frozen = true;
    }
}

// Je memorise la frontiere de reprise independamment des evenements SDL : une
// touche Espace qui ferme un menu ne doit devenir ni un saut, ni un evenement IA.
[[nodiscard]] inline constexpr auto advance_backrooms_resume_state(
    BackroomsResumeState& state,
    bool simulation_frozen,
    bool jump_held) noexcept -> BackroomsResumeDecision {
    if (simulation_frozen) {
        state.simulation_was_frozen = true;
        state.suppress_jump_until_release = jump_held;
        return {
            .synchronize_latches = true,
            .suppress_jump = state.suppress_jump_until_release,
        };
    }

    const auto resumed = state.simulation_was_frozen;
    state.simulation_was_frozen = false;
    if (!jump_held) {
        state.suppress_jump_until_release = false;
    } else if (resumed) {
        state.suppress_jump_until_release = true;
    }

    return {
        .synchronize_latches = resumed,
        .suppress_jump = state.suppress_jump_until_release,
    };
}

// Je reserve l'exception aux scenes smoke automatisees : leurs overlays font
// partie du scenario de QA et ne representent pas une pause demandee au jeu.
[[nodiscard]] inline constexpr auto backrooms_ui_freezes_simulation(
    bool smoke_test,
    bool gameplay_interaction_blocked) noexcept -> bool {
    return !smoke_test && gameplay_interaction_blocked;
}

// Je separe explicitement le temps de la frame du temps de gameplay : les
// interfaces peuvent continuer a vivre sans faire avancer le monde Backrooms.
[[nodiscard]] inline auto resolve_backrooms_frame_time(
    float frame_delta_seconds,
    bool simulation_frozen) noexcept -> BackroomsFrameTime {
    const auto real_delta_seconds =
        std::isfinite(frame_delta_seconds)
            ? std::clamp(
                  frame_delta_seconds,
                  0.0F,
                  kBackroomsMaximumSimulationDeltaSeconds)
            : 0.0F;
    return {
        .real_delta_seconds = real_delta_seconds,
        .simulation_delta_seconds =
            simulation_frozen ? 0.0F : real_delta_seconds,
        .simulation_frozen = simulation_frozen,
    };
}

} // namespace valcraft
