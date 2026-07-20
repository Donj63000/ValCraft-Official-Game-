#include "creatures/CreatureGeometry.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr float kMaterialHide = 0.24F;
constexpr float kMaterialSkin = 0.34F;
constexpr float kMaterialWool = 0.18F;
constexpr float kMaterialHorn = 0.62F;
constexpr float kMaterialKeratin = 0.66F;
constexpr float kMaterialTransitionHide = 0.46F;
constexpr float kMaterialGore = 0.56F;
constexpr float kMaterialZombieFlesh = 0.74F;
constexpr float kMaterialZombieBone = 0.88F;
constexpr float kMaterialZombieGlow = 1.00F;
constexpr float kMaterialCrewFabric = 0.22F;
constexpr float kMaterialCrewSkin = 0.34F;
constexpr float kMaterialCrewLeather = 0.52F;
constexpr float kMaterialCrewWood = 0.58F;
constexpr float kMaterialCrewMetal = 0.86F;
constexpr std::array<float, 2> kSides {{-1.0F, 1.0F}};

struct FaceDefinition {
    std::array<glm::vec3, 4> corners;
    glm::vec3 normal;
};

struct CreatureVisualState {
    float morph = 0.0F;
    float day_presence = 1.0F;
    float transition_presence = 0.0F;
    float night_presence = 0.0F;
    float corruption = 1.0F;
};

auto make_face_definition(std::array<glm::vec3, 4> corners, const glm::vec3& normal) -> FaceDefinition {
    return {corners, normal};
}

auto box_faces() -> const std::array<FaceDefinition, 6>& {
    static const std::array<FaceDefinition, 6> kFaces {{
        make_face_definition(
            {glm::vec3 {0.5F, -0.5F, -0.5F}, glm::vec3 {0.5F, 0.5F, -0.5F}, glm::vec3 {0.5F, 0.5F, 0.5F}, glm::vec3 {0.5F, -0.5F, 0.5F}},
            glm::vec3 {1.0F, 0.0F, 0.0F}),
        make_face_definition(
            {glm::vec3 {-0.5F, -0.5F, 0.5F}, glm::vec3 {-0.5F, 0.5F, 0.5F}, glm::vec3 {-0.5F, 0.5F, -0.5F}, glm::vec3 {-0.5F, -0.5F, -0.5F}},
            glm::vec3 {-1.0F, 0.0F, 0.0F}),
        make_face_definition(
            {glm::vec3 {-0.5F, 0.5F, 0.5F}, glm::vec3 {0.5F, 0.5F, 0.5F}, glm::vec3 {0.5F, 0.5F, -0.5F}, glm::vec3 {-0.5F, 0.5F, -0.5F}},
            glm::vec3 {0.0F, 1.0F, 0.0F}),
        make_face_definition(
            {glm::vec3 {-0.5F, -0.5F, -0.5F}, glm::vec3 {0.5F, -0.5F, -0.5F}, glm::vec3 {0.5F, -0.5F, 0.5F}, glm::vec3 {-0.5F, -0.5F, 0.5F}},
            glm::vec3 {0.0F, -1.0F, 0.0F}),
        make_face_definition(
            {glm::vec3 {0.5F, -0.5F, 0.5F}, glm::vec3 {0.5F, 0.5F, 0.5F}, glm::vec3 {-0.5F, 0.5F, 0.5F}, glm::vec3 {-0.5F, -0.5F, 0.5F}},
            glm::vec3 {0.0F, 0.0F, 1.0F}),
        make_face_definition(
            {glm::vec3 {-0.5F, -0.5F, -0.5F}, glm::vec3 {-0.5F, 0.5F, -0.5F}, glm::vec3 {0.5F, 0.5F, -0.5F}, glm::vec3 {0.5F, -0.5F, -0.5F}},
            glm::vec3 {0.0F, 0.0F, -1.0F}),
    }};
    return kFaces;
}

auto saturate(float value) noexcept -> float {
    return glm::clamp(value, 0.0F, 1.0F);
}

auto smoothstep01(float value) noexcept -> float {
    const auto clamped = saturate(value);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

auto smooth_range(float edge0, float edge1, float value) noexcept -> float {
    const auto width = edge1 - edge0;
    if (std::abs(width) <= 1.0e-6F) {
        return value >= edge1 ? 1.0F : 0.0F;
    }
    return smoothstep01((value - edge0) / width);
}

auto bell_range(float value, float center, float radius) noexcept -> float {
    return smoothstep01(1.0F - std::abs(value - center) / std::max(radius, 1.0e-4F));
}

auto safe_horizontal_direction(const glm::vec3& direction) noexcept -> glm::vec3 {
    const auto horizontal = glm::vec3 {direction.x, 0.0F, direction.z};
    if (glm::dot(horizontal, horizontal) > 1.0e-6F) {
        return glm::normalize(horizontal);
    }
    return {0.0F, 0.0F, 1.0F};
}

auto seed_unit(std::uint32_t seed, int bit_shift) noexcept -> float {
    return static_cast<float>((seed >> bit_shift) & 0xFFU) / 255.0F;
}

auto hash_to_unit(int x, int y, int seed) noexcept -> float {
    auto value = static_cast<std::uint32_t>(x) * 374761393U;
    value ^= static_cast<std::uint32_t>(y) * 668265263U;
    value ^= static_cast<std::uint32_t>(seed) * 2246822519U;
    value = (value ^ (value >> 13U)) * 1274126177U;
    value ^= value >> 16U;
    return static_cast<float>(value & 0xFFFFU) / 65535.0F;
}

auto seed_detail_unit(std::uint32_t seed, int salt) noexcept -> float {
    const auto low = static_cast<int>(seed & 0xFFFFU);
    const auto high = static_cast<int>((seed >> 16U) & 0xFFFFU);
    return hash_to_unit(low + salt * 23, high - salt * 17, salt + 91);
}

auto seed_detail_signed(std::uint32_t seed, int salt) noexcept -> float {
    return seed_detail_unit(seed, salt) * 2.0F - 1.0F;
}

auto tile_noise(int x, int y, int seed) noexcept -> float {
    const auto coarse = hash_to_unit(x / 2 + seed, y / 2 + seed * 3, seed + 17);
    const auto fine = hash_to_unit(x + seed * 11, y + seed * 7, seed + 29);
    return coarse * 0.42F + fine * 0.58F;
}

auto radial_falloff(float x, float y, float center_x, float center_y, float radius) noexcept -> float {
    const auto dx = x - center_x;
    const auto dy = y - center_y;
    const auto distance = std::sqrt(dx * dx + dy * dy);
    return saturate(1.0F - distance / std::max(radius, 0.001F));
}

auto line_mask(float value, float center, float half_width) noexcept -> float {
    return saturate(1.0F - std::abs(value - center) / std::max(half_width, 0.001F));
}

auto edge_distance(float nx, float ny) noexcept -> float {
    return std::min({nx, ny, 1.0F - nx, 1.0F - ny});
}

auto to_byte(float value) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 255.0F));
}

auto make_rgba(float r, float g, float b, float a = 0.0F) noexcept -> std::array<std::uint8_t, 4> {
    return {to_byte(r), to_byte(g), to_byte(b), to_byte(a)};
}

void set_texel(std::vector<std::uint8_t>& pixels, int x, int y, const std::array<std::uint8_t, 4>& rgba) {
    const auto index = static_cast<std::size_t>((y * kCreatureAtlasSize + x) * 4);
    pixels[index + 0] = rgba[0];
    pixels[index + 1] = rgba[1];
    pixels[index + 2] = rgba[2];
    pixels[index + 3] = rgba[3];
}

template <typename ColorFn>
void fill_tile(std::vector<std::uint8_t>& pixels, int tile_x, int tile_y, const ColorFn& color_fn) {
    const auto start_x = tile_x * kCreatureAtlasTileSize;
    const auto start_y = tile_y * kCreatureAtlasTileSize;
    for (int y = 0; y < kCreatureAtlasTileSize; ++y) {
        for (int x = 0; x < kCreatureAtlasTileSize; ++x) {
            set_texel(pixels, start_x + x, start_y + y, color_fn(x, y));
        }
    }
}

auto make_transform(const glm::mat4& root,
                    const glm::vec3& center,
                    const glm::vec3& rotation_radians,
                    const glm::vec3& half_extent) -> glm::mat4 {
    auto transform = glm::translate(root, center);
    transform = glm::rotate(transform, rotation_radians.y, glm::vec3 {0.0F, 1.0F, 0.0F});
    transform = glm::rotate(transform, rotation_radians.z, glm::vec3 {0.0F, 0.0F, 1.0F});
    transform = glm::rotate(transform, rotation_radians.x, glm::vec3 {1.0F, 0.0F, 0.0F});
    return glm::scale(transform, half_extent * 2.0F);
}

auto make_uniform_uvs(CreatureAtlasTile tile) -> std::array<BoxUvRect, 6> {
    const auto tile_coordinates = creature_atlas_tile_coordinates(tile);
    const auto uv_step = 1.0F / kCreatureAtlasTilesPerAxis;
    const auto u0 = static_cast<float>(tile_coordinates[0]) * uv_step;
    const auto v0 = static_cast<float>(tile_coordinates[1]) * uv_step;
    const auto rect = BoxUvRect {u0, v0, u0 + uv_step, v0 + uv_step};
    std::array<BoxUvRect, 6> face_uvs {};
    face_uvs.fill(rect);
    return face_uvs;
}

void append_box(std::vector<CreaturePartInstance>& parts,
                const glm::mat4& root,
                const glm::vec3& center,
                const glm::vec3& half_extent,
                const glm::vec3& rotation_radians,
                CreatureAtlasTile tile,
                float nightmare_factor,
                float tension,
                float material_class,
                float cavity_mask,
                float emissive_strength) {
    if (half_extent.x <= 1.0e-4F || half_extent.y <= 1.0e-4F || half_extent.z <= 1.0e-4F) {
        return;
    }

    parts.push_back({
        make_transform(root, center, rotation_radians, half_extent),
        make_uniform_uvs(tile),
        nightmare_factor,
        tension,
        material_class,
        cavity_mask,
        emissive_strength,
    });
}

void append_pair(std::vector<CreaturePartInstance>& parts,
                 const glm::mat4& root,
                 const glm::vec3& center,
                 float z_offset,
                 const glm::vec3& half_extent,
                 const glm::vec3& base_rotation,
                 const glm::vec3& side_rotation_delta,
                 CreatureAtlasTile tile,
                 float nightmare_factor,
                 float tension,
                 float material_class,
                 float cavity_mask,
                 float emissive_strength) {
    for (const auto side : kSides) {
        append_box(
            parts,
            root,
            center + glm::vec3 {0.0F, 0.0F, side * z_offset},
            half_extent,
            base_rotation + side_rotation_delta * side,
            tile,
            nightmare_factor,
            tension,
            material_class,
            cavity_mask,
            emissive_strength);
    }
}

auto segment_rotation_z(const glm::vec2& top, const glm::vec2& bottom) noexcept -> float {
    const auto axis = top - bottom;
    if (glm::dot(axis, axis) <= 1.0e-8F) {
        return 0.0F;
    }

    return std::atan2(-axis.x, axis.y);
}

void append_limb_segment(std::vector<CreaturePartInstance>& parts,
                         const glm::mat4& root,
                         const glm::vec2& top,
                         const glm::vec2& bottom,
                         float z,
                         float half_x,
                         float half_z,
                         float overlap,
                         CreatureAtlasTile tile,
                         float nightmare_factor,
                         float tension,
                         float material_class,
                         float cavity_mask,
                         float emissive_strength) {
    // Je fais legerement chevaucher les segments pour eviter les trous visibles aux articulations.
    const auto axis = top - bottom;
    const auto length = glm::length(axis);
    if (length <= 1.0e-4F) {
        return;
    }

    const auto direction = axis / length;
    const auto adjusted_top = top + direction * overlap;
    const auto adjusted_bottom = bottom - direction * overlap;
    const auto adjusted_length = glm::length(adjusted_top - adjusted_bottom);
    const auto center = (adjusted_top + adjusted_bottom) * 0.5F;
    append_box(parts,
               root,
               glm::vec3 {center.x, center.y, z},
               glm::vec3 {half_x, adjusted_length * 0.5F, half_z},
               glm::vec3 {0.0F, 0.0F, segment_rotation_z(adjusted_top, adjusted_bottom)},
               tile,
               nightmare_factor,
               tension,
               material_class,
               cavity_mask,
               emissive_strength);
}

void append_quadruped_leg(std::vector<CreaturePartInstance>& parts,
                          const glm::mat4& root,
                          float anchor_x,
                          float anchor_y,
                          float side_z,
                          float foot_swing,
                          float knee_bias,
                          const glm::vec2& upper_half_xz,
                          const glm::vec2& lower_half_xz,
                          const glm::vec3& hoof_half,
                          CreatureAtlasTile upper_tile,
                          CreatureAtlasTile lower_tile,
                          CreatureAtlasTile hoof_tile,
                          float nightmare_factor,
                          float tension,
                          float leg_material_class,
                          float hoof_material_class) {
    const auto clamped_swing = std::clamp(foot_swing, -0.18F, 0.18F);
    const auto hoof_top_y = std::max(hoof_half.y * 2.0F - 0.002F, 0.025F);
    const glm::vec2 hip {anchor_x, anchor_y};
    const glm::vec2 hoof_top {anchor_x + clamped_swing, hoof_top_y};
    const auto lift = std::abs(clamped_swing) * 0.12F;
    const glm::vec2 knee {
        glm::mix(hip.x, hoof_top.x, 0.54F) + knee_bias,
        glm::mix(hip.y, hoof_top.y, 0.54F) + lift,
    };
    const auto joint_overlap = std::max(0.010F, hoof_half.y * 0.30F);

    append_limb_segment(parts,
                        root,
                        hip,
                        knee,
                        side_z,
                        upper_half_xz.x,
                        upper_half_xz.y,
                        joint_overlap,
                        upper_tile,
                        nightmare_factor,
                        tension,
                        leg_material_class,
                        0.08F,
                        0.0F);
    append_limb_segment(parts,
                        root,
                        knee,
                        hoof_top,
                        side_z,
                        lower_half_xz.x,
                        lower_half_xz.y,
                        joint_overlap,
                        lower_tile,
                        nightmare_factor,
                        tension,
                        leg_material_class,
                        0.07F,
                        0.0F);

    append_box(parts,
               root,
               glm::vec3 {hoof_top.x + clamped_swing * 0.10F, hoof_half.y, side_z},
               hoof_half,
               glm::vec3 {0.0F, 0.0F, clamped_swing * 0.12F},
               hoof_tile,
               nightmare_factor,
               tension,
               hoof_material_class,
               0.03F,
               0.0F);
}

auto build_visual_state(const CreatureRenderInstance& creature) noexcept -> CreatureVisualState {
    const auto morph = saturate(creature.morph_factor);
    auto transition = bell_range(morph, 0.50F, 0.38F);
    transition *= smooth_range(0.06F, 0.18F, morph);
    transition *= 1.0F - smooth_range(0.82F, 0.98F, morph);

    return {
        morph,
        1.0F - smooth_range(0.12F, 0.78F, morph),
        transition,
        smooth_range(0.16F, 0.94F, morph),
        creature.phase == CreaturePhase::DawnRecover ? 0.72F : 1.0F,
    };
}

