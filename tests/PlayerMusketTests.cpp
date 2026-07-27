#include "gameplay/PlayerMusket.h"
#include "render/MusketVisualRecipe.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;

[[nodiscard]] auto angle_degrees(
    const glm::vec3& first,
    const glm::vec3& second) -> float {
    const auto normalized_first = glm::normalize(first);
    const auto normalized_second = glm::normalize(second);
    return std::acos(
        std::clamp(
            glm::dot(normalized_first, normalized_second),
            -1.0F,
            1.0F)) *
        (180.0F / kPi);
}

void release_controls(PlayerMusketController& controller) {
    static_cast<void>(controller.update(
        PlayerMusketInput {.active = true},
        0.0F,
        {0.0F, 0.0F, -1.0F}));
}

} // namespace

TEST_CASE("player musket exposes the locked gameplay parameters") {
    PlayerMusketController controller {};
    const auto& config = controller.config();

    CHECK(config.maximum_range == doctest::Approx(50.0F));
    CHECK(config.base_damage == doctest::Approx(20.0F));
    CHECK(config.reload_seconds == doctest::Approx(5.0F));
    CHECK(config.ads_seconds == doctest::Approx(0.18F));
    CHECK(config.recoil_seconds == doctest::Approx(0.30F));
    CHECK(config.hip_spread_degrees == doctest::Approx(2.5F));
    CHECK(controller.state() == PlayerMusketState::Loaded);
    CHECK(controller.loaded());
}

TEST_CASE("player and guard musket reload share the seven historical phases") {
    constexpr std::array<float, 7> samples {{
        0.06F,
        0.18F,
        0.31F,
        0.45F,
        0.62F,
        0.79F,
        0.93F,
    }};
    for (std::size_t stage = 0U;
         stage < samples.size();
         ++stage) {
        CHECK(
            musket_reload_stage(samples[stage]) ==
            stage);
    }

    CHECK(musket_reload_stage(0.0F) == 0U);
    CHECK(musket_reload_stage(0.12F) == 1U);
    CHECK(musket_reload_stage(0.24F) == 2U);
    CHECK(musket_reload_stage(0.38F) == 3U);
    CHECK(musket_reload_stage(0.52F) == 4U);
    CHECK(musket_reload_stage(0.72F) == 5U);
    CHECK(musket_reload_stage(0.86F) == 6U);
    CHECK(
        musket_reload_stage(
            std::numeric_limits<float>::quiet_NaN()) ==
        0U);
}

TEST_CASE("player musket fires once per trigger edge and consumes its chamber") {
    PlayerMusketController controller {};
    const PlayerMusketInput held {
        .damage_multiplier = 1.5F,
        .active = true,
        .fire_held = true,
    };

    const auto first = controller.update(
        held,
        0.0F,
        {0.0F, 0.0F, -1.0F});
    REQUIRE(first.fired);
    CHECK_FALSE(first.dry_fired);
    CHECK(first.chamber_state_changed);
    CHECK_FALSE(first.loaded_after);
    CHECK(first.shot_sequence == 1U);
    CHECK(first.maximum_distance == doctest::Approx(50.0F));
    CHECK(first.damage == doctest::Approx(30.0F));
    CHECK(controller.state() == PlayerMusketState::Empty);

    const auto still_held = controller.update(
        held,
        0.0F,
        {0.0F, 0.0F, -1.0F});
    CHECK_FALSE(still_held.fired);
    CHECK_FALSE(still_held.dry_fired);

    release_controls(controller);
    const auto empty_edge = controller.update(
        held,
        0.0F,
        {0.0F, 0.0F, -1.0F});
    CHECK_FALSE(empty_edge.fired);
    CHECK(empty_edge.dry_fired);
    CHECK(empty_edge.shot_sequence == 1U);
}

TEST_CASE("player musket never loses a quick press released between simulation ticks") {
    PlayerMusketController controller {};

    const auto quick_click =
        controller.update(
            PlayerMusketInput {
                .active = true,
                .fire_pressed = true,
            },
            0.0F,
            {0.0F, 0.0F, -1.0F});
    CHECK(quick_click.fired);
    CHECK_FALSE(controller.loaded());

    controller.synchronize_chamber(true);
    const auto next_quick_click =
        controller.update(
            PlayerMusketInput {
                .active = true,
                .fire_pressed = true,
            },
            0.0F,
            {0.0F, 0.0F, -1.0F});
    CHECK(next_quick_click.fired);
    CHECK(next_quick_click.shot_sequence == 2U);
}

