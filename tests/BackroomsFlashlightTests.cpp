#include "gameplay/BackroomsFlashlight.h"
#include "gameplay/BackroomsSimulationTime.h"
#include "render/Renderer.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace valcraft {

TEST_CASE("les interfaces utilisent une horloge de simulation Backrooms gelee") {
    CHECK(backrooms_ui_freezes_simulation(false, true));
    CHECK_FALSE(backrooms_ui_freezes_simulation(false, false));
    CHECK_FALSE(backrooms_ui_freezes_simulation(true, true));

    const auto running =
        resolve_backrooms_frame_time(0.25F, false);
    CHECK(running.real_delta_seconds ==
          doctest::Approx(kBackroomsMaximumSimulationDeltaSeconds));
    CHECK(running.simulation_delta_seconds ==
          doctest::Approx(kBackroomsMaximumSimulationDeltaSeconds));
    CHECK_FALSE(running.simulation_frozen);

    const auto frozen =
        resolve_backrooms_frame_time(0.016F, true);
    CHECK(frozen.real_delta_seconds == doctest::Approx(0.016F));
    CHECK(frozen.simulation_delta_seconds == doctest::Approx(0.0F));
    CHECK(frozen.simulation_frozen);

    const auto invalid = resolve_backrooms_frame_time(
        std::numeric_limits<float>::infinity(),
        false);
    CHECK(invalid.real_delta_seconds == doctest::Approx(0.0F));
    CHECK(invalid.simulation_delta_seconds == doctest::Approx(0.0F));
}

TEST_CASE("chaque interface Backrooms bloque la simulation") {
    const std::array blockers {
        BackroomsGameplayBlockers {.death_screen = true},
        BackroomsGameplayBlockers {.pause_menu = true},
        BackroomsGameplayBlockers {.inventory = true},
        BackroomsGameplayBlockers {.progression = true},
        BackroomsGameplayBlockers {.command_console = true},
        BackroomsGameplayBlockers {.confirmation_dialog = true},
        BackroomsGameplayBlockers {.front_end = true},
    };

    CHECK_FALSE(backrooms_gameplay_interaction_blocked({}));
    for (const auto& blocker : blockers) {
        CHECK(backrooms_gameplay_interaction_blocked(blocker));
    }
}

TEST_CASE("Espace reste neutralise de la reprise jusqu'a son relachement") {
    BackroomsResumeState state {};

    const auto frozen =
        advance_backrooms_resume_state(state, true, false);
    CHECK(frozen.synchronize_latches);
    CHECK_FALSE(frozen.suppress_jump);

    const auto resumed_with_space =
        advance_backrooms_resume_state(state, false, true);
    CHECK(resumed_with_space.synchronize_latches);
    CHECK(resumed_with_space.suppress_jump);

    const auto still_held =
        advance_backrooms_resume_state(state, false, true);
    CHECK_FALSE(still_held.synchronize_latches);
    CHECK(still_held.suppress_jump);

    const auto released =
        advance_backrooms_resume_state(state, false, false);
    CHECK_FALSE(released.suppress_jump);

    const auto fresh_press =
        advance_backrooms_resume_state(state, false, true);
    CHECK_FALSE(fresh_press.suppress_jump);
}

TEST_CASE("la validation du menu arme la reprise d'une nouvelle session") {
    auto session_state = initialize_backrooms_resume_state(false);
    const auto validated_with_space =
        advance_backrooms_resume_state(session_state, false, true);
    CHECK(validated_with_space.synchronize_latches);
    CHECK(validated_with_space.suppress_jump);

    auto smoke_state = initialize_backrooms_resume_state(true);
    const auto smoke_input =
        advance_backrooms_resume_state(smoke_state, false, true);
    CHECK_FALSE(smoke_input.synchronize_latches);
    CHECK_FALSE(smoke_input.suppress_jump);
}

TEST_CASE("une interface ouverte puis fermee dans le meme lot reste observee") {
    BackroomsResumeState state {};
    note_backrooms_interaction_boundary(state, true, false);

    const auto resumed_with_space =
        advance_backrooms_resume_state(state, false, true);
    CHECK(resumed_with_space.synchronize_latches);
    CHECK(resumed_with_space.suppress_jump);

    BackroomsResumeState smoke_state {};
    note_backrooms_interaction_boundary(smoke_state, true, true);
    CHECK_FALSE(smoke_state.simulation_was_frozen);
}

TEST_CASE("la lampe Backrooms demarre pleine et eteinte") {
    const BackroomsFlashlightState state {};

    CHECK(state.battery_charge == doctest::Approx(1.0F));
    CHECK_FALSE(state.enabled);
    CHECK(backrooms_flashlight_intensity(state) == doctest::Approx(0.0F));
}

TEST_CASE("F allume puis eteint la lampe Backrooms") {
    BackroomsFlashlightState state {};

    CHECK(toggle_backrooms_flashlight(state) ==
          BackroomsFlashlightToggleResult::Activated);
    CHECK(state.enabled);
    CHECK(toggle_backrooms_flashlight(state) ==
          BackroomsFlashlightToggleResult::Deactivated);
    CHECK_FALSE(state.enabled);
}