void append_transition_accents(std::vector<CreaturePartInstance>& mesh,
                               const CreatureRenderInstance& creature,
                               const CreatureVisualState& state,
                               const glm::mat4& root) {
    const auto presence = state.transition_presence * state.corruption;
    if (presence <= 1.0e-4F) {
        return;
    }

    const auto tension = saturate(creature.tension);
    const auto attack = saturate(creature.attack_amount);
    const auto phase = seed_unit(creature.appearance_seed, 18) * kTwoPi;
    const auto pulse = 0.58F + 0.42F * std::sin(creature.animation_time * 10.5F + phase);

    float shoulder_span = 0.18F;
    float hip_span = 0.15F;
    float torso_depth = 0.12F;
    float eye_z = 0.07F;
    bool horn_buds = false;
    bool wool_shroud = false;

    switch (creature.species) {
    case CreatureSpecies::Pig:
        shoulder_span = 0.18F;
        hip_span = 0.14F;
        torso_depth = 0.12F;
        eye_z = 0.07F;
        break;
    case CreatureSpecies::Cow:
        shoulder_span = 0.21F;
        hip_span = 0.16F;
        torso_depth = 0.13F;
        eye_z = 0.08F;
        horn_buds = true;
        break;
    case CreatureSpecies::Sheep:
    default:
        shoulder_span = 0.17F;
        hip_span = 0.14F;
        torso_depth = 0.12F;
        eye_z = 0.075F;
        wool_shroud = true;
        break;
    }

    append_box(mesh, root, glm::vec3 {0.02F, 1.06F, 0.0F},
               glm::vec3 {0.060F * presence, 0.27F * presence, torso_depth * presence},
               glm::vec3 {0.0F, 0.0F, 0.06F + tension * 0.04F}, CreatureAtlasTile::TransformSinew,
               state.morph, tension, kMaterialGore, 0.58F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.08F, 1.12F, 0.0F}, shoulder_span,
                glm::vec3 {0.020F * presence, 0.18F * presence, 0.016F * presence},
                glm::vec3 {0.0F, 0.0F, -0.08F - attack * 0.08F}, glm::vec3 {0.0F, 0.0F, 0.05F},
                CreatureAtlasTile::TransformGlow, state.morph, tension, kMaterialZombieGlow, 0.94F, pulse * 0.85F);
    append_pair(mesh, root, glm::vec3 {0.42F, 1.42F, 0.0F}, eye_z,
                glm::vec3 {0.028F * presence, 0.046F * presence, 0.020F * presence},
                glm::vec3 {-0.10F, 0.0F, 0.0F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieEye, state.morph, tension, kMaterialZombieGlow, 0.96F, pulse);
    append_pair(mesh, root, glm::vec3 {-0.08F, 0.56F, 0.0F}, hip_span,
                glm::vec3 {0.022F * presence, 0.32F * presence, 0.018F * presence},
                glm::vec3 {0.0F, 0.0F, 0.10F}, glm::vec3 {0.0F, 0.0F, 0.03F},
                CreatureAtlasTile::TransformHide, state.morph, tension, kMaterialTransitionHide, 0.44F, 0.0F);

    for (int index = 0; index < 3; ++index) {
        const auto offset = static_cast<float>(index) - 1.0F;
        append_box(mesh, root,
                   glm::vec3 {-0.16F + static_cast<float>(index) * 0.14F, 1.10F + std::abs(offset) * 0.04F, 0.0F},
                   glm::vec3 {0.026F * presence, (0.13F - std::abs(offset) * 0.02F) * presence, 0.032F * presence},
                   glm::vec3 {0.10F * offset, 0.0F, 0.18F * offset},
                   index == 1 ? CreatureAtlasTile::TransformSinew : CreatureAtlasTile::TransformHide,
                   state.morph, tension, index == 1 ? kMaterialGore : kMaterialTransitionHide, 0.68F, index == 1 ? pulse * 0.32F : 0.0F);
    }

    switch (creature.species) {
    case CreatureSpecies::Pig:
        append_box(mesh, root, glm::vec3 {0.56F, 1.18F, 0.0F},
                   glm::vec3 {0.08F * presence, 0.06F * presence, 0.10F * presence},
                   glm::vec3 {-0.08F, 0.0F, 0.0F}, CreatureAtlasTile::TransformHide,
                   state.morph, tension, kMaterialTransitionHide, 0.34F, 0.0F);
        break;
    case CreatureSpecies::Cow:
        if (horn_buds) {
            append_pair(mesh, root, glm::vec3 {0.50F, 1.54F, 0.0F}, 0.10F,
                        glm::vec3 {0.018F * presence, 0.07F * presence, 0.020F * presence},
                        glm::vec3 {-0.20F, 0.0F, -0.12F}, glm::vec3 {0.0F, 0.0F, -0.08F},
                        CreatureAtlasTile::ZombieHorn, state.morph, tension, kMaterialHorn, 0.28F, 0.0F);
        }
        break;
    case CreatureSpecies::Sheep:
    default:
        if (wool_shroud) {
            append_box(mesh, root, glm::vec3 {0.00F, 1.24F, 0.0F},
                       glm::vec3 {0.12F * presence, 0.12F * presence, 0.19F * presence},
                       glm::vec3 {0.06F, 0.0F, 0.02F}, CreatureAtlasTile::ZombieWool,
                       state.morph, tension, kMaterialWool, 0.24F, pulse * 0.18F);
        }
        break;
    }
}

void append_day_pig(std::vector<CreaturePartInstance>& mesh,
                    const CreatureRenderInstance& creature,
                    const CreatureVisualState& state,
                    const glm::mat4& root) {
    const auto presence = state.day_presence;
    if (presence <= 1.0e-4F) {
        return;
    }

    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto attack = saturate(creature.attack_amount);
    const auto tension = saturate(creature.tension);
    const auto graze = creature.behavior_state == CreatureBehaviorState::Graze ? 1.0F : 0.0F;
    const auto phase = seed_unit(creature.appearance_seed, 24) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (5.8F + motion * 4.4F) + phase);
    const auto breath = std::sin(creature.animation_time * 2.3F + phase * 0.4F) * 0.018F;
    const auto sway = std::sin(creature.animation_time * 3.1F + phase) * (0.012F + motion * 0.018F);
    const auto head_pitch = 0.02F + gaze * 0.05F - attack * 0.10F +
                            graze * (0.15F + std::sin(creature.animation_time * 3.0F + phase) * 0.025F);
    const auto head_yaw = std::sin(creature.animation_time * 1.6F + phase) * 0.05F + gaze * 0.04F;
    const auto front_swing = -stride * (0.16F + motion * 0.10F);
    const auto rear_swing = stride * (0.18F + motion * 0.12F);
    const auto scale = 0.62F + 0.38F * presence;
    const auto collapse = (1.0F - presence) * 0.10F;
    const auto body_length = (0.32F + seed_detail_signed(creature.appearance_seed, 1) * 0.025F) * scale;
    const auto body_width = (0.19F + seed_detail_signed(creature.appearance_seed, 2) * 0.018F) * scale;
    const auto body_height = (0.18F + seed_detail_unit(creature.appearance_seed, 3) * 0.03F) * scale;
    const auto shoulder_height = body_height * 0.96F;
    const auto haunch_height = body_height * 1.02F;
    const auto head_size = (0.15F + seed_detail_unit(creature.appearance_seed, 4) * 0.02F) * scale;
    const auto snout_length = (0.10F + seed_detail_unit(creature.appearance_seed, 5) * 0.02F) * scale;
    const auto ear_height = (0.05F + seed_detail_unit(creature.appearance_seed, 6) * 0.012F) * scale;
    const auto hoof_height = 0.035F * scale;
    const auto ear_tilt = 0.24F + std::sin(creature.animation_time * 4.0F + phase) * 0.10F;

    append_box(mesh, root, glm::vec3 {-0.04F, 0.78F + breath - collapse * 0.35F, 0.0F},
               glm::vec3 {body_length, shoulder_height, body_width},
               glm::vec3 {0.01F, 0.0F, 0.02F + sway * 0.25F}, CreatureAtlasTile::PigHide,
               state.morph, tension, kMaterialHide, 0.16F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.20F, 0.80F + breath * 0.5F - collapse * 0.25F, 0.0F},
               glm::vec3 {body_length * 0.36F, body_height * 0.92F, body_width * 0.96F},
               glm::vec3 {-0.04F, 0.0F, 0.03F}, CreatureAtlasTile::PigHide,
               state.morph, tension, kMaterialHide, 0.18F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.36F, 0.78F + breath * 0.35F - collapse * 0.18F, 0.0F},
               glm::vec3 {0.075F * scale, 0.105F * scale, body_width * 0.58F},
               glm::vec3 {head_pitch * 0.25F, head_yaw * 0.20F, -0.03F}, CreatureAtlasTile::PigHide,
               state.morph, tension, kMaterialHide, 0.12F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.27F, 0.79F + breath * 0.4F - collapse * 0.25F, 0.0F},
               glm::vec3 {body_length * 0.30F, haunch_height, body_width},
               glm::vec3 {0.05F, 0.0F, -0.02F}, CreatureAtlasTile::PigHide,
               state.morph, tension, kMaterialHide, 0.17F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.02F, 0.60F - collapse * 0.45F, 0.0F},
               glm::vec3 {body_length * 0.50F, body_height * 0.28F, body_width * 0.82F},
               glm::vec3 {0.0F}, CreatureAtlasTile::PigBelly,
               state.morph, tension, kMaterialSkin, 0.10F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.43F, 0.80F + breath * 0.3F - collapse * 0.18F, 0.0F},
               glm::vec3 {head_size * 0.56F, head_size * 0.44F, head_size * 0.62F},
               glm::vec3 {head_pitch * 0.5F, head_yaw * 0.4F, -0.04F}, CreatureAtlasTile::PigHide,
               state.morph, tension, kMaterialHide, 0.14F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.60F, 0.78F + breath * 0.2F - collapse * 0.12F, 0.0F},
               glm::vec3 {head_size, head_size * 0.70F, head_size * 0.70F},
               glm::vec3 {head_pitch, head_yaw, 0.0F}, CreatureAtlasTile::PigHide,
               state.morph, tension, kMaterialHide, 0.18F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.78F, 0.70F - collapse * 0.10F, 0.0F},
               glm::vec3 {snout_length, head_size * 0.32F, head_size * 0.44F},
               glm::vec3 {head_pitch * 0.4F, head_yaw, 0.0F}, CreatureAtlasTile::PigSnout,
               state.morph, tension, kMaterialSkin, 0.08F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.70F, 0.60F - collapse * 0.10F, 0.0F},
               glm::vec3 {head_size * 0.44F, head_size * 0.18F, head_size * 0.36F},
               glm::vec3 {head_pitch * 0.15F, head_yaw, -0.03F}, CreatureAtlasTile::PigBelly,
               state.morph, tension, kMaterialSkin, 0.10F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.70F, 0.84F - collapse * 0.08F, 0.0F}, head_size * 0.62F,
                glm::vec3 {0.014F * scale, 0.020F * scale, 0.008F * scale},
                glm::vec3 {0.0F}, glm::vec3 {0.0F},
                CreatureAtlasTile::PigHoof, state.morph, tension, kMaterialHorn, 0.02F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.88F, 0.71F - collapse * 0.08F, 0.0F}, head_size * 0.28F,
                glm::vec3 {0.012F * scale, 0.014F * scale, 0.006F * scale},
                glm::vec3 {0.0F}, glm::vec3 {0.0F},
                CreatureAtlasTile::PigHoof, state.morph, tension, kMaterialHorn, 0.02F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.59F, 0.97F - collapse * 0.12F, 0.0F}, head_size * 0.46F,
                glm::vec3 {head_size * 0.18F, ear_height, head_size * 0.12F},
                glm::vec3 {-0.08F, 0.0F, 0.08F}, glm::vec3 {0.0F, 0.0F, -ear_tilt},
                CreatureAtlasTile::PigEar, state.morph, tension, kMaterialSkin, 0.05F, 0.0F);

    for (const auto side : kSides) {
        const auto side_phase = side > 0.0F ? 1.0F : -1.0F;
        append_quadruped_leg(mesh,
                             root,
                             0.18F,
                             0.80F + breath * 0.5F - collapse * 0.25F - body_height * 0.92F + 0.026F,
                             side * 0.14F,
                             front_swing * side_phase * 0.34F,
                             -0.020F * scale,
                             glm::vec2 {0.046F * scale, 0.042F * scale},
                             glm::vec2 {0.040F * scale, 0.038F * scale},
                             glm::vec3 {0.052F * scale, hoof_height * 0.56F, 0.046F * scale},
                             CreatureAtlasTile::PigHide,
                             CreatureAtlasTile::PigHide,
                             CreatureAtlasTile::PigHoof,
                             state.morph,
                             tension,
                             kMaterialSkin,
                             kMaterialHorn);
        append_quadruped_leg(mesh,
                             root,
                             -0.22F,
                             0.79F + breath * 0.4F - collapse * 0.25F - haunch_height + 0.026F,
                             side * 0.15F,
                             rear_swing * side_phase * 0.34F,
                             0.018F * scale,
                             glm::vec2 {0.050F * scale, 0.044F * scale},
                             glm::vec2 {0.042F * scale, 0.038F * scale},
                             glm::vec3 {0.054F * scale, hoof_height * 0.56F, 0.046F * scale},
                             CreatureAtlasTile::PigHide,
                             CreatureAtlasTile::PigHide,
                             CreatureAtlasTile::PigHoof,
                             state.morph,
                             tension,
                             kMaterialSkin,
                             kMaterialHorn);
    }

    append_box(mesh, root, glm::vec3 {-0.44F, 0.84F - collapse * 0.20F, 0.0F},
               glm::vec3 {0.020F * scale, 0.070F * scale, 0.020F * scale},
               glm::vec3 {0.0F, 0.0F, 0.20F + stride * 0.05F}, CreatureAtlasTile::PigHide,
               state.morph, tension, kMaterialSkin, 0.04F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.50F, 0.90F - collapse * 0.18F, 0.0F},
               glm::vec3 {0.018F * scale, 0.045F * scale, 0.018F * scale},
               glm::vec3 {0.0F, 0.0F, -0.24F - stride * 0.04F}, CreatureAtlasTile::PigHide,
               state.morph, tension, kMaterialSkin, 0.04F, 0.0F);
}

void append_day_cow(std::vector<CreaturePartInstance>& mesh,
                    const CreatureRenderInstance& creature,
                    const CreatureVisualState& state,
                    const glm::mat4& root) {
    const auto presence = state.day_presence;
    if (presence <= 1.0e-4F) {
        return;
    }

    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto attack = saturate(creature.attack_amount);
    const auto tension = saturate(creature.tension);
    const auto graze = creature.behavior_state == CreatureBehaviorState::Graze ? 1.0F : 0.0F;
    const auto phase = seed_unit(creature.appearance_seed, 24) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (4.8F + motion * 3.8F) + phase);
    const auto breath = std::sin(creature.animation_time * 2.0F + phase * 0.3F) * 0.016F;
    const auto head_pitch = 0.01F + gaze * 0.04F - attack * 0.08F +
                            graze * (0.13F + std::sin(creature.animation_time * 2.8F + phase) * 0.022F);
    const auto head_yaw = std::sin(creature.animation_time * 1.4F + phase) * 0.04F + gaze * 0.03F;
    const auto front_swing = -stride * (0.10F + motion * 0.10F);
    const auto rear_swing = stride * (0.12F + motion * 0.12F);
    const auto scale = 0.60F + 0.40F * presence;
    const auto collapse = (1.0F - presence) * 0.12F;
    const auto torso_length = (0.42F + seed_detail_signed(creature.appearance_seed, 1) * 0.04F) * scale;
    const auto torso_depth = (0.23F + seed_detail_signed(creature.appearance_seed, 2) * 0.02F) * scale;
    const auto torso_height = (0.22F + seed_detail_unit(creature.appearance_seed, 3) * 0.05F) * scale;
    const auto chest_height = torso_height * 1.08F;
    const auto head_length = (0.20F + seed_detail_unit(creature.appearance_seed, 4) * 0.03F) * scale;
    const auto muzzle_length = (0.12F + seed_detail_unit(creature.appearance_seed, 5) * 0.02F) * scale;
    const auto horn_height = (0.11F + seed_detail_unit(creature.appearance_seed, 6) * 0.03F) * scale;
    const auto horn_curl = seed_detail_signed(creature.appearance_seed, 7) * 0.06F;
    const auto hoof_height = 0.040F * scale;

    append_box(mesh, root, glm::vec3 {-0.06F, 0.88F + breath - collapse * 0.30F, 0.0F},
               glm::vec3 {torso_length, torso_height, torso_depth},
               glm::vec3 {0.01F, 0.0F, 0.01F}, CreatureAtlasTile::CowHide,
               state.morph, tension, kMaterialHide, 0.16F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.22F, 0.92F + breath * 0.5F - collapse * 0.24F, 0.0F},
               glm::vec3 {torso_length * 0.34F, chest_height, torso_depth * 0.94F},
               glm::vec3 {-0.04F, 0.0F, 0.02F}, CreatureAtlasTile::CowHide,
               state.morph, tension, kMaterialHide, 0.18F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.42F, 0.91F + breath * 0.35F - collapse * 0.18F, 0.0F},
               glm::vec3 {0.105F * scale, 0.125F * scale, torso_depth * 0.58F},
               glm::vec3 {head_pitch * 0.22F, head_yaw * 0.18F, -0.025F}, CreatureAtlasTile::CowHide,
               state.morph, tension, kMaterialHide, 0.12F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.38F, 0.86F + breath * 0.4F - collapse * 0.24F, 0.0F},
               glm::vec3 {torso_length * 0.24F, torso_height * 0.92F, torso_depth},
               glm::vec3 {0.05F, 0.0F, -0.02F}, CreatureAtlasTile::CowHide,
               state.morph, tension, kMaterialHide, 0.16F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.04F, 0.61F - collapse * 0.35F, 0.0F},
               glm::vec3 {torso_length * 0.42F, torso_height * 0.30F, torso_depth * 0.78F},
               glm::vec3 {0.0F}, CreatureAtlasTile::CowMuzzle,
               state.morph, tension, kMaterialSkin, 0.10F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.52F, 0.94F + breath * 0.2F - collapse * 0.18F, 0.0F},
               glm::vec3 {head_length * 0.54F, head_length * 0.38F, head_length * 0.58F},
               glm::vec3 {head_pitch * 0.4F, head_yaw * 0.4F, -0.02F}, CreatureAtlasTile::CowHide,
               state.morph, tension, kMaterialHide, 0.14F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.74F, 0.88F - collapse * 0.15F, 0.0F},
               glm::vec3 {head_length, head_length * 0.60F, head_length * 0.62F},
               glm::vec3 {head_pitch, head_yaw, 0.0F}, CreatureAtlasTile::CowHide,
               state.morph, tension, kMaterialHide, 0.18F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.98F, 0.80F - collapse * 0.10F, 0.0F},
               glm::vec3 {muzzle_length, head_length * 0.26F, head_length * 0.44F},
               glm::vec3 {head_pitch * 0.35F, head_yaw, 0.0F}, CreatureAtlasTile::CowMuzzle,
               state.morph, tension, kMaterialSkin, 0.08F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.84F, 0.70F - collapse * 0.12F, 0.0F},
               glm::vec3 {head_length * 0.34F, head_length * 0.14F, head_length * 0.28F},
               glm::vec3 {head_pitch * 0.10F, head_yaw, -0.02F}, CreatureAtlasTile::CowMuzzle,
               state.morph, tension, kMaterialSkin, 0.12F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.87F, 0.94F - collapse * 0.08F, 0.0F}, head_length * 0.58F,
                glm::vec3 {0.016F * scale, 0.022F * scale, 0.008F * scale},
                glm::vec3 {0.0F}, glm::vec3 {0.0F},
                CreatureAtlasTile::CowHoof, state.morph, tension, kMaterialHorn, 0.02F, 0.0F);
    append_pair(mesh, root, glm::vec3 {1.10F, 0.80F - collapse * 0.08F, 0.0F}, head_length * 0.30F,
                glm::vec3 {0.014F * scale, 0.016F * scale, 0.007F * scale},
                glm::vec3 {0.0F}, glm::vec3 {0.0F},
                CreatureAtlasTile::CowHoof, state.morph, tension, kMaterialHorn, 0.02F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.76F, 1.13F - collapse * 0.12F, 0.0F}, head_length * 0.56F,
                glm::vec3 {head_length * 0.10F, head_length * 0.10F, head_length * 0.10F},
                glm::vec3 {0.02F, 0.0F, 0.10F}, glm::vec3 {0.0F, 0.0F, -0.04F},
                CreatureAtlasTile::CowHide, state.morph, tension, kMaterialSkin, 0.04F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.82F, 1.18F - collapse * 0.12F, 0.0F}, head_length * 0.48F,
                glm::vec3 {head_length * 0.10F, horn_height, head_length * 0.10F},
                glm::vec3 {-0.12F, 0.0F, -0.18F - horn_curl}, glm::vec3 {0.0F, 0.0F, -0.10F},
                CreatureAtlasTile::CowHorn, state.morph, tension, kMaterialHorn, 0.06F, 0.0F);

    for (const auto side : kSides) {
        const auto side_phase = side > 0.0F ? 1.0F : -1.0F;
        append_quadruped_leg(mesh,
                             root,
                             0.22F,
                             0.92F + breath * 0.5F - collapse * 0.24F - chest_height + 0.030F,
                             side * 0.16F,
                             front_swing * side_phase * 0.42F,
                             -0.028F * scale,
                             glm::vec2 {0.054F * scale, 0.050F * scale},
                             glm::vec2 {0.048F * scale, 0.044F * scale},
                             glm::vec3 {0.066F * scale, hoof_height * 0.58F, 0.054F * scale},
                             CreatureAtlasTile::CowHide,
                             CreatureAtlasTile::CowHide,
                             CreatureAtlasTile::CowHoof,
                             state.morph,
                             tension,
                             kMaterialSkin,
                             kMaterialHorn);
        append_quadruped_leg(mesh,
                             root,
                             -0.34F,
                             0.86F + breath * 0.4F - collapse * 0.24F - torso_height * 0.92F + 0.030F,
                             side * 0.17F,
                             rear_swing * side_phase * 0.42F,
                             0.026F * scale,
                             glm::vec2 {0.058F * scale, 0.052F * scale},
                             glm::vec2 {0.050F * scale, 0.044F * scale},
                             glm::vec3 {0.068F * scale, hoof_height * 0.58F, 0.056F * scale},
                             CreatureAtlasTile::CowHide,
                             CreatureAtlasTile::CowHide,
                             CreatureAtlasTile::CowHoof,
                             state.morph,
                             tension,
                             kMaterialSkin,
                             kMaterialHorn);
    }

    append_box(mesh, root, glm::vec3 {-0.04F, 0.50F - collapse * 0.28F, 0.0F},
               glm::vec3 {0.070F * scale, 0.060F * scale, 0.080F * scale},
               glm::vec3 {0.0F}, CreatureAtlasTile::CowMuzzle,
               state.morph, tension, kMaterialSkin, 0.06F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.56F, 0.98F - collapse * 0.18F, 0.0F},
               glm::vec3 {0.022F * scale, 0.18F * scale, 0.022F * scale},
               glm::vec3 {0.0F, 0.0F, 0.12F + stride * 0.05F}, CreatureAtlasTile::CowHide,
               state.morph, tension, kMaterialSkin, 0.04F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.62F, 0.82F - collapse * 0.16F, 0.0F},
               glm::vec3 {0.028F * scale, 0.050F * scale, 0.028F * scale},
               glm::vec3 {0.0F, 0.0F, -0.16F - stride * 0.04F}, CreatureAtlasTile::CowHide,
               state.morph, tension, kMaterialSkin, 0.04F, 0.0F);
}

