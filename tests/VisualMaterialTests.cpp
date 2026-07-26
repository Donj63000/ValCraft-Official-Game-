#include "render/VisualMaterials.h"
#include "render/ModernTerrainShaderSource.h"
#include "render/RendererQuality.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace valcraft {
namespace {

constexpr std::uint64_t kExpectedMaterialPackChecksum = 0x1E684B7F8A55B223ULL;

struct MaterialColorStatistics {
    std::array<double, 3> average {};
    double axis_ratio = 1.0;
};

[[nodiscard]] auto material_color_statistics(
    std::span<const std::uint8_t> albedo,
    std::size_t width,
    std::size_t height) -> MaterialColorStatistics {
    if (albedo.size() != width * height * 4U || width == 0U || height == 0U) {
        return {};
    }

    std::array<std::uint64_t, 3> channel_sums {};
    std::uint64_t horizontal_detail = 0U;
    std::uint64_t vertical_detail = 0U;
    for (std::size_t y = 0U; y < height; ++y) {
        for (std::size_t x = 0U; x < width; ++x) {
            const auto offset = (y * width + x) * 4U;
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
                channel_sums[channel] += albedo[offset + channel];
                if (x > 0U) {
                    horizontal_detail += static_cast<std::uint64_t>(std::abs(
                        static_cast<int>(albedo[offset + channel]) -
                        static_cast<int>(albedo[offset + channel - 4U])));
                }
                if (y > 0U) {
                    vertical_detail += static_cast<std::uint64_t>(std::abs(
                        static_cast<int>(albedo[offset + channel]) -
                        static_cast<int>(albedo[offset + channel - width * 4U])));
                }
            }
        }
    }

    const auto pixel_count = static_cast<double>(width * height);
    MaterialColorStatistics result {};
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
        result.average[channel] =
            static_cast<double>(channel_sums[channel]) / pixel_count;
    }
    const auto lesser_detail = std::max<std::uint64_t>(
        1U,
        std::min(horizontal_detail, vertical_detail));
    result.axis_ratio =
        static_cast<double>(std::max(horizontal_detail, vertical_detail)) /
        static_cast<double>(lesser_detail);
    return result;
}

[[nodiscard]] auto mean_neighbor_detail(
    std::span<const std::uint8_t> albedo,
    std::size_t width,
    std::size_t height) -> double {
    if (albedo.size() != width * height * 4U || width == 0U || height == 0U) {
        return 0.0;
    }

    std::uint64_t accumulated_difference = 0U;
    std::uint64_t comparison_count = 0U;
    for (std::size_t y = 0U; y < height; ++y) {
        for (std::size_t x = 0U; x < width; ++x) {
            const auto offset = (y * width + x) * 4U;
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
                if (x > 0U) {
                    accumulated_difference +=
                        static_cast<std::uint64_t>(std::abs(
                            static_cast<int>(albedo[offset + channel]) -
                            static_cast<int>(albedo[offset + channel - 4U])));
                    ++comparison_count;
                }
                if (y > 0U) {
                    accumulated_difference +=
                        static_cast<std::uint64_t>(std::abs(
                            static_cast<int>(albedo[offset + channel]) -
                            static_cast<int>(
                                albedo[offset + channel - width * 4U])));
                    ++comparison_count;
                }
            }
        }
    }
    return comparison_count == 0U
        ? 0.0
        : static_cast<double>(accumulated_difference) /
              static_cast<double>(comparison_count);
}

[[nodiscard]] auto find_material_pack() -> std::filesystem::path {
    std::array<std::filesystem::path, 2> roots {
        std::filesystem::absolute(std::filesystem::path {__FILE__}).parent_path(),
        std::filesystem::current_path(),
    };

    for (auto root : roots) {
        for (int depth = 0; depth < 8; ++depth) {
            const auto candidate =
                root / "assets" / "visual" / "valcraft_visual_materials.vmp";
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) {
                return candidate;
            }
            if (!root.has_parent_path() || root.parent_path() == root) {
                break;
            }
            root = root.parent_path();
        }
    }
    return {};
}

