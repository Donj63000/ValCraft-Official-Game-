#include "render/creatures/ChainedColossusPresentation.h"

#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto finite_clamped(float value,
                                  float minimum,
                                  float maximum,
                                  float fallback = 0.0F) noexcept -> float {
    return std::isfinite(value)
        ? std::clamp(value, minimum, maximum)
        : fallback;
}

[[nodiscard]] auto uniform_uvs(
    CreatureAtlasTile tile) noexcept -> std::array<BoxUvRect, 6U> {
    const auto coordinates = creature_atlas_tile_coordinates(tile);
    constexpr auto step = 1.0F / kCreatureAtlasTilesPerAxis;
    const auto rect = BoxUvRect {
        static_cast<float>(coordinates[0]) * step,
        static_cast<float>(coordinates[1]) * step,
        static_cast<float>(coordinates[0] + 1) * step,
        static_cast<float>(coordinates[1] + 1) * step,
    };
    auto result = std::array<BoxUvRect, 6U> {};
    result.fill(rect);
    return result;
}

[[nodiscard]] auto part_mask(ColossusHiddenPart part) noexcept
    -> std::uint32_t {
    return static_cast<std::uint32_t>(part);
}

[[nodiscard]] auto is_hidden(const ChainedColossusVisualInput& input,
                             ColossusHiddenPart part) noexcept -> bool {
    return (input.hidden_parts_mask & part_mask(part)) != 0U;
}

[[nodiscard]] auto zone_is_wounded(
    const ChainedColossusVisualInput& input,
    DamageZoneId zone_id) noexcept -> bool {
    if (zone_id >= 32U) {
        return false;
    }
    return (input.wounded_zones_mask &
            (std::uint32_t {1U} << zone_id)) != 0U;
}

void append_box(
    std::vector<ChainedColossusPartInstance>& parts,
    const glm::mat4& root,
    const glm::vec3& center,
    const glm::vec3& dimensions,
    const glm::vec3& rotation,
    CreatureAtlasTile tile,
    ColossusVisualPart visual_part,
    DamageZoneId zone_id,
    ColossusArmorState armor_state,
    bool wound_overlay,
    float material_class,
    float cavity,
    float emissive,
    float sky_light,
    float block_light) {
    if (dimensions.x <= 1.0e-4F ||
        dimensions.y <= 1.0e-4F ||
        dimensions.z <= 1.0e-4F) {
        return;
    }
    auto transform = glm::translate(root, center);
    transform =
        glm::rotate(transform, rotation.y, glm::vec3 {0.0F, 1.0F, 0.0F});
    transform =
        glm::rotate(transform, rotation.z, glm::vec3 {0.0F, 0.0F, 1.0F});
    transform =
        glm::rotate(transform, rotation.x, glm::vec3 {1.0F, 0.0F, 0.0F});
    transform = glm::scale(transform, dimensions);
    parts.push_back({
        {
            transform,
            uniform_uvs(tile),
            0.84F,
            wound_overlay ? 0.92F : 0.45F,
            material_class,
            cavity,
            emissive,
            sky_light,
            block_light,
            1.0F,
        },
        visual_part,
        zone_id,
        armor_state,
        wound_overlay,
    });
}

void append_armor(
    std::vector<ChainedColossusPartInstance>& parts,
    const glm::mat4& root,
    const glm::vec3& center,
    const glm::vec3& dimensions,
    const glm::vec3& rotation,
    DamageZoneId zone_id,
    ColossusArmorState armor_state,
    float sky_light,
    float block_light) {
    if (armor_state == ColossusArmorState::Broken) {
        return;
    }
    const auto scale =
        armor_state == ColossusArmorState::Cracked ? 0.86F : 1.0F;
    append_box(
        parts, root, center,
        dimensions * glm::vec3 {scale, 1.0F, scale},
        rotation, CreatureAtlasTile::CrewIron,
        ColossusVisualPart::Armor, zone_id, armor_state, false,
        0.96F,
        armor_state == ColossusArmorState::Cracked ? 0.72F : 0.36F,
        0.0F, sky_light, block_light);
}

