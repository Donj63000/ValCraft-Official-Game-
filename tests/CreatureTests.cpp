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

void sculpt_sheep_hills(World& world, const ChunkCoord& coord, int base_height) {
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

auto is_hostile_state(CreatureBehaviorState state) -> bool {
    return state == CreatureBehaviorState::Chase || state == CreatureBehaviorState::Strike;
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

auto tile_alpha_coverage(const std::vector<std::uint8_t>& atlas, CreatureAtlasTile tile, std::uint8_t threshold = 1) -> float {
    const auto coordinates = creature_atlas_tile_coordinates(tile);
    const auto start_x = coordinates[0] * kCreatureAtlasTileSize;
    const auto start_y = coordinates[1] * kCreatureAtlasTileSize;

    int alpha_pixels = 0;
    for (int y = 0; y < kCreatureAtlasTileSize; ++y) {
        for (int x = 0; x < kCreatureAtlasTileSize; ++x) {
            const auto index = static_cast<std::size_t>(((start_y + y) * kCreatureAtlasSize + (start_x + x)) * 4);
            alpha_pixels += atlas[index + 3] >= threshold ? 1 : 0;
        }
    }

    const auto texel_count = static_cast<float>(kCreatureAtlasTileSize * kCreatureAtlasTileSize);
    return static_cast<float>(alpha_pixels) / texel_count;
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

auto meshes_match_exactly(const CreatureMeshData& lhs, const CreatureMeshData& rhs) -> bool {
    if (lhs.part_count != rhs.part_count || lhs.indices != rhs.indices || lhs.vertices.size() != rhs.vertices.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.vertices.size(); ++index) {
        const auto& a = lhs.vertices[index];
        const auto& b = rhs.vertices[index];
        if (a.x != b.x || a.y != b.y || a.z != b.z ||
            a.u != b.u || a.v != b.v ||
            a.nx != b.nx || a.ny != b.ny || a.nz != b.nz ||
            a.nightmare_factor != b.nightmare_factor ||
            a.tension != b.tension ||
            a.material_class != b.material_class ||
            a.cavity_mask != b.cavity_mask ||
            a.emissive_strength != b.emissive_strength) {
            return false;
        }
    }

    return true;
}

auto max_position_delta(const CreatureMeshData& lhs, const CreatureMeshData& rhs) -> float {
    if (lhs.vertices.size() != rhs.vertices.size()) {
        return std::numeric_limits<float>::max();
    }
    float max_delta = 0.0F;
    for (std::size_t index = 0; index < lhs.vertices.size(); ++index) {
        const auto& a = lhs.vertices[index];
        const auto& b = rhs.vertices[index];
        max_delta = std::max(max_delta, std::abs(a.x - b.x));
        max_delta = std::max(max_delta, std::abs(a.y - b.y));
        max_delta = std::max(max_delta, std::abs(a.z - b.z));
    }
    return max_delta;
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

auto body_volume_proxy(const CreatureMeshData& mesh) -> float {
    MeshBounds bounds {
        glm::vec3 {std::numeric_limits<float>::max()},
        glm::vec3 {std::numeric_limits<float>::lowest()},
    };
    bool found = false;
    for (const auto& vertex : mesh.vertices) {
        if (vertex.y < 0.20F || vertex.y > 1.10F) {
            continue;
        }
        found = true;
        bounds.min.x = std::min(bounds.min.x, vertex.x);
        bounds.min.y = std::min(bounds.min.y, vertex.y);
        bounds.min.z = std::min(bounds.min.z, vertex.z);
        bounds.max.x = std::max(bounds.max.x, vertex.x);
        bounds.max.y = std::max(bounds.max.y, vertex.y);
        bounds.max.z = std::max(bounds.max.z, vertex.z);
    }

    if (!found) {
        return 0.0F;
    }

    return (bounds.max.x - bounds.min.x) * (bounds.max.y - bounds.min.y) * (bounds.max.z - bounds.min.z);
}

} // namespace

TEST_CASE("creature cycle classification uses explicit dusk night and dawn boundaries") {
    const auto dusk_start = EnvironmentClock::classify_creature_cycle(18.0F);
    const auto dusk_mid = EnvironmentClock::classify_creature_cycle(18.5F);
    const auto night_start = EnvironmentClock::classify_creature_cycle(19.0F);
    const auto night_end = EnvironmentClock::classify_creature_cycle(4.99F);
    const auto dawn_start = EnvironmentClock::classify_creature_cycle(5.0F);
    const auto dawn_mid = EnvironmentClock::classify_creature_cycle(5.5F);
    const auto day = EnvironmentClock::classify_creature_cycle(6.0F);

    CHECK(dusk_start.phase == CreaturePhase::DuskMorph);
    CHECK(dusk_start.morph_factor == doctest::Approx(0.0F));
    CHECK(dusk_mid.phase == CreaturePhase::DuskMorph);
    CHECK(dusk_mid.morph_factor == doctest::Approx(0.5F));
    CHECK(night_start.phase == CreaturePhase::Night);
    CHECK(night_start.morph_factor == doctest::Approx(1.0F));
    CHECK(night_end.phase == CreaturePhase::Night);
    CHECK(dawn_start.phase == CreaturePhase::DawnRecover);
    CHECK(dawn_start.morph_factor == doctest::Approx(1.0F));
    CHECK(dawn_mid.phase == CreaturePhase::DawnRecover);
    CHECK(dawn_mid.morph_factor == doctest::Approx(0.5F));
    CHECK(day.phase == CreaturePhase::Day);
    CHECK(day.morph_factor == doctest::Approx(0.0F));
}

TEST_CASE("creature spawn anchors map grass chunks to pig cow sheep and reject desert chunks") {
    CreatureSystem system {};
    World world(7001, 2);

    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    test::make_chunk_surface(world, {1, 0}, 13, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    test::make_chunk_surface(world, {2, 0}, 52, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    test::make_chunk_surface(world, {3, 0}, 12, to_block_id(BlockType::Sand), to_block_id(BlockType::Sand));

    for (const auto x : {18, 21, 24, 27}) {
        add_tree_patch(world, x, 6 + (x % 3), 13);
    }
    sculpt_sheep_hills(world, {2, 0}, 52);

    const auto cow_anchor = system.spawn_anchor_for_chunk(world, {0, 0});
    const auto pig_anchor = system.spawn_anchor_for_chunk(world, {1, 0});
    const auto sheep_anchor = system.spawn_anchor_for_chunk(world, {2, 0});
    const auto desert_anchor = system.spawn_anchor_for_chunk(world, {3, 0});

    REQUIRE(cow_anchor.has_value());
    REQUIRE(pig_anchor.has_value());
    REQUIRE(sheep_anchor.has_value());
    CHECK(cow_anchor->species == CreatureSpecies::Cow);
    CHECK(pig_anchor->species == CreatureSpecies::Pig);
    CHECK(sheep_anchor->species == CreatureSpecies::Sheep);
    CHECK_FALSE(desert_anchor.has_value());
}

TEST_CASE("day creatures stay passive grounded and emit no attack events") {
    CreatureSystem system {};
    World world(9001, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

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
    CHECK_FALSE(is_hostile_state(creatures.front().behavior_state));
    CHECK(system.recent_attacks().empty());
    CHECK(creatures.front().position.y == doctest::Approx(13.001F).epsilon(0.01F));
    CHECK(horizontal_distance_squared(creatures.front().position, creatures.front().anchor.spawn_position) < 26.5F);
}

TEST_CASE("creatures enter chase exactly at 19 and stop attacking immediately at dawn") {
    CreatureSystem system {};
    World world(9002, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto night_environment = EnvironmentClock::compute_state(19.0F);
    const auto night_cycle = EnvironmentClock::classify_creature_cycle(19.0F);
    system.update(0.0F, world, {14.5F, 13.001F, 14.5F}, night_environment, night_cycle);
    REQUIRE(system.active_creatures().size() == 1);

    const auto spawn_position = system.active_creatures().front().position;
    const auto chase_player_position = spawn_position + glm::vec3 {2.4F, 0.0F, 0.0F};
    system.update(1.0F / 60.0F, world, chase_player_position, night_environment, night_cycle);

    REQUIRE(system.active_creatures().size() == 1);
    CHECK(system.active_creatures().front().phase == CreaturePhase::Night);
    CHECK(system.active_creatures().front().behavior_state == CreatureBehaviorState::Chase);
    CHECK(system.render_instances().front().attack_amount > 0.15F);

    const auto strike_player_position = spawn_position + glm::vec3 {0.75F, 0.0F, 0.0F};
    bool attacked = false;
    for (int frame = 0; frame < 90; ++frame) {
        system.update(1.0F / 60.0F, world, strike_player_position, night_environment, night_cycle);
        if (!system.recent_attacks().empty()) {
            attacked = true;
            break;
        }
    }
    REQUIRE(attacked);
    CHECK(system.active_creatures().front().behavior_state == CreatureBehaviorState::Strike);

    const auto dawn_environment = EnvironmentClock::compute_state(5.0F);
    const auto dawn_cycle = EnvironmentClock::classify_creature_cycle(5.0F);
    system.update(1.0F / 60.0F, world, strike_player_position, dawn_environment, dawn_cycle);

    REQUIRE(system.active_creatures().size() == 1);
    CHECK(system.active_creatures().front().phase == CreaturePhase::DawnRecover);
    CHECK_FALSE(is_hostile_state(system.active_creatures().front().behavior_state));
    CHECK(system.recent_attacks().empty());
    CHECK(system.render_instances().front().attack_amount < 0.45F);
}

TEST_CASE("night melee attacks emit stable zombie damage and aggressive render signals") {
    CreatureSystem system {};
    World world(9003, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto environment = EnvironmentClock::compute_state(23.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    system.update(0.0F, world, {14.5F, 13.001F, 14.5F}, environment, cycle);
    REQUIRE(system.active_creatures().size() == 1);

    const auto spawn_position = system.active_creatures().front().position;
    const auto close_player_position = spawn_position + glm::vec3 {0.8F, 0.0F, 0.0F};
    float max_attack_amount = 0.0F;
    bool attacked = false;

    for (int frame = 0; frame < 120; ++frame) {
        system.update(1.0F / 60.0F, world, close_player_position, environment, cycle);
        REQUIRE(system.render_instances().size() == 1);
        max_attack_amount = std::max(max_attack_amount, system.render_instances().front().attack_amount);
        if (system.recent_attacks().empty()) {
            continue;
        }

        attacked = true;
        CHECK(system.recent_attacks().front().damage == doctest::Approx(3.0F));
        CHECK(system.recent_attacks().front().species == system.active_creatures().front().anchor.species);
        CHECK(system.active_creatures().front().behavior_state == CreatureBehaviorState::Strike);
        break;
    }

    REQUIRE(attacked);
    CHECK(max_attack_amount > 0.6F);
}

TEST_CASE("dense grassy spawn regions still cap active creature counts") {
    CreatureSystem system {};
    World world(9004, 4);

    for (int chunk_z = -4; chunk_z <= 4; ++chunk_z) {
        for (int chunk_x = -4; chunk_x <= 4; ++chunk_x) {
            test::make_chunk_surface(
                world,
                {chunk_x, chunk_z},
                12,
                to_block_id(BlockType::Grass),
                to_block_id(BlockType::Dirt));
        }
    }

    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    const glm::vec3 player_position {0.5F, 13.001F, 0.5F};

    system.update(0.0F, world, player_position, environment, cycle);

    CHECK(system.active_creatures().size() == kCreatureMaxActiveCount);
    CHECK(system.render_instances().size() == kCreatureMaxActiveCount);
}

TEST_CASE("creature atlas exposes distinct farm animals and emissive zombie details") {
    const auto atlas = build_creature_atlas_pixels();
    REQUIRE(atlas.size() == static_cast<std::size_t>(kCreatureAtlasSize * kCreatureAtlasSize * 4));

    const auto pig_average = tile_average_rgba(atlas, CreatureAtlasTile::PigHide);
    const auto cow_average = tile_average_rgba(atlas, CreatureAtlasTile::CowHide);
    const auto sheep_average = tile_average_rgba(atlas, CreatureAtlasTile::SheepWool);
    const auto eye_average = tile_average_rgba(atlas, CreatureAtlasTile::ZombieEye);
    const auto scar_average = tile_average_rgba(atlas, CreatureAtlasTile::ZombieScar);

    CHECK(pig_average[0] > cow_average[0] + 18.0F);
    CHECK(sheep_average[2] > pig_average[2] + 32.0F);
    CHECK(tile_average_rgba(atlas, CreatureAtlasTile::PigSnout)[0] > 180.0F);
    CHECK(tile_average_rgba(atlas, CreatureAtlasTile::CowHorn)[0] > 120.0F);
    CHECK(tile_average_rgba(atlas, CreatureAtlasTile::SheepHoof)[0] < 90.0F);
    CHECK(eye_average[3] > 60.0F);
    CHECK(scar_average[3] > 5.0F);
    CHECK(tile_alpha_coverage(atlas, CreatureAtlasTile::ZombieEye) > 0.35F);
    CHECK(tile_alpha_coverage(atlas, CreatureAtlasTile::ZombieScar) > 0.04F);
    CHECK(tile_alpha_coverage(atlas, CreatureAtlasTile::ZombieScar) < 0.24F);
    CHECK(tile_alpha_coverage(atlas, CreatureAtlasTile::ZombieVein) > 0.08F);
    CHECK(tile_alpha_coverage(atlas, CreatureAtlasTile::ZombieVein) < 0.50F);
}

TEST_CASE("creature geometry stretches day animals into deterministic long-limbed zombies") {
    for (const auto species : {CreatureSpecies::Pig, CreatureSpecies::Cow, CreatureSpecies::Sheep}) {
        const CreatureRenderInstance day {
            species,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            0.50F,
            0.0F,
            1.0F,
            0.10F,
            1234U,
            CreatureBehaviorState::Idle,
            CreaturePhase::Day,
            0.10F,
            0.20F,
            0.0F,
        };
        const CreatureRenderInstance night {
            species,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            0.80F,
            1.0F,
            0.18F,
            0.95F,
            1234U,
            CreatureBehaviorState::Strike,
            CreaturePhase::Night,
            0.85F,
            0.92F,
            1.0F,
        };

        const auto day_mesh = build_creature_mesh(day);
        const auto night_mesh = build_creature_mesh(night);
        const auto day_bounds = mesh_bounds(day_mesh);
        const auto night_bounds = mesh_bounds(night_mesh);

        CAPTURE(static_cast<int>(species));
        CHECK_FALSE(day_mesh.empty());
        CHECK_FALSE(night_mesh.empty());
        CHECK(night_mesh.part_count > day_mesh.part_count);
        CHECK(night_mesh.vertices.size() > day_mesh.vertices.size());
        CHECK((night_bounds.max.y - night_bounds.min.y) > (day_bounds.max.y - day_bounds.min.y) + 0.18F);
        CHECK(max_material_class(day_mesh) < 0.70F);
        CHECK(max_material_class(night_mesh) > 0.85F);
        CHECK(has_emissive_vertices(night_mesh));
        CHECK(all_vertex_attributes_are_bounded(day_mesh));
        CHECK(all_vertex_attributes_are_bounded(night_mesh));
    }
}

TEST_CASE("day species silhouettes differ and appearance variation remains deterministic per seed") {
    CreatureMeshData pig_mesh {};
    CreatureMeshData cow_mesh {};
    CreatureMeshData sheep_mesh {};

    for (const auto species : {CreatureSpecies::Pig, CreatureSpecies::Cow, CreatureSpecies::Sheep}) {
        const CreatureRenderInstance seed_a {
            species,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            0.70F,
            0.45F,
            0.70F,
            0.42F,
            2222U,
            CreatureBehaviorState::Lurk,
            CreaturePhase::DuskMorph,
            0.45F,
            0.52F,
            0.25F,
        };
        auto seed_b = seed_a;
        seed_b.appearance_seed = 7788U;
        seed_b.animation_time = 1.05F;

        const auto mesh_a = build_creature_mesh(seed_a);
        const auto mesh_a_repeat = build_creature_mesh(seed_a);
        const auto mesh_b = build_creature_mesh(seed_b);

        CAPTURE(static_cast<int>(species));
        CHECK(meshes_match_exactly(mesh_a, mesh_a_repeat));
        CHECK(mesh_a.part_count == mesh_b.part_count);
        CHECK_FALSE(meshes_match_exactly(mesh_a, mesh_b));
        CHECK(max_position_delta(mesh_a, mesh_b) > 0.003F);

        switch (species) {
        case CreatureSpecies::Pig:
            pig_mesh = build_creature_mesh({
                species, {0.0F, 0.0F, 0.0F}, 0.0F, 0.30F, 0.0F, 1.0F, 0.10F, 999U,
                CreatureBehaviorState::Idle, CreaturePhase::Day, 0.10F, 0.10F, 0.0F
            });
            break;
        case CreatureSpecies::Cow:
            cow_mesh = build_creature_mesh({
                species, {0.0F, 0.0F, 0.0F}, 0.0F, 0.30F, 0.0F, 1.0F, 0.10F, 999U,
                CreatureBehaviorState::Idle, CreaturePhase::Day, 0.10F, 0.10F, 0.0F
            });
            break;
        case CreatureSpecies::Sheep:
            sheep_mesh = build_creature_mesh({
                species, {0.0F, 0.0F, 0.0F}, 0.0F, 0.30F, 0.0F, 1.0F, 0.10F, 999U,
                CreatureBehaviorState::Idle, CreaturePhase::Day, 0.10F, 0.10F, 0.0F
            });
            break;
        }
    }

    CHECK_FALSE(meshes_match_exactly(pig_mesh, cow_mesh));
    CHECK_FALSE(meshes_match_exactly(pig_mesh, sheep_mesh));
    CHECK(body_volume_proxy(cow_mesh) > body_volume_proxy(pig_mesh));
    CHECK(body_volume_proxy(sheep_mesh) > body_volume_proxy(pig_mesh));
}

TEST_CASE("render instances surface motion gaze and attack signals across day and night") {
    CreatureSystem system {};
    World world(9005, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto day_environment = EnvironmentClock::compute_state(12.0F);
    const auto day_cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    system.update(0.0F, world, {15.5F, 13.001F, 15.5F}, day_environment, day_cycle);
    REQUIRE(system.active_creatures().size() == 1);

    const auto baseline_motion = system.render_instances().front().motion_amount;
    const auto creature_position = system.active_creatures().front().position;
    const auto close_player_position = creature_position + glm::vec3 {0.20F, 0.0F, 0.0F};

    for (int frame = 0; frame < 30; ++frame) {
        system.update(1.0F / 60.0F, world, close_player_position, day_environment, day_cycle);
    }

    REQUIRE(system.render_instances().size() == 1);
    CHECK(system.render_instances().front().motion_amount > baseline_motion + 0.08F);
    CHECK(system.render_instances().front().attack_amount == doctest::Approx(0.0F).epsilon(0.05F));

    const auto night_environment = EnvironmentClock::compute_state(23.0F);
    const auto night_cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    for (int frame = 0; frame < 60; ++frame) {
        system.update(1.0F / 60.0F, world, close_player_position, night_environment, night_cycle);
    }

    REQUIRE(system.render_instances().size() == 1);
    CHECK(system.render_instances().front().phase == CreaturePhase::Night);
    CHECK(system.render_instances().front().gaze_weight > 0.75F);
    CHECK(system.render_instances().front().attack_amount > 0.40F);
}

} // namespace valcraft
