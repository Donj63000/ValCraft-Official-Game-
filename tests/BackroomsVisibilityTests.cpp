#include "render/BackroomsVisibility.h"
#include "world/BackroomsGenerator.h"
#include "world/Environment.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace valcraft {

namespace {

auto complete_chunk_square(
    const ChunkCoord& center,
    int radius) -> std::vector<ChunkCoord> {
    std::vector<ChunkCoord> chunks {};
    const auto side = radius * 2 + 1;
    chunks.reserve(
        static_cast<std::size_t>(
            side * side));
    for (auto dz = -radius;
         dz <= radius;
         ++dz) {
        for (auto dx = -radius;
             dx <= radius;
             ++dx) {
            chunks.push_back({
                center.x + dx,
                center.z + dz,
            });
        }
    }
    return chunks;
}

void erase_chunk(
    std::vector<ChunkCoord>& chunks,
    const ChunkCoord& coord) {
    chunks.erase(
        std::remove(
            chunks.begin(),
            chunks.end(),
            coord),
        chunks.end());
}

} // namespace

TEST_CASE(
    "Backrooms radii reserve one safe chunk without integer overflow") {
    CHECK(kBackroomsStreamingSafetyChunks == 1);
    CHECK(kBackroomsCoverageScanRadius == 6);
    CHECK(
        kBackroomsTerminalFogWidth ==
        doctest::Approx(24.0F));
    CHECK(
        kBackroomsCoverageMargin ==
        doctest::Approx(8.0F));
    CHECK(
        kBackroomsTerminalFogEndCap ==
        doctest::Approx(64.0F));
    CHECK(
        kBackroomsFogExpansionSpeed ==
        doctest::Approx(6.0F));
    CHECK(
        kBackroomsFogMaximumDeltaSeconds ==
        doctest::Approx(0.1F));

    CHECK(backrooms_stream_radius(-100) == 1);
    CHECK(backrooms_stream_radius(-1) == 1);
    CHECK(backrooms_stream_radius(0) == 1);
    CHECK(backrooms_stream_radius(1) == 2);
    CHECK(backrooms_stream_radius(5) == 6);
    CHECK(
        backrooms_stream_radius(
            kMaxStreamRadius -
            kBackroomsStreamingSafetyChunks) ==
        kMaxStreamRadius);
    CHECK(
        backrooms_stream_radius(
            kMaxStreamRadius) ==
        kMaxStreamRadius);
    CHECK(
        backrooms_stream_radius(
            std::numeric_limits<int>::max()) ==
        kMaxStreamRadius);

    CHECK(backrooms_initial_preload_radius(-100) == 0);
    CHECK(backrooms_initial_preload_radius(0) == 0);
    CHECK(backrooms_initial_preload_radius(1) == 1);
    CHECK(backrooms_initial_preload_radius(2) == 2);
    CHECK(backrooms_initial_preload_radius(6) == 6);
    CHECK(
        backrooms_initial_preload_radius(
            kMaxStreamRadius) ==
        kMaxStreamRadius);
    CHECK(
        backrooms_initial_preload_radius(
            std::numeric_limits<int>::max()) ==
        kMaxStreamRadius);

    for (auto configured_radius = 0;
         configured_radius <
         kMaxStreamRadius;
         ++configured_radius) {
        CAPTURE(configured_radius);
        const auto internal_radius =
            backrooms_stream_radius(
                configured_radius);
        CHECK(
            backrooms_initial_preload_radius(
                internal_radius) ==
            internal_radius);
    }
}

TEST_CASE(
    "Backrooms coverage measures only complete contiguous chunk rings") {
    const glm::vec3 camera_position {
        8.0F,
        42.0F,
        8.0F,
    };
    const auto full_radius_two =
        complete_chunk_square({0, 0}, 2);

    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            camera_position,
            full_radius_two,
            0) ==
        doctest::Approx(8.0F));
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            camera_position,
            full_radius_two,
            1) ==
        doctest::Approx(24.0F));
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            camera_position,
            full_radius_two,
            2) ==
        doctest::Approx(40.0F));
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            camera_position,
            full_radius_two,
            std::numeric_limits<int>::max()) ==
        doctest::Approx(40.0F));
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            camera_position,
            full_radius_two,
            -1) ==
        doctest::Approx(8.0F));

    auto missing_outer_chunk = full_radius_two;
    erase_chunk(
        missing_outer_chunk,
        {2, 0});
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            camera_position,
            missing_outer_chunk,
            2) ==
        doctest::Approx(24.0F));

    auto missing_middle_chunk = full_radius_two;
    erase_chunk(
        missing_middle_chunk,
        {1, 0});
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            camera_position,
            missing_middle_chunk,
            2) ==
        doctest::Approx(8.0F));

    auto missing_center_chunk = full_radius_two;
    erase_chunk(
        missing_center_chunk,
        {0, 0});
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            camera_position,
            missing_center_chunk,
            2) ==
        doctest::Approx(0.0F));
}