void append_day_sheep(std::vector<CreaturePartInstance>& mesh,
                      const CreatureRenderInstance& creature,
                      const CreatureVisualState& state,
                      const glm::mat4& root) {
    const auto presence = state.day_presence;
    if (presence <= 1.0e-4F) {
        return;
    }

    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto attack = saturate(creature.attack_amount);
    const auto tension = saturate(creature.tension);
    const auto graze = creature.behavior_state == CreatureBehaviorState::Graze ? 1.0F : 0.0F;
    const auto phase = seed_unit(creature.appearance_seed, 24) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (4.4F + motion * 3.6F) + phase);
    const auto breath = std::sin(creature.animation_time * 2.2F + phase * 0.5F) * 0.014F;
    const auto head_pitch = 0.05F + gaze * 0.05F - attack * 0.06F +
                            graze * (0.16F + std::sin(creature.animation_time * 3.1F + phase) * 0.024F);
    const auto head_yaw = std::sin(creature.animation_time * 1.5F + phase) * 0.04F + gaze * 0.03F;
    const auto front_swing = -stride * (0.10F + motion * 0.08F);
    const auto rear_swing = stride * (0.12F + motion * 0.10F);
    const auto scale = 0.62F + 0.38F * presence;
    const auto collapse = (1.0F - presence) * 0.10F;
    const auto body_length = (0.34F + seed_detail_signed(creature.appearance_seed, 1) * 0.03F) * scale;
    const auto body_depth = (0.20F + seed_detail_signed(creature.appearance_seed, 2) * 0.02F) * scale;
    const auto body_height = (0.18F + seed_detail_unit(creature.appearance_seed, 3) * 0.04F) * scale;
    const auto fleece_shell = (0.10F + seed_detail_unit(creature.appearance_seed, 4) * 0.03F) * scale;
    const auto head_length = (0.15F + seed_detail_unit(creature.appearance_seed, 5) * 0.02F) * scale;
    const auto muzzle_length = (0.08F + seed_detail_unit(creature.appearance_seed, 6) * 0.02F) * scale;
    const auto hoof_height = 0.032F * scale;

    append_box(mesh, root, glm::vec3 {-0.05F, 0.74F + breath - collapse * 0.28F, 0.0F},
               glm::vec3 {body_length, body_height, body_depth},
               glm::vec3 {0.0F}, CreatureAtlasTile::SheepFace,
               state.morph, tension, kMaterialSkin, 0.16F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.05F, 0.82F + breath * 0.5F - collapse * 0.20F, 0.0F},
               glm::vec3 {body_length + fleece_shell, body_height + fleece_shell * 0.72F, body_depth + fleece_shell},
               glm::vec3 {0.0F, 0.0F, 0.02F}, CreatureAtlasTile::SheepWool,
               state.morph, tension, kMaterialWool, 0.10F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.16F, 0.84F + breath * 0.3F - collapse * 0.16F, 0.0F},
               glm::vec3 {body_length * 0.36F, body_height + fleece_shell * 0.62F, body_depth + fleece_shell * 0.78F},
               glm::vec3 {-0.05F, 0.0F, 0.03F}, CreatureAtlasTile::SheepShadow,
               state.morph, tension, kMaterialWool, 0.14F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.32F, 0.82F + breath * 0.3F - collapse * 0.12F, 0.0F},
               glm::vec3 {0.090F * scale, 0.115F * scale, body_depth * 0.66F},
               glm::vec3 {head_pitch * 0.20F, head_yaw * 0.18F, -0.020F}, CreatureAtlasTile::SheepShadow,
               state.morph, tension, kMaterialWool, 0.12F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.28F, 0.83F + breath * 0.3F - collapse * 0.16F, 0.0F},
               glm::vec3 {body_length * 0.24F, body_height + fleece_shell * 0.58F, body_depth + fleece_shell * 0.82F},
               glm::vec3 {0.05F, 0.0F, -0.02F}, CreatureAtlasTile::SheepShadow,
               state.morph, tension, kMaterialWool, 0.14F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.46F, 0.80F + breath * 0.2F - collapse * 0.12F, 0.0F},
               glm::vec3 {head_length * 0.70F, head_length * 0.44F, head_length * 0.54F},
               glm::vec3 {head_pitch * 0.6F, head_yaw * 0.5F, -0.03F}, CreatureAtlasTile::SheepFace,
               state.morph, tension, kMaterialSkin, 0.14F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.40F, 0.90F + breath * 0.2F - collapse * 0.10F, 0.0F},
               glm::vec3 {head_length * 0.96F, head_length * 0.58F, head_length * 0.72F},
               glm::vec3 {head_pitch * 0.35F, head_yaw, 0.0F}, CreatureAtlasTile::SheepWool,
               state.morph, tension, kMaterialWool, 0.10F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.62F, 0.73F - collapse * 0.08F, 0.0F},
               glm::vec3 {muzzle_length, head_length * 0.18F, head_length * 0.26F},
               glm::vec3 {head_pitch * 0.35F, head_yaw, 0.0F}, CreatureAtlasTile::SheepFace,
               state.morph, tension, kMaterialSkin, 0.10F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.56F, 0.81F - collapse * 0.08F, 0.0F}, head_length * 0.48F,
                glm::vec3 {0.012F * scale, 0.018F * scale, 0.006F * scale},
                glm::vec3 {0.0F}, glm::vec3 {0.0F},
                CreatureAtlasTile::SheepHoof, state.morph, tension, kMaterialHorn, 0.02F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.72F, 0.71F - collapse * 0.08F, 0.0F}, head_length * 0.22F,
                glm::vec3 {0.010F * scale, 0.012F * scale, 0.005F * scale},
                glm::vec3 {0.0F}, glm::vec3 {0.0F},
                CreatureAtlasTile::SheepHoof, state.morph, tension, kMaterialHorn, 0.02F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.45F, 0.88F - collapse * 0.08F, 0.0F}, head_length * 0.50F,
                glm::vec3 {head_length * 0.16F, head_length * 0.08F, head_length * 0.10F},
                glm::vec3 {0.02F, 0.0F, 0.10F}, glm::vec3 {0.0F, 0.0F, -0.04F},
                CreatureAtlasTile::SheepFace, state.morph, tension, kMaterialSkin, 0.05F, 0.0F);

    for (const auto side : kSides) {
        const auto side_phase = side > 0.0F ? 1.0F : -1.0F;
        append_quadruped_leg(mesh,
                             root,
                             0.16F,
                             0.84F + breath * 0.3F - collapse * 0.16F - (body_height + fleece_shell * 0.62F) + 0.026F,
                             side * 0.14F,
                             front_swing * side_phase * 0.38F,
                             -0.018F * scale,
                             glm::vec2 {0.044F * scale, 0.042F * scale},
                             glm::vec2 {0.040F * scale, 0.038F * scale},
                             glm::vec3 {0.050F * scale, hoof_height * 0.56F, 0.044F * scale},
                             CreatureAtlasTile::SheepFace,
                             CreatureAtlasTile::SheepFace,
                             CreatureAtlasTile::SheepHoof,
                             state.morph,
                             tension,
                             kMaterialSkin,
                             kMaterialHorn);
        append_quadruped_leg(mesh,
                             root,
                             -0.24F,
                             0.83F + breath * 0.3F - collapse * 0.16F - (body_height + fleece_shell * 0.58F) + 0.026F,
                             side * 0.15F,
                             rear_swing * side_phase * 0.38F,
                             0.018F * scale,
                             glm::vec2 {0.046F * scale, 0.042F * scale},
                             glm::vec2 {0.040F * scale, 0.038F * scale},
                             glm::vec3 {0.052F * scale, hoof_height * 0.56F, 0.044F * scale},
                             CreatureAtlasTile::SheepFace,
                             CreatureAtlasTile::SheepFace,
                             CreatureAtlasTile::SheepHoof,
                             state.morph,
                             tension,
                             kMaterialSkin,
                             kMaterialHorn);
    }

    append_box(mesh, root, glm::vec3 {-0.45F, 0.88F - collapse * 0.16F, 0.0F},
               glm::vec3 {0.030F * scale, 0.050F * scale, 0.030F * scale},
               glm::vec3 {0.0F, 0.0F, 0.10F + stride * 0.04F}, CreatureAtlasTile::SheepWool,
               state.morph, tension, kMaterialWool, 0.04F, 0.0F);
}

void append_hanging_strand(std::vector<CreaturePartInstance>& mesh,
                           const glm::mat4& root,
                           const glm::vec3& top,
                           float length,
                           float drift_x,
                           float half_width,
                           CreatureAtlasTile tile,
                           float nightmare_factor,
                           float tension,
                           float material_class,
                           float emissive_strength) {
    const glm::vec2 strand_top {top.x, top.y};
    const glm::vec2 strand_bottom {top.x + drift_x, std::max(top.y - length, 0.035F)};
    append_limb_segment(mesh,
                        root,
                        strand_top,
                        strand_bottom,
                        top.z,
                        half_width,
                        half_width * 0.82F,
                        0.006F,
                        tile,
                        nightmare_factor,
                        tension,
                        material_class,
                        0.76F,
                        emissive_strength);
}

