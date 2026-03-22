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
constexpr float kMaterialFur = 0.12F;
constexpr float kMaterialWool = 0.18F;
constexpr float kMaterialSkin = 0.34F;
constexpr float kMaterialHoof = 0.58F;
constexpr float kMaterialNightFlesh = 0.72F;
constexpr float kMaterialNightBone = 0.88F;
constexpr float kMaterialNightGlow = 1.0F;
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

auto seed_signed(std::uint32_t seed, int bit_shift) noexcept -> float {
    return seed_unit(seed, bit_shift) * 2.0F - 1.0F;
}

auto hash_to_unit(int x, int y, int seed) noexcept -> float {
    auto value = static_cast<std::uint32_t>(x) * 374761393U;
    value ^= static_cast<std::uint32_t>(y) * 668265263U;
    value ^= static_cast<std::uint32_t>(seed) * 2246822519U;
    value = (value ^ (value >> 13U)) * 1274126177U;
    value ^= value >> 16U;
    return static_cast<float>(value & 0xFFFFU) / 65535.0F;
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

auto interpolate_vec3(const glm::vec3& day_value, const glm::vec3& nightmare_value, float nightmare_factor) -> glm::vec3 {
    return glm::mix(day_value, nightmare_value, nightmare_factor);
}

auto behavior_head_pitch(const CreatureRenderInstance& creature) noexcept -> float {
    switch (creature.behavior_state) {
    case CreatureBehaviorState::Sniff:
        return 0.22F + creature.gaze_weight * 0.06F;
    case CreatureBehaviorState::Flee:
        return -0.08F;
    case CreatureBehaviorState::Stare:
        return -0.12F - creature.gaze_weight * 0.10F;
    case CreatureBehaviorState::Twitch:
        return std::sin(creature.animation_time * 21.0F) * 0.12F;
    default:
        return creature.gaze_weight * 0.04F;
    }
}

auto behavior_head_roll(const CreatureRenderInstance& creature) noexcept -> float {
    switch (creature.behavior_state) {
    case CreatureBehaviorState::Twitch:
        return std::sin(creature.animation_time * 17.0F) * 0.14F;
    case CreatureBehaviorState::Stare:
        return 0.03F * creature.gaze_weight;
    default:
        return 0.0F;
    }
}

auto behavior_head_yaw(const CreatureRenderInstance& creature) noexcept -> float {
    switch (creature.behavior_state) {
    case CreatureBehaviorState::Sniff:
        return std::sin(creature.animation_time * 4.5F) * 0.12F;
    case CreatureBehaviorState::Stare:
        return std::sin(creature.animation_time * 0.75F) * 0.03F;
    case CreatureBehaviorState::Twitch:
        return std::sin(creature.animation_time * 18.0F) * 0.18F;
    default:
        return 0.0F;
    }
}

void append_eye_pair(CreatureMeshData& mesh,
                     const glm::mat4& root,
                     const glm::vec3& center,
                     float z_offset,
                     const glm::vec3& half_extent,
                     const glm::vec3& rotation_radians,
                     float nightmare_factor,
                     float tension) {
    const auto scaled_half_extent = half_extent * nightmare_factor;
    for (const auto side : kSides) {
        append_box(
            mesh,
            root,
            center + glm::vec3 {0.0F, 0.0F, side * z_offset},
            scaled_half_extent,
            rotation_radians + glm::vec3 {0.0F, 0.0F, side * 0.04F},
            CreatureAtlasTile::NightmareEye,
            nightmare_factor,
            tension,
            kMaterialNightGlow,
            0.92F,
            1.0F);
    }
}

void append_spine_chain(CreatureMeshData& mesh,
                        const glm::mat4& root,
                        const glm::vec3& start,
                        const glm::vec3& step,
                        int count,
                        float nightmare_factor,
                        float tension) {
    for (int index = 0; index < count; ++index) {
        const auto factor = nightmare_factor * (1.0F - static_cast<float>(index) * 0.15F);
        append_box(
            mesh,
            root,
            start + step * static_cast<float>(index),
            glm::vec3 {0.015F + factor * 0.03F, factor * 0.075F, 0.028F},
            glm::vec3 {0.04F * static_cast<float>(index), 0.0F, 0.03F * static_cast<float>(index % 2 == 0 ? 1 : -1)},
            CreatureAtlasTile::NightmareBone,
            nightmare_factor,
            tension,
            kMaterialNightBone,
            0.78F,
            0.0F);
    }
}

void append_rabbit(CreatureMeshData& mesh, const CreatureRenderInstance& creature, const glm::mat4& root) {
    const auto nightmare = saturate(creature.morph_factor);
    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto tension = saturate(creature.tension);
    const auto shape_a = seed_signed(creature.appearance_seed, 0);
    const auto shape_b = seed_signed(creature.appearance_seed, 8);
    const auto shape_c = seed_signed(creature.appearance_seed, 16);
    const auto stride = std::sin(creature.animation_time * (6.2F + motion * 4.2F) + seed_unit(creature.appearance_seed, 24) * kTwoPi);
    const auto bound = motion * (0.03F + 0.05F * stride);
    const auto breath = std::sin(creature.animation_time * 2.4F + seed_unit(creature.appearance_seed, 4) * 3.0F) * 0.024F;
    const auto tremor = std::sin(creature.animation_time * 18.0F + seed_unit(creature.appearance_seed, 20) * 7.0F) * nightmare *
                        (0.012F + tension * 0.035F);
    const auto head_pitch = behavior_head_pitch(creature) + breath * 0.35F - nightmare * 0.15F;
    const auto head_roll = behavior_head_roll(creature) + tremor * 0.65F + shape_a * 0.04F;
    const auto head_yaw = behavior_head_yaw(creature) + shape_b * 0.08F + nightmare * 0.08F * std::sin(creature.animation_time * 3.6F);
    const auto ear_fold = 0.10F + shape_c * 0.06F + std::sin(creature.animation_time * 4.3F + shape_b) * 0.06F * (0.35F + gaze * 0.65F);
    const auto haunch_roll = -0.06F + bound * 0.75F + nightmare * 0.10F;
    const auto chest_roll = 0.03F - bound * 0.35F - nightmare * 0.06F;

    append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.22F, 0.58F, 0.0F}, glm::vec3 {-0.30F, 0.61F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.35F + shape_a * 0.03F, 0.25F, 0.24F}, glm::vec3 {0.26F, 0.22F, 0.15F}, nightmare),
               glm::vec3 {breath * 0.18F, 0.0F, haunch_roll}, CreatureAtlasTile::RabbitCoat, nightmare, tension, kMaterialFur, 0.22F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.12F, 0.62F, 0.0F}, glm::vec3 {0.08F, 0.56F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.27F, 0.21F, 0.20F}, glm::vec3 {0.19F, 0.15F, 0.12F}, nightmare),
               glm::vec3 {-breath * 0.10F, 0.0F, chest_roll}, CreatureAtlasTile::RabbitCoat, nightmare, tension, kMaterialFur, 0.24F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.02F, 0.43F, 0.0F}, glm::vec3 {-0.08F, 0.44F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.23F, 0.11F, 0.18F}, glm::vec3 {0.18F, 0.08F, 0.10F}, nightmare),
               glm::vec3 {0.0F, 0.0F, haunch_roll * 0.25F}, CreatureAtlasTile::RabbitBelly, nightmare, tension, kMaterialSkin, 0.14F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.34F, 0.78F, 0.0F}, glm::vec3 {0.36F, 0.74F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.10F, 0.10F, 0.10F}, glm::vec3 {0.07F, 0.17F, 0.07F}, nightmare),
               glm::vec3 {head_pitch * 0.35F, head_yaw * 0.2F, head_roll * 0.25F}, CreatureAtlasTile::RabbitBelly, nightmare, tension, kMaterialSkin, 0.18F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.58F, 0.88F, 0.0F}, glm::vec3 {0.70F, 0.88F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.17F, 0.16F, 0.15F}, glm::vec3 {0.16F, 0.13F, 0.11F}, nightmare),
               glm::vec3 {head_pitch, head_yaw, head_roll}, CreatureAtlasTile::RabbitCoat, nightmare, tension, kMaterialFur, 0.26F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.80F, 0.82F, 0.0F}, glm::vec3 {0.96F, 0.76F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.12F, 0.08F, 0.09F}, glm::vec3 {0.18F, 0.06F, 0.07F}, nightmare),
               glm::vec3 {head_pitch - nightmare * 0.12F, head_yaw * 0.7F, head_roll * 0.3F}, CreatureAtlasTile::RabbitNose, nightmare, tension, kMaterialSkin, 0.34F, 0.0F);

    for (const auto side : kSides) {
        const auto z_offset = side * (0.12F + shape_a * 0.01F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {0.67F, 0.82F, z_offset}, glm::vec3 {0.74F, 0.82F, z_offset * 0.70F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.07F, 0.07F, 0.05F}, glm::vec3 {0.05F, 0.05F, 0.04F}, nightmare),
                   glm::vec3 {head_pitch * 0.7F, head_yaw, side * (0.04F + head_roll * 0.3F)}, CreatureAtlasTile::RabbitBelly, nightmare, tension, kMaterialSkin, 0.16F, 0.0F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {0.47F, 1.18F, side * 0.12F}, glm::vec3 {0.72F, 1.20F, side * 0.18F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.05F, 0.26F + shape_c * 0.02F, 0.04F}, glm::vec3 {0.04F, 0.34F, 0.03F}, nightmare),
                   glm::vec3 {-0.05F + nightmare * 0.12F, head_yaw * 0.3F, side * (-0.18F - ear_fold - nightmare * 0.18F)}, CreatureAtlasTile::RabbitCoat, nightmare, tension, kMaterialFur, 0.12F, 0.0F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {0.50F, 1.12F, side * 0.10F}, glm::vec3 {0.74F, 1.16F, side * 0.15F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.026F, 0.18F, 0.022F}, glm::vec3 {0.020F, 0.24F, 0.018F}, nightmare),
                   glm::vec3 {-0.03F + nightmare * 0.10F, head_yaw * 0.3F, side * (-0.14F - ear_fold - nightmare * 0.12F)}, CreatureAtlasTile::RabbitEarInner, nightmare, tension, kMaterialSkin, 0.08F, 0.0F);
    }

    for (const auto side : kSides) {
        const auto front_stride = -side * stride * (0.08F + motion * 0.10F);
        const auto hind_stride = side * stride * (0.12F + motion * 0.16F) + bound;
        const auto front_z = side * 0.11F;
        const auto hind_z = side * 0.13F;
        append_box(mesh, root, interpolate_vec3(glm::vec3 {0.20F, 0.24F, front_z}, glm::vec3 {0.18F, 0.26F, front_z * 0.90F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.05F, 0.15F, 0.05F}, glm::vec3 {0.04F, 0.24F, 0.04F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, front_stride}, CreatureAtlasTile::RabbitCoat, nightmare, tension, kMaterialFur, 0.12F, 0.0F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {0.20F, 0.03F, front_z}, glm::vec3 {0.18F, 0.02F, front_z * 0.90F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.05F, 0.04F, 0.06F}, glm::vec3 {0.04F, 0.03F, 0.05F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, front_stride * 0.35F}, CreatureAtlasTile::RabbitBelly, nightmare, tension, kMaterialSkin, 0.06F, 0.0F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.26F, 0.26F, hind_z}, glm::vec3 {-0.28F, 0.28F, hind_z * 0.85F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.07F, 0.17F, 0.07F}, glm::vec3 {0.05F, 0.26F, 0.04F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, hind_stride}, CreatureAtlasTile::RabbitCoat, nightmare, tension, kMaterialFur, 0.14F, 0.0F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.28F, 0.05F, hind_z}, glm::vec3 {-0.34F, 0.04F, hind_z * 0.85F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.11F, 0.05F, 0.07F}, glm::vec3 {0.14F, 0.03F, 0.05F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, hind_stride * 0.45F + side * nightmare * 0.08F}, CreatureAtlasTile::RabbitBelly, nightmare, tension, kMaterialSkin, 0.08F, 0.0F);
    }

    append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.56F, 0.79F, 0.0F}, glm::vec3 {-0.66F, 0.82F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.10F, 0.10F, 0.10F}, glm::vec3 {0.05F, 0.18F, 0.04F}, nightmare),
               glm::vec3 {0.0F, 0.0F, -0.10F + nightmare * 0.28F}, CreatureAtlasTile::RabbitBelly, nightmare, tension, kMaterialFur, 0.10F, 0.0F);

    append_box(mesh, root, glm::vec3 {0.92F, 0.70F, 0.0F}, glm::vec3 {nightmare * 0.15F, nightmare * 0.045F, 0.06F},
               glm::vec3 {head_pitch - nightmare * 0.22F, head_yaw, head_roll * 0.4F}, CreatureAtlasTile::NightmareFlesh, nightmare, tension, kMaterialNightFlesh, 0.64F, 0.0F);
    append_spine_chain(mesh, root, glm::vec3 {-0.10F, 0.94F, 0.0F}, glm::vec3 {0.20F, 0.03F, 0.0F}, 3, nightmare, tension);
    append_box(mesh, root, glm::vec3 {0.18F, 0.86F, 0.0F}, glm::vec3 {nightmare * 0.16F, nightmare * 0.04F, 0.11F},
               glm::vec3 {0.0F, 0.0F, 0.08F + tremor}, CreatureAtlasTile::NightmareScar, nightmare, tension, kMaterialNightGlow, 0.88F, 1.0F);
    append_box(mesh, root, glm::vec3 {-0.18F, 0.74F, 0.0F}, glm::vec3 {nightmare * 0.12F, nightmare * 0.035F, 0.10F},
               glm::vec3 {0.0F, 0.0F, -0.10F}, CreatureAtlasTile::NightmareScar, nightmare, tension, kMaterialNightGlow, 0.86F, 1.0F);
    append_eye_pair(mesh, root, glm::vec3 {0.82F, 0.92F, 0.0F}, 0.072F, glm::vec3 {0.028F, 0.028F, 0.028F}, glm::vec3 {head_pitch, head_yaw, head_roll}, nightmare, tension);
}

