#pragma once

#include "world/Environment.h"

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace valcraft {

inline constexpr std::size_t kOceanMaxWaveCount = 6U;

// Les trois grandes composantes portent la flottabilite. Les composantes
// courtes restent visuelles : une coque lourde les moyenne naturellement.
inline constexpr std::size_t kOceanBuoyancyWaveCount = 3U;

enum class OceanSeaState : std::uint8_t {
    Calm = 0,
    Moderate,
    Rough,
    Storm,
    Tempest,
};

struct OceanWave {
    glm::vec2 direction {0.0F, 1.0F};
    float amplitude = 0.0F;
    float wave_number = 0.0F;
    float phase = 0.0F;
    float steepness = 0.0F;
};

struct OceanState {
    std::array<OceanWave, kOceanMaxWaveCount> waves {};

    OceanSeaState sea_state = OceanSeaState::Calm;

    float severity = 0.0F;
    float total_amplitude = 0.0F;
    float maximum_displacement = 0.0F;
    float foam_threshold = 1.0F;
    float detail_strength = 0.0F;
    float detail_phase = 0.0F;
};

struct OceanSample {
    float height = 0.0F;

    // Derivees dH/dX et dH/dZ utilisees pour construire la normale.
    glm::vec2 gradient {0.0F};

    // Valeur 0..1 indiquant si le point est proche d'une crete.
    float crest = 0.0F;
};

class OceanSimulation {
public:
    // Construit un spectre continu depuis les valeurs meteorologiques.
    // Les vagues ne changent pas brutalement pendant une transition.
    [[nodiscard]] static auto evaluate(
        const EnvironmentState& environment) noexcept -> OceanState;

    // Echantillonne la meme equation analytique que le shader.
    // Aucune allocation et aucune lecture GPU ne sont necessaires.
    [[nodiscard]] static auto sample(
        const OceanState& ocean,
        const glm::vec2& world_xz,
        std::size_t wave_count = kOceanMaxWaveCount) noexcept
        -> OceanSample;

    [[nodiscard]] static auto state_label(
        OceanSeaState state) noexcept -> const char*;
};

} // namespace valcraft