TEST_CASE(
    "Backrooms coverage keeps floor semantics for negative coordinates") {
    const glm::vec3 camera_position {
        -8.0F,
        42.0F,
        -24.0F,
    };
    const auto negative_chunks =
        complete_chunk_square({-1, -2}, 1);

    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            camera_position,
            negative_chunks,
            1) ==
        doctest::Approx(24.0F));

    const glm::vec3 near_negative_boundary {
        -0.25F,
        42.0F,
        -16.25F,
    };
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            near_negative_boundary,
            negative_chunks,
            1) ==
        doctest::Approx(16.25F));
}

TEST_CASE(
    "Backrooms full preload protects the first crossing in every direction") {
    const auto preloaded_chunks =
        complete_chunk_square(
            {0, 0},
            6);
    constexpr std::array<glm::vec3, 4> crossed_positions {{
        {16.01F, 42.0F, 8.0F},
        {-0.01F, 42.0F, 8.0F},
        {8.0F, 42.0F, 16.01F},
        {8.0F, 42.0F, -0.01F},
    }};

    for (const auto& camera_position :
         crossed_positions) {
        CAPTURE(camera_position.x);
        CAPTURE(camera_position.z);
        const auto coverage =
            backrooms_contiguous_chunk_coverage_distance(
                camera_position,
                preloaded_chunks,
                6);
        CHECK(coverage > 79.9F);
        CHECK(
            backrooms_terminal_fog_range(
                112.0F,
                coverage) ==
            BackroomsTerminalFogRange {
                40.0F,
                64.0F,
            });
    }
}

TEST_CASE(
    "Backrooms coverage clamps scan bounds and rejects invalid cameras") {
    const auto maximum_square =
        complete_chunk_square(
            {0, 0},
            kBackroomsCoverageScanRadius);
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            {8.0F, 42.0F, 8.0F},
            maximum_square,
            std::numeric_limits<int>::max()) ==
        doctest::Approx(104.0F));

    const std::span<const ChunkCoord> no_chunks {};
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            {8.0F, 42.0F, 8.0F},
            no_chunks,
            1) ==
        doctest::Approx(0.0F));
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            {
                std::numeric_limits<float>::quiet_NaN(),
                42.0F,
                8.0F,
            },
            maximum_square,
            1) ==
        doctest::Approx(0.0F));
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            {
                8.0F,
                42.0F,
                std::numeric_limits<float>::infinity(),
            },
            maximum_square,
            1) ==
        doctest::Approx(0.0F));
    CHECK(
        backrooms_contiguous_chunk_coverage_distance(
            {
                std::numeric_limits<float>::max(),
                42.0F,
                std::numeric_limits<float>::max(),
            },
            maximum_square,
            1) ==
        doctest::Approx(0.0F));
}

