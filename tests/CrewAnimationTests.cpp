#include "creatures/CrewAnimation.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace valcraft {

namespace {

constexpr float kExpectedThighLength = 0.37F;
constexpr float kExpectedShinLength = 0.36F;
constexpr float kMaximumLegReach =
    kExpectedThighLength + kExpectedShinLength;
constexpr float kComparisonTolerance = 1.0e-5F;
static_assert(kCrewLocomotionCycleDistance == 0.96F);

[[nodiscard]] auto finite_vector(const glm::vec2& value) noexcept -> bool {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] auto finite_leg(const CrewLegPose& leg) noexcept -> bool {
    return finite_vector(leg.hip) &&
           finite_vector(leg.knee) &&
           finite_vector(leg.ankle) &&
           finite_vector(leg.foot_center) &&
           finite_vector(leg.foot_pivot) &&
           std::isfinite(leg.foot_pitch_radians) &&
           std::isfinite(leg.sole_height);
}

[[nodiscard]] auto finite_pose(const CrewLocomotionPose& pose) noexcept -> bool {
    return finite_leg(pose.legs[0]) &&
           finite_leg(pose.legs[1]) &&
           std::isfinite(pose.pelvis_offset_y) &&
           std::isfinite(pose.torso_sway_radians) &&
           std::isfinite(pose.head_stabilization_radians) &&
           std::isfinite(pose.arm_swing);
}

void check_vector_near(const glm::vec2& lhs,
                       const glm::vec2& rhs,
                       float tolerance = kComparisonTolerance) {
    CHECK(std::abs(lhs.x - rhs.x) <= tolerance);
    CHECK(std::abs(lhs.y - rhs.y) <= tolerance);
}

void check_leg_near(const CrewLegPose& lhs,
                    const CrewLegPose& rhs,
                    float tolerance = kComparisonTolerance) {
    check_vector_near(lhs.hip, rhs.hip, tolerance);
    check_vector_near(lhs.knee, rhs.knee, tolerance);
    check_vector_near(lhs.ankle, rhs.ankle, tolerance);
    check_vector_near(lhs.foot_center, rhs.foot_center, tolerance);
    check_vector_near(lhs.foot_pivot, rhs.foot_pivot, tolerance);
    CHECK(std::abs(lhs.foot_pitch_radians - rhs.foot_pitch_radians) <=
          tolerance);
    CHECK(std::abs(lhs.sole_height - rhs.sole_height) <= tolerance);
    CHECK(lhs.supporting == rhs.supporting);
}

void check_pose_near(const CrewLocomotionPose& lhs,
                     const CrewLocomotionPose& rhs,
                     float tolerance = kComparisonTolerance) {
    check_leg_near(lhs.legs[0], rhs.legs[0], tolerance);
    check_leg_near(lhs.legs[1], rhs.legs[1], tolerance);
    CHECK(std::abs(lhs.pelvis_offset_y - rhs.pelvis_offset_y) <= tolerance);
    CHECK(std::abs(lhs.torso_sway_radians - rhs.torso_sway_radians) <=
          tolerance);
    CHECK(std::abs(lhs.head_stabilization_radians -
                   rhs.head_stabilization_radians) <= tolerance);
    CHECK(std::abs(lhs.arm_swing - rhs.arm_swing) <= tolerance);
}

void check_pose_exact(const CrewLocomotionPose& lhs,
                      const CrewLocomotionPose& rhs) {
    for (std::size_t index = 0; index < lhs.legs.size(); ++index) {
        const auto& left = lhs.legs[index];
        const auto& right = rhs.legs[index];
        CHECK(left.hip.x == right.hip.x);
        CHECK(left.hip.y == right.hip.y);
        CHECK(left.knee.x == right.knee.x);
        CHECK(left.knee.y == right.knee.y);
        CHECK(left.ankle.x == right.ankle.x);
        CHECK(left.ankle.y == right.ankle.y);
        CHECK(left.foot_center.x == right.foot_center.x);
        CHECK(left.foot_center.y == right.foot_center.y);
        CHECK(left.foot_pivot.x == right.foot_pivot.x);
        CHECK(left.foot_pivot.y == right.foot_pivot.y);
        CHECK(left.foot_pitch_radians == right.foot_pitch_radians);
        CHECK(left.sole_height == right.sole_height);
        CHECK(left.supporting == right.supporting);
    }
    CHECK(lhs.pelvis_offset_y == rhs.pelvis_offset_y);
    CHECK(lhs.torso_sway_radians == rhs.torso_sway_radians);
    CHECK(lhs.head_stabilization_radians ==
          rhs.head_stabilization_radians);
    CHECK(lhs.arm_swing == rhs.arm_swing);
}

} // namespace

