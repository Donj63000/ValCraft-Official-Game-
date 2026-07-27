#include "gameplay/PlayerMusketEffects.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <limits>

using namespace valcraft;

namespace {

[[nodiscard]] auto finite_vector(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

} // namespace

TEST_CASE("les effets du fusil partent du socket et restent deterministes") {
    PlayerMusketEffects first {};
    PlayerMusketEffects second {};
    const glm::vec3 muzzle {2.0F, 3.0F, -4.0F};
    const glm::vec3 forward {0.0F, 0.0F, -1.0F};
    const glm::vec3 inherited {1.0F, 0.0F, 0.5F};
    const glm::vec3 wind {0.25F, 0.0F, -0.15F};

    first.spawn(muzzle, forward, inherited, wind, 42U);
    second.spawn(muzzle, forward, inherited, wind, 42U);

    REQUIRE(first.flashes().size() == 1U);
    REQUIRE(first.smoke().size() == 18U);
    REQUIRE(second.smoke().size() == first.smoke().size());
    CHECK(first.flashes().front().position == muzzle);
    CHECK(first.flashes().front().lifetime == doctest::Approx(0.065F));

    for (std::size_t index = 0U; index < first.smoke().size(); ++index) {
        const auto& left = first.smoke()[index];
        const auto& right = second.smoke()[index];
        CHECK(left.seed == right.seed);
        CHECK(left.position == right.position);
        CHECK(left.velocity == right.velocity);
        CHECK(left.lifetime == right.lifetime);
        CHECK(left.lifetime >= 1.25F);
        CHECK(left.lifetime <= 1.80F);
        CHECK(finite_vector(left.position));
        CHECK(finite_vector(left.velocity));
    }
}

TEST_CASE("le flash expire avant la fumee et toute la poudre disparait") {
    PlayerMusketEffects effects {};
    effects.spawn(
        {0.0F, 1.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {},
        {},
        7U);

    effects.update(0.065F, {});
    CHECK(effects.flashes().empty());
    CHECK_FALSE(effects.smoke().empty());

    for (int step = 0; step < 20; ++step) {
        effects.update(0.10F, {0.4F, 0.0F, 0.0F});
    }
    CHECK(effects.smoke().empty());
}

TEST_CASE("annuler le viewmodel retire le flash sans effacer la fumee du monde") {
    PlayerMusketEffects effects {};
    effects.spawn(
        {0.0F, 1.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {},
        {},
        17U);
    REQUIRE_FALSE(effects.flashes().empty());
    REQUIRE_FALSE(effects.smoke().empty());

    effects.clear_flashes();

    CHECK(effects.flashes().empty());
    CHECK_FALSE(effects.smoke().empty());
}

TEST_CASE("les valeurs non finies ne contaminent jamais les effets") {
    const auto nan =
        std::numeric_limits<float>::quiet_NaN();
    PlayerMusketEffects effects {};
    effects.spawn(
        {nan, 2.0F, nan},
        {nan, nan, nan},
        {nan, nan, nan},
        {nan, nan, nan},
        99U);
    effects.update(
        nan,
        {nan, nan, nan});

    REQUIRE(effects.flashes().size() == 1U);
    CHECK(finite_vector(effects.flashes().front().position));
    CHECK(finite_vector(effects.flashes().front().direction));
    for (const auto& puff : effects.smoke()) {
        CHECK(finite_vector(puff.position));
        CHECK(finite_vector(puff.velocity));
        CHECK(std::isfinite(puff.rotation_radians));
    }
}
