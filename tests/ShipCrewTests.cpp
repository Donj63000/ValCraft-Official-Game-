#include "creatures/CrewAnimation.h"
#include "gameplay/MusketCombat.h"
#include "gameplay/SeaAdventure.h"
#include "gameplay/ShipCrew.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <utility>

namespace valcraft {

namespace {

auto station_position(ShipCrewStation station) -> glm::vec3 {
    for (const auto& node : amelie_ship_blueprint().crew_navigation_nodes) {
        if (node.station == station) {
            return node.local_position;
        }
    }
    return {};
}

void place_at_station(ShipCrewMemberSaveState& member, ShipCrewStation station) {
    member.local_position = station_position(station);
    member.current_station = station;
    member.next_station = station;
    member.destination_station = station;
}

auto part_uses_tile_for_test(
    const CreaturePartInstance& part,
    CreatureAtlasTile tile) noexcept -> bool {

    const auto coordinates =
        creature_atlas_tile_coordinates(tile);
    const auto uv_step =
        1.0F /
        kCreatureAtlasTilesPerAxis;
    return
        part.face_uvs[0].u0 ==
            static_cast<float>(
                coordinates[0]) *
                uv_step &&
        part.face_uvs[0].v0 ==
            static_cast<float>(
                coordinates[1]) *
                uv_step;
}

auto update_for(ShipCrewSystem& crew,
                const ShipEntity& ship,
                const EnvironmentState& environment,
                float seconds,
                std::uint32_t& fish,
                std::uint32_t& water) -> ShipCrewUpdateResult {
    ShipCrewUpdateResult combined {};
    auto remaining = seconds;
    while (remaining > 0.0F) {
        const auto step = std::min(remaining, 0.25F);
        const auto result = crew.update(ship, environment, step, fish, water);
        combined.fish_delivered = combined.fish_delivered || result.fish_delivered;
        combined.water_delivered = combined.water_delivered || result.water_delivered;
        remaining -= step;
    }
    return combined;
}

[[nodiscard]] auto interior_half_width_for_test(
    const ShipProtectionProfile& profile,
    float local_y,
    float local_z) noexcept -> float {

    auto half_width =
        profile.half_width_at(
            local_z);
    if (local_y <
        profile.middle_hull_min_y) {
        half_width =
            std::max(
                profile.lower_minimum_half_width,
                half_width -
                    profile.lower_width_inset);
    } else if (
        local_y <
        profile.upper_hull_min_y) {
        half_width =
            std::max(
                profile.middle_minimum_half_width,
                half_width -
                    profile.middle_width_inset);
    }
    const auto thickness =
        std::min(
            0.44F,
            std::max(
                0.22F,
                half_width *
                    0.36F));
    return std::max(
        0.48F,
        half_width -
            thickness);
}

} // namespace

TEST_CASE("L'Amelie expose un graphe d'equipage stable et connexe") {
    const auto& blueprint = amelie_ship_blueprint();
    CHECK(std::string(blueprint.name) == "L'Amélie");
    REQUIRE(blueprint.crew_navigation_nodes.size() == static_cast<std::size_t>(ShipCrewStation::Count));
    CHECK(
        blueprint.navigation_revision ==
        11'548'132'376'574'709'708ULL);

    std::array<bool, static_cast<std::size_t>(ShipCrewStation::Count)> present {};
    for (const auto& node : blueprint.crew_navigation_nodes) {
        const auto index = static_cast<std::size_t>(node.station);
        REQUIRE(index < present.size());
        CHECK_FALSE(present[index]);
        present[index] = true;
        CHECK(std::isfinite(node.local_position.x));
        CHECK(std::isfinite(node.local_position.y));
        CHECK(std::isfinite(node.local_position.z));
    }
    for (const auto value : present) {
        CHECK(value);
    }

    std::array<bool, static_cast<std::size_t>(ShipCrewStation::Count)> visited {};
    std::queue<ShipCrewStation> pending;
    pending.push(ShipCrewStation::Helm);
    visited[static_cast<std::size_t>(ShipCrewStation::Helm)] = true;
    while (!pending.empty()) {
        const auto current = pending.front();
        pending.pop();
        for (const auto& edge : blueprint.crew_navigation_edges) {
            auto neighbor = ShipCrewStation::Count;
            if (edge.first == current) neighbor = edge.second;
            if (edge.second == current) neighbor = edge.first;
            const auto index = static_cast<std::size_t>(neighbor);
            if (index < visited.size() && !visited[index]) {
                visited[index] = true;
                pending.push(neighbor);
            }
        }
    }
    for (const auto value : visited) {
        CHECK(value);
    }
}

TEST_CASE("la refonte de L'Amelie ferme la poupe et libere la hauteur des marins") {
    const auto& blueprint = amelie_ship_blueprint();
    CHECK(blueprint.bounds.max.x - blueprint.bounds.min.x >= 17.0F);
    CHECK(blueprint.bounds.min.z <= -35.0F);
    CHECK(blueprint.bounds.max.z >= 45.0F);

    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    const auto origin = ship.world_origin();
    for (float z = -34.0F; z <= -31.0F; z += 0.5F) {
        const auto feet = origin + glm::vec3 {0.0F, 1.01F, z};
        const auto support = ship.support_height_in_range(feet, feet.y - 0.10F, feet.y + 0.10F);
        REQUIRE_MESSAGE(support.has_value(), "le plancher de la cabine doit atteindre la poupe");
    }
    CHECK(ship.intersects_aabb(
        origin + glm::vec3 {-0.25F, 3.00F, -35.75F},
        origin + glm::vec3 {0.25F, 3.50F, -35.55F}));

    for (const auto& node : blueprint.crew_navigation_nodes) {
        CAPTURE(static_cast<int>(node.station));
        const auto feet = origin + node.local_position;
        const auto support = ship.support_height_in_range(feet, feet.y - 0.16F, feet.y + 0.16F);
        CHECK_MESSAGE(support.has_value(), "chaque station d'equipage doit avoir un support");
        CHECK_FALSE(ship.intersects_aabb(
            feet + glm::vec3 {-0.24F, 0.04F, -0.24F},
            feet + glm::vec3 {0.24F, 1.84F, 0.24F}));
    }
}

TEST_CASE("L'Amelie contient trois ponts interieurs equipes sans collision decorative") {
    const auto& blueprint = amelie_ship_blueprint();

    CHECK(blueprint.parts.size() <= 3'300U);
    CHECK(blueprint.protection_profile.lower_hull_min_y <= -5.80F);
    CHECK(
        blueprint.anchors.safe_spawn ==
        (glm::vec3 {0.5F, 4.10F, -7.5F}));
    CHECK(
        blueprint.anchors.lower_deck ==
        (glm::vec3 {0.0F, 1.01F, -7.5F}));
    CHECK(
        blueprint.anchors.captain_cabin ==
        (glm::vec3 {0.0F, 1.01F, -22.0F}));
    CHECK(
        blueprint.anchors.crew_quarters ==
        (glm::vec3 {0.0F, 1.01F, -5.0F}));
    CHECK(
        blueprint.anchors.galley ==
        (glm::vec3 {0.0F, 1.01F, 5.0F}));
    CHECK(
        blueprint.anchors.cargo_hold ==
        (glm::vec3 {0.0F, 1.01F, 23.0F}));
    CHECK(
        blueprint.anchors.helm ==
        (glm::vec3 {-1.5F, 4.51F, -29.0F}));
    CHECK(
        blueprint.anchors.aft_hatch ==
        (glm::vec3 {0.0F, 4.01F, -7.5F}));
    CHECK(
        blueprint.anchors.fore_hatch ==
        (glm::vec3 {0.0F, 4.01F, 14.5F}));

    auto cannon_barrel_count = std::size_t {0U};
    auto navy_sail_panel_count = std::size_t {0U};
    auto obsolete_gold_part_count = std::size_t {0U};
    for (const auto& part : blueprint.parts) {
        if (part.material == ShipMaterial::Iron &&
            part.shape == ShipPartShape::Segment &&
            std::abs(part.thickness - 0.46F) <= 1.0e-4F &&
            std::abs(part.local_start.y - 1.78F) <= 1.0e-4F &&
            std::abs(part.local_end.y - 1.78F) <= 1.0e-4F) {
            ++cannon_barrel_count;
        }
        if (part.material == ShipMaterial::BlackCanvas &&
            part.shape == ShipPartShape::Panel) {
            ++navy_sail_panel_count;
        }
        if (part.material == ShipMaterial::SolidGold) {
            ++obsolete_gold_part_count;
        }

        // Je garde les cordages et les voiles purement visuels afin qu'ils ne
        // creent jamais de mur invisible autour des mats ou dans les coursives.
        if (part.material == ShipMaterial::Rope ||
            part.material == ShipMaterial::BlackCanvas) {
            CHECK_FALSE(part.collidable);
            CHECK_FALSE(part.supports_player);
        }
        if (part.shape == ShipPartShape::ChamferedBox ||
            part.shape == ShipPartShape::DrapedPanel ||
            part.material == ShipMaterial::Linen ||
            part.material == ShipMaterial::BurgundyTextile ||
            part.material == ShipMaterial::NavyTextile ||
            part.material == ShipMaterial::Paper ||
            part.material == ShipMaterial::Ceramic) {
            CHECK_FALSE(part.collidable);
            CHECK_FALSE(part.supports_player);
        }
    }

    CHECK(cannon_barrel_count == 12U);
    CHECK(navy_sail_panel_count >= 9U);
    CHECK(obsolete_gold_part_count == 0U);

    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    const auto origin = ship.world_origin();
    const std::array interior_anchors {
        blueprint.anchors.lower_deck,
        blueprint.anchors.captain_cabin,
        blueprint.anchors.crew_quarters,
        blueprint.anchors.galley,
        blueprint.anchors.cargo_hold,
    };
    for (const auto& anchor : interior_anchors) {
        CAPTURE(anchor.x);
        CAPTURE(anchor.y);
        CAPTURE(anchor.z);
        const auto feet = origin + anchor;
        CHECK(ship.support_height_in_range(
                  feet,
                  feet.y - 0.16F,
                  feet.y + 0.16F)
                  .has_value());
        CHECK(blueprint.protection_profile.shelters_from_weather_local(anchor));
    }

    const std::array gameplay_anchors {
        blueprint.anchors.safe_spawn,
        blueprint.anchors.lower_deck,
        blueprint.anchors.captain_cabin,
        blueprint.anchors.crew_quarters,
        blueprint.anchors.galley,
        blueprint.anchors.cargo_hold,
        blueprint.anchors.helm,
        blueprint.anchors.aft_hatch,
        blueprint.anchors.fore_hatch,
    };
    for (const auto& anchor : gameplay_anchors) {
        const auto feet =
            origin +
            anchor;
        CAPTURE(anchor.x);
        CAPTURE(anchor.y);
        CAPTURE(anchor.z);
        CHECK(
            ship.support_height_in_range(
                feet,
                feet.y - 0.16F,
                feet.y + 0.16F)
                .has_value());
        CHECK_FALSE(
            ship.intersects_aabb(
                feet +
                    glm::vec3 {
                        -0.34F,
                        0.04F,
                        -0.34F,
                    },
                feet +
                    glm::vec3 {
                        0.34F,
                        1.88F,
                        0.34F,
                    }));
    }

    CHECK_FALSE(blueprint.protection_profile.shelters_from_weather_local(
        {20.0F, -4.99F, 0.0F}));
}

TEST_CASE("les douze canons disposent de vrais sabords ouverts et encadres") {
    const auto& blueprint =
        amelie_ship_blueprint();
    const auto opening_count =
        std::ranges::count_if(
            blueprint.parts,
            [](const ShipPart& part) {
                return part.shape ==
                       ShipPartShape::Opening;
            });
    const auto side_window_count =
        std::ranges::count_if(
            blueprint.parts,
            [](const ShipPart& part) {
                const auto normal =
                    glm::abs(
                        part.orientation);
                const auto center =
                    (part.local_start +
                     part.local_end) *
                    0.5F;
                return part.shape ==
                           ShipPartShape::Panel &&
                       part.material ==
                           ShipMaterial::Glass &&
                       normal.x >= normal.y &&
                       normal.x >= normal.z &&
                       std::abs(center.x) >
                           0.20F &&
                       center.y <
                           4.0F;
            });
    CHECK(opening_count == 12);
    CHECK(side_window_count == 8);

    ShipEntity ship {};
    ship.set_position(
        {0.5F, 49.0F, 0.5F});
    const auto origin =
        ship.world_origin();
    constexpr std::array cannon_rows {
        -15.5F,
        -9.5F,
        -3.5F,
        4.5F,
        10.5F,
        16.5F,
    };
    for (const auto local_z :
         cannon_rows) {
        for (const auto side_sign :
             {-1.0F, 1.0F}) {
            const auto outer_half_width =
                blueprint.protection_profile
                    .half_width_at(
                        local_z);
            const auto wall_x =
                side_sign *
                (outer_half_width -
                 0.20F);
            CAPTURE(local_z);
            CAPTURE(side_sign);

            const auto matching_opening =
                std::ranges::any_of(
                    blueprint.parts,
                    [&](const ShipPart& part) {
                        const auto center =
                            (part.local_start +
                             part.local_end) *
                            0.5F;
                        return part.shape ==
                                   ShipPartShape::Opening &&
                               part.orientation.x *
                                       side_sign >
                                   0.90F &&
                               std::abs(
                                   center.z -
                                   local_z) <
                                   0.01F;
                    });
            CHECK(matching_opening);
            CHECK_FALSE(
                ship.intersects_aabb(
                    origin +
                        glm::vec3 {
                            wall_x - 0.04F,
                            1.62F,
                            local_z - 0.12F,
                        },
                    origin +
                        glm::vec3 {
                            wall_x + 0.04F,
                            1.94F,
                            local_z + 0.12F,
                        }));
            CHECK(
                ship.intersects_aabb(
                    origin +
                        glm::vec3 {
                            wall_x - 0.04F,
                            1.12F,
                            local_z - 0.12F,
                        },
                    origin +
                        glm::vec3 {
                            wall_x + 0.04F,
                            1.24F,
                            local_z + 0.12F,
                        }));
            CHECK(
                ship.intersects_aabb(
                    origin +
                        glm::vec3 {
                            wall_x - 0.04F,
                            2.34F,
                            local_z - 0.12F,
                        },
                    origin +
                        glm::vec3 {
                            wall_x + 0.04F,
                            2.46F,
                            local_z + 0.12F,
                        }));

            const auto side_sample_z =
                local_z +
                0.66F;
            const auto side_wall_x =
                side_sign *
                (blueprint.protection_profile
                     .half_width_at(
                         side_sample_z) -
                 0.20F);
            CHECK(
                ship.intersects_aabb(
                    origin +
                        glm::vec3 {
                            side_wall_x - 0.04F,
                            1.66F,
                            side_sample_z - 0.06F,
                        },
                    origin +
                        glm::vec3 {
                            side_wall_x + 0.04F,
                            1.90F,
                            side_sample_z + 0.06F,
                        }));
        }
    }
}

TEST_CASE("les cloisons ferment chaque piece jusqu'aux deux murailles") {
    struct BulkheadSample {
        float floor_y;
        float ceiling_y;
        float z;
        float door_center_x;
        float door_half_width;
    };
    constexpr std::array bulkheads {
        BulkheadSample {1.00F, 3.65F, -25.75F, 0.30F, 1.05F},
        BulkheadSample {1.00F, 3.65F, -19.50F, 0.00F, 1.65F},
        BulkheadSample {1.00F, 3.65F, 20.25F, 0.00F, 1.10F},
        BulkheadSample {-2.00F, 0.72F, -19.00F, 0.00F, 1.10F},
        BulkheadSample {-2.00F, 0.72F, -10.00F, -0.78F, 1.15F},
        BulkheadSample {-2.00F, 0.72F, 2.30F, 1.20F, 1.05F},
        BulkheadSample {-2.00F, 0.72F, 15.00F, 0.00F, 1.10F},
        BulkheadSample {-5.00F, -2.28F, -23.00F, 0.00F, 1.05F},
        BulkheadSample {-5.00F, -2.28F, -10.00F, 2.80F, 1.05F},
        BulkheadSample {-5.00F, -2.28F, 2.50F, 3.05F, 1.05F},
        BulkheadSample {-5.00F, -2.28F, 15.20F, 0.00F, 1.15F},
    };

    ShipEntity ship {};
    ship.set_position(
        {0.5F, 49.0F, 0.5F});
    const auto origin =
        ship.world_origin();
    const auto& profile =
        amelie_ship_blueprint()
            .protection_profile;
    const auto interior_half_width =
        [&](float local_y,
            float local_z) {
            return interior_half_width_for_test(
                profile,
                local_y,
                local_z);
        };

    for (const auto& bulkhead :
         bulkheads) {
        CAPTURE(bulkhead.z);
        CHECK_FALSE(
            ship.intersects_aabb(
                origin +
                    glm::vec3 {
                        bulkhead.door_center_x -
                            0.30F,
                        bulkhead.floor_y +
                            0.04F,
                        bulkhead.z -
                            0.04F,
                    },
                origin +
                    glm::vec3 {
                        bulkhead.door_center_x +
                            0.30F,
                        bulkhead.floor_y +
                            1.88F,
                        bulkhead.z +
                            0.26F,
                    }));
        const auto solid_x =
            bulkhead.door_center_x +
            bulkhead.door_half_width +
            0.45F;
        CHECK(
            ship.intersects_aabb(
                origin +
                    glm::vec3 {
                        solid_x - 0.08F,
                        bulkhead.floor_y +
                            0.55F,
                        bulkhead.z +
                            0.06F,
                    },
                origin +
                    glm::vec3 {
                        solid_x + 0.08F,
                        bulkhead.floor_y +
                            0.85F,
                        bulkhead.z +
                            0.16F,
                    }));

        const auto upper_y =
            bulkhead.ceiling_y -
            0.24F;
        const auto edge_x =
            interior_half_width(
                upper_y,
                bulkhead.z) -
            0.08F;
        for (const auto side_sign :
             {-1.0F, 1.0F}) {
            CHECK(
                ship.intersects_aabb(
                    origin +
                        glm::vec3 {
                            side_sign * edge_x -
                                0.05F,
                            upper_y - 0.08F,
                            bulkhead.z +
                                0.06F,
                        },
                    origin +
                        glm::vec3 {
                            side_sign * edge_x +
                                0.05F,
                            upper_y + 0.08F,
                            bulkhead.z +
                                0.16F,
                        }));
        }
    }
}

TEST_CASE("les bouchains et les fins de pont ferment toute la coque intérieure") {
    ShipEntity ship {};
    ship.set_position(
        {0.5F, 49.0F, 0.5F});
    const auto origin =
        ship.world_origin();
    const auto& blueprint =
        amelie_ship_blueprint();
    const auto& profile =
        blueprint.protection_profile;

    constexpr std::array sample_z {
        -28.0F,
        0.0F,
        21.35F,
    };
    const std::array transitions {
        profile.middle_hull_min_y,
        profile.upper_hull_min_y,
    };
    for (const auto local_z :
         sample_z) {
        for (const auto transition_y :
             transitions) {
            const auto narrower_inner =
                interior_half_width_for_test(
                    profile,
                    transition_y - 0.02F,
                    local_z);
            const auto wider_inner =
                interior_half_width_for_test(
                    profile,
                    transition_y + 0.02F,
                    local_z);
            const auto probe_x =
                (narrower_inner +
                 wider_inner) *
                0.5F;
            for (const auto side_sign :
                 {-1.0F, 1.0F}) {
                CAPTURE(local_z);
                CAPTURE(transition_y);
                CAPTURE(side_sign);
                CHECK(
                    ship.intersects_aabb(
                        origin +
                            glm::vec3 {
                                side_sign * probe_x -
                                    0.055F,
                                transition_y - 0.045F,
                                local_z - 0.08F,
                            },
                        origin +
                            glm::vec3 {
                                side_sign * probe_x +
                                    0.055F,
                                transition_y + 0.045F,
                                local_z + 0.08F,
                            }));
            }
        }
    }

    struct TerminalBulkhead {
        float floor_y;
        float ceiling_y;
        float z;
    };
    constexpr std::array terminal_bulkheads {
        TerminalBulkhead {-5.00F, -2.28F, -33.0F},
        TerminalBulkhead {-5.00F, -2.28F, 24.0F},
        TerminalBulkhead {-2.00F, 0.72F, -34.0F},
        TerminalBulkhead {-2.00F, 0.72F, 31.0F},
        TerminalBulkhead {1.00F, 3.65F, 34.0F},
    };
    for (const auto& bulkhead :
         terminal_bulkheads) {
        const auto local_y =
            (bulkhead.floor_y +
             bulkhead.ceiling_y) *
            0.5F;
        const auto half_width =
            interior_half_width_for_test(
                profile,
                local_y,
                bulkhead.z);
        for (const auto x_ratio :
             {-0.90F, 0.0F, 0.90F}) {
            CAPTURE(bulkhead.z);
            CAPTURE(x_ratio);
            const auto local_x =
                half_width *
                x_ratio;
            CHECK(
                ship.intersects_aabb(
                    origin +
                        glm::vec3 {
                            local_x - 0.08F,
                            local_y - 0.12F,
                            bulkhead.z - 0.04F,
                        },
                    origin +
                        glm::vec3 {
                            local_x + 0.08F,
                            local_y + 0.12F,
                            bulkhead.z + 0.04F,
                        }));
        }
    }

    const auto cabin_stern_feet =
        origin +
        glm::vec3 {
            0.0F,
            1.01F,
            -35.25F,
        };
    CHECK(
        ship.support_height_in_range(
                cabin_stern_feet,
                cabin_stern_feet.y -
                    0.12F,
                cabin_stern_feet.y +
                    0.12F)
            .has_value());
}

TEST_CASE("les 56 routes gardent le volume du marin dégagé dans les deux sens") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    const auto origin = ship.world_origin();
    const auto& blueprint = amelie_ship_blueprint();
    constexpr std::array stepped_edges {
        std::pair {30U, 31U},
        std::pair {32U, 33U},
        std::pair {16U, 17U},
        std::pair {17U, 18U},
        std::pair {19U, 20U},
        std::pair {20U, 21U},
        std::pair {42U, 43U},
        std::pair {43U, 44U},
        std::pair {45U, 46U},
        std::pair {46U, 47U},
    };

    std::ostringstream failures;
    for (const auto& edge : blueprint.crew_navigation_edges) {
        for (int direction = 0;
             direction < 2;
             ++direction) {
            const auto first =
                direction == 0
                    ? edge.first
                    : edge.second;
            const auto second =
                direction == 0
                    ? edge.second
                    : edge.first;
            const auto start =
                station_position(first);
            const auto end =
                station_position(second);
            const auto stepped_edge =
                std::ranges::any_of(
                    stepped_edges,
                    [first, second](const auto& candidate) {
                        const auto first_index =
                            static_cast<unsigned int>(first);
                        const auto second_index =
                            static_cast<unsigned int>(second);
                        return
                            (candidate.first == first_index &&
                             candidate.second == second_index) ||
                            (candidate.first == second_index &&
                             candidate.second == first_index);
                    });
            const auto sample_count =
                std::max(
                    1,
                    static_cast<int>(
                        std::ceil(
                            glm::length(
                                end -
                                start) /
                            0.20F)));
            auto edge_reported = false;
            for (int sample = 0;
                 sample <= sample_count;
                 ++sample) {
                const auto amount =
                    static_cast<float>(sample) /
                    static_cast<float>(sample_count);
                const auto feet =
                    origin +
                    start +
                    (end - start) *
                        amount;
                const auto supported =
                    ship.support_height_in_range(
                            feet,
                            feet.y -
                                (stepped_edge
                                     ? 0.62F
                                     : 0.16F),
                            feet.y +
                                (stepped_edge
                                     ? 0.62F
                                     : 0.16F))
                        .has_value();
                const auto blocked =
                    ship.intersects_aabb(
                        feet +
                            glm::vec3 {
                                -0.34F,
                                // Je réserve sur les marches le volume bas au
                                // franchissement déjà validé par les scénarios
                                // joueur des quatre escaliers. Le buste et la
                                // tête restent balayés sur chaque échantillon.
                                stepped_edge
                                    ? 0.80F
                                    : 0.04F,
                                -0.34F,
                            },
                        feet +
                            glm::vec3 {
                                0.34F,
                                1.88F,
                                0.34F,
                            });
                if ((!supported ||
                     blocked) &&
                    !edge_reported) {
                    failures
                        << static_cast<int>(first)
                        << "->"
                        << static_cast<int>(second)
                        << " sample="
                        << sample
                        << " position=("
                        << feet.x
                        << ','
                        << feet.y
                        << ','
                        << feet.z
                        << ") support="
                        << supported
                        << " blocked="
                        << blocked
                        << '\n';
                    edge_reported = true;
                }
            }
        }
    }
    CHECK_MESSAGE(failures.str().empty(), failures.str());
}

TEST_CASE("le roster canonique contient un capitaine et cinq matelots distincts") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(1337, ship);