TEST_CASE("la demarche maritime respecte ses invariants sur trente-deux phases") {
    struct StyleExpectation {
        CrewGaitStyle style;
        float minimum_clearance;
        float maximum_clearance;
    };
    constexpr std::array<StyleExpectation, 2> kExpectations {{
        {CrewGaitStyle::Walk, 0.10F, 0.135F},
        {CrewGaitStyle::Carry, 0.08F, 0.105F},
    }};

    for (const auto& expectation : kExpectations) {
        std::array<float, 2> maximum_clearance {{0.0F, 0.0F}};
        for (std::size_t sample_index = 0; sample_index < 32U;
             ++sample_index) {
            const auto phase =
                static_cast<float>(sample_index) / 32.0F;
            const auto pose = sample_crew_locomotion(
                phase,
                1.0F,
                expectation.style);
            CAPTURE(static_cast<int>(expectation.style));
            CAPTURE(sample_index);

            CHECK(finite_pose(pose));
            CHECK(std::abs(pose.pelvis_offset_y) <= 0.024F + 1.0e-5F);
            CHECK(std::abs(pose.torso_sway_radians) <= 0.03F + 1.0e-5F);

            // Je controle separement le contact logique et la proximite reelle
            // du pont pour detecter une semelle marquee en appui mais suspendue.
            CHECK((pose.legs[0].supporting || pose.legs[1].supporting));
            CHECK((pose.legs[0].sole_height <= 0.025F ||
                   pose.legs[1].sole_height <= 0.025F));

            for (std::size_t leg_index = 0; leg_index < pose.legs.size();
                 ++leg_index) {
                const auto& leg = pose.legs[leg_index];
                CHECK(leg.sole_height >= -0.01F);
                if (leg.supporting) {
                    CHECK(leg.sole_height <= 0.025F);
                }

                const auto thigh_length = glm::length(leg.knee - leg.hip);
                const auto shin_length = glm::length(leg.ankle - leg.knee);
                CHECK(std::abs(thigh_length - kExpectedThighLength) <=
                      kExpectedThighLength * 0.01F);
                CHECK(std::abs(shin_length - kExpectedShinLength) <=
                      kExpectedShinLength * 0.01F);

                // Je borne aussi directement la portee de l'IK : une cheville
                // au-dela de cette distance imposerait une hyperextension.
                CHECK(glm::length(leg.ankle - leg.hip) <=
                      kMaximumLegReach + 1.0e-4F);
                const auto hip_to_ankle = leg.ankle - leg.hip;
                const auto hip_to_knee = leg.knee - leg.hip;
                const auto knee_orientation =
                    hip_to_ankle.x * hip_to_knee.y -
                    hip_to_ankle.y * hip_to_knee.x;
                CHECK(knee_orientation >= -1.0e-5F);
                maximum_clearance[leg_index] =
                    std::max(maximum_clearance[leg_index], leg.sole_height);
            }
        }

        CHECK(maximum_clearance[0] >= expectation.minimum_clearance);
        CHECK(maximum_clearance[1] >= expectation.minimum_clearance);
        CHECK(std::abs(
                  maximum_clearance[0] -
                  expectation.maximum_clearance) <= 1.0e-4F);
        CHECK(std::abs(
                  maximum_clearance[1] -
                  expectation.maximum_clearance) <= 1.0e-4F);
    }
}

TEST_CASE("les mouvements secondaires gardent leurs amplitudes et leur contrephase") {
    const auto walk_quarter =
        sample_crew_locomotion(0.25F, 1.0F, CrewGaitStyle::Walk);
    const auto carry_quarter =
        sample_crew_locomotion(0.25F, 1.0F, CrewGaitStyle::Carry);
    CHECK(std::abs(walk_quarter.pelvis_offset_y - 0.024F) <= 1.0e-5F);
    CHECK(std::abs(carry_quarter.pelvis_offset_y - 0.014F) <= 1.0e-5F);
    CHECK(std::abs(walk_quarter.torso_sway_radians - 0.030F) <= 1.0e-5F);
    CHECK(std::abs(carry_quarter.torso_sway_radians - 0.015F) <= 1.0e-5F);
    CHECK(std::abs(
              walk_quarter.head_stabilization_radians +
              0.65F * walk_quarter.torso_sway_radians) <= 1.0e-5F);
    CHECK(std::abs(
              carry_quarter.head_stabilization_radians +
              0.65F * carry_quarter.torso_sway_radians) <= 1.0e-5F);

    const auto walk_start =
        sample_crew_locomotion(0.0F, 1.0F, CrewGaitStyle::Walk);
    const auto walk_opposite =
        sample_crew_locomotion(0.5F, 1.0F, CrewGaitStyle::Walk);
    const auto carry_start =
        sample_crew_locomotion(0.0F, 1.0F, CrewGaitStyle::Carry);
    const auto stopped =
        sample_crew_locomotion(0.0F, 0.0F, CrewGaitStyle::Walk);
    CHECK(std::abs(walk_start.arm_swing + 0.18F) <= 1.0e-5F);
    CHECK(std::abs(walk_opposite.arm_swing - 0.18F) <= 1.0e-5F);
    CHECK(std::abs(carry_start.arm_swing) <= 1.0e-6F);
    CHECK(std::abs(stopped.arm_swing) <= 1.0e-6F);
}