TEST_CASE("player musket ADS stays exact and hip spread stays deterministic") {
    const glm::vec3 forward {0.23F, -0.11F, -0.91F};
    const glm::vec3 up {0.0F, 1.0F, 0.0F};
    const auto expected = glm::normalize(forward);

    const auto aimed = player_musket_shot_direction(
        forward,
        up,
        true,
        77U);
    CHECK(aimed.x == doctest::Approx(expected.x));
    CHECK(aimed.y == doctest::Approx(expected.y));
    CHECK(aimed.z == doctest::Approx(expected.z));

    const auto hip_first = player_musket_shot_direction(
        forward,
        up,
        false,
        77U);
    const auto hip_repeat = player_musket_shot_direction(
        forward,
        up,
        false,
        77U);
    const auto hip_next = player_musket_shot_direction(
        forward,
        up,
        false,
        78U);
    CHECK(hip_first == hip_repeat);
    CHECK(hip_first != hip_next);
    CHECK(glm::length(hip_first) == doctest::Approx(1.0F));
    CHECK(angle_degrees(forward, hip_first) <=
          doctest::Approx(2.5F).epsilon(0.001));
    CHECK(angle_degrees(forward, hip_first) > 0.0F);
}

TEST_CASE("player musket detects ADS from the held aim control") {
    PlayerMusketController controller {};
    const PlayerMusketInput input {
        .active = true,
        .aim_held = true,
        .fire_held = true,
    };
    const glm::vec3 forward {0.4F, 0.1F, -0.8F};

    const auto event = controller.update(
        input,
        0.09F,
        forward);
    REQUIRE(event.fired);
    const auto expected = glm::normalize(forward);
    CHECK(event.shot_direction.x == doctest::Approx(expected.x));
    CHECK(event.shot_direction.y == doctest::Approx(expected.y));
    CHECK(event.shot_direction.z == doctest::Approx(expected.z));
    CHECK(controller.view().aim_ratio == doctest::Approx(0.5F));
    CHECK(controller.view().aim_requested);
}

TEST_CASE("player musket reloads manually in exactly five seconds") {
    PlayerMusketController controller {};
    controller.synchronize_chamber(false);

    const PlayerMusketInput reload {
        .active = true,
        .reload_held = true,
    };
    const auto started = controller.update(
        reload,
        0.0F,
        {0.0F, 0.0F, -1.0F});
    CHECK(started.reload_started);
    CHECK_FALSE(started.reload_completed);
    CHECK(controller.state() == PlayerMusketState::Reloading);
    CHECK(controller.view().reload_progress == doctest::Approx(0.0F));
    CHECK(controller.view().reload_stage == 0U);

    const auto halfway = controller.update(
        reload,
        2.5F,
        {0.0F, 0.0F, -1.0F});
    CHECK_FALSE(halfway.reload_started);
    CHECK_FALSE(halfway.reload_completed);
    CHECK(controller.view().reload_progress == doctest::Approx(0.5F));
    CHECK(controller.view().reload_stage == 3U);

    const auto completed = controller.update(
        reload,
        2.5F,
        {0.0F, 0.0F, -1.0F});
    CHECK(completed.reload_completed);
    CHECK(completed.chamber_state_changed);
    CHECK(completed.loaded_after);
    CHECK(controller.state() == PlayerMusketState::Loaded);

    const auto still_held = controller.update(
        reload,
        8.0F,
        {0.0F, 0.0F, -1.0F});
    CHECK_FALSE(still_held.reload_started);
    CHECK_FALSE(still_held.reload_completed);
    CHECK(controller.loaded());
}

TEST_CASE("player musket completes fifty fractional reload steps at five seconds") {
    PlayerMusketController controller {};
    controller.synchronize_chamber(false);
    static_cast<void>(controller.update(
        PlayerMusketInput {
            .active = true,
            .reload_pressed = true,
        },
        0.0F,
        {0.0F, 0.0F, -1.0F}));

    for (auto step = 0; step < 49; ++step) {
        const auto event = controller.update(
            PlayerMusketInput {
                .active = true,
            },
            0.1F,
            {0.0F, 0.0F, -1.0F});
        CHECK_FALSE(event.reload_completed);
    }
    const auto completed = controller.update(
        PlayerMusketInput {
            .active = true,
        },
        0.1F,
        {0.0F, 0.0F, -1.0F});
    CHECK(completed.reload_completed);
    CHECK(controller.loaded());
}

