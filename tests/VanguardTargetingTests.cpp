#include "gameplay/progression/VanguardTargeting.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <array>
#include <cstddef>

namespace valcraft {

namespace {

struct VisibilityFixture {
    AbilityEventEntityId hidden_id = 0U;
    std::array<VanguardTargetCandidate, 8U> candidates {};
};

auto fixture_visibility(
    void* user_data,
    const glm::vec3&,
    const glm::vec3& direction,
    float distance) noexcept -> bool {
    const auto& fixture =
        *static_cast<VisibilityFixture*>(
            user_data);
    for (const auto& candidate :
         fixture.candidates) {
        const auto candidate_distance =
            glm::length(
                candidate.aim_position);
        if (candidate.id != 0U &&
            candidate.id ==
                fixture.hidden_id &&
            candidate_distance ==
                doctest::Approx(distance) &&
            glm::dot(
                glm::normalize(
                    candidate.aim_position),
                direction) >
                0.999F) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE(
    "la frappe avant-garde sélectionne le cône horizontal de 90 degrés") {
    const std::array candidates {
        VanguardTargetCandidate {
            1U,
            {0.0F, 0.0F, 2.0F},
            true,
        },
        VanguardTargetCandidate {
            2U,
            {1.99F, 0.0F, 2.0F},
            true,
        },
        VanguardTargetCandidate {
            3U,
            {2.01F, 0.0F, 2.0F},
            true,
        },
        VanguardTargetCandidate {
            4U,
            {0.0F, 2.0F, 2.0F},
            true,
        },
        VanguardTargetCandidate {
            5U,
            {0.0F, 0.0F, -1.0F},
            true,
        },
    };

    const auto result =
        select_vanguard_targets(
            VanguardTargetingQuery {
            .origin = glm::vec3 {0.0F},
            .forward =
                glm::vec3 {
                    0.0F,
                    0.0F,
                    1.0F,
                },
            .range_meters = 4.0F,
            .half_angle_degrees = 45.0F,
            .candidates = candidates,
        });

    REQUIRE(result.target_count == 3U);
    CHECK(result.targets[0U].id == 1U);
    CHECK(result.targets[1U].id == 2U);
    CHECK(result.targets[2U].id == 4U);
}

TEST_CASE(
    "la frappe avant-garde filtre hostiles portée occlusion et doublons") {
    VisibilityFixture fixture {};
    fixture.hidden_id = 4U;
    fixture.candidates = {{
        {1U, {0.0F, 0.0F, 2.0F}, true},
        {1U, {0.0F, 0.0F, 2.1F}, true},
        {2U, {0.0F, 0.0F, 2.5F}, false},
        {3U, {0.0F, 0.0F, 5.0F}, true},
        {4U, {0.5F, 0.0F, 2.0F}, true},
        {},
        {},
        {},
    }};

    const auto result =
        select_vanguard_targets(
            VanguardTargetingQuery {
            .origin = glm::vec3 {0.0F},
            .forward =
                glm::vec3 {
                    0.0F,
                    0.0F,
                    1.0F,
                },
            .range_meters = 3.0F,
            .half_angle_degrees = 45.0F,
            .candidates = fixture.candidates,
            .visibility_user_data = &fixture,
            .is_visible = fixture_visibility,
        });

    REQUIRE(result.target_count == 1U);
    CHECK(result.targets[0U].id == 1U);
}

TEST_CASE(
    "la sélection avant-garde reste déterministe par distance puis identifiant") {
    const std::array candidates {
        VanguardTargetCandidate {
            9U,
            {-1.0F, 0.0F, 2.0F},
            true,
        },
        VanguardTargetCandidate {
            7U,
            {1.0F, 0.0F, 2.0F},
            true,
        },
        VanguardTargetCandidate {
            5U,
            {0.0F, 0.0F, 1.0F},
            true,
        },
    };

    const auto result =
        select_vanguard_targets(
            VanguardTargetingQuery {
            .origin = glm::vec3 {0.0F},
            .forward =
                glm::vec3 {
                    0.0F,
                    0.0F,
                    1.0F,
                },
            .range_meters = 3.0F,
            .candidates = candidates,
        });

    REQUIRE(result.target_count == 3U);
    CHECK(result.targets[0U].id == 5U);
    CHECK(result.targets[1U].id == 7U);
    CHECK(result.targets[2U].id == 9U);
}

} // namespace valcraft
