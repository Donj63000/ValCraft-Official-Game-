#include "creatures/CreatureGeometry.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
constexpr float kMaterialZombieFlesh = 0.72F;
constexpr float kMaterialZombieBone = 0.88F;
constexpr float kMaterialZombieGlow = 1.0F;
constexpr std::array<float, 2> kSides {{-1.0F, 1.0F}};

struct FaceDefinition {
    std::array<glm::vec3, 4> corners;
    glm::vec3 normal;
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

void append_box(CreatureMeshData& mesh,
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

    const auto transform = make_transform(root, center, rotation_radians, half_extent);
    const auto normal_matrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    const auto tile_coordinates = creature_atlas_tile_coordinates(tile);
    const auto uv_step = 1.0F / kCreatureAtlasTilesPerAxis;
    const auto u0 = static_cast<float>(tile_coordinates[0]) * uv_step;
    const auto v0 = static_cast<float>(tile_coordinates[1]) * uv_step;
    const auto u1 = u0 + uv_step;
    const auto v1 = v0 + uv_step;
    const std::array<std::array<float, 2>, 4> uvs {{
        {u1, v0},
        {u1, v1},
        {u0, v1},
        {u0, v0},
    }};

    for (const auto& face : box_faces()) {
        const auto face_normal = glm::normalize(normal_matrix * face.normal);
        const auto base_index = static_cast<std::uint32_t>(mesh.vertices.size());
        for (std::size_t vertex_index = 0; vertex_index < face.corners.size(); ++vertex_index) {
            const auto world_position = transform * glm::vec4(face.corners[vertex_index], 1.0F);
            mesh.vertices.push_back({
                world_position.x,
                world_position.y,
                world_position.z,
                uvs[vertex_index][0],
                uvs[vertex_index][1],
                face_normal.x,
                face_normal.y,
                face_normal.z,
                nightmare_factor,
                tension,
                material_class,
                cavity_mask,
                emissive_strength,
            });
        }

        mesh.indices.insert(mesh.indices.end(), {
            base_index + 0U, base_index + 1U, base_index + 2U,
            base_index + 0U, base_index + 2U, base_index + 3U,
        });
    }

    ++mesh.part_count;
}

void append_pair(CreatureMeshData& mesh,
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
            mesh,
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

void append_zombie_teeth(CreatureMeshData& mesh,
                         const glm::mat4& root,
                         float morph_factor,
                         float tension,
                         float attack_amount) {
    if (morph_factor <= 1.0e-4F) {
        return;
    }

    const auto height = morph_factor * (0.035F + attack_amount * 0.012F);
    for (const auto z : std::array<float, 4> {{-0.10F, -0.03F, 0.03F, 0.10F}}) {
        append_box(mesh, root, glm::vec3 {0.59F, 1.07F + attack_amount * 0.01F, z},
                   glm::vec3 {0.015F * morph_factor, height, 0.012F * morph_factor}, glm::vec3 {0.0F},
                   CreatureAtlasTile::ZombieTeeth, morph_factor, tension, kMaterialZombieBone, 0.10F, 0.0F);
        append_box(mesh, root, glm::vec3 {0.57F, 0.99F - attack_amount * 0.03F, z},
                   glm::vec3 {0.014F * morph_factor, height * 0.95F, 0.011F * morph_factor},
                   glm::vec3 {attack_amount * 0.25F, 0.0F, 0.0F},
                   CreatureAtlasTile::ZombieTeeth, morph_factor, tension, kMaterialZombieBone, 0.10F, 0.0F);
    }
}

void append_zombie_overlay(CreatureMeshData& mesh,
                           const CreatureRenderInstance& creature,
                           const glm::mat4& root,
                           float shoulder_span,
                           float hip_span,
                           float torso_depth) {
    const auto morph = saturate(creature.morph_factor);
    if (morph <= 1.0e-4F) {
        return;
    }

    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto attack = saturate(creature.attack_amount);
    const auto tension = saturate(creature.tension);
    const auto phase = seed_unit(creature.appearance_seed, 20) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (4.8F + motion * 4.6F) + phase);
    const auto arm_swing = stride * (0.12F + motion * 0.16F) + attack * 0.42F;
    const auto leg_swing = stride * (0.18F + motion * 0.14F);
    const auto head_pitch = -0.10F - gaze * 0.05F - attack * 0.18F;
    const auto head_roll = std::sin(creature.animation_time * 12.0F + phase) * morph * (0.03F + tension * 0.04F);
    const auto head_yaw = std::sin(creature.animation_time * 2.8F + phase * 0.3F) * (0.02F + gaze * 0.04F);
    const auto jaw_open = morph * (0.03F + attack * 0.08F);

    append_box(mesh, root, glm::vec3 {0.02F, 0.95F, 0.0F}, glm::vec3 {0.17F * morph, 0.34F * morph, torso_depth * morph},
               glm::vec3 {0.0F, 0.0F, 0.02F + tension * 0.05F}, CreatureAtlasTile::ZombieFlesh, morph, tension,
               kMaterialZombieFlesh, 0.28F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.02F, 0.98F, 0.0F}, glm::vec3 {0.14F * morph, 0.28F * morph, (torso_depth - 0.02F) * morph},
               glm::vec3 {0.0F, 0.0F, 0.01F}, CreatureAtlasTile::ZombieBone, morph, tension, kMaterialZombieBone, 0.62F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.40F, 1.20F, 0.0F}, glm::vec3 {0.14F * morph, 0.18F * morph, 0.14F * morph},
               glm::vec3 {head_pitch, head_yaw, head_roll}, CreatureAtlasTile::ZombieFlesh, morph, tension,
               kMaterialZombieFlesh, 0.32F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.56F, 1.09F + attack * 0.01F, 0.0F}, glm::vec3 {0.08F * morph, 0.032F * morph, 0.15F * morph},
               glm::vec3 {head_pitch * 0.75F, head_yaw, head_roll * 0.35F}, CreatureAtlasTile::ZombieMouth, morph, tension,
               kMaterialZombieFlesh, 0.48F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.54F, 1.00F - attack * 0.03F, 0.0F}, glm::vec3 {0.074F * morph, 0.028F * morph, 0.14F * morph},
               glm::vec3 {head_pitch + jaw_open * 3.4F, head_yaw, head_roll * 0.28F}, CreatureAtlasTile::ZombieMouth, morph, tension,
               kMaterialZombieFlesh, 0.46F, 0.0F);
    append_zombie_teeth(mesh, root, morph, tension, attack);
    append_pair(mesh, root, glm::vec3 {0.46F, 1.20F, 0.0F}, 0.085F + attack * 0.01F,
                glm::vec3 {(0.040F + attack * 0.008F) * morph, 0.055F * morph, 0.028F * morph},
                glm::vec3 {head_pitch * 0.40F, head_yaw, head_roll * 0.20F}, glm::vec3 {0.0F, 0.0F, 0.04F},
                CreatureAtlasTile::ZombieEye, morph, tension, kMaterialZombieGlow, 0.92F, 1.0F);
    append_pair(mesh, root, glm::vec3 {0.02F, 0.85F, 0.0F}, shoulder_span, glm::vec3 {0.050F * morph, 0.40F * morph, 0.045F * morph},
                glm::vec3 {0.0F, 0.0F, -0.22F - attack * 0.46F - arm_swing}, glm::vec3 {0.0F, 0.0F, 0.04F},
                CreatureAtlasTile::ZombieFlesh, morph, tension, kMaterialZombieFlesh, 0.28F, 0.0F);
    append_pair(mesh, root, glm::vec3 {-0.14F, 0.38F, 0.0F}, hip_span, glm::vec3 {0.058F * morph, 0.42F * morph, 0.052F * morph},
                glm::vec3 {0.0F, 0.0F, leg_swing * 0.85F}, glm::vec3 {0.0F, 0.0F, -0.03F},
                CreatureAtlasTile::ZombieBone, morph, tension, kMaterialZombieBone, 0.24F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.10F, 0.92F, 0.0F}, shoulder_span + 0.01F, glm::vec3 {0.034F * morph, 0.09F * morph, 0.018F * morph},
                glm::vec3 {0.0F}, glm::vec3 {0.0F, 0.0F, 0.11F}, CreatureAtlasTile::ZombieScar, morph, tension, kMaterialZombieGlow, 0.86F, 1.0F);
    append_pair(mesh, root, glm::vec3 {-0.04F, 1.01F, 0.0F}, shoulder_span - 0.02F, glm::vec3 {0.028F * morph, 0.15F * morph, 0.014F * morph},
                glm::vec3 {0.0F}, glm::vec3 {0.0F, 0.0F, -0.05F}, CreatureAtlasTile::ZombieVein, morph, tension, kMaterialZombieGlow, 0.80F, 0.84F);
    for (int index = 0; index < 3; ++index) {
        append_box(mesh, root, glm::vec3 {-0.08F + static_cast<float>(index) * 0.13F, 1.02F + static_cast<float>(index) * 0.02F, 0.0F},
                   glm::vec3 {0.018F * morph, (0.09F - static_cast<float>(index) * 0.01F) * morph, 0.025F * morph},
                   glm::vec3 {0.05F * static_cast<float>(index), 0.0F, 0.03F * static_cast<float>(index % 2 == 0 ? 1 : -1)},
                   CreatureAtlasTile::ZombieBone, morph, tension, kMaterialZombieBone, 0.74F, 0.0F);
    }
}