[[nodiscard]] auto read_bytes(const std::filesystem::path& path)
    -> std::vector<std::uint8_t> {
    std::error_code error;
    const auto file_size = std::filesystem::file_size(path, error);
    if (error || file_size > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)())) {
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            return {};
        }
    }
    return bytes;
}

void write_u64(std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::uint64_t value) {
    for (std::size_t byte_index = 0U; byte_index < 8U; ++byte_index) {
        bytes[offset + byte_index] =
            static_cast<std::uint8_t>((value >> static_cast<unsigned int>(byte_index * 8U)) & 0xFFU);
    }
}

void refresh_content_checksum(std::vector<std::uint8_t>& bytes) {
    REQUIRE(bytes.size() >= kVisualMaterialPackHeaderSize);
    const auto checksum = visual_material_pack_checksum(
        std::span<const std::uint8_t>(bytes).subspan(kVisualMaterialPackHeaderSize));
    write_u64(bytes, 40U, checksum);
}

[[nodiscard]] auto byte_checksum(std::span<const std::uint8_t> bytes) noexcept
    -> std::uint64_t {
    return visual_material_pack_checksum(bytes);
}

[[nodiscard]] auto count_occurrences(
    std::string_view source,
    std::string_view token) noexcept -> std::size_t {
    auto count = std::size_t {0U};
    auto position = std::size_t {0U};
    while ((position = source.find(token, position)) != std::string_view::npos) {
        ++count;
        position += token.size();
    }
    return count;
}

} // namespace

TEST_CASE("le catalogue visuel couvre tous les blocs sans modifier leurs identifiants") {
    CHECK(visual_material_for_block(to_block_id(BlockType::Air)) == VisualMaterialId::None);
    CHECK(visual_material_for_block(static_cast<BlockId>(255U)) == VisualMaterialId::None);

    for (BlockId block_id = to_block_id(BlockType::Grass);
         block_id <= to_block_id(BlockType::Shovel);
         ++block_id) {
        const auto material_id = visual_material_for_block(block_id);
        CAPTURE(static_cast<unsigned int>(block_id));
        CHECK(is_known_visual_material_id(material_id));
        CHECK(material_id != VisualMaterialId::None);
        CHECK(visual_surface_for_block(block_id) ==
              visual_material_definition(material_id).surface_class);
    }

    CHECK(visual_material_for_block(to_block_id(BlockType::TorchWallPositiveX)) ==
          VisualMaterialId::TorchFlame);
    CHECK(visual_material_for_block(to_block_id(BlockType::TorchWallNegativeX)) ==
          VisualMaterialId::TorchFlame);
    CHECK(visual_material_for_block(to_block_id(BlockType::TorchWallPositiveZ)) ==
          VisualMaterialId::TorchFlame);
    CHECK(visual_material_for_block(to_block_id(BlockType::TorchWallNegativeZ)) ==
          VisualMaterialId::TorchFlame);

    // Je vérifie les catégories qui pilotent les futurs chemins GPU.
    CHECK(visual_surface_for_block(to_block_id(BlockType::Grass)) ==
          VisualSurfaceClass::Organic);
    CHECK(visual_surface_for_block(to_block_id(BlockType::Planks)) ==
          VisualSurfaceClass::Architectural);
    CHECK(visual_surface_for_block(to_block_id(BlockType::Leaves)) ==
          VisualSurfaceClass::Cutout);
    CHECK(visual_surface_for_block(to_block_id(BlockType::Water)) ==
          VisualSurfaceClass::Liquid);
}

