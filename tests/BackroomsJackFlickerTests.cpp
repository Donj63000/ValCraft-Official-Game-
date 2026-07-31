#include "render/BackroomsFlicker.h"

#include "world/BackroomsGenerator.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace valcraft {

namespace {

struct ExpectedFixture {
    BackroomsFlickerAnchor anchor{};
    double center_x = 0.0;
    double center_z = 0.0;
};

[[nodiscard]] constexpr auto light_can_be_disturbed(
    BackroomsLightState state) noexcept -> bool {
    return
        state == BackroomsLightState::Active ||
        state == BackroomsLightState::Emergency;
}

[[nodiscard]] auto expected_canonical_fixture(
    const BackroomsGenerator& generator,
    int world_x,
    int world_z) noexcept
    -> std::optional<ExpectedFixture> {
    const auto sample =
        generator.sample_column(
            world_x,
            world_z);
    if (!light_can_be_disturbed(
            sample.light_state)) {
        return std::nullopt;
    }

    const auto descriptor =
        generator.descriptor_at(
            world_x,
            world_z);
    const auto step_x =
        descriptor.primary_axis_x ? 1 : 0;
    const auto step_z =
        descriptor.primary_axis_x ? 0 : 1;
    const auto previous_x =
        static_cast<std::int64_t>(world_x) -
        step_x;
    const auto previous_z =
        static_cast<std::int64_t>(world_z) -
        step_z;
    if (previous_x >=
            std::numeric_limits<int>::lowest() &&
        previous_x <=
            std::numeric_limits<int>::max() &&
        previous_z >=
            std::numeric_limits<int>::lowest() &&
        previous_z <=
            std::numeric_limits<int>::max() &&
        generator
                .sample_column(
                    static_cast<int>(previous_x),
                    static_cast<int>(previous_z))
                .light_state ==
            sample.light_state) {
        return std::nullopt;
    }

    auto run_length = 1;
    for (auto offset = 1;
         offset < 4;
         ++offset) {
        const auto next_x =
            static_cast<std::int64_t>(world_x) +
            static_cast<std::int64_t>(
                step_x * offset);
        const auto next_z =
            static_cast<std::int64_t>(world_z) +
            static_cast<std::int64_t>(
                step_z * offset);
        if (next_x <
                std::numeric_limits<int>::lowest() ||
            next_x >
                std::numeric_limits<int>::max() ||
            next_z <
                std::numeric_limits<int>::lowest() ||
            next_z >
                std::numeric_limits<int>::max() ||
            generator
                    .sample_column(
                        static_cast<int>(next_x),
                        static_cast<int>(next_z))
                    .light_state !=
                sample.light_state) {
            break;
        }
        ++run_length;
    }

    const auto half_span =
        0.5 *
        static_cast<double>(
            run_length - 1);
    const auto center_x =
        static_cast<double>(world_x) +
        0.5 +
        static_cast<double>(step_x) *
            half_span;
    const auto center_z =
        static_cast<double>(world_z) +
        0.5 +
        static_cast<double>(step_z) *
            half_span;
    return ExpectedFixture{
        {
            world_x,
            world_z,
            static_cast<float>(center_x),
            static_cast<float>(sample.ceiling_y),
            static_cast<float>(center_z),
        },
        center_x,
        center_z,
    };
}

[[nodiscard]] auto brute_force_nearest_fixture(
    int seed,
    double position_x,
    double position_z,
    int radius)
    -> std::optional<BackroomsFlickerAnchor> {
    const auto bounded_radius =
        std::clamp(
            radius,
            0,
            kMaximumBackroomsFixtureSearchRadius);
    const auto center_x =
        static_cast<int>(
            std::floor(position_x));
    const auto center_z =
        static_cast<int>(
            std::floor(position_z));
    const auto scan_radius =
        bounded_radius + 3;
    const auto radius_squared =
        static_cast<double>(
            bounded_radius) *
        static_cast<double>(
            bounded_radius);
    const BackroomsGenerator generator{seed};
    auto nearest =
        std::optional<BackroomsFlickerAnchor>{};
    auto nearest_distance_squared =
        std::numeric_limits<double>::infinity();

    for (auto z =
             center_z - scan_radius;
         z <= center_z + scan_radius;
         ++z) {
        for (auto x =
                 center_x - scan_radius;
             x <= center_x + scan_radius;
             ++x) {
            const auto fixture =
                expected_canonical_fixture(
                    generator,
                    x,
                    z);
            if (!fixture.has_value()) {
                continue;
            }
            const auto delta_x =
                fixture->center_x -
                position_x;
            const auto delta_z =
                fixture->center_z -
                position_z;
            const auto distance_squared =
                delta_x * delta_x +
                delta_z * delta_z;
            if (distance_squared >
                radius_squared) {
                continue;
            }
            const auto tie_breaks_before =
                nearest.has_value() &&
                distance_squared ==
                    nearest_distance_squared &&
                (
                    fixture->anchor.world_z <
                        nearest->world_z ||
                    (
                        fixture->anchor.world_z ==
                            nearest->world_z &&
                        fixture->anchor.world_x <
                            nearest->world_x
                    )
                );
            if (!nearest.has_value() ||
                distance_squared <
                    nearest_distance_squared ||
                tie_breaks_before) {
                nearest =
                    fixture->anchor;
                nearest_distance_squared =
                    distance_squared;
            }
        }
    }
    return nearest;
}

} // namespace

