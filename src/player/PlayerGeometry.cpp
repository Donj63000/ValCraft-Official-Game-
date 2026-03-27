#include "player/PlayerGeometry.h"

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
constexpr float kMaterialSkin = 0.32F;
constexpr float kMaterialFabric = 0.24F;
constexpr float kMaterialDenim = 0.48F;
constexpr float kMaterialLeather = 0.64F;
constexpr float kHurtFlashDuration = 0.35F;

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
    const auto index = static_cast<std::size_t>((y * kPlayerAtlasSize + x) * 4);
    pixels[index + 0] = rgba[0];
    pixels[index + 1] = rgba[1];
    pixels[index + 2] = rgba[2];
    pixels[index + 3] = rgba[3];
}

template <typename ColorFn>
void fill_tile(std::vector<std::uint8_t>& pixels, int tile_x, int tile_y, const ColorFn& color_fn) {
    const auto start_x = tile_x * kPlayerAtlasTileSize;
    const auto start_y = tile_y * kPlayerAtlasTileSize;
    for (int y = 0; y < kPlayerAtlasTileSize; ++y) {
        for (int x = 0; x < kPlayerAtlasTileSize; ++x) {
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
                PlayerAtlasTile tile,
                float material_class,
                float cavity_mask,
                float emissive_strength) {
    if (half_extent.x <= 1.0e-4F || half_extent.y <= 1.0e-4F || half_extent.z <= 1.0e-4F) {
        return;
    }

    const auto transform = make_transform(root, center, rotation_radians, half_extent);
    const auto normal_matrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    const auto tile_coordinates = player_atlas_tile_coordinates(tile);
    const auto uv_step = 1.0F / kPlayerAtlasTilesPerAxis;
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
                0.0F,
                0.0F,
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

auto sample_player_tile(PlayerAtlasTile tile, int x, int y) noexcept -> std::array<std::uint8_t, 4> {
    const auto fx = static_cast<float>(x) + 0.5F;
    const auto fy = static_cast<float>(y) + 0.5F;
    const auto nx = fx / static_cast<float>(kPlayerAtlasTileSize);
    const auto ny = fy / static_cast<float>(kPlayerAtlasTileSize);
    const auto grain = tile_noise(x, y, static_cast<int>(tile));
    const auto soft_grain = tile_noise(x + 3, y + 9, static_cast<int>(tile) + 31);

    switch (tile) {
    case PlayerAtlasTile::Skin:
        return make_rgba(188.0F + grain * 12.0F, 138.0F + soft_grain * 10.0F, 104.0F + grain * 8.0F, 0.0F);
    case PlayerAtlasTile::Hair: {
        const auto highlight = saturate(1.0F - ny * 0.8F + grain * 0.2F);
        return make_rgba(88.0F + highlight * 26.0F, 56.0F + highlight * 18.0F, 38.0F + highlight * 12.0F, 0.0F);
    }
    case PlayerAtlasTile::Shirt: {
        const auto band = 0.45F + 0.55F * std::sin(nx * 9.0F + grain * 4.0F);
        return make_rgba(44.0F + band * 16.0F, 112.0F + soft_grain * 18.0F, 198.0F + grain * 22.0F, 0.0F);
    }
    case PlayerAtlasTile::Pants:
        return make_rgba(54.0F + grain * 10.0F, 66.0F + soft_grain * 12.0F, 154.0F + grain * 18.0F, 0.0F);
    case PlayerAtlasTile::Shoes:
        return make_rgba(62.0F + grain * 8.0F, 42.0F + soft_grain * 8.0F, 24.0F + grain * 6.0F, 0.0F);
    case PlayerAtlasTile::Eye: {
        const auto sclera = radial_falloff(nx, ny, 0.5F, 0.5F, 0.44F);
        const auto pupil = radial_falloff(nx, ny, 0.52F, 0.52F, 0.14F);
        return make_rgba(198.0F + sclera * 34.0F - pupil * 180.0F,
                         214.0F + sclera * 30.0F - pupil * 190.0F,
                         228.0F + sclera * 20.0F - pupil * 200.0F,
                         0.0F);
    }
    case PlayerAtlasTile::Mouth: {
        const auto smile = std::abs((ny - 0.58F) - std::sin((nx - 0.5F) * kPi) * 0.10F) < 0.06F;
        return smile ? make_rgba(122.0F, 46.0F, 42.0F, 0.0F) : make_rgba(196.0F, 156.0F, 126.0F, 0.0F);
    }
    case PlayerAtlasTile::Hurt: {
        const auto pulse = radial_falloff(nx, ny, 0.5F, 0.5F, 0.7F);
        return make_rgba(198.0F + pulse * 22.0F, 68.0F + grain * 12.0F, 64.0F + soft_grain * 10.0F, 0.0F);
    }
    }

    return make_rgba(255.0F, 0.0F, 255.0F, 0.0F);
}

} // namespace

auto player_atlas_tile_coordinates(PlayerAtlasTile tile) noexcept -> std::array<int, 2> {
    const auto index = static_cast<int>(tile);
    return {index % static_cast<int>(kPlayerAtlasTilesPerAxis), index / static_cast<int>(kPlayerAtlasTilesPerAxis)};
}

auto build_player_atlas_pixels() -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kPlayerAtlasSize * kPlayerAtlasSize * 4), 0);
    for (int tile_index = 0; tile_index <= static_cast<int>(PlayerAtlasTile::Hurt); ++tile_index) {
        const auto tile = static_cast<PlayerAtlasTile>(tile_index);
        const auto coordinates = player_atlas_tile_coordinates(tile);
        fill_tile(pixels, coordinates[0], coordinates[1], [tile](int x, int y) {
            return sample_player_tile(tile, x, y);
        });
    }
    return pixels;
}

