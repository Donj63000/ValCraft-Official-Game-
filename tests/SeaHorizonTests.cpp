#include "render/SeaHorizon.h"

#include "world/World.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace valcraft {
namespace {

[[nodiscard]] auto terrain_vertex_position(
    const SeaHorizonTerrainVertex& vertex) noexcept -> glm::vec3 {
    return {vertex.x, vertex.y, vertex.z};
}

[[nodiscard]] auto terrain_vertex_normal(
    const SeaHorizonTerrainVertex& vertex) noexcept -> glm::vec3 {
    return {
        unpack_sea_horizon_snorm(vertex.nx),
        unpack_sea_horizon_snorm(vertex.ny),
        unpack_sea_horizon_snorm(vertex.nz),
    };
}

[[nodiscard]] auto triangle_double_area_squared(
    const glm::vec3& first,
    const glm::vec3& second,
    const glm::vec3& third) noexcept -> float {
    const auto cross = glm::cross(second - first, third - first);
    return glm::dot(cross, cross);
}

} // namespace

TEST_CASE("le sommet d'horizon conserve un format compact stable") {
    CHECK(sizeof(SeaHorizonTerrainVertex) == 16U);
    CHECK(offsetof(SeaHorizonTerrainVertex, x) == 0U);
    CHECK(offsetof(SeaHorizonTerrainVertex, nx) == 12U);
    CHECK(offsetof(SeaHorizonTerrainVertex, block_id) == 15U);

    const auto packed = pack_sea_horizon_snorm(-0.75F);
    CHECK(
        unpack_sea_horizon_snorm(packed) ==
        doctest::Approx(-0.75F).epsilon(0.01F));
}

TEST_CASE("l'ancrage de l'horizon respecte la grille jusque dans les coordonnees negatives") {
    CHECK(
        sea_horizon_snapped_center({15.99F, 5.0F, 16.0F}) ==
        SeaHorizonSnappedCenter {16, 16});
    CHECK(
        sea_horizon_snapped_center({-0.01F, 5.0F, -16.0F}) ==
        SeaHorizonSnappedCenter {0, -16});
    CHECK(
        sea_horizon_snapped_center({-16.01F, 5.0F, -31.99F}) ==
        SeaHorizonSnappedCenter {-16, -32});

    const auto invalid = sea_horizon_snapped_center({
        std::numeric_limits<float>::quiet_NaN(),
        0.0F,
        std::numeric_limits<float>::infinity(),
    });
    CHECK(invalid == SeaHorizonSnappedCenter {});
}

TEST_CASE("l'hysteresis empeche le roulis de reconstruire le terrain lointain") {
    const SeaHorizonSnappedCenter origin {};

    CHECK(
        sea_horizon_stable_center(
            origin,
            {11.99F, 52.0F, -11.99F}) ==
        origin);
    CHECK(
        sea_horizon_stable_center(
            origin,
            {-11.99F, 52.0F, 11.99F}) ==
        origin);
    CHECK(
        sea_horizon_stable_center(
            origin,
            {12.01F, 52.0F, 0.0F}) ==
        SeaHorizonSnappedCenter {16, 0});
    CHECK(
        sea_horizon_stable_center(
            SeaHorizonSnappedCenter {16, 0},
            {4.01F, 52.0F, 0.0F}) ==
        SeaHorizonSnappedCenter {16, 0});
    CHECK(
        sea_horizon_stable_center(
            SeaHorizonSnappedCenter {16, 0},
            {3.99F, 52.0F, 0.0F}) ==
        SeaHorizonSnappedCenter {0, 0});

    const auto invalid = sea_horizon_stable_center(
        SeaHorizonSnappedCenter {48, -32},
        {
            std::numeric_limits<float>::quiet_NaN(),
            52.0F,
            std::numeric_limits<float>::infinity(),
        });
    CHECK(invalid == SeaHorizonSnappedCenter {48, -32});
}