void append_night_stalker(std::vector<CreaturePartInstance>& mesh,
                          const CreatureRenderInstance& creature,
                          const CreatureVisualState& state,
                          const glm::mat4& base_root) {
    const auto presence = state.night_presence;
    if (presence <= 1.0e-4F) {
        return;
    }

    const auto root = glm::scale(base_root, glm::vec3 {1.08F, 1.13F, 1.08F});
    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto attack = saturate(creature.attack_amount);
    const auto tension = saturate(creature.tension);
    const auto phase = seed_unit(creature.appearance_seed, 20) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (3.2F + motion * 3.8F) + phase);
    const auto twitch = std::sin(creature.animation_time * 9.4F + phase * 0.7F) * tension;
    const auto breath = std::sin(creature.animation_time * 1.7F + phase * 0.3F) * 0.028F * presence;
    const auto pulse = (0.88F + 0.12F * std::sin(creature.animation_time * 9.7F + phase)) * state.corruption;
    const auto scale = 0.70F + 0.30F * presence;
    const auto lean = -0.10F - gaze * 0.08F - attack * 0.10F + twitch * 0.018F;
    const auto spine_sway = std::sin(creature.animation_time * 2.1F + phase) * 0.035F * (0.35F + tension * 0.65F);
    const auto head_yaw = std::sin(creature.animation_time * 1.4F + phase) * 0.055F + gaze * 0.060F;
    const auto head_roll = std::sin(creature.animation_time * 5.6F + phase) * 0.055F + twitch * 0.025F;
    const auto arm_drag = stride * (0.10F + motion * 0.08F) - attack * 0.12F;
    const auto leg_swing = stride * (0.11F + motion * 0.08F);
    const auto asymmetry = seed_detail_signed(creature.appearance_seed, 7) * 0.045F;

    append_box(mesh, root, glm::vec3 {-0.02F, 1.46F + breath * 0.35F, 0.0F},
               glm::vec3 {0.18F * scale, 0.16F * scale, 0.19F * scale},
               glm::vec3 {0.0F, 0.0F, 0.11F + spine_sway}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.54F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.02F, 1.72F + breath, 0.0F},
               glm::vec3 {0.070F * scale, 0.30F * scale, 0.060F * scale},
               glm::vec3 {0.0F, 0.0F, 0.08F + spine_sway}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.70F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.07F, 2.16F + breath, 0.0F},
               glm::vec3 {0.060F * scale, 0.34F * scale, 0.055F * scale},
               glm::vec3 {0.0F, 0.0F, lean * 0.35F + spine_sway}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.78F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.13F, 2.61F + breath, 0.0F},
               glm::vec3 {0.075F * scale, 0.36F * scale, 0.060F * scale},
               glm::vec3 {0.0F, 0.0F, lean * 0.65F + spine_sway}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.80F, 0.0F);

    for (int rib = 0; rib < 5; ++rib) {
        const auto rib_index = static_cast<float>(rib);
        const auto rib_y = 2.10F + rib_index * 0.16F + breath;
        const auto rib_x = 0.03F + rib_index * 0.030F;
        const auto rib_span = 0.23F + rib_index * 0.018F;
        append_pair(mesh, root, glm::vec3 {rib_x, rib_y, 0.0F}, rib_span,
                    glm::vec3 {(0.145F - rib_index * 0.006F) * scale, 0.020F * scale, 0.034F * scale},
                    glm::vec3 {0.0F, 0.0F, -0.10F - rib_index * 0.025F + spine_sway},
                    glm::vec3 {0.0F, 0.0F, 0.14F + rib_index * 0.018F},
                    CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.82F, 0.0F);
    }

    append_pair(mesh, root, glm::vec3 {0.12F, 2.84F + breath, 0.0F}, 0.31F,
                glm::vec3 {0.16F * scale, 0.042F * scale, 0.052F * scale},
                glm::vec3 {0.0F, 0.0F, -0.12F + spine_sway}, glm::vec3 {0.0F, 0.0F, 0.08F},
                CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.72F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.20F, 3.15F + breath, 0.0F},
               glm::vec3 {0.055F * scale, 0.25F * scale, 0.050F * scale},
               glm::vec3 {0.0F, 0.0F, lean + spine_sway}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.80F, 0.0F);

    append_box(mesh, root, glm::vec3 {0.34F, 3.74F + breath * 0.55F, 0.0F},
               glm::vec3 {0.29F * scale, 0.34F * scale, 0.29F * scale},
               glm::vec3 {-0.05F + twitch * 0.018F, head_yaw, -0.16F + head_roll},
               CreatureAtlasTile::ZombieFlesh, presence, tension, kMaterialZombieBone, 0.64F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.43F, 3.42F + breath * 0.35F, 0.0F},
               glm::vec3 {0.18F * scale, 0.11F * scale, 0.22F * scale},
               glm::vec3 {0.02F, head_yaw, -0.12F + head_roll * 0.45F},
               CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.58F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.63F, 3.82F + breath * 0.45F, 0.0F}, 0.105F,
                glm::vec3 {0.032F * scale, 0.055F * scale, 0.030F * scale},
                glm::vec3 {-0.03F, head_yaw, -0.12F + head_roll * 0.20F}, glm::vec3 {0.0F, 0.0F, 0.025F},
                CreatureAtlasTile::ZombieEye, presence, tension, kMaterialZombieGlow, 0.98F, pulse);
    append_box(mesh, root, glm::vec3 {0.64F, 3.64F + breath * 0.40F, 0.0F},
               glm::vec3 {0.030F * scale, 0.046F * scale, 0.035F * scale},
               glm::vec3 {-0.03F, head_yaw, -0.10F + head_roll * 0.16F},
               CreatureAtlasTile::TransformGlow, presence, tension, kMaterialZombieGlow, 0.96F, pulse * 0.92F);

    const std::array<glm::vec3, 12> head_chips {{
        {0.18F, 4.03F, -0.18F}, {0.33F, 4.09F, -0.03F}, {0.22F, 4.00F, 0.18F},
        {0.09F, 3.84F, -0.26F}, {0.08F, 3.68F, 0.25F}, {0.21F, 3.48F, -0.27F},
        {0.36F, 3.49F, 0.27F}, {0.47F, 3.96F, -0.22F}, {0.50F, 3.78F, 0.23F},
        {0.15F, 3.95F, 0.03F}, {0.31F, 3.33F, -0.08F}, {0.30F, 3.35F, 0.10F},
    }};
    for (std::size_t index = 0; index < head_chips.size(); ++index) {
        const auto salt = static_cast<int>(index) + 61;
        const auto chip_size = (0.052F + seed_detail_unit(creature.appearance_seed, salt) * 0.036F) * scale;
        append_box(mesh,
                   root,
                   head_chips[index] + glm::vec3 {
                       seed_detail_signed(creature.appearance_seed, salt + 13) * 0.018F,
                       breath * 0.35F + seed_detail_signed(creature.appearance_seed, salt + 19) * 0.018F,
                       seed_detail_signed(creature.appearance_seed, salt + 29) * 0.016F,
                   },
                   glm::vec3 {chip_size * 1.18F, chip_size, chip_size * 1.10F},
                   glm::vec3 {
                       seed_detail_signed(creature.appearance_seed, salt + 31) * 0.24F,
                       head_yaw * 0.35F + seed_detail_signed(creature.appearance_seed, salt + 37) * 0.26F,
                       head_roll * 0.30F + seed_detail_signed(creature.appearance_seed, salt + 41) * 0.22F,
                   },
                   index % 3 == 0 ? CreatureAtlasTile::ZombieBone : CreatureAtlasTile::ZombieFlesh,
                   presence,
                   tension,
                   kMaterialZombieBone,
                   0.70F,
                   0.0F);
    }

    for (const auto side : kSides) {
        const auto side_bias = side * asymmetry;
        const auto shoulder_z = side * 0.36F;
        const auto elbow_z = side * (0.47F + side_bias);
        const auto wrist_z = side * (0.52F + side_bias);
        const glm::vec2 shoulder {0.06F + side_bias * 0.20F, 2.72F + breath};
        const glm::vec2 elbow {-0.18F + arm_drag * 0.08F - side_bias * 0.25F, 1.58F + breath * 0.35F};
        const glm::vec2 wrist {0.10F + attack * 0.16F + arm_drag * 0.10F, 0.52F + std::abs(stride) * 0.035F};

        append_box(mesh, root, glm::vec3 {shoulder.x, shoulder.y, shoulder_z},
                   glm::vec3 {0.085F * scale, 0.115F * scale, 0.075F * scale},
                   glm::vec3 {0.0F, 0.0F, -0.22F * side + spine_sway},
                   CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.62F, 0.0F);
        append_limb_segment(mesh, root, shoulder, elbow, elbow_z, 0.044F * scale, 0.040F * scale, 0.020F,
                            CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.66F, 0.0F);
        append_limb_segment(mesh, root, elbow, wrist, wrist_z, 0.036F * scale, 0.034F * scale, 0.018F,
                            CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.70F, 0.0F);
        append_box(mesh, root, glm::vec3 {wrist.x, wrist.y - 0.06F, wrist_z},
                   glm::vec3 {0.075F * scale, 0.082F * scale, 0.055F * scale},
                   glm::vec3 {0.0F, 0.0F, -0.12F + attack * 0.10F},
                   CreatureAtlasTile::ZombieClaw, presence, tension, kMaterialKeratin, 0.48F, 0.0F);

        for (int finger = 0; finger < 4; ++finger) {
            const auto finger_index = static_cast<float>(finger);
            const auto finger_z = wrist_z + side * (-0.052F + finger_index * 0.035F);
            const auto finger_top = glm::vec3 {
                wrist.x + 0.025F * finger_index,
                wrist.y - 0.13F - finger_index * 0.010F,
                finger_z,
            };
            append_hanging_strand(mesh,
                                  root,
                                  finger_top,
                                  0.36F + finger_index * 0.045F + attack * 0.08F,
                                  0.020F + finger_index * 0.010F,
                                  0.014F * scale,
                                  CreatureAtlasTile::ZombieClaw,
                                  presence,
                                  tension,
                                  kMaterialKeratin,
                                  0.0F);
        }
    }

    for (const auto side : kSides) {
        const auto knee_z = side * (0.17F + asymmetry * 0.25F);
        const auto ankle_z = side * 0.15F;
        const glm::vec2 hip {-0.03F + side * asymmetry * 0.20F, 1.42F + breath * 0.25F};
        const glm::vec2 knee {-0.13F - leg_swing * side * 0.22F, 0.72F + std::abs(stride) * 0.026F};
        const glm::vec2 ankle {0.04F + leg_swing * side * 0.14F, 0.15F};

        append_limb_segment(mesh, root, hip, knee, knee_z, 0.050F * scale, 0.044F * scale, 0.018F,
                            CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.62F, 0.0F);
        append_limb_segment(mesh, root, knee, ankle, ankle_z, 0.040F * scale, 0.036F * scale, 0.018F,
                            CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.66F, 0.0F);
        append_box(mesh, root, glm::vec3 {0.21F + leg_swing * side * 0.10F, 0.055F, side * 0.15F},
                   glm::vec3 {0.22F * scale, 0.048F * scale, 0.070F * scale},
                   glm::vec3 {0.0F, 0.0F, 0.035F + leg_swing * 0.20F},
                   CreatureAtlasTile::ZombieClaw, presence, tension, kMaterialKeratin, 0.42F, 0.0F);
    }

    for (int spike = 0; spike < 6; ++spike) {
        const auto spike_index = static_cast<float>(spike);
        append_box(mesh, root,
                   glm::vec3 {-0.10F - spike_index * 0.018F, 2.18F + spike_index * 0.22F + breath, 0.0F},
                   glm::vec3 {0.030F * scale, (0.10F + spike_index * 0.012F) * scale, 0.032F * scale},
                   glm::vec3 {0.0F, 0.0F, 0.30F + spike_index * 0.05F},
                   CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.76F, 0.0F);
    }

    const std::array<glm::vec3, 13> strand_roots {{
        {0.18F, 3.45F, -0.18F}, {0.33F, 3.40F, -0.05F}, {0.45F, 3.38F, 0.12F},
        {0.07F, 2.72F, -0.33F}, {0.14F, 2.55F, 0.34F}, {-0.03F, 2.30F, -0.26F},
        {0.19F, 2.22F, 0.27F}, {-0.02F, 1.80F, -0.22F}, {0.05F, 1.74F, 0.24F},
        {0.25F, 1.18F, -0.46F}, {0.16F, 1.06F, 0.48F}, {-0.08F, 1.54F, -0.08F},
        {-0.02F, 1.50F, 0.10F},
    }};
    for (std::size_t index = 0; index < strand_roots.size(); ++index) {
        const auto salt = static_cast<int>(index) + 17;
        append_hanging_strand(mesh,
                              root,
                              strand_roots[index] + glm::vec3 {seed_detail_signed(creature.appearance_seed, salt) * 0.030F, breath, 0.0F},
                              0.58F + seed_detail_unit(creature.appearance_seed, salt + 11) * 0.64F,
                              seed_detail_signed(creature.appearance_seed, salt + 23) * 0.070F,
                              (0.010F + seed_detail_unit(creature.appearance_seed, salt + 31) * 0.009F) * scale,
                              index < 3 ? CreatureAtlasTile::ZombieClaw : CreatureAtlasTile::ZombieBone,
                              presence,
                              tension,
                              index < 3 ? kMaterialKeratin : kMaterialZombieBone,
                              0.0F);
    }
}

void append_night_pig(std::vector<CreaturePartInstance>& mesh,
                      const CreatureRenderInstance& creature,
                      const CreatureVisualState& state,
                      const glm::mat4& root) {
    append_night_stalker(mesh, creature, state, root);
}

void append_night_cow(std::vector<CreaturePartInstance>& mesh,
                      const CreatureRenderInstance& creature,
                      const CreatureVisualState& state,
                      const glm::mat4& root) {
    append_night_stalker(mesh, creature, state, root);
}

void append_night_sheep(std::vector<CreaturePartInstance>& mesh,
                        const CreatureRenderInstance& creature,
                        const CreatureVisualState& state,
                        const glm::mat4& root) {
    append_night_stalker(mesh, creature, state, root);
}

void append_day_villager(std::vector<CreaturePartInstance>& mesh,
                         const CreatureRenderInstance& creature,
                         const CreatureVisualState& state,
                         const glm::mat4& root) {
    const auto presence = state.day_presence;
    if (presence <= 1.0e-4F) {
        return;
    }

    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto tension = saturate(creature.tension);
    const auto activity = saturate(creature.attack_amount);
    const auto work_pose = creature.behavior_state == CreatureBehaviorState::Work ? 1.0F : 0.0F;
    const auto graze_pose = creature.behavior_state == CreatureBehaviorState::Graze ? 1.0F : 0.0F;
    const auto social_pose = creature.behavior_state == CreatureBehaviorState::Socialize ? 1.0F : 0.0F;
    const auto sleep_pose = creature.behavior_state == CreatureBehaviorState::Sleep ? 1.0F : 0.0F;
    const auto return_home_pose = creature.behavior_state == CreatureBehaviorState::ReturnHome ? 1.0F : 0.0F;
    const auto phase = seed_unit(creature.appearance_seed, 22) * kTwoPi;
    const auto stride_wave = std::sin(creature.animation_time * (4.8F + motion * 3.6F) + phase);
    const auto stride = stride_wave * motion;
    const auto work_wave = std::sin(creature.animation_time * (5.4F + activity * 2.2F) + phase * 0.7F);
    const auto social_wave = std::sin(creature.animation_time * 3.8F + phase * 1.3F);
    const auto sleep_breath = std::sin(creature.animation_time * 1.15F + phase * 0.20F);
    const auto breath = std::sin(creature.animation_time * 2.0F + phase * 0.35F) * (0.018F - sleep_pose * 0.008F);
    const auto body_bob = motion * 0.012F * std::sin(creature.animation_time * 8.0F + phase) +
                          return_home_pose * motion * 0.010F;
    const auto head_pitch = 0.012F + gaze * 0.045F + work_pose * (0.065F + work_wave * 0.040F) +
                            graze_pose * (0.135F + work_wave * 0.035F) + sleep_pose * (0.155F + sleep_breath * 0.025F);
    const auto head_yaw = std::sin(creature.animation_time * 1.2F + phase) * 0.025F + gaze * 0.070F +
                          social_pose * social_wave * 0.105F;
    const auto arm_swing = stride * (0.10F + motion * 0.08F) + return_home_pose * stride * 0.055F;
    const auto folded_sway = stride * motion * 0.035F + social_pose * social_wave * 0.018F;
    const auto scale = 0.98F + seed_detail_signed(creature.appearance_seed, 1) * 0.035F;
    const auto hip_span = 0.108F * scale;
    const auto torso_half = glm::vec3 {
        (0.155F + seed_detail_unit(creature.appearance_seed, 2) * 0.010F) * scale,
        (0.365F + seed_detail_unit(creature.appearance_seed, 3) * 0.025F) * scale,
        (0.112F + seed_detail_unit(creature.appearance_seed, 4) * 0.012F) * scale,
    };
    const auto head_half = glm::vec3 {
        (0.180F + seed_detail_unit(creature.appearance_seed, 5) * 0.012F) * scale,
        (0.200F + seed_detail_unit(creature.appearance_seed, 6) * 0.012F) * scale,
        (0.180F + seed_detail_unit(creature.appearance_seed, 7) * 0.012F) * scale,
    };
    const auto neck_half = glm::vec3 {0.070F * scale, 0.066F * scale, 0.070F * scale};
    const auto robe_half = glm::vec3 {torso_half.x * 1.06F, 0.292F * scale, torso_half.z * 1.16F};
    const auto front_apron_half = glm::vec3 {0.028F * scale, torso_half.y * 0.88F, torso_half.z * 0.86F};
    const auto belt_half = glm::vec3 {torso_half.x * 1.09F, 0.026F * scale, torso_half.z * 1.20F};
    const auto hem_half = glm::vec3 {robe_half.x * 1.02F, 0.026F * scale, robe_half.z * 1.04F};
    const auto hair_half = glm::vec3 {head_half.x * 1.04F, 0.055F * scale, head_half.z * 1.04F};
    const auto brow_half = glm::vec3 {0.014F * scale, 0.022F * scale, head_half.z * 0.72F};
    const auto nose_half = glm::vec3 {0.070F * scale, 0.075F * scale, 0.046F * scale};
    const auto upper_arm_half = glm::vec3 {0.058F * scale, 0.225F * scale, 0.058F * scale};
    const auto folded_arm_half = glm::vec3 {0.050F * scale, 0.055F * scale, 0.210F * scale};
    const auto hand_half = glm::vec3 {0.038F * scale, 0.052F * scale, 0.050F * scale};
    const auto upper_leg_half_xz = glm::vec2 {0.061F * scale, 0.058F * scale};
    const auto lower_leg_half_xz = glm::vec2 {0.056F * scale, 0.054F * scale};
    const auto foot_half = glm::vec3 {0.086F * scale, 0.048F * scale, 0.060F * scale};
    const auto shoulder_span = torso_half.z + upper_arm_half.z * 0.78F;

    append_box(mesh, root, glm::vec3 {0.0F, 1.12F + breath + body_bob, 0.0F},
               torso_half, glm::vec3 {0.01F, 0.0F, 0.02F}, CreatureAtlasTile::VillagerCloth,
               state.morph, tension, kMaterialHide, 0.14F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.02F, 0.70F + breath + body_bob, 0.0F},
               robe_half, glm::vec3 {0.0F, 0.0F, 0.0F}, CreatureAtlasTile::VillagerCloth,
               state.morph, tension, kMaterialHide, 0.12F, 0.0F);
    append_box(mesh, root, glm::vec3 {torso_half.x + front_apron_half.x * 0.80F, 1.08F + breath + body_bob, 0.0F},
               front_apron_half, glm::vec3 {0.0F, 0.0F, 0.0F}, CreatureAtlasTile::VillagerApron,
               state.morph, tension, kMaterialTransitionHide, 0.06F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.0F, 0.98F + breath + body_bob, 0.0F},
               belt_half, glm::vec3 {0.0F, 0.0F, 0.0F}, CreatureAtlasTile::VillagerApron,
               state.morph, tension, kMaterialTransitionHide, 0.08F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.02F, 0.405F + body_bob, 0.0F},
               hem_half, glm::vec3 {0.0F, 0.0F, 0.0F}, CreatureAtlasTile::VillagerApron,
               state.morph, tension, kMaterialTransitionHide, 0.06F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.03F * scale, 1.515F + breath * 0.28F + body_bob, 0.0F},
               neck_half, glm::vec3 {0.0F, head_yaw * 0.3F, 0.0F}, CreatureAtlasTile::VillagerSkin,
               state.morph, tension, kMaterialSkin, 0.08F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.05F * scale, 1.72F + breath * 0.35F + body_bob, 0.0F},
               head_half, glm::vec3 {head_pitch, head_yaw, 0.0F}, CreatureAtlasTile::VillagerSkin,
               state.morph, tension, kMaterialSkin, 0.12F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.05F * scale, 1.95F + body_bob, 0.0F},
               hair_half, glm::vec3 {head_pitch * 0.18F, head_yaw, 0.0F}, CreatureAtlasTile::VillagerHair,
               state.morph, tension, kMaterialKeratin, 0.08F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.05F * scale + head_half.x + brow_half.x * 0.95F, 1.82F + body_bob, 0.0F},
               brow_half, glm::vec3 {head_pitch * 0.18F, head_yaw, 0.0F}, CreatureAtlasTile::VillagerHair,
               state.morph, tension, kMaterialKeratin, 0.10F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.05F * scale + head_half.x + nose_half.x * 0.68F, 1.68F + body_bob, 0.0F},
               nose_half, glm::vec3 {head_pitch * 0.7F, head_yaw, 0.0F}, CreatureAtlasTile::VillagerSkin,
               state.morph, tension, kMaterialSkin, 0.04F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.05F * scale + head_half.x + 0.016F * scale, 1.77F + body_bob, 0.0F}, 0.082F * scale,
                glm::vec3 {0.016F * scale, 0.032F * scale, 0.013F * scale},
                glm::vec3 {head_pitch * 0.3F, head_yaw, 0.0F}, glm::vec3 {0.0F, 0.0F, 0.01F},
                CreatureAtlasTile::VillagerEye, state.morph, tension, kMaterialSkin, 0.88F, 0.0F);

    append_pair(mesh, root, glm::vec3 {0.0F, 1.17F + body_bob - sleep_pose * 0.035F, 0.0F}, shoulder_span,
                upper_arm_half,
                glm::vec3 {0.0F, 0.0F, -0.05F + arm_swing - sleep_pose * 0.06F}, glm::vec3 {0.0F, 0.0F, 0.025F},
                CreatureAtlasTile::VillagerCloth, state.morph, tension, kMaterialHide, 0.12F, 0.0F);

    if (work_pose > 0.5F || graze_pose > 0.5F) {
        const auto task_pose = std::max(work_pose, graze_pose);
        const auto work_reach = (0.115F + activity * 0.035F + work_wave * 0.018F) * scale;
        const auto work_height = (graze_pose > 0.5F ? 0.96F : 1.16F) + body_bob + work_wave * 0.018F;
        append_pair(mesh, root, glm::vec3 {torso_half.x + work_reach, work_height, 0.0F}, 0.116F * scale,
                    glm::vec3 {0.050F * scale, 0.168F * scale, 0.052F * scale},
                    glm::vec3 {0.0F, 0.0F, -0.42F - work_wave * 0.10F - graze_pose * 0.22F},
                    glm::vec3 {0.0F, 0.0F, 0.08F},
                    CreatureAtlasTile::VillagerCloth, state.morph, tension, kMaterialHide, 0.12F, 0.0F);
        append_pair(mesh, root, glm::vec3 {torso_half.x + work_reach + 0.075F * scale, work_height - 0.142F * scale, 0.0F},
                    0.104F * scale,
                    glm::vec3 {hand_half.x, hand_half.y * (1.0F + task_pose * 0.12F), hand_half.z},
                    glm::vec3 {0.0F, 0.0F, -0.20F - work_wave * 0.10F}, glm::vec3 {0.0F, 0.0F, 0.05F},
                    CreatureAtlasTile::VillagerSkin, state.morph, tension, kMaterialSkin, 0.10F, 0.0F);
        if (work_pose > 0.5F) {
            append_box(mesh, root, glm::vec3 {torso_half.x + 0.265F * scale, work_height - 0.20F * scale, 0.0F},
                       glm::vec3 {0.028F * scale, 0.145F * scale, 0.030F * scale},
                       glm::vec3 {0.0F, 0.0F, 0.62F + work_wave * 0.20F}, CreatureAtlasTile::VillagerHair,
                       state.morph, tension, kMaterialKeratin, 0.08F, 0.0F);
        }
    } else if (social_pose > 0.5F) {
        append_box(mesh, root, glm::vec3 {torso_half.x + folded_arm_half.x * 0.82F, 1.18F + body_bob, 0.060F * scale + folded_sway},
                   folded_arm_half, glm::vec3 {0.0F, 0.0F, 0.12F + social_wave * 0.04F}, CreatureAtlasTile::VillagerCloth,
                   state.morph, tension, kMaterialHide, 0.12F, 0.0F);
        append_box(mesh, root, glm::vec3 {torso_half.x + 0.115F * scale, 1.31F + body_bob + social_wave * 0.030F, -0.122F * scale},
                   glm::vec3 {0.052F * scale, 0.190F * scale, 0.052F * scale},
                   glm::vec3 {0.0F, 0.0F, -0.70F + social_wave * 0.12F}, CreatureAtlasTile::VillagerCloth,
                   state.morph, tension, kMaterialHide, 0.12F, 0.0F);
        append_box(mesh, root, glm::vec3 {torso_half.x + 0.210F * scale, 1.44F + body_bob + social_wave * 0.040F, -0.128F * scale},
                   hand_half, glm::vec3 {0.0F, 0.0F, -0.42F + social_wave * 0.18F}, CreatureAtlasTile::VillagerSkin,
                   state.morph, tension, kMaterialSkin, 0.10F, 0.0F);
        append_box(mesh, root, glm::vec3 {torso_half.x + 0.105F * scale, 1.12F + body_bob, 0.104F * scale},
                   hand_half, glm::vec3 {0.0F, 0.0F, 0.04F}, CreatureAtlasTile::VillagerSkin,
                   state.morph, tension, kMaterialSkin, 0.10F, 0.0F);
    } else {
        append_box(mesh, root, glm::vec3 {torso_half.x + folded_arm_half.x * 0.82F, 1.19F + body_bob - sleep_pose * 0.030F, 0.055F * scale + folded_sway},
                   folded_arm_half, glm::vec3 {0.0F, 0.0F, 0.12F - sleep_pose * 0.08F}, CreatureAtlasTile::VillagerCloth,
                   state.morph, tension, kMaterialHide, 0.12F, 0.0F);
        append_box(mesh, root, glm::vec3 {torso_half.x + folded_arm_half.x * 0.82F, 1.10F + body_bob - sleep_pose * 0.050F, -0.055F * scale - folded_sway},
                   folded_arm_half, glm::vec3 {0.0F, 0.0F, -0.12F + sleep_pose * 0.08F}, CreatureAtlasTile::VillagerCloth,
                   state.morph, tension, kMaterialHide, 0.12F, 0.0F);
        append_pair(mesh, root, glm::vec3 {torso_half.x + 0.105F * scale, 1.14F + body_bob - sleep_pose * 0.040F, 0.0F}, 0.115F * scale,
                    hand_half,
                    glm::vec3 {0.0F, 0.0F, 0.0F}, glm::vec3 {0.0F, 0.0F, 0.04F},
                    CreatureAtlasTile::VillagerSkin, state.morph, tension, kMaterialSkin, 0.10F, 0.0F);
    }

    const auto upper_leg_length = 0.280F * scale;
    const auto lower_leg_length = 0.300F * scale;
    const auto joint_overlap = 0.020F * scale;
    for (const auto side : kSides) {
        const auto leg_phase = stride * side;
        const auto thigh_angle = leg_phase * (0.11F + motion * 0.13F);
        const auto knee_bend = (0.030F + motion * 0.090F) * (0.62F + 0.38F * smoothstep01((leg_phase + 1.0F) * 0.5F));
        const auto shin_angle = thigh_angle * 0.40F - knee_bend;
        const glm::vec2 hip {-0.025F * scale, 0.642F * scale + body_bob};
        const auto knee = hip + glm::vec2 {std::sin(thigh_angle) * upper_leg_length, -std::cos(thigh_angle) * upper_leg_length};
        const auto ankle = knee + glm::vec2 {std::sin(shin_angle) * lower_leg_length, -std::cos(shin_angle) * lower_leg_length};
        const auto leg_z = side * hip_span;

        append_limb_segment(mesh,
                            root,
                            hip,
                            knee,
                            leg_z,
                            upper_leg_half_xz.x,
                            upper_leg_half_xz.y,
                            joint_overlap,
                            CreatureAtlasTile::VillagerCloth,
                            state.morph,
                            tension,
                            kMaterialHide,
                            0.10F,
                            0.0F);
        append_limb_segment(mesh,
                            root,
                            knee,
                            ankle,
                            leg_z,
                            lower_leg_half_xz.x,
                            lower_leg_half_xz.y,
                            joint_overlap,
                            CreatureAtlasTile::VillagerSkin,
                            state.morph,
                            tension,
                            kMaterialSkin,
                            0.08F,
                            0.0F);
        append_box(mesh,
                   root,
                   glm::vec3 {ankle.x + (0.040F + motion * 0.020F) * scale, foot_half.y * 0.95F, leg_z},
                   foot_half,
                   glm::vec3 {0.0F, 0.0F, thigh_angle * 0.18F},
                   CreatureAtlasTile::VillagerHair,
                   state.morph,
                   tension,
                   kMaterialKeratin,
                   0.08F,
                   0.0F);
    }
}

