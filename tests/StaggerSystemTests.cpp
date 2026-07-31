#include "gameplay/combat/StaggerSystem.h"

#include <doctest/doctest.h>

#include <limits>

namespace valcraft {

TEST_CASE("stagger triggers once at capacity and owns a bounded vulnerability window") {
    StaggerSystem stagger {};
    REQUIRE(
        stagger.configure({
            120.0F,
            12.0F,
            1.0F,
            1.0F,
        }).configured);

    CHECK_FALSE(stagger.apply(30.0F).triggered);
    CHECK_FALSE(stagger.apply(35.0F).triggered);
    const auto third = stagger.apply(60.0F);
    REQUIRE(third.accepted);
    CHECK(third.triggered);
    CHECK(third.applied_power == doctest::Approx(55.0F));
    CHECK(third.current == doctest::Approx(120.0F));
    CHECK(stagger.state().staggered);

    const auto during_window = stagger.apply(100.0F);
    CHECK(during_window.accepted);
    CHECK(during_window.ignored_while_staggered);
    CHECK_FALSE(during_window.triggered);
    CHECK(during_window.applied_power == doctest::Approx(0.0F));

    const auto almost_finished =
        stagger.update(59.0F / 60.0F);
    REQUIRE(almost_finished.accepted);
    CHECK_FALSE(almost_finished.stagger_ended);
    CHECK(stagger.state().staggered);

    const auto finished =
        stagger.update(kStaggerFixedStepSeconds);
    REQUIRE(finished.accepted);
    CHECK(finished.stagger_ended);
    CHECK_FALSE(stagger.state().staggered);
    CHECK(stagger.state().current == doctest::Approx(0.0F));
}

TEST_CASE("stagger recovery waits after a hit and is invariant to update chunking") {
    const StaggerConfig config {
        100.0F,
        12.0F,
        0.50F,
        1.0F,
    };
    StaggerSystem batched {};
    StaggerSystem stepped {};
    REQUIRE(batched.configure(config).configured);
    REQUIRE(stepped.configure(config).configured);
    REQUIRE(batched.apply(60.0F).accepted);
    REQUIRE(stepped.apply(60.0F).accepted);

    REQUIRE(batched.update(0.50F).accepted);
    CHECK(
        batched.state().current ==
        doctest::Approx(60.0F));
    REQUIRE(batched.update(1.0F).accepted);

    for (int tick = 0; tick < 90; ++tick) {
        REQUIRE(
            stepped.update(
                kStaggerFixedStepSeconds)
                .accepted);
    }

    CHECK(
        batched.state().current ==
        doctest::Approx(48.0F));
    CHECK(
        stepped.state().current ==
        doctest::Approx(
            batched.state().current));
    CHECK(
        stepped.state()
            .recovery_delay_remaining_seconds ==
        doctest::Approx(
            batched.state()
                .recovery_delay_remaining_seconds));
}

TEST_CASE("stagger multiplier is deterministic and a new hit restarts recovery delay") {
    StaggerSystem stagger {};
    REQUIRE(
        stagger.configure({
            100.0F,
            20.0F,
            0.50F,
            1.0F,
        }).configured);

    const auto amplified =
        stagger.apply(20.0F, 1.25F);
    REQUIRE(amplified.accepted);
    CHECK(
        amplified.applied_power ==
        doctest::Approx(25.0F));
    REQUIRE(stagger.update(0.25F).accepted);
    CHECK(
        stagger.state()
            .recovery_delay_remaining_seconds ==
        doctest::Approx(0.25F));

    REQUIRE(stagger.apply(5.0F).accepted);
    CHECK(
        stagger.state()
            .recovery_delay_remaining_seconds ==
        doctest::Approx(0.50F));
    REQUIRE(stagger.update(0.50F).accepted);
    CHECK(
        stagger.state().current ==
        doctest::Approx(30.0F));
    REQUIRE(stagger.update(0.25F).accepted);
    CHECK(
        stagger.state().current ==
        doctest::Approx(25.0F));
}

TEST_CASE("stagger rejects non finite data and preserves a valid configuration") {
    StaggerSystem stagger {};
    const StaggerConfig valid {
        80.0F,
        10.0F,
        1.0F,
        2.0F,
    };
    REQUIRE(stagger.configure(valid).configured);
    REQUIRE(stagger.apply(30.0F).accepted);

    auto invalid = valid;
    invalid.maximum =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(
        stagger.configure(invalid).error ==
        StaggerConfigureError::InvalidMaximum);
    CHECK(
        stagger.state().current ==
        doctest::Approx(30.0F));
    CHECK(
        stagger.state().maximum ==
        doctest::Approx(80.0F));

    CHECK_FALSE(
        stagger.apply(
            std::numeric_limits<float>::infinity())
            .accepted);
    CHECK_FALSE(stagger.apply(-1.0F).accepted);
    CHECK_FALSE(
        stagger.apply(1.0F, -0.1F).accepted);
    CHECK_FALSE(
        stagger.update(
            std::numeric_limits<float>::quiet_NaN())
            .accepted);
    CHECK_FALSE(stagger.update(-0.01F).accepted);
    CHECK_FALSE(
        stagger.update(
            kMaximumStaggerUpdateSeconds + 0.1F)
            .accepted);
    CHECK(
        stagger.state().current ==
        doctest::Approx(30.0F));
}

} // namespace valcraft
