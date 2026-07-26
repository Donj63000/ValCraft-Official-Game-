#include "render/VisualVegetation.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace valcraft {

namespace {

using TestBlocks = std::map<std::tuple<int, int, int>, BlockId>;

[[nodiscard]] auto make_sampler(const TestBlocks& blocks) -> VisualVegetationSampler {
    return [&blocks](int x, int y, int z) {
        const auto found = blocks.find({x, y, z});
        return found == blocks.end()
            ? to_block_id(BlockType::Air)
            : found->second;
    };
}

void place_oak(TestBlocks& blocks, int x, int y, int z) {
    const auto wood = to_block_id(BlockType::Wood);
    const auto leaves = to_block_id(BlockType::Leaves);
    for (int offset_y = 0; offset_y < 4; ++offset_y) {
        blocks[{x, y + offset_y, z}] = wood;
    }
    for (int offset_z = -1; offset_z <= 1; ++offset_z) {
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
            blocks[{x + offset_x, y + 3, z + offset_z}] = leaves;
            blocks[{x + offset_x, y + 4, z + offset_z}] = leaves;
        }
    }
    blocks[{x, y + 3, z}] = wood;
}

void place_pine(TestBlocks& blocks, int x, int y, int z) {
    const auto wood = to_block_id(BlockType::PineWood);
    const auto leaves = to_block_id(BlockType::PineLeaves);
    for (int offset_y = 0; offset_y < 4; ++offset_y) {
        blocks[{x, y + offset_y, z}] = wood;
    }
    for (int offset_z = -1; offset_z <= 1; ++offset_z) {
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
            blocks[{x + offset_x, y + 3, z + offset_z}] = leaves;
            blocks[{x + offset_x, y + 4, z + offset_z}] = leaves;
        }
    }
    blocks[{x, y + 3, z}] = wood;
}

[[nodiscard]] auto finite_bounds(const VisualVegetationBounds& bounds) -> bool {
    return std::isfinite(bounds.min_x) &&
           std::isfinite(bounds.min_y) &&
           std::isfinite(bounds.min_z) &&
           std::isfinite(bounds.max_x) &&
           std::isfinite(bounds.max_y) &&
           std::isfinite(bounds.max_z);
}

void check_instance(const VisualVegetationInstance& instance) {
    CHECK(std::isfinite(instance.position_x));
    CHECK(std::isfinite(instance.position_y));
    CHECK(std::isfinite(instance.position_z));
    CHECK(std::isfinite(instance.scale_x));
    CHECK(std::isfinite(instance.scale_y));
    CHECK(std::isfinite(instance.scale_z));
    CHECK(std::isfinite(instance.yaw_radians));
    CHECK(std::isfinite(instance.wind_phase));
    CHECK(instance.scale_x > 0.0F);
    CHECK(instance.scale_y > 0.0F);
    CHECK(instance.scale_z > 0.0F);
    CHECK(instance.bounds.valid);
    CHECK(finite_bounds(instance.bounds));
    CHECK(instance.bounds.min_x <= instance.position_x);
    CHECK(instance.bounds.max_x >= instance.position_x);
    CHECK(instance.bounds.min_y <= instance.position_y);
    CHECK(instance.bounds.max_y >= instance.position_y);
    CHECK(instance.bounds.min_z <= instance.position_z);
    CHECK(instance.bounds.max_z >= instance.position_z);
}

} // namespace

