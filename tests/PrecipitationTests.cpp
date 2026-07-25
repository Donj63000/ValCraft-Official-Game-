#include "render/RendererQuality.h"
#include "world/Environment.h"
#include "world/PrecipitationField.h"

#include <doctest/doctest.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace valcraft {

static_assert(std::is_trivially_copyable_v<PrecipitationBudget>);
static_assert(PrecipitationBudget {1'200U, 16U, 18.0F}.max_drops == 1'200U);
static_assert(
    PrecipitationBudget {1'200U, 16U, 18.0F} ==
    PrecipitationBudget {1'200U, 16U, 18.0F});

namespace {

auto finite_vec2(const glm::vec2& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y);
}

auto finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

auto angular_distance(const glm::vec2& lhs, const glm::vec2& rhs) noexcept -> float {
    return std::acos(
        glm::clamp(
            glm::dot(lhs, rhs),
            -1.0F,
            1.0F));
}

auto average_fall_speed(const std::vector<RainDropInstance>& drops) -> float {
    if (drops.empty()) {
        return 0.0F;
    }
    auto total = 0.0F;
    for (const auto& drop : drops) {
        total += -drop.velocity.y;
    }
    return total /
           static_cast<float>(
               drops.size());
}

auto average_drop_length(const std::vector<RainDropInstance>& drops) -> float {
    if (drops.empty()) {
        return 0.0F;
    }
    auto total = 0.0F;
    for (const auto& drop : drops) {
        total += drop.length;
    }
    return total /
           static_cast<float>(
               drops.size());
}

auto average_horizontal_speed(
    const std::vector<RainDropInstance>& drops) -> float {
    if (drops.empty()) {
        return 0.0F;
    }
    auto total = 0.0F;
    for (const auto& drop : drops) {
        total +=
            glm::length(
                glm::vec2 {
                    drop.velocity.x,
                    drop.velocity.z,
                });
    }
    return total /
           static_cast<float>(
               drops.size());
}

} // namespace

TEST_CASE("renderer quality profiles expose explicit precipitation budgets") {
    const auto low =
        resolve_renderer_quality_settings(
            RendererQuality::Low,
            1280,
            720);
    const auto medium =
        resolve_renderer_quality_settings(
            RendererQuality::Medium,
            2560,
            1440);
    const auto high =
        resolve_renderer_quality_settings(
            RendererQuality::High,
            3840,
            2160);

    CHECK(low.precipitation_drop_budget == 1'200U);
    CHECK(low.precipitation_impact_budget == 16U);
    CHECK(low.precipitation_radius == doctest::Approx(18.0F));
    CHECK(medium.precipitation_drop_budget == 3'000U);
    CHECK(medium.precipitation_impact_budget == 48U);
    CHECK(medium.precipitation_radius == doctest::Approx(28.0F));
    CHECK(high.precipitation_drop_budget == 6'000U);
    CHECK(high.precipitation_impact_budget == 96U);
    CHECK(high.precipitation_radius == doctest::Approx(38.0F));

    CHECK(low.precipitation_drop_budget <
          medium.precipitation_drop_budget);
    CHECK(medium.precipitation_drop_budget <
          high.precipitation_drop_budget);
    CHECK(low.precipitation_impact_budget <
          medium.precipitation_impact_budget);
    CHECK(medium.precipitation_impact_budget <
          high.precipitation_impact_budget);
    CHECK(low.precipitation_radius <
          medium.precipitation_radius);
    CHECK(medium.precipitation_radius <
          high.precipitation_radius);
}

TEST_CASE("environment wind direction stays deterministic normalized and continuous") {
    constexpr std::array<std::uint32_t, 5> seeds {
        0U,
        1U,
        1'337U,
        424'242U,
        0xffffffffU,
    };
    constexpr std::array<float, 6> weather_times {
        0.0F,
        120.0F,
        239.99F,
        261.0F,
        2'685.0979F,
        kMaximumWeatherTimeSeconds,
    };

    for (const auto seed : seeds) {
        for (const auto weather_time : weather_times) {
            const auto first =
                EnvironmentClock::compute_state(
                    12.0F,
                    seed,
                    weather_time);
            const auto replay =
                EnvironmentClock::compute_state(
                    12.0F,
                    seed,
                    weather_time);
            CAPTURE(seed);
            CAPTURE(weather_time);
            CHECK(finite_vec2(first.wind_direction_xz));
            CHECK(glm::length(first.wind_direction_xz) ==
                  doctest::Approx(1.0F).epsilon(0.0001));
            CHECK(first.wind_direction_xz.x ==
                  replay.wind_direction_xz.x);
            CHECK(first.wind_direction_xz.y ==
                  replay.wind_direction_xz.y);
        }
    }

    for (int slot = 1; slot <= 64; ++slot) {
        const auto boundary =
            static_cast<float>(slot) *
            240.0F;
        const auto before =
            EnvironmentClock::compute_state(
                12.0F,
                91'337U,
                boundary - 1.0F / 60.0F);
        const auto at_boundary =
            EnvironmentClock::compute_state(
                12.0F,
                91'337U,
                boundary);
        const auto after =
            EnvironmentClock::compute_state(
                12.0F,
                91'337U,
                boundary + 1.0F / 60.0F);

        CAPTURE(slot);
        CHECK(glm::length(
                  before.wind_direction_xz -
                  at_boundary.wind_direction_xz) <
              0.002F);
        CHECK(glm::length(
                  after.wind_direction_xz -
                  at_boundary.wind_direction_xz) <
              0.002F);
    }

    bool checked_interpolation = false;
    for (int slot = 1; slot <= 64 && !checked_interpolation; ++slot) {
        const auto boundary =
            static_cast<float>(slot) *
            240.0F;
        const auto start =
            EnvironmentClock::compute_state(
                12.0F,
                777U,
                boundary);
        const auto middle =
            EnvironmentClock::compute_state(
                12.0F,
                777U,
                boundary + 21.0F);
        const auto end =
            EnvironmentClock::compute_state(
                12.0F,
                777U,
                boundary + 42.0F);
        const auto full_angle =
            angular_distance(
                start.wind_direction_xz,
                end.wind_direction_xz);
        if (full_angle < 0.20F) {
            continue;
        }

        CHECK(
            angular_distance(
                start.wind_direction_xz,
                middle.wind_direction_xz) ==
            doctest::Approx(full_angle * 0.5F).epsilon(0.001));
        CHECK(
            angular_distance(
                middle.wind_direction_xz,
                end.wind_direction_xz) ==
            doctest::Approx(full_angle * 0.5F).epsilon(0.001));
        checked_interpolation = true;
    }
    CHECK(checked_interpolation);

    const auto sanitized =
        EnvironmentClock::compute_state(
            12.0F,
            1'337U,
            std::numeric_limits<float>::quiet_NaN());
    CHECK(finite_vec2(sanitized.wind_direction_xz));
    CHECK(glm::length(sanitized.wind_direction_xz) ==
          doctest::Approx(1.0F).epsilon(0.0001));
}

TEST_CASE("precipitation field clears stale data for inactive or invalid inputs") {
    PrecipitationField field {12'345U};
    EnvironmentState environment {};
    const PrecipitationBudget budget {
        128U,
        12U,
        20.0F,
    };
    const glm::vec3 camera_position {
        8.0F,
        54.0F,
        -12.0F,
    };

    environment.precipitation_intensity = 1.0F;
    REQUIRE_FALSE(
        field.sample(
                 environment,
                 camera_position,
                 52.0F,
                 budget)
            .drops.empty());

    environment.precipitation_intensity = 0.0F;
    CHECK(
        field.sample(
                 environment,
                 camera_position,
                 52.0F,
                 budget)
            .drops.empty());
    CHECK(field.frame().impacts.empty());

    environment.precipitation_intensity =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(
        field.sample(
                 environment,
                 camera_position,
                 52.0F,
                 budget)
            .drops.empty());

    environment.precipitation_intensity =
        std::numeric_limits<float>::infinity();
    CHECK(
        field.sample(
                 environment,
                 camera_position,
                 52.0F,
                 budget)
            .drops.empty());

    environment.precipitation_intensity = 1.0F;
    CHECK(
        field.sample(
                 environment,
                 {
                     std::numeric_limits<float>::quiet_NaN(),
                     54.0F,
                     -12.0F,
                 },
                 52.0F,
                 budget)
            .drops.empty());
    CHECK(
        field.sample(
                 environment,
                 camera_position,
                 std::numeric_limits<float>::infinity(),
                 budget)
            .drops.empty());
    CHECK(
        field.sample(
                 environment,
                 camera_position,
                 52.0F,
                 {128U, 12U, 0.0F})
            .drops.empty());
    CHECK(
        field.sample(
                 environment,
                 camera_position,
                 52.0F,
                 {
                     128U,
                     12U,
                     std::numeric_limits<float>::quiet_NaN(),
                 })
            .drops.empty());
}

TEST_CASE("precipitation field is deterministic for absolute time and quantized camera") {
    EnvironmentState environment {};
    environment.weather_time_seconds = 1'234.5F;
    environment.precipitation_intensity = 0.76F;
    environment.storm_intensity = 0.42F;
    environment.violent_storm_intensity = 0.18F;
    environment.wind_strength = 0.70F;
    environment.wind_direction_xz = {3.0F, 4.0F};
    const PrecipitationBudget budget {
        512U,
        32U,
        24.0F,
    };
    const glm::vec3 first_camera {
        10.1F,
        52.1F,
        -5.9F,
    };
    const glm::vec3 same_cell_camera {
        10.9F,
        52.9F,
        -5.1F,
    };

    PrecipitationField first_field {77U};
    PrecipitationField replay_field {77U};
    PrecipitationField same_cell_field {77U};
    PrecipitationField other_seed_field {78U};
    const auto first =
        first_field.sample(
            environment,
            first_camera,
            51.5F,
            budget);
    const auto replay =
        replay_field.sample(
            environment,
            first_camera,
            51.5F,
            budget);
    const auto same_cell =
        same_cell_field.sample(
            environment,
            same_cell_camera,
            51.5F,
            budget);
    const auto other_seed =
        other_seed_field.sample(
            environment,
            first_camera,
            51.5F,
            budget);

    // Je calcule la valeur de référence avec la même règle métier afin de
    // rendre explicite la densification progressive apportée par la tempête.
    const auto expected_density =
        environment.precipitation_intensity *
        (1.0F +
         environment.storm_intensity * 0.18F +
         environment.violent_storm_intensity * 0.12F);
    const auto expected_drops =
        static_cast<std::size_t>(
            std::floor(
                static_cast<float>(
                    budget.max_drops) *
                expected_density));
    const auto expected_impacts =
        static_cast<std::size_t>(
            std::floor(
                static_cast<float>(
                    budget.max_impacts) *
                expected_density));

    CHECK(expected_drops == 426U);
    CHECK(expected_impacts == 26U);
    CHECK(first.drops.size() == expected_drops);
    CHECK(first.impacts.size() == expected_impacts);
    CHECK(first == replay);
    CHECK(first == same_cell);
    CHECK(first != other_seed);

    auto later_environment = environment;
    later_environment.weather_time_seconds += 0.5F;
    PrecipitationField later_field {77U};
    const auto later =
        later_field.sample(
            later_environment,
            first_camera,
            51.5F,
            budget);
    CHECK(first != later);
}

TEST_CASE("precipitation field respects budgets and emits finite render data") {
    EnvironmentState environment {};
    environment.weather_time_seconds =
        kMaximumWeatherTimeSeconds;
    environment.precipitation_intensity = 1.0F;
    environment.storm_intensity = 1.0F;
    environment.violent_storm_intensity = 1.0F;
    environment.wind_strength = 1.0F;
    environment.wind_direction_xz = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
    };
    const PrecipitationBudget budget {
        6'000U,
        96U,
        38.0F,
    };

    PrecipitationField field {0xCAFEU};
    const auto frame =
        field.sample(
            environment,
            {0.0F, 52.0F, 0.0F},
            51.0F,
            budget);

    REQUIRE(frame.drops.size() == budget.max_drops);
    REQUIRE(frame.impacts.size() == budget.max_impacts);
    for (const auto& drop : frame.drops) {
        CHECK(finite_vec3(drop.position));
        CHECK(finite_vec3(drop.velocity));
        CHECK(drop.position.x >= -budget.radius);
        CHECK(drop.position.x <= budget.radius);
        CHECK(drop.position.z >= -budget.radius);
        CHECK(drop.position.z <= budget.radius);
        CHECK(drop.position.y >= 51.0F);
        CHECK(drop.velocity.y < 0.0F);
        CHECK(drop.length > 0.0F);
        CHECK(drop.width > 0.0F);
        CHECK(drop.opacity >= 0.0F);
        CHECK(drop.opacity <= 1.0F);
    }
    for (const auto& impact : frame.impacts) {
        CHECK(finite_vec3(impact.position));
        CHECK(glm::length(
                  glm::vec2 {
                      impact.position.x,
                      impact.position.z,
                  }) <=
              budget.radius + 0.001F);
        CHECK(impact.position.y ==
              doctest::Approx(51.035F));
        CHECK(impact.age_seconds >= 0.0F);
        CHECK(impact.age_seconds <
              impact.lifetime_seconds);
        CHECK(impact.lifetime_seconds > 0.0F);
        CHECK(impact.radius >= 0.0F);
        CHECK(impact.opacity >= 0.0F);
        CHECK(impact.opacity <= 1.0F);
    }
}

TEST_CASE("precipitation field preserves most world cells across a camera boundary") {
    EnvironmentState environment {};
    environment.weather_time_seconds = 3'456.75F;
    environment.precipitation_intensity = 1.0F;
    environment.storm_intensity = 0.75F;
    environment.wind_strength = 0.80F;
    environment.wind_direction_xz = {0.6F, 0.8F};
    const PrecipitationBudget budget {
        6'000U,
        96U,
        38.0F,
    };

    PrecipitationField before_field {4'242U};
    PrecipitationField after_field {4'242U};
    const auto before =
        before_field.sample(
            environment,
            {1.99F, 52.0F, 0.0F},
            51.0F,
            budget);
    const auto after =
        after_field.sample(
            environment,
            {2.01F, 52.0F, 0.0F},
            51.0F,
            budget);

    std::unordered_set<std::uint32_t> before_ids;
    before_ids.reserve(before.drops.size());
    for (const auto& drop : before.drops) {
        before_ids.insert(drop.id);
    }

    auto preserved_ids = std::size_t {0U};
    for (const auto& drop : after.drops) {
        preserved_ids +=
            before_ids.contains(drop.id)
                ? 1U
                : 0U;
    }
    const auto preserved_ratio =
        static_cast<float>(
            preserved_ids) /
        static_cast<float>(
            after.drops.size());

    // Je renouvelle seulement la bordure du champ au lieu de faire apparaître
    // six mille nouvelles gouttes lors du franchissement d'une cellule.
    CHECK(preserved_ratio > 0.90F);
}

TEST_CASE("precipitation intensity and storm severity scale density and motion") {
    EnvironmentState light_rain {};
    light_rain.weather_time_seconds = 4'321.25F;
    light_rain.precipitation_intensity = 0.25F;
    light_rain.wind_strength = 0.30F;
    light_rain.wind_direction_xz = {1.0F, 0.0F};
    auto tempest = light_rain;
    tempest.precipitation_intensity = 1.0F;
    tempest.storm_intensity = 1.0F;
    tempest.violent_storm_intensity = 1.0F;
    tempest.wind_strength = 1.0F;
    const PrecipitationBudget budget {
        1'200U,
        32U,
        18.0F,
    };

    PrecipitationField light_field {8'888U};
    PrecipitationField tempest_field {8'888U};
    const auto light_frame =
        light_field.sample(
            light_rain,
            {0.0F, 52.0F, 0.0F},
            51.0F,
            budget);
    const auto tempest_frame =
        tempest_field.sample(
            tempest,
            {0.0F, 52.0F, 0.0F},
            51.0F,
            budget);

    CHECK(light_frame.drops.size() == 300U);
    CHECK(light_frame.impacts.size() == 8U);
    CHECK(tempest_frame.drops.size() == 1'200U);
    CHECK(tempest_frame.impacts.size() == 32U);
    CHECK(average_fall_speed(tempest_frame.drops) >
          average_fall_speed(light_frame.drops));
    CHECK(average_drop_length(tempest_frame.drops) >
          average_drop_length(light_frame.drops));
    CHECK(average_horizontal_speed(tempest_frame.drops) >
          average_horizontal_speed(light_frame.drops));

    auto calm_density_weather = light_rain;
    calm_density_weather.precipitation_intensity = 0.50F;
    auto storm_density_weather = calm_density_weather;
    storm_density_weather.storm_intensity = 1.0F;
    storm_density_weather.violent_storm_intensity = 1.0F;
    PrecipitationField calm_density_field {4'444U};
    PrecipitationField storm_density_field {4'444U};
    const auto& calm_density_frame =
        calm_density_field.sample(
            calm_density_weather,
            {0.0F, 52.0F, 0.0F},
            51.0F,
            budget);
    const auto& storm_density_frame =
        storm_density_field.sample(
            storm_density_weather,
            {0.0F, 52.0F, 0.0F},
            51.0F,
            budget);

    // Je verifie separement que la violence densifie une pluie deja active,
    // sans jamais creer de gouttes lorsque la precipitation vaut zero.
    CHECK(storm_density_frame.drops.size() >
          calm_density_frame.drops.size());
    CHECK(storm_density_frame.impacts.size() >
          calm_density_frame.impacts.size());
}

TEST_CASE("precipitation field clamps hostile budgets without unbounded allocation") {
    EnvironmentState environment {};
    environment.precipitation_intensity = 2.0F;
    environment.weather_time_seconds = -900.0F;
    environment.storm_intensity =
        std::numeric_limits<float>::infinity();
    environment.violent_storm_intensity =
        -std::numeric_limits<float>::infinity();
    environment.wind_strength =
        std::numeric_limits<float>::quiet_NaN();
    environment.wind_direction_xz = {};

    PrecipitationField field {};
    const auto frame =
        field.sample(
            environment,
            {0.0F, 52.0F, 0.0F},
            51.0F,
            {
                std::numeric_limits<std::size_t>::max(),
                std::numeric_limits<std::size_t>::max(),
                10'000.0F,
            });

    CHECK(frame.drops.size() ==
          kMaximumPrecipitationDropBudget);
    CHECK(frame.impacts.size() ==
          kMaximumPrecipitationImpactBudget);
    REQUIRE_FALSE(frame.drops.empty());
    REQUIRE_FALSE(frame.impacts.empty());
    CHECK(finite_vec3(frame.drops.front().position));
    CHECK(finite_vec3(frame.drops.front().velocity));
    CHECK(finite_vec3(frame.impacts.front().position));
}

} // namespace valcraft
