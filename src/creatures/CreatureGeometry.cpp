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
    const auto phase = seed_unit(creature.appearance_seed, 24) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (5.8F + motion * 4.4F) + phase);
    const auto breath = std::sin(creature.animation_time * 2.3F + phase * 0.4F) * 0.018F;
    const auto sway = std::sin(creature.animation_time * 3.1F + phase) * (0.012F + motion * 0.018F);
    const auto head_pitch = 0.02F + gaze * 0.05F - attack * 0.10F;
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
    const auto phase = seed_unit(creature.appearance_seed, 24) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (4.8F + motion * 3.8F) + phase);
    const auto breath = std::sin(creature.animation_time * 2.0F + phase * 0.3F) * 0.016F;
    const auto head_pitch = 0.01F + gaze * 0.04F - attack * 0.08F;
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
    const auto phase = seed_unit(creature.appearance_seed, 24) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (4.4F + motion * 3.6F) + phase);
    const auto breath = std::sin(creature.animation_time * 2.2F + phase * 0.5F) * 0.014F;
    const auto head_pitch = 0.05F + gaze * 0.05F - attack * 0.06F;
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

void append_night_pig(std::vector<CreaturePartInstance>& mesh,
                      const CreatureRenderInstance& creature,
                      const CreatureVisualState& state,
                      const glm::mat4& root) {
    const auto presence = state.night_presence;
    if (presence <= 1.0e-4F) {
        return;
    }

    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto attack = saturate(creature.attack_amount);
    const auto tension = saturate(creature.tension);
    const auto phase = seed_unit(creature.appearance_seed, 20) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (4.8F + motion * 4.2F) + phase);
    const auto breath = std::sin(creature.animation_time * 2.6F + phase * 0.4F) * 0.03F * presence;
    const auto body_bob = stride * (0.02F + motion * 0.03F) + breath;
    const auto body_roll = std::sin(creature.animation_time * 2.2F + phase) * 0.04F * (0.35F + tension * 0.65F);
    const auto head_pitch = -0.18F - gaze * 0.10F - attack * 0.22F;
    const auto head_yaw = std::sin(creature.animation_time * 1.8F + phase * 0.6F) * 0.05F + gaze * 0.05F;
    const auto head_roll = std::sin(creature.animation_time * 7.0F + phase) * 0.04F * presence + body_roll * 0.35F;
    const auto arm_swing = -stride * (0.22F + motion * 0.12F) - attack * 0.42F;
    const auto leg_swing = stride * (0.26F + motion * 0.10F) + attack * 0.08F;
    const auto jaw_open = 0.05F + attack * 0.12F + tension * 0.04F;
    const auto pulse = (0.78F + 0.22F * std::sin(creature.animation_time * 8.4F + phase)) * state.corruption;
    const auto scale = 0.42F + 0.58F * presence;
    const auto shoulder_span = 0.20F + seed_detail_signed(creature.appearance_seed, 1) * 0.014F;
    const auto hip_span = 0.15F + seed_detail_signed(creature.appearance_seed, 2) * 0.010F;
    const auto head_length = 0.20F + seed_detail_unit(creature.appearance_seed, 3) * 0.03F;
    const auto ear_length = 0.10F + seed_detail_unit(creature.appearance_seed, 4) * 0.02F;

    append_box(mesh, root, glm::vec3 {-0.06F, 0.98F + body_bob, 0.0F},
               glm::vec3 {0.13F * scale, 0.18F * scale, 0.12F * scale},
               glm::vec3 {0.02F, 0.0F, 0.04F}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.34F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.02F, 1.24F + body_bob, 0.0F},
               glm::vec3 {0.16F * scale, 0.20F * scale, 0.14F * scale},
               glm::vec3 {0.06F, 0.0F, 0.06F + body_roll * 0.4F}, CreatureAtlasTile::ZombieFlesh,
               presence, tension, kMaterialZombieFlesh, 0.28F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.12F, 1.50F + body_bob, 0.0F},
               glm::vec3 {0.21F * scale, 0.24F * scale, 0.17F * scale},
               glm::vec3 {0.08F, 0.0F, 0.10F + body_roll}, CreatureAtlasTile::ZombieFlesh,
               presence, tension, kMaterialZombieFlesh, 0.30F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.10F, 1.68F + body_bob, 0.0F},
               glm::vec3 {0.15F * scale, 0.10F * scale, 0.18F * scale},
               glm::vec3 {0.18F, 0.0F, 0.12F + body_roll * 0.75F}, CreatureAtlasTile::ZombieFlesh,
               presence, tension, kMaterialZombieFlesh, 0.22F, pulse * 0.10F);
    append_box(mesh, root, glm::vec3 {0.00F, 1.42F + body_bob, 0.0F},
               glm::vec3 {0.06F * scale, 0.32F * scale, 0.05F * scale},
               glm::vec3 {0.12F, 0.0F, 0.06F}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.60F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.34F, 1.70F + body_bob, 0.0F},
               glm::vec3 {0.08F * scale, 0.12F * scale, 0.06F * scale},
               glm::vec3 {head_pitch * 0.35F, head_yaw, head_roll * 0.2F}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.38F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.52F, 1.86F + body_bob, 0.0F},
               glm::vec3 {head_length * scale, 0.18F * scale, 0.15F * scale},
               glm::vec3 {head_pitch, head_yaw, head_roll}, CreatureAtlasTile::ZombieFlesh,
               presence, tension, kMaterialZombieFlesh, 0.34F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.58F, 1.74F + body_bob, 0.0F},
               glm::vec3 {0.13F * scale, 0.08F * scale, 0.11F * scale},
               glm::vec3 {head_pitch * 0.55F, head_yaw, head_roll * 0.40F}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.30F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.74F, 1.80F + body_bob, 0.0F},
               glm::vec3 {0.14F * scale, 0.05F * scale, 0.12F * scale},
               glm::vec3 {head_pitch * 0.55F, head_yaw, head_roll * 0.35F}, CreatureAtlasTile::ZombieMouth,
               presence, tension, kMaterialZombieFlesh, 0.42F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.72F, 1.66F + body_bob, 0.0F},
               glm::vec3 {0.13F * scale, 0.04F * scale, 0.11F * scale},
               glm::vec3 {head_pitch + jaw_open * 3.2F, head_yaw, head_roll * 0.25F}, CreatureAtlasTile::ZombieMouth,
               presence, tension, kMaterialZombieFlesh, 0.46F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.63F, 1.90F + body_bob, 0.0F}, 0.09F,
                glm::vec3 {0.035F * scale, 0.050F * scale, 0.022F * scale},
                glm::vec3 {head_pitch * 0.2F, head_yaw, head_roll * 0.15F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieEye, presence, tension, kMaterialZombieGlow, 0.96F, pulse);
    append_pair(mesh, root, glm::vec3 {0.79F, 1.62F + body_bob, 0.0F}, 0.10F,
                glm::vec3 {0.018F * scale, 0.07F * scale, 0.014F * scale},
                glm::vec3 {head_pitch + jaw_open * 0.8F, head_yaw, 0.08F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieTeeth, presence, tension, kMaterialZombieBone, 0.10F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.56F, 1.98F + body_bob, 0.0F}, 0.10F,
                glm::vec3 {0.03F * scale, ear_length * scale, 0.02F * scale},
                glm::vec3 {0.05F, 0.0F, -0.12F}, glm::vec3 {0.0F, 0.0F, -0.16F},
                CreatureAtlasTile::ZombieFlesh, presence, tension, kMaterialZombieFlesh, 0.24F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.78F, 1.70F + body_bob, 0.0F}, 0.11F,
                glm::vec3 {0.016F * scale, 0.07F * scale, 0.014F * scale},
                glm::vec3 {head_pitch * 0.4F, head_yaw, -0.08F}, glm::vec3 {0.0F, 0.0F, -0.04F},
                CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.18F, 0.0F);

    append_pair(mesh, root, glm::vec3 {0.10F, 1.42F + body_bob, 0.0F}, shoulder_span,
                glm::vec3 {0.062F * scale, 0.22F * scale, 0.054F * scale},
                glm::vec3 {0.02F, 0.0F, -0.44F - attack * 0.12F + arm_swing}, glm::vec3 {0.0F, 0.0F, 0.03F},
                CreatureAtlasTile::ZombieFlesh, presence, tension, kMaterialZombieFlesh, 0.18F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.24F, 0.94F + body_bob, 0.0F}, shoulder_span + 0.02F,
                glm::vec3 {0.050F * scale, 0.28F * scale, 0.044F * scale},
                glm::vec3 {-0.10F, 0.0F, -0.18F - attack * 0.40F + arm_swing * 0.8F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.26F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.34F, 0.42F + body_bob, 0.0F}, shoulder_span + 0.03F,
                glm::vec3 {0.042F * scale, 0.11F * scale, 0.036F * scale},
                glm::vec3 {0.20F, 0.0F, -0.24F - attack * 0.55F}, glm::vec3 {0.0F, 0.0F, 0.05F},
                CreatureAtlasTile::ZombieClaw, presence, tension, kMaterialKeratin, 0.22F, 0.0F);
    append_pair(mesh, root, glm::vec3 {-0.10F, 0.62F + body_bob, 0.0F}, hip_span,
                glm::vec3 {0.060F * scale, 0.31F * scale, 0.054F * scale},
                glm::vec3 {0.0F, 0.0F, leg_swing}, glm::vec3 {0.0F, 0.0F, -0.03F},
                CreatureAtlasTile::ZombieFlesh, presence, tension, kMaterialZombieFlesh, 0.16F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.05F, 0.14F + body_bob, 0.0F}, hip_span,
                glm::vec3 {0.050F * scale, 0.38F * scale, 0.044F * scale},
                glm::vec3 {0.08F, 0.0F, -0.18F - leg_swing * 0.55F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.22F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.18F, 0.06F + body_bob, 0.0F}, hip_span,
                glm::vec3 {0.10F * scale, 0.06F * scale, 0.06F * scale},
                glm::vec3 {0.0F, 0.0F, 0.03F + attack * 0.04F}, glm::vec3 {0.0F, 0.0F, -0.02F},
                CreatureAtlasTile::ZombieClaw, presence, tension, kMaterialKeratin, 0.16F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.12F, 1.34F + body_bob, 0.0F}, shoulder_span + 0.01F,
                glm::vec3 {0.028F * scale, 0.16F * scale, 0.018F * scale},
                glm::vec3 {0.0F, 0.0F, 0.08F}, glm::vec3 {0.0F, 0.0F, 0.04F},
                CreatureAtlasTile::ZombieScar, presence, tension, kMaterialZombieGlow, 0.84F, pulse * 0.45F);
    append_pair(mesh, root, glm::vec3 {0.02F, 1.14F + body_bob, 0.0F}, shoulder_span - 0.02F,
                glm::vec3 {0.022F * scale, 0.19F * scale, 0.016F * scale},
                glm::vec3 {0.0F, 0.0F, -0.03F}, glm::vec3 {0.0F, 0.0F, -0.03F},
                CreatureAtlasTile::ZombieVein, presence, tension, kMaterialZombieGlow, 0.88F, pulse * 0.80F);

    for (int index = 0; index < 3; ++index) {
        append_box(mesh, root,
                   glm::vec3 {-0.06F + static_cast<float>(index) * 0.13F, 1.44F + static_cast<float>(index) * 0.09F + body_bob, 0.0F},
                   glm::vec3 {0.022F * scale, (0.12F - static_cast<float>(index) * 0.01F) * scale, 0.028F * scale},
                   glm::vec3 {0.10F * static_cast<float>(index), 0.0F, 0.14F * (index % 2 == 0 ? 1.0F : -1.0F)},
                   CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.74F, 0.0F);
    }
}