TEST_CASE("visual vegetation classifies leafy wood as trees and bare wood as structures") {
    TestBlocks blocks {};
    place_oak(blocks, 2, 3, 2);
    for (int x = 7; x <= 10; ++x) {
        blocks[{x, 3, 4}] = to_block_id(BlockType::PineWood);
    }
    const VisualVegetationSection section {{0, 0, 0}, {15, 15, 15}, 1};

    const auto build = build_visual_vegetation(section, make_sampler(blocks), 42U);

    REQUIRE(build.sources.size() == 2U);
    const auto tree = std::find_if(
        build.sources.begin(),
        build.sources.end(),
        [](const VisualVegetationSource& source) {
            return source.kind == VisualVegetationSourceKind::BroadleafTree;
        });
    const auto structure = std::find_if(
        build.sources.begin(),
        build.sources.end(),
        [](const VisualVegetationSource& source) {
            return source.kind == VisualVegetationSourceKind::PineStructure;
        });
    REQUIRE(tree != build.sources.end());
    REQUIRE(structure != build.sources.end());
    CHECK(tree->source_cell_count == 4U);
    CHECK(tree->foliage_cell_count > 0U);
    CHECK(tree->owns_instances);
    REQUIRE(tree->source_bounds.valid);
    REQUIRE(tree->foliage_bounds.valid);
    CHECK(tree->source_bounds.min_y == doctest::Approx(3.0F));
    CHECK(tree->source_bounds.max_y == doctest::Approx(7.0F));
    CHECK(tree->foliage_bounds.min_x == doctest::Approx(1.0F));
    CHECK(tree->foliage_bounds.max_x == doctest::Approx(4.0F));
    CHECK(tree->foliage_bounds.min_y == doctest::Approx(6.0F));
    CHECK(tree->foliage_bounds.max_y == doctest::Approx(8.0F));
    CHECK(structure->source_cell_count == 4U);
    CHECK(structure->foliage_cell_count == 0U);

    for (const auto& batch : build.lods) {
        CHECK(std::none_of(
            batch.instances.begin(),
            batch.instances.end(),
            [](const VisualVegetationInstance& instance) {
                return instance.source_kind == VisualVegetationSourceKind::WoodStructure ||
                       instance.source_kind == VisualVegetationSourceKind::PineStructure;
            }));
    }
}