TEST_CASE("les bottes pivotent reellement sur le talon puis la pointe") {
    const auto heel_strike =
        sample_crew_locomotion(0.0F, 1.0F, CrewGaitStyle::Walk).legs[0];
    const auto flat_support =
        sample_crew_locomotion(0.20F, 1.0F, CrewGaitStyle::Walk).legs[0];
    const auto toe_push =
        sample_crew_locomotion(0.45F, 1.0F, CrewGaitStyle::Walk).legs[0];

    CHECK(heel_strike.foot_pitch_radians > 0.0F);
    CHECK(std::abs(heel_strike.foot_pivot.x + kCrewFootHalfLength) <= 1.0e-6F);
    CHECK(std::abs(flat_support.foot_pitch_radians) <= 1.0e-6F);
    CHECK(std::abs(flat_support.foot_pivot.x) <= 1.0e-6F);
    CHECK(toe_push.foot_pitch_radians < 0.0F);
    CHECK(std::abs(toe_push.foot_pivot.x - kCrewFootHalfLength) <= 1.0e-6F);

    for (const auto& leg : {heel_strike, flat_support, toe_push}) {
        // Le point de pivot est le point de semelle conserve par la matrice de
        // chaussure : son altitude doit rester exactement celle de l'appui.
        CHECK(std::abs(
                  leg.foot_center.y +
                  leg.foot_pivot.y -
                  leg.sole_height) <= 1.0e-6F);
    }
}

TEST_CASE("l'IK conserve ses longueurs lorsque le marin s'accroupit") {
    constexpr std::array<float, 4> kLowerings {{
        0.035F,
        0.080F,
        0.135F,
        0.180F,
    }};
    for (const auto lowering : kLowerings) {
        for (std::size_t sample_index = 0; sample_index < 32U; ++sample_index) {
            const auto phase = static_cast<float>(sample_index) / 32.0F;
            const auto pose = sample_crew_locomotion(
                phase,
                0.0F,
                CrewGaitStyle::Walk,
                lowering);
            CAPTURE(lowering);
            CAPTURE(sample_index);
            for (const auto& leg : pose.legs) {
                CHECK(std::abs(
                          glm::length(leg.knee - leg.hip) -
                          kExpectedThighLength) <=
                      kExpectedThighLength * 0.01F);
                CHECK(std::abs(
                          glm::length(leg.ankle - leg.knee) -
                          kExpectedShinLength) <=
                      kExpectedShinLength * 0.01F);
                CHECK(std::abs(
                          leg.hip.y -
                          (0.785F - lowering)) <= 1.0e-5F);
            }
        }
    }
}

TEST_CASE("les jambes sont symetriques au demi-cycle et continues au bouclage") {
    constexpr std::array<CrewGaitStyle, 2> kStyles {{
        CrewGaitStyle::Walk,
        CrewGaitStyle::Carry,
    }};

    for (const auto style : kStyles) {
        CAPTURE(static_cast<int>(style));
        for (std::size_t sample_index = 0; sample_index < 32U;
             ++sample_index) {
            const auto phase =
                static_cast<float>(sample_index) / 32.0F;
            const auto opposite_phase =
                std::fmod(phase + 0.5F, 1.0F);
            const auto pose = sample_crew_locomotion(phase, 1.0F, style);
            const auto opposite =
                sample_crew_locomotion(opposite_phase, 1.0F, style);
            CAPTURE(sample_index);

            check_leg_near(pose.legs[0], opposite.legs[1]);
            check_leg_near(pose.legs[1], opposite.legs[0]);
        }

        const auto cycle_start = sample_crew_locomotion(0.0F, 1.0F, style);
        const auto cycle_end = sample_crew_locomotion(1.0F, 1.0F, style);
        check_pose_near(cycle_start, cycle_end);
    }
}