    const auto members = crew.members();
    const auto render = crew.render_instances();
    REQUIRE(members.size() == kShipCrewMemberCount);
    REQUIRE(render.size() == kShipCrewMemberCount);
    const std::array<ShipCrewRole, kShipCrewMemberCount> expected {{
        ShipCrewRole::Captain,
        ShipCrewRole::Fisher,
        ShipCrewRole::Rigger,
        ShipCrewRole::WaterTender,
        ShipCrewRole::Deckhand,
        ShipCrewRole::Quartermaster,
    }};
    for (std::size_t index = 0; index < members.size(); ++index) {
        CHECK(members[index].id == index);
        CHECK(members[index].role == expected[index]);
        CHECK(members[index].health == doctest::Approx(ship_crew_max_health(expected[index])));
        CHECK(render[index].appearance_seed != 0U);
        if (index > 0U) {
            CHECK(render[index].appearance_seed != render[index - 1U].appearance_seed);
        }
    }
}

TEST_CASE("les marins restent exactement dans le repere local du navire") {
    constexpr float kRadiansPerDegree =
        0.01745329251994329577F;

    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ship.set_ocean_pose(0.0F, 0.0F, 0.0F);
    ship.synchronize_motion_history();

    ShipCrewSystem crew {};
    crew.reset(731, ship);
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, {}, 0.0F, fish, water);
    const auto local_before =
        crew.members()[0].local_position;
    const auto visual_local_before =
        ship.world_to_local_point(
            crew.render_instances()[0].position);

    ship.begin_motion_step();
    ship.set_position({0.5F, 49.0F, 500'000.5F});
    ship.set_ocean_pose(
        0.45F,
        6.0F * kRadiansPerDegree,
        -9.0F * kRadiansPerDegree);
    (void)crew.update(ship, {}, 0.0F, fish, water);

    CHECK(crew.members()[0].local_position.x ==
          doctest::Approx(local_before.x));
    CHECK(crew.members()[0].local_position.y ==
          doctest::Approx(local_before.y));
    CHECK(crew.members()[0].local_position.z ==
          doctest::Approx(local_before.z));

    const auto expected_world =
        ship.local_to_world_point(
            visual_local_before);
    const auto& render =
        crew.render_instances()[0];
    CHECK(render.position.x ==
          doctest::Approx(expected_world.x).epsilon(0.0001F));
    CHECK(render.position.y ==
          doctest::Approx(expected_world.y).epsilon(0.0001F));
    CHECK(render.position.z ==
          doctest::Approx(expected_world.z).epsilon(0.0001F));

    const auto& expected_orientation =
        ship.orientation();
    const auto quaternion_alignment =
        std::abs(
            render.platform_orientation.w *
                expected_orientation.w +
            render.platform_orientation.x *
                expected_orientation.x +
            render.platform_orientation.y *
                expected_orientation.y +
            render.platform_orientation.z *
                expected_orientation.z);
    CHECK(quaternion_alignment ==
          doctest::Approx(1.0F).epsilon(0.0001F));
}

TEST_CASE("la marche reste alignee avec l'avant reel du corps") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(732, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.next_station = ShipCrewStation::MidDeckPort;
    fisher.destination_station = ShipCrewStation::MidDeckPort;
    fisher.yaw_radians = 3.14159265358979323846F;
    crew.load_state(state, 732, ship);

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    const auto initial_position = crew.members()[1].local_position;

    // Un angle important doit d'abord provoquer un pivot, jamais un glissement.
    (void)crew.update(ship, {}, 0.05F, fish, water);
    CHECK(glm::length(crew.members()[1].local_position - initial_position) < 1.0e-5F);

    auto observed_translation = false;
    for (int frame = 0; frame < 160; ++frame) {
        const auto before = crew.members()[1].local_position;
        (void)crew.update(ship, {}, 0.05F, fish, water);
        const auto after = crew.members()[1].local_position;
        auto displacement = after - before;
        displacement.y = 0.0F;
        if (glm::dot(displacement, displacement) <= 1.0e-8F) {
            continue;
        }

        displacement = glm::normalize(displacement);
        const auto yaw = crew.members()[1].yaw_radians;

        // Le modele regarde localement vers +X. Apres une rotation GLM autour
        // de +Y, son avant monde vaut (cos(yaw), 0, -sin(yaw)).
        const glm::vec3 body_forward {
            std::cos(yaw),
            0.0F,
            -std::sin(yaw),
        };
        CHECK(glm::dot(body_forward, displacement) >= 0.939F);
        observed_translation = true;
    }
    CHECK(observed_translation);
}

TEST_CASE("la phase spatiale verrouille le pied puis se fige et reprend sans saut") {
    constexpr float kFrameSeconds = 1.0F / 60.0F;
    struct LocomotionCase {
        ShipCrewCargo cargo;
        CrewVisualActivity activity;
        CrewGaitStyle style;
    };
    constexpr std::array<LocomotionCase, 2> kCases {{
        {ShipCrewCargo::None, CrewVisualActivity::Walk, CrewGaitStyle::Walk},
        {ShipCrewCargo::Fish, CrewVisualActivity::Carry, CrewGaitStyle::Carry},
    }};

    for (std::size_t case_index = 0; case_index < kCases.size(); ++case_index) {
        ShipEntity ship {};
        ship.set_position({0.5F, 49.0F, 0.5F});
        ShipCrewSystem crew {};
        crew.reset(8'100 + static_cast<std::uint32_t>(case_index), ship);

        auto state = crew.save_state();
        auto& fisher = state.members[1];
        place_at_station(fisher, ShipCrewStation::PortFishing);
        fisher.cargo = kCases[case_index].cargo;
        fisher.activity = kCases[case_index].cargo == ShipCrewCargo::None
                              ? ShipCrewActivity::Idle
                              : ShipCrewActivity::Carry;
        fisher.next_station = ShipCrewStation::MidDeckPort;
        fisher.destination_station = ShipCrewStation::MidDeckPort;
        auto route_direction =
            station_position(ShipCrewStation::MidDeckPort) -
            station_position(ShipCrewStation::PortFishing);
        route_direction.y = 0.0F;
        route_direction = glm::normalize(route_direction);
        fisher.yaw_radians =
            std::atan2(-route_direction.z, route_direction.x);
        crew.load_state(
            state,
            8'100 + static_cast<std::uint32_t>(case_index),
            ship);

        std::uint32_t fish = 0U;
        std::uint32_t water = 0U;
        auto travelled_distance = 0.0F;
        auto support_checks = 0U;
        const auto initial_activity_phase =
            crew.render_instances()[1].activity_phase;
        auto previous_member = crew.members()[1];
        const auto initial_render = crew.render_instances()[1];
        auto previous_pose = sample_crew_locomotion(
            initial_render.locomotion_phase,
            initial_render.motion_amount,
            kCases[case_index].style);

        const auto foot_position = [](
                                       const ShipCrewMemberSaveState& member,
                                       const CrewLegPose& leg,
                                       std::size_t leg_index) {
            const glm::vec3 forward {
                std::cos(member.yaw_radians),
                0.0F,
                -std::sin(member.yaw_radians),
            };
            const glm::vec3 lateral {
                std::sin(member.yaw_radians),
                0.0F,
                std::cos(member.yaw_radians),
            };
            const auto side = leg_index == 0U ? -1.0F : 1.0F;
            return member.local_position +
                   forward * (leg.foot_center.x + leg.foot_pivot.x) +
                   lateral * (side * 0.10F);
        };

        for (int frame = 0; frame < 240 && travelled_distance < 0.75F; ++frame) {
            (void)crew.update(ship, {}, kFrameSeconds, fish, water);
            const auto& current_member = crew.members()[1];
            const auto& current_render = crew.render_instances()[1];
            const auto step_distance =
                glm::length(current_member.local_position - previous_member.local_position);
            travelled_distance += step_distance;

            CAPTURE(case_index);
            CAPTURE(frame);
            CHECK(current_render.activity == kCases[case_index].activity);
            CHECK(std::abs(
                      current_render.activity_phase -
                      initial_activity_phase) <= 1.0e-6F);
            CHECK(current_render.locomotion_phase >= 0.0F);
            CHECK(current_render.locomotion_phase < 1.0F);
            const auto expected_phase =
                std::fmod(travelled_distance, kCrewLocomotionCycleDistance) /
                kCrewLocomotionCycleDistance;
            CHECK(std::abs(
                      current_render.locomotion_phase -
                      expected_phase) <= 2.0e-4F);

            const auto current_pose = sample_crew_locomotion(
                current_render.locomotion_phase,
                current_render.motion_amount,
                kCases[case_index].style);
            for (std::size_t leg_index = 0; leg_index < 2U; ++leg_index) {
                if (!previous_pose.legs[leg_index].supporting ||
                    !current_pose.legs[leg_index].supporting ||
                    glm::length(
                        previous_pose.legs[leg_index].foot_pivot -
                        current_pose.legs[leg_index].foot_pivot) > 1.0e-5F ||
                    step_distance <= 0.0F) {
                    continue;
                }
                auto foot_delta =
                    foot_position(current_member, current_pose.legs[leg_index], leg_index) -
                    foot_position(previous_member, previous_pose.legs[leg_index], leg_index);
                foot_delta.y = 0.0F;
                CHECK(glm::length(foot_delta) <= 0.03F);
                ++support_checks;
            }

            previous_member = current_member;
            previous_pose = current_pose;
        }
        REQUIRE(travelled_distance >= 0.75F);
        REQUIRE(support_checks > 10U);

        const auto frozen_position = crew.members()[1].local_position;
        const auto frozen_phase = crew.render_instances()[1].locomotion_phase;
        auto remaining_direction =
            station_position(ShipCrewStation::MidDeckPort) - frozen_position;
        remaining_direction.y = 0.0F;
        remaining_direction = glm::normalize(remaining_direction);
        const auto blocker_world_position = ship.local_to_world_point(
            frozen_position + remaining_direction * 0.72F);

        for (int frame = 0; frame < 75; ++frame) {
            (void)crew.update(
                ship,
                {},
                kFrameSeconds,
                fish,
                water,
                blocker_world_position);
            CAPTURE(case_index);
            CAPTURE(frame);
            CHECK(glm::length(crew.members()[1].local_position - frozen_position) <
                  1.0e-5F);
            CHECK(std::abs(
                      crew.render_instances()[1].locomotion_phase -
                      frozen_phase) <= 1.0e-6F);
        }

        const auto stopped_render = crew.render_instances()[1];
        const auto stopped_pose = sample_crew_locomotion(
            stopped_render.locomotion_phase,
            stopped_render.motion_amount,
            kCases[case_index].style);
        CHECK(stopped_render.motion_amount < 1.0e-4F);
        for (const auto& leg : stopped_pose.legs) {
            CHECK(leg.supporting);
            CHECK(leg.sole_height <= 0.025F);
        }

        auto resumed = false;
        for (int frame = 0; frame < 30; ++frame) {
            const auto before = crew.members()[1].local_position;
            const auto phase_before = crew.render_instances()[1].locomotion_phase;
            (void)crew.update(ship, {}, kFrameSeconds, fish, water);
            const auto moved = glm::length(crew.members()[1].local_position - before);
            if (moved <= 0.0F) {
                continue;
            }
            const auto phase_after = crew.render_instances()[1].locomotion_phase;
            const auto phase_advance =
                std::fmod(phase_after - phase_before + 1.0F, 1.0F);
            CHECK(std::abs(
                      phase_advance -
                      moved / kCrewLocomotionCycleDistance) <= 2.0e-4F);
            CHECK(phase_advance < 0.01F);
            resumed = true;
            break;
        }
        CHECK(resumed);
    }
}

TEST_CASE("un chargement reprend en double appui sans sauvegarder la phase") {
    constexpr std::array<ShipCrewCargo, 2> kCargoCases {{
        ShipCrewCargo::None,
        ShipCrewCargo::Fish,
    }};
    for (std::size_t case_index = 0; case_index < kCargoCases.size(); ++case_index) {
        ShipEntity ship {};
        ship.set_position({0.5F, 49.0F, 0.5F});
        const auto seed = 8'250U + static_cast<std::uint32_t>(case_index);
        ShipCrewSystem source {};
        source.reset(seed, ship);
        auto state = source.save_state();
        auto& fisher = state.members[1];
        place_at_station(fisher, ShipCrewStation::PortFishing);
        fisher.cargo = kCargoCases[case_index];
        fisher.activity = kCargoCases[case_index] == ShipCrewCargo::None
                              ? ShipCrewActivity::Idle
                              : ShipCrewActivity::Carry;
        fisher.next_station = ShipCrewStation::MidDeckPort;
        fisher.destination_station = ShipCrewStation::MidDeckPort;
        auto direction =
            station_position(ShipCrewStation::MidDeckPort) -
            station_position(ShipCrewStation::PortFishing);
        direction.y = 0.0F;
        direction = glm::normalize(direction);
        fisher.yaw_radians = std::atan2(-direction.z, direction.x);
        source.load_state(state, seed, ship);

        std::uint32_t fish = 0U;
        std::uint32_t water = 0U;
        for (int frame = 0; frame < 180; ++frame) {
            (void)source.update(ship, {}, 1.0F / 60.0F, fish, water);
            if (source.render_instances()[1].locomotion_phase > 0.15F &&
                source.render_instances()[1].motion_amount > 0.50F) {
                break;
            }
        }
        REQUIRE(source.render_instances()[1].locomotion_phase > 0.15F);
        const auto saved_position = source.members()[1].local_position;
        const auto saved = source.save_state();

        ShipCrewSystem restored {};
        restored.load_state(saved, seed, ship);
        const auto& initial_render = restored.render_instances()[1];
        CAPTURE(case_index);
        CHECK(glm::length(restored.members()[1].local_position - saved_position) <
              1.0e-5F);
        CHECK(std::abs(initial_render.locomotion_phase) <= 1.0e-6F);
        CHECK(std::abs(initial_render.motion_amount) <= 1.0e-6F);
        const auto style = kCargoCases[case_index] == ShipCrewCargo::None
                               ? CrewGaitStyle::Walk
                               : CrewGaitStyle::Carry;
        const auto initial_pose = sample_crew_locomotion(
            initial_render.locomotion_phase,
            initial_render.motion_amount,
            style);
        for (const auto& leg : initial_pose.legs) {
            CHECK(leg.supporting);
            CHECK(leg.sole_height <= 0.025F);
        }

        auto resumed = false;
        for (int frame = 0; frame < 30; ++frame) {
            const auto before = restored.members()[1].local_position;
            (void)restored.update(ship, {}, 1.0F / 60.0F, fish, water);
            const auto moved =
                glm::length(restored.members()[1].local_position - before);
            if (moved <= 0.0F) {
                continue;
            }
            CHECK(std::abs(
                      restored.render_instances()[1].locomotion_phase -
                      moved / kCrewLocomotionCycleDistance) <= 2.0e-4F);
            CHECK(restored.render_instances()[1].locomotion_phase < 0.01F);
            resumed = true;
            break;
        }
        CHECK(resumed);
    }
}

TEST_CASE("les quatre escaliers conservent la demarche sous tangage et roulis") {
    struct StairRoute {
        ShipCrewStation start;
        ShipCrewStation middle;
        ShipCrewStation destination;
    };
    constexpr std::array<StairRoute, 4> kRoutes {{
        {
            ShipCrewStation::AftStairsTop,
            ShipCrewStation::AftStairsMid,
            ShipCrewStation::AftStairsBottom,
        },
        {
            ShipCrewStation::ForeStairsTop,
            ShipCrewStation::ForeStairsMid,
            ShipCrewStation::ForeStairsBottom,
        },
        {
            ShipCrewStation::CrewStairsTop,
            ShipCrewStation::CrewStairsMid,
            ShipCrewStation::CrewStairsBottom,
        },
        {
            ShipCrewStation::HoldStairsTop,
            ShipCrewStation::HoldStairsMid,
            ShipCrewStation::HoldStairsBottom,
        },
    }};
    constexpr std::array<ShipCrewStation, 5> kParkingStations {{
        ShipCrewStation::Helm,
        ShipCrewStation::PortFishing,
        ShipCrewStation::WaterStill,
        ShipCrewStation::Capstan,
        ShipCrewStation::CargoFish,
    }};

    for (std::size_t route_index = 0; route_index < kRoutes.size(); ++route_index) {
        ShipEntity ship {};
        ship.set_position({0.5F, 49.0F, 0.5F});
        ship.set_ocean_pose(0.38F, 0.085F, -0.11F);
        ship.synchronize_motion_history();

        ShipCrewSystem crew {};
        const auto seed = 8'300U + static_cast<std::uint32_t>(route_index);
        crew.reset(seed, ship);
        auto state = crew.save_state();
        for (std::size_t member_index = 0; member_index < state.members.size(); ++member_index) {
            if (member_index == 4U) {
                continue;
            }
            const auto parking_index = member_index < 4U ? member_index : 4U;
            place_at_station(state.members[member_index], kParkingStations[parking_index]);
            state.members[member_index].health = 0.0F;
            state.members[member_index].recovery_timer = 60.0F;
        }

        auto& deckhand = state.members[4];
        place_at_station(deckhand, kRoutes[route_index].start);
        deckhand.next_station = kRoutes[route_index].middle;
        deckhand.destination_station = kRoutes[route_index].destination;
        deckhand.activity = ShipCrewActivity::Idle;
        auto first_direction =
            station_position(kRoutes[route_index].middle) -
            station_position(kRoutes[route_index].start);
        first_direction.y = 0.0F;
        first_direction = glm::normalize(first_direction);
        deckhand.yaw_radians =
            std::atan2(-first_direction.z, first_direction.x);
        crew.load_state(state, seed, ship);

        std::uint32_t fish = 0U;
        std::uint32_t water = 0U;
        auto travelled_distance = 0.0F;
        auto minimum_height = deckhand.local_position.y;
        auto maximum_height = deckhand.local_position.y;
        auto visited_middle = false;
        auto reached_destination = false;
        auto previous_position = crew.members()[4].local_position;

        for (int frame = 0; frame < 720; ++frame) {
            (void)crew.update(ship, {}, 1.0F / 60.0F, fish, water);
            const auto& member = crew.members()[4];
            const auto& render = crew.render_instances()[4];
            const auto step_distance =
                glm::length(member.local_position - previous_position);
            travelled_distance += step_distance;
            previous_position = member.local_position;
            minimum_height = std::min(minimum_height, member.local_position.y);
            maximum_height = std::max(maximum_height, member.local_position.y);
            visited_middle =
                visited_middle || member.current_station == kRoutes[route_index].middle;

            CAPTURE(route_index);
            CAPTURE(frame);
            CHECK(std::isfinite(render.locomotion_phase));
            CHECK(render.locomotion_phase >= 0.0F);
            CHECK(render.locomotion_phase < 1.0F);
            const auto expected_phase =
                std::fmod(travelled_distance, kCrewLocomotionCycleDistance) /
                kCrewLocomotionCycleDistance;
            CHECK(std::abs(render.locomotion_phase - expected_phase) <= 2.0e-4F);

            const auto pose = sample_crew_locomotion(
                render.locomotion_phase,
                render.motion_amount,
                CrewGaitStyle::Walk);
            CHECK(pose.legs[0].sole_height >= -0.01F);
            CHECK(pose.legs[1].sole_height >= -0.01F);
            CHECK((pose.legs[0].sole_height <= 0.025F ||
                   pose.legs[1].sole_height <= 0.025F));

            const auto& orientation = ship.orientation();
            const auto orientation_alignment = std::abs(
                render.platform_orientation.w * orientation.w +
                render.platform_orientation.x * orientation.x +
                render.platform_orientation.y * orientation.y +
                render.platform_orientation.z * orientation.z);
            CHECK(orientation_alignment ==
                  doctest::Approx(1.0F).epsilon(0.0001F));

            if (member.current_station == kRoutes[route_index].destination) {
                reached_destination = true;
                break;
            }
        }

        CHECK(visited_middle);
        CHECK(reached_destination);
        CHECK(maximum_height - minimum_height >= 2.95F);
    }
}


TEST_CASE("une cargaison reste visible pendant tout le trajet") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(733, ship);

    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.cargo = ShipCrewCargo::Fish;
    fisher.activity = ShipCrewActivity::Carry;
    fisher.routine_step = 1U;
    fisher.destination_station = ShipCrewStation::CargoFish;
    crew.load_state(state, 733, ship);

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    auto observed_walk = false;
    for (int frame = 0; frame < 240; ++frame) {
        (void)crew.update(ship, {}, 0.05F, fish, water);
        const auto& render = crew.render_instances()[1];
        if (render.motion_amount <= 0.05F) {
            continue;
        }

        CHECK(render.activity == CrewVisualActivity::Carry);
        observed_walk = true;
        break;
    }
    CHECK(observed_walk);
}

TEST_CASE("un marin cede le passage au joueur puis repart") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(734, ship);

    auto state = crew.save_state();
    auto& deckhand = state.members[4];
    place_at_station(deckhand, ShipCrewStation::AftDeck);
    deckhand.next_station = ShipCrewStation::MizzenMast;
    deckhand.destination_station = ShipCrewStation::MizzenMast;
    deckhand.yaw_radians = -1.57079632679489661923F;
    crew.load_state(state, 734, ship);

    const auto start = crew.members()[4].local_position;
    auto direction = station_position(ShipCrewStation::MizzenMast) - start;
    direction.y = 0.0F;
    direction = glm::normalize(direction);
    const auto player_position =
        ship.world_origin() + start + direction * 0.72F;

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    for (int frame = 0; frame < 30; ++frame) {
        (void)crew.update(
            ship,
            {},
            0.05F,
            fish,
            water,
            player_position);
    }
    CHECK(glm::length(crew.members()[4].local_position - start) < 1.0e-4F);

    for (int frame = 0; frame < 30; ++frame) {
        (void)crew.update(ship, {}, 0.05F, fish, water);
    }
    CHECK(glm::length(crew.members()[4].local_position - start) > 0.15F);
}