auto build_player_mesh(const PlayerController& player) -> CreatureMeshData {
    CreatureMeshData mesh {};

    const auto& state = player.state();
    auto forward = player.look_direction();
    forward.y = 0.0F;
    if (glm::dot(forward, forward) <= 1.0e-6F) {
        forward = glm::vec3 {0.0F, 0.0F, -1.0F};
    } else {
        forward = glm::normalize(forward);
    }

    const auto horizontal_velocity = glm::vec2 {state.velocity.x, state.velocity.z};
    const auto walk_amount = saturate(glm::length(horizontal_velocity) / 5.6F);
    const auto stride_phase = (state.position.x * 1.35F + state.position.z * 1.15F) * 2.1F;
    const auto stride = std::sin(stride_phase) * (0.16F + walk_amount * 0.34F);
    const auto arm_swing = stride * (0.90F + walk_amount * 0.55F);
    const auto leg_swing = -stride * (0.95F + walk_amount * 0.35F);
    const auto look_down = saturate((-state.pitch_degrees - 8.0F) / 42.0F);
    const auto hurt_amount = saturate(state.hurt_timer / kHurtFlashDuration);
    const auto body_offset = forward * (0.14F + look_down * 0.06F) + glm::vec3 {0.0F, -0.03F, 0.0F};

    auto root = glm::translate(glm::mat4(1.0F), state.position + body_offset);
    root = glm::rotate(root, glm::radians(state.yaw_degrees), glm::vec3 {0.0F, 1.0F, 0.0F});

    const auto torso_tile = hurt_amount > 0.12F ? PlayerAtlasTile::Hurt : PlayerAtlasTile::Shirt;
    const auto arm_tile = hurt_amount > 0.22F ? PlayerAtlasTile::Hurt : PlayerAtlasTile::Skin;
    const auto torso_roll = hurt_amount * 0.12F;
    const auto shoulder_twist = look_down * 0.10F;

    append_box(mesh, root, glm::vec3 {0.04F, 0.98F, 0.0F}, glm::vec3 {0.18F, 0.28F, 0.12F},
               glm::vec3 {0.0F, 0.0F, torso_roll}, torso_tile, kMaterialFabric, 0.12F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.16F, 0.40F, -0.08F}, glm::vec3 {0.07F, 0.34F, 0.07F},
               glm::vec3 {0.0F, 0.0F, leg_swing}, PlayerAtlasTile::Pants, kMaterialDenim, 0.08F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.16F, 0.06F, -0.08F}, glm::vec3 {0.072F, 0.04F, 0.075F},
               glm::vec3 {0.0F, 0.0F, leg_swing * 0.15F}, PlayerAtlasTile::Shoes, kMaterialLeather, 0.06F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.16F, 0.40F, 0.08F}, glm::vec3 {0.07F, 0.34F, 0.07F},
               glm::vec3 {0.0F, 0.0F, -leg_swing}, PlayerAtlasTile::Pants, kMaterialDenim, 0.08F, 0.0F);
    append_box(mesh, root, glm::vec3 {-0.16F, 0.06F, 0.08F}, glm::vec3 {0.072F, 0.04F, 0.075F},
               glm::vec3 {0.0F, 0.0F, -leg_swing * 0.15F}, PlayerAtlasTile::Shoes, kMaterialLeather, 0.06F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.04F, 0.94F, -0.24F}, glm::vec3 {0.060F, 0.31F, 0.060F},
               glm::vec3 {0.0F, 0.0F, -arm_swing - shoulder_twist}, arm_tile, kMaterialSkin, 0.10F, 0.0F);
    append_box(mesh, root, glm::vec3 {0.04F, 0.94F, 0.24F}, glm::vec3 {0.060F, 0.31F, 0.060F},
               glm::vec3 {0.0F, 0.0F, arm_swing + shoulder_twist}, arm_tile, kMaterialSkin, 0.10F, 0.0F);

    if (look_down > 0.02F) {
        const auto head_center = glm::vec3 {0.28F + look_down * 0.10F, 1.46F - (1.0F - look_down) * 0.22F, 0.0F};
        const auto head_pitch = glm::radians(glm::clamp(-state.pitch_degrees * 0.62F, -24.0F, 52.0F));
        append_box(mesh, root, head_center, glm::vec3 {0.17F, 0.17F, 0.17F},
                   glm::vec3 {head_pitch, 0.0F, hurt_amount * 0.06F}, PlayerAtlasTile::Skin, kMaterialSkin, 0.16F, 0.0F);
        append_box(mesh, root, head_center + glm::vec3 {-0.02F, 0.10F, 0.0F}, glm::vec3 {0.18F, 0.08F, 0.18F},
                   glm::vec3 {head_pitch, 0.0F, hurt_amount * 0.04F}, PlayerAtlasTile::Hair, kMaterialLeather, 0.10F, 0.0F);
        append_box(mesh, root, head_center + glm::vec3 {0.15F, 0.02F, -0.06F}, glm::vec3 {0.012F, 0.042F, 0.032F},
                   glm::vec3 {head_pitch, 0.0F, 0.0F}, PlayerAtlasTile::Eye, kMaterialSkin, 0.0F, 0.0F);
        append_box(mesh, root, head_center + glm::vec3 {0.15F, 0.02F, 0.06F}, glm::vec3 {0.012F, 0.042F, 0.032F},
                   glm::vec3 {head_pitch, 0.0F, 0.0F}, PlayerAtlasTile::Eye, kMaterialSkin, 0.0F, 0.0F);
        append_box(mesh, root, head_center + glm::vec3 {0.16F, -0.06F, 0.0F}, glm::vec3 {0.010F, 0.020F, 0.050F},
                   glm::vec3 {head_pitch, 0.0F, 0.0F}, PlayerAtlasTile::Mouth, kMaterialSkin, 0.0F, 0.0F);
    }

    return mesh;
}

} // namespace valcraft
