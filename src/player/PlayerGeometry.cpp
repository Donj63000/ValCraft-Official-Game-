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
constexpr float kMaterialRubber = 0.72F;
constexpr float kHurtFlashDuration = 0.35F;

struct FaceDefinition {
    std::array<glm::vec3, 4> corners;
    glm::vec3 normal;
};

struct PlayerBoxTiles {
    std::array<PlayerAtlasTile, 6> faces {};
};

struct PlayerPoseState {
    glm::vec3 camera_forward {0.0F, 0.0F, -1.0F};
    glm::vec3 camera_right {1.0F, 0.0F, 0.0F};
    glm::vec3 body_forward {0.0F, 0.0F, -1.0F};
    glm::vec3 body_right {1.0F, 0.0F, 0.0F};
    float body_visibility = 0.0F;
    float presentation_arm_visibility = 1.0F;
    float walk_amount = 0.0F;
    float stride = 0.0F;
    float stride_cos = 1.0F;
    float breathing_offset = 0.0F;
    float hurt_amount = 0.0F;
    float swim_amount = 0.0F;
    float airborne_amount = 0.0F;
    float landing_amount = 0.0F;
    float mine_arc = 0.0F;
    float mine_pull = 0.0F;
    float place_arc = 0.0F;
    float place_pull = 0.0F;
    float torso_yaw = 0.0F;
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

auto wrap_degrees(float angle) noexcept -> float {
    while (angle <= -180.0F) {
        angle += 360.0F;
    }
    while (angle > 180.0F) {
        angle -= 360.0F;
    }
    return angle;
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

auto make_frame_transform(const glm::mat4& parent,
                          const glm::vec3& translation,
                          const glm::vec3& rotation_radians = glm::vec3 {0.0F}) -> glm::mat4 {
    auto transform = glm::translate(parent, translation);
    transform = glm::rotate(transform, rotation_radians.y, glm::vec3 {0.0F, 1.0F, 0.0F});
    transform = glm::rotate(transform, rotation_radians.z, glm::vec3 {0.0F, 0.0F, 1.0F});
    transform = glm::rotate(transform, rotation_radians.x, glm::vec3 {1.0F, 0.0F, 0.0F});
    return transform;
}

auto make_transform(const glm::mat4& root,
                    const glm::vec3& center,
                    const glm::vec3& rotation_radians,
                    const glm::vec3& half_extent) -> glm::mat4 {
    auto transform = make_frame_transform(root, center, rotation_radians);
    return glm::scale(transform, half_extent * 2.0F);
}

auto make_box_tiles(PlayerAtlasTile front,
                    PlayerAtlasTile back,
                    PlayerAtlasTile top,
                    PlayerAtlasTile bottom,
                    PlayerAtlasTile right,
                    PlayerAtlasTile left) noexcept -> PlayerBoxTiles {
    return {{{front, back, top, bottom, right, left}}};
}

auto uniform_tiles(PlayerAtlasTile tile) noexcept -> PlayerBoxTiles {
    return make_box_tiles(tile, tile, tile, tile, tile, tile);
}

auto hurt_tiles_if_needed(const PlayerBoxTiles& tiles, float hurt_amount, float threshold) noexcept -> PlayerBoxTiles {
    if (hurt_amount > threshold) {
        return uniform_tiles(PlayerAtlasTile::Hurt);
    }
    return tiles;
}

auto player_tile_uvs(PlayerAtlasTile tile) noexcept -> std::array<std::array<float, 2>, 4> {
    const auto tile_coordinates = player_atlas_tile_coordinates(tile);
    const auto uv_step = 1.0F / kPlayerAtlasTilesPerAxis;
    const auto u0 = static_cast<float>(tile_coordinates[0]) * uv_step;
    const auto v0 = static_cast<float>(tile_coordinates[1]) * uv_step;
    const auto u1 = u0 + uv_step;
    const auto v1 = v0 + uv_step;
    return {{
        {u1, v0},
        {u1, v1},
        {u0, v1},
        {u0, v0},
    }};
}

void append_box(CreatureMeshData& mesh,
                const glm::mat4& root,
                const glm::vec3& center,
                const glm::vec3& half_extent,
                const glm::vec3& rotation_radians,
                const PlayerBoxTiles& tiles,
                float material_class,
                float cavity_mask,
                float emissive_strength) {
    if (half_extent.x <= 1.0e-4F || half_extent.y <= 1.0e-4F || half_extent.z <= 1.0e-4F) {
        return;
    }

    const auto transform = make_transform(root, center, rotation_radians, half_extent);
    const auto normal_matrix = glm::transpose(glm::inverse(glm::mat3(transform)));

    for (std::size_t face_index = 0; face_index < box_faces().size(); ++face_index) {
        const auto& face = box_faces()[face_index];
        const auto face_normal = glm::normalize(normal_matrix * face.normal);
        const auto uvs = player_tile_uvs(tiles.faces[face_index]);
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

[[maybe_unused]] void append_box(CreatureMeshData& mesh,
                                 const glm::mat4& root,
                                 const glm::vec3& center,
                                 const glm::vec3& half_extent,
                                 const glm::vec3& rotation_radians,
                                 PlayerAtlasTile tile,
                                 float material_class,
                                 float cavity_mask,
                                 float emissive_strength) {
    append_box(mesh, root, center, half_extent, rotation_radians, uniform_tiles(tile), material_class, cavity_mask, emissive_strength);
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
        return make_rgba(194.0F + grain * 12.0F, 156.0F + soft_grain * 10.0F, 124.0F + grain * 8.0F, 0.0F);
    case PlayerAtlasTile::Hair: {
        const auto highlight = saturate(1.0F - ny * 0.84F + grain * 0.18F);
        return make_rgba(82.0F + highlight * 24.0F, 56.0F + highlight * 16.0F, 36.0F + highlight * 12.0F, 0.0F);
    }
    case PlayerAtlasTile::Shirt: {
        const auto band = 0.45F + 0.55F * std::sin(nx * 8.0F + grain * 4.0F);
        const auto seam = line_mask(nx, 0.5F, 0.07F) * line_mask(ny, 0.48F, 0.30F);
        return make_rgba(42.0F + band * 18.0F + seam * 6.0F,
                         118.0F + soft_grain * 20.0F + seam * 8.0F,
                         188.0F + grain * 24.0F + seam * 10.0F,
                         0.0F);
    }
    case PlayerAtlasTile::Pants: {
        const auto stitch = line_mask(nx, 0.5F, 0.05F) * line_mask(ny, 0.56F, 0.44F);
        return make_rgba(54.0F + grain * 10.0F + stitch * 8.0F,
                         74.0F + soft_grain * 10.0F + stitch * 8.0F,
                         158.0F + grain * 18.0F + stitch * 10.0F,
                         0.0F);
    }
    case PlayerAtlasTile::Shoes: {
        const auto toe = smooth_range(0.30F, 0.86F, 1.0F - ny);
        return make_rgba(42.0F + grain * 6.0F + toe * 12.0F,
                         42.0F + soft_grain * 6.0F + toe * 12.0F,
                         48.0F + grain * 8.0F + toe * 14.0F,
                         0.0F);
    }
    case PlayerAtlasTile::Eye: {
        const auto sclera = radial_falloff(nx, ny, 0.5F, 0.5F, 0.42F);
        const auto iris = radial_falloff(nx, ny, 0.5F, 0.5F, 0.18F);
        const auto pupil = radial_falloff(nx, ny, 0.52F, 0.52F, 0.09F);
        return make_rgba(208.0F + sclera * 24.0F - iris * 44.0F - pupil * 160.0F,
                         220.0F + sclera * 20.0F - iris * 72.0F - pupil * 170.0F,
                         232.0F + sclera * 12.0F - iris * 118.0F - pupil * 180.0F,
                         0.0F);
    }
    case PlayerAtlasTile::Mouth: {
        const auto base = sample_player_tile(PlayerAtlasTile::Skin, x, y);
        const auto lip = line_mask(ny, 0.68F + std::sin((nx - 0.5F) * kPi) * 0.05F, 0.05F);
        return make_rgba(static_cast<float>(base[0]) - lip * 68.0F,
                         static_cast<float>(base[1]) - lip * 102.0F,
                         static_cast<float>(base[2]) - lip * 84.0F,
                         0.0F);
    }
    case PlayerAtlasTile::Hurt: {
        const auto pulse = radial_falloff(nx, ny, 0.5F, 0.5F, 0.74F);
        const auto veins = line_mask(nx, 0.5F + std::sin(ny * 10.0F) * 0.10F, 0.08F) * 0.28F;
        return make_rgba(206.0F + pulse * 26.0F,
                         72.0F + grain * 14.0F - veins * 20.0F,
                         64.0F + soft_grain * 10.0F - veins * 18.0F,
                         0.0F);
    }
    case PlayerAtlasTile::SkinShadow:
        return make_rgba(158.0F + grain * 10.0F, 122.0F + soft_grain * 8.0F, 94.0F + grain * 7.0F, 0.0F);
    case PlayerAtlasTile::HairShadow: {
        const auto highlight = saturate(1.0F - ny * 0.72F + grain * 0.10F);
        return make_rgba(60.0F + highlight * 14.0F, 40.0F + highlight * 10.0F, 24.0F + highlight * 8.0F, 0.0F);
    }
    case PlayerAtlasTile::ShirtShadow: {
        const auto band = 0.45F + 0.55F * std::sin(nx * 7.0F + grain * 4.0F);
        return make_rgba(30.0F + band * 12.0F, 92.0F + soft_grain * 16.0F, 154.0F + grain * 16.0F, 0.0F);
    }
    case PlayerAtlasTile::PantsShadow: {
        const auto band = 0.42F + 0.58F * std::sin(nx * 8.0F + grain * 3.0F);
        return make_rgba(42.0F + band * 10.0F, 56.0F + soft_grain * 9.0F, 122.0F + grain * 14.0F, 0.0F);
    }
    case PlayerAtlasTile::Sole: {
        const auto edge = 1.0F - smoothstep01(edge_distance(nx, ny) / 0.18F);
        return make_rgba(158.0F - edge * 46.0F, 162.0F - edge * 46.0F, 168.0F - edge * 48.0F, 0.0F);
    }
    case PlayerAtlasTile::Sleeve: {
        const auto cuff = line_mask(ny, 0.88F, 0.08F);
        return make_rgba(42.0F + grain * 14.0F + cuff * 44.0F,
                         118.0F + soft_grain * 18.0F + cuff * 44.0F,
                         188.0F + grain * 20.0F + cuff * 32.0F,
                         0.0F);
    }
    case PlayerAtlasTile::Belt: {
        const auto buckle = line_mask(nx, 0.5F, 0.09F) * line_mask(ny, 0.50F, 0.22F);
        return make_rgba(86.0F + grain * 12.0F + buckle * 94.0F,
                         54.0F + soft_grain * 10.0F + buckle * 74.0F,
                         32.0F + grain * 8.0F + buckle * 32.0F,
                         0.0F);
    }
    case PlayerAtlasTile::Face: {
        auto color = sample_player_tile(PlayerAtlasTile::Skin, x, y);
        if (y <= 3) {
            return sample_player_tile(y <= 2 ? PlayerAtlasTile::Hair : PlayerAtlasTile::HairShadow, x, y);
        }
        if (((x >= 3 && x <= 5) || (x >= 10 && x <= 12)) && y >= 6 && y <= 7) {
            return sample_player_tile(PlayerAtlasTile::Eye, x, y);
        }
        if (y >= 10 && y <= 12 && x >= 5 && x <= 10) {
            const auto mouth = sample_player_tile(PlayerAtlasTile::Mouth, x, y);
            color[0] = static_cast<std::uint8_t>(std::max(static_cast<int>(mouth[0]), static_cast<int>(color[0]) - 72));
            color[1] = static_cast<std::uint8_t>(std::min(static_cast<int>(color[1]), static_cast<int>(mouth[1])));
            color[2] = static_cast<std::uint8_t>(std::min(static_cast<int>(color[2]), static_cast<int>(mouth[2])));
        }
        const auto cheek = line_mask(nx, 0.5F, 0.42F) * line_mask(ny, 0.58F, 0.36F) * 6.0F;
        return make_rgba(static_cast<float>(color[0]) + cheek,
                         static_cast<float>(color[1]) + cheek * 0.55F,
                         static_cast<float>(color[2]) + cheek * 0.35F,
                         0.0F);
    }
    case PlayerAtlasTile::Count:
        break;
    }

    return make_rgba(255.0F, 0.0F, 255.0F, 0.0F);
}

void append_full_body(CreatureMeshData& mesh, const PlayerController& player, const PlayerPoseState& pose) {
    const auto& state = player.state();
    const auto shoulder_anchor = player.eye_position()
                               + pose.body_forward * (0.06F + pose.body_visibility * 0.16F + pose.mine_pull * 0.02F)
                               + pose.body_right * (pose.stride * pose.walk_amount * 0.012F +
                                                    std::sin(state.animation_time * 11.0F) * pose.hurt_amount * 0.006F)
                               + glm::vec3 {
                                     0.0F,
                                     -0.24F + pose.breathing_offset + pose.stride_cos * pose.walk_amount * 0.018F -
                                         pose.landing_amount * 0.028F - pose.swim_amount * 0.06F,
                                     0.0F,
                                 };

    auto root = glm::translate(glm::mat4(1.0F), shoulder_anchor);
    root = glm::rotate(root, glm::radians(state.body_yaw_degrees), glm::vec3 {0.0F, 1.0F, 0.0F});

    const auto emissive = pose.hurt_amount * 0.08F;
    const auto shirt_tiles = hurt_tiles_if_needed(
        make_box_tiles(PlayerAtlasTile::Shirt,
                       PlayerAtlasTile::ShirtShadow,
                       PlayerAtlasTile::Shirt,
                       PlayerAtlasTile::Belt,
                       PlayerAtlasTile::ShirtShadow,
                       PlayerAtlasTile::ShirtShadow),
        pose.hurt_amount,
        0.16F);
    const auto belt_tiles = hurt_tiles_if_needed(
        make_box_tiles(PlayerAtlasTile::Belt,
                       PlayerAtlasTile::Belt,
                       PlayerAtlasTile::Belt,
                       PlayerAtlasTile::Pants,
                       PlayerAtlasTile::Belt,
                       PlayerAtlasTile::Belt),
        pose.hurt_amount,
        0.14F);
    const auto sleeve_tiles = hurt_tiles_if_needed(
        make_box_tiles(PlayerAtlasTile::Sleeve,
                       PlayerAtlasTile::ShirtShadow,
                       PlayerAtlasTile::Sleeve,
                       PlayerAtlasTile::SkinShadow,
                       PlayerAtlasTile::ShirtShadow,
                       PlayerAtlasTile::ShirtShadow),
        pose.hurt_amount,
        0.20F);
    const auto hand_tiles = hurt_tiles_if_needed(
        make_box_tiles(PlayerAtlasTile::Skin,
                       PlayerAtlasTile::SkinShadow,
                       PlayerAtlasTile::Skin,
                       PlayerAtlasTile::SkinShadow,
                       PlayerAtlasTile::SkinShadow,
                       PlayerAtlasTile::SkinShadow),
        pose.hurt_amount,
        0.24F);
    const auto pants_tiles = hurt_tiles_if_needed(
        make_box_tiles(PlayerAtlasTile::Pants,
                       PlayerAtlasTile::PantsShadow,
                       PlayerAtlasTile::Belt,
                       PlayerAtlasTile::Shoes,
                       PlayerAtlasTile::PantsShadow,
                       PlayerAtlasTile::PantsShadow),
        pose.hurt_amount,
        0.18F);
    const auto shoes_tiles = hurt_tiles_if_needed(
        make_box_tiles(PlayerAtlasTile::Shoes,
                       PlayerAtlasTile::Shoes,
                       PlayerAtlasTile::Shoes,
                       PlayerAtlasTile::Sole,
                       PlayerAtlasTile::Shoes,
                       PlayerAtlasTile::Shoes),
        pose.hurt_amount,
        0.12F);

    const auto torso_roll = pose.stride * pose.walk_amount * 0.05F + pose.hurt_amount * 0.08F;
    const auto torso_pitch = -0.08F - pose.body_visibility * 0.10F - pose.mine_arc * 0.06F + pose.airborne_amount * 0.10F;
    append_box(mesh,
               root,
               glm::vec3 {0.02F, -0.33F, 0.0F},
               glm::vec3 {0.14F, 0.34F, 0.12F},
               glm::vec3 {torso_roll, pose.torso_yaw, torso_pitch},
               shirt_tiles,
               kMaterialFabric,
               0.10F,
               emissive * 0.60F);
    append_box(mesh,
               root,
               glm::vec3 {-0.02F, -0.59F, 0.0F},
               glm::vec3 {0.13F, 0.05F, 0.12F},
               glm::vec3 {torso_roll * 0.5F, pose.torso_yaw, torso_pitch * 0.5F},
               belt_tiles,
               kMaterialLeather,
               0.12F,
               emissive * 0.24F);

    auto left_arm_pitch = pose.stride * (0.20F + pose.walk_amount * 0.84F) - pose.airborne_amount * 0.16F;
    auto right_arm_pitch = -pose.stride * (0.20F + pose.walk_amount * 0.84F) - pose.airborne_amount * 0.20F -
                           pose.mine_arc * 1.25F - pose.mine_pull * 0.32F - pose.place_arc * 0.55F - pose.place_pull * 0.10F;
    auto left_leg_pitch = -pose.stride * (0.24F + pose.walk_amount * 0.90F) - pose.airborne_amount * 0.18F;
    auto right_leg_pitch = pose.stride * (0.24F + pose.walk_amount * 0.90F) - pose.airborne_amount * 0.18F;

    if (pose.swim_amount > 0.0F) {
        const auto swim_cycle = std::sin(state.animation_time * 6.4F);
        left_arm_pitch = glm::mix(left_arm_pitch, -1.02F + swim_cycle * 0.34F, pose.swim_amount);
        right_arm_pitch = glm::mix(right_arm_pitch, -1.18F - swim_cycle * 0.36F - pose.mine_arc * 0.44F, pose.swim_amount);
        left_leg_pitch = glm::mix(left_leg_pitch, swim_cycle * 0.28F, pose.swim_amount);
        right_leg_pitch = glm::mix(right_leg_pitch, -swim_cycle * 0.28F, pose.swim_amount);
    }

    const auto left_arm_root = make_frame_transform(
        root,
        glm::vec3 {0.03F, -0.05F, -0.22F},
        glm::vec3 {0.06F + pose.walk_amount * 0.04F, pose.torso_yaw * 0.30F + 0.04F, left_arm_pitch});
    append_box(mesh,
               left_arm_root,
               glm::vec3 {0.0F, -0.18F, 0.0F},
               glm::vec3 {0.08F, 0.18F, 0.08F},
               glm::vec3 {0.0F},
               sleeve_tiles,
               kMaterialFabric,
               0.08F,
               emissive * 0.42F);
    append_box(mesh,
               left_arm_root,
               glm::vec3 {0.0F, -0.40F, 0.0F},
               glm::vec3 {0.075F, 0.07F, 0.075F},
               glm::vec3 {0.0F},
               hand_tiles,
               kMaterialSkin,
               0.08F,
               emissive * 0.36F);

    const auto right_arm_root = make_frame_transform(
        root,
        glm::vec3 {0.03F, -0.05F, 0.22F},
        glm::vec3 {
            -0.06F - pose.walk_amount * 0.04F + pose.mine_pull * 0.22F + pose.place_arc * 0.08F,
            pose.torso_yaw * 0.34F - 0.10F - pose.mine_arc * 0.22F + pose.place_arc * 0.12F,
            right_arm_pitch,
        });
    append_box(mesh,
               right_arm_root,
               glm::vec3 {0.0F, -0.18F, 0.0F},
               glm::vec3 {0.08F, 0.18F, 0.08F},
               glm::vec3 {0.0F},
               sleeve_tiles,
               kMaterialFabric,
               0.08F,
               emissive * 0.42F);
    append_box(mesh,
               right_arm_root,
               glm::vec3 {0.0F, -0.40F, 0.0F},
               glm::vec3 {0.075F, 0.07F, 0.075F},
               glm::vec3 {0.0F},
               hand_tiles,
               kMaterialSkin,
               0.08F,
               emissive * 0.36F);

    const auto left_leg_root = make_frame_transform(
        root,
        glm::vec3 {-0.04F, -0.82F, -0.08F},
        glm::vec3 {pose.stride_cos * pose.walk_amount * 0.02F, 0.0F, left_leg_pitch});
    append_box(mesh,
               left_leg_root,
               glm::vec3 {0.0F, -0.22F, 0.0F},
               glm::vec3 {0.09F, 0.22F, 0.09F},
               glm::vec3 {0.0F},
               pants_tiles,
               kMaterialDenim,
               0.10F,
               emissive * 0.28F);
    append_box(mesh,
               left_leg_root,
               glm::vec3 {0.06F, -0.49F, 0.0F},
               glm::vec3 {0.12F, 0.06F, 0.09F},
               glm::vec3 {0.0F},
               shoes_tiles,
               kMaterialRubber,
               0.12F,
               emissive * 0.12F);

    const auto right_leg_root = make_frame_transform(
        root,
        glm::vec3 {-0.04F, -0.82F, 0.08F},
        glm::vec3 {-pose.stride_cos * pose.walk_amount * 0.02F, 0.0F, right_leg_pitch});
    append_box(mesh,
               right_leg_root,
               glm::vec3 {0.0F, -0.22F, 0.0F},
               glm::vec3 {0.09F, 0.22F, 0.09F},
               glm::vec3 {0.0F},
               pants_tiles,
               kMaterialDenim,
               0.10F,
               emissive * 0.28F);
    append_box(mesh,
               right_leg_root,
               glm::vec3 {0.06F, -0.49F, 0.0F},
               glm::vec3 {0.12F, 0.06F, 0.09F},
               glm::vec3 {0.0F},
               shoes_tiles,
               kMaterialRubber,
               0.12F,
               emissive * 0.12F);
}

void append_presentation_arm(CreatureMeshData& mesh, const PlayerController& player, const PlayerPoseState& pose) {
    if (pose.presentation_arm_visibility <= 0.05F) {
        return;
    }

    const auto& state = player.state();
    const auto visibility = pose.presentation_arm_visibility;
    const auto camera_up = glm::normalize(glm::cross(pose.camera_right, pose.camera_forward));
    const auto jitter = std::sin(state.animation_time * 12.0F + pose.mine_pull * 3.0F) * pose.walk_amount * 0.006F;
    const auto anchor = player.eye_position()
                      + pose.camera_forward * (0.18F + pose.mine_pull * 0.04F - pose.place_arc * 0.02F)
                      + pose.camera_right * (0.22F + pose.place_pull * 0.02F)
                      + camera_up * (-0.30F + pose.stride_cos * pose.walk_amount * 0.014F - pose.hurt_amount * 0.04F + jitter);

    auto root = glm::mat4(1.0F);
    // Je reconstruis la base exacte de ma camera pour que le bras FPS suive vraiment mon regard.
    root[0] = glm::vec4(pose.camera_right, 0.0F);
    root[1] = glm::vec4(camera_up, 0.0F);
    root[2] = glm::vec4(-pose.camera_forward, 0.0F);
    root[3] = glm::vec4(anchor, 1.0F);

    const auto emissive = pose.hurt_amount * 0.10F;
    const auto sleeve_tiles = hurt_tiles_if_needed(
        make_box_tiles(PlayerAtlasTile::Sleeve,
                       PlayerAtlasTile::ShirtShadow,
                       PlayerAtlasTile::Sleeve,
                       PlayerAtlasTile::SkinShadow,
                       PlayerAtlasTile::ShirtShadow,
                       PlayerAtlasTile::ShirtShadow),
        pose.hurt_amount,
        0.20F);
    const auto hand_tiles = hurt_tiles_if_needed(
        make_box_tiles(PlayerAtlasTile::Skin,
                       PlayerAtlasTile::SkinShadow,
                       PlayerAtlasTile::Skin,
                       PlayerAtlasTile::SkinShadow,
                       PlayerAtlasTile::SkinShadow,
                       PlayerAtlasTile::SkinShadow),
        pose.hurt_amount,
        0.24F);

    const auto shoulder_root = make_frame_transform(
        root,
        glm::vec3 {0.0F},
        glm::vec3 {
            0.24F + pose.place_arc * 0.08F + pose.mine_pull * 0.12F,
            -0.08F - pose.place_arc * 0.18F + pose.mine_arc * 0.10F,
            -1.02F - pose.mine_arc * 0.60F - pose.mine_pull * 0.18F - pose.place_arc * 0.28F - pose.hurt_amount * 0.12F,
        });
    append_box(mesh,
               shoulder_root,
               glm::vec3 {0.0F, -0.18F, 0.0F},
               glm::vec3 {0.085F, 0.18F, 0.085F} * (0.92F + visibility * 0.08F),
               glm::vec3 {0.0F},
               sleeve_tiles,
               kMaterialFabric,
               0.08F,
               emissive * 0.52F);

    const auto elbow_root = make_frame_transform(
        shoulder_root,
        glm::vec3 {0.0F, -0.36F, 0.0F},
        glm::vec3 {0.04F, -0.02F, -0.34F - pose.mine_arc * 1.05F - pose.mine_pull * 0.18F - pose.place_arc * 0.34F});
    append_box(mesh,
               elbow_root,
               glm::vec3 {0.02F, -0.10F, 0.0F},
               glm::vec3 {0.10F, 0.10F, 0.085F} * (0.92F + visibility * 0.08F),
               glm::vec3 {0.0F},
               hand_tiles,
               kMaterialSkin,
               0.08F,
               emissive * 0.48F);
    append_box(mesh,
               elbow_root,
               glm::vec3 {0.10F, -0.20F, 0.0F},
               glm::vec3 {0.06F, 0.05F, 0.06F} * (0.92F + visibility * 0.08F),
               glm::vec3 {0.0F},
               hand_tiles,
               kMaterialSkin,
               0.06F,
               emissive * 0.44F);
}

} // namespace

auto player_atlas_tile_coordinates(PlayerAtlasTile tile) noexcept -> std::array<int, 2> {
    const auto index = static_cast<int>(tile);
    return {index % static_cast<int>(kPlayerAtlasTilesPerAxis), index / static_cast<int>(kPlayerAtlasTilesPerAxis)};
}

auto build_player_atlas_pixels() -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kPlayerAtlasSize * kPlayerAtlasSize * 4), 0);
    for (int tile_index = 0; tile_index < static_cast<int>(PlayerAtlasTile::Count); ++tile_index) {
        const auto tile = static_cast<PlayerAtlasTile>(tile_index);
        const auto coordinates = player_atlas_tile_coordinates(tile);
        fill_tile(pixels, coordinates[0], coordinates[1], [tile](int x, int y) {
            return sample_player_tile(tile, x, y);
        });
    }
    return pixels;
}