void append_day_pig(CreatureMeshData& mesh, const CreatureRenderInstance& creature, const glm::mat4& root) {
    const auto morph = saturate(creature.morph_factor);
    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto attack = saturate(creature.attack_amount);
    const auto tension = saturate(creature.tension);
    const auto day_scale = 1.0F - morph * 0.55F;
    const auto phase = seed_unit(creature.appearance_seed, 24) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (5.4F + motion * 4.8F) + phase);
    const auto breath = std::sin(creature.animation_time * 2.0F + phase * 0.4F) * 0.018F;
    const auto head_pitch = gaze * 0.05F - attack * 0.08F;
    const auto head_yaw = std::sin(creature.animation_time * 1.8F + phase) * 0.03F + gaze * 0.02F;
    const auto ear_tilt = 0.10F + std::sin(creature.animation_time * 3.6F + phase) * 0.05F;
    const auto front_stride = -stride * (0.10F + motion * 0.10F);
    const auto rear_stride = stride * (0.12F + motion * 0.14F);

    append_box(mesh, root, glm::vec3 {-0.02F, 0.72F + breath, 0.0F}, glm::vec3 {0.44F * day_scale, 0.24F * day_scale, 0.23F * day_scale},
               glm::vec3 {0.0F, 0.0F, 0.02F}, CreatureAtlasTile::PigHide, morph, tension, kMaterialHide, 0.14F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.46F, 0.80F + breath * 0.4F, 0.0F}, glm::vec3 {0.20F * day_scale, 0.17F * day_scale, 0.15F * day_scale},
               glm::vec3 {head_pitch, head_yaw, 0.0F}, CreatureAtlasTile::PigHide, morph, tension, kMaterialHide, 0.18F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.70F, 0.72F, 0.0F}, glm::vec3 {0.09F * day_scale, 0.07F * day_scale, 0.10F * day_scale},
               glm::vec3 {head_pitch * 0.4F, head_yaw, 0.0F}, CreatureAtlasTile::PigSnout, morph, tension, kMaterialSkin, 0.08F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.42F, 0.98F, 0.0F}, 0.11F, glm::vec3 {0.04F * day_scale, 0.06F * day_scale, 0.03F * day_scale},
                glm::vec3 {-0.06F, 0.0F, 0.02F}, glm::vec3 {0.0F, 0.0F, -ear_tilt}, CreatureAtlasTile::PigEar,
                morph, tension, kMaterialSkin, 0.05F, 0.0F);

    for (const auto side : kSides) {
        append_box(mesh, root, glm::vec3 {0.22F, 0.26F, side * 0.14F}, glm::vec3 {0.05F * day_scale, 0.23F * day_scale, 0.05F * day_scale},
                   glm::vec3 {0.0F, 0.0F, front_stride}, CreatureAtlasTile::PigHide, morph, tension, kMaterialSkin, 0.08F, 0.0F);
        append_box(mesh, root, glm::vec3 {-0.24F, 0.26F, side * 0.14F}, glm::vec3 {0.055F * day_scale, 0.24F * day_scale, 0.05F * day_scale},
                   glm::vec3 {0.0F, 0.0F, rear_stride}, CreatureAtlasTile::PigHide, morph, tension, kMaterialSkin, 0.08F, 0.0F);
    }

    append_zombie_overlay(mesh, creature, root, 0.19F, 0.16F, 0.12F);
}

