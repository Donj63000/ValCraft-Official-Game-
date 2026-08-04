#include "gameplay/BackroomsThreatArbiter.h"

#include <algorithm>
#include <cmath>

namespace valcraft {
namespace {

auto next_random(std::uint32_t& state) noexcept -> std::uint32_t {
    if (state == 0U) {
        state = UINT32_C(0x41524254);
    }
    auto value = state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    state = value == 0U ? UINT32_C(0x9E3779B9) : value;
    return state;
}

auto pending_arrival(
    BackroomsThreatArbiterRuntime& runtime,
    BackroomsThreatOwner threat) noexcept -> std::uint64_t& {
    return threat == BackroomsThreatOwner::Jack
               ? runtime.pending_jack_arrival
               : runtime.pending_marlow_arrival;
}

void apply_intent_before_requests(
    BackroomsThreatArbiterRuntime& runtime,
    BackroomsThreatOwner owner,
    const BackroomsThreatIntent& intent) noexcept {
    if (intent.cancel || intent.release) {
        cancel_backrooms_threat_request(runtime, owner);
    }
    if (runtime.owner == owner &&
        (intent.release || !intent.hold)) {
        release_backrooms_threat(runtime, owner);
    }
}

void publish_intent_request(
    BackroomsThreatArbiterRuntime& runtime,
    BackroomsThreatOwner owner,
    const BackroomsThreatIntent& intent,
    std::uint64_t arrival_sequence) noexcept {
    if (!intent.request || intent.cancel || intent.release ||
        runtime.owner == owner) {
        return;
    }
    request_backrooms_threat(runtime, owner, arrival_sequence);
}

} // namespace

void request_backrooms_threat(
    BackroomsThreatArbiterRuntime& runtime,
    BackroomsThreatOwner threat,
    std::uint64_t arrival_sequence) noexcept {
    if (threat == BackroomsThreatOwner::None) {
        return;
    }
    // Je garde la premiere intention tant qu'elle n'est pas annulee : un
    // monstre refuse ne perd donc jamais artificiellement sa fenetre.
    auto& pending = pending_arrival(runtime, threat);
    pending = std::min(pending, arrival_sequence);
}

void cancel_backrooms_threat_request(
    BackroomsThreatArbiterRuntime& runtime,
    BackroomsThreatOwner threat) noexcept {
    if (threat == BackroomsThreatOwner::None) {
        return;
    }
    pending_arrival(runtime, threat) = kBackroomsThreatNoArrival;
}

auto resolve_backrooms_threat(
    BackroomsThreatArbiterRuntime& runtime) noexcept
    -> BackroomsThreatOwner {
    if (runtime.owner != BackroomsThreatOwner::None ||
        runtime.grace_seconds > 0.0F) {
        return runtime.owner;
    }
    const auto jack = runtime.pending_jack_arrival;
    const auto marlow = runtime.pending_marlow_arrival;
    if (jack == kBackroomsThreatNoArrival &&
        marlow == kBackroomsThreatNoArrival) {
        return BackroomsThreatOwner::None;
    }
    runtime.owner = jack <= marlow
                        ? BackroomsThreatOwner::Jack
                        : BackroomsThreatOwner::Marlow;
    cancel_backrooms_threat_request(runtime, runtime.owner);
    return runtime.owner;
}

void release_backrooms_threat(
    BackroomsThreatArbiterRuntime& runtime,
    BackroomsThreatOwner threat) noexcept {
    if (threat == BackroomsThreatOwner::None ||
        runtime.owner != threat) {
        return;
    }
    // Je purge toute intention residuelle du proprietaire sortant. Seule la
    // demande refusee de l'autre menace doit survivre a la respiration.
    cancel_backrooms_threat_request(runtime, threat);
    runtime.owner = BackroomsThreatOwner::None;
    // Je tire un repos deterministe dans [10, 14] s sans toucher aux demandes
    // refusees, qui seront reprises automatiquement apres cette respiration.
    const auto random = next_random(runtime.random_state);
    runtime.grace_seconds = 10.0F +
        static_cast<float>(random & UINT32_C(0xFFFF)) /
            65535.0F * 4.0F;
}

auto commit_backrooms_threat_intents(
    BackroomsThreatArbiterRuntime& runtime,
    const BackroomsThreatIntent& jack,
    const BackroomsThreatIntent& marlow,
    std::uint64_t arrival_sequence) noexcept
    -> BackroomsThreatOwner {
    apply_intent_before_requests(
        runtime,
        BackroomsThreatOwner::Jack,
        jack);
    apply_intent_before_requests(
        runtime,
        BackroomsThreatOwner::Marlow,
        marlow);

    publish_intent_request(
        runtime,
        BackroomsThreatOwner::Jack,
        jack,
        arrival_sequence);
    publish_intent_request(
        runtime,
        BackroomsThreatOwner::Marlow,
        marlow,
        arrival_sequence);
    return resolve_backrooms_threat(runtime);
}

void reset_backrooms_threat_context(
    BackroomsThreatArbiterRuntime& runtime) noexcept {
    runtime.owner = BackroomsThreatOwner::None;
    runtime.pending_jack_arrival = kBackroomsThreatNoArrival;
    runtime.pending_marlow_arrival = kBackroomsThreatNoArrival;
    runtime.grace_seconds = 0.0F;
    if (runtime.random_state == 0U) {
        runtime.random_state = UINT32_C(0x41524254);
    }
}

void update_backrooms_threat_arbiter(
    BackroomsThreatArbiterRuntime& runtime,
    float dt) noexcept {
    const auto safe_dt = std::isfinite(dt)
                             ? std::clamp(dt, 0.0F, 0.25F)
                             : 0.0F;
    runtime.grace_seconds = std::max(
        std::isfinite(runtime.grace_seconds)
            ? runtime.grace_seconds - safe_dt
            : 0.0F,
        0.0F);
}

auto backrooms_threat_may_manifest(
    const BackroomsThreatArbiterRuntime& runtime,
    BackroomsThreatOwner threat) noexcept -> bool {
    return threat != BackroomsThreatOwner::None &&
           runtime.owner == threat;
}

} // namespace valcraft