void append_wound(
    std::vector<ChainedColossusPartInstance>& parts,
    const ChainedColossusVisualInput& input,
    const glm::mat4& root,
    const glm::vec3& center,
    const glm::vec3& dimensions,
    DamageZoneId zone_id) {
    if (!zone_is_wounded(input, zone_id)) {
        return;
    }
    const auto full =
        input.gore_mode == GorePresentationMode::Full;
    const auto reduced =
        input.gore_mode == GorePresentationMode::Reduced;
    append_box(
        parts, root, center,
        dimensions * (full ? 1.0F : 0.74F),
        {}, full ? CreatureAtlasTile::ZombieFlesh
                 : CreatureAtlasTile::ZombieScar,
        ColossusVisualPart::Wound, zone_id,
        ColossusArmorState::Broken, true,
        full ? 0.88F : 0.68F,
        full ? 0.92F : 0.72F,
        reduced ? 0.0F : (full ? 0.04F : 0.0F),
        input.sky_light, input.block_light);
}

[[nodiscard]] auto next_unit(std::uint32_t& state) noexcept -> float {
    state = state * 1664525U + 1013904223U;
    return static_cast<float>((state >> 8U) & 0x00FFFFFFU) /
           static_cast<float>(0x01000000U);
}

} // namespace

auto build_chained_colossus_parts(
    const ChainedColossusVisualInput& raw_input)
    -> std::vector<ChainedColossusPartInstance> {
    auto input = raw_input;
    input.health_ratio =
        finite_clamped(input.health_ratio, 0.0F, 1.0F, 1.0F);
    input.stagger_ratio =
        finite_clamped(input.stagger_ratio, 0.0F, 1.0F);
    input.movement_amount =
        finite_clamped(input.movement_amount, 0.0F, 1.0F);
    input.sky_light =
        finite_clamped(input.sky_light, 0.0F, 1.0F, 1.0F);
    input.block_light =
        finite_clamped(input.block_light, 0.0F, 1.0F);
    const auto time = std::isfinite(input.animation_seconds)
        ? input.animation_seconds
        : 0.0F;
    const auto yaw = std::isfinite(input.yaw_radians)
        ? input.yaw_radians
        : 0.0F;
    const auto breath = std::sin(time * 1.7F) * 0.035F;
    const auto walk = std::sin(time * 3.6F) *
                      input.movement_amount * 0.30F;
    const auto wounded =
        finite_clamped((0.55F - input.health_ratio) * 1.5F,
                       0.0F, 0.55F);
    const auto kneel =
        input.phase == ChainedColossusPhase::Kneeling
            ? 0.72F
            : 0.0F;

    auto root = glm::translate(
        glm::mat4 {1.0F},
        {
            std::isfinite(input.position.x) ? input.position.x : 0.0F,
            std::isfinite(input.position.y) ? input.position.y : 0.0F,
            std::isfinite(input.position.z) ? input.position.z : 0.0F,
        });
    root = glm::rotate(root, yaw, glm::vec3 {0.0F, 1.0F, 0.0F});
    root = glm::rotate(
        root, wounded * 0.08F,
        glm::vec3 {0.0F, 0.0F, 1.0F});

    std::vector<ChainedColossusPartInstance> parts {};
    parts.reserve(48U);

    if (!is_hidden(input, ColossusHiddenPart::LeftLeg)) {
        append_box(
            parts, root, {-0.48F, 0.92F - kneel, 0.0F},
            {0.60F, 1.84F, 0.66F}, {walk, 0.0F, 0.0F},
            CreatureAtlasTile::ZombieFlesh,
            ColossusVisualPart::LeftLeg, kColossusLeftLegZone,
            input.armor_states[4U], false, 0.84F, 0.48F, 0.0F,
            input.sky_light, input.block_light);
        append_armor(
            parts, root, {-0.50F, 1.30F - kneel, -0.35F},
            {0.68F, 0.56F, 0.12F}, {walk, 0.0F, 0.0F},
            kColossusLeftLegZone, input.armor_states[4U],
            input.sky_light, input.block_light);
        append_wound(
            parts, input, root, {-0.48F, 0.92F - kneel, -0.34F},
            {0.34F, 0.42F, 0.025F}, kColossusLeftLegZone);
    }
    if (!is_hidden(input, ColossusHiddenPart::RightLeg)) {
        append_box(
            parts, root, {0.48F, 0.92F - kneel, 0.0F},
            {0.60F, 1.84F, 0.66F}, {-walk, 0.0F, 0.0F},
            CreatureAtlasTile::ZombieFlesh,
            ColossusVisualPart::RightLeg, kColossusRightLegZone,
            input.armor_states[5U], false, 0.84F, 0.48F, 0.0F,
            input.sky_light, input.block_light);
        append_armor(
            parts, root, {0.50F, 1.30F - kneel, -0.35F},
            {0.68F, 0.56F, 0.12F}, {-walk, 0.0F, 0.0F},
            kColossusRightLegZone, input.armor_states[5U],
            input.sky_light, input.block_light);
        append_wound(
            parts, input, root, {0.48F, 0.92F - kneel, -0.34F},
            {0.34F, 0.42F, 0.025F}, kColossusRightLegZone);
    }

    append_box(
        parts, root, {0.0F, 2.08F - kneel * 0.58F, 0.0F},
        {1.46F, 0.58F, 0.92F}, {},
        CreatureAtlasTile::ZombieBone,
        ColossusVisualPart::Torso, kColossusTorsoZone,
        input.armor_states[0U], false, 0.88F, 0.52F, 0.0F,
        input.sky_light, input.block_light);
    append_box(
        parts, root,
        {0.0F, 2.92F + breath - kneel * 0.36F, 0.0F},
        {1.62F, 1.18F, 0.92F}, {0.0F, 0.0F, wounded * 0.08F},
        CreatureAtlasTile::ZombieFlesh,
        ColossusVisualPart::Torso, kColossusTorsoZone,
        input.armor_states[0U], false, 0.86F, 0.54F, 0.0F,
        input.sky_light, input.block_light);
    append_armor(
        parts, root,
        {0.0F, 3.02F + breath - kneel * 0.36F, -0.49F},
        {1.40F, 0.88F, 0.14F}, {},
        kColossusTorsoZone, input.armor_states[0U],
        input.sky_light, input.block_light);
    append_wound(
        parts, input, root,
        {0.08F, 2.94F + breath - kneel * 0.36F, -0.48F},
        {0.56F, 0.34F, 0.028F}, kColossusTorsoZone);

    auto attack_left = 0.0F;
    auto attack_right = 0.0F;
    if (input.attack_stage != ChainedColossusAttackStage::Idle) {
        if (input.attack == ChainedColossusAttack::ArmSweep) {
            attack_left = -0.82F;
        } else if (input.attack == ChainedColossusAttack::ChainSlam) {
            attack_right = 0.88F;
        }
    }
    if (!is_hidden(input, ColossusHiddenPart::LeftArm)) {
        append_box(
            parts, root,
            {-1.08F, 2.85F + breath - kneel * 0.34F, 0.0F},
            {0.62F, 1.64F, 0.62F},
            {attack_left, 0.0F, 0.28F + attack_left * 0.35F},
            CreatureAtlasTile::ZombieFlesh,
            ColossusVisualPart::LeftArm, kColossusLeftArmZone,
            input.armor_states[2U], false, 0.85F, 0.50F, 0.0F,
            input.sky_light, input.block_light);
        append_armor(
            parts, root,
            {-1.26F, 3.28F + breath - kneel * 0.34F, -0.29F},
            {0.62F, 0.58F, 0.16F},
            {attack_left, 0.0F, 0.28F},
            kColossusLeftArmZone, input.armor_states[2U],
            input.sky_light, input.block_light);
        append_wound(
            parts, input, root,
            {-1.10F, 2.80F + breath - kneel * 0.34F, -0.31F},
            {0.30F, 0.42F, 0.026F}, kColossusLeftArmZone);
    }
    if (!is_hidden(input, ColossusHiddenPart::RightArm)) {
        append_box(
            parts, root,
            {1.08F, 2.85F + breath - kneel * 0.34F, 0.0F},
            {0.62F, 1.64F, 0.62F},
            {attack_right, 0.0F, -0.28F - attack_right * 0.35F},
            CreatureAtlasTile::ZombieFlesh,
            ColossusVisualPart::RightArm, kColossusRightArmZone,
            input.armor_states[3U], false, 0.85F, 0.50F, 0.0F,
            input.sky_light, input.block_light);
        append_armor(
            parts, root,
            {1.26F, 3.28F + breath - kneel * 0.34F, -0.29F},
            {0.62F, 0.58F, 0.16F},
            {attack_right, 0.0F, -0.28F},
            kColossusRightArmZone, input.armor_states[3U],
            input.sky_light, input.block_light);
        append_wound(
            parts, input, root,
            {1.10F, 2.80F + breath - kneel * 0.34F, -0.31F},
            {0.30F, 0.42F, 0.026F}, kColossusRightArmZone);
    }

    if (!is_hidden(input, ColossusHiddenPart::Head)) {
        append_box(
            parts, root,
            {0.0F, 4.10F + breath * 0.4F - kneel * 0.26F, -0.02F},
            {0.92F, 0.74F, 0.80F}, {},
            CreatureAtlasTile::ZombieFlesh,
            ColossusVisualPart::Head, kColossusHeadZone,
            input.armor_states[1U], false, 0.90F, 0.62F,
            input.stagger_ratio * 0.10F,
            input.sky_light, input.block_light);
        append_armor(
            parts, root,
            {0.0F, 4.25F + breath * 0.4F - kneel * 0.26F, -0.43F},
            {0.88F, 0.48F, 0.12F}, {},
            kColossusHeadZone, input.armor_states[1U],
            input.sky_light, input.block_light);
        append_wound(
            parts, input, root,
            {0.18F, 4.08F + breath * 0.4F - kneel * 0.26F, -0.43F},
            {0.26F, 0.22F, 0.026F}, kColossusHeadZone);
        if (!is_hidden(input, ColossusHiddenPart::Horn)) {
            append_box(
                parts, root,
                {0.0F, 4.64F + breath * 0.3F - kneel * 0.26F, 0.0F},
                {0.18F, 0.22F, 0.28F}, {0.0F, 0.0F, 0.0F},
                CreatureAtlasTile::ZombieHorn,
                ColossusVisualPart::Horn, kColossusHornZone,
                input.armor_states[6U], false, 0.92F, 0.42F, 0.0F,
                input.sky_light, input.block_light);
        }
    }

    if (input.phase == ChainedColossusPhase::Chained) {
        for (const auto side : std::array<float, 2U> {-1.0F, 1.0F}) {
            for (std::size_t link = 0U; link < 7U; ++link) {
                const auto link_value = static_cast<float>(link);
                append_box(
                    parts, root,
                    {
                        side * (1.18F + link_value * 0.30F),
                        3.02F - link_value * 0.24F,
                        0.12F,
                    },
                    {0.10F, 0.28F, 0.10F},
                    {0.0F, 0.0F, side * 0.72F},
                    CreatureAtlasTile::CrewIron,
                    ColossusVisualPart::Chain, kColossusTorsoZone,
                    ColossusArmorState::Intact, false,
                    0.98F, 0.30F, 0.0F,
                    input.sky_light, input.block_light);
            }
        }
    }
    return parts;
}