TEST_CASE("les recettes d'arbre gardent un tronc fin et une couronne bornee") {
    TestBlocks oak_blocks {};
    place_oak(oak_blocks, 4, 3, 5);
    const auto oak_build = build_visual_vegetation(
        {{0, 0, 0}, {15, 15, 15}, 1},
        make_sampler(oak_blocks),
        0xA8B2C4D6U);
    const auto& oak_medium =
        oak_build.lods[visual_vegetation_lod_index(
            VisualVegetationLod::Medium)];
    const auto oak_trunk = std::find_if(
        oak_medium.instances.begin(),
        oak_medium.instances.end(),
        [](const VisualVegetationInstance& instance) {
            return instance.primitive ==
                   VisualVegetationPrimitive::TaperedTrunk;
        });
    REQUIRE(oak_trunk != oak_medium.instances.end());
    CHECK(oak_trunk->material_block == to_block_id(BlockType::Wood));
    CHECK(oak_trunk->scale_x == doctest::Approx(0.90F));
    CHECK(oak_trunk->scale_z == doctest::Approx(0.90F));
    CHECK(oak_trunk->scale_y == doctest::Approx(4.0F));
    CHECK(oak_trunk->bounds.min_y == doctest::Approx(3.0F));
    CHECK(oak_trunk->bounds.max_y == doctest::Approx(7.0F));

    std::vector<const VisualVegetationInstance*> oak_lobes {};
    for (const auto& instance : oak_medium.instances) {
        if (instance.primitive ==
            VisualVegetationPrimitive::EllipsoidCanopy) {
            oak_lobes.push_back(&instance);
        }
    }
    REQUIRE(oak_lobes.size() == 3U);
    CHECK(std::none_of(
        oak_medium.instances.begin(),
        oak_medium.instances.end(),
        [](const VisualVegetationInstance& instance) {
            return instance.primitive ==
                   VisualVegetationPrimitive::SimplifiedBouquet;
        }));
    for (const auto* lobe : oak_lobes) {
        REQUIRE(lobe != nullptr);
        CHECK(lobe->material_block == to_block_id(BlockType::Leaves));
        CHECK(lobe->scale_x >= 1.80F);
        CHECK(lobe->scale_x <= 2.42F);
        CHECK(lobe->scale_y >= 1.24F);
        CHECK(lobe->scale_y <= 1.74F);
        CHECK(lobe->scale_z >= 1.80F);
        CHECK(lobe->scale_z <= 2.40F);
        CHECK(lobe->bounds.valid);
    }
    CHECK(oak_lobes[0]->seed != oak_lobes[1]->seed);
    CHECK(oak_lobes[1]->seed != oak_lobes[2]->seed);
    const auto bounds_overlap =
        [](const VisualVegetationBounds& lhs,
           const VisualVegetationBounds& rhs) {
            return lhs.valid &&
                   rhs.valid &&
                   lhs.min_x <= rhs.max_x &&
                   lhs.max_x >= rhs.min_x &&
                   lhs.min_y <= rhs.max_y &&
                   lhs.max_y >= rhs.min_y &&
                   lhs.min_z <= rhs.max_z &&
                   lhs.max_z >= rhs.min_z;
        };
    CHECK(bounds_overlap(oak_lobes[0]->bounds, oak_lobes[1]->bounds));
    CHECK(bounds_overlap(oak_lobes[1]->bounds, oak_lobes[2]->bounds));
    CHECK(bounds_overlap(oak_lobes[2]->bounds, oak_lobes[0]->bounds));
    CHECK(std::count_if(
        oak_medium.instances.begin(),
        oak_medium.instances.end(),
        [](const VisualVegetationInstance& instance) {
            return instance.primitive ==
                       VisualVegetationPrimitive::LeafSpray &&
                   instance.material_block ==
                       to_block_id(BlockType::Leaves) &&
                   instance.bounds.valid;
        }) == 2);

    TestBlocks pine_blocks {};
    place_pine(pine_blocks, 8, 3, 8);
    const auto pine_build = build_visual_vegetation(
        {{0, 0, 0}, {15, 15, 15}, 1},
        make_sampler(pine_blocks),
        0x36F17A91U);
    const auto& pine_medium =
        pine_build.lods[visual_vegetation_lod_index(
            VisualVegetationLod::Medium)];
    const auto pine_trunk = std::find_if(
        pine_medium.instances.begin(),
        pine_medium.instances.end(),
        [](const VisualVegetationInstance& instance) {
            return instance.primitive ==
                   VisualVegetationPrimitive::TaperedTrunk;
        });
    REQUIRE(pine_trunk != pine_medium.instances.end());
    CHECK(pine_trunk->material_block == to_block_id(BlockType::PineWood));
    CHECK(pine_trunk->scale_x == doctest::Approx(0.84F));
    CHECK(pine_trunk->scale_z == doctest::Approx(0.84F));
    CHECK(pine_trunk->scale_y == doctest::Approx(4.0F));
    CHECK(std::count_if(
        pine_medium.instances.begin(),
        pine_medium.instances.end(),
        [](const VisualVegetationInstance& instance) {
            return instance.primitive ==
                       VisualVegetationPrimitive::ConicalCanopy &&
                   instance.material_block ==
                       to_block_id(BlockType::PineLeaves);
        }) == 3);
    CHECK(std::count_if(
        pine_medium.instances.begin(),
        pine_medium.instances.end(),
        [](const VisualVegetationInstance& instance) {
            return instance.primitive ==
                       VisualVegetationPrimitive::LeafSpray &&
                   instance.material_block ==
                       to_block_id(BlockType::PineLeaves);
        }) == 2);
}