void append_day_cow(CreatureMeshData& mesh, const CreatureRenderInstance& creature, const glm::mat4& root) {
    const auto morph = saturate(creature.morph_factor);
    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto attack = saturate(creature.attack_amount);
    const auto tension = saturate(creature.tension);
    const auto day_scale = 1.0F - morph * 0.58F;
    const auto phase = seed_unit(creature.appearance_seed, 24) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (4.7F + motion * 4.0F) + phase);
    const auto head_pitch = gaze * 0.04F - attack * 0.06F;
    const auto head_yaw = std::sin(creature.animation_time * 1.5F + phase) * 0.02F + gaze * 0.02F;
    const auto horn_curl = seed_detail_signed(creature.appearance_seed, 2) * 0.05F;

    append_box(mesh, root, glm::vec3 {-0.05F, 0.82F, 0.0F}, glm::vec3 {0.56F * day_scale, 0.28F * day_scale, 0.26F * day_scale},
               glm::vec3 {0.0F, 0.0F, 0.01F}, CreatureAtlasTile::CowHide, morph, tension, kMaterialHide, 0.16F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.48F, 0.86F, 0.0F}, glm::vec3 {0.22F * day_scale, 0.18F * day_scale, 0.16F * day_scale},
               glm::vec3 {head_pitch, head_yaw, 0.0F}, CreatureAtlasTile::CowHide, morph, tension, kMaterialHide, 0.18F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.74F, 0.76F, 0.0F}, glm::vec3 {0.09F * day_scale, 0.07F * day_scale, 0.11F * day_scale},
               glm::vec3 {head_pitch * 0.35F, head_yaw, 0.0F}, CreatureAtlasTile::CowMuzzle, morph, tension, kMaterialSkin, 0.10F, 0.0F);
    append_pair(mesh, root, glm::vec3 {0.50F, 1.06F, 0.0F}, 0.12F, glm::vec3 {0.025F * day_scale, 0.10F * day_scale, 0.025F * day_scale},
                glm::vec3 {-0.03F, 0.0F, 0.0F}, glm::vec3 {0.0F, 0.0F, -0.12F - horn_curl}, CreatureAtlasTile::CowHorn,
                morph, tension, kMaterialHorn, 0.08F, 0.0F);

    for (const auto side : kSides) {
        const auto front_stride = -side * stride * (0.08F + motion * 0.10F);
        const auto rear_stride = side * stride * (0.10F + motion * 0.12F);
        append_box(mesh, root, glm::vec3 {0.26F, 0.31F, side * 0.15F}, glm::vec3 {0.055F * day_scale, 0.28F * day_scale, 0.055F * day_scale},
                   glm::vec3 {0.0F, 0.0F, front_stride}, CreatureAtlasTile::CowMuzzle, morph, tension, kMaterialSkin, 0.06F, 0.0F);
        append_box(mesh, root, glm::vec3 {-0.28F, 0.31F, side * 0.16F}, glm::vec3 {0.06F * day_scale, 0.29F * day_scale, 0.055F * day_scale},
                   glm::vec3 {0.0F, 0.0F, rear_stride}, CreatureAtlasTile::CowHide, morph, tension, kMaterialSkin, 0.08F, 0.0F);
    }

    append_box(mesh, root, glm::vec3 {-0.60F, 0.92F, 0.0F}, glm::vec3 {0.04F * day_scale, 0.20F * day_scale, 0.04F * day_scale},
               glm::vec3 {0.0F, 0.0F, 0.10F + stride * 0.04F}, CreatureAtlasTile::CowHide, morph, tension, kMaterialSkin, 0.08F, 0.0F);

    append_zombie_overlay(mesh, creature, root, 0.20F, 0.17F, 0.13F);
}

