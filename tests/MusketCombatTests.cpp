#include "gameplay/MusketCombat.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>

using namespace valcraft;

namespace {

constexpr float kRange = 50.0F;

} // namespace

TEST_CASE("le resoluteur de mousquet choisit la cible reellement la plus proche") {
    const std::array candidates {
        MusketHit {MusketHitKind::Crew, {0.0F, 0.0F, -8.0F}, 8.0F, 4U},
        MusketHit {MusketHitKind::Creature, {0.0F, 0.0F, -3.0F}, 3.0F, 7U},
    };

    const auto hit =
        select_nearest_musket_hit(
            candidates,
            kRange);

    CHECK(hit.kind == MusketHitKind::Creature);
    CHECK(hit.target_id == 7U);
    CHECK(hit.distance == doctest::Approx(3.0F));
}

TEST_CASE("mur coque et garde arretent chacun une balle") {
    constexpr std::array blockers {
        MusketHitKind::World,
        MusketHitKind::Ship,
        MusketHitKind::OldGuard,
    };

    for (const auto blocker : blockers) {
        CAPTURE(blocker);
        const std::array candidates {
            MusketHit {
                MusketHitKind::Crew,
                {0.0F, 0.0F, -8.0F},
                8.0F,
                4U,
            },
            MusketHit {
                MusketHitKind::Creature,
                {0.0F, 0.0F, -12.0F},
                12.0F,
                7U,
            },
            MusketHit {
                blocker,
                {0.0F, 0.0F, -5.0F},
                5.0F,
                2U,
            },
        };

        const auto hit =
            select_nearest_musket_hit(
                candidates,
                kRange);

        CHECK(hit.kind == blocker);
        CHECK(musket_hit_is_priority_blocker(hit.kind));
        CHECK_FALSE(musket_hit_can_receive_damage(hit.kind));
    }
}

TEST_CASE("un bloqueur dans la tolerance protege une cible legerement devant lui") {
    constexpr std::array blockers {
        MusketHitKind::World,
        MusketHitKind::Ship,
        MusketHitKind::OldGuard,
    };

    for (const auto blocker : blockers) {
        CAPTURE(blocker);
        const std::array candidates {
            MusketHit {
                MusketHitKind::Creature,
                {0.0F, 0.0F, -10.0F},
                10.0F,
                7U,
            },
            MusketHit {
                blocker,
                {0.0F, 0.0F, -10.024F},
                10.024F,
                2U,
            },
        };

        CHECK(
            select_nearest_musket_hit(
                candidates,
                kRange)
                .kind == blocker);
    }
}

TEST_CASE("la surface la plus proche gagne entre plusieurs bloqueurs") {
    const std::array candidates {
        MusketHit {MusketHitKind::Creature, {0.0F, 0.0F, -4.0F}, 4.0F, 7U},
        MusketHit {MusketHitKind::World, {0.0F, 0.0F, -4.01F}, 4.01F, 0U},
        MusketHit {MusketHitKind::OldGuard, {0.0F, 0.0F, -4.005F}, 4.005F, 2U},
    };

    const auto hit =
        select_nearest_musket_hit(
            candidates,
            kRange);

    CHECK(hit.kind == MusketHitKind::OldGuard);
    CHECK(hit.distance == doctest::Approx(4.005F));
}

TEST_CASE("equipage et creature sont atteints uniquement quand ils sont devant") {
    SUBCASE("le marin devant la creature recoit le tir") {
        const std::array candidates {
            MusketHit {MusketHitKind::Creature, {0.0F, 0.0F, -9.0F}, 9.0F, 8U},
            MusketHit {MusketHitKind::Crew, {0.0F, 0.0F, -4.0F}, 4.0F, 2U},
        };

        const auto hit =
            select_nearest_musket_hit(
                candidates,
                kRange);

        CHECK(hit.kind == MusketHitKind::Crew);
        CHECK(hit.target_id == 2U);
        CHECK(musket_hit_can_receive_damage(hit.kind));
    }

    SUBCASE("la creature devant le marin recoit le tir") {
        const std::array candidates {
            MusketHit {MusketHitKind::Crew, {0.0F, 0.0F, -10.024F}, 10.024F, 2U},
            MusketHit {MusketHitKind::Creature, {0.0F, 0.0F, -10.0F}, 10.0F, 8U},
        };

        const auto hit =
            select_nearest_musket_hit(
                candidates,
                kRange);

        CHECK(hit.kind == MusketHitKind::Creature);
        CHECK(hit.target_id == 8U);
        CHECK(musket_hit_can_receive_damage(hit.kind));
    }
}