void append_fennec(CreatureMeshData& mesh, const CreatureRenderInstance& creature, const glm::mat4& root) {
    const auto nightmare = saturate(creature.morph_factor);
    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto tension = saturate(creature.tension);
    const auto shape_a = seed_signed(creature.appearance_seed, 0);
    const auto shape_b = seed_signed(creature.appearance_seed, 8);
    const auto shape_c = seed_signed(creature.appearance_seed, 16);
    const auto stride = std::sin(creature.animation_time * (5.6F + motion * 3.8F) + seed_unit(creature.appearance_seed, 24) * kTwoPi);
    const auto sway = std::sin(creature.animation_time * 2.1F + seed_unit(creature.appearance_seed, 6) * 4.0F) * 0.020F;
    const auto tremor = std::sin(creature.animation_time * 15.0F + seed_unit(creature.appearance_seed, 18) * 9.0F) * nightmare *
                        (0.010F + tension * 0.028F);
    const auto head_pitch = behavior_head_pitch(creature) + sway * 0.30F - nightmare * 0.10F;
    const auto head_roll = behavior_head_roll(creature) + tremor * 0.45F + shape_a * 0.02F;
    const auto head_yaw = behavior_head_yaw(creature) + shape_b * 0.05F;
    const auto ear_spread = 0.34F + shape_c * 0.04F + gaze * 0.06F;
    const auto tail_swing = std::sin(creature.animation_time * 3.4F + shape_b) * (0.16F + motion * 0.10F);

    append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.02F, 0.55F, 0.0F}, glm::vec3 {-0.08F, 0.54F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.26F, 0.19F, 0.17F}, glm::vec3 {0.19F, 0.15F, 0.12F}, nightmare),
               glm::vec3 {-sway * 0.15F, 0.0F, -0.03F - nightmare * 0.06F}, CreatureAtlasTile::FennecCoat, nightmare, tension, kMaterialFur, 0.22F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.28F, 0.52F, 0.0F}, glm::vec3 {0.22F, 0.48F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.30F, 0.16F, 0.16F}, glm::vec3 {0.22F, 0.12F, 0.11F}, nightmare),
               glm::vec3 {sway * 0.12F, 0.0F, 0.03F - nightmare * 0.04F}, CreatureAtlasTile::FennecCoat, nightmare, tension, kMaterialFur, 0.20F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.10F, 0.66F, 0.0F}, glm::vec3 {0.06F, 0.64F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.23F, 0.06F, 0.10F}, glm::vec3 {0.20F, 0.04F, 0.07F}, nightmare),
               glm::vec3 {0.0F, 0.0F, 0.02F - nightmare * 0.03F}, CreatureAtlasTile::FennecBack, nightmare, tension, kMaterialFur, 0.30F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.48F, 0.70F, 0.0F}, glm::vec3 {0.52F, 0.72F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.09F, 0.09F, 0.09F}, glm::vec3 {0.07F, 0.12F, 0.06F}, nightmare),
               glm::vec3 {head_pitch * 0.45F, head_yaw * 0.2F, head_roll * 0.25F}, CreatureAtlasTile::FennecCoat, nightmare, tension, kMaterialFur, 0.16F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.73F, 0.79F, 0.0F}, glm::vec3 {0.88F, 0.74F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.17F, 0.14F, 0.13F}, glm::vec3 {0.18F, 0.10F, 0.09F}, nightmare),
               glm::vec3 {head_pitch, head_yaw, head_roll}, CreatureAtlasTile::FennecCoat, nightmare, tension, kMaterialFur, 0.24F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.98F, 0.69F, 0.0F}, glm::vec3 {1.10F, 0.65F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.15F, 0.07F, 0.07F}, glm::vec3 {0.20F, 0.05F, 0.06F}, nightmare),
               glm::vec3 {head_pitch - nightmare * 0.16F, head_yaw * 0.7F, head_roll * 0.3F}, CreatureAtlasTile::RabbitNose, nightmare, tension, kMaterialSkin, 0.38F, 0.0F);

    for (const auto side : kSides) {
        append_box(mesh, root, interpolate_vec3(glm::vec3 {0.60F, 1.10F, side * 0.17F}, glm::vec3 {0.84F, 1.16F, side * 0.23F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.08F, 0.33F + shape_c * 0.02F, 0.03F}, glm::vec3 {0.07F, 0.42F, 0.03F}, nightmare),
                   glm::vec3 {-0.02F, head_yaw * 0.2F, side * (ear_spread + nightmare * 0.12F + tremor * 0.4F)}, CreatureAtlasTile::FennecBack, nightmare, tension, kMaterialFur, 0.14F, 0.0F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {0.64F, 1.04F, side * 0.15F}, glm::vec3 {0.87F, 1.10F, side * 0.20F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.035F, 0.24F, 0.020F}, glm::vec3 {0.030F, 0.31F, 0.016F}, nightmare),
                   glm::vec3 {-0.02F, head_yaw * 0.2F, side * (ear_spread - 0.06F + nightmare * 0.10F)}, CreatureAtlasTile::FennecEarInner, nightmare, tension, kMaterialSkin, 0.10F, 0.0F);
    }

    for (const auto side : kSides) {
        const auto front_z = side * 0.10F;
        const auto rear_z = side * 0.11F;
        const auto front_stride = -side * stride * (0.10F + motion * 0.12F);
        const auto rear_stride = side * stride * (0.12F + motion * 0.14F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {0.42F, 0.24F, front_z}, glm::vec3 {0.40F, 0.26F, front_z * 0.92F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.04F, 0.19F, 0.04F}, glm::vec3 {0.035F, 0.27F, 0.035F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, front_stride}, CreatureAtlasTile::FennecCoat, nightmare, tension, kMaterialFur, 0.12F, 0.0F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {0.42F, 0.00F, front_z}, glm::vec3 {0.40F, -0.01F, front_z * 0.92F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.045F, 0.035F, 0.05F}, glm::vec3 {0.040F, 0.025F, 0.040F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, front_stride * 0.30F}, CreatureAtlasTile::FennecBack, nightmare, tension, kMaterialSkin, 0.08F, 0.0F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.30F, 0.24F, rear_z}, glm::vec3 {-0.34F, 0.25F, rear_z * 0.92F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.045F, 0.18F, 0.045F}, glm::vec3 {0.035F, 0.29F, 0.032F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, rear_stride}, CreatureAtlasTile::FennecCoat, nightmare, tension, kMaterialFur, 0.14F, 0.0F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.30F, 0.00F, rear_z}, glm::vec3 {-0.34F, -0.01F, rear_z * 0.92F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.05F, 0.03F, 0.05F}, glm::vec3 {0.040F, 0.025F, 0.040F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, rear_stride * 0.30F}, CreatureAtlasTile::FennecBack, nightmare, tension, kMaterialSkin, 0.08F, 0.0F);
    }

    append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.56F, 0.62F, 0.0F}, glm::vec3 {-0.68F, 0.66F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.16F, 0.08F, 0.08F}, glm::vec3 {0.18F, 0.05F, 0.05F}, nightmare),
               glm::vec3 {0.0F, 0.0F, -0.20F + tail_swing}, CreatureAtlasTile::FennecTail, nightmare, tension, kMaterialFur, 0.16F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.80F, 0.72F, 0.0F}, glm::vec3 {-0.98F, 0.77F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.22F, 0.10F, 0.10F}, glm::vec3 {0.24F, 0.06F, 0.05F}, nightmare),
               glm::vec3 {0.0F, 0.0F, -0.28F + tail_swing + nightmare * 0.10F}, CreatureAtlasTile::FennecTail, nightmare, tension, kMaterialFur, 0.18F, 0.0F);

    append_box(mesh, root, glm::vec3 {0.04F, 0.58F, 0.0F}, glm::vec3 {nightmare * 0.18F, nightmare * 0.045F, 0.12F},
               glm::vec3 {0.0F, 0.0F, -0.06F}, CreatureAtlasTile::NightmareScar, nightmare, tension, kMaterialNightGlow, 0.88F, 1.0F);
    append_box(mesh, root, glm::vec3 {1.04F, 0.60F, 0.0F}, glm::vec3 {nightmare * 0.18F, nightmare * 0.040F, 0.07F},
               glm::vec3 {head_pitch - nightmare * 0.20F, head_yaw * 0.8F, head_roll * 0.35F}, CreatureAtlasTile::NightmareFlesh, nightmare, tension, kMaterialNightFlesh, 0.68F, 0.0F);
    append_spine_chain(mesh, root, glm::vec3 {-0.18F, 0.84F, 0.0F}, glm::vec3 {0.18F, 0.02F, 0.0F}, 4, nightmare, tension);
    for (int index = 0; index < 3; ++index) {
        append_box(mesh, root, glm::vec3 {0.06F + static_cast<float>(index) * 0.16F, 0.54F + static_cast<float>(index % 2) * 0.04F, 0.0F},
                   glm::vec3 {nightmare * (0.05F - static_cast<float>(index) * 0.01F), nightmare * (0.07F - static_cast<float>(index) * 0.01F), 0.08F},
                   glm::vec3 {0.0F, 0.0F, index % 2 == 0 ? 0.10F : -0.10F}, CreatureAtlasTile::NightmareBone, nightmare, tension, kMaterialNightBone, 0.82F, 0.0F);
    }
    append_eye_pair(mesh, root, glm::vec3 {0.92F, 0.80F, 0.0F}, 0.070F, glm::vec3 {0.030F, 0.030F, 0.026F}, glm::vec3 {head_pitch, head_yaw, head_roll}, nightmare, tension);
}