TEST_CASE(
    "Backrooms terminal fog always seals before incomplete coverage") {
    const auto capped =
        backrooms_terminal_fog_range(
            120.0F,
            104.0F);
    CHECK(
        capped.start_distance ==
        doctest::Approx(40.0F));
    CHECK(
        capped.end_distance ==
        doctest::Approx(64.0F));
    CHECK(capped.enabled());

    const auto coverage_limited =
        backrooms_terminal_fog_range(
            100.0F,
            40.0F);
    CHECK(
        coverage_limited.start_distance ==
        doctest::Approx(8.0F));
    CHECK(
        coverage_limited.end_distance ==
        doctest::Approx(32.0F));
    CHECK(coverage_limited.enabled());

    const auto draw_limited =
        backrooms_terminal_fog_range(
            20.0F,
            100.0F);
    CHECK(
        draw_limited.start_distance ==
        doctest::Approx(0.0F));
    CHECK(
        draw_limited.end_distance ==
        doctest::Approx(20.0F));
    CHECK(draw_limited.enabled());

    const auto no_safe_coverage =
        backrooms_terminal_fog_range(
            100.0F,
            kBackroomsCoverageMargin);
    CHECK(
        no_safe_coverage ==
        BackroomsTerminalFogRange {
            0.0F,
            0.0F,
        });
    CHECK(no_safe_coverage.enabled());

    constexpr std::array<float, 7> draw_distances {{
        -20.0F,
        0.0F,
        12.0F,
        40.0F,
        64.0F,
        120.0F,
        1'000.0F,
    }};
    constexpr std::array<float, 8> coverage_distances {{
        -20.0F,
        0.0F,
        4.0F,
        8.0F,
        20.0F,
        40.0F,
        72.0F,
        1'000.0F,
    }};
    for (const auto draw_distance :
         draw_distances) {
        for (const auto coverage_distance :
             coverage_distances) {
            const auto range =
                backrooms_terminal_fog_range(
                    draw_distance,
                    coverage_distance);
            CAPTURE(draw_distance);
            CAPTURE(coverage_distance);
            CAPTURE(range.start_distance);
            CAPTURE(range.end_distance);
            if (draw_distance <= 0.0F) {
                CHECK(
                    range ==
                    BackroomsTerminalFogRange {});
                CHECK_FALSE(range.enabled());
            } else {
                CHECK(range.enabled());
                CHECK(range.start_distance >= 0.0F);
                CHECK(
                    range.start_distance <=
                    range.end_distance);
                CHECK(
                    range.end_distance <=
                    draw_distance);
                CHECK(
                    range.end_distance <=
                    std::max(
                        coverage_distance -
                            kBackroomsCoverageMargin,
                        0.0F));
                CHECK(
                    range.end_distance <=
                    kBackroomsTerminalFogEndCap);
                CHECK(
                    range.end_distance -
                        range.start_distance <=
                    kBackroomsTerminalFogWidth);
            }
        }
    }

    CHECK(
        backrooms_terminal_fog_range(
            std::numeric_limits<float>::quiet_NaN(),
            100.0F) ==
        BackroomsTerminalFogRange {});
    CHECK(
        backrooms_terminal_fog_range(
            -1.0F,
            100.0F) ==
        BackroomsTerminalFogRange {});
    CHECK(
        backrooms_terminal_fog_range(
            100.0F,
            std::numeric_limits<float>::infinity()) ==
        BackroomsTerminalFogRange {
            0.0F,
            0.0F,
        });
}

TEST_CASE(
    "Backrooms fog closes immediately and only opens progressively") {
    const BackroomsTerminalFogRange full_range {
        40.0F,
        64.0F,
    };
    const BackroomsTerminalFogRange contracted_range {
        24.0F,
        48.0F,
    };

    CHECK(
        backrooms_advance_terminal_fog_range(
            full_range,
            contracted_range,
            1.0F) ==
        contracted_range);
    CHECK(
        backrooms_advance_terminal_fog_range(
            BackroomsTerminalFogRange {},
            full_range,
            0.016F) ==
        full_range);
    CHECK(
        backrooms_advance_terminal_fog_range(
            contracted_range,
            BackroomsTerminalFogRange {},
            0.016F) ==
        BackroomsTerminalFogRange {});

    const auto one_frame_expansion =
        backrooms_advance_terminal_fog_range(
            contracted_range,
            full_range,
            1.0F / 60.0F);
    CHECK(
        one_frame_expansion.end_distance ==
        doctest::Approx(48.1F));
    CHECK(
        one_frame_expansion.start_distance ==
        doctest::Approx(24.1F));
    CHECK(
        one_frame_expansion.end_distance <
        full_range.end_distance);

    const auto hitch_expansion =
        backrooms_advance_terminal_fog_range(
            contracted_range,
            full_range,
            10.0F);
    CHECK(
        hitch_expansion.end_distance ==
        doctest::Approx(48.6F));
    CHECK(
        hitch_expansion.start_distance ==
        doctest::Approx(24.6F));

    const auto invalid_delta =
        backrooms_advance_terminal_fog_range(
            contracted_range,
            full_range,
            std::numeric_limits<float>::quiet_NaN());
    CHECK(invalid_delta == contracted_range);
}

TEST_CASE(
    "Backrooms darkness is absolute without local light or flashlight") {
    CHECK(
        backrooms_darkness_visibility(
            0.0F,
            0.0F,
            true) ==
        0.0F);
    CHECK(
        backrooms_darkness_visibility(
            -1.0F,
            -1.0F,
            true) ==
        0.0F);
    CHECK(
        backrooms_darkness_visibility(
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            true) ==
        0.0F);
    CHECK(
        backrooms_darkness_visibility(
            -std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN(),
            true) ==
        0.0F);
}