TEST_CASE("la priorite des bloqueurs cesse exactement a 0,025 metre") {
    const std::array just_inside {
        MusketHit {
            MusketHitKind::Creature,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            8U,
        },
        MusketHit {
            MusketHitKind::World,
            {0.0F, 0.0F, -kMusketHitTieEpsilon},
            kMusketHitTieEpsilon,
            0U,
        },
    };
    const auto inside_distance =
        std::nextafter(
            kMusketHitTieEpsilon,
            0.0F);
    auto inside = just_inside;
    inside[1].position.z = -inside_distance;
    inside[1].distance = inside_distance;

    CHECK(
        select_nearest_musket_hit(
            inside,
            kRange)
            .kind == MusketHitKind::World);
    CHECK(
        select_nearest_musket_hit(
            just_inside,
            kRange)
            .kind == MusketHitKind::Creature);
}

TEST_CASE("une cible a exactement 50 metres reste atteignable") {
    const std::array candidates {
        MusketHit {
            MusketHitKind::Crew,
            {0.0F, 0.0F, -kRange},
            kRange,
            3U,
        },
        MusketHit {
            MusketHitKind::Creature,
            {0.0F, 0.0F, -(kRange + 0.001F)},
            kRange + 0.001F,
            9U,
        },
    };

    const auto hit =
        select_nearest_musket_hit(
            candidates,
            kRange);

    CHECK(hit.kind == MusketHitKind::Crew);
    CHECK(hit.target_id == 3U);
    CHECK(hit.distance == doctest::Approx(kRange));
}

TEST_CASE("le resoluteur rejette les candidats invalides ou hors portee") {
    const auto nan =
        std::numeric_limits<float>::quiet_NaN();
    const std::array candidates {
        MusketHit {MusketHitKind::Creature, {0.0F, 0.0F, -51.0F}, 51.0F, 1U},
        MusketHit {MusketHitKind::Crew, {nan, 0.0F, 0.0F}, 3.0F, 2U},
        MusketHit {MusketHitKind::Ship, {0.0F, 0.0F, -5.0F}, nan, 0U},
    };

    CHECK_FALSE(
        select_nearest_musket_hit(
            candidates,
            kRange)
            .hit());
    CHECK_FALSE(
        select_nearest_musket_hit(
            candidates,
            nan)
            .hit());
    CHECK_FALSE(
        select_nearest_musket_hit(
            std::span<const MusketHit> {},
            kRange)
            .hit());
}

TEST_CASE("le choix final est independant de l'ordre des requetes") {
    constexpr std::array forward {
        MusketHit {MusketHitKind::Creature, {0.0F, 0.0F, -7.0F}, 7.0F, 8U},
        MusketHit {MusketHitKind::World, {0.0F, 0.0F, -7.02F}, 7.02F, 0U},
        MusketHit {MusketHitKind::OldGuard, {0.0F, 0.0F, -7.01F}, 7.01F, 5U},
        MusketHit {MusketHitKind::Crew, {0.0F, 0.0F, -6.0F}, 6.0F, 2U},
    };
    constexpr std::array reverse {
        forward[3],
        forward[2],
        forward[1],
        forward[0],
    };

    const auto first =
        select_nearest_musket_hit(
            forward,
            kRange);
    const auto second =
        select_nearest_musket_hit(
            reverse,
            kRange);

    REQUIRE(first.hit());
    REQUIRE(second.hit());
    CHECK(first.kind == MusketHitKind::Crew);
    CHECK(second.kind == first.kind);
    CHECK(second.target_id == first.target_id);
    CHECK(second.distance == doctest::Approx(first.distance));
}