void append_night_cow(std::vector<CreaturePartInstance>& mesh,
                      const CreatureRenderInstance& creature,
                      const CreatureVisualState& state,
                      const glm::mat4& root) {
    const auto presence = state.night_presence;
    if (presence <= 1.0e-4F) {
        return;
    }

    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto attack = saturate(creature.attack_amount);
    const auto tension = saturate(creature.tension);
    const auto phase = seed_unit(creature.appearance_seed, 20) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (4.4F + motion * 4.0F) + phase);
    const auto breath = std::sin(creature.animation_time * 2.2F + phase * 0.4F) * 0.03F * presence;
    const auto body_bob = stride * (0.024F + motion * 0.026F) + breath;
    const auto body_roll = std::sin(creature.animation_time * 1.8F + phase) * 0.03F * (0.40F + tension * 0.60F);
    const auto head_pitch = -0.14F - gaze * 0.10F - attack * 0.18F;
    const auto head_yaw = std::sin(creature.animation_time * 1.6F + phase * 0.6F) * 0.04F + gaze * 0.05F;
    const auto arm_swing = -stride * (0.20F + motion * 0.14F) - attack * 0.30F;
    const auto leg_swing = stride * (0.30F + motion * 0.12F) + attack * 0.06F;
    const auto jaw_open = 0.04F + attack * 0.10F + tension * 0.03F;
    const auto pulse = (0.80F + 0.20F * std::sin(creature.animation_time * 8.0F + phase)) * state.corruption;
    const auto scale = 0.44F + 0.56F * presence;
    const auto shoulder_span = 0.23F + seed_detail_signed(creature.appearance_seed, 1) * 0.016F;
    const auto hip_span = 0.17F + seed_detail_signed(creature.appearance_seed, 2) * 0.012F;
    const auto horn_height = 0.20F + seed_detail_unit(creature.appearance_seed, 3) * 0.04F;
    const auto horn_curl = seed_detail_signed(creature.appearance_seed, 4) * 0.08F;
    const auto muzzle_length = 0.18F + seed_detail_unit(creature.appearance_seed, 5) * 0.03F;

    append_box(mesh, root, glm::vec3 {-0.08F, 1.02F + body_bob, 0.0F},
               glm::vec3 {0.12F * scale, 0.21F * scale, 0.12F * scale},
               glm::vec3 {0.02F, 0.0F, 0.02F}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.34F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.00F, 1.28F + body_bob, 0.0F},
               glm::vec3 {0.16F * scale, 0.20F * scale, 0.15F * scale},
               glm::vec3 {0.05F, 0.0F, 0.05F}, CreatureAtlasTile::ZombieFlesh,
               presence, tension, kMaterialZombieFlesh, 0.28F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.08F, 1.58F + body_bob, 0.0F},
               glm::vec3 {0.20F * scale, 0.27F * scale, 0.17F * scale},
               glm::vec3 {0.08F, 0.0F, 0.08F + body_roll}, CreatureAtlasTile::ZombieFlesh,
               presence, tension, kMaterialZombieFlesh, 0.30F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.06F, 1.82F + body_bob, 0.0F},
               glm::vec3 {0.19F * scale, 0.11F * scale, 0.20F * scale},
               glm::vec3 {0.16F, 0.0F, 0.10F + body_roll * 0.80F}, CreatureAtlasTile::ZombieFlesh,
               presence, tension, kMaterialZombieFlesh, 0.22F, pulse * 0.10F);
    append_box(mesh, root, glm::vec3 {-0.02F, 1.48F + body_bob, 0.0F},
               glm::vec3 {0.06F * scale, 0.36F * scale, 0.05F * scale},
               glm::vec3 {0.10F, 0.0F, 0.04F}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.60F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.30F, 1.76F + body_bob, 0.0F},
               glm::vec3 {0.08F * scale, 0.13F * scale, 0.06F * scale},
               glm::vec3 {head_pitch * 0.30F, head_yaw, 0.0F}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.36F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.50F, 1.92F + body_bob, 0.0F},
               glm::vec3 {0.16F * scale, 0.18F * scale, 0.14F * scale},
               glm::vec3 {head_pitch, head_yaw, body_roll * 0.4F}, CreatureAtlasTile::ZombieFlesh,
               presence, tension, kMaterialZombieFlesh, 0.34F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.36F, 1.58F + body_bob, 0.0F},
               glm::vec3 {0.10F * scale, 0.14F * scale, 0.09F * scale},
               glm::vec3 {head_pitch * 0.18F, head_yaw, body_roll * 0.20F}, CreatureAtlasTile::ZombieFlesh,
               presence, tension, kMaterialZombieFlesh, 0.24F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.70F, 1.84F + body_bob, 0.0F},
               glm::vec3 {muzzle_length * scale, 0.05F * scale, 0.10F * scale},
               glm::vec3 {head_pitch * 0.55F, head_yaw, 0.0F}, CreatureAtlasTile::ZombieMouth,
               presence, tension, kMaterialZombieFlesh, 0.44F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.68F, 1.70F + body_bob, 0.0F},
               glm::vec3 {muzzle_length * 0.90F * scale, 0.04F * scale, 0.09F * scale},
               glm::vec3 {head_pitch + jaw_open * 3.0F, head_yaw, 0.0F}, CreatureAtlasTile::ZombieMouth,
               presence, tension, kMaterialZombieFlesh, 0.48F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.60F, 1.96F + body_bob, 0.0F}, 0.08F,
                glm::vec3 {0.030F * scale, 0.046F * scale, 0.020F * scale},
                glm::vec3 {head_pitch * 0.2F, head_yaw, 0.0F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieEye, presence, tension, kMaterialZombieGlow, 0.96F, pulse);
    append_pair(mesh, root, glm::vec3 {0.75F, 1.66F + body_bob, 0.0F}, 0.08F,
                glm::vec3 {0.018F * scale, 0.06F * scale, 0.014F * scale},
                glm::vec3 {head_pitch + jaw_open, head_yaw, 0.0F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieTeeth, presence, tension, kMaterialZombieBone, 0.10F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.56F, 2.02F + body_bob, 0.0F}, 0.11F,
                glm::vec3 {0.020F * scale, horn_height * scale, 0.020F * scale},
                glm::vec3 {-0.20F, 0.0F, -0.16F - horn_curl}, glm::vec3 {0.0F, 0.0F, -0.12F},
                CreatureAtlasTile::ZombieHorn, presence, tension, kMaterialHorn, 0.20F, 0.0F);

    append_pair(mesh, root, glm::vec3 {0.06F, 1.46F + body_bob, 0.0F}, shoulder_span,
                glm::vec3 {0.058F * scale, 0.22F * scale, 0.052F * scale},
                glm::vec3 {0.00F, 0.0F, -0.36F - attack * 0.10F + arm_swing}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieFlesh, presence, tension, kMaterialZombieFlesh, 0.18F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.18F, 0.98F + body_bob, 0.0F}, shoulder_span + 0.01F,
                glm::vec3 {0.048F * scale, 0.28F * scale, 0.042F * scale},
                glm::vec3 {-0.08F, 0.0F, -0.16F - attack * 0.22F + arm_swing * 0.75F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.26F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.28F, 0.46F + body_bob, 0.0F}, shoulder_span + 0.01F,
                glm::vec3 {0.044F * scale, 0.11F * scale, 0.038F * scale},
                glm::vec3 {0.10F, 0.0F, -0.12F - attack * 0.18F}, glm::vec3 {0.0F, 0.0F, 0.04F},
                CreatureAtlasTile::ZombieClaw, presence, tension, kMaterialKeratin, 0.20F, 0.0F);
    append_pair(mesh, root, glm::vec3 {-0.10F, 0.68F + body_bob, 0.0F}, hip_span,
                glm::vec3 {0.058F * scale, 0.35F * scale, 0.054F * scale},
                glm::vec3 {0.0F, 0.0F, leg_swing}, glm::vec3 {0.0F, 0.0F, -0.02F},
                CreatureAtlasTile::ZombieFlesh, presence, tension, kMaterialZombieFlesh, 0.16F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.06F, 0.18F + body_bob, 0.0F}, hip_span,
                glm::vec3 {0.050F * scale, 0.42F * scale, 0.044F * scale},
                glm::vec3 {0.10F, 0.0F, -0.18F - leg_swing * 0.55F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.22F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.18F, 0.06F + body_bob, 0.0F}, hip_span,
                glm::vec3 {0.095F * scale, 0.06F * scale, 0.065F * scale},
                glm::vec3 {0.0F, 0.0F, 0.02F}, glm::vec3 {0.0F, 0.0F, -0.02F},
                CreatureAtlasTile::ZombieHorn, presence, tension, kMaterialHorn, 0.16F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.10F, 1.38F + body_bob, 0.0F}, shoulder_span,
                glm::vec3 {0.025F * scale, 0.17F * scale, 0.018F * scale},
                glm::vec3 {0.0F, 0.0F, 0.08F}, glm::vec3 {0.0F, 0.0F, 0.03F},
                CreatureAtlasTile::ZombieScar, presence, tension, kMaterialZombieGlow, 0.84F, pulse * 0.40F);
    append_pair(mesh, root, glm::vec3 {-0.02F, 1.20F + body_bob, 0.0F}, shoulder_span - 0.02F,
                glm::vec3 {0.022F * scale, 0.18F * scale, 0.016F * scale},
                glm::vec3 {0.0F, 0.0F, -0.04F}, glm::vec3 {0.0F, 0.0F, -0.02F},
                CreatureAtlasTile::ZombieVein, presence, tension, kMaterialZombieGlow, 0.88F, pulse * 0.78F);

    for (int index = 0; index < 3; ++index) {
        append_box(mesh, root,
                   glm::vec3 {-0.08F + static_cast<float>(index) * 0.14F, 1.50F + static_cast<float>(index) * 0.10F + body_bob, 0.0F},
                   glm::vec3 {0.024F * scale, (0.13F - static_cast<float>(index) * 0.01F) * scale, 0.028F * scale},
                   glm::vec3 {0.08F * static_cast<float>(index), 0.0F, 0.12F * (index % 2 == 0 ? 1.0F : -1.0F)},
                   CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.74F, 0.0F);
    }
}