auto safe_crew_role(CrewVisualRole role) noexcept -> CrewVisualRole {
    const auto value = static_cast<std::uint8_t>(role);
    return value <= static_cast<std::uint8_t>(CrewVisualRole::Quartermaster) ? role : CrewVisualRole::Deckhand;
}

auto safe_crew_activity(CrewVisualActivity activity) noexcept -> CrewVisualActivity {
    const auto value = static_cast<std::uint8_t>(activity);
    return value <= static_cast<std::uint8_t>(CrewVisualActivity::Recover) ? activity : CrewVisualActivity::Idle;
}

auto finite_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) ? value : fallback;
}

auto finite_position(const glm::vec3& position) noexcept -> glm::vec3 {
    return {
        finite_or(position.x, 0.0F),
        finite_or(position.y, 0.0F),
        finite_or(position.z, 0.0F),
    };
}

auto crew_cloth_tile(CrewVisualRole role) noexcept -> CreatureAtlasTile {
    switch (role) {
    case CrewVisualRole::Captain:
        return CreatureAtlasTile::CrewNavyCloth;
    case CrewVisualRole::Fisher:
        return CreatureAtlasTile::CrewIvoryCloth;
    case CrewVisualRole::Rigger:
        return CreatureAtlasTile::CrewStripedCloth;
    case CrewVisualRole::WaterTender:
        return CreatureAtlasTile::CrewOchreCloth;
    case CrewVisualRole::Deckhand:
        return CreatureAtlasTile::CrewRedCloth;
    case CrewVisualRole::Quartermaster:
    default:
        return CreatureAtlasTile::CrewBurgundyCloth;
    }
}

auto crew_skin_tile(std::uint32_t seed) noexcept -> CreatureAtlasTile {
    switch ((seed >> 3U) % 3U) {
    case 0U:
        return CreatureAtlasTile::CrewSkinLight;
    case 1U:
        return CreatureAtlasTile::CrewSkinMedium;
    case 2U:
    default:
        return CreatureAtlasTile::CrewSkinDark;
    }
}

auto crew_hair_tile(std::uint32_t seed, CrewVisualRole role) noexcept -> CreatureAtlasTile {
    if (role == CrewVisualRole::Captain && ((seed >> 11U) & 1U) != 0U) {
        return CreatureAtlasTile::CrewHairGrey;
    }
    switch ((seed >> 7U) % 3U) {
    case 0U:
        return CreatureAtlasTile::CrewHairBrown;
    case 1U:
        return CreatureAtlasTile::CrewHairBlack;
    case 2U:
    default:
        return CreatureAtlasTile::CrewHairGrey;
    }
}

void append_crew_box(std::vector<CreaturePartInstance>& parts,
                     const glm::mat4& root,
                     const glm::vec3& center,
                     const glm::vec3& half_extent,
                     const glm::vec3& rotation,
                     CreatureAtlasTile tile,
                     float material_class,
                     float cavity_mask = 0.08F) {
    append_box(parts, root, center, half_extent, rotation, tile, 0.0F, 0.0F, material_class, cavity_mask, 0.0F);
}

void append_crew_box(std::vector<CreaturePartInstance>& parts,
                     const glm::mat4& root,
                     const glm::vec3& center,
                     const glm::vec3& half_extent,
                     float uniform_rotation,
                     CreatureAtlasTile tile,
                     float material_class,
                     float cavity_mask = 0.08F) {
    append_crew_box(
        parts,
        root,
        center,
        half_extent,
        glm::vec3 {uniform_rotation},
        tile,
        material_class,
        cavity_mask);
}

void append_crew_segment(std::vector<CreaturePartInstance>& parts,
                         const glm::mat4& root,
                         const glm::vec3& start,
                         const glm::vec3& end,
                         const glm::vec2& half_width,
                         CreatureAtlasTile tile,
                         float material_class,
                         float cavity_mask = 0.08F) {
    const auto delta = end - start;
    const auto length = glm::length(delta);
    if (!std::isfinite(length) || length <= 1.0e-4F || half_width.x <= 1.0e-4F || half_width.y <= 1.0e-4F) {
        return;
    }

    const auto axis_y = delta / length;
    const auto reference = std::abs(glm::dot(axis_y, glm::vec3 {0.0F, 1.0F, 0.0F})) > 0.94F
                               ? glm::vec3 {1.0F, 0.0F, 0.0F}
                               : glm::vec3 {0.0F, 1.0F, 0.0F};
    const auto axis_x = glm::normalize(glm::cross(reference, axis_y));
    const auto axis_z = glm::normalize(glm::cross(axis_y, axis_x));
    auto basis = glm::mat4 {1.0F};
    basis[0] = glm::vec4 {axis_x, 0.0F};
    basis[1] = glm::vec4 {axis_y, 0.0F};
    basis[2] = glm::vec4 {axis_z, 0.0F};

    auto transform = glm::translate(root, (start + end) * 0.5F);
    transform *= basis;
    transform = glm::scale(transform, glm::vec3 {half_width.x * 2.0F, length + 0.018F, half_width.y * 2.0F});
    parts.push_back({
        transform,
        make_uniform_uvs(tile),
        0.0F,
        0.0F,
        material_class,
        cavity_mask,
        0.0F,
    });
}

struct CrewPose {
    std::array<glm::vec3, 2> elbows {};
    std::array<glm::vec3, 2> hands {};
    float crouch = 0.0F;
    float body_lean = 0.0F;
    float head_pitch = 0.0F;
    float head_yaw = 0.0F;
    float stride = 0.0F;
};

auto build_crew_pose(const CrewRenderInstance& crew, CrewVisualActivity activity, float phase) noexcept -> CrewPose {
    const auto animation_time = finite_or(crew.animation_time, 0.0F);
    const auto motion = saturate(finite_or(crew.motion_amount, 0.0F));
    const auto locomotion_activity =
        activity == CrewVisualActivity::Walk ||
        activity == CrewVisualActivity::Carry;
    // En marche, la phase vient de la distance reellement parcourue. Les pieds
    // s'immobilisent donc lorsqu'un marin cede le passage, y compris avec une caisse.
    const auto cycle = locomotion_activity
                           ? std::sin(phase * kTwoPi)
                           : std::sin(animation_time * 5.4F + phase * kTwoPi);
    const auto slow_cycle = std::sin(animation_time * 2.2F + phase * kTwoPi);
    CrewPose pose {};
    pose.stride = locomotion_activity ? cycle * motion : 0.0F;
    pose.head_yaw = slow_cycle * 0.035F;
    for (std::size_t index = 0; index < kSides.size(); ++index) {
        const auto side = kSides[index];
        const auto arm_swing = pose.stride * side;
        pose.elbows[index] = {-0.015F - arm_swing * 0.065F, 1.105F, side * 0.225F};
        pose.hands[index] = {0.030F - arm_swing * 0.135F, 0.910F, side * 0.205F};
    }

    switch (activity) {
    case CrewVisualActivity::Steer:
        pose.body_lean = 0.035F + slow_cycle * 0.015F;
        pose.elbows = {{{0.205F, 1.285F, -0.235F}, {0.205F, 1.285F, 0.235F}}};
        pose.hands = {{{0.455F, 1.175F + slow_cycle * 0.025F, -0.190F}, {0.455F, 1.175F - slow_cycle * 0.025F, 0.190F}}};
        break;
    case CrewVisualActivity::Inspect:
        pose.head_pitch = -0.075F;
        pose.elbows = {{{0.175F, 1.405F, -0.185F}, {-0.035F, 1.105F, 0.225F}}};
        pose.hands = {{{0.315F, 1.550F, -0.090F}, {0.075F, 0.985F, 0.190F}}};
        break;
    case CrewVisualActivity::FishCast:
        pose.body_lean = -0.055F;
        pose.head_pitch = -0.035F;
        pose.elbows = {{{0.205F, 1.370F, -0.165F}, {0.145F, 1.260F, 0.175F}}};
        pose.hands = {{{0.430F, 1.455F, -0.075F}, {0.365F, 1.270F, 0.075F}}};
        break;
    case CrewVisualActivity::FishWait:
        pose.head_pitch = 0.055F;
        pose.elbows = {{{0.185F, 1.215F, -0.175F}, {0.150F, 1.145F, 0.175F}}};
        pose.hands = {{{0.405F, 1.155F, -0.070F}, {0.355F, 1.075F, 0.070F}}};
        break;
    case CrewVisualActivity::FishReel:
        pose.body_lean = 0.060F + cycle * 0.025F;
        pose.elbows = {{{0.185F, 1.285F, -0.170F}, {0.150F, 1.155F, 0.170F}}};
        pose.hands = {{{0.415F, 1.235F + cycle * 0.045F, -0.065F}, {0.355F, 1.075F - cycle * 0.035F, 0.065F}}};
        break;
    case CrewVisualActivity::TendWater:
        pose.crouch = 0.075F;
        pose.body_lean = 0.085F;
        pose.elbows = {{{0.170F, 1.125F, -0.190F}, {0.215F, 1.095F, 0.190F}}};
        pose.hands = {{{0.380F, 0.965F + cycle * 0.025F, -0.145F}, {0.420F, 0.945F - cycle * 0.025F, 0.145F}}};
        break;
    case CrewVisualActivity::Carry:
        pose.body_lean = 0.045F;
        pose.elbows = {{{0.170F, 1.105F, -0.230F}, {0.170F, 1.105F, 0.230F}}};
        pose.hands = {{{0.405F, 0.960F, -0.185F}, {0.405F, 0.960F, 0.185F}}};
        break;
    case CrewVisualActivity::HaulRope:
        pose.body_lean = 0.105F + cycle * 0.045F;
        pose.crouch = 0.035F;
        pose.elbows = {{{0.145F, 1.275F, -0.175F}, {0.245F, 1.065F, 0.175F}}};
        pose.hands = {{{0.375F, 1.185F, -0.060F}, {0.450F, 0.925F, 0.060F}}};
        break;
    case CrewVisualActivity::Scrub:
        pose.body_lean = 0.120F;
        pose.crouch = 0.080F;
        pose.elbows = {{{0.155F, 1.215F, -0.165F}, {0.260F, 0.980F, 0.165F}}};
        pose.hands = {{{0.340F, 1.145F, -0.055F}, {0.455F, 0.825F, 0.055F}}};
        break;
    case CrewVisualActivity::TurnCapstan:
        pose.body_lean = 0.115F;
        pose.crouch = 0.040F;
        pose.elbows = {{{0.230F, 1.180F, -0.205F}, {0.230F, 1.180F, 0.205F}}};
        pose.hands = {{{0.485F, 1.055F, -0.155F}, {0.485F, 1.055F, 0.155F}}};
        break;
    case CrewVisualActivity::SortCargo:
        pose.body_lean = 0.130F;
        pose.crouch = 0.135F;
        pose.head_pitch = 0.110F;
        pose.elbows = {{{0.165F, 1.055F, -0.190F}, {0.165F, 1.055F, 0.190F}}};
        pose.hands = {{{0.360F, 0.785F + cycle * 0.025F, -0.130F}, {0.360F, 0.785F - cycle * 0.025F, 0.130F}}};
        break;
    case CrewVisualActivity::Socialize:
        pose.head_yaw = slow_cycle * 0.110F;
        pose.elbows[0] = {0.090F, 1.335F, -0.210F};
        pose.hands[0] = {0.265F, 1.455F + slow_cycle * 0.045F, -0.185F};
        pose.elbows[1] = {0.020F, 1.100F, 0.225F};
        pose.hands[1] = {0.115F, 1.015F, 0.190F};
        break;
    case CrewVisualActivity::Rest:
        pose.crouch = 0.095F;
        pose.head_pitch = 0.035F;
        pose.elbows = {{{0.095F, 1.180F, -0.225F}, {0.095F, 1.100F, 0.225F}}};
        pose.hands = {{{0.245F, 1.080F, 0.105F}, {0.235F, 1.155F, -0.105F}}};
        break;
    case CrewVisualActivity::Hurt:
        pose.body_lean = -0.120F;
        pose.head_pitch = -0.090F;
        pose.elbows = {{{-0.105F, 1.390F, -0.250F}, {-0.105F, 1.390F, 0.250F}}};
        pose.hands = {{{0.010F, 1.570F, -0.300F}, {0.010F, 1.570F, 0.300F}}};
        break;
    case CrewVisualActivity::Recover:
        pose.crouch = 0.180F * (1.0F - phase);
        pose.body_lean = 0.150F * (1.0F - phase);
        pose.elbows[0] = {0.125F, 1.075F, -0.225F};
        pose.hands[0] = {0.315F, 0.760F, -0.205F};
        break;
    case CrewVisualActivity::Idle:
    case CrewVisualActivity::Walk:
    case CrewVisualActivity::KnockedOut:
    default:
        break;
    }
    return pose;
}