void append_lamb(CreatureMeshData& mesh, const CreatureRenderInstance& creature, const glm::mat4& root) {
    const auto nightmare = saturate(creature.morph_factor);
    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto tension = saturate(creature.tension);
    const auto shape_a = seed_signed(creature.appearance_seed, 0);
    const auto shape_b = seed_signed(creature.appearance_seed, 8);
    const auto shape_c = seed_signed(creature.appearance_seed, 16);
    const auto stride = std::sin(creature.animation_time * (5.1F + motion * 3.4F) + seed_unit(creature.appearance_seed, 24) * kTwoPi);
    const auto wool_breathe = std::sin(creature.animation_time * 2.0F + seed_unit(creature.appearance_seed, 5) * 2.8F) * 0.022F;
    const auto tremor = std::sin(creature.animation_time * 14.0F + seed_unit(creature.appearance_seed, 18) * 6.0F) * nightmare *
                        (0.012F + tension * 0.030F);
    const auto head_pitch = behavior_head_pitch(creature) + wool_breathe * 0.25F - nightmare * 0.10F;
    const auto head_roll = behavior_head_roll(creature) + tremor * 0.35F + shape_a * 0.03F;
    const auto head_yaw = behavior_head_yaw(creature) + shape_b * 0.05F;
    const auto horn_sway = std::sin(creature.animation_time * 3.0F + shape_c) * (0.05F + nightmare * 0.05F + gaze * 0.03F);

    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.00F, 0.60F, 0.0F}, glm::vec3 {-0.06F, 0.60F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.34F, 0.23F, 0.22F}, glm::vec3 {0.26F, 0.17F, 0.14F}, nightmare),
               glm::vec3 {0.0F, 0.0F, -0.02F}, CreatureAtlasTile::LambWoolShadow, nightmare, tension, kMaterialWool, 0.30F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.18F, 0.73F, 0.0F}, glm::vec3 {-0.16F, 0.74F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.26F + shape_a * 0.02F, 0.13F, 0.17F}, glm::vec3 {0.18F, 0.09F, 0.10F}, nightmare),
               glm::vec3 {wool_breathe * 0.15F, 0.0F, -0.02F}, CreatureAtlasTile::LambWoolLight, nightmare, tension, kMaterialWool, 0.14F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.16F, 0.75F, 0.0F}, glm::vec3 {0.12F, 0.74F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.24F, 0.14F, 0.17F}, glm::vec3 {0.16F, 0.08F, 0.10F}, nightmare),
               glm::vec3 {-wool_breathe * 0.12F, 0.0F, 0.02F}, CreatureAtlasTile::LambWoolLight, nightmare, tension, kMaterialWool, 0.14F, 0.0F);
    for (const auto side : kSides) {
        append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.02F, 0.62F, side * 0.19F}, glm::vec3 {-0.02F, 0.60F, side * 0.14F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.18F, 0.18F, 0.10F}, glm::vec3 {0.12F, 0.10F, 0.06F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, side * (0.05F - nightmare * 0.04F)}, CreatureAtlasTile::LambWoolLight, nightmare, tension, kMaterialWool, 0.18F, 0.0F);
    }
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.38F, 0.70F, 0.0F}, glm::vec3 {0.36F, 0.72F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.11F, 0.12F, 0.10F}, glm::vec3 {0.08F, 0.16F, 0.07F}, nightmare),
               glm::vec3 {head_pitch * 0.45F, head_yaw * 0.3F, head_roll * 0.20F}, CreatureAtlasTile::LambWoolShadow, nightmare, tension, kMaterialWool, 0.24F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.62F, 0.82F, 0.0F}, glm::vec3 {0.68F, 0.84F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.15F, 0.13F, 0.11F}, glm::vec3 {0.14F, 0.10F, 0.09F}, nightmare),
               glm::vec3 {head_pitch, head_yaw, head_roll}, CreatureAtlasTile::LambFace, nightmare, tension, kMaterialSkin, 0.28F, 0.0F);
    append_box(mesh, root, interpolate_vec3(glm::vec3 {0.84F, 0.77F, 0.0F}, glm::vec3 {0.96F, 0.74F, 0.0F}, nightmare),
               interpolate_vec3(glm::vec3 {0.13F, 0.08F, 0.08F}, glm::vec3 {0.17F, 0.06F, 0.06F}, nightmare),
               glm::vec3 {head_pitch - nightmare * 0.10F, head_yaw * 0.7F, head_roll * 0.3F}, CreatureAtlasTile::LambFace, nightmare, tension, kMaterialSkin, 0.36F, 0.0F);

    for (const auto side : kSides) {
        append_box(mesh, root, interpolate_vec3(glm::vec3 {0.50F, 1.00F, side * 0.10F}, glm::vec3 {0.70F, 1.04F, side * 0.13F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.02F, 0.05F, 0.02F}, glm::vec3 {0.04F, 0.16F, 0.03F}, nightmare),
                   glm::vec3 {-0.05F, 0.0F, side * (-0.18F - horn_sway - nightmare * 0.12F)}, CreatureAtlasTile::HoofHorn, nightmare, tension, kMaterialHoof, 0.18F, 0.0F);
    }

    for (const auto side : kSides) {
        const auto front_z = side * 0.12F;
        const auto rear_z = side * 0.14F;
        const auto front_stride = -side * stride * (0.08F + motion * 0.08F);
        const auto rear_stride = side * stride * (0.08F + motion * 0.08F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {0.22F, 0.24F, front_z}, glm::vec3 {0.22F, 0.24F, front_z * 0.92F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.05F, 0.19F, 0.05F}, glm::vec3 {0.04F, 0.28F, 0.035F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, front_stride}, CreatureAtlasTile::LambFace, nightmare, tension, kMaterialSkin, 0.14F, 0.0F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {0.22F, -0.01F, front_z}, glm::vec3 {0.22F, -0.01F, front_z * 0.92F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.05F, 0.03F, 0.05F}, glm::vec3 {0.04F, 0.025F, 0.04F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, front_stride * 0.30F}, CreatureAtlasTile::HoofHorn, nightmare, tension, kMaterialHoof, 0.08F, 0.0F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.22F, 0.24F, rear_z}, glm::vec3 {-0.24F, 0.26F, rear_z * 0.92F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.05F, 0.19F, 0.05F}, glm::vec3 {0.04F, 0.30F, 0.035F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, rear_stride + nightmare * side * 0.04F}, CreatureAtlasTile::LambFace, nightmare, tension, kMaterialSkin, 0.14F, 0.0F);
        append_box(mesh, root, interpolate_vec3(glm::vec3 {-0.22F, -0.01F, rear_z}, glm::vec3 {-0.24F, -0.01F, rear_z * 0.92F}, nightmare),
                   interpolate_vec3(glm::vec3 {0.05F, 0.03F, 0.05F}, glm::vec3 {0.04F, 0.025F, 0.04F}, nightmare),
                   glm::vec3 {0.0F, 0.0F, rear_stride * 0.30F}, CreatureAtlasTile::HoofHorn, nightmare, tension, kMaterialHoof, 0.08F, 0.0F);
    }

    append_box(mesh, root, glm::vec3 {0.72F, 0.82F, 0.0F}, glm::vec3 {nightmare * 0.18F, nightmare * 0.05F, 0.08F},
               glm::vec3 {head_pitch - nightmare * 0.20F, head_yaw, head_roll * 0.4F}, CreatureAtlasTile::NightmareBone, nightmare, tension, kMaterialNightBone, 0.78F, 0.0F);
    append_spine_chain(mesh, root, glm::vec3 {-0.08F, 0.98F, 0.0F}, glm::vec3 {0.18F, 0.02F, 0.0F}, 3, nightmare, tension);
    for (const auto side : kSides) {
        append_box(mesh, root, glm::vec3 {0.08F, 0.78F, side * 0.18F}, glm::vec3 {nightmare * 0.07F, nightmare * 0.09F, nightmare * 0.03F},
                   glm::vec3 {0.0F, 0.0F, side * (0.12F + nightmare * 0.16F)}, CreatureAtlasTile::NightmareScar, nightmare, tension, kMaterialNightGlow, 0.90F, 1.0F);
        append_box(mesh, root, glm::vec3 {-0.16F, 0.70F, side * 0.20F}, glm::vec3 {nightmare * 0.06F, nightmare * 0.08F, nightmare * 0.025F},
                   glm::vec3 {0.0F, 0.0F, side * (-0.08F - nightmare * 0.12F)}, CreatureAtlasTile::NightmareScar, nightmare, tension, kMaterialNightGlow, 0.88F, 1.0F);
    }
    append_eye_pair(mesh, root, glm::vec3 {0.76F, 0.85F, 0.0F}, 0.075F, glm::vec3 {0.028F, 0.028F, 0.026F}, glm::vec3 {head_pitch, head_yaw, head_roll}, nightmare, tension);
}

} // namespace

