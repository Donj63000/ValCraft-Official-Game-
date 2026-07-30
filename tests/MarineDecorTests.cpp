#include "render/MarineDecor.h"

#include "world/OceanAdventureLayout.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace valcraft {
namespace {

[[nodiscard]] auto flat_surface(
    int surface_height,
    int water_level,
    BlockType substrate = BlockType::Sand) -> MarineTerrainSurfaceSampler {
    return [surface_height, water_level, substrate](int, int) {
        TerrainSurfaceSample sample {};
        sample.surface_height = surface_height;
        sample.water_level = water_level;
        sample.surface_block = to_block_id(substrate);
        return sample;
    };
}

[[nodiscard]] constexpr auto expected_material(
    MarineDecorKind kind) noexcept -> VisualMaterialId {
    switch (kind) {
    case MarineDecorKind::Seagrass:
        return VisualMaterialId::MarineSeagrass;
    case MarineDecorKind::Kelp:
        return VisualMaterialId::MarineKelp;
    case MarineDecorKind::CoralFan:
        return VisualMaterialId::CoralFan;
    case MarineDecorKind::BranchCoralWarm:
        return VisualMaterialId::CoralWarm;
    case MarineDecorKind::BranchCoralLagoon:
        return VisualMaterialId::CoralLagoon;
    case MarineDecorKind::Shell:
        return VisualMaterialId::MarineShell;
    }
    return VisualMaterialId::None;
}

[[nodiscard]] constexpr auto inside_expanded_rectangle(
    int world_x,
    int world_z,
    int min_x,
    int max_x,
    int min_z,
    int max_z,
    int margin) noexcept -> bool {
    return world_x >= min_x - margin && world_x <= max_x + margin &&
           world_z >= min_z - margin && world_z <= max_z + margin;
}

[[nodiscard]] auto all_fields_are_finite(
    const MarineDecorInstance& instance) noexcept -> bool {
    return std::isfinite(instance.position_x) &&
           std::isfinite(instance.position_y) &&
           std::isfinite(instance.position_z) &&
           std::isfinite(instance.scale_x) &&
           std::isfinite(instance.scale_y) &&
           std::isfinite(instance.scale_z) &&
           std::isfinite(instance.yaw_radians) &&
           std::isfinite(instance.phase);
}

} // namespace

TEST_CASE("le decor marin reste reserve aux archipels oceaniques") {
    const auto sampler = flat_surface(40, kSeaLevel);

    CHECK(build_marine_decor(
              {8, 8},
              WorldGenerationVersion::LegacyV1,
              1729,
              sampler)
              .empty());
    CHECK_FALSE(build_marine_decor(
                    {8, 8},
                    WorldGenerationVersion::SparseArchipelagoV2,
                    1729,
                    sampler)
                    .empty());
    CHECK_FALSE(build_marine_decor(
                    {8, 8},
                    WorldGenerationVersion::LivingOceanV3,
                    1729,
                    sampler)
                    .empty());
    CHECK(build_marine_decor(
              {8, 8},
              WorldGenerationVersion::Latest,
              1729,
              sampler)
              .empty());

    const MarineTerrainSurfaceSampler absent_sampler {};
    CHECK(build_marine_decor(
              {8, 8},
              WorldGenerationVersion::LivingOceanV3,
              1729,
              absent_sampler)
              .empty());
}

TEST_CASE("le decor marin est deterministe et borne a quarante-huit instances") {
    const auto sampler = flat_surface(40, kSeaLevel);
    const auto first = build_marine_decor(
        {11, 9},
        WorldGenerationVersion::LivingOceanV3,
        0x1635,
        sampler);
    const auto repeated = build_marine_decor(
        {11, 9},
        WorldGenerationVersion::LivingOceanV3,
        0x1635,
        sampler);
    const auto other_seed = build_marine_decor(
        {11, 9},
        WorldGenerationVersion::LivingOceanV3,
        0x1636,
        sampler);

    REQUIRE_FALSE(first.empty());
    CHECK(first == repeated);
    CHECK(first != other_seed);
    CHECK(first.size() <= kMarineDecorMaxInstancesPerChunk);
    CHECK(other_seed.size() <= kMarineDecorMaxInstancesPerChunk);
}