TEST_CASE("player musket camera curves keep exact endpoints and sanitize inputs") {
    CHECK(player_musket_world_fov(0.0F) == doctest::Approx(75.0F));
    CHECK(player_musket_world_fov(1.0F) == doctest::Approx(58.0F));
    CHECK(player_musket_world_fov(0.5F) == doctest::Approx(66.5F));
    CHECK(player_musket_look_scale(0.0F) == doctest::Approx(1.0F));
    CHECK(player_musket_look_scale(1.0F) == doctest::Approx(0.65F));
    CHECK(player_musket_viewmodel_fov(62.0F, 0.0F) ==
          doctest::Approx(62.0F));
    CHECK(player_musket_viewmodel_fov(62.0F, 1.0F) ==
          doctest::Approx(50.0F));
    CHECK(player_musket_viewmodel_fov(40.0F, 1.0F) ==
          doctest::Approx(40.0F));
    CHECK(player_musket_world_fov(
              std::numeric_limits<float>::quiet_NaN()) ==
          doctest::Approx(75.0F));
    CHECK(player_musket_viewmodel_fov(
              std::numeric_limits<float>::quiet_NaN(),
              std::numeric_limits<float>::infinity()) ==
          doctest::Approx(62.0F));
}

TEST_CASE("player musket resolves simultaneous fire before reload") {
    PlayerMusketController controller {};
    const PlayerMusketInput input {
        .active = true,
        .fire_held = true,
        .reload_held = true,
    };

    const auto event = controller.update(
        input,
        0.0F,
        {0.0F, 0.0F, -1.0F});
    CHECK(event.fired);
    CHECK(event.reload_started);
    CHECK_FALSE(event.loaded_after);
    CHECK(controller.state() == PlayerMusketState::Reloading);
}

TEST_CASE("player musket explicit reload pulse survives a cancelled held edge") {
    PlayerMusketController controller {};

    static_cast<void>(controller.update(
        PlayerMusketInput {
            .active = true,
            .reload_held = true,
        },
        0.0F,
        {0.0F, 0.0F, -1.0F}));
    controller.cancel_transient_actions();

    const auto event = controller.update(
        PlayerMusketInput {
            .active = true,
            .fire_pressed = true,
            .reload_pressed = true,
        },
        0.0F,
        {0.0F, 0.0F, -1.0F});
    CHECK(event.fired);
    CHECK(event.reload_started);
    CHECK(controller.state() == PlayerMusketState::Reloading);
}

TEST_CASE("player musket cancellation keeps an interrupted chamber empty") {
    PlayerMusketController controller {};
    controller.synchronize_chamber(false);
    static_cast<void>(controller.update(
        PlayerMusketInput {
            .active = true,
            .aim_held = true,
            .reload_held = true,
        },
        1.0F,
        {0.0F, 0.0F, -1.0F}));
    REQUIRE(controller.state() == PlayerMusketState::Reloading);

    const auto cancelled = controller.update(
        PlayerMusketInput {
            .active = true,
            .cancel_requested = true,
        },
        1.0F,
        {0.0F, 0.0F, -1.0F});
    CHECK(cancelled.reload_cancelled);
    CHECK_FALSE(cancelled.loaded_after);
    CHECK(controller.state() == PlayerMusketState::Empty);
    CHECK_FALSE(controller.view().active);
    CHECK_FALSE(controller.view().aim_requested);
    CHECK(controller.view().aim_ratio == doctest::Approx(0.0F));
    CHECK(controller.view().reload_progress == doctest::Approx(0.0F));
}

TEST_CASE("player musket inactive input cancels reload without creating a latent shot") {
    PlayerMusketController controller {};
    controller.synchronize_chamber(false);
    static_cast<void>(controller.update(
        PlayerMusketInput {
            .active = true,
            .reload_held = true,
        },
        1.0F,
        {0.0F, 0.0F, -1.0F}));
    REQUIRE(controller.state() == PlayerMusketState::Reloading);

    const auto inactive = controller.update(
        PlayerMusketInput {
            .active = false,
            .fire_held = true,
        },
        10.0F,
        {0.0F, 0.0F, -1.0F});
    CHECK(inactive.reload_cancelled);
    CHECK_FALSE(inactive.fired);
    CHECK(controller.state() == PlayerMusketState::Empty);

    controller.synchronize_chamber(true);
    const auto newly_active = controller.update(
        PlayerMusketInput {
            .active = true,
            .fire_held = true,
        },
        0.0F,
        {0.0F, 0.0F, -1.0F});
    CHECK_FALSE(newly_active.fired);
    CHECK(controller.loaded());

    release_controls(controller);
    const auto released_then_pressed = controller.update(
        PlayerMusketInput {
            .active = true,
            .fire_held = true,
        },
        0.0F,
        {0.0F, 0.0F, -1.0F});
    CHECK(released_then_pressed.fired);
}