void append_night_sheep(std::vector<CreaturePartInstance>& mesh,
                        const CreatureRenderInstance& creature,
                        const CreatureVisualState& state,
                        const glm::mat4& root) {
    const auto presence = state.night_presence;
    if (presence <= 1.0e-4F) {
        return;
    }

    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto attack = saturate(creature.attack_amount);
    const auto tension = saturate(creature.tension);
    const auto phase = seed_unit(creature.appearance_seed, 20) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (4.9F + motion * 4.0F) + phase);
    const auto breath = std::sin(creature.animation_time * 2.5F + phase * 0.4F) * 0.03F * presence;
    const auto body_bob = stride * (0.022F + motion * 0.028F) + breath;
    const auto body_roll = std::sin(creature.animation_time * 2.1F + phase) * 0.03F * (0.45F + tension * 0.55F);
    const auto head_pitch = -0.20F - gaze * 0.12F - attack * 0.18F;
    const auto head_yaw = std::sin(creature.animation_time * 1.8F + phase * 0.6F) * 0.04F + gaze * 0.06F;
    const auto arm_swing = -stride * (0.24F + motion * 0.14F) - attack * 0.34F;
    const auto leg_swing = stride * (0.32F + motion * 0.14F) + attack * 0.06F;
    const auto jaw_open = 0.05F + attack * 0.10F + tension * 0.04F;
    const auto pulse = (0.80F + 0.20F * std::sin(creature.animation_time * 8.6F + phase)) * state.corruption;
    const auto scale = 0.42F + 0.58F * presence;
    const auto shoulder_span = 0.19F + seed_detail_signed(creature.appearance_seed, 1) * 0.014F;
    const auto hip_span = 0.15F + seed_detail_signed(creature.appearance_seed, 2) * 0.010F;
    const auto head_length = 0.22F + seed_detail_unit(creature.appearance_seed, 3) * 0.03F;
    const auto mane_height = 0.18F + seed_detail_unit(creature.appearance_seed, 4) * 0.03F;

    append_box(mesh, root, glm::vec3 {-0.06F, 1.00F + body_bob, 0.0F},
               glm::vec3 {0.11F * scale, 0.18F * scale, 0.11F * scale},
               glm::vec3 {0.02F, 0.0F, 0.04F}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.34F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.02F, 1.24F + body_bob, 0.0F},
               glm::vec3 {0.15F * scale, 0.19F * scale, 0.14F * scale},
               glm::vec3 {0.05F, 0.0F, 0.06F}, CreatureAtlasTile::ZombieFlesh,
               presence, tension, kMaterialZombieFlesh, 0.28F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.10F, 1.52F + body_bob, 0.0F},
               glm::vec3 {0.18F * scale, 0.26F * scale, 0.16F * scale},
               glm::vec3 {0.08F, 0.0F, 0.10F + body_roll}, CreatureAtlasTile::ZombieFlesh,
               presence, tension, kMaterialZombieFlesh, 0.30F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.02F, 1.42F + body_bob, 0.0F},
               glm::vec3 {0.05F * scale, 0.34F * scale, 0.05F * scale},
               glm::vec3 {0.10F, 0.0F, 0.04F}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.60F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.04F, 1.72F + body_bob, 0.0F},
               glm::vec3 {0.20F * scale, mane_height * scale, 0.22F * scale},
               glm::vec3 {0.06F, 0.0F, 0.04F}, CreatureAtlasTile::ZombieWool,
               presence, tension, kMaterialWool, 0.24F, pulse * 0.16F);
    append_box(mesh, root, glm::vec3 {-0.04F, 1.42F + body_bob, 0.0F},
               glm::vec3 {0.17F * scale, 0.12F * scale, 0.20F * scale},
               glm::vec3 {0.04F, 0.0F, 0.05F + body_roll * 0.45F}, CreatureAtlasTile::ZombieWool,
               presence, tension, kMaterialWool, 0.18F, pulse * 0.10F);
    append_box(mesh, root, glm::vec3 {0.30F, 1.72F + body_bob, 0.0F},
               glm::vec3 {0.07F * scale, 0.12F * scale, 0.05F * scale},
               glm::vec3 {head_pitch * 0.35F, head_yaw, 0.0F}, CreatureAtlasTile::ZombieBone,
               presence, tension, kMaterialZombieBone, 0.36F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.52F, 1.86F + body_bob, 0.0F},
               glm::vec3 {head_length * scale, 0.15F * scale, 0.12F * scale},
               glm::vec3 {head_pitch, head_yaw, body_roll * 0.25F}, CreatureAtlasTile::ZombieFlesh,
               presence, tension, kMaterialZombieFlesh, 0.36F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.74F, 1.76F + body_bob, 0.0F},
               glm::vec3 {0.13F * scale, 0.05F * scale, 0.08F * scale},
               glm::vec3 {head_pitch * 0.55F, head_yaw, 0.0F}, CreatureAtlasTile::ZombieMouth,
               presence, tension, kMaterialZombieFlesh, 0.46F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.72F, 1.64F + body_bob, 0.0F},
               glm::vec3 {0.12F * scale, 0.04F * scale, 0.07F * scale},
               glm::vec3 {head_pitch + jaw_open * 3.2F, head_yaw, 0.0F}, CreatureAtlasTile::ZombieMouth,
               presence, tension, kMaterialZombieFlesh, 0.48F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.64F, 1.90F + body_bob, 0.0F}, 0.07F,
                glm::vec3 {0.028F * scale, 0.048F * scale, 0.018F * scale},
                glm::vec3 {head_pitch * 0.2F, head_yaw, 0.0F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieEye, presence, tension, kMaterialZombieGlow, 0.96F, pulse);
    append_pair(mesh, root, glm::vec3 {0.78F, 1.60F + body_bob, 0.0F}, 0.07F,
                glm::vec3 {0.014F * scale, 0.06F * scale, 0.012F * scale},
                glm::vec3 {head_pitch + jaw_open, head_yaw, 0.0F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieTeeth, presence, tension, kMaterialZombieBone, 0.10F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.48F, 1.86F + body_bob, 0.0F}, 0.09F,
                glm::vec3 {0.022F * scale, 0.12F * scale, 0.016F * scale},
                glm::vec3 {0.02F, 0.0F, 0.08F}, glm::vec3 {0.0F, 0.0F, -0.04F},
                CreatureAtlasTile::ZombieWool, presence, tension, kMaterialWool, 0.20F, pulse * 0.12F);

    append_pair(mesh, root, glm::vec3 {0.08F, 1.44F + body_bob, 0.0F}, shoulder_span,
                glm::vec3 {0.054F * scale, 0.22F * scale, 0.046F * scale},
                glm::vec3 {0.0F, 0.0F, -0.48F - attack * 0.10F + arm_swing}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieFlesh, presence, tension, kMaterialZombieFlesh, 0.18F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.22F, 0.92F + body_bob, 0.0F}, shoulder_span + 0.02F,
                glm::vec3 {0.044F * scale, 0.30F * scale, 0.038F * scale},
                glm::vec3 {-0.10F, 0.0F, -0.20F - attack * 0.24F + arm_swing * 0.80F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.26F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.32F, 0.38F + body_bob, 0.0F}, shoulder_span + 0.03F,
                glm::vec3 {0.038F * scale, 0.13F * scale, 0.032F * scale},
                glm::vec3 {0.18F, 0.0F, -0.18F - attack * 0.24F}, glm::vec3 {0.0F, 0.0F, 0.04F},
                CreatureAtlasTile::ZombieClaw, presence, tension, kMaterialKeratin, 0.20F, 0.0F);
    append_pair(mesh, root, glm::vec3 {-0.10F, 0.64F + body_bob, 0.0F}, hip_span,
                glm::vec3 {0.056F * scale, 0.34F * scale, 0.050F * scale},
                glm::vec3 {0.0F, 0.0F, leg_swing}, glm::vec3 {0.0F, 0.0F, -0.02F},
                CreatureAtlasTile::ZombieFlesh, presence, tension, kMaterialZombieFlesh, 0.16F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.06F, 0.16F + body_bob, 0.0F}, hip_span,
                glm::vec3 {0.048F * scale, 0.40F * scale, 0.040F * scale},
                glm::vec3 {0.10F, 0.0F, -0.20F - leg_swing * 0.55F}, glm::vec3 {0.0F, 0.0F, 0.02F},
                CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.22F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.18F, 0.06F + body_bob, 0.0F}, hip_span,
                glm::vec3 {0.09F * scale, 0.06F * scale, 0.05F * scale},
                glm::vec3 {0.0F, 0.0F, 0.02F}, glm::vec3 {0.0F, 0.0F, -0.02F},
                CreatureAtlasTile::ZombieClaw, presence, tension, kMaterialKeratin, 0.16F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.10F, 1.40F + body_bob, 0.0F}, shoulder_span,
                glm::vec3 {0.024F * scale, 0.18F * scale, 0.016F * scale},
                glm::vec3 {0.0F, 0.0F, 0.08F}, glm::vec3 {0.0F, 0.0F, 0.03F},
                CreatureAtlasTile::ZombieScar, presence, tension, kMaterialZombieGlow, 0.84F, pulse * 0.42F);
    append_pair(mesh, root, glm::vec3 {-0.02F, 1.18F + body_bob, 0.0F}, shoulder_span - 0.02F,
                glm::vec3 {0.020F * scale, 0.20F * scale, 0.014F * scale},
                glm::vec3 {0.0F, 0.0F, -0.05F}, glm::vec3 {0.0F, 0.0F, -0.02F},
                CreatureAtlasTile::ZombieVein, presence, tension, kMaterialZombieGlow, 0.88F, pulse * 0.78F);

    for (int index = 0; index < 3; ++index) {
        append_box(mesh, root,
                   glm::vec3 {-0.06F + static_cast<float>(index) * 0.12F, 1.50F + static_cast<float>(index) * 0.09F + body_bob, 0.0F},
                   glm::vec3 {0.022F * scale, (0.12F - static_cast<float>(index) * 0.01F) * scale, 0.026F * scale},
                   glm::vec3 {0.08F * static_cast<float>(index), 0.0F, 0.10F * (index % 2 == 0 ? 1.0F : -1.0F)},
                   CreatureAtlasTile::ZombieBone, presence, tension, kMaterialZombieBone, 0.74F, 0.0F);
    }
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
    const auto phase = seed_unit(creature.appearance_seed, 22) * kTwoPi;
    const auto stride_wave = std::sin(creature.animation_time * (4.8F + motion * 3.6F) + phase);
    const auto stride = stride_wave * motion;
    const auto breath = std::sin(creature.animation_time * 2.0F + phase * 0.35F) * 0.018F;
    const auto body_bob = motion * 0.012F * std::sin(creature.animation_time * 8.0F + phase);
    const auto head_pitch = 0.012F + gaze * 0.045F;
    const auto head_yaw = std::sin(creature.animation_time * 1.2F + phase) * 0.025F + gaze * 0.070F;
    const auto arm_swing = stride * (0.10F + motion * 0.08F);
    const auto folded_sway = stride * motion * 0.035F;
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

    append_pair(mesh, root, glm::vec3 {0.0F, 1.17F + body_bob, 0.0F}, shoulder_span,
                upper_arm_half,
                glm::vec3 {0.0F, 0.0F, -0.05F + arm_swing}, glm::vec3 {0.0F, 0.0F, 0.025F},
                CreatureAtlasTile::VillagerCloth, state.morph, tension, kMaterialHide, 0.12F, 0.0F);
    append_box(mesh, root, glm::vec3 {torso_half.x + folded_arm_half.x * 0.82F, 1.19F + body_bob, 0.055F * scale + folded_sway},
               folded_arm_half, glm::vec3 {0.0F, 0.0F, 0.12F}, CreatureAtlasTile::VillagerCloth,
               state.morph, tension, kMaterialHide, 0.12F, 0.0F);
    append_box(mesh, root, glm::vec3 {torso_half.x + folded_arm_half.x * 0.82F, 1.10F + body_bob, -0.055F * scale - folded_sway},
               folded_arm_half, glm::vec3 {0.0F, 0.0F, -0.12F}, CreatureAtlasTile::VillagerCloth,
               state.morph, tension, kMaterialHide, 0.12F, 0.0F);
    append_pair(mesh, root, glm::vec3 {torso_half.x + 0.105F * scale, 1.14F + body_bob, 0.0F}, 0.115F * scale,
                hand_half,
                glm::vec3 {0.0F, 0.0F, 0.0F}, glm::vec3 {0.0F, 0.0F, 0.04F},
                CreatureAtlasTile::VillagerSkin, state.morph, tension, kMaterialSkin, 0.10F, 0.0F);

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
        const auto sick = 0.5F + 0.5F * std::sin(nx * 8.0F + ny * 6.0F + grain * 5.0F);
        const auto sinew = 0.5F + 0.5F * std::sin(nx * 18.0F + ny * 4.0F + soft_grain * 7.0F);
        const auto pallor = radial_falloff(nx, ny, 0.48F, 0.36F, 0.72F);
        return make_rgba(112.0F + sinew * 18.0F + pallor * 10.0F + grain * 8.0F - bruise * 10.0F,
                         128.0F + soft_grain * 16.0F + pallor * 10.0F - bruise * 10.0F,
                         102.0F + sick * 18.0F + pallor * 6.0F - bruise * 8.0F,
                         0.0F);
    }
    case CreatureAtlasTile::ZombieBone: {
        const auto crack = (x + y) % 7 == 0 ? 28.0F : 0.0F;
        const auto ridge = 0.5F + 0.5F * std::sin(nx * 10.0F + ny * 6.0F + grain * 5.0F);
        return make_rgba(210.0F + ridge * 18.0F - crack + grain * 8.0F,
                         198.0F + ridge * 16.0F - crack * 0.7F + soft_grain * 8.0F,
                         176.0F + ridge * 12.0F - crack * 0.6F + grain * 6.0F,
                         0.0F);
    }
    case CreatureAtlasTile::ZombieMouth: {
        const auto wet = radial_falloff(nx, ny, 0.46F, 0.44F, 0.64F);
        return make_rgba(96.0F + wet * 18.0F,
                         22.0F + grain * 10.0F,
                         18.0F + soft_grain * 8.0F,
                         0.0F);
    }
    case CreatureAtlasTile::ZombieTeeth: {
        const auto enamel = 0.6F + 0.4F * std::sin(nx * 16.0F);
        return make_rgba(236.0F + enamel * 10.0F,
                         228.0F + grain * 10.0F,
                         198.0F + soft_grain * 12.0F,
                         0.0F);
    }
    case CreatureAtlasTile::ZombieEye: {
        const auto sclera = radial_falloff(nx, ny, 0.5F, 0.5F, 0.46F);
        const auto pupil = radial_falloff(nx, ny, 0.52F, 0.52F, 0.12F);
        const auto iris = radial_falloff(nx, ny, 0.50F, 0.50F, 0.20F) * (1.0F - pupil);
        const auto ember = radial_falloff(nx, ny, 0.50F, 0.50F, 0.28F) * (1.0F - pupil * 0.55F);
        const auto alpha = 116.0F + sclera * 132.0F;
        return make_rgba(232.0F + sclera * 18.0F + ember * 12.0F - pupil * 188.0F,
                         144.0F + ember * 84.0F + iris * 32.0F - pupil * 138.0F,
                         84.0F + ember * 48.0F - pupil * 104.0F,
                         alpha);
    }
    case CreatureAtlasTile::ZombieVein: {
        const auto wave = std::sin(nx * 15.0F + ny * 4.0F + grain * 6.0F) + 0.45F * std::sin(nx * 5.0F - ny * 14.0F + soft_grain * 5.0F);
        const auto mask = smooth_range(0.72F, 0.92F, std::abs(wave));
        return make_rgba(172.0F + mask * 34.0F,
                         42.0F + mask * 24.0F,
                         34.0F + mask * 16.0F,
                         mask * (48.0F + soft_grain * 108.0F));
    }
    case CreatureAtlasTile::ZombieScar: {
        const auto line_a = std::abs((nx - 0.24F) * 0.70F + (ny - 0.54F)) < 0.05F;
        const auto line_b = std::abs((nx - 0.66F) * 0.80F - (ny - 0.34F)) < 0.05F;
        const auto line_c = std::abs((nx - 0.44F) * 0.28F + (ny - 0.72F)) < 0.04F;
        const auto mask = (line_a || line_b || line_c) ? 1.0F : 0.0F;
        return make_rgba(198.0F + grain * 16.0F,
                         54.0F + soft_grain * 16.0F,
                         42.0F + grain * 10.0F,
                         mask * 168.0F);
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
        return make_rgba(214.0F + fissure * 28.0F,
                         84.0F + fissure * 44.0F,
                         42.0F + fissure * 16.0F,
                         fissure * (74.0F + soft_grain * 120.0F));
    }
    case CreatureAtlasTile::ZombieClaw: {
        const auto highlight = 0.5F + 0.5F * std::sin(nx * 7.0F + ny * 3.0F + grain * 4.0F);
        return make_rgba(52.0F + highlight * 20.0F, 44.0F + highlight * 16.0F, 54.0F + highlight * 20.0F, 0.0F);
    }
    case CreatureAtlasTile::ZombieWool: {
        const auto curl = std::sin(nx * 16.0F + grain * 5.0F) * std::sin(ny * 12.0F + soft_grain * 4.0F);
        const auto stain = radial_falloff(nx, ny, 0.60F, 0.64F, 0.32F);
        return make_rgba(188.0F + curl * 14.0F,
                         178.0F + grain * 12.0F - stain * 22.0F,
                         168.0F + soft_grain * 12.0F - stain * 18.0F,
                         0.0F);
    }
    case CreatureAtlasTile::ZombieHorn: {
        const auto gradient = ny;
        return make_rgba(146.0F - gradient * 42.0F,
                         128.0F - gradient * 38.0F,
                         102.0F - gradient * 34.0F,
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
    mesh.reserve(96U);

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

} // namespace valcraft
