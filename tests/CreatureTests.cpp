#include "creatures/CreatureGeometry.h"
#include "creatures/CreatureSystem.h"

#include "TestUtils.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

void add_tree_patch(World& world, int base_x, int base_z, int trunk_y) {
    for (int y = 1; y <= 3; ++y) {
        world.set_block(base_x, trunk_y + y, base_z, to_block_id(BlockType::Wood));
    }

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            world.set_block(base_x + dx, trunk_y + 4, base_z + dz, to_block_id(BlockType::Leaves));
        }
    }
}

void sculpt_lamb_hills(World& world, const ChunkCoord& coord, int base_height) {
    const auto origin_x = coord.x * kChunkSizeX;
    const auto origin_z = coord.z * kChunkSizeZ;
    for (int local_z = 0; local_z < kChunkSizeZ; ++local_z) {
        for (int local_x = 0; local_x < kChunkSizeX; ++local_x) {
            const auto world_x = origin_x + local_x;
            const auto world_z = origin_z + local_z;
            const auto extra_layers = ((local_x + local_z) % 3 == 0) ? 2 : (((local_x * 2 + local_z) % 5 == 0) ? 1 : 0);
            for (int layer = 1; layer <= extra_layers; ++layer) {
                world.set_block(world_x, base_height + layer, world_z, to_block_id(BlockType::Grass));
            }
        }
    }
}

auto contains_only_night_states(CreatureBehaviorState state) -> bool {
    return state == CreatureBehaviorState::Lurk ||
           state == CreatureBehaviorState::Stare ||
           state == CreatureBehaviorState::Twitch;
}

auto horizontal_distance_squared(const glm::vec3& lhs, const glm::vec3& rhs) -> float {
    const auto dx = lhs.x - rhs.x;
    const auto dz = lhs.z - rhs.z;
    return dx * dx + dz * dz;
}

auto tile_average_rgba(const std::vector<std::uint8_t>& atlas, CreatureAtlasTile tile) -> std::array<float, 4> {
    const auto coordinates = creature_atlas_tile_coordinates(tile);
    const auto start_x = coordinates[0] * kCreatureAtlasTileSize;
    const auto start_y = coordinates[1] * kCreatureAtlasTileSize;

    std::array<float, 4> accum {{0.0F, 0.0F, 0.0F, 0.0F}};
    for (int y = 0; y < kCreatureAtlasTileSize; ++y) {
        for (int x = 0; x < kCreatureAtlasTileSize; ++x) {
            const auto index = static_cast<std::size_t>(((start_y + y) * kCreatureAtlasSize + (start_x + x)) * 4);
            accum[0] += static_cast<float>(atlas[index + 0]);
            accum[1] += static_cast<float>(atlas[index + 1]);
            accum[2] += static_cast<float>(atlas[index + 2]);
            accum[3] += static_cast<float>(atlas[index + 3]);
        }
    }

    const auto texel_count = static_cast<float>(kCreatureAtlasTileSize * kCreatureAtlasTileSize);
    for (auto& channel : accum) {
        channel /= texel_count;
    }
    return accum;
}

struct MeshBounds {
    glm::vec3 min {0.0F};
    glm::vec3 max {0.0F};
};

auto mesh_bounds(const CreatureMeshData& mesh) -> MeshBounds {
    MeshBounds bounds {
        glm::vec3 {std::numeric_limits<float>::max()},
        glm::vec3 {std::numeric_limits<float>::lowest()},
    };

    for (const auto& vertex : mesh.vertices) {
        bounds.min.x = std::min(bounds.min.x, vertex.x);
        bounds.min.y = std::min(bounds.min.y, vertex.y);
        bounds.min.z = std::min(bounds.min.z, vertex.z);
        bounds.max.x = std::max(bounds.max.x, vertex.x);
        bounds.max.y = std::max(bounds.max.y, vertex.y);
        bounds.max.z = std::max(bounds.max.z, vertex.z);
    }
    return bounds;
}

auto all_vertex_attributes_are_bounded(const CreatureMeshData& mesh) -> bool {
    return std::all_of(mesh.vertices.begin(), mesh.vertices.end(), [](const CreatureVertex& vertex) {
        return vertex.material_class >= 0.0F &&
               vertex.material_class <= 1.0F &&
               vertex.cavity_mask >= 0.0F &&
               vertex.cavity_mask <= 1.0F &&
               vertex.emissive_strength >= 0.0F &&
               vertex.emissive_strength <= 1.0F;
    });
}