TEST_CASE("le terrain lointain reste deterministe sans charger le moindre chunk") {
    World world(
        1337,
        5,
        WorldGenerationProfile::OceanAdventure,
        WorldGenerationVersion::SparseArchipelagoV2);
    const auto chunks_before = world.chunk_records().size();
    const glm::vec3 camera_position {0.5F, 55.0F, 0.5F};

    const auto first =
        build_sea_horizon_terrain_mesh(world, camera_position);
    const auto second =
        build_sea_horizon_terrain_mesh(world, camera_position);

    REQUIRE_FALSE(first.empty());
    CHECK(first == second);
    CHECK(world.chunk_records().size() == chunks_before);
    CHECK(world.pending_generation_count() == 0U);
    CHECK(first.vertices.size() > 0U);
    CHECK(first.vertices.size() <= kSeaHorizonMaxTerrainVertices);
    CHECK(first.triangle_count() > 0U);
    CHECK(first.triangle_count() <= kSeaHorizonMaxTerrainTriangles);
    CHECK(first.indices.size() % 3U == 0U);

    for (const auto& vertex : first.vertices) {
        CHECK(std::isfinite(vertex.x));
        CHECK(std::isfinite(vertex.y));
        CHECK(std::isfinite(vertex.z));

        const auto local_x =
            vertex.x - static_cast<float>(first.snapped_center.x);
        const auto local_z =
            vertex.z - static_cast<float>(first.snapped_center.z);
        CHECK(
            std::abs(local_x) <=
            kSeaHorizonTerrainOuterRadius +
                kSeaHorizonGridStep);
        CHECK(
            std::abs(local_z) <=
            kSeaHorizonTerrainOuterRadius +
                kSeaHorizonGridStep);

        const auto world_x = static_cast<int>(vertex.x);
        const auto world_z = static_cast<int>(vertex.z);
        const auto generated =
            world.sample_generated_surface(world_x, world_z);
        CHECK(
            vertex.y ==
            doctest::Approx(
                static_cast<float>(generated.surface_height + 1) +
                kSeaHorizonTerrainHeightBias));
        CHECK(vertex.block_id == generated.surface_block);

        const auto normal = terrain_vertex_normal(vertex);
        CHECK(std::isfinite(normal.x));
        CHECK(std::isfinite(normal.y));
        CHECK(std::isfinite(normal.z));
        CHECK(glm::length(normal) == doctest::Approx(1.0F).epsilon(0.02F));
        CHECK(normal.y > 0.0F);
    }

    auto emerged_triangle_count = std::size_t {0U};
    auto submerged_triangle_count = std::size_t {0U};
    for (std::size_t index = 0U;
         index < first.indices.size();
         index += 3U) {
        const auto first_index = first.indices[index];
        const auto second_index = first.indices[index + 1U];
        const auto third_index = first.indices[index + 2U];
        REQUIRE(first_index < first.vertices.size());
        REQUIRE(second_index < first.vertices.size());
        REQUIRE(third_index < first.vertices.size());

        const auto first_position =
            terrain_vertex_position(first.vertices[first_index]);
        const auto second_position =
            terrain_vertex_position(first.vertices[second_index]);
        const auto third_position =
            terrain_vertex_position(first.vertices[third_index]);
        CHECK(
            triangle_double_area_squared(
                first_position,
                second_position,
                third_position) >
            0.001F);
        CHECK(
            glm::cross(
                second_position - first_position,
                third_position - first_position)
                .y >
            0.0F);
        const auto horizontal_edge_length =
            [](const glm::vec3& left,
               const glm::vec3& right) noexcept {
                return glm::length(
                    glm::vec2 {
                        right.x - left.x,
                        right.z - left.z,
                    });
            };
        CHECK(
            horizontal_edge_length(
                first_position,
                second_position) <=
            std::sqrt(128.0F) + 0.001F);
        CHECK(
            horizontal_edge_length(
                second_position,
                third_position) <=
            std::sqrt(128.0F) + 0.001F);
        CHECK(
            horizontal_edge_length(
                third_position,
                first_position) <=
            std::sqrt(128.0F) + 0.001F);

        const auto first_sample = world.sample_generated_surface(
            static_cast<int>(first_position.x),
            static_cast<int>(first_position.z));
        const auto second_sample = world.sample_generated_surface(
            static_cast<int>(second_position.x),
            static_cast<int>(second_position.z));
        const auto third_sample = world.sample_generated_surface(
            static_cast<int>(third_position.x),
            static_cast<int>(third_position.z));
        const auto touches_emerged_terrain =
            first_sample.water_level <= first_sample.surface_height ||
            second_sample.water_level <= second_sample.surface_height ||
            third_sample.water_level <= third_sample.surface_height;
        if (touches_emerged_terrain) {
            ++emerged_triangle_count;
        } else {
            ++submerged_triangle_count;
        }
    }

    CHECK(emerged_triangle_count > 0U);
    CHECK(submerged_triangle_count == 0U);
}

