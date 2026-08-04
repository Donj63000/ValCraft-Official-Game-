#include "gameplay/BackroomsThreatArbiter.h"

#include <doctest/doctest.h>

#include <limits>

namespace valcraft {

TEST_CASE("L'arbitre Backrooms donne les egalites exactes a Jack") {
    BackroomsThreatArbiterRuntime runtime {};
    request_backrooms_threat(runtime, BackroomsThreatOwner::Marlow, 42U);
    request_backrooms_threat(runtime, BackroomsThreatOwner::Jack, 42U);

    CHECK(resolve_backrooms_threat(runtime) == BackroomsThreatOwner::Jack);
    CHECK(backrooms_threat_may_manifest(runtime, BackroomsThreatOwner::Jack));
    CHECK_FALSE(backrooms_threat_may_manifest(runtime, BackroomsThreatOwner::Marlow));
    CHECK(runtime.pending_marlow_arrival == 42U);
}

TEST_CASE("L'arbitre conserve une demande refusee pendant la grace") {
    BackroomsThreatArbiterRuntime runtime {};
    request_backrooms_threat(runtime, BackroomsThreatOwner::Jack, 10U);
    request_backrooms_threat(runtime, BackroomsThreatOwner::Marlow, 11U);
    REQUIRE(resolve_backrooms_threat(runtime) == BackroomsThreatOwner::Jack);

    release_backrooms_threat(runtime, BackroomsThreatOwner::Jack);
    CHECK(runtime.grace_seconds >= 10.0F);
    CHECK(runtime.grace_seconds <= 14.0F);
    CHECK(resolve_backrooms_threat(runtime) == BackroomsThreatOwner::None);

    for (auto index = 0; index < 80; ++index) {
        update_backrooms_threat_arbiter(runtime, 0.25F);
    }
    CHECK(runtime.grace_seconds == doctest::Approx(0.0F));
    CHECK(resolve_backrooms_threat(runtime) == BackroomsThreatOwner::Marlow);
}

TEST_CASE("L'arbitre purge la demande fantome du proprietaire sortant") {
    BackroomsThreatArbiterRuntime runtime {};
    request_backrooms_threat(runtime, BackroomsThreatOwner::Jack, 10U);
    request_backrooms_threat(runtime, BackroomsThreatOwner::Marlow, 11U);
    REQUIRE(resolve_backrooms_threat(runtime) == BackroomsThreatOwner::Jack);

    // Je reproduis defensivement l'ancien doublon : Jack redemandait la scene
    // pendant qu'il la possedait deja.
    request_backrooms_threat(runtime, BackroomsThreatOwner::Jack, 12U);
    REQUIRE(runtime.pending_jack_arrival == 12U);
    release_backrooms_threat(runtime, BackroomsThreatOwner::Jack);

    CHECK(runtime.pending_jack_arrival == kBackroomsThreatNoArrival);
    CHECK(runtime.pending_marlow_arrival == 11U);
}

TEST_CASE("L'arbitre assainit le temps et permet d'annuler une intention") {
    BackroomsThreatArbiterRuntime runtime {};
    runtime.grace_seconds = 2.0F;
    update_backrooms_threat_arbiter(
        runtime,
        std::numeric_limits<float>::quiet_NaN());
    CHECK(runtime.grace_seconds == doctest::Approx(2.0F));

    request_backrooms_threat(runtime, BackroomsThreatOwner::Marlow, 3U);
    cancel_backrooms_threat_request(runtime, BackroomsThreatOwner::Marlow);
    runtime.grace_seconds = 0.0F;
    CHECK(resolve_backrooms_threat(runtime) == BackroomsThreatOwner::None);
}

TEST_CASE("L'arbitre valide les deux intentions une seule fois par frame") {
    BackroomsThreatArbiterRuntime runtime {};
    const BackroomsThreatIntent jack {
        .request = true,
    };
    const BackroomsThreatIntent marlow {
        .request = true,
    };

    CHECK(
        commit_backrooms_threat_intents(runtime, jack, marlow, 17U) ==
        BackroomsThreatOwner::Jack);
    CHECK(runtime.pending_jack_arrival == kBackroomsThreatNoArrival);
    CHECK(runtime.pending_marlow_arrival == 17U);

    const BackroomsThreatIntent jack_holds {
        .hold = true,
    };
    const BackroomsThreatIntent marlow_waits {
        .request = true,
        .hold = true,
    };
    CHECK(
        commit_backrooms_threat_intents(
            runtime,
            jack_holds,
            marlow_waits,
            18U) == BackroomsThreatOwner::Jack);
    CHECK(runtime.pending_marlow_arrival == 17U);
}

TEST_CASE("L'arbitre annule un ancien contexte sans creer de grace") {
    BackroomsThreatArbiterRuntime runtime {};
    request_backrooms_threat(runtime, BackroomsThreatOwner::Marlow, 4U);
    REQUIRE(
        resolve_backrooms_threat(runtime) ==
        BackroomsThreatOwner::Marlow);
    request_backrooms_threat(runtime, BackroomsThreatOwner::Jack, 5U);

    reset_backrooms_threat_context(runtime);

    CHECK(runtime.owner == BackroomsThreatOwner::None);
    CHECK(runtime.pending_jack_arrival == kBackroomsThreatNoArrival);
    CHECK(runtime.pending_marlow_arrival == kBackroomsThreatNoArrival);
    CHECK(runtime.grace_seconds == doctest::Approx(0.0F));
}

TEST_CASE("Une annulation domine une nouvelle demande dans la meme intention") {
    BackroomsThreatArbiterRuntime runtime {};
    const BackroomsThreatIntent marlow {
        .request = true,
        .cancel = true,
    };

    CHECK(
        commit_backrooms_threat_intents(
            runtime,
            {},
            marlow,
            9U) == BackroomsThreatOwner::None);
    CHECK(runtime.pending_marlow_arrival == kBackroomsThreatNoArrival);
}

TEST_CASE("Le protocole request acquire hold release conserve la file refusee") {
    BackroomsThreatArbiterRuntime runtime {};
    CHECK(commit_backrooms_threat_intents(
              runtime,
              {},
              {.request = true, .hold = true},
              21U) == BackroomsThreatOwner::Marlow);

    CHECK(commit_backrooms_threat_intents(
              runtime,
              {.request = true, .hold = true},
              {.hold = true},
              22U) == BackroomsThreatOwner::Marlow);
    CHECK(runtime.pending_jack_arrival == 22U);

    CHECK(commit_backrooms_threat_intents(
              runtime,
              {.request = true, .hold = true},
              {.release = true},
              23U) == BackroomsThreatOwner::None);
    CHECK(runtime.pending_jack_arrival == 22U);
    CHECK(runtime.grace_seconds >= 10.0F);

    update_backrooms_threat_arbiter(runtime, 14.0F);
    for (auto index = 0; index < 56; ++index) {
        update_backrooms_threat_arbiter(runtime, 0.25F);
    }
    CHECK(commit_backrooms_threat_intents(
              runtime,
              {.request = true, .hold = true},
              {},
              24U) == BackroomsThreatOwner::Jack);
}

} // namespace valcraft