auto max_material_class(const CreatureMeshData& mesh) -> float {
    float maximum = 0.0F;
    for (const auto& vertex : mesh.vertices) {
        maximum = std::max(maximum, vertex.material_class);
    }
    return maximum;
}

auto has_emissive_vertices(const CreatureMeshData& mesh) -> bool {
    return std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [](const CreatureVertex& vertex) {
        return vertex.emissive_strength > 0.5F;
    });
}

} // namespace

TEST_CASE("creature cycle classification matches day dusk night and dawn windows") {
    const auto day = EnvironmentClock::classify_creature_cycle(12.0F);
    const auto dusk = EnvironmentClock::classify_creature_cycle(18.5F);
    const auto night = EnvironmentClock::classify_creature_cycle(23.0F);
    const auto dawn = EnvironmentClock::classify_creature_cycle(5.25F);

    CHECK(day.phase == CreaturePhase::Day);
    CHECK(day.morph_factor == doctest::Approx(0.0F));
    CHECK(dusk.phase == CreaturePhase::DuskMorph);
    CHECK(dusk.morph_factor == doctest::Approx(0.5F));
    CHECK(night.phase == CreaturePhase::Night);
    CHECK(night.morph_factor == doctest::Approx(1.0F));
    CHECK(dawn.phase == CreaturePhase::DawnRecover);
    CHECK(dawn.morph_factor == doctest::Approx(0.75F));
}

TEST_CASE("creature spawn anchors are deterministic and follow chunk biome rules") {
    CreatureSystem system {};
    World world_a(7001, 2);
    World world_b(7001, 2);

    test::make_chunk_surface(world_a, {0, 0}, 12, to_block_id(BlockType::Sand), to_block_id(BlockType::Sand));
    test::make_chunk_surface(world_b, {0, 0}, 12, to_block_id(BlockType::Sand), to_block_id(BlockType::Sand));

    test::make_chunk_surface(world_a, {1, 0}, 13, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    test::make_chunk_surface(world_b, {1, 0}, 13, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    add_tree_patch(world_a, 18, 6, 13);
    add_tree_patch(world_a, 21, 8, 13);
    add_tree_patch(world_b, 18, 6, 13);
    add_tree_patch(world_b, 21, 8, 13);

    test::make_chunk_surface(world_a, {2, 0}, 52, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    test::make_chunk_surface(world_b, {2, 0}, 52, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    sculpt_lamb_hills(world_a, {2, 0}, 52);
    sculpt_lamb_hills(world_b, {2, 0}, 52);

    const auto sand_anchor_a = system.spawn_anchor_for_chunk(world_a, {0, 0});
    const auto sand_anchor_b = system.spawn_anchor_for_chunk(world_b, {0, 0});
    const auto rabbit_anchor = system.spawn_anchor_for_chunk(world_a, {1, 0});
    const auto lamb_anchor = system.spawn_anchor_for_chunk(world_a, {2, 0});

    REQUIRE(sand_anchor_a.has_value());
    REQUIRE(sand_anchor_b.has_value());
    CHECK(*sand_anchor_a == *sand_anchor_b);
    CHECK(sand_anchor_a->species == CreatureSpecies::Fennec);

    REQUIRE(rabbit_anchor.has_value());
    CHECK(rabbit_anchor->species == CreatureSpecies::Rabbit);

    REQUIRE(lamb_anchor.has_value());
    CHECK(lamb_anchor->species == CreatureSpecies::Lamb);
}

TEST_CASE("creature spawn is invalidated when the chunk cannot host a clear standing column") {
    CreatureSystem system {};
    World world(8123, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Sand), to_block_id(BlockType::Sand));

    for (int z = 0; z < kChunkSizeZ; ++z) {
        for (int x = 0; x < kChunkSizeX; ++x) {
            world.set_block(x, 13, z, to_block_id(BlockType::Stone));
        }
    }

    CHECK_FALSE(system.spawn_anchor_for_chunk(world, {0, 0}).has_value());
}

TEST_CASE("day creatures stay pacifist grounded and bounded around their spawn chunk") {
    CreatureSystem system {};
    World world(9001, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Sand), to_block_id(BlockType::Sand));

    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    const glm::vec3 player_position {2.5F, 13.001F, 2.5F};

    for (int frame = 0; frame < 240; ++frame) {
        system.update(1.0F / 60.0F, world, player_position, environment, cycle);
    }

    const auto creatures = system.active_creatures();
    REQUIRE(creatures.size() == 1);
    CHECK(creatures.front().phase == CreaturePhase::Day);
    CHECK(creatures.front().morph_factor == doctest::Approx(0.0F));
    CHECK_FALSE(contains_only_night_states(creatures.front().behavior_state));
    CHECK(creatures.front().position.y == doctest::Approx(13.001F).epsilon(0.01F));
    CHECK(horizontal_distance_squared(creatures.front().position, creatures.front().anchor.spawn_position) < 26.5F);
}

TEST_CASE("night creatures switch to horror states without enabling any attack logic") {
    CreatureSystem system {};
    World world(9002, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Sand), to_block_id(BlockType::Sand));

    const auto environment = EnvironmentClock::compute_state(23.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    const glm::vec3 player_position {4.0F, 13.001F, 4.0F};

    for (int frame = 0; frame < 120; ++frame) {
        system.update(1.0F / 60.0F, world, player_position, environment, cycle);
    }

    const auto creatures = system.active_creatures();
    const auto render_instances = system.render_instances();
    REQUIRE(creatures.size() == 1);
    REQUIRE(render_instances.size() == 1);
    CHECK(creatures.front().phase == CreaturePhase::Night);
    CHECK(creatures.front().morph_factor == doctest::Approx(1.0F));
    CHECK(contains_only_night_states(creatures.front().behavior_state));
    CHECK(render_instances.front().morph_factor == doctest::Approx(1.0F));
    CHECK(render_instances.front().tension > 0.3F);
}

TEST_CASE("creatures deactivate when leaving the activation radius and reactivate near the player") {
    CreatureSystem system {};
    World world(9003, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Sand), to_block_id(BlockType::Sand));

    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);

    system.update(0.0F, world, {2.0F, 13.001F, 2.0F}, environment, cycle);
    REQUIRE(system.active_creatures().size() == 1);

    system.update(0.0F, world, {static_cast<float>(kChunkSizeX * 10) + 0.5F, 13.001F, 2.0F}, environment, cycle);
    CHECK(system.active_creatures().empty());

    system.update(0.0F, world, {2.0F, 13.001F, 2.0F}, environment, cycle);
    REQUIRE(system.active_creatures().size() == 1);
    CHECK(system.active_creatures().front().position.y == doctest::Approx(13.001F).epsilon(0.01F));
}