TEST_CASE("player musket ADS recoil and reload timings remain finite") {
    PlayerMusketController controller {};
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();

    static_cast<void>(controller.update(
        PlayerMusketInput {
            .active = true,
            .aim_held = true,
        },
        nan,
        {nan, infinity, 0.0F},
        {0.0F, nan, 0.0F}));
    CHECK(std::isfinite(controller.view().aim_ratio));
    CHECK(controller.view().aim_ratio == doctest::Approx(0.0F));

    static_cast<void>(controller.update(
        PlayerMusketInput {
            .active = true,
            .aim_held = true,
        },
        0.09F,
        {0.0F, 0.0F, -1.0F}));
    CHECK(controller.view().aim_ratio == doctest::Approx(0.5F));

    static_cast<void>(controller.update(
        PlayerMusketInput {
            .active = true,
            .aim_held = true,
            .fire_held = true,
        },
        0.0F,
        {0.0F, 0.0F, -1.0F}));
    CHECK(controller.view().recoil_ratio == doctest::Approx(1.0F));

    static_cast<void>(controller.update(
        PlayerMusketInput {
            .active = true,
            .aim_held = true,
        },
        0.15F,
        {0.0F, 0.0F, -1.0F}));
    CHECK(controller.view().recoil_ratio == doctest::Approx(0.5F));

    release_controls(controller);
    static_cast<void>(controller.update(
        PlayerMusketInput {
            .active = true,
            .reload_held = true,
        },
        infinity,
        {0.0F, 0.0F, -1.0F}));
    CHECK(controller.state() == PlayerMusketState::Reloading);
    CHECK(std::isfinite(controller.view().reload_progress));

    static_cast<void>(controller.update(
        PlayerMusketInput {
            .active = true,
            .reload_held = true,
        },
        1000.0F,
        {0.0F, 0.0F, -1.0F}));
    CHECK(controller.loaded());
    CHECK(std::isfinite(controller.view().recoil_ratio));
}

TEST_CASE("player musket sanitizes configuration and damage at its API boundary") {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    PlayerMusketController controller {
        MusketConfig {
            .maximum_range = -10.0F,
            .base_damage = nan,
            .reload_seconds = 0.0F,
            .ads_seconds = -1.0F,
            .recoil_seconds = nan,
            .hip_spread_degrees = 180.0F,
        },
    };

    CHECK(controller.config().maximum_range == doctest::Approx(50.0F));
    CHECK(controller.config().base_damage == doctest::Approx(20.0F));
    CHECK(controller.config().reload_seconds == doctest::Approx(5.0F));
    CHECK(controller.config().ads_seconds == doctest::Approx(0.18F));
    CHECK(controller.config().recoil_seconds == doctest::Approx(0.30F));
    CHECK(controller.config().hip_spread_degrees == doctest::Approx(45.0F));

    const auto event = controller.update(
        PlayerMusketInput {
            .damage_multiplier = std::numeric_limits<float>::max(),
            .active = true,
            .fire_held = true,
        },
        0.0F,
        {0.0F, 0.0F, -1.0F});
    REQUIRE(event.fired);
    CHECK(std::isfinite(event.damage));
    CHECK(event.damage > 0.0F);
}

TEST_CASE("player musket direction helper sanitizes degenerate camera vectors") {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto direction = player_musket_shot_direction(
        {nan, 0.0F, 0.0F},
        {0.0F, 0.0F, 0.0F},
        false,
        4U);

    CHECK(std::isfinite(direction.x));
    CHECK(std::isfinite(direction.y));
    CHECK(std::isfinite(direction.z));
    CHECK(glm::length(direction) == doctest::Approx(1.0F));
    CHECK(angle_degrees(
              glm::vec3 {0.0F, 0.0F, -1.0F},
              direction) <=
          doctest::Approx(2.5F).epsilon(0.001));
}

TEST_CASE("player musket restores its deterministic sequence across a save boundary") {
    PlayerMusketController controller {};
    controller.reset(
        true,
        81U);

    const auto event =
        controller.update(
            PlayerMusketInput {
                .active = true,
                .fire_held = true,
            },
            0.0F,
            {0.0F, 0.0F, -1.0F});
    REQUIRE(event.fired);
    CHECK(event.shot_sequence == 82U);
    CHECK(controller.view().shot_sequence == 82U);
    CHECK(
        event.shot_direction ==
        player_musket_shot_direction(
            {0.0F, 0.0F, -1.0F},
            {0.0F, 1.0F, 0.0F},
            false,
            82U));
}

} // namespace valcraft