void append_crew_role_details(std::vector<CreaturePartInstance>& parts,
                              const glm::mat4& root,
                              CrewVisualRole role,
                              CreatureAtlasTile cloth,
                              CreatureAtlasTile hair,
                              float scale,
                              float body_y) {
    const auto fabric = kMaterialCrewFabric;
    const auto metal = kMaterialCrewMetal;
    switch (role) {
    case CrewVisualRole::Captain:
        // Je construis un tricorne en plusieurs volumes pour garder une silhouette lisible de loin.
        append_crew_box(parts, root, {0.015F, body_y + 0.692F, 0.0F}, {0.185F, 0.026F, 0.225F}, {0.0F}, cloth, fabric);
        append_crew_box(parts, root, {0.010F, body_y + 0.737F, -0.125F}, {0.145F, 0.048F, 0.045F}, {0.15F, 0.06F, 0.0F}, cloth, fabric);
        append_crew_box(parts, root, {0.010F, body_y + 0.737F, 0.125F}, {0.145F, 0.048F, 0.045F}, {-0.15F, -0.06F, 0.0F}, cloth, fabric);
        append_crew_box(parts, root, {-0.105F, body_y + 0.735F, 0.0F}, {0.050F, 0.050F, 0.135F}, {0.0F, 0.0F, 0.18F}, cloth, fabric);
        append_crew_box(parts, root, {0.188F, body_y + 0.172F, -0.185F}, {0.030F, 0.030F, 0.100F}, {0.0F}, CreatureAtlasTile::CrewGold, metal, 0.04F);
        append_crew_box(parts, root, {0.188F, body_y + 0.172F, 0.185F}, {0.030F, 0.030F, 0.100F}, {0.0F}, CreatureAtlasTile::CrewGold, metal, 0.04F);
        for (int button = 0; button < 3; ++button) {
            append_crew_box(parts, root, {0.188F, body_y + 0.205F - static_cast<float>(button) * 0.105F, 0.0F},
                            {0.014F, 0.014F, 0.014F}, {0.0F}, CreatureAtlasTile::CrewGold, metal, 0.02F);
        }
        append_crew_box(parts, root, {-0.085F, body_y - 0.210F, -0.105F}, {0.115F, 0.245F, 0.095F}, {0.0F, 0.0F, -0.035F}, cloth, fabric);
        append_crew_box(parts, root, {-0.085F, body_y - 0.210F, 0.105F}, {0.115F, 0.245F, 0.095F}, {0.0F, 0.0F, -0.035F}, cloth, fabric);
        break;
    case CrewVisualRole::Fisher:
        append_crew_box(parts, root, {0.000F, body_y + 0.626F, 0.0F}, {0.150F, 0.035F, 0.155F}, {0.0F}, CreatureAtlasTile::CrewRedCloth, fabric);
        append_crew_box(parts, root, {-0.165F, body_y + 0.585F, -0.085F}, {0.075F, 0.025F, 0.035F}, {0.0F, 0.25F, -0.35F}, CreatureAtlasTile::CrewRedCloth, fabric);
        append_crew_box(parts, root, {0.180F, body_y - 0.020F, 0.0F}, {0.020F, 0.255F, 0.130F}, {0.0F, 0.0F, -0.55F}, CreatureAtlasTile::CrewRedCloth, fabric);
        append_crew_box(parts, root, {-0.020F, body_y - 0.310F, 0.180F}, {0.080F, 0.105F, 0.055F}, {0.0F}, CreatureAtlasTile::CrewCanvas, fabric);
        break;
    case CrewVisualRole::Rigger:
        append_crew_box(parts, root, {-0.010F, body_y + 0.665F, 0.0F}, {0.150F, 0.075F, 0.155F}, {0.0F}, CreatureAtlasTile::CrewNavyCloth, fabric);
        append_crew_box(parts, root, {-0.118F, body_y + 0.725F, 0.0F}, {0.075F, 0.035F, 0.160F}, {0.0F, 0.0F, 0.15F}, CreatureAtlasTile::CrewNavyCloth, fabric);
        for (int coil = 0; coil < 4; ++coil) {
            const auto angle = static_cast<float>(coil) * kPi * 0.5F;
            const auto center = glm::vec3 {-0.105F, body_y + 0.105F + std::sin(angle) * 0.120F, 0.185F + std::cos(angle) * 0.060F};
            append_crew_box(parts, root, center, {0.025F, 0.085F, 0.025F}, {angle * 0.20F, 0.0F, angle}, CreatureAtlasTile::CrewRope, kMaterialCrewLeather);
        }
        break;
    case CrewVisualRole::WaterTender:
        append_crew_box(parts, root, {-0.010F, body_y + 0.655F, 0.0F}, {0.150F, 0.035F, 0.155F}, {0.0F}, CreatureAtlasTile::CrewIvoryCloth, fabric);
        append_crew_box(parts, root, {-0.165F, body_y + 0.610F, 0.105F}, {0.065F, 0.024F, 0.032F}, {0.0F, -0.18F, -0.30F}, CreatureAtlasTile::CrewIvoryCloth, fabric);
        append_crew_box(parts, root, {0.188F, body_y - 0.045F, 0.0F}, {0.025F, 0.285F, 0.145F}, {0.0F}, CreatureAtlasTile::CrewCanvas, fabric);
        append_crew_box(parts, root, {0.208F, body_y - 0.295F, -0.095F}, {0.035F, 0.055F, 0.040F}, {0.0F}, CreatureAtlasTile::CrewWater, kMaterialCrewMetal);
        append_crew_box(parts, root, {0.208F, body_y - 0.295F, 0.095F}, {0.035F, 0.055F, 0.040F}, {0.0F}, CreatureAtlasTile::CrewWater, kMaterialCrewMetal);
        break;
    case CrewVisualRole::Deckhand:
        append_crew_box(parts, root, {-0.010F, body_y + 0.645F, 0.0F}, {0.150F, 0.030F, 0.155F}, {0.0F}, CreatureAtlasTile::CrewNavyCloth, fabric);
        append_crew_box(parts, root, {-0.165F, body_y + 0.600F, -0.095F}, {0.070F, 0.024F, 0.032F}, {0.0F, 0.22F, -0.32F}, CreatureAtlasTile::CrewNavyCloth, fabric);
        append_crew_box(parts, root, {0.168F, body_y + 0.315F, 0.0F}, {0.028F, 0.085F, 0.145F}, {0.0F, 0.0F, -0.28F}, CreatureAtlasTile::CrewIvoryCloth, fabric);
        append_crew_box(parts, root, {0.145F, body_y + 0.240F, 0.115F}, {0.025F, 0.120F, 0.035F}, {0.25F, 0.0F, -0.15F}, CreatureAtlasTile::CrewIvoryCloth, fabric);
        break;
    case CrewVisualRole::Quartermaster:
        append_crew_box(parts, root, {0.188F, body_y + 0.115F, 0.0F}, {0.026F, 0.250F, 0.145F}, {0.0F}, CreatureAtlasTile::CrewBurgundyCloth, fabric);
        append_crew_box(parts, root, {-0.015F, body_y - 0.255F, 0.185F}, {0.075F, 0.105F, 0.035F}, {0.0F}, CreatureAtlasTile::CrewCanvas, fabric);
        append_crew_box(parts, root, {-0.015F, body_y - 0.382F, -0.185F}, {0.025F, 0.060F, 0.018F}, {0.0F}, CreatureAtlasTile::CrewGold, metal);
        for (int button = 0; button < 3; ++button) {
            append_crew_box(parts, root, {0.216F, body_y + 0.185F - static_cast<float>(button) * 0.105F, 0.0F},
                            {0.012F, 0.012F, 0.012F}, {0.0F}, CreatureAtlasTile::CrewGold, metal, 0.02F);
        }
        break;
    }

    static_cast<void>(hair);
    static_cast<void>(scale);
}

void append_crew_activity_prop(std::vector<CreaturePartInstance>& parts,
                               const glm::mat4& root,
                               const CrewRenderInstance& crew,
                               CrewVisualRole role,
                               CrewVisualActivity activity,
                               const CrewPose& pose,
                               float phase) {
    const auto& left_hand = pose.hands[0];
    const auto& right_hand = pose.hands[1];
    switch (activity) {
    case CrewVisualActivity::Inspect: {
        const auto eyepiece = glm::vec3 {0.245F, 1.625F - pose.crouch, -0.065F};
        const auto tip = glm::vec3 {0.515F, 1.660F - pose.crouch, -0.065F};
        append_crew_segment(parts, root, eyepiece, tip, {0.032F, 0.032F}, CreatureAtlasTile::CrewGold, kMaterialCrewMetal);
        append_crew_box(parts, root, tip, {0.030F, 0.045F, 0.045F}, {0.0F, 0.0F, kPi * 0.5F}, CreatureAtlasTile::CrewLeather, kMaterialCrewLeather);
        break;
    }
    case CrewVisualActivity::FishCast:
    case CrewVisualActivity::FishWait:
    case CrewVisualActivity::FishReel: {
        const auto reel = (left_hand + right_hand) * 0.5F;
        const auto cast = activity == CrewVisualActivity::FishCast ? 0.48F : 0.0F;
        const auto rod_tip = glm::vec3 {0.92F + cast, 1.78F + cast * 0.45F, 0.0F};
        append_crew_segment(parts, root, reel, rod_tip, {0.018F, 0.018F}, CreatureAtlasTile::CrewWood, kMaterialCrewWood);
        append_crew_box(parts, root, reel, {0.035F, 0.055F, 0.055F}, {0.0F}, CreatureAtlasTile::CrewIron, kMaterialCrewMetal);
        const auto line_end_y = activity == CrewVisualActivity::FishReel ? 0.46F + phase * 0.55F : 0.18F;
        const auto line_end = glm::vec3 {rod_tip.x + 0.12F, line_end_y, 0.0F};
        append_crew_segment(parts, root, rod_tip, line_end, {0.006F, 0.006F}, CreatureAtlasTile::CrewRope, kMaterialCrewLeather, 0.02F);
        if (activity == CrewVisualActivity::FishReel && phase >= 0.58F) {
            append_crew_box(parts, root, {line_end.x, line_end.y - 0.045F, 0.0F}, {0.115F, 0.050F, 0.040F}, {0.0F, 0.0F, -0.18F}, CreatureAtlasTile::CrewFish, kMaterialCrewSkin);
            append_crew_box(parts, root, {line_end.x - 0.125F, line_end.y - 0.045F, 0.0F}, {0.045F, 0.040F, 0.012F}, {0.0F, 0.0F, 0.45F}, CreatureAtlasTile::CrewFish, kMaterialCrewSkin);
        }
        break;
    }
    case CrewVisualActivity::TendWater: {
        const auto center = glm::vec3 {0.515F, 0.635F, 0.0F};
        append_crew_box(parts, root, center + glm::vec3 {0.0F, -0.13F, -0.135F}, {0.135F, 0.135F, 0.018F}, {0.0F}, CreatureAtlasTile::CrewIron, kMaterialCrewMetal);
        append_crew_box(parts, root, center + glm::vec3 {0.0F, -0.13F, 0.135F}, {0.135F, 0.135F, 0.018F}, {0.0F}, CreatureAtlasTile::CrewIron, kMaterialCrewMetal);
        append_crew_box(parts, root, center + glm::vec3 {0.0F, -0.255F, 0.0F}, {0.135F, 0.018F, 0.135F}, {0.0F}, CreatureAtlasTile::CrewIron, kMaterialCrewMetal);
        append_crew_box(parts, root, center + glm::vec3 {0.0F, -0.105F, 0.0F}, {0.118F, 0.010F, 0.118F}, {0.0F}, CreatureAtlasTile::CrewWater, kMaterialCrewMetal, 0.02F);
        append_crew_segment(parts, root, center + glm::vec3 {0.0F, -0.02F, -0.135F}, center + glm::vec3 {0.0F, 0.20F, 0.0F}, {0.012F, 0.012F}, CreatureAtlasTile::CrewIron, kMaterialCrewMetal);
        append_crew_segment(parts, root, center + glm::vec3 {0.0F, 0.20F, 0.0F}, center + glm::vec3 {0.0F, -0.02F, 0.135F}, {0.012F, 0.012F}, CreatureAtlasTile::CrewIron, kMaterialCrewMetal);
        break;
    }
    case CrewVisualActivity::Carry: {
        const auto center = glm::vec3 {0.500F, 0.920F, 0.0F};
        const auto content = role == CrewVisualRole::WaterTender ? CreatureAtlasTile::CrewWater : CreatureAtlasTile::CrewFish;
        append_crew_box(parts, root, center, {0.175F, 0.135F, 0.245F}, {0.0F}, CreatureAtlasTile::CrewWood, kMaterialCrewWood);
        append_crew_box(parts, root, center + glm::vec3 {0.185F, 0.0F, 0.0F}, {0.018F, 0.155F, 0.265F}, {0.0F}, CreatureAtlasTile::CrewIron, kMaterialCrewMetal);
        append_crew_box(parts, root, center + glm::vec3 {0.0F, 0.145F, 0.0F}, {0.145F, 0.030F, 0.205F}, {0.0F}, content, kMaterialCrewSkin);
        break;
    }
    case CrewVisualActivity::HaulRope:
        append_crew_segment(parts, root, {0.28F, 1.35F, -0.06F}, {0.72F, 0.68F, 0.06F}, {0.020F, 0.020F}, CreatureAtlasTile::CrewRope, kMaterialCrewLeather);
        break;
    case CrewVisualActivity::Scrub: {
        const auto mop_end = glm::vec3 {0.70F + std::sin(finite_or(crew.animation_time, 0.0F) * 4.0F) * 0.20F, 0.075F, 0.0F};
        append_crew_segment(parts, root, left_hand, mop_end, {0.018F, 0.018F}, CreatureAtlasTile::CrewWood, kMaterialCrewWood);
        append_crew_box(parts, root, mop_end, {0.105F, 0.035F, 0.230F}, {0.0F}, CreatureAtlasTile::CrewCanvas, kMaterialCrewFabric);
        break;
    }
    case CrewVisualActivity::SortCargo:
        if (role == CrewVisualRole::Quartermaster) {
            append_crew_box(parts, root, {0.440F, 0.755F, 0.0F}, {0.115F, 0.018F, 0.165F}, {0.0F, 0.0F, -0.16F}, CreatureAtlasTile::CrewCanvas, kMaterialCrewFabric);
            append_crew_box(parts, root, {0.455F, 0.785F, 0.0F}, {0.080F, 0.010F, 0.125F}, {0.0F, 0.0F, -0.16F}, CreatureAtlasTile::CrewIvoryCloth, kMaterialCrewFabric);
        }
        break;
    case CrewVisualActivity::Rest:
        append_crew_box(parts, root, {0.305F, 1.030F, 0.0F}, {0.065F, 0.075F, 0.065F}, {0.0F}, CreatureAtlasTile::CrewWood, kMaterialCrewWood);
        break;
    case CrewVisualActivity::Idle:
    case CrewVisualActivity::Walk:
    case CrewVisualActivity::Steer:
    case CrewVisualActivity::TurnCapstan:
    case CrewVisualActivity::Socialize:
    case CrewVisualActivity::Hurt:
    case CrewVisualActivity::KnockedOut:
    case CrewVisualActivity::Recover:
    default:
        break;
    }
}