void append_day_sheep(CreatureMeshData& mesh, const CreatureRenderInstance& creature, const glm::mat4& root) {
    const auto morph = saturate(creature.morph_factor);
    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto attack = saturate(creature.attack_amount);
    const auto tension = saturate(creature.tension);
    const auto day_scale = 1.0F - morph * 0.55F;
    const auto phase = seed_unit(creature.appearance_seed, 24) * kTwoPi;
    const auto stride = std::sin(creature.animation_time * (4.3F + motion * 3.8F) + phase);
    const auto head_pitch = gaze * 0.06F - attack * 0.05F;
    const auto head_yaw = std::sin(creature.animation_time * 1.4F + phase) * 0.03F + gaze * 0.02F;
    const auto wool_pulse = std::sin(creature.animation_time * 2.1F + phase * 0.6F) * 0.01F;

    append_box(mesh, root, glm::vec3 {-0.06F, 0.78F, 0.0F}, glm::vec3 {(0.38F + wool_pulse) * day_scale, 0.25F * day_scale, 0.21F * day_scale},
               glm::vec3 {0.0F}, CreatureAtlasTile::SheepFace, morph, tension, kMaterialSkin, 0.14F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.06F, 0.82F, 0.0F}, glm::vec3 {0.46F * day_scale, 0.31F * day_scale, 0.28F * day_scale},
               glm::vec3 {0.0F}, CreatureAtlasTile::SheepWool, morph, tension, kMaterialWool, 0.10F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.44F, 0.82F, 0.0F}, glm::vec3 {0.16F * day_scale, 0.16F * day_scale, 0.13F * day_scale},
               glm::vec3 {head_pitch, head_yaw, 0.0F}, CreatureAtlasTile::SheepFace, morph, tension, kMaterialSkin, 0.18F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.36F, 0.90F, 0.0F}, glm::vec3 {0.21F * day_scale, 0.19F * day_scale, 0.17F * day_scale},
               glm::vec3 {head_pitch * 0.7F, head_yaw, 0.0F}, CreatureAtlasTile::SheepWool, morph, tension, kMaterialWool, 0.10F, 0.0F);

    for (const auto side : kSides) {
        const auto front_stride = -side * stride * (0.08F + motion * 0.08F);
        const auto rear_stride = side * stride * (0.10F + motion * 0.10F);
        append_box(mesh, root, glm::vec3 {0.18F, 0.26F, side * 0.14F}, glm::vec3 {0.05F * day_scale, 0.22F * day_scale, 0.05F * day_scale},
                   glm::vec3 {0.0F, 0.0F, front_stride}, CreatureAtlasTile::SheepFace, morph, tension, kMaterialSkin, 0.05F, 0.0F);
        append_box(mesh, root, glm::vec3 {0.18F, 0.01F, side * 0.14F}, glm::vec3 {0.05F * day_scale, 0.03F * day_scale, 0.05F * day_scale},
                   glm::vec3 {0.0F, 0.0F, front_stride * 0.30F}, CreatureAtlasTile::SheepHoof, morph, tension, kMaterialHorn, 0.04F, 0.0F);
        append_box(mesh, root, glm::vec3 {-0.24F, 0.27F, side * 0.15F}, glm::vec3 {0.05F * day_scale, 0.23F * day_scale, 0.05F * day_scale},
                   glm::vec3 {0.0F, 0.0F, rear_stride}, CreatureAtlasTile::SheepFace, morph, tension, kMaterialSkin, 0.05F, 0.0F);
        append_box(mesh, root, glm::vec3 {-0.24F, 0.01F, side * 0.15F}, glm::vec3 {0.05F * day_scale, 0.03F * day_scale, 0.05F * day_scale},
                   glm::vec3 {0.0F, 0.0F, rear_stride * 0.30F}, CreatureAtlasTile::SheepHoof, morph, tension, kMaterialHorn, 0.04F, 0.0F);
    }

    append_zombie_overlay(mesh, creature, root, 0.18F, 0.15F, 0.12F);
}