TEST_CASE("visual vegetation is bit deterministic and derives variations from world coordinates") {
    TestBlocks blocks {};
    place_oak(blocks, -4, 2, -7);
    blocks[{-2, 2, -2}] = to_block_id(BlockType::TallGrass);
    blocks[{-1, 2, -2}] = to_block_id(BlockType::RedFlower);
    blocks[{-5, 2, -3}] = to_block_id(BlockType::YellowFlower);
    blocks[{-8, 2, -4}] = to_block_id(BlockType::Cactus);
    blocks[{-8, 3, -4}] = to_block_id(BlockType::Cactus);
    const VisualVegetationSection section {{-16, 0, -16}, {-1, 15, -1}, 1};

    const auto first = build_visual_vegetation(section, make_sampler(blocks), 0xC0FFEEU);
    const auto second = build_visual_vegetation(section, make_sampler(blocks), 0xC0FFEEU);
    const auto other_seed = build_visual_vegetation(section, make_sampler(blocks), 0xC0FFEFU);

    CHECK(first.sources == second.sources);
    REQUIRE(first.lods.size() == second.lods.size());
    for (std::size_t index = 0U; index < first.lods.size(); ++index) {
        CHECK(first.lods[index].lod == second.lods[index].lod);
        CHECK(first.lods[index].instances == second.lods[index].instances);
        CHECK(first.lods[index].bounds == second.lods[index].bounds);
    }
    CHECK(visual_vegetation_deterministic_hash(first) ==
          visual_vegetation_deterministic_hash(second));
    CHECK(visual_vegetation_deterministic_hash(first) !=
          visual_vegetation_deterministic_hash(other_seed));

    for (const auto& batch : first.lods) {
        for (const auto& instance : batch.instances) {
            check_instance(instance);
        }
    }
}

TEST_CASE("visual vegetation keeps boundary classification stable with an identical halo") {
    TestBlocks blocks {};
    place_oak(blocks, 0, 5, 0);
    const VisualVegetationSection section {{0, 0, 0}, {15, 15, 15}, 1};
    const auto baseline = build_visual_vegetation(section, make_sampler(blocks), 91U);

    // Je change uniquement une cellule situee au-dela du halo contractuel.
    blocks[{-2, 5, 0}] = to_block_id(BlockType::PineLeaves);
    blocks[{17, 5, 1}] = to_block_id(BlockType::Wood);
    const auto outside_changed = build_visual_vegetation(section, make_sampler(blocks), 91U);

    REQUIRE(baseline.sources.size() == 1U);
    REQUIRE(outside_changed.sources.size() == 1U);
    CHECK(baseline.sources == outside_changed.sources);
    CHECK(baseline.sources.front().owns_instances);
    CHECK(baseline.sources.front().kind == VisualVegetationSourceKind::BroadleafTree);
    CHECK(visual_vegetation_deterministic_hash(baseline) ==
          visual_vegetation_deterministic_hash(outside_changed));
}

TEST_CASE("visual vegetation handles negative chunk boundaries without duplicate ownership") {
    TestBlocks blocks {};
    place_oak(blocks, -1, 4, -4);
    const VisualVegetationSection left {{-16, 0, -16}, {-1, 15, -1}, 1};
    const VisualVegetationSection right {{0, 0, -16}, {15, 15, -1}, 1};

    const auto left_build = build_visual_vegetation(left, make_sampler(blocks), 7U);
    const auto right_build = build_visual_vegetation(right, make_sampler(blocks), 7U);

    REQUIRE(left_build.sources.size() == 1U);
    CHECK(left_build.sources.front().owns_instances);
    CHECK(right_build.sources.empty());
    CHECK(left_build.sources.front().anchor == BlockCoord {-1, 4, -4});
    CHECK(!left_build.lods[visual_vegetation_lod_index(VisualVegetationLod::Near)].instances.empty());
    CHECK(right_build.lods[visual_vegetation_lod_index(VisualVegetationLod::Near)].instances.empty());
}

