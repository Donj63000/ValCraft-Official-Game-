#include "audio/BackroomsAmbience.h"
#include "gameplay/BackroomsFlashlight.h"
#include "gameplay/BackroomsSimulationTime.h"
#include "gameplay/BackroomsThreatArbiter.h"
#include "world/BackroomsGenerator.h"
#include "world/BackroomsSpatialStack.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>

namespace valcraft {

TEST_CASE("Le coeur Backrooms headless conserve une generation deterministe") {
    const BackroomsGenerator first {
        73'331,
        -2,
        kBackroomsSpatialConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    const BackroomsGenerator repeated {
        73'331,
        -2,
        kBackroomsSpatialConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };

    constexpr std::array<BlockCoord, 6U> probes {{
        {-65, kBackroomsFloorY + 1, -65},
        {-1, kBackroomsFloorY + 1, 0},
        {0, kBackroomsFloorY + 1, 0},
        {63, kBackroomsFloorY + 1, 64},
        {64, kBackroomsFloorY + 2, 63},
        {129, kBackroomsFloorY + 4, -127},
    }};
    for (const auto& probe : probes) {
        CHECK(
            first.sample_block(probe.x, probe.y, probe.z) ==
            repeated.sample_block(probe.x, probe.y, probe.z));
        CHECK(
            first.sample_water_state(probe.x, probe.y, probe.z) ==
            repeated.sample_water_state(probe.x, probe.y, probe.z));
    }
}

TEST_CASE("La pile Backrooms headless expose ses cinq niveaux physiques") {
    const BackroomsSpatialStack stack {
        42'424,
        0,
        BackroomsSpatialProfile::FloodedPoolroomsV4,
    };
    CHECK(stack.placements().size() == kBackroomsSpatialLevelCount);
    for (const auto& placement : stack.placements()) {
        CHECK(stack.placement_for_level(placement.logical_level).has_value());
        CHECK(
            stack.logical_level_at_y(
                static_cast<float>(placement.floor_y)) ==
            placement.logical_level);
    }
}

TEST_CASE("L'ambiance Backrooms headless reste finie en mono et stereo") {
    BackroomsAmbience ambience {48'000};
    ambience.set_context({
        .active = true,
        .seed = 0x4241434BU,
        .darkness = 0.72F,
        .anomaly = 0.38F,
    });

    std::array<float, 256U> mono {};
    ambience.mix_interleaved(mono, 1U);
    for (const auto sample : mono) {
        CHECK(std::isfinite(sample));
    }

    std::array<float, 512U> stereo {};
    ambience.mix_interleaved(stereo, 2U);
    for (const auto sample : stereo) {
        CHECK(std::isfinite(sample));
    }
}

TEST_CASE("L'arbitre Backrooms headless reste deterministe") {
    BackroomsThreatArbiterRuntime arbiter {};
    request_backrooms_threat(
        arbiter,
        BackroomsThreatOwner::Marlow,
        17U);
    request_backrooms_threat(
        arbiter,
        BackroomsThreatOwner::Jack,
        17U);
    CHECK(resolve_backrooms_threat(arbiter) == BackroomsThreatOwner::Jack);
}

TEST_CASE("Le temps gameplay Backrooms headless se fige derriere une interface") {
    BackroomsFlashlightState flashlight {
        .battery_charge = 0.75F,
        .enabled = true,
    };
    const auto initial = flashlight;
    const auto frame_time =
        resolve_backrooms_frame_time(1.0F / 60.0F, true);

    static_cast<void>(update_backrooms_flashlight(
        flashlight,
        frame_time.simulation_delta_seconds));

    CHECK(frame_time.real_delta_seconds > 0.0F);
    CHECK(frame_time.simulation_delta_seconds == doctest::Approx(0.0F));
    CHECK(flashlight == initial);
    CHECK_FALSE(backrooms_ui_freezes_simulation(true, true));
}

TEST_CASE("La reprise Backrooms headless absorbe la touche de validation") {
    auto state = initialize_backrooms_resume_state(false);

    const auto resumed =
        advance_backrooms_resume_state(state, false, true);
    CHECK(resumed.synchronize_latches);
    CHECK(resumed.suppress_jump);

    CHECK(advance_backrooms_resume_state(state, false, true).suppress_jump);
    CHECK_FALSE(
        advance_backrooms_resume_state(state, false, false).suppress_jump);
    CHECK_FALSE(
        advance_backrooms_resume_state(state, false, true).suppress_jump);

    BackroomsResumeState same_event_batch {};
    note_backrooms_interaction_boundary(
        same_event_batch,
        true,
        false);
    CHECK(advance_backrooms_resume_state(
              same_event_batch,
              false,
              true)
              .suppress_jump);
}

} // namespace valcraft