TEST_CASE("le masque de chunks detailles retire exactement leurs triangles proxy") {
    SeaHorizonTerrainMesh mesh {};
    mesh.vertices = {
        {.x = 0.0F, .z = 0.0F},
        {.x = 0.0F, .z = 16.0F},
        {.x = 16.0F, .z = 16.0F},
        {.x = 16.0F, .z = 0.0F},
        {.x = 16.0F, .z = 16.0F},
        {.x = 32.0F, .z = 16.0F},
        {.x = -16.0F, .z = 0.0F},
        {.x = 0.0F, .z = 0.0F},
        {.x = 0.0F, .z = 16.0F},
    };
    mesh.indices = {
        0U, 1U, 2U,
        3U, 4U, 5U,
        6U, 7U, 8U,
        99U, 0U, 1U,
    };

    std::vector<std::uint32_t> filtered {};
    std::vector<std::uint32_t> transition {};
    const std::array detailed_chunks {
        ChunkCoord {-1, 0},
        ChunkCoord {0, 0},
    };
    filter_sea_horizon_terrain_indices(
        mesh,
        detailed_chunks,
        filtered,
        transition);

    CHECK(
        filtered ==
        std::vector<std::uint32_t> {
            3U,
            4U,
            5U,
        });
    CHECK(
        transition ==
        std::vector<std::uint32_t> {
            0U,
            1U,
            2U,
            6U,
            7U,
            8U,
        });

    std::vector<std::uint32_t> deterministic {};
    std::vector<std::uint32_t> deterministic_transition {};
    filter_sea_horizon_terrain_indices(
        mesh,
        detailed_chunks,
        deterministic,
        deterministic_transition);
    CHECK(deterministic == filtered);
    CHECK(
        deterministic_transition ==
        transition);
    CHECK(
        mesh.indices ==
        std::vector<std::uint32_t> {
            0U, 1U, 2U,
            3U, 4U, 5U,
            6U, 7U, 8U,
            99U, 0U, 1U,
        });
}

TEST_CASE("la transition detail proxy reste hors du bord de streaming") {
    const auto high =
        sea_horizon_detail_transition_range(
            112.0F);
    const auto medium =
        sea_horizon_detail_transition_range(
            96.0F);
    const auto low =
        sea_horizon_detail_transition_range(
            80.0F);
    const auto minimum =
        sea_horizon_detail_transition_range(
            32.0F);
    const auto invalid =
        sea_horizon_detail_transition_range(
            std::numeric_limits<float>::quiet_NaN());

    CHECK(high.start_distance == doctest::Approx(80.0F));
    CHECK(high.end_distance == doctest::Approx(104.0F));
    CHECK(high.enabled());
    CHECK(medium.start_distance == doctest::Approx(64.0F));
    CHECK(medium.end_distance == doctest::Approx(88.0F));
    CHECK(medium.enabled());
    CHECK(low.start_distance == doctest::Approx(48.0F));
    CHECK(low.end_distance == doctest::Approx(72.0F));
    CHECK(low.enabled());
    CHECK(minimum.start_distance == doctest::Approx(0.0F));
    CHECK(minimum.end_distance == doctest::Approx(24.0F));
    CHECK(minimum.enabled());
    CHECK_FALSE(invalid.enabled());
    CHECK(
        high.end_distance -
            high.start_distance ==
        doctest::Approx(
            kSeaHorizonDetailTransitionWidth));
}

TEST_CASE("le raccord d'eau reste avant chaque bord de streaming") {
    const auto high =
        sea_horizon_water_blend_range(
            112.0F);
    const auto medium =
        sea_horizon_water_blend_range(
            96.0F);
    const auto low =
        sea_horizon_water_blend_range(
            80.0F);
    const auto minimum =
        sea_horizon_water_blend_range(
            0.0F);
    const auto radius_two =
        sea_horizon_water_blend_range(
            64.0F);
    const auto radius_one =
        sea_horizon_water_blend_range(
            48.0F);
    const auto invalid =
        sea_horizon_water_blend_range(
            std::numeric_limits<float>::infinity());

    CHECK(high.start_distance == doctest::Approx(16.0F));
    CHECK(high.end_distance == doctest::Approx(40.0F));
    CHECK(medium.start_distance == doctest::Approx(16.0F));
    CHECK(medium.end_distance == doctest::Approx(40.0F));
    CHECK(low.start_distance == doctest::Approx(0.0F));
    CHECK(low.end_distance == doctest::Approx(24.0F));
    CHECK(radius_two.start_distance == doctest::Approx(0.0F));
    CHECK(radius_two.end_distance == doctest::Approx(8.0F));
    CHECK(radius_one == SeaHorizonWaterBlendRange {});
    CHECK(minimum == SeaHorizonWaterBlendRange {});
    CHECK(invalid == SeaHorizonWaterBlendRange {});
}