TEST_CASE("les définitions des matériaux gardent des couches et des noms uniques") {
    const auto definitions = visual_material_definitions();
    REQUIRE(definitions.size() == kVisualMaterialCount);
    CHECK(definitions.front().id == VisualMaterialId::None);
    CHECK(definitions.front().pack_layer == kInvalidVisualMaterialLayer);

    std::set<std::uint16_t> layers;
    std::set<std::uint32_t> name_hashes;
    for (std::size_t index = 1U; index < definitions.size(); ++index) {
        const auto& definition = definitions[index];
        CAPTURE(index);
        CAPTURE(definition.name);
        CHECK(static_cast<std::size_t>(definition.id) == index);
        CHECK(definition.pack_layer == index - 1U);
        CHECK_FALSE(definition.name.empty());
        CHECK(definition.texture_scale > 0.0F);
        CHECK(definition.normal_strength >= 0.0F);
        CHECK(definition.macro_variation >= 0.0F);
        CHECK(layers.insert(definition.pack_layer).second);
        CHECK(name_hashes.insert(visual_material_name_hash(definition.name)).second);
    }
}

TEST_CASE("le pack procédural versionné se charge avec tous ses mipmaps") {
    const auto pack_path = find_material_pack();
    REQUIRE_FALSE(pack_path.empty());

    const auto first_load = load_visual_material_pack(pack_path);
    const auto second_load = load_visual_material_pack(pack_path);
    REQUIRE_MESSAGE(first_load, first_load.message);
    REQUIRE_MESSAGE(second_load, second_load.message);
    REQUIRE(first_load.pack.has_value());
    REQUIRE(second_load.pack.has_value());

    const auto& pack = *first_load.pack;
    const auto& second_pack = *second_load.pack;
    CHECK(pack.format_version == kVisualMaterialPackVersion);
    CHECK(pack.width == 128U);
    CHECK(pack.height == 128U);
    CHECK(pack.mip_count == 8U);
    CHECK(pack.layers.size() == kVisualMaterialCount - 1U);
    CHECK(pack.content_checksum == kExpectedMaterialPackChecksum);
    CHECK(pack.content_checksum == second_pack.content_checksum);
    CHECK(pack.texels == second_pack.texels);

    std::set<std::uint64_t> albedo_checksums;
    for (std::size_t material_index = 1U;
         material_index < kVisualMaterialCount;
         ++material_index) {
        const auto material_id = static_cast<VisualMaterialId>(material_index);
        for (const auto texture : {
                 VisualMaterialTexture::Albedo,
                 VisualMaterialTexture::NormalHeight,
                 VisualMaterialTexture::OrmEmission,
             }) {
            auto expected_width = std::size_t {128U};
            auto expected_height = std::size_t {128U};
            for (std::uint16_t mip_level = 0U; mip_level < pack.mip_count; ++mip_level) {
                const auto texels = pack.texels_for(material_id, texture, mip_level);
                CAPTURE(material_index);
                CAPTURE(static_cast<unsigned int>(texture));
                CAPTURE(mip_level);
                CHECK(texels.size() == expected_width * expected_height * 4U);
                expected_width = std::max<std::size_t>(1U, expected_width / 2U);
                expected_height = std::max<std::size_t>(1U, expected_height / 2U);
            }
        }

        const auto albedo =
            pack.texels_for(material_id, VisualMaterialTexture::Albedo, 0U);
        CHECK(albedo_checksums.insert(byte_checksum(albedo)).second);

        const auto normal_height =
            pack.texels_for(material_id, VisualMaterialTexture::NormalHeight, 0U);
        REQUIRE(normal_height.size() == 128U * 128U * 4U);
        auto all_normals_face_outward = true;
        for (std::size_t offset = 0U; offset < normal_height.size(); offset += 4U) {
            all_normals_face_outward =
                all_normals_face_outward &&
                normal_height[offset + 2U] >= 128U;
        }
        CHECK(all_normals_face_outward);
    }

    CHECK(pack.texels_for(VisualMaterialId::None,
                          VisualMaterialTexture::Albedo,
                          0U).empty());
    CHECK(pack.texels_for(VisualMaterialId::MeadowGrass,
                          VisualMaterialTexture::Albedo,
                          pack.mip_count).empty());
    CHECK(pack.texels_for(
                  VisualMaterialId::MeadowGrass,
                  static_cast<VisualMaterialTexture>(255U),
                  0U).empty());
}

