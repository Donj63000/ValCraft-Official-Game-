#include "world/World.h"

#include "TestUtils.h"

#include <doctest/doctest.h>
#include <stdexcept>

namespace valcraft {

TEST_CASE("chunk stores and retrieves local blocks") {
    Chunk chunk({2, -1});
    chunk.fill(to_block_id(BlockType::Air));

    chunk.set_local(3, 40, 5, to_block_id(BlockType::Stone));
    CHECK(chunk.get_local(3, 40, 5) == to_block_id(BlockType::Stone));
    CHECK(chunk.get_local(0, 0, 0) == to_block_id(BlockType::Air));
}

TEST_CASE("chunk get_local throws when coordinates are out of bounds") {
    Chunk chunk({0, 0});
    CHECK_THROWS_AS(([&]() { static_cast<void>(chunk.get_local(-1, 0, 0)); }()), std::out_of_range);
    CHECK_THROWS_AS(([&]() { static_cast<void>(chunk.get_local(0, kChunkHeight, 0)); }()), std::out_of_range);
    CHECK_THROWS_AS(([&]() { static_cast<void>(chunk.get_local(0, 0, kChunkSizeZ)); }()), std::out_of_range);
}

TEST_CASE("chunk set_local throws when coordinates are out of bounds") {
    Chunk chunk({0, 0});
    CHECK_THROWS_AS(chunk.set_local(kChunkSizeX, 0, 0, to_block_id(BlockType::Stone)), std::out_of_range);
    CHECK_THROWS_AS(chunk.set_local(0, -1, 0, to_block_id(BlockType::Stone)), std::out_of_range);
    CHECK_THROWS_AS(chunk.set_local(0, 0, -1, to_block_id(BlockType::Stone)), std::out_of_range);
}

TEST_CASE("world converts negative coordinates into chunk and local positions") {
    World world(1234, 1);

    const ChunkCoord expected_chunk_a {-1, -1};
    const ChunkCoord expected_chunk_b {-1, -1};
    const ChunkCoord expected_chunk_c {-2, 1};
    const BlockCoord expected_local_a {15, 12, 15};
    const BlockCoord expected_local_b {15, 9, 15};

    CHECK(world.world_to_chunk(-1, -1) == expected_chunk_a);
    CHECK(world.world_to_chunk(-16, -16) == expected_chunk_b);
    CHECK(world.world_to_chunk(-17, 31) == expected_chunk_c);
    CHECK(world.world_to_local(-1, 12, -1) == expected_local_a);
    CHECK(world.world_to_local(-17, 9, 31) == expected_local_b);
}

TEST_CASE("world local and chunk conversions round-trip to original coordinates") {
    World world(4321, 1);
    const BlockCoord world_position {-17, 22, 31};
    const auto chunk = world.world_to_chunk(world_position.x, world_position.z);
    const auto local = world.world_to_local(world_position.x, world_position.y, world_position.z);
    const auto reconstructed = world.local_to_world(chunk, local);

    CHECK(reconstructed == world_position);
}

TEST_CASE("world get_block outside valid Y returns air") {
    World world(91, 1);
    CHECK(world.get_block(0, -1, 0) == to_block_id(BlockType::Air));
    CHECK(world.get_block(0, kChunkHeight, 0) == to_block_id(BlockType::Air));
}

TEST_CASE("world set_block outside valid Y is a no-op") {
    World world(92, 1);
    test::make_chunk_empty(world, {0, 0});

    world.set_block(1, -1, 1, to_block_id(BlockType::Stone));
    world.set_block(1, kChunkHeight, 1, to_block_id(BlockType::Stone));

    CHECK(world.get_block(1, 0, 1) == to_block_id(BlockType::Air));
    CHECK(world.get_block(1, kWorldMaxY, 1) == to_block_id(BlockType::Air));
}

TEST_CASE("generation is deterministic for identical seeds") {
    World first(98765, 1);
    World second(98765, 1);
    const ChunkCoord coord {1, -2};

    first.ensure_chunk_loaded(coord);
    second.ensure_chunk_loaded(coord);

    const auto* first_chunk = first.find_chunk(coord);
    const auto* second_chunk = second.find_chunk(coord);
    REQUIRE(first_chunk != nullptr);
    REQUIRE(second_chunk != nullptr);

    for (int y = 0; y < kChunkHeight; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                CHECK(first_chunk->get_local(x, y, z) == second_chunk->get_local(x, y, z));
            }
        }
    }
}