TEST_CASE("le raccord d'eau suit uniquement les chunks réellement publiés") {
    std::vector<ChunkCoord> uploaded {};
    for (auto z = -1; z <= 1; ++z) {
        for (auto x = -1; x <= 1; ++x) {
            uploaded.push_back({x, z});
        }
    }

    const auto centered_coverage =
        sea_horizon_contiguous_chunk_coverage_distance(
            {8.0F, 52.0F, 8.0F},
            uploaded,
            3);
    const auto edge_coverage =
        sea_horizon_contiguous_chunk_coverage_distance(
            {15.5F, 52.0F, 15.5F},
            uploaded,
            3);
    CHECK(
        centered_coverage ==
        doctest::Approx(24.0F));
    CHECK(
        edge_coverage ==
        doctest::Approx(16.5F));

    const auto loading_range =
        sea_horizon_water_blend_range(
            112.0F,
            centered_coverage);
    CHECK(
        loading_range.start_distance ==
        doctest::Approx(0.0F));
    CHECK(
        loading_range.end_distance ==
        doctest::Approx(16.0F));

    uploaded.pop_back();
    const auto incomplete_coverage =
        sea_horizon_contiguous_chunk_coverage_distance(
            {8.0F, 52.0F, 8.0F},
            uploaded,
            3);
    CHECK(
        incomplete_coverage ==
        doctest::Approx(8.0F));
    CHECK(
        sea_horizon_water_blend_range(
            112.0F,
            incomplete_coverage) ==
        SeaHorizonWaterBlendRange {});

    std::vector<ChunkCoord> negative_uploaded {};
    for (auto z = -2; z <= 0; ++z) {
        for (auto x = -2; x <= 0; ++x) {
            negative_uploaded.push_back({x, z});
        }
    }
    CHECK(
        sea_horizon_contiguous_chunk_coverage_distance(
            {-0.5F, 52.0F, -0.5F},
            negative_uploaded,
            1) ==
        doctest::Approx(16.5F));
    CHECK(
        sea_horizon_contiguous_chunk_coverage_distance(
            {
                std::numeric_limits<float>::infinity(),
                52.0F,
                0.0F,
            },
            negative_uploaded,
            1) ==
        doctest::Approx(0.0F));
    CHECK(
        sea_horizon_contiguous_chunk_coverage_distance(
            {0.0F, 52.0F, 0.0F},
            {},
            1) ==
        doctest::Approx(0.0F));
}

TEST_CASE("la grille carree garde le brouillard devant son bord meme en diagonale") {
    const glm::vec3 camera_position {
        kSeaHorizonGridStep - 0.001F,
        55.0F,
        kSeaHorizonGridStep - 0.001F,
    };
    const auto center =
        sea_horizon_snapped_center(camera_position);
    const auto geometry_half_extent =
        kSeaHorizonTerrainOuterRadius +
        kSeaHorizonGridStep;
    const auto minimum_border_distance = std::min({
        static_cast<float>(center.x) + geometry_half_extent -
            camera_position.x,
        camera_position.x -
            (static_cast<float>(center.x) - geometry_half_extent),
        static_cast<float>(center.z) + geometry_half_extent -
            camera_position.z,
        camera_position.z -
            (static_cast<float>(center.z) - geometry_half_extent),
    });

    CHECK(center == SeaHorizonSnappedCenter {16, 16});
    CHECK(
        minimum_border_distance >=
        kSeaHorizonTerrainOuterRadius);
}

TEST_CASE("le terrain lointain reste desactive hors de l'aventure en mer") {
    World continental(
        811,
        5,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::LegacyV1);
    const auto mesh = build_sea_horizon_terrain_mesh(
        continental,
        {-12.0F, 70.0F, 24.0F});

    CHECK(mesh.empty());
    CHECK(mesh.vertices.empty());
    CHECK(mesh.snapped_center == SeaHorizonSnappedCenter {-16, 32});
    CHECK(continental.chunk_records().empty());
}