TEST_CASE("les matériaux découpés conservent une couverture alpha exploitable") {
    const auto pack_path = find_material_pack();
    REQUIRE_FALSE(pack_path.empty());
    const auto loaded = load_visual_material_pack(pack_path);
    REQUIRE_MESSAGE(loaded, loaded.message);
    REQUIRE(loaded.pack.has_value());

    for (const auto material_id : {
             VisualMaterialId::Broadleaf,
             VisualMaterialId::PineNeedles,
             VisualMaterialId::TallGrass,
             VisualMaterialId::CrimsonFlower,
             VisualMaterialId::GoldenFlower,
             VisualMaterialId::DeadShrub,
         }) {
        const auto albedo =
            loaded.pack->texels_for(material_id, VisualMaterialTexture::Albedo, 0U);
        auto transparent_pixels = std::size_t {0U};
        auto opaque_pixels = std::size_t {0U};
        for (std::size_t offset = 3U; offset < albedo.size(); offset += 4U) {
            transparent_pixels += albedo[offset] == 0U ? 1U : 0U;
            opaque_pixels += albedo[offset] == 255U ? 1U : 0U;
        }
        CAPTURE(static_cast<unsigned int>(material_id));
        CHECK(transparent_pixels > 0U);
        CHECK(opaque_pixels > 0U);

        const auto base_coverage =
            static_cast<double>(opaque_pixels) /
            static_cast<double>(albedo.size() / 4U);
        auto mip_width = std::size_t {128U};
        for (std::uint16_t mip_level = 1U;
             mip_level < loaded.pack->mip_count;
             ++mip_level) {
            mip_width = std::max<std::size_t>(1U, mip_width / 2U);
            const auto mip = loaded.pack->texels_for(
                material_id,
                VisualMaterialTexture::Albedo,
                mip_level);
            const auto mip_pixels = mip_width * mip_width;
            auto retained_pixels = std::size_t {0U};
            for (std::size_t offset = 3U; offset < mip.size(); offset += 4U) {
                retained_pixels += mip[offset] >= 118U ? 1U : 0U;
            }
            CAPTURE(mip_level);
            CHECK(retained_pixels > 0U);
            CHECK(std::abs(
                      static_cast<double>(retained_pixels) /
                          static_cast<double>(mip_pixels) -
                      base_coverage) <=
                  1.0 / static_cast<double>(mip_pixels) + 0.001);
        }
    }
}

TEST_CASE("la palette organique reste douce et sans direction artificielle") {
    const auto loaded = load_visual_material_pack(find_material_pack());
    REQUIRE_MESSAGE(loaded, loaded.message);
    REQUIRE(loaded.pack.has_value());

    for (const auto material_id : {
             VisualMaterialId::MeadowGrass,
             VisualMaterialId::Loam,
             VisualMaterialId::WarmStone,
             VisualMaterialId::SunlitSand,
             VisualMaterialId::RiverGravel,
             VisualMaterialId::MossyStone,
             VisualMaterialId::PowderSnow,
             VisualMaterialId::CoalOre,
             VisualMaterialId::IronOre,
             VisualMaterialId::GoldOre,
             VisualMaterialId::DiamondOre,
             VisualMaterialId::AlloyOre,
         }) {
        const auto albedo = loaded.pack->texels_for(
            material_id,
            VisualMaterialTexture::Albedo,
            0U);
        const auto statistics =
            material_color_statistics(albedo, 128U, 128U);
        CAPTURE(static_cast<unsigned int>(material_id));
        CHECK(statistics.axis_ratio < 1.20);

        const auto normal_height = loaded.pack->texels_for(
            material_id,
            VisualMaterialTexture::NormalHeight,
            0U);
        for (std::size_t offset = 2U;
             offset < normal_height.size();
             offset += 4U) {
            CHECK(normal_height[offset] >= 248U);
        }
    }

    const auto grass = material_color_statistics(
        loaded.pack->texels_for(
            VisualMaterialId::MeadowGrass,
            VisualMaterialTexture::Albedo,
            0U),
        128U,
        128U);
    CHECK(grass.average[0] >= 75.0);
    CHECK(grass.average[0] <= 100.0);
    CHECK(grass.average[1] >= 105.0);
    CHECK(grass.average[1] <= 130.0);
    CHECK(grass.average[2] >= 55.0);
    CHECK(grass.average[2] <= 85.0);
    CHECK(grass.average[1] - grass.average[0] <= 45.0);

    const auto loam = material_color_statistics(
        loaded.pack->texels_for(
            VisualMaterialId::Loam,
            VisualMaterialTexture::Albedo,
            0U),
        128U,
        128U);
    CHECK(loam.average[0] <= 120.0);
    CHECK(loam.average[0] - loam.average[2] <= 55.0);
}