auto creature_atlas_tile_coordinates(CreatureAtlasTile tile) noexcept -> std::array<int, 2> {
    const auto tile_index = static_cast<int>(static_cast<std::uint8_t>(tile));
    return {tile_index % static_cast<int>(kCreatureAtlasTilesPerAxis), tile_index / static_cast<int>(kCreatureAtlasTilesPerAxis)};
}

auto build_creature_atlas_pixels() -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kCreatureAtlasSize * kCreatureAtlasSize * 4), 0);

    fill_tile(pixels, 0, 0, [](int x, int y) {
        const auto fur = 0.5F + 0.5F * std::sin((static_cast<float>(x) + 0.5F) * 0.75F + (static_cast<float>(y) + 0.5F) * 0.30F);
        const auto noise = tile_noise(x, y, 3);
        return make_rgba(156.0F + fur * 24.0F + noise * 10.0F, 118.0F + fur * 18.0F + noise * 8.0F, 92.0F + noise * 8.0F);
    });
    fill_tile(pixels, 1, 0, [](int x, int y) {
        const auto soft = radial_falloff((static_cast<float>(x) + 0.5F) / 16.0F, (static_cast<float>(y) + 0.5F) / 16.0F, 0.5F, 0.45F, 0.80F);
        const auto noise = tile_noise(x, y, 5);
        return make_rgba(214.0F + soft * 18.0F, 192.0F + soft * 14.0F + noise * 6.0F, 172.0F + noise * 7.0F);
    });
    fill_tile(pixels, 2, 0, [](int x, int y) {
        const auto edge = 1.0F - radial_falloff((static_cast<float>(x) + 0.5F) / 16.0F, (static_cast<float>(y) + 0.5F) / 16.0F, 0.52F, 0.28F, 0.86F);
        const auto noise = tile_noise(x, y, 7);
        return make_rgba(220.0F + noise * 12.0F, 176.0F + edge * 16.0F + noise * 8.0F, 182.0F + edge * 12.0F + noise * 4.0F);
    });
    fill_tile(pixels, 3, 0, [](int x, int y) {
        const auto highlight = radial_falloff((static_cast<float>(x) + 0.5F) / 16.0F, (static_cast<float>(y) + 0.5F) / 16.0F, 0.50F, 0.42F, 0.50F);
        const auto noise = tile_noise(x, y, 9);
        return make_rgba(118.0F + highlight * 24.0F + noise * 5.0F, 86.0F + highlight * 14.0F + noise * 5.0F, 74.0F + noise * 4.0F);
    });
    fill_tile(pixels, 4, 0, [](int x, int y) {
        const auto dorsal = 1.0F - (static_cast<float>(y) + 0.5F) / 16.0F;
        const auto noise = tile_noise(x, y, 11);
        return make_rgba(214.0F + dorsal * 18.0F + noise * 7.0F, 182.0F + dorsal * 12.0F + noise * 6.0F, 132.0F + (static_cast<float>(x) / 15.0F) * 10.0F + noise * 5.0F);
    });
    fill_tile(pixels, 5, 0, [](int x, int y) {
        const auto band = std::sin((static_cast<float>(x) + 0.5F) * 0.9F + (static_cast<float>(y) + 0.5F) * 0.35F) * 0.5F + 0.5F;
        const auto noise = tile_noise(x, y, 13);
        return make_rgba(170.0F + band * 18.0F + noise * 6.0F, 136.0F + band * 12.0F + noise * 5.0F, 92.0F + band * 6.0F + noise * 4.0F);
    });
    fill_tile(pixels, 6, 0, [](int x, int y) {
        const auto cartilage = std::sin((static_cast<float>(y) + 0.5F) * 1.1F + (static_cast<float>(x) + 0.5F) * 0.3F) * 0.5F + 0.5F;
        const auto noise = tile_noise(x, y, 15);
        return make_rgba(238.0F + noise * 7.0F, 206.0F + cartilage * 14.0F + noise * 6.0F, 188.0F + cartilage * 10.0F + noise * 4.0F);
    });
    fill_tile(pixels, 7, 0, [](int x, int y) {
        const auto tip = saturate((static_cast<float>(x) + static_cast<float>(y)) / 18.0F);
        const auto noise = tile_noise(x, y, 17);
        return make_rgba(232.0F - tip * 34.0F + noise * 6.0F, 214.0F - tip * 28.0F + noise * 5.0F, 180.0F - tip * 40.0F + noise * 4.0F);
    });
    fill_tile(pixels, 0, 1, [](int x, int y) {
        const auto puff = radial_falloff((static_cast<float>(x) + 0.5F) / 16.0F, (static_cast<float>(y) + 0.5F) / 16.0F, 0.35F, 0.35F, 0.35F) +
                          radial_falloff((static_cast<float>(x) + 0.5F) / 16.0F, (static_cast<float>(y) + 0.5F) / 16.0F, 0.68F, 0.55F, 0.32F);
        const auto noise = tile_noise(x, y, 19);
        return make_rgba(234.0F + puff * 10.0F + noise * 6.0F, 232.0F + puff * 8.0F + noise * 5.0F, 220.0F + puff * 6.0F + noise * 6.0F);
    });
    fill_tile(pixels, 1, 1, [](int x, int y) {
        const auto curl = std::sin((static_cast<float>(x) + 0.5F) * 0.8F + (static_cast<float>(y) + 0.5F) * 1.4F) * 0.5F + 0.5F;
        const auto noise = tile_noise(x, y, 21);
        return make_rgba(196.0F + curl * 12.0F + noise * 7.0F, 188.0F + curl * 10.0F + noise * 6.0F, 170.0F + curl * 9.0F + noise * 6.0F);
    });
    fill_tile(pixels, 2, 1, [](int x, int y) {
        const auto mask = radial_falloff((static_cast<float>(x) + 0.5F) / 16.0F, (static_cast<float>(y) + 0.5F) / 16.0F, 0.45F, 0.40F, 0.72F);
        const auto noise = tile_noise(x, y, 23);
        return make_rgba(128.0F + mask * 20.0F + noise * 8.0F, 102.0F + mask * 12.0F + noise * 7.0F, 88.0F + noise * 6.0F);
    });
    fill_tile(pixels, 3, 1, [](int x, int y) {
        const auto grain = std::sin((static_cast<float>(x) + 0.5F) * 1.1F - (static_cast<float>(y) + 0.5F) * 0.7F) * 0.5F + 0.5F;
        const auto noise = tile_noise(x, y, 25);
        return make_rgba(176.0F + grain * 20.0F + noise * 7.0F, 156.0F + grain * 14.0F + noise * 6.0F, 128.0F + grain * 10.0F + noise * 5.0F);
    });
    fill_tile(pixels, 4, 1, [](int x, int y) {
        const auto vein = std::sin((static_cast<float>(x) + 0.5F) * 1.3F + (static_cast<float>(y) + 0.5F) * 0.9F) * 0.5F + 0.5F;
        const auto noise = tile_noise(x, y, 27);
        return make_rgba(72.0F + vein * 18.0F + noise * 8.0F, 34.0F + vein * 10.0F + noise * 4.0F, 38.0F + noise * 5.0F);
    });
    fill_tile(pixels, 5, 1, [](int x, int y) {
        const auto crack = std::sin((static_cast<float>(x) + 0.5F) * 1.0F + (static_cast<float>(y) + 0.5F) * 2.0F) * 0.5F + 0.5F;
        const auto noise = tile_noise(x, y, 29);
        return make_rgba(148.0F + crack * 20.0F + noise * 6.0F, 150.0F + crack * 18.0F + noise * 6.0F, 164.0F + crack * 12.0F + noise * 5.0F);
    });
    fill_tile(pixels, 6, 1, [](int x, int y) {
        const auto line = std::sin((static_cast<float>(x) + 0.5F) * 1.4F + (static_cast<float>(y) + 0.5F) * 1.1F) * 0.5F + 0.5F;
        const auto glow_line = line > 0.82F || (x + y) % 7 == 0;
        const auto noise = tile_noise(x, y, 31);
        return make_rgba(58.0F + noise * 8.0F, 18.0F + noise * 4.0F, 24.0F + noise * 4.0F, glow_line ? 255.0F : 0.0F);
    });
    fill_tile(pixels, 7, 1, [](int x, int y) {
        const auto fx = (static_cast<float>(x) + 0.5F) / 16.0F;
        const auto fy = (static_cast<float>(y) + 0.5F) / 16.0F;
        const auto eye = radial_falloff(fx, fy, 0.52F, 0.46F, 0.24F);
        const auto slit = std::abs(fx - 0.52F) < 0.06F && std::abs(fy - 0.46F) < 0.20F;
        return make_rgba(14.0F + eye * 18.0F, 4.0F + eye * 4.0F, 6.0F + eye * 4.0F, slit ? 255.0F : eye * 36.0F);
    });

    return pixels;
}