TEST_CASE("la batterie se vide en quatre vingt dix secondes allumee") {
    BackroomsFlashlightState state {
        .battery_charge = 1.0F,
        .enabled = true,
    };

    const auto half = update_backrooms_flashlight(
        state,
        kBackroomsFlashlightDrainSeconds * 0.5F);
    CHECK(half.battery_changed);
    CHECK_FALSE(half.depleted);
    CHECK(state.enabled);
    CHECK(state.battery_charge == doctest::Approx(0.5F));

    const auto empty = update_backrooms_flashlight(
        state,
        kBackroomsFlashlightDrainSeconds * 0.5F);
    CHECK(empty.battery_changed);
    CHECK(empty.depleted);
    CHECK_FALSE(state.enabled);
    CHECK(state.battery_charge == doctest::Approx(0.0F));
}

TEST_CASE("la batterie eteinte se recharge lentement et reste bornee") {
    BackroomsFlashlightState state {
        .battery_charge = 0.25F,
        .enabled = false,
    };

    static_cast<void>(update_backrooms_flashlight(
        state,
        kBackroomsFlashlightRechargeSeconds * 0.5F));
    CHECK(state.battery_charge == doctest::Approx(0.75F));
    CHECK_FALSE(state.enabled);

    static_cast<void>(update_backrooms_flashlight(
        state,
        kBackroomsFlashlightRechargeSeconds));
    CHECK(state.battery_charge == doctest::Approx(1.0F));
}

TEST_CASE("une batterie presque vide refuse le rallumage") {
    BackroomsFlashlightState state {
        .battery_charge =
            kBackroomsFlashlightMinimumActivationCharge * 0.5F,
        .enabled = false,
    };

    CHECK(toggle_backrooms_flashlight(state) ==
          BackroomsFlashlightToggleResult::BatteryTooLow);
    CHECK_FALSE(state.enabled);
}

TEST_CASE("les valeurs invalides ne contaminent jamais la simulation") {
    BackroomsFlashlightState state {
        .battery_charge =
            std::numeric_limits<float>::quiet_NaN(),
        .enabled = true,
    };

    const auto sanitized =
        sanitize_backrooms_flashlight_state(state);
    CHECK(std::isfinite(sanitized.battery_charge));
    CHECK(sanitized.battery_charge == doctest::Approx(0.0F));
    CHECK_FALSE(sanitized.enabled);

    state = {};
    static_cast<void>(update_backrooms_flashlight(
        state,
        std::numeric_limits<float>::infinity()));
    CHECK(state == BackroomsFlashlightState {});
}

TEST_CASE("la vue HUD est normalisee et uniquement visible en Backrooms") {
    const BackroomsFlashlightState state {
        .battery_charge = 1.4F,
        .enabled = true,
    };

    const auto visible =
        make_backrooms_flashlight_hud_view(state, true);
    CHECK(visible.visible);
    CHECK(visible.active);
    CHECK(visible.battery_ratio == doctest::Approx(1.0F));
    CHECK(visible.beam_intensity == doctest::Approx(1.0F));

    const auto hidden =
        make_backrooms_flashlight_hud_view(state, false);
    CHECK_FALSE(hidden.visible);
}

TEST_CASE("la batterie reste deterministe aux frequences de rendu usuelles") {
    const auto simulate =
        [](int frequency) {
            BackroomsFlashlightState state {
                .battery_charge = 1.0F,
                .enabled = true,
            };
            const auto frame_count =
                frequency * 30;
            const auto dt =
                1.0F /
                static_cast<float>(frequency);
            for (auto frame = 0;
                 frame < frame_count;
                 ++frame) {
                static_cast<void>(
                    update_backrooms_flashlight(
                        state,
                        dt));
            }
            return state.battery_charge;
        };

    const auto at_30_hz = simulate(30);
    const auto at_60_hz = simulate(60);
    const auto at_144_hz = simulate(144);
    CHECK(at_30_hz == doctest::Approx(2.0F / 3.0F).epsilon(0.0002));
    CHECK(at_60_hz == doctest::Approx(at_30_hz).epsilon(0.0002));
    CHECK(at_144_hz == doctest::Approx(at_30_hz).epsilon(0.0002));
    CHECK(
        kBackroomsFlashlightRechargeSeconds >
        kBackroomsFlashlightDrainSeconds);
}

TEST_CASE("le HUD de batterie reste fini et dans chaque viewport") {
    for (const auto& [width, height] :
         {std::pair{480, 320},
          std::pair{1280, 720},
          std::pair{2560, 1080}}) {
        const auto vertices =
            build_backrooms_flashlight_hud_geometry(
                {
                    .battery_ratio = 0.42F,
                    .beam_intensity = 1.0F,
                    .visible = true,
                    .active = true,
                },
                width,
                height);
        REQUIRE_FALSE(vertices.empty());
        for (const auto& vertex : vertices) {
            CHECK(std::isfinite(vertex.x));
            CHECK(std::isfinite(vertex.y));
            CHECK(vertex.x >= -1.0001F);
            CHECK(vertex.x <= 1.0001F);
            CHECK(vertex.y >= -1.0001F);
            CHECK(vertex.y <= 1.0001F);
        }
    }

    CHECK(
        build_backrooms_flashlight_hud_geometry(
            {},
            1280,
            720)
            .empty());
}

} // namespace valcraft