auto chained_colossus_visual_bounds(
    std::span<const ChainedColossusPartInstance> parts) noexcept
    -> ColossusVisualBounds {
    if (parts.empty()) {
        return {};
    }
    auto minimum =
        glm::vec3 {std::numeric_limits<float>::infinity()};
    auto maximum =
        glm::vec3 {-std::numeric_limits<float>::infinity()};
    for (const auto& part : parts) {
        const auto& transform = part.geometry.transform;
        const auto center = glm::vec3 {transform[3]};
        // Je projette chaque axe oriente pour obtenir une vraie AABB monde.
        const auto half_extent =
            (glm::abs(glm::vec3 {transform[0]}) +
             glm::abs(glm::vec3 {transform[1]}) +
             glm::abs(glm::vec3 {transform[2]})) *
            0.5F;
        minimum = glm::min(minimum, center - half_extent);
        maximum = glm::max(maximum, center + half_extent);
    }
    return {minimum, maximum};
}

void ColossusBloodTraceBuffer::clear() noexcept {
    traces_.clear();
    accepted_impact_count_ = 0U;
}

void ColossusBloodTraceBuffer::add_impact(
    const glm::vec3& raw_position,
    const glm::vec3& raw_normal,
    float raw_intensity,
    std::uint32_t seed,
    GorePresentationMode mode) {
    ++accepted_impact_count_;
    if (mode == GorePresentationMode::Disabled) {
        return;
    }

    const auto intensity =
        finite_clamped(raw_intensity, 0.0F, 1.0F);
    auto position = raw_position;
    if (!std::isfinite(position.x) ||
        !std::isfinite(position.y) ||
        !std::isfinite(position.z)) {
        position = {};
    }
    auto normal = raw_normal;
    if (!std::isfinite(normal.x) ||
        !std::isfinite(normal.y) ||
        !std::isfinite(normal.z) ||
        glm::length(normal) <= 1.0e-5F) {
        normal = {0.0F, 1.0F, 0.0F};
    } else {
        normal = glm::normalize(normal);
    }
    if (seed == 0U) {
        seed = 1U;
    }

    const auto trace_count =
        mode == GorePresentationMode::Reduced
            ? 1U
            : static_cast<std::uint32_t>(
                  std::clamp(
                      static_cast<int>(std::lround(1.0F +
                                                  intensity * 3.0F)),
                      1, 4));
    for (std::uint32_t index = 0U; index < trace_count; ++index) {
        const auto jitter_x = next_unit(seed) - 0.5F;
        const auto jitter_z = next_unit(seed) - 0.5F;
        const auto radius_variation = next_unit(seed);
        if (traces_.size() == kColossusMaximumBloodTraces) {
            traces_.erase(traces_.begin());
        }
        traces_.push_back({
            position +
                glm::vec3 {jitter_x * 0.24F, 0.0F, jitter_z * 0.24F},
            normal,
            (mode == GorePresentationMode::Reduced ? 0.10F : 0.14F) +
                radius_variation * intensity * 0.20F,
            mode == GorePresentationMode::Reduced
                ? 0.28F
                : 0.55F + intensity * 0.35F,
            0.0F,
            mode == GorePresentationMode::Reduced ? 4.0F : 8.0F,
            seed,
            mode == GorePresentationMode::Reduced,
        });
    }
}

void ColossusBloodTraceBuffer::update(float delta_seconds) noexcept {
    const auto delta =
        finite_clamped(delta_seconds, 0.0F, 60.0F);
    for (auto& trace : traces_) {
        trace.age_seconds =
            std::min(trace.age_seconds + delta, trace.lifetime_seconds);
        const auto remaining =
            trace.lifetime_seconds > 1.0e-5F
                ? 1.0F - trace.age_seconds / trace.lifetime_seconds
                : 0.0F;
        trace.opacity = std::min(trace.opacity, remaining);
    }
    std::erase_if(
        traces_,
        [](const ColossusBloodTrace& trace) noexcept {
            return trace.age_seconds >= trace.lifetime_seconds;
        });
}

auto ColossusBloodTraceBuffer::traces() const noexcept
    -> std::span<const ColossusBloodTrace> {
    return traces_;
}

auto ColossusBloodTraceBuffer::accepted_impact_count() const noexcept
    -> std::uint64_t {
    return accepted_impact_count_;
}

} // namespace valcraft