TEST_CASE("un arret repose les deux pieds et les entrees invalides restent sures") {
    constexpr std::array<CrewGaitStyle, 2> kStyles {{
        CrewGaitStyle::Walk,
        CrewGaitStyle::Carry,
    }};

    for (const auto style : kStyles) {
        CAPTURE(static_cast<int>(style));
        for (std::size_t sample_index = 0; sample_index < 32U;
             ++sample_index) {
            const auto phase =
                static_cast<float>(sample_index) / 32.0F;
            const auto pose = sample_crew_locomotion(phase, 0.0F, style);
            CAPTURE(sample_index);

            REQUIRE(finite_pose(pose));
            for (const auto& leg : pose.legs) {
                CHECK(leg.supporting);
                CHECK(leg.sole_height >= -0.01F);
                CHECK(leg.sole_height <= 0.025F);
                CHECK(std::abs(leg.foot_pitch_radians) <= 1.0e-4F);
            }
            CHECK(std::abs(pose.pelvis_offset_y) <= 1.0e-4F);
        }
    }

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();
    const std::array<CrewLocomotionPose, 8> invalid_samples {{
        sample_crew_locomotion(nan, 1.0F, CrewGaitStyle::Walk),
        sample_crew_locomotion(infinity, 1.0F, CrewGaitStyle::Walk),
        sample_crew_locomotion(-infinity, 1.0F, CrewGaitStyle::Carry),
        sample_crew_locomotion(0.25F, nan, CrewGaitStyle::Walk),
        sample_crew_locomotion(0.25F, infinity, CrewGaitStyle::Carry),
        sample_crew_locomotion(0.25F, -infinity, CrewGaitStyle::Carry),
        sample_crew_locomotion(0.25F, 1.0F, CrewGaitStyle::Walk, nan),
        sample_crew_locomotion(0.25F, 1.0F, CrewGaitStyle::Carry, infinity),
    }};
    for (const auto& pose : invalid_samples) {
        CHECK(finite_pose(pose));
        CHECK(pose.legs[0].sole_height >= -0.01F);
        CHECK(pose.legs[1].sole_height >= -0.01F);
    }
}

TEST_CASE("l'echantillonneur de demarche est deterministe") {
    constexpr std::array<CrewGaitStyle, 2> kStyles {{
        CrewGaitStyle::Walk,
        CrewGaitStyle::Carry,
    }};
    constexpr std::array<float, 5> kPhases {{
        -0.25F,
        0.0F,
        0.1875F,
        0.75F,
        1.25F,
    }};

    for (const auto style : kStyles) {
        for (const auto phase : kPhases) {
            CAPTURE(static_cast<int>(style));
            CAPTURE(phase);
            const auto first =
                sample_crew_locomotion(phase, 0.73F, style);
            const auto second =
                sample_crew_locomotion(phase, 0.73F, style);
            check_pose_exact(first, second);
        }
    }
}

TEST_CASE("le pied d'appui reste verrouille pendant l'avance de la racine") {
    constexpr std::array<CrewGaitStyle, 2> kStyles {{
        CrewGaitStyle::Walk,
        CrewGaitStyle::Carry,
    }};
    constexpr std::array<std::array<float, 2>, 6> kSupportWindows {{
        {{0.01F, 0.04F}},
        {{0.10F, 0.15F}},
        {{0.40F, 0.45F}},
        {{0.51F, 0.54F}},
        {{0.60F, 0.65F}},
        {{0.90F, 0.95F}},
    }};
    constexpr std::array<float, 3> kMotionAmounts {{0.20F, 0.50F, 1.0F}};

    for (const auto style : kStyles) {
        for (const auto& window : kSupportWindows) {
            for (const auto motion : kMotionAmounts) {
                const auto first =
                    sample_crew_locomotion(window[0], motion, style);
                const auto second =
                    sample_crew_locomotion(window[1], motion, style);
                CAPTURE(static_cast<int>(style));
                CAPTURE(window[0]);
                CAPTURE(window[1]);
                CAPTURE(motion);

                std::size_t supporting_leg = first.legs.size();
                for (std::size_t index = 0; index < first.legs.size(); ++index) {
                    if (first.legs[index].supporting &&
                        second.legs[index].supporting) {
                        supporting_leg = index;
                        break;
                    }
                }
                REQUIRE(supporting_leg < first.legs.size());

                // Je reconstruis ici la position monde analytique du pied :
                // la racine avance d'une fraction exacte du cycle spatial,
                // y compris pendant les fondus de depart et d'arret.
                const auto root_advance =
                    (window[1] - window[0]) *
                    kCrewLocomotionCycleDistance;
                const auto first_world_x =
                    first.legs[supporting_leg].foot_center.x +
                    first.legs[supporting_leg].foot_pivot.x;
                const auto second_world_x =
                    root_advance +
                    second.legs[supporting_leg].foot_center.x +
                    second.legs[supporting_leg].foot_pivot.x;
                CHECK(std::abs(second_world_x - first_world_x) <= 0.001F);
            }
        }
    }
}

} // namespace valcraft