TEST_CASE("surface_height returns the highest solid block in a column") {
    World world(18, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(3, 4, 5, to_block_id(BlockType::Stone));
    world.set_block(3, 9, 5, to_block_id(BlockType::Stone));
    world.set_block(3, 7, 5, to_block_id(BlockType::Stone));

    CHECK(world.surface_height(3, 5) == 9);
}

TEST_CASE("update_streaming loads the full radius around the player") {
    World world(56, 1);
    world.update_streaming({0.5F, 0.0F, 0.5F});

    CHECK(world.chunk_records().size() == 9);
    CHECK(world.find_chunk({0, 0}) != nullptr);
    CHECK(world.find_chunk({1, 1}) != nullptr);
    CHECK(world.find_chunk({-1, -1}) != nullptr);
}

TEST_CASE("update_streaming unloads chunks that move outside the unload radius") {
    World world(57, 1);
    world.update_streaming({0.5F, 0.0F, 0.5F});
    REQUIRE(world.find_chunk({0, 0}) != nullptr);

    world.update_streaming({static_cast<float>(kChunkSizeX * 5) + 0.5F, 0.0F, 0.5F});

    CHECK(world.find_chunk({0, 0}) == nullptr);
    CHECK(world.find_chunk({5, 0}) != nullptr);
}

TEST_CASE("mesher hides internal faces between adjacent solid blocks") {
    World world(77, 1);
    const ChunkCoord coord {0, 0};

    test::make_chunk_empty(world, coord);
    world.set_block(0, 10, 0, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto* single_block_mesh = world.mesh_for(coord);
    REQUIRE(single_block_mesh != nullptr);
    CHECK(single_block_mesh->face_count == 6);

    world.set_block(1, 10, 0, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto* adjacent_mesh = world.mesh_for(coord);
    REQUIRE(adjacent_mesh != nullptr);
    CHECK(adjacent_mesh->face_count == 10);
}

TEST_CASE("raycast returns first solid block and adjacent placement cell") {
    World world(11, 1);
    const ChunkCoord coord {0, 0};
    test::make_chunk_empty(world, coord);
    world.set_block(1, 10, 0, to_block_id(BlockType::Stone));

    const auto hit = world.raycast({0.5F, 10.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, 8.0F);
    const BlockCoord expected_hit_block {1, 10, 0};
    const BlockCoord expected_adjacent {0, 10, 0};
    REQUIRE(hit.hit);
    CHECK(hit.block == expected_hit_block);
    CHECK(hit.adjacent == expected_adjacent);
    CHECK(hit.block_id == to_block_id(BlockType::Stone));
}

TEST_CASE("raycast returns no hit when the path is empty") {
    World world(12, 1);
    test::make_chunk_empty(world, {0, 0});

    const auto hit = world.raycast({0.5F, 10.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, 8.0F);
    CHECK_FALSE(hit.hit);
}

TEST_CASE("raycast returns the current block immediately when starting inside a solid block") {
    World world(13, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(0, 10, 0, to_block_id(BlockType::Stone));

    const auto hit = world.raycast({0.5F, 10.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, 8.0F);
    const BlockCoord expected_current {0, 10, 0};
    REQUIRE(hit.hit);
    CHECK(hit.block == expected_current);
    CHECK(hit.adjacent == expected_current);
}

TEST_CASE("boundary block edits remesh both chunks touching the border") {
    World world(16, 1);
    const ChunkCoord left {0, 0};
    const ChunkCoord right {1, 0};

    test::make_chunk_empty(world, left);
    test::make_chunk_empty(world, right);
    world.set_block(15, 12, 4, to_block_id(BlockType::Stone));
    world.set_block(16, 12, 4, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto left_revision_before = world.mesh_revision(left);
    const auto right_revision_before = world.mesh_revision(right);

    world.set_block(15, 12, 4, to_block_id(BlockType::Air));
    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(left) > left_revision_before);
    CHECK(world.mesh_revision(right) > right_revision_before);

    const auto* left_mesh = world.mesh_for(left);
    const auto* right_mesh = world.mesh_for(right);
    REQUIRE(left_mesh != nullptr);
    REQUIRE(right_mesh != nullptr);
    CHECK(left_mesh->face_count == 0);
    CHECK(right_mesh->face_count == 6);
}

} // namespace valcraft
