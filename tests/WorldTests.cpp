#include "app/PerformanceReport.h"
#include "world/World.h"
#include "world/Environment.h"

#include "TestUtils.h"

#include <algorithm>
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

TEST_CASE("torch block properties are non opaque non collidable and emissive") {
    const auto properties = block_properties(to_block_id(BlockType::Torch));

    CHECK_FALSE(properties.opaque);
    CHECK_FALSE(properties.collidable);
    CHECK(properties.mesh_type == BlockMeshType::Torch);
    CHECK(properties.emissive_level == 14);
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

TEST_CASE("sky light stays at 15 until the first opaque block and 0 below it") {
    World world(19, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(2, 10, 2, to_block_id(BlockType::Stone));

    world.rebuild_lighting();

    CHECK(world.get_sky_light(2, 20, 2) == 15);
    CHECK(world.get_sky_light(2, 11, 2) == 15);
    CHECK(world.get_sky_light(2, 10, 2) == 0);
    CHECK(world.get_sky_light(2, 9, 2) == 0);
}

TEST_CASE("torch light propagates in air and stops on opaque blocks") {
    World world(20, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(4, 1, 4, to_block_id(BlockType::Torch));
    for (int y = 0; y < kChunkHeight; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            world.set_block(6, y, z, to_block_id(BlockType::Stone));
        }
    }

    world.rebuild_lighting();

    CHECK(world.get_block_light(4, 1, 4) == 14);
    CHECK(world.get_block_light(5, 1, 4) == 13);
    CHECK(world.get_block_light(6, 1, 4) == 0);
    CHECK(world.get_block_light(7, 1, 4) == 0);
}

TEST_CASE("torch light crosses chunk boundaries") {
    World world(21, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_chunk_empty(world, {1, 0});
    world.set_block(15, 1, 2, to_block_id(BlockType::Torch));

    world.rebuild_lighting();

    CHECK(world.get_block_light(15, 1, 2) == 14);
    CHECK(world.get_block_light(16, 1, 2) == 13);
    CHECK(world.get_block_light(17, 1, 2) == 12);
}

TEST_CASE("removing the support block removes the torch above it") {
    World world(22, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(3, 0, 3, to_block_id(BlockType::Stone));
    world.set_block(3, 1, 3, to_block_id(BlockType::Torch));
    world.rebuild_lighting();
    REQUIRE(world.get_block(3, 1, 3) == to_block_id(BlockType::Torch));
    REQUIRE(world.get_block_light(3, 1, 3) == 14);

    world.set_block(3, 0, 3, to_block_id(BlockType::Air));
    world.rebuild_lighting();

    CHECK(world.get_block(3, 1, 3) == to_block_id(BlockType::Air));
    CHECK(world.get_block_light(3, 1, 3) == 0);
}

TEST_CASE("update_streaming plans the full radius around the player without immediate loads") {
    World world(56, 1);
    world.update_streaming({0.5F, 0.0F, 0.5F});

    CHECK(world.chunk_records().empty());
    CHECK(world.pending_generation_count() == 9);
    CHECK(world.pending_mesh_count() == 0);
    CHECK(world.pending_lighting_count() == 0);
    CHECK(world.has_pending_work());
}

TEST_CASE("update_streaming is a no-op while the player stays in the same chunk") {
    World world(55, 1);

    const auto first_update = world.update_streaming({0.5F, 0.0F, 0.5F});
    const auto second_update = world.update_streaming({5.5F, 0.0F, 6.5F});

    CHECK(first_update.chunk_changed);
    CHECK(first_update.generation_enqueued == 9);
    CHECK_FALSE(second_update.chunk_changed);
    CHECK(second_update.generation_enqueued == 0);
    CHECK(second_update.generation_pruned == 0);
    CHECK(world.pending_generation_count() == 9);
}

TEST_CASE("process_pending_work respects chunk generation budget and eventually readies nearby chunks") {
    World world(58, 1);
    world.update_streaming({0.5F, 0.0F, 0.5F});

    const auto first_stats = world.process_pending_work({1, 16, 65536});
    CHECK(first_stats.generated_chunks == 1);
    CHECK(world.chunk_records().size() == 1);
    CHECK(world.pending_generation_count() == 8);

    test::flush_pending_work(world);

    CHECK(world.chunk_records().size() == 9);
    CHECK(world.find_chunk({0, 0}) != nullptr);
    CHECK(world.find_chunk({1, 1}) != nullptr);
    CHECK(world.find_chunk({-1, -1}) != nullptr);
    CHECK(world.are_chunks_ready({0.5F, 70.0F, 0.5F}, 1));
}

TEST_CASE("spawn preload stays ready while outer streaming work starts") {
    World world(59, kDefaultStreamRadius);
    const auto preload_center = world.world_to_chunk(0, 0);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            world.ensure_chunk_loaded({preload_center.x + dx, preload_center.z + dz});
        }
    }
    world.rebuild_dirty_meshes();

    const glm::vec3 player_position {0.5F, 80.0F, 0.5F};
    REQUIRE(world.are_chunks_ready(player_position, 1));

    world.update_streaming(player_position);
    for (int frame_index = 0; frame_index < 60; ++frame_index) {
        const auto stats = world.process_pending_work({2, 4, 16384});
        (void)stats;
        CAPTURE(frame_index);
        CHECK(world.are_chunks_ready(player_position, 1));
    }
}

TEST_CASE("update_streaming unloads chunks that move outside the unload radius") {
    World world(57, 1);
    world.update_streaming({0.5F, 0.0F, 0.5F});
    test::flush_pending_work(world);
    REQUIRE(world.find_chunk({0, 0}) != nullptr);

    world.update_streaming({static_cast<float>(kChunkSizeX * 5) + 0.5F, 0.0F, 0.5F});
    test::flush_pending_work(world);

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

TEST_CASE("mesher computes full ambient occlusion for an isolated block and darker corners when enclosed") {
    World world(78, 1);
    const ChunkCoord coord {0, 0};
    test::make_chunk_empty(world, coord);
    world.set_block(1, 1, 1, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto* isolated_mesh = world.mesh_for(coord);
    REQUIRE(isolated_mesh != nullptr);
    REQUIRE_FALSE(isolated_mesh->vertices.empty());
    CHECK(std::all_of(isolated_mesh->vertices.begin(), isolated_mesh->vertices.end(), [](const ChunkVertex& vertex) {
        return vertex.ao == doctest::Approx(1.0F);
    }));

    test::make_chunk_empty(world, coord);
    world.set_block(1, 1, 1, to_block_id(BlockType::Stone));
    world.set_block(0, 1, 1, to_block_id(BlockType::Stone));
    world.set_block(1, 1, 0, to_block_id(BlockType::Stone));
    world.set_block(0, 1, 0, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto* occluded_mesh = world.mesh_for(coord);
    REQUIRE(occluded_mesh != nullptr);
    auto darkest_top_ao = 1.0F;
    for (const auto& vertex : occluded_mesh->vertices) {
        if (vertex.ny > 0.9F && vertex.y > 1.9F) {
            darkest_top_ao = std::min(darkest_top_ao, vertex.ao);
        }
    }
    CHECK(darkest_top_ao < 1.0F);
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

TEST_CASE("loading an orthogonal neighbor remeshes an already meshed chunk") {
    World world(83, 2);
    const ChunkCoord origin {0, 0};
    const ChunkCoord east {1, 0};

    test::make_chunk_empty(world, origin);
    world.rebuild_dirty_meshes();

    const auto origin_revision_before = world.mesh_revision(origin);
    REQUIRE(origin_revision_before > 0);

    world.ensure_chunk_loaded(east);
    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(origin) > origin_revision_before);
}

TEST_CASE("loading a diagonal neighbor remeshes an already meshed chunk") {
    World world(84, 2);
    const ChunkCoord origin {0, 0};
    const ChunkCoord diagonal {1, 1};

    test::make_chunk_empty(world, origin);
    world.rebuild_dirty_meshes();

    const auto origin_revision_before = world.mesh_revision(origin);
    REQUIRE(origin_revision_before > 0);

    world.ensure_chunk_loaded(diagonal);
    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(origin) > origin_revision_before);
}

TEST_CASE("near-player chunk load keeps seam remeshes on the priority path") {
    World world(85, 0);
    world.update_streaming({0.5F, 80.0F, 0.5F});

    const ChunkCoord origin {0, 0};
    const ChunkCoord east {1, 0};
    test::make_chunk_empty(world, origin);
    world.rebuild_dirty_meshes();

    const auto origin_revision_before = world.mesh_revision(origin);
    REQUIRE(origin_revision_before > 0);

    world.ensure_chunk_loaded(east);
    const auto stats = world.process_pending_work({0, 0, 65536});

    CHECK(stats.prioritized_meshed_chunks >= 1);
    CHECK(world.mesh_revision(origin) > origin_revision_before);
}

TEST_CASE("far chunk load defers seam remeshes to the normal mesh budget") {
    World world(86, 0);
    world.update_streaming({0.5F, 80.0F, 0.5F});

    const ChunkCoord far_existing {3, 0};
    const ChunkCoord far_new {4, 0};
    test::make_chunk_empty(world, far_existing);
    world.rebuild_dirty_meshes();

    const auto far_revision_before = world.mesh_revision(far_existing);
    REQUIRE(far_revision_before > 0);

    world.ensure_chunk_loaded(far_new);
    const auto stats = world.process_pending_work({0, 0, 65536});

    CHECK(stats.prioritized_meshed_chunks == 0);
    CHECK(world.mesh_revision(far_existing) == far_revision_before);
    CHECK(world.pending_mesh_count() >= 1);
}

TEST_CASE("process_pending_work respects mesh rebuild budget") {
    World world(17, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_chunk_empty(world, {1, 0});
    world.rebuild_dirty_meshes();

    world.set_block(15, 10, 4, to_block_id(BlockType::Stone));
    world.set_block(16, 10, 4, to_block_id(BlockType::Stone));

    const auto stats = world.process_pending_work({0, 1, 65536});
    CHECK(stats.meshed_chunks == 1);
    CHECK(world.pending_mesh_count() >= 1);
}

TEST_CASE("overlapping lighting updates coalesce into a single pending job") {
    World world(80, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_chunk_empty(world, {1, 0});
    world.rebuild_dirty_meshes();

    world.set_block(1, 0, 1, to_block_id(BlockType::Stone));
    world.set_block(16, 0, 1, to_block_id(BlockType::Stone));

    CHECK(world.pending_lighting_count() == 1);
}

TEST_CASE("lighting completion enqueues mesh rebuilds without a global dirty scan") {
    World world(81, 1);
    test::make_chunk_empty(world, {0, 0});
    world.rebuild_dirty_meshes();

    world.set_block(1, 0, 1, to_block_id(BlockType::Stone));
    REQUIRE(world.pending_lighting_count() == 1);

    const auto stats = world.process_pending_work({0, 0, 65536});
    CHECK(stats.lighting_jobs_completed == 1);
    CHECK(world.pending_mesh_count() >= 1);
}

TEST_CASE("local lighting updates do not remesh unrelated chunks") {
    World world(79, 2);
    const ChunkCoord origin {0, 0};
    const ChunkCoord neighbor {1, 0};
    const ChunkCoord far {2, 0};

    test::make_chunk_empty(world, origin);
    test::make_chunk_empty(world, neighbor);
    test::make_chunk_empty(world, far);
    world.rebuild_dirty_meshes();

    const auto far_revision_before = world.mesh_revision(far);

    world.set_block(1, 0, 1, to_block_id(BlockType::Stone));
    world.set_block(1, 1, 1, to_block_id(BlockType::Torch));
    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(origin) > 0);
    CHECK(world.mesh_revision(neighbor) > 0);
    CHECK(world.mesh_revision(far) == far_revision_before);
}

TEST_CASE("placing and removing a torch only remeshes nearby chunks") {
    World world(82, 2);
    const ChunkCoord origin {0, 0};
    const ChunkCoord neighbor {1, 0};
    const ChunkCoord far {2, 0};

    test::make_chunk_empty(world, origin);
    test::make_chunk_empty(world, neighbor);
    test::make_chunk_empty(world, far);
    world.rebuild_dirty_meshes();

    const auto far_revision_before = world.mesh_revision(far);
    world.set_block(1, 0, 1, to_block_id(BlockType::Stone));
    world.set_block(1, 1, 1, to_block_id(BlockType::Torch));
    world.rebuild_dirty_meshes();

    const auto origin_revision_after_place = world.mesh_revision(origin);
    REQUIRE(origin_revision_after_place > 0);
    CHECK(world.mesh_revision(far) == far_revision_before);

    world.set_block(1, 1, 1, to_block_id(BlockType::Air));
    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(origin) > origin_revision_after_place);
    CHECK(world.mesh_revision(far) == far_revision_before);
}

TEST_CASE("environment curve is brightest at noon and remains readable at midnight") {
    const auto noon = EnvironmentClock::compute_state(12.0F);
    const auto dusk = EnvironmentClock::compute_state(18.5F);
    const auto midnight = EnvironmentClock::compute_state(0.0F);

    CHECK(noon.daylight_factor > dusk.daylight_factor);
    CHECK(dusk.daylight_factor > midnight.daylight_factor);
    CHECK(midnight.daylight_factor >= 0.18F);
}

TEST_CASE("environment clock respects freeze mode") {
    EnvironmentClock frozen_clock(8.0F, true);
    frozen_clock.update(120.0F);
    CHECK(frozen_clock.time_of_day() == doctest::Approx(8.0F));

    EnvironmentClock running_clock(8.0F, false);
    running_clock.update(30.0F);
    CHECK(running_clock.time_of_day() == doctest::Approx(9.0F).epsilon(0.01));
}

TEST_CASE("performance report formatting includes frame and scheduler counters") {
    PerformanceReportMetadata metadata {};
    metadata.scenario = "baseline";
    std::vector<FramePerformanceSample> samples {
        {0, 13.0, 1.0, 1.0, 2.0, 3.0, 0.4, 0.1, 0.3, 3, 4, 42, 6, 8, 3, 2, 3, 12, 3, 2, 7, 30, 20, 30, PerformanceStage::Unattributed},
        {1, 20.0, 0.8, 1.5, 2.5, 2.0, 0.2, 0.15, 0.35, 2, 3, 21, 3, 10, 2, 1, 2, 0, 0, 0, 0, 26, 18, 26, PerformanceStage::Unattributed},
    };

    const auto report = format_performance_report(build_performance_report(metadata, samples, false));

    CHECK(report.find("frame_total_ms_avg=") != std::string::npos);
    CHECK(report.find("p95=") != std::string::npos);
    CHECK(report.find("pending_generation_avg=") != std::string::npos);
    CHECK(report.find("lag_frames_16_7=") != std::string::npos);
    CHECK(report.find("scheduler_stream_changes=") != std::string::npos);
    CHECK(report.find("jobs_total=7") != std::string::npos);
}

} // namespace valcraft
