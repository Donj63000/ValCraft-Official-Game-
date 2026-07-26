#include "app/SmokeCamera.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

namespace valcraft {
namespace {

TEST_CASE("la camera smoke terrestre cadre le terrain sans accélérer le streaming normal") {
    const auto start =
        make_land_smoke_camera_pose(0.0F, 64, false);
    const auto after_four_seconds =
        make_land_smoke_camera_pose(4.0F, 67, false);

    CHECK(start.position.x == doctest::Approx(0.5F));
    CHECK(start.position.y == doctest::Approx(66.4F));
    CHECK(after_four_seconds.position.x == doctest::Approx(5.5F));
    CHECK(after_four_seconds.position.z == doctest::Approx(2.3F));
    CHECK(after_four_seconds.position.y == doctest::Approx(69.4F));
    CHECK(after_four_seconds.pitch_degrees == doctest::Approx(-12.5F));
    CHECK(after_four_seconds.yaw_degrees > 0.0F);
    CHECK(after_four_seconds.yaw_degrees < 45.0F);
}

TEST_CASE("la camera smoke conserve une trajectoire rapide uniquement pour le stress monde") {
    const auto normal =
        make_land_smoke_camera_pose(2.0F, 70, false);
    const auto stress =
        make_land_smoke_camera_pose(2.0F, 70, true);

    CHECK(stress.position.x > normal.position.x + 10.0F);
    CHECK(stress.position.z > normal.position.z + 4.0F);
    CHECK(stress.position.y == normal.position.y);
}

TEST_CASE("la camera smoke repare les entrées non finies") {
    const auto pose =
        make_land_smoke_camera_pose(
            std::numeric_limits<float>::quiet_NaN(),
            -42,
            false);

    CHECK(std::isfinite(pose.position.x));
    CHECK(std::isfinite(pose.position.y));
    CHECK(std::isfinite(pose.position.z));
    CHECK(pose.position.x == doctest::Approx(0.5F));
    CHECK(pose.position.y == doctest::Approx(2.4F));
}

} // namespace
} // namespace valcraft
