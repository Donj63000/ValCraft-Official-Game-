#include "render/OceanLifeField.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <set>
#include <vector>

namespace valcraft {
namespace {

[[nodiscard]] auto deep_ocean_surface(
    int,
    int) noexcept -> TerrainSurfaceSample {
    TerrainSurfaceSample sample {};
    sample.surface_height = 10;
    sample.water_level = 20;
    return sample;
}

[[nodiscard]] auto land_surface(
    int,
    int) noexcept -> TerrainSurfaceSample {
    TerrainSurfaceSample sample {};
    sample.surface_height = 20;
    sample.water_level = 20;
    return sample;
}

[[nodiscard]] auto shallow_surface(
    int,
    int) noexcept -> TerrainSurfaceSample {
    TerrainSurfaceSample sample {};
    sample.surface_height = 18;
    sample.water_level = 20;
    return sample;
}

[[nodiscard]] auto school_ids(
    const OceanLifeFrame& frame) -> std::set<std::uint32_t> {
    std::set<std::uint32_t> ids {};
    for (const auto& instance : frame.instances) {
        ids.insert(
            ocean_life_instance_school_id(instance));
    }
    return ids;
}

} // namespace

TEST_CASE("les budgets de vie marine suivent exactement la qualite") {
    CHECK(
        ocean_life_budget_for_quality(
            RendererQuality::High) ==
        OceanLifeBudget {6U, 8U, 56.0F});
    CHECK(
        ocean_life_budget_for_quality(
            RendererQuality::Dynamic) ==
        OceanLifeBudget {6U, 8U, 56.0F});
    CHECK(
        ocean_life_budget_for_quality(
            RendererQuality::Medium) ==
        OceanLifeBudget {4U, 6U, 40.0F});
    CHECK(
        ocean_life_budget_for_quality(
            RendererQuality::Low) ==
        OceanLifeBudget {3U, 4U, 28.0F});

    CHECK(
        ocean_life_distance_fade(47.9F, 56.0F) ==
        doctest::Approx(1.0F));
    CHECK(
        ocean_life_distance_fade(52.0F, 56.0F) ==
        doctest::Approx(0.5F));
    CHECK(
        ocean_life_distance_fade(56.0F, 56.0F) ==
        doctest::Approx(0.0F));
    CHECK(
        ocean_life_distance_fade(36.0F, 40.0F) ==
        doctest::Approx(0.5F));
    CHECK(
        ocean_life_distance_fade(24.0F, 28.0F) ==
        doctest::Approx(0.5F));
    CHECK(
        ocean_life_distance_fade(
            std::numeric_limits<float>::quiet_NaN(),
            56.0F) ==
        doctest::Approx(0.0F));
}

TEST_CASE("une instance de poisson reste compacte et directement exploitable") {
    CHECK(sizeof(OceanLifeInstance) == 32U);
    CHECK(offsetof(OceanLifeInstance, position) == 0U);
    CHECK(offsetof(OceanLifeInstance, scale) == 12U);
    CHECK(offsetof(OceanLifeInstance, heading_radians) == 16U);
    CHECK(offsetof(OceanLifeInstance, animation_phase) == 20U);
    CHECK(offsetof(OceanLifeInstance, fade) == 24U);
    CHECK(offsetof(OceanLifeInstance, packed_visual) == 28U);
}

TEST_CASE("le champ marin est deterministe borne et fini") {
    const auto sampler =
        make_ocean_life_surface_sampler(
            deep_ocean_surface);
    const auto high =
        ocean_life_budget_for_quality(
            RendererQuality::High);

    OceanLifeField first_field {};
    OceanLifeField replay_field {};
    OceanLifeField other_seed_field {};
    const auto first =
        first_field.sample(
            WorldGenerationProfile::OceanAdventure,
            0x12345678U,
            {0.25F, 54.0F, -0.5F},
            123.25F,
            high,
            sampler);
    const auto replay =
        replay_field.sample(
            WorldGenerationProfile::OceanAdventure,
            0x12345678U,
            {0.25F, 54.0F, -0.5F},
            123.25F,
            high,
            sampler);
    const auto other_seed =
        other_seed_field.sample(
            WorldGenerationProfile::OceanAdventure,
            0x12345679U,
            {0.25F, 54.0F, -0.5F},
            123.25F,
            high,
            sampler);

    CHECK(first == replay);
    CHECK_FALSE(first == other_seed);
    CHECK(first.school_count == 6U);
    CHECK(first.instances.size() == 48U);
    CHECK(
        first.instances.size() <=
        kOceanLifeMaximumInstanceCount);

    std::set<std::uint32_t> instance_ids {};
    auto minimum_school_depth =
        std::numeric_limits<float>::max();
    auto maximum_school_depth =
        std::numeric_limits<float>::lowest();
    for (const auto& instance : first.instances) {
        CHECK(std::isfinite(instance.position.x));
        CHECK(std::isfinite(instance.position.y));
        CHECK(std::isfinite(instance.position.z));
        CHECK(std::isfinite(instance.scale));
        CHECK(std::isfinite(instance.heading_radians));
        CHECK(std::isfinite(instance.animation_phase));
        CHECK(std::isfinite(instance.fade));
        CHECK(instance.position.y > 11.0F);
        CHECK(instance.position.y < 21.0F);
        const auto school_depth =
            21.0F - instance.position.y;
        minimum_school_depth =
            std::min(
                minimum_school_depth,
                school_depth);
        maximum_school_depth =
            std::max(
                maximum_school_depth,
                school_depth);
        CHECK(instance.scale >= 0.22F);
        CHECK(instance.scale <= 0.38F);
        CHECK(instance.animation_phase >= 0.0F);
        CHECK(instance.animation_phase <= 6.284F);
        CHECK(instance.fade >= 0.0F);
        CHECK(instance.fade <= 1.0F);
        CHECK(
            ocean_life_instance_member_index(instance) <
            high.fish_per_school);

        const auto direction =
            ocean_life_instance_direction(instance);
        CHECK(std::isfinite(direction.x));
        CHECK(std::isfinite(direction.y));
        CHECK(
            glm::length(direction) ==
            doctest::Approx(1.0F).epsilon(0.0001));

        const auto color =
            ocean_life_instance_color(instance);
        CHECK(std::isfinite(color.x));
        CHECK(std::isfinite(color.y));
        CHECK(std::isfinite(color.z));
        CHECK(color.x >= 0.0F);
        CHECK(color.y >= 0.0F);
        CHECK(color.z >= 0.0F);
        CHECK(color.x <= 1.0F);
        CHECK(color.y <= 1.0F);
        CHECK(color.z <= 1.0F);

        const auto identity =
            (ocean_life_instance_school_id(instance) << 3U) |
            ocean_life_instance_member_index(instance);
        CHECK(instance_ids.insert(identity).second);
    }
    CHECK(minimum_school_depth >= 0.55F);
    CHECK(maximum_school_depth <= 9.55F);
    CHECK(maximum_school_depth - minimum_school_depth > 3.0F);
}

TEST_CASE("chaque qualite respecte ses nombres de bancs et de poissons") {
    const auto sampler =
        make_ocean_life_surface_sampler(
            deep_ocean_surface);
    OceanLifeField field {};

    for (const auto quality : {
             RendererQuality::High,
             RendererQuality::Medium,
             RendererQuality::Low,
         }) {
        const auto budget =
            ocean_life_budget_for_quality(
                quality);
        const auto frame =
            field.sample(
                WorldGenerationProfile::OceanAdventure,
                7788U,
                {4.0F, 48.0F, -7.0F},
                17.0F,
                budget,
                sampler);
        CHECK(frame.school_count == budget.max_schools);
        CHECK(
            frame.instances.size() ==
            budget.max_schools *
                budget.fish_per_school);
    }
}

TEST_CASE("les budgets hostiles restent bornes sans allocation non controlee") {
    const auto sampler =
        make_ocean_life_surface_sampler(
            deep_ocean_surface);
    OceanLifeField field {};
    const auto oversized =
        field.sample(
            WorldGenerationProfile::OceanAdventure,
            0xffffffffU,
            {},
            9.0F,
            {
                std::numeric_limits<std::size_t>::max(),
                std::numeric_limits<std::size_t>::max(),
                100'000.0F,
            },
            sampler);
    CHECK(
        oversized.school_count ==
        kOceanLifeMaximumSchoolCount);
    CHECK(
        oversized.instances.size() ==
        kOceanLifeMaximumInstanceCount);
    OceanLifeField maximum_radius_field {};
    const auto maximum_radius =
        maximum_radius_field.sample(
            WorldGenerationProfile::OceanAdventure,
            0xffffffffU,
            {},
            9.0F,
            {
                kOceanLifeMaximumSchoolCount,
                kOceanLifeMaximumFishPerSchool,
                56.0F,
            },
            sampler);
    CHECK(oversized == maximum_radius);

    CHECK(
        field.sample(
                 WorldGenerationProfile::OceanAdventure,
                 1U,
                 {},
                 9.0F,
                 {0U, 8U, 56.0F},
                 sampler)
            .instances
            .empty());
    CHECK(
        field.sample(
                 WorldGenerationProfile::OceanAdventure,
                 1U,
                 {},
                 9.0F,
                 {
                     6U,
                     8U,
                     std::numeric_limits<float>::quiet_NaN(),
                 },
                 sampler)
            .instances
            .empty());
    CHECK(
        field.sample(
                 WorldGenerationProfile::OceanAdventure,
                 1U,
                 {},
                 9.0F,
                 {6U, 8U, 56.0F},
                 {})
            .instances
            .empty());
}

TEST_CASE("le champ reste visuel et exclusif a aventure en mer") {
    int sample_count = 0;
    const auto counting_surface =
        [&sample_count](int, int) noexcept {
            ++sample_count;
            return deep_ocean_surface(0, 0);
        };
    const auto sampler =
        make_ocean_life_surface_sampler(
            counting_surface);
    const auto budget =
        ocean_life_budget_for_quality(
            RendererQuality::High);
    OceanLifeField field {};

    CHECK(
        field.sample(
                 WorldGenerationProfile::Continental,
                 5U,
                 {},
                 1.0F,
                 budget,
                 sampler)
            .instances
            .empty());
    CHECK(sample_count == 0);

    CHECK(
        field.sample(
                 WorldGenerationProfile::OceanAdventure,
                 5U,
                 {
                     std::numeric_limits<float>::quiet_NaN(),
                     0.0F,
                     0.0F,
                 },
                 1.0F,
                 budget,
                 sampler)
            .instances
            .empty());
    CHECK(sample_count == 0);

    CHECK(
        field.sample(
                 WorldGenerationProfile::OceanAdventure,
                 5U,
                 {},
                 std::numeric_limits<float>::infinity(),
                 budget,
                 sampler)
            .instances
            .empty());
    CHECK(sample_count == 0);
}

TEST_CASE("la terre et les fonds trop proches excluent les poissons") {
    OceanLifeField field {};
    const auto budget =
        ocean_life_budget_for_quality(
            RendererQuality::High);
    const auto land_sampler =
        make_ocean_life_surface_sampler(
            land_surface);
    const auto shallow_sampler =
        make_ocean_life_surface_sampler(
            shallow_surface);

    CHECK(
        field.sample(
                 WorldGenerationProfile::OceanAdventure,
                 91U,
                 {},
                 3.0F,
                 budget,
                 land_sampler)
            .instances
            .empty());
    CHECK(
        field.sample(
                 WorldGenerationProfile::OceanAdventure,
                 91U,
                 {},
                 3.0F,
                 budget,
                 shallow_sampler)
            .instances
            .empty());

    const auto half_ocean =
        [](int world_x, int) noexcept {
            return world_x >= 0
                       ? deep_ocean_surface(0, 0)
                       : land_surface(0, 0);
        };
    const auto mixed_sampler =
        make_ocean_life_surface_sampler(
            half_ocean);
    const auto mixed =
        field.sample(
            WorldGenerationProfile::OceanAdventure,
            91U,
            {0.0F, 40.0F, 0.0F},
            3.0F,
            budget,
            mixed_sampler);
    REQUIRE_FALSE(mixed.instances.empty());
    for (const auto& instance : mixed.instances) {
        CHECK(instance.position.x >= 0.0F);
        CHECK(instance.position.y > 11.0F);
        CHECK(instance.position.y < 21.0F);
    }
}

TEST_CASE("le mouvement analytique reste continu et conserve les identites") {
    const auto sampler =
        make_ocean_life_surface_sampler(
            deep_ocean_surface);
    const auto budget =
        ocean_life_budget_for_quality(
            RendererQuality::High);
    OceanLifeField first_field {};
    OceanLifeField later_field {};
    const auto first =
        first_field.sample(
            WorldGenerationProfile::OceanAdventure,
            3344U,
            {7.0F, 52.0F, -3.0F},
            80.0F,
            budget,
            sampler);
    const auto later =
        later_field.sample(
            WorldGenerationProfile::OceanAdventure,
            3344U,
            {7.0F, 52.0F, -3.0F},
            80.05F,
            budget,
            sampler);

    REQUIRE(first.instances.size() == later.instances.size());
    auto moved = false;
    for (std::size_t index = 0U;
         index < first.instances.size();
         ++index) {
        const auto& before =
            first.instances[index];
        const auto& after =
            later.instances[index];
        CHECK(
            ocean_life_instance_school_id(before) ==
            ocean_life_instance_school_id(after));
        CHECK(
            ocean_life_instance_member_index(before) ==
            ocean_life_instance_member_index(after));
        CHECK(before.scale == after.scale);
        CHECK(
            before.animation_phase ==
            after.animation_phase);
        const auto displacement =
            glm::length(
                after.position -
                before.position);
        CHECK(displacement < 0.08F);
        moved = moved ||
                displacement > 0.00001F;
    }
    CHECK(moved);
}

TEST_CASE("franchir une frontiere de cellule conserve les bancs mondiaux") {
    const auto sampler =
        make_ocean_life_surface_sampler(
            deep_ocean_surface);
    const auto budget =
        ocean_life_budget_for_quality(
            RendererQuality::High);
    OceanLifeField before_field {};
    OceanLifeField after_field {};
    const auto before =
        before_field.sample(
            WorldGenerationProfile::OceanAdventure,
            9981U,
            {15.99F, 55.0F, 3.0F},
            41.0F,
            budget,
            sampler);
    const auto after =
        after_field.sample(
            WorldGenerationProfile::OceanAdventure,
            9981U,
            {16.01F, 55.0F, 3.0F},
            41.0F,
            budget,
            sampler);

    const auto before_ids =
        school_ids(before);
    const auto after_ids =
        school_ids(after);
    std::vector<std::uint32_t> common {};
    std::set_intersection(
        before_ids.begin(),
        before_ids.end(),
        after_ids.begin(),
        after_ids.end(),
        std::back_inserter(common));

    CHECK(before_ids.size() == budget.max_schools);
    CHECK(after_ids.size() == budget.max_schools);
    CHECK(common.size() >= budget.max_schools - 1U);
}

} // namespace valcraft