TEST_CASE("les matériaux organiques gardent un microdétail filtrable sans bruit dur") {
    const auto loaded = load_visual_material_pack(find_material_pack());
    REQUIRE_MESSAGE(loaded, loaded.message);
    REQUIRE(loaded.pack.has_value());

    struct DetailExpectation {
        VisualMaterialId material = VisualMaterialId::None;
        double minimum = 0.0;
        double maximum = 0.0;
    };
    constexpr std::array expectations {
        DetailExpectation {VisualMaterialId::MeadowGrass, 1.8, 4.5},
        DetailExpectation {VisualMaterialId::Loam, 2.0, 4.8},
        DetailExpectation {VisualMaterialId::WarmStone, 1.8, 4.5},
        DetailExpectation {VisualMaterialId::SunlitSand, 1.2, 3.5},
        DetailExpectation {VisualMaterialId::RiverGravel, 2.0, 5.0},
        DetailExpectation {VisualMaterialId::MossyStone, 1.6, 4.5},
        DetailExpectation {VisualMaterialId::PowderSnow, 1.0, 3.0},
    };

    for (const auto& expectation : expectations) {
        const auto albedo = loaded.pack->texels_for(
            expectation.material,
            VisualMaterialTexture::Albedo,
            0U);
        const auto detail = mean_neighbor_detail(albedo, 128U, 128U);
        CAPTURE(static_cast<unsigned int>(expectation.material));
        CAPTURE(detail);
        // Je borne les deux extrêmes : une valeur trop faible redonne un
        // aplat flou, une valeur trop forte recrée du moiré en mouvement.
        CHECK(detail >= expectation.minimum);
        CHECK(detail <= expectation.maximum);
    }
}

TEST_CASE("le shader terrain borne et filtre le relief procédural") {
    const std::string_view source = kModernTerrainFragmentShaderSource;

    // Je verrouille ici les garde-fous qui empêchent les stries, le moiré et
    // les NaN noirs de réapparaître lors d'une évolution du PBR.
    CHECK(source.find("safe_normalize") != std::string_view::npos);
    CHECK(source.find("material_normal_strength") != std::string_view::npos);
    CHECK(source.find("fwidth(coordinate.zy)") != std::string_view::npos);
    CHECK(source.find("footprint_fade") != std::string_view::npos);
    CHECK(source.find("clamp(strength, 0.0, 0.14)") != std::string_view::npos);
    CHECK(source.find("normalize(view_direction + light_direction)") ==
          std::string_view::npos);
}

