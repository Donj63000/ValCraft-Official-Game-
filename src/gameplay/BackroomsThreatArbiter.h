#pragma once

#include <cstdint>
#include <limits>

namespace valcraft {

enum class BackroomsThreatOwner : std::uint8_t {
    None,
    Jack,
    Marlow,
};

constexpr auto kBackroomsThreatNoArrival =
    std::numeric_limits<std::uint64_t>::max();

struct BackroomsThreatArbiterRuntime {
    BackroomsThreatOwner owner = BackroomsThreatOwner::None;
    std::uint64_t pending_jack_arrival = kBackroomsThreatNoArrival;
    std::uint64_t pending_marlow_arrival = kBackroomsThreatNoArrival;
    std::uint32_t random_state = UINT32_C(0x41524254);
    float grace_seconds = 0.0F;
};

// Je laisse les signaux distants hors de cet arbitre : seule une demande de
// manifestation physique doit reserver la scene partagee.
void request_backrooms_threat(
    BackroomsThreatArbiterRuntime& runtime,
    BackroomsThreatOwner threat,
    std::uint64_t arrival_sequence) noexcept;

void cancel_backrooms_threat_request(
    BackroomsThreatArbiterRuntime& runtime,
    BackroomsThreatOwner threat) noexcept;

// Je resous les demandes apres que les deux directeurs ont eu la possibilite
// de publier leur intention. L'egalite exacte revient volontairement a Jack.
[[nodiscard]] auto resolve_backrooms_threat(
    BackroomsThreatArbiterRuntime& runtime) noexcept
    -> BackroomsThreatOwner;

void release_backrooms_threat(
    BackroomsThreatArbiterRuntime& runtime,
    BackroomsThreatOwner threat) noexcept;

void update_backrooms_threat_arbiter(
    BackroomsThreatArbiterRuntime& runtime,
    float dt) noexcept;

[[nodiscard]] auto backrooms_threat_may_manifest(
    const BackroomsThreatArbiterRuntime& runtime,
    BackroomsThreatOwner threat) noexcept -> bool;

} // namespace valcraft