auto build_creature_mesh(const CreatureRenderInstance& creature) -> CreatureMeshData {
    CreatureMeshData mesh {};
    mesh.vertices.reserve(24U * 32U);
    mesh.indices.reserve(36U * 32U);

    const auto nightmare = saturate(creature.morph_factor);
    const auto motion = saturate(creature.motion_amount);
    const auto gaze = saturate(creature.gaze_weight);
    const auto phase = seed_unit(creature.appearance_seed, 24) * kTwoPi;
    const auto pace = std::sin(creature.animation_time * (3.0F + motion * 7.0F) + phase);
    const auto breath = std::sin(creature.animation_time * 1.8F + phase * 0.35F);
    const auto body_bob = breath * (0.016F + (1.0F - motion) * 0.012F) + pace * (0.006F + motion * 0.040F);
    const auto tremor = nightmare * (0.008F + creature.tension * 0.025F + gaze * 0.010F) *
                        std::sin(creature.animation_time * 20.0F + phase * 1.7F);
    const auto roll = pace * motion * (0.012F + nightmare * 0.010F) + tremor * 0.12F;
    const auto pitch = -motion * 0.030F + nightmare * 0.020F * std::sin(creature.animation_time * 4.0F + phase * 0.3F);

    auto root = glm::mat4(1.0F);
    root = glm::translate(root, creature.position + glm::vec3 {0.0F, body_bob + tremor, 0.0F});
    root = glm::rotate(root, creature.yaw_radians, glm::vec3 {0.0F, 1.0F, 0.0F});
    root = glm::rotate(root, pitch, glm::vec3 {1.0F, 0.0F, 0.0F});
    root = glm::rotate(root, roll, glm::vec3 {0.0F, 0.0F, 1.0F});

    switch (creature.species) {
    case CreatureSpecies::Rabbit:
        append_rabbit(mesh, creature, root);
        break;
    case CreatureSpecies::Fennec:
        append_fennec(mesh, creature, root);
        break;
    case CreatureSpecies::Lamb:
    default:
        append_lamb(mesh, creature, root);
        break;
    }

    return mesh;
}

} // namespace valcraft