TEST_CASE("viser un marin expose son role sa tache et sa progression") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(735, ship);

    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.activity = ShipCrewActivity::Fish;
    fisher.work_progress = kAutomaticFishWorkSeconds * 0.50F;
    crew.load_state(state, 735, ship);

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, {}, 0.0F, fish, water);

    const auto target =
        crew.render_instances()[1].position +
        glm::vec3 {0.0F, 0.95F, 0.0F};
    const auto origin =
        target + glm::vec3 {0.0F, 0.0F, -3.0F};
    const auto focus = crew.focus_from_ray(
        ship,
        origin,
        {0.0F, 0.0F, 1.0F},
        5.0F);

    REQUIRE(focus.visible);
    CHECK(focus.role == ShipCrewRole::Fisher);
    CHECK(focus.activity == ShipCrewActivity::Fish);
    CHECK(focus.has_progress);
    CHECK(focus.progress_ratio == doctest::Approx(0.50F));
}

TEST_CASE("les reservations de passage ne bloquent pas la production a long terme") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(736, ship);

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    const auto result = update_for(
        crew,
        ship,
        {},
        600.0F,
        fish,
        water);

    CHECK(result.fish_delivered);
    CHECK(result.water_delivered);
    CHECK(fish > 0U);
    CHECK(water > 0U);
}