auto sample_creature_tile(CreatureAtlasTile tile, int x, int y) noexcept -> std::array<std::uint8_t, 4> {
    const auto fx = static_cast<float>(x) + 0.5F;
    const auto fy = static_cast<float>(y) + 0.5F;
    const auto nx = fx / static_cast<float>(kCreatureAtlasTileSize);
    const auto ny = fy / static_cast<float>(kCreatureAtlasTileSize);
    const auto grain = tile_noise(x, y, static_cast<int>(tile));
    const auto soft_grain = tile_noise(x + 7, y + 5, static_cast<int>(tile) + 23);

    switch (tile) {
    case CreatureAtlasTile::PigHide: {
        const auto stripe = 0.45F + 0.55F * std::sin(nx * 8.0F + grain * 4.0F);
        return make_rgba(214.0F + grain * 18.0F, 142.0F + soft_grain * 14.0F, 156.0F + stripe * 10.0F, 0.0F);
    }
    case CreatureAtlasTile::PigSnout: {
        const auto nostril =
            (std::abs(nx - 0.35F) < 0.08F && std::abs(ny - 0.52F) < 0.14F) ||
            (std::abs(nx - 0.65F) < 0.08F && std::abs(ny - 0.52F) < 0.14F);
        return nostril ? make_rgba(126.0F, 64.0F, 82.0F, 0.0F)
                       : make_rgba(236.0F + grain * 8.0F, 176.0F + soft_grain * 10.0F, 186.0F + grain * 12.0F, 0.0F);
    }
    case CreatureAtlasTile::PigEar: {
        const auto edge = std::min({nx, ny, 1.0F - nx, 1.0F - ny});
        return make_rgba(224.0F - edge * 40.0F, 154.0F - edge * 28.0F, 170.0F - edge * 26.0F, 0.0F);
    }
    case CreatureAtlasTile::CowHide: {
        const auto patch = tile_noise(x * 2, y * 2, 41) > 0.58F;
        if (patch) {
            return make_rgba(82.0F + grain * 18.0F, 64.0F + grain * 12.0F, 46.0F + grain * 10.0F, 0.0F);
        }
        return make_rgba(198.0F + soft_grain * 18.0F, 183.0F + grain * 14.0F, 166.0F + grain * 12.0F, 0.0F);
    }
    case CreatureAtlasTile::CowMuzzle: {
        const auto bridge = radial_falloff(nx, ny, 0.5F, 0.52F, 0.48F);
        return make_rgba(178.0F + bridge * 18.0F, 132.0F + grain * 16.0F, 120.0F + soft_grain * 14.0F, 0.0F);
    }
    case CreatureAtlasTile::CowHorn: {
        const auto tip = saturate(ny);
        return make_rgba(196.0F - tip * 58.0F, 178.0F - tip * 54.0F, 134.0F - tip * 48.0F, 0.0F);
    }
    case CreatureAtlasTile::SheepWool: {
        const auto curl = std::sin(nx * 18.0F + grain * 5.0F) * std::sin(ny * 16.0F + soft_grain * 5.0F);
        return make_rgba(228.0F + curl * 12.0F, 228.0F + grain * 10.0F, 218.0F + soft_grain * 12.0F, 0.0F);
    }
    case CreatureAtlasTile::SheepFace: {
        const auto muzzle = radial_falloff(nx, ny, 0.55F, 0.56F, 0.42F);
        return make_rgba(112.0F + grain * 16.0F + muzzle * 12.0F, 98.0F + soft_grain * 16.0F + muzzle * 10.0F,
                         86.0F + grain * 14.0F + muzzle * 8.0F, 0.0F);
    }
    case CreatureAtlasTile::SheepHoof: {
        const auto sheen = saturate(nx * 0.6F + ny * 0.4F);
        return make_rgba(54.0F + sheen * 22.0F, 46.0F + sheen * 18.0F, 40.0F + sheen * 14.0F, 0.0F);
    }
    case CreatureAtlasTile::ZombieFlesh: {
        const auto bruise = tile_noise(x * 3, y * 3, 77);
        return make_rgba(98.0F + grain * 20.0F, 124.0F + soft_grain * 22.0F - bruise * 10.0F, 94.0F + grain * 14.0F, 0.0F);
    }
    case CreatureAtlasTile::ZombieBone: {
        const auto crack = (x + y) % 7 == 0 ? 26.0F : 0.0F;
        return make_rgba(214.0F - crack + grain * 10.0F, 206.0F - crack * 0.8F + soft_grain * 8.0F, 184.0F - crack * 0.7F + grain * 8.0F, 0.0F);
    }
    case CreatureAtlasTile::ZombieMouth: {
        const auto wet = radial_falloff(nx, ny, 0.45F, 0.48F, 0.62F);
        return make_rgba(92.0F + wet * 20.0F, 22.0F + grain * 10.0F, 18.0F + soft_grain * 8.0F, 0.0F);
    }
    case CreatureAtlasTile::ZombieTeeth: {
        const auto enamel = 0.6F + 0.4F * std::sin(nx * 16.0F);
        return make_rgba(236.0F + enamel * 10.0F, 228.0F + grain * 10.0F, 198.0F + soft_grain * 12.0F, 0.0F);
    }
    case CreatureAtlasTile::ZombieEye: {
        const auto sclera = radial_falloff(nx, ny, 0.5F, 0.5F, 0.42F);
        const auto pupil = radial_falloff(nx, ny, 0.52F, 0.52F, 0.12F);
        const auto iris = radial_falloff(nx, ny, 0.50F, 0.50F, 0.20F) * (1.0F - pupil);
        const auto alpha = 70.0F + sclera * 155.0F;
        return make_rgba(202.0F + sclera * 34.0F - pupil * 160.0F,
                         202.0F + sclera * 30.0F - pupil * 170.0F + iris * 20.0F,
                         188.0F + sclera * 26.0F - pupil * 182.0F,
                         alpha);
    }
    case CreatureAtlasTile::ZombieVein: {
        const auto wave = std::sin(nx * 15.0F + ny * 4.0F + grain * 6.0F);
        const auto mask = std::abs(wave) > 0.82F ? 1.0F : 0.0F;
        return make_rgba(164.0F + mask * 30.0F, 32.0F + mask * 18.0F, 26.0F + mask * 12.0F, mask * (38.0F + soft_grain * 92.0F));
    }
    case CreatureAtlasTile::ZombieScar: {
        const auto line = std::abs((nx - 0.22F) * 0.65F + (ny - 0.55F)) < 0.06F ||
                          std::abs((nx - 0.64F) * 0.8F - (ny - 0.36F)) < 0.05F;
        return make_rgba(178.0F + grain * 20.0F, 34.0F + soft_grain * 10.0F, 30.0F + grain * 8.0F, line ? 136.0F : 0.0F);
    }
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
    for (int tile_index = 0; tile_index <= static_cast<int>(CreatureAtlasTile::ZombieScar); ++tile_index) {
        const auto tile = static_cast<CreatureAtlasTile>(tile_index);
        const auto coordinates = creature_atlas_tile_coordinates(tile);
        fill_tile(pixels, coordinates[0], coordinates[1], [tile](int x, int y) {
            return sample_creature_tile(tile, x, y);
        });
    }
    return pixels;
}

auto build_creature_mesh(const CreatureRenderInstance& creature) -> CreatureMeshData {
    CreatureMeshData mesh {};

    auto root = glm::translate(glm::mat4(1.0F), creature.position);
    root = glm::rotate(root, creature.yaw_radians, glm::vec3 {0.0F, 1.0F, 0.0F});

    switch (creature.species) {
    case CreatureSpecies::Pig:
        append_day_pig(mesh, creature, root);
        break;
    case CreatureSpecies::Cow:
        append_day_cow(mesh, creature, root);
        break;
    case CreatureSpecies::Sheep:
    default:
        append_day_sheep(mesh, creature, root);
        break;
    }

    return mesh;
}

} // namespace valcraft