TEST_CASE(
    "Backrooms darkness never changes visibility outside an enclosed interior") {
    constexpr std::array<float, 7> samples {{
        -1.0F,
        0.0F,
        0.1F,
        0.5F,
        1.0F,
        2.0F,
        std::numeric_limits<float>::infinity(),
    }};
    for (const auto block_light : samples) {
        for (const auto flashlight_energy : samples) {
            CAPTURE(block_light);
            CAPTURE(flashlight_energy);
            CHECK(
                backrooms_darkness_visibility(
                    block_light,
                    flashlight_energy,
                    false) ==
                1.0F);
        }
    }
    CHECK(
        backrooms_darkness_visibility(
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::quiet_NaN(),
            false) ==
        1.0F);
}

TEST_CASE(
    "Backrooms named darkness thresholds reproduce the smooth visibility curve") {
    CHECK(
        kBackroomsDarknessBlockLightBlackThreshold ==
        0.0F);
    CHECK(
        kBackroomsDarknessFlashlightBlackThreshold ==
        0.0F);
    CHECK(
        kBackroomsDarknessBlockLightFullVisibilityThreshold >
        kBackroomsDarknessBlockLightBlackThreshold);
    CHECK(
        kBackroomsDarknessFlashlightFullVisibilityThreshold >
        kBackroomsDarknessFlashlightBlackThreshold);

    const auto half_block_light =
        (
            kBackroomsDarknessBlockLightBlackThreshold +
            kBackroomsDarknessBlockLightFullVisibilityThreshold
        ) *
        0.5F;
    const auto half_flashlight =
        (
            kBackroomsDarknessFlashlightBlackThreshold +
            kBackroomsDarknessFlashlightFullVisibilityThreshold
        ) *
        0.5F;
    CHECK(
        backrooms_darkness_visibility(
            half_block_light,
            0.0F,
            true) ==
        doctest::Approx(0.5F));
    CHECK(
        backrooms_darkness_visibility(
            0.0F,
            half_flashlight,
            true) ==
        doctest::Approx(0.5F));
    CHECK(
        backrooms_darkness_visibility(
            kBackroomsDarknessBlockLightFullVisibilityThreshold,
            0.0F,
            true) ==
        1.0F);
    CHECK(
        backrooms_darkness_visibility(
            0.0F,
            kBackroomsDarknessFlashlightFullVisibilityThreshold,
            true) ==
        1.0F);
}

TEST_CASE(
    "Backrooms flashlight alone progressively restores visibility in darkness") {
    auto previous_visibility = 0.0F;
    constexpr auto sample_count = 512;
    for (auto index = 0;
         index <= sample_count;
         ++index) {
        const auto flashlight_energy =
            kBackroomsDarknessFlashlightFullVisibilityThreshold *
            static_cast<float>(index) /
            static_cast<float>(sample_count);
        const auto visibility =
            backrooms_darkness_visibility(
                0.0F,
                flashlight_energy,
                true);
        CAPTURE(index);
        CAPTURE(flashlight_energy);
        CHECK(std::isfinite(visibility));
        CHECK(visibility >= 0.0F);
        CHECK(visibility <= 1.0F);
        CHECK(visibility >= previous_visibility);
        if (index > 0) {
            CHECK(visibility > 0.0F);
        }
        previous_visibility = visibility;
    }
    CHECK(previous_visibility == 1.0F);
}

TEST_CASE(
    "Backrooms darkness remains bounded and monotone for both light sources") {
    constexpr auto sample_count = 128;
    auto previous_block_row =
        std::array<float, sample_count + 1> {};
    for (auto block_index = 0;
         block_index <= sample_count;
         ++block_index) {
        auto previous_flash_visibility = 0.0F;
        for (auto flashlight_index = 0;
             flashlight_index <= sample_count;
             ++flashlight_index) {
            const auto block_light =
                static_cast<float>(block_index) /
                static_cast<float>(sample_count);
            const auto flashlight_energy =
                static_cast<float>(flashlight_index) /
                static_cast<float>(sample_count);
            const auto visibility =
                backrooms_darkness_visibility(
                    block_light,
                    flashlight_energy,
                    true);
            CAPTURE(block_index);
            CAPTURE(flashlight_index);
            CHECK(std::isfinite(visibility));
            CHECK(visibility >= 0.0F);
            CHECK(visibility <= 1.0F);
            CHECK(
                visibility >=
                previous_flash_visibility);
            CHECK(
                visibility >=
                previous_block_row[
                    static_cast<std::size_t>(
                        flashlight_index)]);
            previous_flash_visibility = visibility;
            previous_block_row[
                static_cast<std::size_t>(
                    flashlight_index)] = visibility;
        }
    }
}