TEST_CASE("le pecheur reprend sa production apres une tempete") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(737, ship);

    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::MessTable);
    fisher.routine_step = 2U;
    fisher.activity = ShipCrewActivity::Rest;
    crew.load_state(state, 737, ship);

    EnvironmentState storm {};
    storm.storm_intensity = 0.80F;
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, storm, 0.25F, fish, water);

    CHECK(crew.members()[1].destination_station == ShipCrewStation::MainMast);
    CHECK(crew.members()[1].activity == ShipCrewActivity::HaulRope);

    (void)crew.update(ship, {}, 0.25F, fish, water);
    CHECK(crew.members()[1].destination_station == ShipCrewStation::PortFishing);
    CHECK(crew.members()[1].activity == ShipCrewActivity::Fish);
    CHECK(crew.members()[1].routine_step == 0U);
}


TEST_CASE("le poisson est credite uniquement apres son transport dans la cale") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(91, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.activity = ShipCrewActivity::Fish;
    fisher.routine_step = 0U;
    fisher.work_progress = kAutomaticFishWorkSeconds - 0.10F;
    crew.load_state(state, 91, ship);

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, {}, 0.05F, fish, water);
    CHECK(fish == 0U);
    CHECK(crew.members()[1].cargo == ShipCrewCargo::None);
    (void)crew.update(ship, {}, 0.05F, fish, water);
    CHECK(fish == 0U);
    CHECK(crew.members()[1].cargo == ShipCrewCargo::Fish);

    // Je laisse au pêcheur le temps de franchir les deux nouvelles volées
    // d'escalier avant d'atteindre la cale.
    const auto result = update_for(crew, ship, {}, 240.0F, fish, water);
    CHECK(result.fish_delivered);
    CHECK(fish == 1U);
    CHECK(crew.members()[1].cargo == ShipCrewCargo::None);
}

