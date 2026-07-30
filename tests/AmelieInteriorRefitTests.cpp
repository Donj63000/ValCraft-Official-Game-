#include "gameplay/SeaAdventure.h"
#include "render/ShipMesh.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>

namespace valcraft {

namespace {

[[nodiscard]] auto part_minimum(
    const ShipPart& part) noexcept -> glm::vec3 {

    return glm::min(
        part.local_start,
        part.local_end);
}

[[nodiscard]] auto part_maximum(
    const ShipPart& part) noexcept -> glm::vec3 {

    return glm::max(
        part.local_start,
        part.local_end);
}

[[nodiscard]] auto near_value(
    float value,
    float expected,
    float epsilon = 1.0e-3F) noexcept -> bool {

    return std::abs(value - expected) <= epsilon;
}

[[nodiscard]] auto is_soft_decor(
    const ShipPart& part) noexcept -> bool {

    return part.shape == ShipPartShape::ChamferedBox ||
           part.shape == ShipPartShape::DrapedPanel ||
           part.material == ShipMaterial::Linen ||
           part.material == ShipMaterial::BurgundyTextile ||
           part.material == ShipMaterial::NavyTextile ||
           part.material == ShipMaterial::Paper ||
           part.material == ShipMaterial::Ceramic;
}

[[nodiscard]] auto interior_half_width_for_test(
    const ShipProtectionProfile& profile,
    float local_y,
    float local_z) noexcept -> float {

    auto half_width =
        profile.half_width_at(
            local_z);
    if (local_y <
        profile.middle_hull_min_y) {
        half_width =
            std::max(
                profile.lower_minimum_half_width,
                half_width -
                    profile.lower_width_inset);
    } else if (
        local_y <
        profile.upper_hull_min_y) {
        half_width =
            std::max(
                profile.middle_minimum_half_width,
                half_width -
                    profile.middle_width_inset);
    }
    const auto wall_thickness =
        std::min(
            0.44F,
            std::max(
                0.22F,
                half_width *
                    0.36F));
    return std::max(
        0.48F,
        half_width -
            wall_thickness);
}

[[nodiscard]] auto visual_bounds(
    const ShipPart& part) noexcept
    -> std::pair<glm::vec3, glm::vec3> {

    auto minimum =
        part_minimum(part);
    auto maximum =
        part_maximum(part);
    auto expansion =
        glm::vec3 {0.0F};
    if (part.shape ==
        ShipPartShape::Segment) {
        expansion =
            glm::vec3 {
                std::max(
                    part.thickness,
                    0.0F) *
                0.5F,
            };
    } else if (
        part.shape ==
            ShipPartShape::Panel ||
        part.shape ==
            ShipPartShape::DrapedPanel) {
        const auto length =
            glm::length(
                part.orientation);
        if (length >
            1.0e-5F) {
            expansion =
                glm::abs(
                    part.orientation /
                    length) *
                (std::max(
                     part.thickness,
                     0.0F) *
                 0.5F);
        }
    }
    return {
        minimum - expansion,
        maximum + expansion,
    };
}

[[nodiscard]] auto legacy_visual_checksum(
    std::span<const ShipPart> parts) noexcept
    -> std::uint64_t {

    constexpr auto offset =
        std::uint64_t {14695981039346656037ULL};
    constexpr auto prime =
        std::uint64_t {1099511628211ULL};
    auto hash = offset;
    const auto hash_byte =
        [&](std::uint8_t value) {
            hash ^= value;
            hash *= prime;
        };
    const auto hash_u32 =
        [&](std::uint32_t value) {
            for (int shift = 0;
                 shift < 32;
                 shift += 8) {
                hash_byte(
                    static_cast<std::uint8_t>(
                        (
                            value >>
                            static_cast<unsigned>(
                                shift)
                        ) &
                        0xFFU));
            }
        };
    const auto hash_vec3 =
        [&](const glm::vec3& value) {
            hash_u32(
                std::bit_cast<std::uint32_t>(
                    value.x));
            hash_u32(
                std::bit_cast<std::uint32_t>(
                    value.y));
            hash_u32(
                std::bit_cast<std::uint32_t>(
                    value.z));
        };
    for (const auto& part : parts) {
        hash_byte(
            static_cast<std::uint8_t>(
                part.shape));
        hash_byte(
            static_cast<std::uint8_t>(
                part.material));
        hash_vec3(part.local_start);
        hash_vec3(part.local_end);
        hash_vec3(part.orientation);
        hash_u32(
            std::bit_cast<std::uint32_t>(
                part.thickness));
        hash_byte(
            part.collidable
                ? 1U
                : 0U);
        hash_byte(
            part.supports_player
                ? 1U
                : 0U);
        hash_u32(
            static_cast<std::uint32_t>(
                part.glyph));
    }
    return hash;
}

[[nodiscard]] auto legacy_mesh_checksum(
    const ChunkMeshData& mesh) noexcept
    -> std::uint64_t {

    constexpr auto offset =
        std::uint64_t {14695981039346656037ULL};
    constexpr auto prime =
        std::uint64_t {1099511628211ULL};
    auto hash = offset;
    const auto hash_byte =
        [&](std::uint8_t value) {
            hash ^= value;
            hash *= prime;
        };
    const auto hash_u32 =
        [&](std::uint32_t value) {
            for (int shift = 0;
                 shift < 32;
                 shift += 8) {
                hash_byte(
                    static_cast<std::uint8_t>(
                        (
                            value >>
                            static_cast<unsigned>(
                                shift)
                        ) &
                        0xFFU));
            }
        };
    const auto hash_float =
        [&](float value) {
            hash_u32(
                std::bit_cast<std::uint32_t>(
                    value));
        };
    for (const auto& vertex :
         mesh.vertices) {
        hash_float(vertex.x);
        hash_float(vertex.y);
        hash_float(vertex.z);
        hash_float(vertex.u);
        hash_float(vertex.v);
        hash_float(vertex.nx);
        hash_float(vertex.ny);
        hash_float(vertex.nz);
        hash_float(vertex.face_shade);
        hash_float(vertex.ao);
        hash_float(vertex.sky_light);
        hash_float(vertex.block_light);
        hash_float(vertex.material_class);
        hash_float(vertex.wave_weight);
    }
    for (const auto index :
         mesh.indices) {
        hash_u32(index);
    }
    hash_u32(
        static_cast<std::uint32_t>(
            mesh.face_count));
    return hash;
}

} // namespace

TEST_CASE("les identifiants de la refonte intérieure restent append-only") {
    CHECK(
        static_cast<std::uint8_t>(
            ShipMaterial::BlackCanvas) ==
        9U);
    CHECK(
        static_cast<std::uint8_t>(
            ShipMaterial::SolidGold) ==
        10U);
    CHECK(
        static_cast<std::uint8_t>(
            ShipMaterial::OiledOak) ==
        11U);
    CHECK(
        static_cast<std::uint8_t>(
            ShipMaterial::Ceramic) ==
        17U);

    CHECK(
        static_cast<std::uint8_t>(
            ShipPartShape::Opening) ==
        8U);
    CHECK(
        static_cast<std::uint8_t>(
            ShipPartShape::ChamferedBox) ==
        9U);
    CHECK(
        static_cast<std::uint8_t>(
            ShipPartShape::DrapedPanel) ==
        10U);
}

TEST_CASE("le décor Legacy reste la photographie historique antérieure à la refonte") {
    const auto legacy_parts =
        amelie_ship_blueprint()
            .legacy_visual_parts;

    REQUIRE(legacy_parts.size() == 1'104U);
    CHECK(
        std::ranges::all_of(
            legacy_parts,
            [](const ShipPart& part) {
                return
                    part.shape <=
                        ShipPartShape::ClimbableNet &&
                    part.material <=
                        ShipMaterial::SolidGold;
            }));
    CHECK(
        legacy_visual_checksum(
            legacy_parts) ==
        4'299'205'597'173'179'901ULL);

    const auto legacy_mesh =
        build_ship_mesh_data(
            legacy_parts,
            {},
            ShipMeshLightingModel::
                LegacyHistorical);
    REQUIRE_FALSE(
        legacy_mesh.empty());
    CHECK(
        legacy_mesh.vertices.size() ==
        56'976U);
    CHECK(
        legacy_mesh.indices.size() ==
        85'464U);
    CHECK(
        legacy_mesh.face_count ==
        14'244U);
    CHECK(
        legacy_mesh_checksum(
            legacy_mesh) ==
        7'175'173'902'441'106'935ULL);
}

TEST_CASE("la cabine et l'infirmerie utilisent des lits à échelle humaine") {
    const auto parts =
        amelie_ship_blueprint().parts;

    const auto captain_core =
        std::ranges::find_if(
            parts,
            [](const ShipPart& part) {
                const auto minimum =
                    part_minimum(part);
                const auto maximum =
                    part_maximum(part);
                return
                    part.shape ==
                        ShipPartShape::Box &&
                    part.material ==
                        ShipMaterial::OiledOak &&
                    part.collidable &&
                    near_value(minimum.x, -5.45F) &&
                    near_value(maximum.x, -3.75F) &&
                    near_value(minimum.z, -32.20F) &&
                    near_value(maximum.z, -29.30F);
            });
    REQUIRE(captain_core != parts.end());
    CHECK(
        part_maximum(*captain_core).x -
            part_minimum(*captain_core).x ==
        doctest::Approx(1.70F));
    CHECK(
        part_maximum(*captain_core).z -
            part_minimum(*captain_core).z ==
        doctest::Approx(2.90F));

    auto infirmary_cores = std::size_t {0U};
    for (const auto& part : parts) {
        const auto minimum =
            part_minimum(part);
        const auto maximum =
            part_maximum(part);
        if (part.shape != ShipPartShape::Box ||
            part.material != ShipMaterial::OiledOak ||
            !part.collidable ||
            !near_value(minimum.y, -1.84F) ||
            !near_value(maximum.y, -1.66F) ||
            !near_value(minimum.z, -27.30F) ||
            !near_value(maximum.z, -25.00F)) {
            continue;
        }
        ++infirmary_cores;
        CHECK(
            maximum.x - minimum.x ==
            doctest::Approx(1.25F));
        CHECK(
            maximum.z - minimum.z ==
            doctest::Approx(2.30F));
    }
    CHECK(infirmary_cores == 2U);
}

TEST_CASE("les douze couchettes restent avant la cloison de la distillerie") {
    const auto parts =
        amelie_ship_blueprint().parts;
    auto bunk_cores = std::size_t {0U};
    auto bunk_blankets = std::size_t {0U};

    for (const auto& part : parts) {
        const auto minimum =
            part_minimum(part);
        const auto maximum =
            part_maximum(part);
        if (part.shape == ShipPartShape::Box &&
            part.material == ShipMaterial::OiledOak &&
            part.collidable &&
            near_value(minimum.y, -1.94F) &&
            near_value(maximum.y, -1.72F) &&
            near_value(
                maximum.z - minimum.z,
                2.25F)) {

            ++bunk_cores;
            CHECK(minimum.z >= -18.55F);
            CHECK(maximum.z <= -10.45F);
            CHECK(
                maximum.x - minimum.x ==
                doctest::Approx(1.35F));
            if (maximum.x < 0.0F) {
                CHECK(maximum.x <= -1.10F);
            } else {
                CHECK(minimum.x >= 1.10F);
            }
        }

        const auto bunk_textile =
            part.material ==
                ShipMaterial::NavyTextile ||
            part.material ==
                ShipMaterial::BurgundyTextile;
        if (part.shape ==
                ShipPartShape::DrapedPanel &&
            bunk_textile &&
            minimum.z >= -18.55F &&
            maximum.z <= -10.45F) {

            ++bunk_blankets;
            CHECK_FALSE(part.collidable);
            CHECK_FALSE(part.supports_player);
        }
    }

    CHECK(bunk_cores == 6U);
    CHECK(bunk_blankets == 12U);
}

TEST_CASE("les détails souples ne créent aucune micro-collision sur le navire mobile") {
    const auto parts =
        amelie_ship_blueprint().parts;
    auto soft_part_count =
        std::size_t {0U};
    for (const auto& part : parts) {
        if (!is_soft_decor(part)) {
            continue;
        }
        ++soft_part_count;
        CHECK_FALSE(part.collidable);
        CHECK_FALSE(part.supports_player);
    }
    CHECK(soft_part_count >= 120U);
}

TEST_CASE("la palette intérieure moderne est complète et reste dans la carène habitée") {
    const auto& blueprint =
        amelie_ship_blueprint();
    const std::array required_materials {
        ShipMaterial::OiledOak,
        ShipMaterial::Linen,
        ShipMaterial::BurgundyTextile,
        ShipMaterial::NavyTextile,
        ShipMaterial::Leather,
        ShipMaterial::Paper,
        ShipMaterial::Ceramic,
    };
    for (const auto material :
         required_materials) {
        CHECK(
            std::ranges::any_of(
                blueprint.parts,
                [&](const ShipPart& part) {
                    return
                        part.material ==
                        material;
                }));
    }

    for (const auto& part :
         blueprint.parts) {
        const auto bounds =
            visual_bounds(part);
        const auto minimum =
            bounds.first;
        const auto maximum =
            bounds.second;
        const auto center =
            (minimum +
             maximum) *
            0.5F;
        const auto extent =
            maximum -
            minimum;
        const auto structural_oiled_oak =
            part.material ==
                ShipMaterial::OiledOak &&
            (
                part.shape ==
                    ShipPartShape::Segment ||
                (
                    extent.x >= 4.0F &&
                    extent.z <= 0.50F
                ) ||
                (
                    extent.x >= 4.0F &&
                    extent.z >= 4.0F &&
                    extent.y <= 0.35F
                ) ||
                (
                    extent.z >= 8.0F &&
                    extent.x <= 0.80F
                )
            );
        const auto enclosed_oiled_oak_detail =
            part.material ==
                ShipMaterial::OiledOak &&
            !structural_oiled_oak &&
            center.y < 3.65F &&
            center.z >= -33.0F &&
            center.z <= 31.0F;
        // Je contrôle les formes modernes et le mobilier en chêne huilé. Je
        // n'écarte que les membrures, ponts et cloisons dont les proportions
        // structurelles sont clairement reconnaissables.
        const auto modern_detail =
            static_cast<std::uint8_t>(
                part.material) >
                static_cast<std::uint8_t>(
                    ShipMaterial::OiledOak) ||
            enclosed_oiled_oak_detail ||
            part.shape ==
                ShipPartShape::ChamferedBox ||
            part.shape ==
                ShipPartShape::DrapedPanel;
        const auto voilerie_detail =
            center.y < 0.90F &&
            center.z >= 16.0F &&
            center.z <= 25.35F &&
            (part.material ==
                 ShipMaterial::Rope ||
             part.material ==
                 ShipMaterial::Iron ||
             (part.material ==
                  ShipMaterial::OiledOak &&
              part.shape ==
                  ShipPartShape::Box &&
              center.x > 2.50F &&
              center.z <= 22.30F) ||
             part.shape ==
                 ShipPartShape::Wheel);
        const auto powder_locker_detail =
            center.y >= 0.90F &&
            center.y <= 2.75F &&
            center.z >= 29.20F &&
            center.z <= 31.20F &&
            std::abs(center.x) >= 1.30F &&
            ((part.material ==
                  ShipMaterial::OiledOak &&
              part.shape ==
                  ShipPartShape::Box) ||
             (part.material ==
                  ShipMaterial::Iron &&
              part.shape ==
                  ShipPartShape::Panel) ||
             (part.material ==
                  ShipMaterial::Brass &&
              part.shape ==
                  ShipPartShape::ChamferedBox));
        if (!modern_detail &&
            !voilerie_detail &&
            !powder_locker_detail) {
            continue;
        }
        if (center.y >= 3.65F ||
            center.z < -33.0F ||
            center.z > 31.0F) {
            continue;
        }
        for (const auto local_y :
             {
                 minimum.y,
                 maximum.y,
             }) {
            for (const auto local_z :
                 {
                     minimum.z,
                     maximum.z,
                 }) {
                const auto safe_half_width =
                    interior_half_width_for_test(
                        blueprint.protection_profile,
                        local_y,
                        local_z);
                CAPTURE(
                    static_cast<int>(
                        part.material));
                CAPTURE(
                    static_cast<int>(
                        part.shape));
                CAPTURE(center.x);
                CAPTURE(local_y);
                CAPTURE(local_z);
                CAPTURE(safe_half_width);
                CHECK(
                    std::max(
                        std::abs(
                            minimum.x),
                        std::abs(
                            maximum.x)) <=
                    safe_half_width +
                        0.015F);
            }
        }
    }
}

} // namespace valcraft