TEST_CASE("dense spawn regions cap the number of active creatures to protect frame rate") {
    CreatureSystem system {};
    World world(9005, 4);

    for (int chunk_z = -4; chunk_z <= 4; ++chunk_z) {
        for (int chunk_x = -4; chunk_x <= 4; ++chunk_x) {
            test::make_chunk_surface(
                world,
                {chunk_x, chunk_z},
                12,
                to_block_id(BlockType::Sand),
                to_block_id(BlockType::Sand));
        }
    }

    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    const glm::vec3 player_position {0.5F, 13.001F, 0.5F};

    system.update(0.0F, world, player_position, environment, cycle);

    CHECK(system.active_creatures().size() == kCreatureMaxActiveCount);
    CHECK(system.render_instances().size() == kCreatureMaxActiveCount);
}

TEST_CASE("creature atlas exposes rich day materials and emissive nightmare details") {
    const auto atlas = build_creature_atlas_pixels();
    REQUIRE(atlas.size() == static_cast<std::size_t>(kCreatureAtlasSize * kCreatureAtlasSize * 4));
    bool has_emissive_pixels = false;
    for (std::size_t index = 3; index < atlas.size(); index += 4) {
        if (atlas[index] > 0) {
            has_emissive_pixels = true;
            break;
        }
    }
    CHECK(has_emissive_pixels);

    const auto rabbit_average = tile_average_rgba(atlas, CreatureAtlasTile::RabbitCoat);
    const auto fennec_average = tile_average_rgba(atlas, CreatureAtlasTile::FennecCoat);
    const auto lamb_average = tile_average_rgba(atlas, CreatureAtlasTile::LambWoolLight);
    const auto scar_average = tile_average_rgba(atlas, CreatureAtlasTile::NightmareScar);
    const auto eye_average = tile_average_rgba(atlas, CreatureAtlasTile::NightmareEye);

    CHECK(std::abs(rabbit_average[0] - fennec_average[0]) > 8.0F);
    CHECK(std::abs(lamb_average[2] - rabbit_average[2]) > 20.0F);
    CHECK(scar_average[3] > 18.0F);
    CHECK(eye_average[3] > 10.0F);
}