TEST_CASE("l'eau profite de la pluie mais respecte son objectif automatique") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(92, ship);
    auto state = crew.save_state();
    auto& tender = state.members[3];
    place_at_station(tender, ShipCrewStation::WaterStill);
    tender.activity = ShipCrewActivity::TendWater;
    tender.routine_step = 0U;
    tender.work_progress = kAutomaticWaterWorkSeconds - 0.30F;
    crew.load_state(state, 92, ship);

    EnvironmentState rain {};
    rain.precipitation_intensity = 1.0F;
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, rain, 0.10F, fish, water);
    CHECK(crew.members()[3].cargo == ShipCrewCargo::None);
    (void)crew.update(ship, rain, 0.11F, fish, water);
    CHECK(crew.members()[3].cargo == ShipCrewCargo::Water);
    CHECK(water == 0U);
    const auto delivered = update_for(crew, ship, rain, 120.0F, fish, water);
    CHECK(delivered.water_delivered);
    CHECK(water == 1U);

    state = crew.save_state();
    auto& full_tender = state.members[3];
    full_tender.work_progress = 200.0F;
    full_tender.cargo = ShipCrewCargo::None;
    full_tender.routine_step = 0U;
    place_at_station(full_tender, ShipCrewStation::WaterStill);
    crew.load_state(state, 92, ship);
    water = kAutomaticWaterTarget;
    (void)crew.update(ship, rain, 0.25F, fish, water);
    CHECK(crew.members()[3].work_progress == doctest::Approx(0.0F));
    CHECK(water == kAutomaticWaterTarget);
}

