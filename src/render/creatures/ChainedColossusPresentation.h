#pragma once

#include "creatures/CreatureGeometry.h"
#include "creatures/bosses/ChainedColossusState.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace valcraft {

inline constexpr std::size_t kColossusMaximumBloodTraces = 28U;
inline constexpr float kColossusVisualHeightBlocks = 4.75F;

enum class ColossusVisualPart : std::uint8_t {
    Torso = 0,
    Head,
    LeftArm,
    RightArm,
    LeftLeg,
    RightLeg,
    Horn,
    Armor,
    Chain,
    Wound,
};

enum class ColossusHiddenPart : std::uint32_t {
    None = 0U,
    Head = 1U << 0U,
    LeftArm = 1U << 1U,
    RightArm = 1U << 2U,
    LeftLeg = 1U << 3U,
    RightLeg = 1U << 4U,
    Horn = 1U << 5U,
};

[[nodiscard]] constexpr auto operator|(
    ColossusHiddenPart left,
    ColossusHiddenPart right) noexcept -> ColossusHiddenPart {
    return static_cast<ColossusHiddenPart>(
        static_cast<std::uint32_t>(left) |
        static_cast<std::uint32_t>(right));
}

struct ChainedColossusVisualInput {
    glm::vec3 position {0.0F};
    float yaw_radians = 0.0F;
    float animation_seconds = 0.0F;
    float health_ratio = 1.0F;
    float stagger_ratio = 0.0F;
    float movement_amount = 0.0F;
    ChainedColossusPhase phase =
        ChainedColossusPhase::Chained;
    ChainedColossusAttack attack =
        ChainedColossusAttack::None;
    ChainedColossusAttackStage attack_stage =
        ChainedColossusAttackStage::Idle;
    std::array<ColossusArmorState, 7U> armor_states {};
    std::uint32_t hidden_parts_mask = 0U;
    std::uint32_t wounded_zones_mask = 0U;
    GorePresentationMode gore_mode =
        GorePresentationMode::Full;
    float sky_light = 1.0F;
    float block_light = 0.0F;
};

struct ChainedColossusPartInstance {
    CreaturePartInstance geometry {};
    ColossusVisualPart visual_part =
        ColossusVisualPart::Torso;
    DamageZoneId zone_id = kColossusTorsoZone;
    ColossusArmorState armor_state =
        ColossusArmorState::Intact;
    bool wound_overlay = false;
};

struct ColossusVisualBounds {
    glm::vec3 minimum {0.0F};
    glm::vec3 maximum {0.0F};
};

struct ColossusBloodTrace {
    glm::vec3 position {0.0F};
    glm::vec3 normal {0.0F, 1.0F, 0.0F};
    float radius = 0.0F;
    float opacity = 0.0F;
    float age_seconds = 0.0F;
    float lifetime_seconds = 0.0F;
    std::uint32_t seed = 0U;
    bool muted = false;
};

class ColossusBloodTraceBuffer {
public:
    void clear() noexcept;
    void add_impact(const glm::vec3& position,
                    const glm::vec3& normal,
                    float intensity,
                    std::uint32_t seed,
                    GorePresentationMode mode);
    void update(float delta_seconds) noexcept;

    [[nodiscard]] auto traces() const noexcept
        -> std::span<const ColossusBloodTrace>;
    [[nodiscard]] auto accepted_impact_count() const noexcept
        -> std::uint64_t;

private:
    std::vector<ColossusBloodTrace> traces_ {};
    std::uint64_t accepted_impact_count_ = 0U;
};

[[nodiscard]] auto build_chained_colossus_parts(
    const ChainedColossusVisualInput& input)
    -> std::vector<ChainedColossusPartInstance>;
[[nodiscard]] auto chained_colossus_visual_bounds(
    std::span<const ChainedColossusPartInstance> parts) noexcept
    -> ColossusVisualBounds;
[[nodiscard]] constexpr auto
colossus_presentation_gameplay_signature(
    const ChainedColossusVisualInput& input) noexcept
    -> std::array<float, 3U> {
    // Je ne laisse aucun reglage de gore modifier ces trois donnees de combat.
    return {
        input.health_ratio,
        input.stagger_ratio,
        static_cast<float>(input.hidden_parts_mask),
    };
}

} // namespace valcraft