TEST_CASE("creature geometry exposes staged silhouettes and richer vertex materials") {
    for (const auto species : {CreatureSpecies::Rabbit, CreatureSpecies::Fennec, CreatureSpecies::Lamb}) {
        const CreatureRenderInstance day {
            species,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            0.5F,
            0.0F,
            1.0F,
            0.1F,
            1234U,
            CreatureBehaviorState::Idle,
            CreaturePhase::Day,
        };
        const CreatureRenderInstance dusk {
            species,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            0.9F,
            0.5F,
            0.55F,
            0.55F,
            1234U,
            CreatureBehaviorState::Twitch,
            CreaturePhase::DuskMorph,
            0.40F,
            0.70F,
        };
        const CreatureRenderInstance night {
            species,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            0.5F,
            1.0F,
            0.2F,
            0.9F,
            1234U,
            CreatureBehaviorState::Stare,
            CreaturePhase::Night,
            0.22F,
            0.92F,
        };

        const auto day_mesh = build_creature_mesh(day);
        const auto dusk_mesh = build_creature_mesh(dusk);
        const auto night_mesh = build_creature_mesh(night);
        const auto day_bounds = mesh_bounds(day_mesh);
        const auto dusk_bounds = mesh_bounds(dusk_mesh);
        const auto night_bounds = mesh_bounds(night_mesh);

        CAPTURE(static_cast<int>(species));
        CHECK_FALSE(day_mesh.empty());
        CHECK_FALSE(dusk_mesh.empty());
        CHECK_FALSE(night_mesh.empty());
        CHECK(dusk_mesh.vertices.size() > day_mesh.vertices.size());
        CHECK(night_mesh.part_count > day_mesh.part_count);
        CHECK(night_mesh.vertices.size() > day_mesh.vertices.size());
        CHECK(night_mesh.indices.size() > day_mesh.indices.size());
        CHECK(std::abs((dusk_bounds.max.x - dusk_bounds.min.x) - (day_bounds.max.x - day_bounds.min.x)) > 0.02F);
        const auto night_differs_from_dusk =
            std::abs((night_bounds.max.x - night_bounds.min.x) - (dusk_bounds.max.x - dusk_bounds.min.x)) > 0.02F ||
            std::abs((night_bounds.max.y - night_bounds.min.y) - (dusk_bounds.max.y - dusk_bounds.min.y)) > 0.02F;
        CHECK(night_differs_from_dusk);
        CHECK(all_vertex_attributes_are_bounded(day_mesh));
        CHECK(all_vertex_attributes_are_bounded(dusk_mesh));
        CHECK(all_vertex_attributes_are_bounded(night_mesh));
        CHECK(max_material_class(day_mesh) < 0.8F);
        CHECK(max_material_class(night_mesh) > 0.85F);
        CHECK(has_emissive_vertices(night_mesh));
    }
}

TEST_CASE("render instances expose motion and gaze signals for day and night creature poses") {
    CreatureSystem system {};
    World world(9004, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Sand), to_block_id(BlockType::Sand));

    const auto day_environment = EnvironmentClock::compute_state(12.0F);
    const auto day_cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    system.update(0.0F, world, {15.5F, 13.001F, 15.5F}, day_environment, day_cycle);
    REQUIRE(system.active_creatures().size() == 1);
    REQUIRE(system.render_instances().size() == 1);

    const auto baseline_motion = system.render_instances().front().motion_amount;
    const auto creature_position = system.active_creatures().front().position;
    const auto close_player_position = creature_position + glm::vec3 {0.15F, 0.0F, 0.0F};

    for (int frame = 0; frame < 30; ++frame) {
        system.update(1.0F / 60.0F, world, close_player_position, day_environment, day_cycle);
    }

    REQUIRE(system.render_instances().size() == 1);
    CHECK(system.render_instances().front().motion_amount > baseline_motion + 0.08F);
    CHECK(system.render_instances().front().gaze_weight < 0.5F);

    const auto night_environment = EnvironmentClock::compute_state(23.0F);
    const auto night_cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    bool found_stare = false;
    int stare_frames = 0;
    float stare_motion_amount = 1.0F;
    float stare_gaze_weight = 0.0F;

    for (int frame = 0; frame < 1200; ++frame) {
        system.update(1.0F / 60.0F, world, close_player_position, night_environment, night_cycle);
        if (!system.active_creatures().empty() && system.active_creatures().front().behavior_state == CreatureBehaviorState::Stare) {
            found_stare = true;
            ++stare_frames;
            stare_motion_amount = std::min(stare_motion_amount, system.render_instances().front().motion_amount);
            stare_gaze_weight = std::max(stare_gaze_weight, system.render_instances().front().gaze_weight);
            if (stare_frames >= 20) {
                break;
            }
        }
    }

    REQUIRE(found_stare);
    CHECK(stare_motion_amount < 0.3F);
    CHECK(stare_gaze_weight > 0.75F);
}

} // namespace valcraft