TEST_CASE("dt nul et tempete forte ne font pas progresser la production") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(51, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.work_progress = 42.0F;
    fisher.activity = ShipCrewActivity::Fish;
    crew.load_state(state, 51, ship);
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;

    (void)crew.update(ship, {}, 0.0F, fish, water);
    CHECK(crew.members()[1].work_progress == doctest::Approx(42.0F));
    EnvironmentState storm {};
    storm.storm_intensity = 0.80F;
    (void)update_for(crew, ship, storm, 5.0F, fish, water);
    CHECK(crew.members()[1].work_progress == doctest::Approx(42.0F));
    CHECK(fish == 0U);
}

TEST_CASE("un marin assomme conserve son travail puis recupere sans mourir") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(404, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.work_progress = 57.0F;
    crew.load_state(state, 404, ship);

    const auto target = crew.render_instances()[1].position + glm::vec3 {0.0F, 0.95F, 0.0F};
    const auto origin = target + glm::vec3 {0.0F, 0.0F, -3.0F};
    const auto hit = crew.try_damage_from_player(ship, origin, {0.0F, 0.0F, 1.0F}, 5.0F, 100.0F);
    REQUIRE(hit.hit);
    CHECK(hit.knocked_out);
    CHECK(hit.member_id == 1U);

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)update_for(crew, ship, {}, 29.75F, fish, water);
    CHECK(crew.members()[1].health == doctest::Approx(0.0F));
    CHECK(crew.members()[1].work_progress == doctest::Approx(57.0F));

    const auto saved_while_down = crew.save_state();
    ShipCrewSystem restored {};
    restored.load_state(saved_while_down, 404, ship);
    (void)update_for(restored, ship, {}, 0.25F, fish, water);
    CHECK(restored.members()[1].health == doctest::Approx(14.0F));
    CHECK(restored.members()[1].work_progress == doctest::Approx(57.0F));
    CHECK(restored.render_instances()[1].activity == CrewVisualActivity::Recover);
}