TEST_CASE("le top quarante-huit est choisi par hash sans favoriser la fin du chunk") {
    struct ExpectedCandidate {
        float position_x = 0.0F;
        float position_z = 0.0F;
        std::uint32_t score = 0U;
        std::uint8_t ordinal = 0U;
    };

    constexpr ChunkCoord kChunk {11, 9};
    constexpr auto kOriginX = kChunk.x * kChunkSizeX;
    constexpr auto kOriginZ = kChunk.z * kChunkSizeZ;
    auto selected_seed = 0;
    std::vector<ExpectedCandidate> expected;
    for (int seed = 0; seed < 512; ++seed) {
        expected.clear();
        for (int local_z = 0; local_z < kChunkSizeZ;
             local_z += kMarineDecorGridStep) {
            for (int local_x = 0; local_x < kChunkSizeX;
                 local_x += kMarineDecorGridStep) {
                const auto grid_x = kOriginX + local_x;
                const auto grid_z = kOriginZ + local_z;
                const auto candidate_hash =
                    ocean_adventure_layout_hash(
                        grid_x,
                        grid_z,
                        seed,
                        0x4D415249U);
                if ((candidate_hash & 3U) == 0U) {
                    continue;
                }
                const auto world_x =
                    grid_x +
                    static_cast<int>(
                        (candidate_hash >> 6U) & 1U);
                const auto world_z =
                    grid_z +
                    static_cast<int>(
                        (candidate_hash >> 7U) & 1U);
                const auto ordinal =
                    static_cast<std::uint8_t>(
                        (local_z / kMarineDecorGridStep) *
                            (kChunkSizeX /
                             kMarineDecorGridStep) +
                        (local_x /
                         kMarineDecorGridStep));
                expected.push_back({
                    static_cast<float>(world_x) + 0.5F,
                    static_cast<float>(world_z) + 0.5F,
                    ocean_adventure_layout_hash(
                        world_x,
                        world_z,
                        seed,
                        0x53454C45U),
                    ordinal,
                });
            }
        }
        if (expected.size() >
            kMarineDecorMaxInstancesPerChunk) {
            selected_seed = seed;
            break;
        }
    }
    REQUIRE(
        expected.size() >
        kMarineDecorMaxInstancesPerChunk);

    std::sort(
        expected.begin(),
        expected.end(),
        [](const ExpectedCandidate& left,
           const ExpectedCandidate& right) {
            return left.score < right.score ||
                   (left.score == right.score &&
                    left.ordinal < right.ordinal);
        });
    expected.resize(kMarineDecorMaxInstancesPerChunk);
    std::sort(
        expected.begin(),
        expected.end(),
        [](const ExpectedCandidate& left,
           const ExpectedCandidate& right) {
            return left.ordinal < right.ordinal;
        });

    const auto actual = build_marine_decor(
        kChunk,
        WorldGenerationVersion::LivingOceanV3,
        selected_seed,
        flat_surface(40, kSeaLevel));
    REQUIRE(
        actual.size() ==
        kMarineDecorMaxInstancesPerChunk);
    for (std::size_t index = 0U;
         index < actual.size();
         ++index) {
        CAPTURE(selected_seed);
        CAPTURE(index);
        CHECK(
            actual[index].position_x ==
            expected[index].position_x);
        CHECK(
            actual[index].position_z ==
            expected[index].position_z);
    }
}

TEST_CASE("les instances marines sont compactes finies ancrees et coherentes") {
    CHECK(sizeof(MarineDecorInstance) == 36U);

    constexpr int kSurfaceHeight = 40;
    constexpr int kDepth = kSeaLevel - kSurfaceHeight;
    const auto instances = build_marine_decor(
        {9, 7},
        WorldGenerationVersion::LivingOceanV3,
        4471,
        flat_surface(kSurfaceHeight, kSeaLevel));
    REQUIRE_FALSE(instances.empty());

    for (const auto& instance : instances) {
        CAPTURE(static_cast<int>(instance.kind));
        CAPTURE(instance.position_x);
        CAPTURE(instance.position_z);
        CHECK(all_fields_are_finite(instance));
        CHECK(instance.position_y ==
              doctest::Approx(static_cast<float>(kSurfaceHeight + 1)));
        CHECK(instance.scale_x > 0.0F);
        CHECK(instance.scale_y > 0.0F);
        CHECK(instance.scale_z > 0.0F);
        CHECK(instance.yaw_radians >= 0.0F);
        CHECK(instance.yaw_radians < 2.0F * std::numbers::pi_v<float>);
        CHECK(instance.phase >= 0.0F);
        CHECK(instance.phase < 2.0F * std::numbers::pi_v<float>);
        CHECK(instance.material == expected_material(instance.kind));
        CHECK(instance.reserved == 0U);

        if (instance.kind == MarineDecorKind::Kelp) {
            CHECK(instance.scale_y <=
                  std::min(6.0F, static_cast<float>(kDepth) - 1.5F));
        }
    }
}

TEST_CASE("chaque famille marine respecte sa plage de profondeur") {
    std::array<bool, 6> observed {};
    for (int seed = 0; seed < 12; ++seed) {
        for (int chunk_x = 7; chunk_x < 11; ++chunk_x) {
            const auto instances = build_marine_decor(
                {chunk_x, 9},
                WorldGenerationVersion::LivingOceanV3,
                seed,
                flat_surface(40, kSeaLevel));
            for (const auto& instance : instances) {
                observed[static_cast<std::size_t>(instance.kind)] = true;
            }
        }
    }
    for (const auto was_observed : observed) {
        CHECK(was_observed);
    }

    const auto shallow = build_marine_decor(
        {8, 10},
        WorldGenerationVersion::LivingOceanV3,
        881,
        flat_surface(kSeaLevel - 4, kSeaLevel));
    for (const auto& instance : shallow) {
        CHECK(instance.kind != MarineDecorKind::Kelp);
    }

    const auto middle_depth = build_marine_decor(
        {8, 10},
        WorldGenerationVersion::LivingOceanV3,
        881,
        flat_surface(kSeaLevel - 22, kSeaLevel));
    REQUIRE_FALSE(middle_depth.empty());

    auto deepest_supported =
        std::vector<MarineDecorInstance> {};
    for (int seed = 0;
         seed < 64 && deepest_supported.empty();
         ++seed) {
        deepest_supported = build_marine_decor(
            {8, 10},
            WorldGenerationVersion::LivingOceanV3,
            seed,
            flat_surface(kSeaLevel - 36, kSeaLevel));
    }
    REQUIRE_FALSE(deepest_supported.empty());
    for (const auto& instance : deepest_supported) {
        CHECK(instance.kind == MarineDecorKind::Shell);
    }

    CHECK(
        build_marine_decor(
            {8, 10},
            WorldGenerationVersion::LivingOceanV3,
            881,
            flat_surface(kSeaLevel - 37, kSeaLevel))
            .empty());
}