TEST_CASE("le brouillard terminal reste borne lisible et plus proche pendant la tempete") {
    const auto clear = sea_horizon_fog_range(0.0F);
    const auto tempest = sea_horizon_fog_range(1.0F);
    const auto negative = sea_horizon_fog_range(-4.0F);
    const auto excessive = sea_horizon_fog_range(9.0F);
    const auto invalid = sea_horizon_fog_range(
        std::numeric_limits<float>::quiet_NaN());

    CHECK(clear.start_distance == doctest::Approx(384.0F));
    CHECK(clear.end_distance == doctest::Approx(512.0F));
    CHECK(tempest.start_distance == doctest::Approx(256.0F));
    CHECK(tempest.end_distance == doctest::Approx(416.0F));
    CHECK(tempest.start_distance > 250.0F);
    CHECK(tempest.start_distance < clear.start_distance);
    CHECK(tempest.end_distance < clear.end_distance);
    CHECK(tempest.start_distance < tempest.end_distance);
    CHECK(kSeaHorizonProjectionFarPlane == doctest::Approx(576.0F));
    CHECK(
        kSeaHorizonProjectionFarPlane >
        kSeaHorizonTerrainOuterRadius +
            kSeaHorizonGridStep);
    CHECK(negative == clear);
    CHECK(excessive == tempest);
    CHECK(invalid == clear);
}

TEST_CASE("le focus predictif avance d'un chunk seulement a proximite du navire") {
    const glm::vec3 base {10.0F, 57.0F, 20.0F};
    const glm::vec3 ship {0.0F, 49.0F, 0.0F};
    const auto predicted = sea_horizon_predictive_streaming_focus(
        base,
        ship,
        {3.0F, 7.0F, 4.0F});

    CHECK(predicted.x == doctest::Approx(19.6F));
    CHECK(predicted.y == base.y);
    CHECK(predicted.z == doctest::Approx(32.8F));
    CHECK(
        glm::length(
            glm::vec2 {predicted.x - base.x, predicted.z - base.z}) ==
        doctest::Approx(16.0F));

    const auto distant = sea_horizon_predictive_streaming_focus(
        {100.0F, 57.0F, 100.0F},
        ship,
        {3.0F, 0.0F, 4.0F});
    CHECK(distant == glm::vec3 {100.0F, 57.0F, 100.0F});

    const auto stopped = sea_horizon_predictive_streaming_focus(
        base,
        ship,
        {0.0F, 0.0F, 0.0F});
    CHECK(stopped == base);

    const auto rolling_noise =
        sea_horizon_predictive_streaming_focus(
            base,
            ship,
            {0.10F, 0.0F, 0.0F});
    CHECK(rolling_noise == base);

    const auto accelerating =
        sea_horizon_predictive_streaming_focus(
            base,
            ship,
            {0.625F, 0.0F, 0.0F});
    CHECK(
        accelerating.x ==
        doctest::Approx(
            base.x + 8.0F));
    CHECK(accelerating.y == base.y);
    CHECK(accelerating.z == base.z);

    const auto at_proximity_limit =
        sea_horizon_predictive_streaming_focus(
            {64.0F, 57.0F, 0.0F},
            ship,
            {1.0F, 0.0F, 0.0F});
    CHECK(
        at_proximity_limit ==
        glm::vec3 {80.0F, 57.0F, 0.0F});

    const auto outside_proximity_limit =
        sea_horizon_predictive_streaming_focus(
            {64.01F, 57.0F, 0.0F},
            ship,
            {1.0F, 0.0F, 0.0F});
    CHECK(
        outside_proximity_limit ==
        glm::vec3 {64.01F, 57.0F, 0.0F});

    const auto invalid = sea_horizon_predictive_streaming_focus(
        {
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -3.0F,
        },
        ship,
        {
            1.0F,
            0.0F,
            0.0F,
        });
    CHECK(std::isfinite(invalid.x));
    CHECK(std::isfinite(invalid.y));
    CHECK(std::isfinite(invalid.z));
    CHECK(invalid == glm::vec3 {0.0F, 0.0F, -3.0F});

    const auto invalid_velocity =
        sea_horizon_predictive_streaming_focus(
            base,
            ship,
            {
                std::numeric_limits<float>::infinity(),
                0.0F,
                1.0F,
            });
    CHECK(invalid_velocity == base);
}

} // namespace valcraft