TEST_CASE("la requete de rayon equipage reste pure avant l'application unique des degats") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(4'404, ship);
    auto state = crew.save_state();
    place_at_station(
        state.members[1],
        ShipCrewStation::PortFishing);
    crew.load_state(state, 4'404, ship);

    std::array<float, kShipCrewMemberCount> initial_health {};
    for (std::size_t index = 0U;
         index < initial_health.size();
         ++index) {
        initial_health[index] =
            crew.members()[index].health;
    }
    const auto target =
        crew.render_instances()[1].position +
        glm::vec3 {0.0F, 0.95F, 0.0F};
    const auto origin =
        target +
        glm::vec3 {0.0F, 0.0F, -3.0F};

    const auto query =
        crew.raycast_first_living(
            ship,
            origin,
            {0.0F, 0.0F, 1.0F},
            5.0F);
    REQUIRE(query.hit);
    CHECK(query.member_id == 1U);
    CHECK(query.distance <= 5.0F);
    CHECK(
        glm::length(
            query.position -
            (origin +
             glm::vec3 {0.0F, 0.0F, 1.0F} *
                 query.distance)) <
        1.0e-5F);

    const auto repeated_query =
        crew.raycast_first_living(
            ship,
            origin,
            {0.0F, 0.0F, 1.0F},
            5.0F);
    REQUIRE(repeated_query.hit);
    CHECK(repeated_query.member_id == query.member_id);
    CHECK(repeated_query.distance ==
          doctest::Approx(query.distance));
    for (std::size_t index = 0U;
         index < initial_health.size();
         ++index) {
        CHECK(crew.members()[index].health ==
              doctest::Approx(initial_health[index]));
    }

    const auto wall_distance =
        std::max(query.distance - 0.20F, 0.0F);
    const std::array blocked_candidates {
        MusketHit {
            MusketHitKind::Crew,
            query.position,
            query.distance,
            query.member_id,
        },
        MusketHit {
            MusketHitKind::World,
            origin +
                glm::vec3 {0.0F, 0.0F, 1.0F} *
                    wall_distance,
            wall_distance,
            0U,
        },
    };
    const auto blocked_hit =
        select_nearest_musket_hit(
            blocked_candidates,
            5.0F);
    REQUIRE(blocked_hit.hit());
    CHECK(blocked_hit.kind == MusketHitKind::World);
    CHECK_FALSE(
        musket_hit_can_receive_damage(
            blocked_hit.kind));
    for (std::size_t index = 0U;
         index < initial_health.size();
         ++index) {
        // Je verifie que la requete d'equipage et la selection du mur
        // n'endommagent ni le marin masque, ni un autre membre du bord.
        CHECK(crew.members()[index].health ==
              doctest::Approx(initial_health[index]));
    }

    const std::array exposed_candidate {
        blocked_candidates[0],
    };
    const auto exposed_hit =
        select_nearest_musket_hit(
            exposed_candidate,
            5.0F);
    REQUIRE(exposed_hit.hit());
    REQUIRE(exposed_hit.kind == MusketHitKind::Crew);

    const auto applied =
        crew.apply_damage(
            static_cast<std::uint8_t>(
                exposed_hit.target_id),
            4.0F,
            exposed_hit.distance);
    REQUIRE(applied.hit);
    CHECK(applied.member_id == query.member_id);
    CHECK(applied.damage == doctest::Approx(4.0F));
    CHECK(applied.distance ==
          doctest::Approx(query.distance));
    CHECK(crew.members()[1].health ==
          doctest::Approx(initial_health[1] - 4.0F));
    for (std::size_t index = 0U;
         index < initial_health.size();
         ++index) {
        if (index != 1U) {
            CHECK(crew.members()[index].health ==
                  doctest::Approx(initial_health[index]));
        }
    }

    CHECK_FALSE(
        crew.apply_damage(
            255U,
            4.0F)
            .hit);
}

TEST_CASE("un marin assomme en transport reprend sa livraison apres rechargement") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(405, ship);

    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.cargo = ShipCrewCargo::Fish;
    fisher.activity = ShipCrewActivity::Carry;
    fisher.routine_step = 1U;
    fisher.destination_station = ShipCrewStation::CargoFish;
    crew.load_state(state, 405, ship);

    const auto target = crew.render_instances()[1].position + glm::vec3 {0.0F, 0.95F, 0.0F};
    const auto origin = target + glm::vec3 {0.0F, 0.0F, -3.0F};
    const auto hit = crew.try_damage_from_player(ship, origin, {0.0F, 0.0F, 1.0F}, 5.0F, 100.0F);
    REQUIRE(hit.hit);
    REQUIRE(hit.knocked_out);
    REQUIRE(crew.members()[1].cargo == ShipCrewCargo::Fish);

    ShipCrewSystem restored {};
    restored.load_state(crew.save_state(), 405, ship);
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)update_for(restored, ship, {}, kShipCrewKnockoutSeconds, fish, water);

    CHECK(restored.members()[1].health == doctest::Approx(ship_crew_max_health(ShipCrewRole::Fisher)));
    CHECK(restored.members()[1].cargo == ShipCrewCargo::Fish);
    CHECK(restored.members()[1].destination_station == ShipCrewStation::CargoFish);

    const auto delivered = update_for(restored, ship, {}, 120.0F, fish, water);
    CHECK(delivered.fish_delivered);
    CHECK(fish == 1U);
    CHECK(restored.members()[1].cargo == ShipCrewCargo::None);
}

TEST_CASE("le reveil est progressif et bloque le mouvement pendant toute son animation") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(406, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.health = 0.0F;
    fisher.recovery_timer = 0.25F;
    fisher.work_progress = 57.0F;
    crew.load_state(state, 406, ship);

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, {}, 0.25F, fish, water);
    const auto wake_position = crew.members()[1].local_position;
    CHECK(crew.render_instances()[1].activity == CrewVisualActivity::Recover);
    CHECK(crew.render_instances()[1].activity_phase == doctest::Approx(0.0F));

    (void)crew.update(ship, {}, 0.25F, fish, water);
    CHECK(crew.render_instances()[1].activity == CrewVisualActivity::Recover);
    CHECK(crew.render_instances()[1].activity_phase == doctest::Approx(0.25F));
    CHECK(glm::length(crew.members()[1].local_position - wake_position) < 1.0e-5F);
    CHECK(crew.members()[1].work_progress == doctest::Approx(57.0F));

    (void)update_for(crew, ship, {}, 0.50F, fish, water);
    CHECK(crew.render_instances()[1].activity == CrewVisualActivity::Recover);
    CHECK(crew.render_instances()[1].activity_phase == doctest::Approx(0.75F));
    CHECK(glm::length(crew.members()[1].local_position - wake_position) < 1.0e-5F);
}

TEST_CASE("le chargement repare les cargaisons et les aretes semantiquement invalides") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(407, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.cargo = ShipCrewCargo::Fish;
    fisher.activity = ShipCrewActivity::Idle;
    fisher.routine_step = 0U;

    auto& captain = state.members[0];
    place_at_station(captain, ShipCrewStation::Helm);
    captain.next_station = ShipCrewStation::CargoSort;
    captain.destination_station = ShipCrewStation::CargoSort;
    crew.load_state(state, 407, ship);

    CHECK(crew.members()[1].destination_station == ShipCrewStation::CargoFish);
    CHECK(crew.members()[1].activity == ShipCrewActivity::Carry);
    CHECK(crew.members()[1].routine_step == 1U);
    const auto& repaired_captain = crew.members()[0];
    CHECK(glm::length(repaired_captain.local_position - station_position(ShipCrewStation::Helm)) < 1.0e-5F);
    const auto adjacent = std::ranges::any_of(
        amelie_ship_blueprint().crew_navigation_edges,
        [&](const ShipCrewNavigationEdge& edge) {
            return (edge.first == repaired_captain.current_station && edge.second == repaired_captain.next_station) ||
                   (edge.second == repaired_captain.current_station && edge.first == repaired_captain.next_station);
        });
    CHECK(adjacent);
}