void append_crew_model(std::vector<CreaturePartInstance>& parts, const CrewRenderInstance& source) {
    const auto first_part = parts.size();
    const auto role = safe_crew_role(source.role);
    const auto activity = safe_crew_activity(source.activity);
    const auto phase = saturate(finite_or(source.activity_phase, 0.0F));
    const auto hurt = saturate(finite_or(source.hurt_amount, 0.0F));
    auto knockout = saturate(finite_or(source.knockout_amount, 0.0F));
    if (activity == CrewVisualActivity::KnockedOut) {
        knockout = std::max(knockout, 1.0F);
    } else if (activity == CrewVisualActivity::Recover) {
        knockout = std::max(knockout, 1.0F - phase);
    }

    const auto scale = 0.94F + seed_detail_unit(source.appearance_seed, 75) * 0.055F +
                       (role == CrewVisualRole::Rigger ? 0.025F : 0.0F);
    const auto cloth = crew_cloth_tile(role);
    const auto skin = crew_skin_tile(source.appearance_seed);
    const auto hair = crew_hair_tile(source.appearance_seed, role);
    const auto pose = build_crew_pose(source, activity, phase);
    const auto fall_sign = seed_detail_signed(source.appearance_seed, 76) >= 0.0F ? 1.0F : -1.0F;
    const auto yaw = finite_or(source.yaw_radians, 0.0F);

    auto root = glm::translate(glm::mat4 {1.0F}, finite_position(source.position));
    root = glm::rotate(root, yaw, glm::vec3 {0.0F, 1.0F, 0.0F});
    root = glm::translate(root, glm::vec3 {hurt * -0.055F, knockout * 0.18F, 0.0F});
    root = glm::rotate(root, -knockout * 1.48F, glm::vec3 {0.0F, 0.0F, 1.0F});
    root = glm::rotate(root, fall_sign * knockout * 0.16F + hurt * fall_sign * 0.08F, glm::vec3 {1.0F, 0.0F, 0.0F});
    root = glm::rotate(root, pose.body_lean * (1.0F - knockout), glm::vec3 {0.0F, 0.0F, 1.0F});
    root = glm::scale(root, glm::vec3 {scale});

    const auto breath = std::sin(finite_or(source.animation_time, 0.0F) * (activity == CrewVisualActivity::Rest ? 1.25F : 2.0F) + phase * kTwoPi) * 0.010F;
    const auto body_y = 1.145F - pose.crouch + breath;
    const auto torso_half = glm::vec3 {0.175F, 0.315F, 0.120F};
    const auto pelvis_y = 0.785F - pose.crouch;

    append_crew_box(parts, root, {0.0F, body_y, 0.0F}, torso_half, {0.0F}, cloth, kMaterialCrewFabric, 0.13F);
    append_crew_box(parts, root, {-0.020F, pelvis_y, 0.0F}, {0.155F, 0.105F, 0.115F}, {0.0F}, CreatureAtlasTile::CrewNavyCloth, kMaterialCrewFabric, 0.10F);
    append_crew_box(parts, root, {0.0F, 0.925F - pose.crouch, 0.0F}, {0.185F, 0.026F, 0.130F}, {0.0F}, CreatureAtlasTile::CrewLeather, kMaterialCrewLeather, 0.06F);
    append_crew_box(parts, root, {0.020F, 1.475F - pose.crouch + breath * 0.25F, 0.0F}, {0.060F, 0.055F, 0.060F}, {0.0F}, skin, kMaterialCrewSkin);
    append_crew_box(parts, root, {0.025F, 1.655F - pose.crouch + breath * 0.35F, 0.0F}, {0.145F, 0.160F, 0.140F}, {pose.head_pitch, pose.head_yaw, 0.0F}, skin, kMaterialCrewSkin, 0.10F);
    append_crew_box(parts, root, {-0.010F, 1.817F - pose.crouch, 0.0F}, {0.145F, 0.035F, 0.142F}, {pose.head_pitch * 0.25F, pose.head_yaw, 0.0F}, hair, kMaterialCrewLeather, 0.07F);
    append_crew_box(parts, root, {-0.100F, 1.685F - pose.crouch, 0.0F}, {0.045F, 0.125F, 0.145F}, {pose.head_pitch * 0.20F, pose.head_yaw, 0.0F}, hair, kMaterialCrewLeather, 0.08F);
    append_crew_box(parts, root, {0.185F, 1.650F - pose.crouch, 0.0F}, {0.050F, 0.062F, 0.042F}, {pose.head_pitch, pose.head_yaw, 0.0F}, skin, kMaterialCrewSkin, 0.04F);
    for (const auto side : kSides) {
        append_crew_box(parts, root, {0.164F, 1.704F - pose.crouch, side * 0.067F}, {0.016F, 0.025F, 0.018F}, {0.0F}, CreatureAtlasTile::VillagerEye, kMaterialCrewSkin, 0.82F);
        append_crew_box(parts, root, {0.010F, 1.655F - pose.crouch, side * 0.151F}, {0.025F, 0.045F, 0.022F}, {0.0F}, skin, kMaterialCrewSkin);
    }
    if (((source.appearance_seed >> 13U) & 1U) != 0U || role == CrewVisualRole::Captain) {
        append_crew_box(parts, root, {0.166F, 1.580F - pose.crouch, 0.0F}, {0.022F, 0.028F, 0.090F}, {0.0F}, hair, kMaterialCrewLeather);
        append_crew_box(parts, root, {0.072F, 1.505F - pose.crouch, 0.0F}, {0.070F, 0.075F, 0.105F}, {0.0F}, hair, kMaterialCrewLeather, 0.12F);
    }

    const auto shoulder_y = 1.350F - pose.crouch;
    for (std::size_t index = 0; index < kSides.size(); ++index) {
        const auto side = kSides[index];
        const auto shoulder = glm::vec3 {0.0F, shoulder_y, side * 0.220F};
        append_crew_segment(parts, root, shoulder, pose.elbows[index] - glm::vec3 {0.0F, pose.crouch, 0.0F},
                            {0.055F, 0.055F}, cloth, kMaterialCrewFabric, 0.11F);
        append_crew_segment(parts, root, pose.elbows[index] - glm::vec3 {0.0F, pose.crouch, 0.0F},
                            pose.hands[index] - glm::vec3 {0.0F, pose.crouch, 0.0F}, {0.048F, 0.048F}, skin, kMaterialCrewSkin, 0.08F);
        append_crew_box(parts, root, pose.hands[index] - glm::vec3 {0.0F, pose.crouch, 0.0F}, {0.050F, 0.052F, 0.050F}, {0.0F}, skin, kMaterialCrewSkin, 0.07F);

        const auto leg_stride = pose.stride * side;
        const auto hip = glm::vec3 {-0.020F, pelvis_y, side * 0.095F};
        const auto knee = glm::vec3 {leg_stride * 0.105F, 0.455F - pose.crouch * 0.45F, side * 0.098F};
        const auto ankle = glm::vec3 {-leg_stride * 0.050F, 0.135F, side * 0.100F};
        append_crew_segment(parts, root, hip, knee, {0.062F, 0.058F}, CreatureAtlasTile::CrewNavyCloth, kMaterialCrewFabric, 0.10F);
        append_crew_segment(parts, root, knee, ankle, {0.056F, 0.052F}, CreatureAtlasTile::CrewNavyCloth, kMaterialCrewFabric, 0.09F);
        append_crew_box(parts, root, {0.045F + leg_stride * 0.020F, 0.070F, side * 0.100F}, {0.105F, 0.065F, 0.065F}, {0.0F}, CreatureAtlasTile::CrewLeather, kMaterialCrewLeather, 0.06F);
    }

    append_crew_role_details(parts, root, role, cloth, hair, scale, body_y);
    append_crew_activity_prop(parts, root, source, role, activity, pose, phase);

    // Je laisse le renderer appliquer le cycle jour/nuit global une seule fois ;
    // cette valeur represente uniquement l'occlusion locale du ciel.
    const auto sky_light = saturate(finite_or(source.sky_light, 1.0F));
    const auto block_light = saturate(finite_or(source.local_light, 0.0F));
    const auto precipitation = saturate(finite_or(source.precipitation_exposure, 1.0F));
    for (auto index = first_part; index < parts.size(); ++index) {
        parts[index].sky_light = sky_light;
        parts[index].block_light = block_light;
        parts[index].precipitation_exposure = precipitation;
    }
}