TEST_CASE("le substrat l'immersion et la pente filtrent les candidats") {
    for (const auto substrate :
         {BlockType::Sand, BlockType::Gravel, BlockType::MossyStone}) {
        CHECK_FALSE(build_marine_decor(
                        {9, 8},
                        WorldGenerationVersion::LivingOceanV3,
                        90210,
                        flat_surface(40, kSeaLevel, substrate))
                        .empty());
    }

    CHECK(build_marine_decor(
              {9, 8},
              WorldGenerationVersion::LivingOceanV3,
              90210,
              flat_surface(40, kSeaLevel, BlockType::Stone))
              .empty());
    CHECK(build_marine_decor(
              {9, 8},
              WorldGenerationVersion::LivingOceanV3,
              90210,
              flat_surface(40, kWorldMinY - 1))
              .empty());

    const MarineTerrainSurfaceSampler steep = [](int world_x, int world_z) {
        TerrainSurfaceSample sample {};
        sample.surface_height = ((world_x + world_z) & 1) == 0 ? 31 : 38;
        sample.water_level = kSeaLevel;
        sample.surface_block = to_block_id(BlockType::Sand);
        return sample;
    };
    CHECK(build_marine_decor(
              {9, 8},
              WorldGenerationVersion::LivingOceanV3,
              90210,
              steep)
              .empty());
}

TEST_CASE("le chenal le port et son bassin restent sans decor marin") {
    const auto sampler = flat_surface(40, kSeaLevel);

    CHECK(build_marine_decor(
              {0, 0},
              WorldGenerationVersion::LivingOceanV3,
              2026,
              sampler)
              .empty());
    CHECK(build_marine_decor(
              {-4, -2},
              WorldGenerationVersion::LivingOceanV3,
              2026,
              sampler)
              .empty());
    CHECK(build_marine_decor(
              {-2, -2},
              WorldGenerationVersion::LivingOceanV3,
              2026,
              sampler)
              .empty());

    for (int chunk_z = -6; chunk_z <= 3; ++chunk_z) {
        for (int chunk_x = -6; chunk_x <= 2; ++chunk_x) {
            const auto instances = build_marine_decor(
                {chunk_x, chunk_z},
                WorldGenerationVersion::LivingOceanV3,
                2026,
                sampler);
            for (const auto& instance : instances) {
                const auto world_x =
                    static_cast<int>(std::floor(instance.position_x));
                const auto world_z =
                    static_cast<int>(std::floor(instance.position_z));
                CHECK_FALSE(
                    is_ocean_navigation_corridor_column(world_x, world_z));
                CHECK_FALSE(inside_expanded_rectangle(
                    world_x,
                    world_z,
                    kStartingPortMinX,
                    kStartingPortMaxX,
                    kStartingPortMinZ,
                    kStartingPortMaxZ,
                    3));
                CHECK_FALSE(inside_expanded_rectangle(
                    world_x,
                    world_z,
                    kStartingPortBasinMinX,
                    kStartingPortBasinMaxX,
                    kStartingPortBasinMinZ,
                    kStartingPortBasinMaxZ,
                    3));
            }
        }
    }
}

TEST_CASE("les coordonnees negatives gardent le meme contrat de chunk") {
    constexpr ChunkCoord kChunk {-8, -9};
    const auto sampler = flat_surface(36, kSeaLevel);
    const auto first = build_marine_decor(
        kChunk,
        WorldGenerationVersion::LivingOceanV3,
        -337,
        sampler);
    const auto second = build_marine_decor(
        kChunk,
        WorldGenerationVersion::LivingOceanV3,
        -337,
        sampler);

    REQUIRE_FALSE(first.empty());
    CHECK(first == second);
    for (const auto& instance : first) {
        CHECK(instance.position_x >=
              static_cast<float>(kChunk.x * kChunkSizeX));
        CHECK(instance.position_x <
              static_cast<float>((kChunk.x + 1) * kChunkSizeX));
        CHECK(instance.position_z >=
              static_cast<float>(kChunk.z * kChunkSizeZ));
        CHECK(instance.position_z <
              static_cast<float>((kChunk.z + 1) * kChunkSizeZ));
        CHECK(all_fields_are_finite(instance));
    }
}

} // namespace valcraft
