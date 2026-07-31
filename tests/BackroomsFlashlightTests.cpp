#include "gameplay/BackroomsFlashlight.h"
#include "render/Renderer.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <utility>

namespace valcraft {

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
    for (const auto [width, height] :
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