TEST_CASE("visual vegetation LOD batches strictly reduce procedural complexity") {
    TestBlocks blocks {};
    place_oak(blocks, 2, 2, 2);
    blocks[{6, 2, 2}] = to_block_id(BlockType::TallGrass);
    blocks[{7, 2, 2}] = to_block_id(BlockType::RedFlower);
    blocks[{8, 2, 2}] = to_block_id(BlockType::YellowFlower);
    blocks[{10, 2, 2}] = to_block_id(BlockType::Cactus);
    blocks[{10, 3, 2}] = to_block_id(BlockType::Cactus);
    blocks[{10, 4, 2}] = to_block_id(BlockType::Cactus);
    const VisualVegetationSection section {{0, 0, 0}, {15, 15, 15}, 1};

    const auto build = build_visual_vegetation(section, make_sampler(blocks), 123U);
    const auto near_count =
        build.lods[visual_vegetation_lod_index(VisualVegetationLod::Near)].instance_count();
    const auto medium_count =
        build.lods[visual_vegetation_lod_index(VisualVegetationLod::Medium)].instance_count();
    const auto far_count =
        build.lods[visual_vegetation_lod_index(VisualVegetationLod::Far)].instance_count();

    CHECK(near_count > medium_count);
    CHECK(medium_count > far_count);
    CHECK(far_count == 5U);
    for (const auto& batch : build.lods) {
        REQUIRE(batch.bounds.valid);
        CHECK(finite_bounds(batch.bounds));
        for (const auto& instance : batch.instances) {
            check_instance(instance);
        }
    }
}

TEST_CASE("la classification canonique garde un seul proprietaire aux frontieres de chunk et de section") {
    TestBlocks blocks {};
    place_oak(blocks, 15, 13, 8);
    const VisualVegetationSection west {
        {0, kWorldMinY, 0},
        {15, kWorldMaxY, 15},
        8,
    };
    const VisualVegetationSection east {
        {16, kWorldMinY, 0},
        {31, kWorldMaxY, 15},
        8,
    };

    const auto west_build =
        build_visual_vegetation(west, make_sampler(blocks), 0xB0A7DA7U);
    const auto east_build =
        build_visual_vegetation(east, make_sampler(blocks), 0xB0A7DA7U);

    REQUIRE(west_build.sources.size() == 1U);
    CHECK(west_build.sources.front().owns_instances);
    CHECK(west_build.sources.front().anchor ==
          BlockCoord {15, 13, 8});
    CHECK(west_build.sources.front().source_cell_count == 4U);
    CHECK(west_build.sources.front().foliage_cell_count > 0U);
    CHECK(west_build.sources.front().logical_bounds.min_y <= 15.0F);
    CHECK(west_build.sources.front().logical_bounds.max_y >= 17.0F);
    CHECK(east_build.sources.empty());

    const auto& west_medium =
        west_build.lods[
            visual_vegetation_lod_index(
                VisualVegetationLod::Medium)];
    const auto& east_medium =
        east_build.lods[
            visual_vegetation_lod_index(
                VisualVegetationLod::Medium)];
    CHECK_FALSE(west_medium.instances.empty());
    CHECK(east_medium.instances.empty());
    CHECK(std::any_of(
        west_medium.instances.begin(),
        west_medium.instances.end(),
        [](const VisualVegetationInstance& instance) {
            return instance.material_block ==
                   to_block_id(BlockType::Leaves);
        }));
}

TEST_CASE("visual vegetation rejects invalid sections and missing samplers") {
    const VisualVegetationSection inverted {{2, 0, 2}, {1, 4, 4}, 1};
    CHECK_THROWS_AS(
        static_cast<void>(build_visual_vegetation(inverted, [](int, int, int) {
            return to_block_id(BlockType::Air);
        }, 0U)),
        std::invalid_argument);

    const VisualVegetationSection no_halo {{0, 0, 0}, {1, 1, 1}, 0};
    CHECK_THROWS_AS(
        static_cast<void>(build_visual_vegetation(no_halo, [](int, int, int) {
            return to_block_id(BlockType::Air);
        }, 0U)),
        std::invalid_argument);

    const VisualVegetationSection valid {{0, 0, 0}, {1, 1, 1}, 1};
    CHECK_THROWS_AS(
        static_cast<void>(build_visual_vegetation(
            valid,
            VisualVegetationSampler {},
            0U)),
        std::invalid_argument);
}

} // namespace valcraft