TEST_CASE(
    "Backrooms darkness sanitizes each invalid source independently") {
    const auto block_only =
        backrooms_darkness_visibility(
            0.31F,
            0.0F,
            true);
    const auto flashlight_only =
        backrooms_darkness_visibility(
            0.0F,
            0.09F,
            true);
    CHECK(block_only > 0.0F);
    CHECK(flashlight_only > 0.0F);

    CHECK(
        backrooms_darkness_visibility(
            0.31F,
            std::numeric_limits<float>::infinity(),
            true) ==
        doctest::Approx(block_only));
    CHECK(
        backrooms_darkness_visibility(
            std::numeric_limits<float>::quiet_NaN(),
            0.09F,
            true) ==
        doctest::Approx(flashlight_only));
    CHECK(
        backrooms_darkness_visibility(
            std::numeric_limits<float>::max(),
            0.0F,
            true) ==
        1.0F);
    CHECK(
        backrooms_darkness_visibility(
            0.0F,
            std::numeric_limits<float>::max(),
            true) ==
        1.0F);
}

TEST_CASE(
    "Backrooms darkness keeps a sanitized interior visibility floor") {
    constexpr auto floor = 0.22F;
    CHECK(
        backrooms_darkness_visibility(
            0.0F,
            0.0F,
            true,
            floor) ==
        doctest::Approx(floor));

    const auto half_block_visibility =
        backrooms_darkness_visibility(
            kBackroomsDarknessBlockLightFullVisibilityThreshold * 0.5F,
            0.0F,
            true,
            floor);
    CHECK(
        half_block_visibility ==
        doctest::Approx(floor + (1.0F - floor) * 0.5F));
    CHECK(
        backrooms_darkness_visibility(
            0.0F,
            kBackroomsDarknessFlashlightFullVisibilityThreshold,
            true,
            floor) ==
        1.0F);

    // Je borne le plancher fourni par l'environnement avant de l'appliquer.
    CHECK(
        backrooms_darkness_visibility(
            0.0F,
            0.0F,
            true,
            -1.0F) ==
        0.0F);
    CHECK(
        backrooms_darkness_visibility(
            0.0F,
            0.0F,
            true,
            2.0F) ==
        1.0F);
    CHECK(
        backrooms_darkness_visibility(
            0.0F,
            0.0F,
            true,
            std::numeric_limits<float>::quiet_NaN()) ==
        0.0F);
    CHECK(
        backrooms_darkness_visibility(
            0.0F,
            0.0F,
            false,
            std::numeric_limits<float>::infinity()) ==
        1.0F);
}

TEST_CASE(
    "Backrooms environment reserves absolute black for Blackout modules") {
    constexpr auto seed = 424242;
    const BackroomsGenerator generator {seed};
    auto normal_module_x = 0;
    auto normal_module_z = 0;
    auto blackout_module_x = 0;
    auto blackout_module_z = 0;
    auto found_normal = false;
    auto found_blackout = false;

    for (auto module_z = -16;
         module_z <= 16 && !(found_normal && found_blackout);
         ++module_z) {
        for (auto module_x = -16;
             module_x <= 16;
             ++module_x) {
            const auto tension =
                generator.module_descriptor(module_x, module_z).tension;
            if (tension == BackroomsTension::Blackout) {
                if (!found_blackout) {
                    blackout_module_x = module_x;
                    blackout_module_z = module_z;
                    found_blackout = true;
                }
            } else if (!found_normal) {
                normal_module_x = module_x;
                normal_module_z = module_z;
                found_normal = true;
            }
        }
    }
    REQUIRE(found_normal);
    REQUIRE(found_blackout);

    const auto state_at_module_center =
        [&](int module_x, int module_z, bool poolrooms) {
            const BackroomsGenerationContext generation_context {
                .seed = seed,
                .theme = poolrooms
                             ? BackroomsTheme::Poolrooms
                             : BackroomsTheme::Offices,
            };
            return make_backrooms_environment_state(
                17.0F,
                generation_context,
                static_cast<float>(
                    module_x * kBackroomsModuleSize +
                    kBackroomsModuleSize / 2),
                static_cast<float>(
                    module_z * kBackroomsModuleSize +
                    kBackroomsModuleSize / 2));
        };

    CHECK(
        state_at_module_center(
            normal_module_x,
            normal_module_z,
            false).interior_visibility_floor ==
        doctest::Approx(kBackroomsOfficeInteriorVisibilityFloor));
    CHECK(
        state_at_module_center(
            normal_module_x,
            normal_module_z,
            true).interior_visibility_floor ==
        doctest::Approx(kPoolroomsInteriorVisibilityFloor));
    CHECK(
        state_at_module_center(
            blackout_module_x,
            blackout_module_z,
            false).interior_visibility_floor ==
        doctest::Approx(kBackroomsBlackoutVisibilityFloor));
    CHECK(
        state_at_module_center(
            blackout_module_x,
            blackout_module_z,
            true).interior_visibility_floor ==
        doctest::Approx(kBackroomsBlackoutVisibilityFloor));
}

