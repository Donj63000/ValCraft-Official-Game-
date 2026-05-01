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
constexpr float kMaterialMetal = 0.92F;
constexpr float kHurtFlashDuration = 0.35F;

struct FaceDefinition {
    std::array<glm::vec3, 4> corners;
    glm::vec3 normal;
};

struct PlayerBoxTiles {
    std::array<PlayerAtlasTile, 6> faces {};
};

struct CameraBasis {
    glm::vec3 forward {0.0F, 0.0F, -1.0F};
    glm::vec3 right {1.0F, 0.0F, 0.0F};
    glm::vec3 up {0.0F, 1.0F, 0.0F};
};

struct PlayerWorldAvatarPose {
    glm::vec3 body_forward {0.0F, 0.0F, -1.0F};
    glm::vec3 body_right {1.0F, 0.0F, 0.0F};
    float body_visibility = 0.0F;
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

struct PlayerViewModelRigState {
    CameraBasis camera {};
    glm::vec3 anchor {0.0F};
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
    float look_sway_yaw = 0.0F;
    float look_sway_pitch = 0.0F;
    float bob_side = 0.0F;
    float bob_vertical = 0.0F;
    float bob_depth = 0.0F;
    float bob_roll = 0.0F;
    bool weapon_pose = false;
};

auto make_face_definition(std::array<glm::vec3, 4> corners, const glm::vec3& normal) -> FaceDefinition {
    return {corners, normal};
}

[[maybe_unused]] auto box_faces() -> const std::array<FaceDefinition, 6>& {
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

auto safe_normalize(const glm::vec3& value, const glm::vec3& fallback) noexcept -> glm::vec3 {
    if (glm::dot(value, value) <= 1.0e-6F) {
        return fallback;
    }
    return glm::normalize(value);
}

auto make_camera_basis(const PlayerController& player) noexcept -> CameraBasis {
    auto forward = safe_normalize(player.look_direction(), glm::vec3 {0.0F, 0.0F, -1.0F});
    auto right = glm::cross(forward, glm::vec3 {0.0F, 1.0F, 0.0F});
    right = safe_normalize(right, glm::vec3 {1.0F, 0.0F, 0.0F});
    auto up = glm::cross(right, forward);
    up = safe_normalize(up, glm::vec3 {0.0F, 1.0F, 0.0F});
    return {forward, right, up};
}

auto make_body_forward(float yaw_degrees) noexcept -> glm::vec3 {
    const auto yaw_radians = glm::radians(yaw_degrees);
    return safe_normalize(glm::vec3 {std::cos(yaw_radians), 0.0F, std::sin(yaw_radians)}, glm::vec3 {0.0F, 0.0F, -1.0F});
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

[[maybe_unused]] auto player_tile_uvs(PlayerAtlasTile tile) noexcept -> std::array<std::array<float, 2>, 4> {
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

auto player_tile_uv_rect(PlayerAtlasTile tile) noexcept -> BoxUvRect {
    const auto tile_coordinates = player_atlas_tile_coordinates(tile);
    const auto uv_step = 1.0F / kPlayerAtlasTilesPerAxis;
    const auto u0 = static_cast<float>(tile_coordinates[0]) * uv_step;
    const auto v0 = static_cast<float>(tile_coordinates[1]) * uv_step;
    return {u0, v0, u0 + uv_step, v0 + uv_step};
}

auto make_box_uvs(const PlayerBoxTiles& tiles) noexcept -> std::array<BoxUvRect, 6> {
    std::array<BoxUvRect, 6> face_uvs {};
    for (std::size_t face_index = 0; face_index < face_uvs.size(); ++face_index) {
        face_uvs[face_index] = player_tile_uv_rect(tiles.faces[face_index]);
    }
    return face_uvs;
}

void append_box(std::vector<CreaturePartInstance>& mesh,
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

    mesh.push_back({
        make_transform(root, center, rotation_radians, half_extent),
        make_box_uvs(tiles),
        0.0F,
        0.0F,
        material_class,
        cavity_mask,
        emissive_strength,
    });
}

auto transform_translation(const glm::mat4& transform) noexcept -> glm::vec3 {
    return {transform[3].x, transform[3].y, transform[3].z};
}

auto action_arc(bool active, float progress) noexcept -> float {
    return active ? std::sin(glm::clamp(progress, 0.0F, 1.0F) * kPi) : 0.0F;
}

auto action_pull(bool active, float progress) noexcept -> float {
    return active ? smoothstep01(1.0F - glm::clamp(progress, 0.0F, 1.0F)) : 0.0F;
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
    case PlayerAtlasTile::SwordBlade: {
        const auto fuller = line_mask(nx, 0.50F + std::sin(ny * 7.0F) * 0.018F, 0.10F);
        const auto longitudinal = 1.0F - smoothstep01(std::abs(ny - 0.46F) / 0.58F);
        const auto bevel = 1.0F - smoothstep01(edge_distance(nx, ny) / 0.22F);
        const auto cold_glint = line_mask(nx, 0.32F + std::sin(ny * 12.0F) * 0.026F, 0.045F) * 0.75F;
        return make_rgba(148.0F + fuller * 34.0F + longitudinal * 30.0F + bevel * 18.0F + cold_glint * 44.0F + grain * 8.0F,
                         162.0F + fuller * 42.0F + longitudinal * 34.0F + bevel * 20.0F + cold_glint * 48.0F + soft_grain * 8.0F,
                         180.0F + fuller * 52.0F + longitudinal * 40.0F + bevel * 24.0F + cold_glint * 56.0F + grain * 7.0F,
                         0.0F);
    }
    case PlayerAtlasTile::SwordEdge: {
        const auto bevel_line = std::max(line_mask(nx, 0.18F, 0.075F), line_mask(nx, 0.82F, 0.075F));
        const auto sharpened_tip = smooth_range(0.58F, 0.98F, ny);
        const auto shine = bevel_line * (0.70F + sharpened_tip * 0.35F);
        return make_rgba(208.0F + shine * 42.0F + grain * 7.0F,
                         220.0F + shine * 36.0F + soft_grain * 7.0F,
                         230.0F + shine * 30.0F + grain * 6.0F,
                         0.0F);
    }
    case PlayerAtlasTile::SwordGuard: {
        const auto bevel = 1.0F - smoothstep01(edge_distance(nx, ny) / 0.20F);
        const auto crest = line_mask(ny, 0.42F, 0.18F);
        const auto warm_metal = crest * 0.74F + bevel * 0.30F;
        return make_rgba(152.0F + warm_metal * 78.0F + grain * 9.0F,
                         112.0F + warm_metal * 58.0F + soft_grain * 8.0F,
                         44.0F + warm_metal * 30.0F + grain * 5.0F,
                         0.0F);
    }
    case PlayerAtlasTile::SwordGrip: {
        const auto wrap_a = line_mask(glm::fract((ny + nx * 0.30F) * 5.0F), 0.50F, 0.15F);
        const auto wrap_b = line_mask(glm::fract((ny - nx * 0.22F) * 5.0F), 0.50F, 0.08F);
        const auto leather_edge = 1.0F - smoothstep01(edge_distance(nx, ny) / 0.18F);
        const auto wrap = std::max(wrap_a, wrap_b);
        return make_rgba(58.0F + wrap * 55.0F - leather_edge * 12.0F + grain * 10.0F,
                         36.0F + wrap * 34.0F - leather_edge * 8.0F + soft_grain * 8.0F,
                         24.0F + wrap * 20.0F - leather_edge * 5.0F + grain * 6.0F,
                         0.0F);
    }
    case PlayerAtlasTile::SwordPommel: {
        const auto shine = radial_falloff(nx, ny, 0.40F, 0.34F, 0.48F);
        const auto rim = 1.0F - smoothstep01(edge_distance(nx, ny) / 0.20F);
        return make_rgba(120.0F + shine * 80.0F + rim * 18.0F + grain * 8.0F,
                         124.0F + shine * 72.0F + rim * 16.0F + soft_grain * 8.0F,
                         126.0F + shine * 56.0F + rim * 14.0F + grain * 6.0F,
                         0.0F);
    }

    case PlayerAtlasTile::Count:
        break;
    }

    return make_rgba(255.0F, 0.0F, 255.0F, 0.0F);
}

auto build_world_avatar_pose(const PlayerController& player) -> PlayerWorldAvatarPose {
    const auto& state = player.state();
    const auto hurt_amount = saturate(state.hurt_timer / kHurtFlashDuration);
    const auto walk_reference_speed = state.swimming ? 3.8F : (state.fly_mode ? 10.0F : 5.6F);
    const auto walk_amount = saturate(glm::length(glm::vec2 {state.velocity.x, state.velocity.z}) / std::max(walk_reference_speed, 0.001F));
    const auto body_forward = make_body_forward(state.body_yaw_degrees);
    const auto camera_basis = make_camera_basis(player);
    auto body_right = glm::cross(body_forward, glm::vec3 {0.0F, 1.0F, 0.0F});
    body_right = safe_normalize(body_right, camera_basis.right);

    PlayerWorldAvatarPose pose {};
    pose.body_forward = body_forward;
    pose.body_right = body_right;
    pose.body_visibility = 0.0F;
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
    return pose;
}

auto build_viewmodel_rig_state(const PlayerController& player, BlockId held_item) -> PlayerViewModelRigState {
    const auto& state = player.state();
    const auto held_item_id = block_item_id(held_item);
    const auto weapon_pose = held_item_id == to_block_id(BlockType::Sword);
    const auto hurt_amount = saturate(state.hurt_timer / kHurtFlashDuration);
    const auto walk_reference_speed = state.swimming ? 3.8F : (state.fly_mode ? 10.0F : 5.6F);
    const auto walk_amount = saturate(glm::length(glm::vec2 {state.velocity.x, state.velocity.z}) / std::max(walk_reference_speed, 0.001F));
    const auto stride = std::sin(state.step_phase);
    const auto stride_cos = std::cos(state.step_phase);
    const auto breathing_offset = std::sin(state.animation_time * 1.75F) * 0.014F;
    const auto airborne_amount = (!state.on_ground && !state.swimming && !state.fly_mode) ? smoothstep01(state.airborne_time / 0.15F) : 0.0F;
    const auto landing_amount = state.landing_impact * state.landing_impact;
    const auto primary_progress = saturate(state.primary_action_progress);
    const auto weapon_peak = state.primary_action_active && weapon_pose ? std::sin(primary_progress * kPi) : 0.0F;
    const auto weapon_snap = weapon_pose ? smooth_range(0.02F, 0.36F, primary_progress) * (1.0F - smooth_range(0.62F, 1.0F, primary_progress)) : 0.0F;
    const auto mine_arc_value = weapon_pose ? weapon_peak * 1.08F + weapon_snap * 0.34F : action_arc(state.primary_action_active, state.primary_action_progress);
    const auto mine_pull_value = weapon_pose ? weapon_peak * 0.54F + weapon_snap * 0.16F : action_pull(state.primary_action_active, state.primary_action_progress);
    const auto place_arc_value = action_arc(state.secondary_action_active, state.secondary_action_progress);
    const auto place_pull_value = action_pull(state.secondary_action_active, state.secondary_action_progress);

    PlayerViewModelRigState rig {};
    rig.camera = make_camera_basis(player);
    rig.walk_amount = walk_amount;
    rig.stride = stride;
    rig.stride_cos = stride_cos;
    rig.breathing_offset = breathing_offset;
    rig.hurt_amount = hurt_amount;
    rig.swim_amount = state.swimming ? 1.0F : 0.0F;
    rig.airborne_amount = airborne_amount;
    rig.landing_amount = landing_amount;
    rig.mine_arc = mine_arc_value;
    rig.mine_pull = mine_pull_value;
    rig.place_arc = place_arc_value;
    rig.place_pull = place_pull_value;
    rig.look_sway_yaw = state.look_sway_yaw;
    rig.look_sway_pitch = state.look_sway_pitch;
    rig.bob_side = stride * walk_amount * 0.018F;
    rig.bob_vertical = stride_cos * walk_amount * 0.012F - landing_amount * 0.050F;
    rig.bob_depth = std::abs(stride_cos) * walk_amount * 0.014F + airborne_amount * 0.012F;
    rig.bob_roll = stride * walk_amount * 0.050F;
    rig.weapon_pose = weapon_pose;
    rig.anchor = player.eye_position()
               + rig.camera.right * (0.66F + rig.bob_side + place_pull_value * 0.024F + (weapon_pose ? 0.045F : 0.0F))
               + rig.camera.up * (-0.31F + rig.breathing_offset * 0.40F + rig.bob_vertical - hurt_amount * 0.040F -
                                  (weapon_pose ? 0.035F + weapon_peak * 0.035F : 0.0F))
               + rig.camera.forward * (0.46F + rig.bob_depth + mine_pull_value * 0.042F - place_arc_value * 0.022F +
                                       (weapon_pose ? 0.060F - weapon_peak * 0.050F : 0.0F));
    return rig;
}

void append_full_body(std::vector<CreaturePartInstance>& mesh, const PlayerController& player, const PlayerWorldAvatarPose& pose) {
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

void append_viewmodel_arm(PlayerViewModelParts& output, const PlayerController& player, const PlayerViewModelRigState& rig) {
    auto& mesh = output.parts;
    const auto camera_up = rig.camera.up;

    auto root = glm::mat4(1.0F);
    root[0] = glm::vec4(rig.camera.right, 0.0F);
    root[1] = glm::vec4(camera_up, 0.0F);
    root[2] = glm::vec4(-rig.camera.forward, 0.0F);
    root[3] = glm::vec4(rig.anchor, 1.0F);

    const auto emissive = rig.hurt_amount * 0.10F;
    const auto sleeve_tiles = hurt_tiles_if_needed(
        make_box_tiles(PlayerAtlasTile::Sleeve,
                       PlayerAtlasTile::ShirtShadow,
                       PlayerAtlasTile::Sleeve,
                       PlayerAtlasTile::SkinShadow,
                       PlayerAtlasTile::ShirtShadow,
                       PlayerAtlasTile::ShirtShadow),
        rig.hurt_amount,
        0.20F);
    const auto hand_tiles = hurt_tiles_if_needed(
        make_box_tiles(PlayerAtlasTile::Skin,
                       PlayerAtlasTile::SkinShadow,
                       PlayerAtlasTile::Skin,
                       PlayerAtlasTile::SkinShadow,
                       PlayerAtlasTile::SkinShadow,
                       PlayerAtlasTile::SkinShadow),
        rig.hurt_amount,
        0.24F);

    const auto sway_yaw = rig.look_sway_yaw * 0.20F;
    const auto sway_pitch = rig.look_sway_pitch * 0.16F;
    const auto sway_roll = -rig.look_sway_yaw * 0.12F + rig.look_sway_pitch * 0.05F;
    const auto weapon_idle = rig.weapon_pose ? 1.0F : 0.0F;
    const auto weapon_swing = rig.weapon_pose ? saturate(std::max(rig.mine_arc, rig.mine_pull)) : 0.0F;

    const auto shoulder_pitch_base =
        0.20F + sway_pitch + rig.airborne_amount * 0.08F - rig.landing_amount * 0.08F + weapon_idle * 0.08F - weapon_swing * 0.16F;
    const auto shoulder_yaw_base = -0.18F + sway_yaw - rig.place_arc * 0.18F + rig.mine_arc * 0.10F + weapon_idle * 0.12F - weapon_swing * 0.26F;
    const auto shoulder_roll_base = -1.12F - rig.mine_arc * 0.70F - rig.mine_pull * 0.10F - rig.place_arc * 0.34F -
                                    rig.hurt_amount * 0.14F - rig.bob_roll - sway_roll - weapon_idle * 0.18F - weapon_swing * 0.50F;

    auto shoulder_rotation = glm::vec3 {
        shoulder_pitch_base,
        shoulder_yaw_base,
        shoulder_roll_base,
    };
    auto elbow_rotation = glm::vec3 {
        0.10F + rig.mine_arc * 0.48F + rig.place_arc * 0.24F + weapon_idle * 0.14F + weapon_swing * 0.20F,
        -0.02F + sway_yaw * 0.60F - rig.place_pull * 0.10F + weapon_idle * 0.08F - weapon_swing * 0.30F,
        -0.34F - rig.mine_arc * 0.92F - rig.mine_pull * 0.16F - rig.place_arc * 0.42F - rig.hurt_amount * 0.08F - weapon_idle * 0.22F -
            weapon_swing * 0.55F,
    };
    auto wrist_rotation = glm::vec3 {
        -0.02F + rig.mine_arc * 0.12F + rig.place_arc * 0.06F + weapon_idle * 0.16F - weapon_swing * 0.24F,
        0.08F + sway_yaw * 0.38F + rig.place_pull * 0.08F + weapon_idle * 0.12F - weapon_swing * 0.10F,
        -0.08F - rig.mine_arc * 0.22F - rig.place_arc * 0.14F + rig.bob_roll * 0.28F - weapon_idle * 0.34F - weapon_swing * 0.72F,
    };

    if (rig.swim_amount > 0.0F) {
        const auto swim_cycle = std::sin(player.state().animation_time * 5.0F);
        shoulder_rotation = glm::mix(shoulder_rotation,
                                     glm::vec3 {0.58F + swim_cycle * 0.10F, -0.04F, -1.42F - rig.mine_arc * 0.18F},
                                     rig.swim_amount);
        elbow_rotation = glm::mix(elbow_rotation,
                                  glm::vec3 {0.26F + swim_cycle * 0.14F, 0.02F, -0.64F - rig.mine_arc * 0.40F},
                                  rig.swim_amount);
        wrist_rotation = glm::mix(wrist_rotation,
                                  glm::vec3 {0.06F + swim_cycle * 0.08F, 0.08F, -0.16F},
                                  rig.swim_amount);
    }

    const auto shoulder_root = make_frame_transform(root, glm::vec3 {0.0F}, shoulder_rotation);
    const auto elbow_root = make_frame_transform(shoulder_root, glm::vec3 {0.0F, -0.26F, 0.0F}, elbow_rotation);
    const auto wrist_root = make_frame_transform(elbow_root, glm::vec3 {0.02F, -0.24F, 0.0F}, wrist_rotation);
    const auto item_socket = make_frame_transform(
        wrist_root,
        glm::vec3 {0.17F + weapon_idle * 0.030F, -0.15F + weapon_idle * 0.015F, -weapon_idle * 0.012F},
        glm::vec3 {0.18F + rig.place_pull * 0.08F + weapon_idle * 0.18F - weapon_swing * 0.08F,
                   -0.08F + rig.mine_pull * 0.04F + weapon_idle * 0.06F + weapon_swing * 0.14F,
                   -0.26F - rig.mine_arc * 0.12F - weapon_idle * 0.52F - weapon_swing * 0.38F});

    append_box(mesh,
               shoulder_root,
               glm::vec3 {0.0F, -0.12F, 0.0F},
               glm::vec3 {0.088F, 0.12F, 0.088F},
               glm::vec3 {0.0F},
               sleeve_tiles,
               kMaterialFabric,
               0.08F,
               emissive * 0.46F);
    append_box(mesh,
               elbow_root,
               glm::vec3 {0.0F, -0.12F, 0.0F},
               glm::vec3 {0.082F, 0.14F, 0.082F},
               glm::vec3 {0.0F},
               sleeve_tiles,
               kMaterialFabric,
               0.08F,
               emissive * 0.42F);
    append_box(mesh,
               wrist_root,
               glm::vec3 {0.05F, -0.06F, 0.0F},
               glm::vec3 {0.11F, 0.08F, 0.09F},
               glm::vec3 {0.0F},
               hand_tiles,
               kMaterialSkin,
               0.08F,
               emissive * 0.40F);
    append_box(mesh,
               wrist_root,
               glm::vec3 {0.13F, -0.14F, 0.0F},
               glm::vec3 {0.055F, 0.045F, 0.06F},
               glm::vec3 {0.0F},
               hand_tiles,
               kMaterialSkin,
               0.06F,
               emissive * 0.36F);

    output.pose.root_position = rig.anchor;
    output.pose.shoulder_position = transform_translation(shoulder_root);
    output.pose.elbow_position = transform_translation(elbow_root);
    output.pose.wrist_position = transform_translation(wrist_root);
    output.pose.item_socket_transform = item_socket;
    output.pose.look_sway_yaw = rig.look_sway_yaw;
    output.pose.look_sway_pitch = rig.look_sway_pitch;
    output.pose.walk_bob = rig.bob_vertical;
    output.pose.action_swing = std::max(std::max(rig.mine_arc, rig.place_arc), std::max(rig.mine_pull, rig.place_pull));
}

void append_viewmodel_held_item(PlayerViewModelParts& output, BlockId held_item) {
    held_item = block_item_id(held_item);
    if (held_item != to_block_id(BlockType::Sword)) {
        return;
    }

    auto& mesh = output.parts;
    const auto slash = saturate(output.pose.action_swing);
    const auto sword_root = make_frame_transform(
        output.pose.item_socket_transform,
        glm::vec3 {0.026F + slash * 0.010F, 0.012F - slash * 0.020F, -0.018F - slash * 0.025F},
        glm::vec3 {0.10F + slash * 0.22F, 0.18F + slash * 0.08F, -0.66F - slash * 0.34F});

    const auto blade_tiles = make_box_tiles(PlayerAtlasTile::SwordBlade,
                                            PlayerAtlasTile::SwordBlade,
                                            PlayerAtlasTile::SwordEdge,
                                            PlayerAtlasTile::SwordEdge,
                                            PlayerAtlasTile::SwordEdge,
                                            PlayerAtlasTile::SwordEdge);
    const auto edge_tiles = uniform_tiles(PlayerAtlasTile::SwordEdge);
    const auto guard_tiles = uniform_tiles(PlayerAtlasTile::SwordGuard);
    const auto grip_tiles = uniform_tiles(PlayerAtlasTile::SwordGrip);
    const auto pommel_tiles = uniform_tiles(PlayerAtlasTile::SwordPommel);

    append_box(mesh,
               sword_root,
               glm::vec3 {0.0F, 0.46F, 0.0F},
               glm::vec3 {0.026F, 0.43F, 0.012F},
               glm::vec3 {0.0F},
               blade_tiles,
               kMaterialMetal,
               0.07F,
               0.0F);
    append_box(mesh,
               sword_root,
               glm::vec3 {-0.036F, 0.47F, 0.0F},
               glm::vec3 {0.008F, 0.43F, 0.016F},
               glm::vec3 {0.0F},
               edge_tiles,
               kMaterialMetal,
               0.05F,
               0.0F);
    append_box(mesh,
               sword_root,
               glm::vec3 {0.036F, 0.47F, 0.0F},
               glm::vec3 {0.008F, 0.43F, 0.016F},
               glm::vec3 {0.0F},
               edge_tiles,
               kMaterialMetal,
               0.05F,
               0.0F);
    append_box(mesh,
               sword_root,
               glm::vec3 {0.0F, 0.48F, 0.014F},
               glm::vec3 {0.010F, 0.38F, 0.006F},
               glm::vec3 {0.0F},
               blade_tiles,
               kMaterialMetal,
               0.05F,
               0.0F);
    append_box(mesh,
               sword_root,
               glm::vec3 {0.0F, 0.91F, 0.0F},
               glm::vec3 {0.020F, 0.070F, 0.012F},
               glm::vec3 {0.0F},
               blade_tiles,
               kMaterialMetal,
               0.04F,
               0.0F);

    append_box(mesh,
               sword_root,
               glm::vec3 {0.0F, -0.055F, 0.0F},
               glm::vec3 {0.074F, 0.035F, 0.032F},
               glm::vec3 {0.0F},
               guard_tiles,
               kMaterialMetal,
               0.12F,
               0.0F);
    append_box(mesh,
               sword_root,
               glm::vec3 {-0.148F, -0.058F, 0.0F},
               glm::vec3 {0.088F, 0.026F, 0.028F},
               glm::vec3 {0.0F, 0.0F, -0.10F},
               guard_tiles,
               kMaterialMetal,
               0.12F,
               0.0F);
    append_box(mesh,
               sword_root,
               glm::vec3 {0.148F, -0.058F, 0.0F},
               glm::vec3 {0.088F, 0.026F, 0.028F},
               glm::vec3 {0.0F, 0.0F, 0.10F},
               guard_tiles,
               kMaterialMetal,
               0.12F,
               0.0F);

    append_box(mesh,
               sword_root,
               glm::vec3 {0.0F, -0.205F, 0.0F},
               glm::vec3 {0.043F, 0.135F, 0.036F},
               glm::vec3 {0.0F},
               grip_tiles,
               kMaterialLeather,
               0.20F,
               0.0F);
    append_box(mesh,
               sword_root,
               glm::vec3 {0.0F, -0.105F, 0.0F},
               glm::vec3 {0.047F, 0.012F, 0.039F},
               glm::vec3 {0.0F},
               pommel_tiles,
               kMaterialMetal,
               0.12F,
               0.0F);
    append_box(mesh,
               sword_root,
               glm::vec3 {0.0F, -0.205F, 0.0F},
               glm::vec3 {0.047F, 0.012F, 0.039F},
               glm::vec3 {0.0F},
               pommel_tiles,
               kMaterialMetal,
               0.12F,
               0.0F);
    append_box(mesh,
               sword_root,
               glm::vec3 {0.0F, -0.305F, 0.0F},
               glm::vec3 {0.047F, 0.012F, 0.039F},
               glm::vec3 {0.0F},
               pommel_tiles,
               kMaterialMetal,
               0.12F,
               0.0F);

    append_box(mesh,
               sword_root,
               glm::vec3 {0.0F, -0.365F, 0.0F},
               glm::vec3 {0.060F, 0.044F, 0.044F},
               glm::vec3 {0.0F},
               pommel_tiles,
               kMaterialMetal,
               0.14F,
               0.0F);
    append_box(mesh,
               sword_root,
               glm::vec3 {0.0F, -0.410F, 0.0F},
               glm::vec3 {0.036F, 0.018F, 0.036F},
               glm::vec3 {0.0F},
               guard_tiles,
               kMaterialMetal,
               0.12F,
               0.0F);
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

auto build_player_world_avatar_parts(const PlayerController& player) -> std::vector<CreaturePartInstance> {
    std::vector<CreaturePartInstance> mesh {};
    mesh.reserve(16U);
    append_full_body(mesh, player, build_world_avatar_pose(player));
    return mesh;
}

auto build_player_viewmodel_parts(const PlayerController& player, BlockId held_item) -> PlayerViewModelParts {
    PlayerViewModelParts output {};
    output.parts.reserve(18U);
    append_viewmodel_arm(output, player, build_viewmodel_rig_state(player, held_item));
    append_viewmodel_held_item(output, held_item);
    return output;
}

auto build_player_world_avatar_mesh(const PlayerController& player) -> CreatureMeshData {
    const auto parts = build_player_world_avatar_parts(player);
    return build_creature_mesh(std::span<const CreaturePartInstance>(parts.data(), parts.size()));
}

auto build_player_viewmodel_mesh(const PlayerController& player, BlockId held_item) -> PlayerViewModelMesh {
    const auto parts = build_player_viewmodel_parts(player, held_item);
    PlayerViewModelMesh output {};
    output.mesh = build_creature_mesh(std::span<const CreaturePartInstance>(parts.parts.data(), parts.parts.size()));
    output.pose = parts.pose;
    return output;
}

auto build_player_mesh(const PlayerController& player, PlayerMeshView view) -> CreatureMeshData {
    if (view == PlayerMeshView::WorldAvatar) {
        return build_player_world_avatar_mesh(player);
    }
    return build_player_viewmodel_mesh(player).mesh;
}


} // namespace valcraft