auto build_player_mesh(const PlayerController& player, PlayerMeshView view) -> CreatureMeshData {
    CreatureMeshData mesh {};

    const auto& state = player.state();
    const auto first_person_view = view == PlayerMeshView::FirstPerson;
    const auto hurt_amount = saturate(state.hurt_timer / kHurtFlashDuration);
    const auto body_visibility = first_person_view ? smooth_range(34.0F, 78.0F, -state.pitch_degrees) : 0.0F;
    const auto presentation_arm_visibility = first_person_view ? 1.0F : 0.0F;
    const auto walk_reference_speed = state.swimming ? 3.8F : (state.fly_mode ? 10.0F : 5.6F);
    const auto walk_amount = saturate(glm::length(glm::vec2 {state.velocity.x, state.velocity.z}) / std::max(walk_reference_speed, 0.001F));

    auto camera_forward = player.look_direction();
    if (glm::dot(camera_forward, camera_forward) <= 1.0e-6F) {
        camera_forward = glm::vec3 {0.0F, 0.0F, -1.0F};
    } else {
        camera_forward = glm::normalize(camera_forward);
    }

    auto camera_right = glm::cross(camera_forward, glm::vec3 {0.0F, 1.0F, 0.0F});
    if (glm::dot(camera_right, camera_right) <= 1.0e-6F) {
        camera_right = glm::vec3 {1.0F, 0.0F, 0.0F};
    } else {
        camera_right = glm::normalize(camera_right);
    }

    const auto body_yaw_radians = glm::radians(state.body_yaw_degrees);
    auto body_forward = glm::vec3 {std::cos(body_yaw_radians), 0.0F, std::sin(body_yaw_radians)};
    if (glm::dot(body_forward, body_forward) <= 1.0e-6F) {
        body_forward = glm::vec3 {0.0F, 0.0F, -1.0F};
    } else {
        body_forward = glm::normalize(body_forward);
    }
    auto body_right = glm::cross(body_forward, glm::vec3 {0.0F, 1.0F, 0.0F});
    if (glm::dot(body_right, body_right) <= 1.0e-6F) {
        body_right = camera_right;
    } else {
        body_right = glm::normalize(body_right);
    }

    const auto action_arc = [](bool active, float progress) noexcept {
        return active ? std::sin(glm::clamp(progress, 0.0F, 1.0F) * kPi) : 0.0F;
    };
    const auto action_pull = [](bool active, float progress) noexcept {
        return active ? smoothstep01(1.0F - glm::clamp(progress, 0.0F, 1.0F)) : 0.0F;
    };

    PlayerPoseState pose {};
    pose.camera_forward = camera_forward;
    pose.camera_right = camera_right;
    pose.body_forward = body_forward;
    pose.body_right = body_right;
    pose.body_visibility = body_visibility;
    pose.presentation_arm_visibility = presentation_arm_visibility;
    pose.walk_amount = walk_amount;
    pose.stride = std::sin(state.step_phase);
    pose.stride_cos = std::cos(state.step_phase);
    pose.breathing_offset = std::sin(state.animation_time * 1.75F) * 0.014F;
    pose.hurt_amount = hurt_amount;
    pose.swim_amount = state.swimming ? 1.0F : 0.0F;
    pose.airborne_amount = (!state.on_ground && !state.swimming && !state.fly_mode) ? smoothstep01(state.airborne_time / 0.15F) : 0.0F;
    pose.landing_amount = state.landing_impact * state.landing_impact;
    pose.mine_arc = action_arc(state.primary_action_active, state.primary_action_progress);
    pose.mine_pull = action_pull(state.primary_action_active, state.primary_action_progress);
    pose.place_arc = action_arc(state.secondary_action_active, state.secondary_action_progress);
    pose.place_pull = action_pull(state.secondary_action_active, state.secondary_action_progress);
    pose.torso_yaw = glm::radians(glm::clamp(wrap_degrees(state.yaw_degrees - state.body_yaw_degrees), -46.0F, 46.0F) * 0.35F);

    if (first_person_view) {
        // Vue FPS locale facon Minecraft : on garde uniquement le bras de presentation,
        // attache a la camera, pour eviter tout decalage avec le view-model et ne jamais
        // masquer le sol quand le joueur regarde vers le bas.
        if (pose.presentation_arm_visibility > 0.05F) {
            append_presentation_arm(mesh, player, pose);
        }
        return mesh;
    }

    append_full_body(mesh, player, pose);
    return mesh;
}

} // namespace valcraft
