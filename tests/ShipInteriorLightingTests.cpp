#include "gameplay/SeaAdventure.h"

#include <doctest/doctest.h>
#include <glm/common.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

TEST_CASE("la lumière intérieure décroît sans traverser les limites de sa pièce") {
    const ShipInteriorLight light {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.68F, 0.38F},
        6.0F,
        1.0F,
        0.37F,
        {-2.0F, -1.0F, -3.0F},
        {2.0F, 1.0F, 3.0F},
        0.50F,
        {0.0F, 0.0F},
        {0.0F, 0.75F},
    };

    const auto center =
        ship_interior_light_attenuation(
            light,
            {0.0F, 0.0F, 0.0F});
    const auto middle =
        ship_interior_light_attenuation(
            light,
            {0.0F, 0.0F, 2.0F});
    const auto doorway_spill =
        ship_interior_light_attenuation(
            light,
            {0.0F, 0.0F, 3.20F});
    const auto wall_spill =
        ship_interior_light_attenuation(
            light,
            {1.50F, 0.0F, 3.20F});
    const auto next_room =
        ship_interior_light_attenuation(
            light,
            {0.0F, 0.0F, 3.60F});

    CHECK(center > middle);
    CHECK(middle > 0.0F);
    CHECK(doorway_spill > 0.0F);
    CHECK(doorway_spill < center);
    CHECK(wall_spill == doctest::Approx(0.0F));
    CHECK(next_room == doctest::Approx(0.0F));
}

TEST_CASE("le scintillement des lanternes est déterministe et reste discret") {
    const ShipInteriorLight light {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.70F, 0.42F},
        5.0F,
        1.0F,
        0.73F,
        glm::vec3 {-3.0F},
        glm::vec3 {3.0F},
        0.0F,
    };

    auto minimum = 10.0F;
    auto maximum = 0.0F;
    for (int sample = 0; sample < 240; ++sample) {
        const auto time =
            static_cast<float>(sample) /
            60.0F;
        const auto first =
            ship_interior_light_attenuation(
                light,
                glm::vec3 {0.0F},
                time);
        const auto second =
            ship_interior_light_attenuation(
                light,
                glm::vec3 {0.0F},
                time);
        CHECK(first == doctest::Approx(second));
        minimum = std::min(minimum, first);
        maximum = std::max(maximum, first);
    }

    CHECK(minimum >= 0.959F);
    CHECK(maximum <= 1.041F);
    CHECK(maximum - minimum > 0.025F);
}

TEST_CASE("les lanternes de l'Amelie ne débordent que par les portes") {
    const auto lights =
        amelie_interior_lights();
    REQUIRE(lights.size() == 19U);
    auto previous_seed =
        -1.0F;
    for (const auto& light :
         lights) {
        CHECK(light.radius >= 4.0F);
        CHECK(light.radius <= 7.0F);
        CHECK(light.intensity > 0.0F);
        CHECK(light.color.r >= light.color.g);
        CHECK(light.color.g >= light.color.b);
        CHECK(light.color.g >= 0.50F);
        CHECK(light.color.g <= 0.75F);
        CHECK(light.color.b >= 0.20F);
        CHECK(light.color.b <= 0.50F);
        CHECK(light.flicker_seed > previous_seed);
        CHECK(light.zone_min.x < light.zone_max.x);
        CHECK(light.zone_min.y < light.zone_max.y);
        CHECK(light.zone_min.z < light.zone_max.z);
        previous_seed =
            light.flicker_seed;
    }

    const auto stern_z =
        amelie_ship_blueprint()
            .protection_profile
            .stern_z;
    // Je raccorde les trois pièces arrière à la face intérieure exacte du
    // tableau. Aucun bordé ne peut ainsi retomber à noir comme un faux trou.
    for (const auto light_index :
         {0U, 7U, 13U}) {
        CHECK(
            lights[light_index].zone_min.z ==
            doctest::Approx(stern_z));
    }

    const auto& cabin_light =
        lights.front();
    const auto through_door =
        ship_interior_light_attenuation(
            cabin_light,
            {0.30F, 2.10F, -25.55F});
    const auto through_bulkhead =
        ship_interior_light_attenuation(
            cabin_light,
            {2.10F, 2.10F, -25.55F});
    const auto outside_hull_zone =
        ship_interior_light_attenuation(
            cabin_light,
            {8.20F, 2.10F, -29.00F});

    CHECK(through_door > 0.0F);
    CHECK(through_bulkhead == doctest::Approx(0.0F));
    CHECK(outside_hull_zone == doctest::Approx(0.0F));
}

TEST_CASE("une lumière intérieure corrompue ne propage aucune valeur non finie") {
    auto light = ShipInteriorLight {};
    light.local_position.x =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(
        ship_interior_light_attenuation(
            light,
            glm::vec3 {0.0F}) ==
        doctest::Approx(0.0F));

    light = ShipInteriorLight {};
    light.radius =
        std::numeric_limits<float>::infinity();
    CHECK(
        ship_interior_light_attenuation(
            light,
            glm::vec3 {0.0F}) ==
        doctest::Approx(0.0F));
}