TEST_CASE("le shader terrain court-circuite les matériaux imperceptibles") {
    const std::string_view source = kModernTerrainFragmentShaderSource;

    // Je garde un epsilon nul en High pour préserver exactement ses poids,
    // puis je n'autorise le pincement quantifié que sur les qualités réduites.
    CHECK(source.find(
              "const float k_quantized_material_blend_epsilon = "
              "1.0 / 255.0;") != std::string_view::npos);
    CHECK(source.find("float blend_epsilon = full_material_detail") !=
          std::string_view::npos);
    CHECK(source.find("? 0.0") != std::string_view::npos);
    CHECK(source.find(": k_quantized_material_blend_epsilon;") !=
          std::string_view::npos);
    CHECK(source.find(
              "v_secondary_block == 0u || blend <= blend_epsilon") !=
          std::string_view::npos);
    CHECK(source.find(
              "!primary_material_only && blend >= 1.0 - blend_epsilon") !=
          std::string_view::npos);

    const auto primary_branch = source.find("if (primary_material_only)");
    const auto secondary_branch =
        source.find("else if (secondary_material_only)");
    const auto mixed_branch = source.find(
        "primary_sample = sample_material_surface(",
        secondary_branch);
    REQUIRE(primary_branch != std::string_view::npos);
    REQUIRE(secondary_branch != std::string_view::npos);
    REQUIRE(mixed_branch != std::string_view::npos);
    CHECK(primary_branch < secondary_branch);
    CHECK(secondary_branch < mixed_branch);
    CHECK(source.find("secondary_sample = primary_sample;", primary_branch) <
          secondary_branch);
    CHECK(source.find("primary_sample = secondary_sample;", secondary_branch) <
          mixed_branch);
    CHECK(source.find("blend = 0.0;", primary_branch) < secondary_branch);
    CHECK(source.find("blend = 1.0;", secondary_branch) < mixed_branch);

    // Je centralise les deux textures d'une matière dans un seul helper : un
    // chemin extrême l'appelle une fois, le chemin mélangé exactement deux fois.
    CHECK(count_occurrences(source, "sample_material_surface(") == 5U);
    CHECK(count_occurrences(source, "sample_triplanar(") == 3U);
}

TEST_CASE("le shader terrain réduit les normales et le PCF selon la qualité") {
    const std::string_view source = kModernTerrainFragmentShaderSource;
    const auto high = resolve_renderer_quality_settings(
        RendererQuality::High, 1920, 1080);
    const auto medium = resolve_renderer_quality_settings(
        RendererQuality::Medium, 1920, 1080);
    const auto low = resolve_renderer_quality_settings(
        RendererQuality::Low, 1920, 1080);

    // Je vérifie que les seuils du shader séparent bien les trois réglages déjà
    // fournis par le renderer, sans introduire un nouvel uniform de qualité.
    CHECK(high.material_detail_scale >= 0.999F);
    CHECK(medium.material_detail_scale >= 0.50F);
    CHECK(medium.material_detail_scale < 0.999F);
    CHECK(low.material_detail_scale < 0.50F);
    CHECK(source.find(
              "if (u_material_detail_scale >= "
              "k_normal_mapping_detail_threshold)") !=
          std::string_view::npos);
    CHECK(source.find(
              "if (u_material_detail_scale < "
              "k_normal_mapping_detail_threshold)") !=
          std::string_view::npos);
    CHECK(source.find(
              "if (u_material_detail_scale < "
              "k_full_material_detail_threshold)") !=
          std::string_view::npos);

    // Je verrouille les budgets : Low effectue un tap central, Medium quatre
    // taps, et High conserve strictement la boucle PCF 3x3 historique.
    CHECK(source.find(
              "far_cascade, vec2(0.0));") != std::string_view::npos);
    CHECK(count_occurrences(source, "reduced_visibility +=") == 4U);
    CHECK(source.find("return reduced_visibility * 0.25;") !=
          std::string_view::npos);
    CHECK(source.find("for (int y = -1; y <= 1; ++y)") !=
          std::string_view::npos);
    CHECK(source.find("for (int x = -1; x <= 1; ++x)") !=
          std::string_view::npos);
    CHECK(source.find("return visibility / 9.0;") !=
          std::string_view::npos);
}

