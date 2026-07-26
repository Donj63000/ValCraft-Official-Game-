#include "render/WaterVertex.h"
#include "world/ChunkMesher.h"
#include "world/World.h"

#include "TestUtils.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace valcraft {

TEST_CASE("water vertex owns a stable compact GPU layout") {
    static_assert(!std::is_same_v<WaterVertex, ChunkVertex>);
    static_assert(sizeof(WaterVertex) <= 32U);

    CHECK(sizeof(WaterVertex) == 32U);
    CHECK(offsetof(WaterVertex, x) == 0U);
    CHECK(offsetof(WaterVertex, u) == 12U);
    CHECK(offsetof(WaterVertex, nx) == 20U);
    CHECK(offsetof(WaterVertex, face_shade_half) == 24U);
    CHECK(offsetof(WaterVertex, ao) == 26U);
    CHECK(offsetof(WaterVertex, wave_weight) == 30U);
}

TEST_CASE("water vertex compact attributes preserve their rendering domain") {
    const auto vertex = make_water_vertex(
        12.5F,
        63.875F,
        -4.25F,
        0.3125F,
        0.875F,
        -1.0F,
        0.0F,
        1.0F,
        1.02F,
        1.0F,
        11.0F / 15.0F,
        4.0F / 15.0F,
        6.0F,
        1.0F);

    CHECK(vertex.x == 12.5F);
    CHECK(vertex.y == 63.875F);
    CHECK(vertex.z == -4.25F);
    CHECK(vertex.u == 0.3125F);
    CHECK(vertex.v == 0.875F);
    CHECK(unpack_water_snorm(vertex.nx) == doctest::Approx(-1.0F));
    CHECK(unpack_water_snorm(vertex.ny) == doctest::Approx(0.0F));
    CHECK(unpack_water_snorm(vertex.nz) == doctest::Approx(1.0F));
    CHECK(unpack_water_half(vertex.face_shade_half) == doctest::Approx(1.02F).epsilon(0.001F));
    CHECK(vertex.ao == 1U);
    CHECK(unpack_water_unorm(vertex.sky_light) == doctest::Approx(11.0F / 15.0F).epsilon(0.003F));
    CHECK(unpack_water_unorm(vertex.block_light) == doctest::Approx(4.0F / 15.0F).epsilon(0.003F));
    CHECK(vertex.material_class == 6U);
    CHECK(vertex.wave_weight == 1U);
    CHECK(vertex.normal_padding == 0U);
    CHECK(vertex.reserved == 0U);
}

TEST_CASE("water meshing remains deterministic with compact vertices") {
    World world(9327, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(3, kSeaLevel, 5, to_block_id(BlockType::Water));
    world.set_block(4, kSeaLevel, 5, to_block_id(BlockType::Water));
    world.set_block(4, kSeaLevel + 1, 5, to_block_id(BlockType::Water));

    ChunkMesher mesher {};
    const auto first = mesher.build_mesh(world, {0, 0});
    const auto second = mesher.build_mesh(world, {0, 0});

    REQUIRE_FALSE(first.water_vertices.empty());
    CHECK(first.water_vertices == second.water_vertices);
    CHECK(first.water_indices == second.water_indices);
    CHECK(first.water_face_count == second.water_face_count);
    CHECK(std::all_of(
        first.water_indices.begin(),
        first.water_indices.end(),
        [&first](std::uint32_t index) {
            return static_cast<std::size_t>(index) < first.water_vertices.size();
        }));

    for (const auto& vertex : first.water_vertices) {
        CHECK(std::isfinite(vertex.x));
        CHECK(std::isfinite(vertex.y));
        CHECK(std::isfinite(vertex.z));
        CHECK(std::isfinite(vertex.u));
        CHECK(std::isfinite(vertex.v));

        const auto nx = unpack_water_snorm(vertex.nx);
        const auto ny = unpack_water_snorm(vertex.ny);
        const auto nz = unpack_water_snorm(vertex.nz);
        const auto normal_length = std::sqrt(nx * nx + ny * ny + nz * nz);
        CHECK(normal_length == doctest::Approx(1.0F).epsilon(0.001F));
        CHECK(vertex.ao == 1U);
        CHECK(vertex.material_class == 6U);
        CHECK((vertex.wave_weight == 0U || vertex.wave_weight == 1U));
    }
}

} // namespace valcraft