TEST_CASE("les dix fanaux extérieurs partagent exactement leur lumière et leur géométrie") {
    const auto lights =
        amelie_exterior_lights();
    const auto& blueprint =
        amelie_ship_blueprint();
    REQUIRE(lights.size() == 10U);
    REQUIRE(
        blueprint.exterior_lanterns.size() ==
        lights.size());
    CHECK(
        blueprint.exterior_lanterns.data() ==
        lights.data());

    auto exterior_fixture_count =
        std::size_t {0U};
    for (const auto& part :
         blueprint.parts) {
        if (part.material ==
                ShipMaterial::Lantern &&
            (part.local_start.y +
             part.local_end.y) *
                    0.5F >=
                4.0F) {
            ++exterior_fixture_count;
        }
    }
    CHECK(exterior_fixture_count == 10U);

    for (auto index = std::size_t {0U};
         index < lights.size();
         ++index) {
        const auto& light =
            lights[index];
        CHECK(
            light.color.r ==
            doctest::Approx(1.00F));
        CHECK(
            light.color.g ==
            doctest::Approx(0.62F));
        CHECK(
            light.color.b ==
            doctest::Approx(0.30F));
        CHECK(
            light.minimum_y ==
            doctest::Approx(3.65F));
        CHECK(
            light.maximum_y ==
            doctest::Approx(7.50F));
        if (index < 8U) {
            CHECK(
                light.radius ==
                doctest::Approx(14.0F));
            CHECK(
                light.intensity ==
                doctest::Approx(0.95F));
            CHECK(
                light.fixture_half_extent.x ==
                doctest::Approx(0.15F));
            CHECK(
                light.fixture_half_extent.y ==
                doctest::Approx(0.275F));
            CHECK(
                light.fixture_half_extent.z ==
                doctest::Approx(0.14F));
        } else {
            CHECK(
                light.radius ==
                doctest::Approx(8.0F));
            CHECK(
                light.intensity ==
                doctest::Approx(0.85F));
            CHECK(
                light.fixture_half_extent.x ==
                doctest::Approx(0.25F));
            CHECK(
                light.fixture_half_extent.y ==
                doctest::Approx(0.425F));
            CHECK(
                light.fixture_half_extent.z ==
                doctest::Approx(0.20F));
        }

        auto matching_fixture_count =
            std::size_t {0U};
        for (const auto& part :
             blueprint.parts) {
            if (part.material !=
                    ShipMaterial::Lantern) {
                continue;
            }
            const auto center =
                (part.local_start +
                 part.local_end) *
                0.5F;
            const auto half_extent =
                glm::abs(
                    part.local_end -
                    part.local_start) *
                0.5F;
            if (glm::length(
                    center -
                    light.local_position) <
                    1.0e-4F &&
                glm::length(
                    half_extent -
                    light.fixture_half_extent) <
                    1.0e-4F) {
                ++matching_fixture_count;
                CHECK_FALSE(part.collidable);
                CHECK_FALSE(
                    part.supports_player);
            }
        }
        CHECK(matching_fixture_count == 1U);
    }
}

TEST_CASE("l'atténuation extérieure est verticale déterministe et agrégée par maximum") {
    const ShipExteriorLight light {
        {0.0F, 4.50F, 0.0F},
        {1.0F, 0.62F, 0.30F},
        10.0F,
        0.70F,
        3.65F,
        7.50F,
        {0.15F, 0.275F, 0.14F},
    };

    const auto center =
        ship_exterior_light_attenuation(
            light,
            light.local_position);
    const auto middle =
        ship_exterior_light_attenuation(
            light,
            light.local_position +
                glm::vec3 {5.0F, 0.0F, 0.0F});
    const auto edge =
        ship_exterior_light_attenuation(
            light,
            light.local_position +
                glm::vec3 {10.0F, 0.0F, 0.0F});
    const auto below_deck =
        ship_exterior_light_attenuation(
            light,
            {0.0F, 3.64F, 0.0F});
    const auto above_zone =
        ship_exterior_light_attenuation(
            light,
            {0.0F, 7.51F, 0.0F});

    CHECK(center == doctest::Approx(0.70F));
    CHECK(middle > 0.0F);
    CHECK(middle < center);
    CHECK(edge == doctest::Approx(0.0F));
    CHECK(
        below_deck ==
        doctest::Approx(0.0F));
    CHECK(
        above_zone ==
        doctest::Approx(0.0F));

    const std::array<ShipExteriorLight, 2>
        overlapping_lights {{
            light,
            light,
        }};
    CHECK(
        ship_exterior_light_level(
            overlapping_lights,
            light.local_position) ==
        doctest::Approx(0.70F));
    CHECK(
        ship_exterior_light_level(
            {},
            light.local_position) ==
        doctest::Approx(0.0F));
}

TEST_CASE("l'activation des fanaux utilise le ciel le plus couvert et résiste aux valeurs invalides") {
    CHECK(
        ship_exterior_light_activation(
            1.0F,
            0.0F,
            0.0F,
            0.0F) ==
        doctest::Approx(0.06F));
    CHECK(
        ship_exterior_light_activation(
            0.0F,
            0.0F,
            0.0F,
            0.0F) ==
        doctest::Approx(1.0F));
    CHECK(
        ship_exterior_light_activation(
            1.0F,
            1.0F,
            0.10F,
            1.0F) ==
        doctest::Approx(
            0.06F +
            0.94F * 0.45F));
    CHECK(
        ship_exterior_light_activation(
            0.80F,
            1.0F,
            0.90F,
            0.20F) ==
        doctest::Approx(
            0.06F +
            0.94F * 0.405F));

    const auto invalid =
        std::numeric_limits<float>::
            quiet_NaN();
    CHECK(
        ship_exterior_light_activation(
            invalid,
            invalid,
            invalid,
            invalid) ==
        doctest::Approx(0.06F));

    auto corrupted_light =
        ShipExteriorLight {};
    corrupted_light.radius =
        std::numeric_limits<float>::
            infinity();
    CHECK(
        ship_exterior_light_attenuation(
            corrupted_light,
            corrupted_light.local_position) ==
        doctest::Approx(0.0F));
}

} // namespace valcraft