TEST_CASE("le shader terrain fond la geologie sans ajouter de couche triplanaire") {
    const std::string_view source = kModernTerrainFragmentShaderSource;

    // Je verrouille le chemin à deux matériaux : la continuité géologique ne
    // doit jamais tripler le coût des échantillons albédo, ORM et normales.
    CHECK(source.find("(v_surface_flags & 16u)") !=
          std::string_view::npos);
    CHECK(source.find("value_noise_2d") != std::string_view::npos);
    CHECK(source.find("hash31(floor") == std::string_view::npos);
    CHECK(source.find("environmental_rock") != std::string_view::npos);
    CHECK(source.find("loam_band") != std::string_view::npos);
    CHECK(source.find("v_primary_block == 1u") !=
          std::string_view::npos);

    CHECK(count_occurrences(source, "sample_triplanar(") == 3U);
    CHECK(count_occurrences(source, "sample_material_surface(") == 5U);
}

TEST_CASE("le shader terrain conserve les vegetaux et les creux lisibles") {
    const std::string_view source = kModernTerrainFragmentShaderSource;

    CHECK(source.find("occlusion_floor = 0.52") !=
          std::string_view::npos);
    CHECK(source.find("wrapped_ndotl") != std::string_view::npos);
    CHECK(source.find("transmission_strength") !=
          std::string_view::npos);
    CHECK(source.find("vec3 bounce =") != std::string_view::npos);
    CHECK(source.find("readability_floor") !=
          std::string_view::npos);
    CHECK(source.find("mix(0.38, 1.0, visibility)") !=
          std::string_view::npos);
}

TEST_CASE("le parseur refuse les packs tronqués, altérés ou incohérents") {
    const auto original = read_bytes(find_material_pack());
    REQUIRE(original.size() > kVisualMaterialPackHeaderSize + 16U);

    auto truncated_header = original;
    truncated_header.resize(kVisualMaterialPackHeaderSize - 1U);
    CHECK(parse_visual_material_pack(truncated_header).error ==
          VisualMaterialPackError::Truncated);

    auto invalid_magic = original;
    invalid_magic[0U] ^= 0x7FU;
    CHECK(parse_visual_material_pack(invalid_magic).error ==
          VisualMaterialPackError::InvalidMagic);

    auto unsupported_version = original;
    unsupported_version[8U] = 0xFFU;
    unsupported_version[9U] = 0x7FU;
    CHECK(parse_visual_material_pack(unsupported_version).error ==
          VisualMaterialPackError::UnsupportedVersion);

    auto invalid_dimensions = original;
    invalid_dimensions[12U] = 127U;
    invalid_dimensions[13U] = 0U;
    CHECK(parse_visual_material_pack(invalid_dimensions).error ==
          VisualMaterialPackError::InvalidDimensions);

    auto invalid_mips = original;
    invalid_mips[18U] = 1U;
    invalid_mips[19U] = 0U;
    CHECK(parse_visual_material_pack(invalid_mips).error ==
          VisualMaterialPackError::InvalidMipChain);

    auto invalid_size = original;
    invalid_size.push_back(0U);
    CHECK(parse_visual_material_pack(invalid_size).error ==
          VisualMaterialPackError::SizeMismatch);

    auto corrupted_payload = original;
    corrupted_payload.back() ^= 0x01U;
    CHECK(parse_visual_material_pack(corrupted_payload).error ==
          VisualMaterialPackError::ChecksumMismatch);

    auto corrupted_layer = original;
    corrupted_layer[kVisualMaterialPackHeaderSize] = 2U;
    corrupted_layer[kVisualMaterialPackHeaderSize + 1U] = 0U;
    refresh_content_checksum(corrupted_layer);
    CHECK(parse_visual_material_pack(corrupted_layer).error ==
          VisualMaterialPackError::InvalidLayerTable);

    const auto missing = load_visual_material_pack(
        find_material_pack().parent_path() / "pack-absent.vmp");
    CHECK(missing.error == VisualMaterialPackError::IoFailure);
    CHECK_FALSE(missing.pack.has_value());
    CHECK_FALSE(missing.message.empty());
}

} // namespace valcraft