auto sample_creature_tile(CreatureAtlasTile tile, int x, int y) noexcept -> std::array<std::uint8_t, 4> {
    const auto fx = static_cast<float>(x) + 0.5F;
    const auto fy = static_cast<float>(y) + 0.5F;
    const auto nx = fx / static_cast<float>(kCreatureAtlasTileSize);
    const auto ny = fy / static_cast<float>(kCreatureAtlasTileSize);
    const auto grain = tile_noise(x, y, static_cast<int>(tile));
    const auto soft_grain = tile_noise(x + 7, y + 5, static_cast<int>(tile) + 23);
    const auto edge = edge_distance(nx, ny);

    switch (tile) {
    case CreatureAtlasTile::PigHide: {
        const auto blush = radial_falloff(nx, ny, 0.42F, 0.56F, 0.62F);
        const auto warm_band = 0.5F + 0.5F * std::sin(nx * 10.0F + ny * 4.0F + grain * 6.0F);
        return make_rgba(214.0F + grain * 18.0F + warm_band * 10.0F,
                         154.0F + soft_grain * 14.0F + blush * 6.0F,
                         168.0F + warm_band * 12.0F,
                         0.0F);
    }
    case CreatureAtlasTile::PigSnout: {
        const auto nostril_left = line_mask(nx, 0.33F, 0.08F) * line_mask(ny, 0.58F, 0.14F);
        const auto nostril_right = line_mask(nx, 0.67F, 0.08F) * line_mask(ny, 0.58F, 0.14F);
        const auto seam = line_mask(nx, 0.50F, 0.03F) * line_mask(ny, 0.50F, 0.24F);
        const auto nostril = std::max(nostril_left, nostril_right);
        if (nostril > 0.55F) {
            return make_rgba(112.0F + grain * 8.0F, 58.0F + soft_grain * 6.0F, 70.0F + grain * 8.0F, 0.0F);
        }
        return make_rgba(238.0F + grain * 8.0F,
                         180.0F + soft_grain * 10.0F - seam * 10.0F,
                         192.0F + grain * 10.0F - seam * 8.0F,
                         0.0F);
    }
    case CreatureAtlasTile::PigEar: {
        const auto translucency = 1.0F - edge * 1.4F;
        return make_rgba(230.0F - edge * 36.0F,
                         164.0F - edge * 26.0F + translucency * 12.0F,
                         178.0F - edge * 24.0F + translucency * 10.0F,
                         0.0F);
    }
    case CreatureAtlasTile::CowHide: {
        const auto patch_noise = tile_noise(x / 2 + 11, y / 2 + 17, 41);
        const auto streak = tile_noise(x + 23, y + 9, 59);
        if (patch_noise > 0.60F || (patch_noise > 0.52F && streak > 0.68F)) {
            return make_rgba(88.0F + grain * 16.0F, 66.0F + soft_grain * 10.0F, 48.0F + grain * 10.0F, 0.0F);
        }
        return make_rgba(196.0F + soft_grain * 18.0F,
                         184.0F + grain * 14.0F,
                         166.0F + soft_grain * 12.0F,
                         0.0F);
    }
    case CreatureAtlasTile::CowMuzzle: {
        const auto bridge = radial_falloff(nx, ny, 0.50F, 0.48F, 0.54F);
        const auto nostrils = std::max(line_mask(nx, 0.34F, 0.08F) * line_mask(ny, 0.60F, 0.14F),
                                       line_mask(nx, 0.66F, 0.08F) * line_mask(ny, 0.60F, 0.14F));
        return make_rgba(180.0F + bridge * 16.0F - nostrils * 26.0F,
                         136.0F + grain * 14.0F - nostrils * 18.0F,
                         122.0F + soft_grain * 12.0F - nostrils * 16.0F,
                         0.0F);
    }
    case CreatureAtlasTile::CowHorn: {
        const auto gradient = ny;
        return make_rgba(200.0F - gradient * 62.0F,
                         184.0F - gradient * 58.0F,
                         142.0F - gradient * 56.0F,
                         0.0F);
    }
    case CreatureAtlasTile::SheepWool: {
        const auto curl = std::sin(nx * 18.0F + grain * 6.0F) * std::sin(ny * 16.0F + soft_grain * 5.0F);
        const auto loft = radial_falloff(nx, ny, 0.50F, 0.44F, 0.74F);
        return make_rgba(232.0F + curl * 12.0F + loft * 6.0F,
                         230.0F + grain * 10.0F + loft * 4.0F,
                         220.0F + soft_grain * 12.0F + loft * 8.0F,
                         0.0F);
    }
    case CreatureAtlasTile::SheepFace: {
        const auto muzzle = radial_falloff(nx, ny, 0.54F, 0.56F, 0.42F);
        return make_rgba(114.0F + grain * 14.0F + muzzle * 10.0F,
                         100.0F + soft_grain * 14.0F + muzzle * 8.0F,
                         90.0F + grain * 12.0F + muzzle * 6.0F,
                         0.0F);
    }
    case CreatureAtlasTile::SheepHoof: {
        const auto sheen = 0.5F + 0.5F * std::sin(nx * 8.0F + ny * 5.0F);
        return make_rgba(54.0F + sheen * 18.0F, 46.0F + sheen * 14.0F, 40.0F + sheen * 12.0F, 0.0F);
    }
    case CreatureAtlasTile::ZombieFlesh: {
        const auto bruise = tile_noise(x * 3, y * 3, 77);
        const auto basalt = 0.5F + 0.5F * std::sin(nx * 18.0F + ny * 13.0F + grain * 7.0F);
        const auto soot = radial_falloff(nx, ny, 0.48F, 0.36F, 0.72F);
        return make_rgba(44.0F + basalt * 22.0F + grain * 10.0F - bruise * 8.0F - soot * 6.0F,
                         43.0F + soft_grain * 14.0F + basalt * 12.0F - bruise * 7.0F - soot * 5.0F,
                         39.0F + grain * 10.0F + basalt * 10.0F - bruise * 7.0F - soot * 4.0F,
                         0.0F);
    }
    case CreatureAtlasTile::ZombieBone: {
        const auto crack = (x + y * 2) % 7 == 0 ? 24.0F : 0.0F;
        const auto ridge = 0.5F + 0.5F * std::sin(nx * 11.0F + ny * 7.0F + grain * 6.0F);
        return make_rgba(72.0F + ridge * 26.0F + grain * 12.0F - crack,
                         70.0F + ridge * 23.0F + soft_grain * 10.0F - crack * 0.82F,
                         64.0F + ridge * 18.0F + grain * 8.0F - crack * 0.76F,
                         0.0F);
    }
    case CreatureAtlasTile::ZombieMouth: {
        const auto wet = radial_falloff(nx, ny, 0.46F, 0.44F, 0.64F);
        const auto ember = radial_falloff(nx, ny, 0.50F, 0.52F, 0.30F);
        return make_rgba(52.0F + wet * 16.0F + ember * 160.0F,
                         14.0F + grain * 8.0F + ember * 20.0F,
                         12.0F + soft_grain * 6.0F + ember * 10.0F,
                         0.0F);
    }
    case CreatureAtlasTile::ZombieTeeth: {
        const auto enamel = 0.6F + 0.4F * std::sin(nx * 16.0F);
        return make_rgba(104.0F + enamel * 24.0F + grain * 8.0F,
                         98.0F + grain * 10.0F,
                         88.0F + soft_grain * 10.0F,
                         0.0F);
    }
    case CreatureAtlasTile::ZombieEye: {
        const auto sclera = radial_falloff(nx, ny, 0.5F, 0.5F, 0.46F);
        const auto pupil = radial_falloff(nx, ny, 0.52F, 0.52F, 0.12F);
        const auto ember = radial_falloff(nx, ny, 0.50F, 0.50F, 0.34F) * (1.0F - pupil * 0.28F);
        const auto alpha = 172.0F + sclera * 80.0F;
        return make_rgba(220.0F + sclera * 34.0F + ember * 26.0F - pupil * 50.0F,
                         18.0F + ember * 36.0F - pupil * 14.0F,
                         12.0F + ember * 18.0F - pupil * 10.0F,
                         alpha);
    }
    case CreatureAtlasTile::ZombieVein: {
        const auto wave = std::sin(nx * 15.0F + ny * 4.0F + grain * 6.0F) + 0.45F * std::sin(nx * 5.0F - ny * 14.0F + soft_grain * 5.0F);
        const auto mask = smooth_range(0.72F, 0.92F, std::abs(wave));
        return make_rgba(176.0F + mask * 58.0F,
                         18.0F + mask * 20.0F,
                         12.0F + mask * 12.0F,
                         mask * (64.0F + soft_grain * 132.0F));
    }
    case CreatureAtlasTile::ZombieScar: {
        const auto line_a = std::abs((nx - 0.24F) * 0.70F + (ny - 0.54F)) < 0.05F;
        const auto line_b = std::abs((nx - 0.66F) * 0.80F - (ny - 0.34F)) < 0.05F;
        const auto line_c = std::abs((nx - 0.44F) * 0.28F + (ny - 0.72F)) < 0.04F;
        const auto mask = (line_a || line_b || line_c) ? 1.0F : 0.0F;
        return make_rgba(196.0F + grain * 28.0F,
                         20.0F + soft_grain * 14.0F,
                         12.0F + grain * 8.0F,
                         mask * 190.0F);
    }
    case CreatureAtlasTile::PigBelly:
        return make_rgba(240.0F + grain * 8.0F, 190.0F + soft_grain * 10.0F, 198.0F + grain * 10.0F, 0.0F);
    case CreatureAtlasTile::PigHoof:
        return make_rgba(100.0F + grain * 10.0F, 84.0F + soft_grain * 8.0F, 96.0F + grain * 10.0F, 0.0F);
    case CreatureAtlasTile::CowHoof:
        return make_rgba(68.0F + grain * 10.0F, 54.0F + soft_grain * 8.0F, 42.0F + grain * 6.0F, 0.0F);
    case CreatureAtlasTile::SheepShadow: {
        const auto curl = std::sin(nx * 14.0F + grain * 5.0F) * std::sin(ny * 13.0F + soft_grain * 5.0F);
        return make_rgba(198.0F + curl * 10.0F, 194.0F + grain * 8.0F, 186.0F + soft_grain * 10.0F, 0.0F);
    }
    case CreatureAtlasTile::TransformHide: {
        const auto slice = line_mask(nx + ny * 0.28F, 0.62F, 0.10F);
        return make_rgba(128.0F + grain * 20.0F + slice * 24.0F,
                         64.0F + soft_grain * 10.0F + slice * 8.0F,
                         56.0F + grain * 8.0F + slice * 6.0F,
                         0.0F);
    }
    case CreatureAtlasTile::TransformSinew: {
        const auto fiber = 0.5F + 0.5F * std::sin(nx * 26.0F + soft_grain * 8.0F);
        return make_rgba(168.0F + fiber * 24.0F,
                         42.0F + grain * 10.0F,
                         34.0F + soft_grain * 8.0F,
                         0.0F);
    }
    case CreatureAtlasTile::TransformGlow: {
        const auto fissure = smooth_range(0.78F, 0.94F, std::abs(std::sin(nx * 15.0F + ny * 6.0F + grain * 6.0F)));
        return make_rgba(220.0F + fissure * 34.0F,
                         18.0F + fissure * 24.0F,
                         10.0F + fissure * 12.0F,
                         fissure * (96.0F + soft_grain * 140.0F));
    }
    case CreatureAtlasTile::ZombieClaw: {
        const auto highlight = 0.5F + 0.5F * std::sin(nx * 7.0F + ny * 3.0F + grain * 4.0F);
        return make_rgba(38.0F + highlight * 30.0F, 36.0F + highlight * 25.0F, 34.0F + highlight * 22.0F, 0.0F);
    }
    case CreatureAtlasTile::ZombieWool: {
        const auto curl = std::sin(nx * 16.0F + grain * 5.0F) * std::sin(ny * 12.0F + soft_grain * 4.0F);
        const auto stain = radial_falloff(nx, ny, 0.60F, 0.64F, 0.32F);
        return make_rgba(58.0F + curl * 18.0F + grain * 8.0F,
                         56.0F + grain * 12.0F - stain * 12.0F,
                         52.0F + soft_grain * 12.0F - stain * 10.0F,
                         0.0F);
    }
    case CreatureAtlasTile::ZombieHorn: {
        const auto gradient = ny;
        return make_rgba(78.0F - gradient * 26.0F,
                         72.0F - gradient * 23.0F,
                         64.0F - gradient * 20.0F,
                         0.0F);
    }
    case CreatureAtlasTile::VillagerCloth: {
        const auto weave = 0.5F + 0.5F * std::sin(nx * 12.0F + ny * 7.0F + grain * 5.0F);
        const auto fold = line_mask(nx + ny * 0.22F, 0.54F, 0.055F) + line_mask(nx - ny * 0.18F, 0.28F, 0.045F);
        return make_rgba(218.0F + weave * 18.0F - fold * 18.0F,
                         204.0F + soft_grain * 16.0F - fold * 12.0F,
                         172.0F + weave * 12.0F + grain * 6.0F - fold * 8.0F,
                         0.0F);
    }
    case CreatureAtlasTile::VillagerSkin: {
        const auto warmth = radial_falloff(nx, ny, 0.48F, 0.52F, 0.70F);
        return make_rgba(204.0F + warmth * 18.0F + grain * 10.0F,
                         156.0F + soft_grain * 14.0F + warmth * 8.0F,
                         122.0F + grain * 10.0F + warmth * 6.0F,
                         0.0F);
    }
    case CreatureAtlasTile::VillagerHair: {
        const auto streak = 0.5F + 0.5F * std::sin(nx * 16.0F + soft_grain * 6.0F);
        return make_rgba(82.0F + streak * 24.0F,
                         58.0F + grain * 14.0F,
                         34.0F + soft_grain * 10.0F,
                         0.0F);
    }
    case CreatureAtlasTile::VillagerApron: {
        const auto border = edge < 0.12F ? 1.0F : 0.0F;
        const auto stripe = line_mask(nx, 0.50F, 0.04F) * line_mask(ny, 0.52F, 0.36F);
        const auto woven = 0.5F + 0.5F * std::sin((nx * 9.0F - ny * 7.0F) + grain * 5.0F);
        return make_rgba(172.0F + grain * 22.0F + woven * 16.0F - stripe * 28.0F - border * 20.0F,
                         94.0F + soft_grain * 16.0F + woven * 8.0F - stripe * 16.0F - border * 10.0F,
                         46.0F + grain * 8.0F + woven * 4.0F - stripe * 8.0F,
                         0.0F);
    }
    case CreatureAtlasTile::VillagerEye: {
        const auto eye_white = radial_falloff(nx, ny, 0.50F, 0.50F, 0.42F);
        const auto pupil = radial_falloff(nx, ny, 0.54F, 0.52F, 0.12F);
        const auto brow = smooth_range(0.52F, 0.84F, line_mask(ny, 0.24F, 0.08F));
        const auto alpha = std::max(eye_white, pupil) > 0.12F ? 196.0F : 0.0F;
        return make_rgba(220.0F + eye_white * 26.0F - pupil * 220.0F - brow * 28.0F,
                         206.0F + eye_white * 18.0F - pupil * 180.0F - brow * 18.0F,
                         194.0F + eye_white * 14.0F - pupil * 160.0F - brow * 12.0F,
                         alpha);
    }
    case CreatureAtlasTile::CrewNavyCloth:
    case CreatureAtlasTile::CrewIvoryCloth:
    case CreatureAtlasTile::CrewOchreCloth:
    case CreatureAtlasTile::CrewRedCloth:
    case CreatureAtlasTile::CrewBurgundyCloth: {
        auto base = glm::vec3 {38.0F, 58.0F, 82.0F};
        if (tile == CreatureAtlasTile::CrewIvoryCloth) {
            base = {214.0F, 202.0F, 170.0F};
        } else if (tile == CreatureAtlasTile::CrewOchreCloth) {
            base = {174.0F, 116.0F, 42.0F};
        } else if (tile == CreatureAtlasTile::CrewRedCloth) {
            base = {142.0F, 48.0F, 40.0F};
        } else if (tile == CreatureAtlasTile::CrewBurgundyCloth) {
            base = {92.0F, 34.0F, 48.0F};
        }
        const auto weave = std::sin(nx * 29.0F + grain * 3.0F) * 3.0F +
                           std::cos(ny * 31.0F + soft_grain * 3.0F) * 3.0F;
        const auto fold = line_mask(nx + ny * 0.16F, 0.54F, 0.045F) * 12.0F;
        return make_rgba(base.r + weave - fold, base.g + weave - fold * 0.72F, base.b + weave - fold * 0.52F, 0.0F);
    }
    case CreatureAtlasTile::CrewStripedCloth: {
        const auto stripe = (static_cast<int>(std::floor(ny * 8.0F)) & 1) == 0 ? 1.0F : 0.0F;
        const auto seam = edge < 0.08F ? 12.0F : 0.0F;
        return make_rgba(196.0F - stripe * 116.0F - seam,
                         190.0F - stripe * 108.0F - seam,
                         168.0F - stripe * 92.0F - seam * 0.7F,
                         0.0F);
    }
    case CreatureAtlasTile::CrewSkinLight:
    case CreatureAtlasTile::CrewSkinMedium:
    case CreatureAtlasTile::CrewSkinDark: {
        auto base = glm::vec3 {218.0F, 166.0F, 126.0F};
        if (tile == CreatureAtlasTile::CrewSkinMedium) {
            base = {172.0F, 116.0F, 78.0F};
        } else if (tile == CreatureAtlasTile::CrewSkinDark) {
            base = {112.0F, 72.0F, 50.0F};
        }
        const auto warmth = radial_falloff(nx, ny, 0.48F, 0.50F, 0.72F) * 9.0F;
        return make_rgba(base.r + warmth + grain * 5.0F,
                         base.g + warmth * 0.55F + soft_grain * 4.0F,
                         base.b + warmth * 0.34F + grain * 3.0F,
                         0.0F);
    }
    case CreatureAtlasTile::CrewHairBrown:
    case CreatureAtlasTile::CrewHairBlack:
    case CreatureAtlasTile::CrewHairGrey: {
        auto base = glm::vec3 {70.0F, 45.0F, 27.0F};
        if (tile == CreatureAtlasTile::CrewHairBlack) {
            base = {30.0F, 28.0F, 26.0F};
        } else if (tile == CreatureAtlasTile::CrewHairGrey) {
            base = {132.0F, 126.0F, 116.0F};
        }
        const auto strand = (0.5F + 0.5F * std::sin(nx * 34.0F + soft_grain * 5.0F)) * 18.0F;
        return make_rgba(base.r + strand, base.g + strand * 0.82F, base.b + strand * 0.62F, 0.0F);
    }
    case CreatureAtlasTile::CrewLeather: {
        const auto scratch = line_mask(nx - ny * 0.36F, 0.31F, 0.025F) * 18.0F;
        return make_rgba(92.0F + grain * 18.0F + scratch,
                         55.0F + soft_grain * 10.0F + scratch * 0.55F,
                         28.0F + grain * 7.0F,
                         0.0F);
    }
    case CreatureAtlasTile::CrewGold: {
        const auto glint = smooth_range(0.68F, 0.96F, std::sin((nx + ny) * 13.0F) * 0.5F + 0.5F);
        return make_rgba(190.0F + glint * 54.0F, 132.0F + glint * 56.0F, 34.0F + glint * 30.0F, 0.0F);
    }
    case CreatureAtlasTile::CrewRope: {
        const auto twist = 0.5F + 0.5F * std::sin((nx * 2.0F + ny) * 24.0F);
        return make_rgba(142.0F + twist * 32.0F, 105.0F + twist * 25.0F, 58.0F + twist * 16.0F, 0.0F);
    }
    case CreatureAtlasTile::CrewWood: {
        const auto grain_line = 0.5F + 0.5F * std::sin(ny * 26.0F + grain * 8.0F);
        return make_rgba(104.0F + grain_line * 30.0F, 62.0F + grain_line * 17.0F, 29.0F + grain_line * 9.0F, 0.0F);
    }
    case CreatureAtlasTile::CrewIron: {
        const auto brushed = 0.5F + 0.5F * std::sin(nx * 37.0F + grain * 4.0F);
        return make_rgba(92.0F + brushed * 54.0F, 98.0F + brushed * 55.0F, 102.0F + brushed * 57.0F, 0.0F);
    }
    case CreatureAtlasTile::CrewWater: {
        const auto ripple = 0.5F + 0.5F * std::sin(nx * 15.0F + ny * 21.0F + soft_grain * 5.0F);
        return make_rgba(42.0F + ripple * 28.0F, 116.0F + ripple * 42.0F, 166.0F + ripple * 54.0F, 0.0F);
    }
    case CreatureAtlasTile::CrewFish: {
        const auto scale_pattern = std::abs(std::sin(nx * 25.0F) * std::sin(ny * 19.0F));
        return make_rgba(92.0F + scale_pattern * 58.0F,
                         132.0F + scale_pattern * 52.0F,
                         142.0F + scale_pattern * 48.0F,
                         0.0F);
    }
    case CreatureAtlasTile::CrewCanvas: {
        const auto fiber = std::sin(nx * 38.0F) * 4.0F + std::cos(ny * 39.0F) * 4.0F;
        return make_rgba(188.0F + fiber, 172.0F + fiber, 132.0F + fiber * 0.72F, 0.0F);
    }
    case CreatureAtlasTile::Count:
        break;
    }

    return make_rgba(255.0F, 0.0F, 255.0F, 0.0F);
}

} // namespace

auto creature_atlas_tile_coordinates(CreatureAtlasTile tile) noexcept -> std::array<int, 2> {
    const auto index = static_cast<int>(tile);
    return {index % static_cast<int>(kCreatureAtlasTilesPerAxis), index / static_cast<int>(kCreatureAtlasTilesPerAxis)};
}

auto build_creature_atlas_pixels() -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kCreatureAtlasSize * kCreatureAtlasSize * 4), 0);
    for (int tile_index = 0; tile_index < static_cast<int>(CreatureAtlasTile::Count); ++tile_index) {
        const auto tile = static_cast<CreatureAtlasTile>(tile_index);
        const auto coordinates = creature_atlas_tile_coordinates(tile);
        fill_tile(pixels, coordinates[0], coordinates[1], [tile](int x, int y) {
            return sample_creature_tile(tile, x, y);
        });
    }
    return pixels;
}

auto build_creature_parts(const CreatureRenderInstance& creature) -> std::vector<CreaturePartInstance> {
    std::vector<CreaturePartInstance> mesh {};
    mesh.reserve(128U);

    const auto hurt_amount = saturate(creature.hurt_amount);
    const auto death_amount = saturate(creature.death_amount);
    const auto hit_direction = safe_horizontal_direction(creature.hit_direction);
    const auto hit_yaw = std::atan2(hit_direction.z, hit_direction.x);
    const auto stagger = hurt_amount * (1.0F - death_amount);
    const auto death_sink = death_amount * (0.26F + 0.10F * seed_detail_unit(creature.appearance_seed, 31));
    const auto death_roll_sign = seed_detail_signed(creature.appearance_seed, 32) >= 0.0F ? 1.0F : -1.0F;
    const auto death_roll = death_amount * (0.48F + 0.20F * seed_detail_unit(creature.appearance_seed, 33)) * death_roll_sign;
    const auto death_pitch = death_amount * (1.18F + 0.28F * seed_detail_unit(creature.appearance_seed, 34));
    const auto impact_bounce = std::sin((1.0F - death_amount) * kPi) * stagger * 0.050F;

    auto root = glm::translate(
        glm::mat4(1.0F),
        creature.position + hit_direction * (stagger * 0.085F + death_amount * 0.16F) +
            glm::vec3 {0.0F, impact_bounce - death_sink, 0.0F});
    root = glm::rotate(root, glm::mix(creature.yaw_radians, hit_yaw, death_amount * 0.34F), glm::vec3 {0.0F, 1.0F, 0.0F});
    root = glm::rotate(root, -death_pitch, glm::vec3 {0.0F, 0.0F, 1.0F});
    root = glm::rotate(root, death_roll + stagger * 0.18F, glm::vec3 {1.0F, 0.0F, 0.0F});
    root = glm::scale(root, glm::vec3 {1.0F + stagger * 0.025F, 1.0F - death_amount * 0.08F, 1.0F + stagger * 0.020F});

    const auto state = build_visual_state(creature);

    switch (creature.species) {
    case CreatureSpecies::Pig:
        append_day_pig(mesh, creature, state, root);
        append_transition_accents(mesh, creature, state, root);
        append_night_pig(mesh, creature, state, root);
        break;
    case CreatureSpecies::Cow:
        append_day_cow(mesh, creature, state, root);
        append_transition_accents(mesh, creature, state, root);
        append_night_cow(mesh, creature, state, root);
        break;
    case CreatureSpecies::Villager:
        append_day_villager(mesh, creature, state, root);
        break;
    case CreatureSpecies::Sheep:
    default:
        append_day_sheep(mesh, creature, state, root);
        append_transition_accents(mesh, creature, state, root);
        append_night_sheep(mesh, creature, state, root);
        break;
    }

    return mesh;
}

void append_crew_parts(std::vector<CreaturePartInstance>& parts, const CrewRenderInstance& crew) {
    append_crew_model(parts, crew);
}

auto build_crew_parts(const CrewRenderInstance& crew) -> std::vector<CreaturePartInstance> {
    std::vector<CreaturePartInstance> parts {};
    parts.reserve(64U);
    append_crew_parts(parts, crew);
    return parts;
}

auto build_creature_mesh(std::span<const CreaturePartInstance> parts) -> CreatureMeshData {
    CreatureMeshData mesh {};
    mesh.vertices.reserve(parts.size() * 24U);
    mesh.indices.reserve(parts.size() * 36U);
    mesh.part_count = parts.size();

    const std::array<std::array<float, 2>, 4> kFaceUvs {{
        {1.0F, 0.0F},
        {1.0F, 1.0F},
        {0.0F, 1.0F},
        {0.0F, 0.0F},
    }};

    for (const auto& part : parts) {
        const auto normal_matrix = glm::transpose(glm::inverse(glm::mat3(part.transform)));
        for (std::size_t face_index = 0; face_index < box_faces().size(); ++face_index) {
            const auto& face = box_faces()[face_index];
            const auto& uv_rect = part.face_uvs[face_index];
            const auto face_normal = glm::normalize(normal_matrix * face.normal);
            const auto base_index = static_cast<std::uint32_t>(mesh.vertices.size());
            for (std::size_t vertex_index = 0; vertex_index < face.corners.size(); ++vertex_index) {
                const auto world_position = part.transform * glm::vec4(face.corners[vertex_index], 1.0F);
                const auto u = glm::mix(uv_rect.u0, uv_rect.u1, kFaceUvs[vertex_index][0]);
                const auto v = glm::mix(uv_rect.v0, uv_rect.v1, kFaceUvs[vertex_index][1]);
                mesh.vertices.push_back({
                    world_position.x,
                    world_position.y,
                    world_position.z,
                    u,
                    v,
                    face_normal.x,
                    face_normal.y,
                    face_normal.z,
                    part.nightmare_factor,
                    part.tension,
                    part.material_class,
                    part.cavity_mask,
                    part.emissive_strength,
                });
            }

            mesh.indices.insert(mesh.indices.end(), {
                base_index + 0U, base_index + 1U, base_index + 2U,
                base_index + 0U, base_index + 2U, base_index + 3U,
            });
        }
    }

    return mesh;
}

auto build_creature_mesh(const CreatureRenderInstance& creature) -> CreatureMeshData {
    const auto parts = build_creature_parts(creature);
    return build_creature_mesh(std::span<const CreaturePartInstance>(parts.data(), parts.size()));
}

auto build_crew_mesh(const CrewRenderInstance& crew) -> CreatureMeshData {
    const auto parts = build_crew_parts(crew);
    return build_creature_mesh(std::span<const CreaturePartInstance>(parts.data(), parts.size()));
}

} // namespace valcraft