TEST_CASE("une revision de navigation migre les etats humains sans garder une cargaison obsolete") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(408, ship);
    auto state = crew.save_state();
    state.navigation_revision ^= 0x55U;
    state.members[0].health = 0.0F;
    state.members[0].recovery_timer = 12.0F;
    state.members[1].work_progress = 73.0F;
    state.members[1].cargo = ShipCrewCargo::Fish;
    crew.load_state(state, 408, ship);

    CHECK(crew.save_state().navigation_revision == amelie_ship_blueprint().navigation_revision);
    CHECK(crew.members()[0].health == doctest::Approx(0.0F));
    CHECK(crew.members()[0].recovery_timer == doctest::Approx(12.0F));
    CHECK(crew.members()[1].work_progress == doctest::Approx(73.0F));
    CHECK(crew.members()[1].cargo == ShipCrewCargo::None);
}

TEST_CASE("dt nul ne livre pas une cargaison et la meteo non finie reste neutre") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(409, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::CargoFish);
    fisher.cargo = ShipCrewCargo::Fish;
    auto& tender = state.members[3];
    place_at_station(tender, ShipCrewStation::WaterStill);
    tender.work_progress = 0.0F;
    crew.load_state(state, 409, ship);

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, {}, 0.0F, fish, water);
    CHECK(fish == 0U);
    CHECK(crew.members()[1].cargo == ShipCrewCargo::Fish);

    EnvironmentState invalid_environment {};
    invalid_environment.precipitation_intensity = std::numeric_limits<float>::quiet_NaN();
    invalid_environment.storm_intensity = std::numeric_limits<float>::quiet_NaN();
    invalid_environment.daylight_factor = std::numeric_limits<float>::quiet_NaN();
    (void)crew.update(ship, invalid_environment, 0.25F, fish, water);
    CHECK(crew.members()[3].work_progress == doctest::Approx(0.25F));
    CHECK(std::isfinite(crew.render_instances()[3].daylight_factor));
}

TEST_CASE("la peche ne montre la prise que pendant les dernieres secondes du cycle") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(410, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.activity = ShipCrewActivity::Fish;
    fisher.work_progress = 60.0F;
    crew.load_state(state, 410, ship);
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, {}, 0.25F, fish, water);
    CHECK(crew.render_instances()[1].activity == CrewVisualActivity::FishWait);

    state = crew.save_state();
    state.members[1].work_progress = kAutomaticFishWorkSeconds - 2.0F;
    crew.load_state(state, 410, ship);
    (void)crew.update(ship, {}, 0.25F, fish, water);
    CHECK(crew.render_instances()[1].activity == CrewVisualActivity::FishReel);
}

TEST_CASE("le navire bloque les coups portes a travers son pont") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(411, ship);
    auto state = crew.save_state();
    place_at_station(state.members[0], ShipCrewStation::CaptainCabin);
    crew.load_state(state, 411, ship);

    const auto origin = ship.world_origin() + glm::vec3 {0.0F, 6.0F, -22.0F};
    REQUIRE(ship.raycast_collidable_distance(origin, {0.0F, -1.0F, 0.0F}, 10.0F).has_value());
    const auto blocked = crew.try_damage_from_player(ship, origin, {0.0F, -1.0F, 0.0F}, 10.0F, 5.0F);
    CHECK_FALSE(blocked.hit);
}

TEST_CASE("les postes partages gardent des positions visuelles distinctes") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(412, ship);
    auto state = crew.save_state();
    place_at_station(state.members[2], ShipCrewStation::Capstan);
    place_at_station(state.members[4], ShipCrewStation::Capstan);
    crew.load_state(state, 412, ship);
    CHECK(glm::length(crew.render_instances()[2].position - crew.render_instances()[4].position) > 1.0F);
}

TEST_CASE("les lumieres d'equipage reutilisent exactement les lanternes des trois ponts") {
    const auto& blueprint = amelie_ship_blueprint();
    REQUIRE(blueprint.interior_lanterns.size() == 19U);
    std::array<std::size_t, 3> lights_by_deck {};
    for (const auto& lantern : blueprint.interior_lanterns) {
        const auto deck =
            lantern.local_position.y >= 0.86F
                ? 0U
                : lantern.local_position.y >= -2.14F
                      ? 1U
                      : 2U;
        ++lights_by_deck[deck];
        CHECK(lantern.radius >= 4.0F);
        CHECK(lantern.radius <= 7.0F);
        CHECK(lantern.intensity > 0.0F);
        CHECK(lantern.color.r > lantern.color.g);
        CHECK(lantern.color.g > lantern.color.b);
        CHECK(lantern.zone_min.x <= lantern.local_position.x);
        CHECK(lantern.zone_min.y <= lantern.local_position.y);
        CHECK(lantern.zone_min.z <= lantern.local_position.z);
        CHECK(lantern.zone_max.x >= lantern.local_position.x);
        CHECK(lantern.zone_max.y >= lantern.local_position.y);
        CHECK(lantern.zone_max.z >= lantern.local_position.z);
        const auto matching_part = std::ranges::any_of(blueprint.parts, [&](const ShipPart& part) {
            if (part.material != ShipMaterial::Lantern) {
                return false;
            }
            const auto center = (part.local_start + part.local_end) * 0.5F;
            return glm::length(center - lantern.local_position) < 0.01F;
        });
        CHECK(matching_part);
    }
    const std::array<std::size_t, 3> expected_lights {{
        7U,
        6U,
        6U,
    }};
    CHECK(lights_by_deck == expected_lights);
}

TEST_CASE("deux marins du pont gardent leurs couleurs et recoivent des fanaux selon leur distance") {
    ShipEntity ship {};
    ship.set_position(
        {0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(413, ship);
    auto state =
        crew.save_state();
    place_at_station(
        state.members[1],
        ShipCrewStation::PortFishing);
    place_at_station(
        state.members[2],
        ShipCrewStation::MainMast);
    crew.load_state(
        state,
        413,
        ship);

    EnvironmentState night {};
    night.daylight_factor = 0.0F;
    night.storm_intensity = 0.0F;
    night.cloud_intensity = 0.0F;
    night.overcast_intensity = 0.0F;
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(
        ship,
        night,
        0.0F,
        fish,
        water);

    const auto render =
        crew.render_instances();
    const auto& near_fisher =
        render[1];
    const auto& distant_rigger =
        render[2];
    CHECK(
        near_fisher.sky_light ==
        doctest::Approx(1.0F));
    CHECK(
        distant_rigger.sky_light ==
        doctest::Approx(1.0F));
    CHECK(
        near_fisher.local_light >
        distant_rigger.local_light);
    CHECK(
        distant_rigger.local_light >
        0.0F);

    const auto& blueprint =
        amelie_ship_blueprint();
    const auto activation =
        ship_exterior_light_activation(
            night.daylight_factor,
            night.storm_intensity,
            night.cloud_intensity,
            night.overcast_intensity);
    const auto expected_light =
        [&](const CrewRenderInstance& instance) {
            return std::clamp(
                ship_exterior_light_level(
                    blueprint.exterior_lanterns,
                    ship.world_to_local_point(
                        instance.position)) *
                    activation,
                0.0F,
                1.0F);
        };
    CHECK(
        near_fisher.local_light ==
        doctest::Approx(
            expected_light(
                near_fisher)));
    CHECK(
        distant_rigger.local_light ==
        doctest::Approx(
            expected_light(
                distant_rigger)));

    const auto fisher_parts =
        build_crew_parts(
            near_fisher);
    const auto rigger_parts =
        build_crew_parts(
            distant_rigger);
    REQUIRE_FALSE(
        fisher_parts.empty());
    REQUIRE_FALSE(
        rigger_parts.empty());
    CHECK(
        part_uses_tile_for_test(
            fisher_parts.front(),
            CreatureAtlasTile::CrewIvoryCloth));
    CHECK(
        part_uses_tile_for_test(
            rigger_parts.front(),
            CreatureAtlasTile::CrewStripedCloth));
    for (const auto& part :
         fisher_parts) {
        CHECK(
            part.block_light ==
            doctest::Approx(
                near_fisher.local_light));
    }
    for (const auto& part :
         rigger_parts) {
        CHECK(
            part.block_light ==
            doctest::Approx(
                distant_rigger.local_light));
    }
}

TEST_CASE("la sanitization restaure les identites et rejette les valeurs corrompues") {
    ShipCrewSaveState state {};
    state.initialized = true;
    state.navigation_revision = 7U;
    for (auto& member : state.members) {
        member.id = 0U;
        member.role = static_cast<ShipCrewRole>(255U);
        member.activity = static_cast<ShipCrewActivity>(255U);
        member.cargo = static_cast<ShipCrewCargo>(255U);
        member.current_station = static_cast<ShipCrewStation>(255U);
        member.local_position = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        member.health = std::numeric_limits<float>::infinity();
    }
    const auto sanitized = sanitize_ship_crew_save_state(state);
    REQUIRE(sanitized.initialized);
    for (std::size_t index = 0; index < sanitized.members.size(); ++index) {
        const auto& member = sanitized.members[index];
        CHECK(member.id == index);
        CHECK(static_cast<std::size_t>(member.role) == index);
        CHECK(std::isfinite(member.local_position.x));
        CHECK(std::isfinite(member.health));
        CHECK(member.cargo == ShipCrewCargo::None);
    }
}

} // namespace valcraft