TEST_CASE(
    "Backrooms visibility floor blends continuously across Blackout boundaries") {
    constexpr auto seed = 424242;
    const BackroomsGenerator generator {seed};
    auto boundary_module_x = 0;
    auto boundary_module_z = 0;
    auto boundary_along_x = true;
    auto found_boundary = false;

    for (auto module_z = -24;
         module_z <= 24 && !found_boundary;
         ++module_z) {
        for (auto module_x = -24;
             module_x <= 24;
             ++module_x) {
            const auto current_is_blackout =
                generator.module_descriptor(module_x, module_z).tension ==
                BackroomsTension::Blackout;
            const auto right_is_blackout =
                generator.module_descriptor(module_x + 1, module_z).tension ==
                BackroomsTension::Blackout;
            if (current_is_blackout != right_is_blackout) {
                boundary_module_x = module_x;
                boundary_module_z = module_z;
                boundary_along_x = true;
                found_boundary = true;
                break;
            }
            const auto next_is_blackout =
                generator.module_descriptor(module_x, module_z + 1).tension ==
                BackroomsTension::Blackout;
            if (current_is_blackout != next_is_blackout) {
                boundary_module_x = module_x;
                boundary_module_z = module_z;
                boundary_along_x = false;
                found_boundary = true;
                break;
            }
        }
    }
    REQUIRE(found_boundary);

    const auto fixed_x = static_cast<float>(
        boundary_module_x * kBackroomsModuleSize +
        kBackroomsModuleSize / 2);
    const auto fixed_z = static_cast<float>(
        boundary_module_z * kBackroomsModuleSize +
        kBackroomsModuleSize / 2);
    const auto boundary_coordinate = static_cast<float>(
        (boundary_along_x ? boundary_module_x : boundary_module_z) *
            kBackroomsModuleSize +
        kBackroomsModuleSize);
    const auto sample =
        [&](float offset, bool poolrooms) {
            const BackroomsGenerationContext generation_context {
                .seed = seed,
                .theme = poolrooms
                             ? BackroomsTheme::Poolrooms
                             : BackroomsTheme::Offices,
            };
            return make_backrooms_environment_state(
                23.0F,
                generation_context,
                boundary_along_x
                    ? boundary_coordinate + offset
                    : fixed_x,
                boundary_along_x
                    ? fixed_z
                    : boundary_coordinate + offset)
                .interior_visibility_floor;
        };

    for (const auto& [poolrooms, normal_floor] :
         std::array<std::pair<bool, float>, 2> {{
             {false, kBackroomsOfficeInteriorVisibilityFloor},
             {true, kPoolroomsInteriorVisibilityFloor},
         }}) {
        CAPTURE(poolrooms);
        CHECK(
            sample(0.0F, poolrooms) ==
            doctest::Approx(normal_floor * 0.5F)
                .epsilon(0.00001));
        CHECK(
            std::abs(
                sample(-0.001F, poolrooms) -
                sample(0.001F, poolrooms)) <
            0.00001F);

        const auto first_interior = sample(-4.0F, poolrooms);
        const auto second_interior = sample(4.0F, poolrooms);
        const auto first_is_normal =
            first_interior == doctest::Approx(normal_floor);
        const auto first_is_blackout =
            first_interior == doctest::Approx(0.0F);
        const auto second_is_normal =
            second_interior == doctest::Approx(normal_floor);
        const auto second_is_blackout =
            second_interior == doctest::Approx(0.0F);
        const auto endpoints_match_profiles =
            (first_is_normal && second_is_blackout) ||
            (first_is_blackout && second_is_normal);
        CHECK(endpoints_match_profiles);
    }
}

} // namespace valcraft