TEST_CASE("Jack retrouve exactement la rampe lumineuse canonique la plus proche") {
    constexpr auto seed = 1337;
    constexpr auto radius = 80;
    const BackroomsGenerator generator{seed};
    const auto spawn =
        generator.spawn_block();
    const auto query_x =
        static_cast<double>(spawn.x) +
        0.37;
    const auto query_z =
        static_cast<double>(spawn.z) +
        0.61;

    const auto expected =
        brute_force_nearest_fixture(
            seed,
            query_x,
            query_z,
            radius);
    const auto actual =
        find_nearest_backrooms_light_fixture(
            seed,
            query_x,
            query_z,
            radius);

    REQUIRE(expected.has_value());
    REQUIRE(actual.has_value());
    CHECK(*actual == *expected);

    const auto sample =
        generator.sample_column(
            actual->world_x,
            actual->world_z);
    CHECK(
        light_can_be_disturbed(
            sample.light_state));
    CHECK(std::isfinite(actual->position_x));
    CHECK(std::isfinite(actual->position_y));
    CHECK(std::isfinite(actual->position_z));

    const auto descriptor =
        generator.descriptor_at(
            actual->world_x,
            actual->world_z);
    const auto previous =
        generator.sample_column(
            actual->world_x -
                (descriptor.primary_axis_x
                     ? 1
                     : 0),
            actual->world_z -
                (descriptor.primary_axis_x
                     ? 0
                     : 1));
    CHECK(
        previous.light_state !=
        sample.light_state);
}

TEST_CASE("Jack peut perturber une rampe de secours") {
    constexpr auto seed = 1337;
    const BackroomsGenerator generator{seed};
    auto emergency =
        std::optional<ExpectedFixture>{};

    for (auto z = -192;
         z <= 192 &&
         !emergency.has_value();
         ++z) {
        for (auto x = -192;
             x <= 192;
             ++x) {
            const auto sample =
                generator.sample_column(
                    x,
                    z);
            if (sample.light_state !=
                BackroomsLightState::Emergency) {
                continue;
            }
            emergency =
                expected_canonical_fixture(
                    generator,
                    x,
                    z);
            if (emergency.has_value()) {
                break;
            }
        }
    }

    REQUIRE(emergency.has_value());
    const auto found =
        find_nearest_backrooms_light_fixture(
            seed,
            emergency->center_x,
            emergency->center_z,
            0);
    REQUIRE(found.has_value());
    CHECK(
        *found ==
        emergency->anchor);
    CHECK(
        generator
            .sample_column(
                found->world_x,
                found->world_z)
            .light_state ==
        BackroomsLightState::Emergency);
}

TEST_CASE("la recherche de rampe borne son rayon et assainit les entrees") {
    constexpr auto seed = 7331;
    const BackroomsGenerator generator{seed};
    const auto spawn =
        generator.spawn_block();
    const auto position_x =
        static_cast<double>(spawn.x) +
        0.5;
    const auto position_z =
        static_cast<double>(spawn.z) +
        0.5;

    CHECK(
        find_nearest_backrooms_light_fixture(
            seed,
            position_x,
            position_z,
            std::numeric_limits<int>::max()) ==
        find_nearest_backrooms_light_fixture(
            seed,
            position_x,
            position_z,
            kMaximumBackroomsFixtureSearchRadius));
    CHECK(
        find_nearest_backrooms_light_fixture(
            seed,
            position_x,
            position_z,
            -500) ==
        find_nearest_backrooms_light_fixture(
            seed,
            position_x,
            position_z,
            0));
    CHECK_FALSE(
        find_nearest_backrooms_light_fixture(
            seed,
            std::numeric_limits<double>::
                quiet_NaN(),
            position_z,
            32)
            .has_value());
    CHECK_FALSE(
        find_nearest_backrooms_light_fixture(
            seed,
            position_x,
            std::numeric_limits<double>::
                infinity(),
            32)
            .has_value());
    CHECK_FALSE(
        find_nearest_backrooms_light_fixture(
            seed,
            static_cast<double>(
                std::numeric_limits<int>::max()) +
                1.0,
            position_z,
            32)
            .has_value());
}

TEST_CASE("la recherche de rampe reste sure aux limites du monde signe") {
    constexpr auto seed = 1337;
    const auto minimum =
        find_nearest_backrooms_light_fixture(
            seed,
            static_cast<double>(
                std::numeric_limits<int>::lowest()) +
                0.25,
            static_cast<double>(
                std::numeric_limits<int>::lowest()) +
                0.25,
            std::numeric_limits<int>::max());
    const auto maximum =
        find_nearest_backrooms_light_fixture(
            seed,
            static_cast<double>(
                std::numeric_limits<int>::max()) +
                0.25,
            static_cast<double>(
                std::numeric_limits<int>::max()) +
                0.25,
            std::numeric_limits<int>::max());

    if (minimum.has_value()) {
        CHECK(std::isfinite(minimum->position_x));
        CHECK(std::isfinite(minimum->position_y));
        CHECK(std::isfinite(minimum->position_z));
    }
    if (maximum.has_value()) {
        CHECK(std::isfinite(maximum->position_x));
        CHECK(std::isfinite(maximum->position_y));
        CHECK(std::isfinite(maximum->position_z));
    }
}

} // namespace valcraft
