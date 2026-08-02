#include "render/ModelIconAtlas.h"
#include "render/MusketVisualRecipe.h"
#include "render/VisualItemModel.h"
#include "render/VisualMaterials.h"

#include <doctest/doctest.h>
#include <glm/common.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace valcraft {
namespace {

constexpr std::uint64_t kExpectedMaterialChecksum =
    0x8D841BFD53A89EC7ULL;
constexpr std::uint64_t kExpectedIconContentChecksum =
    0x74D3CC0F846DA790ULL;

[[nodiscard]] auto find_project_asset(
    const std::filesystem::path& relative_path)
    -> std::filesystem::path {
    std::array<std::filesystem::path, 2> roots {
        std::filesystem::absolute(
            std::filesystem::path {__FILE__})
            .parent_path(),
        std::filesystem::current_path(),
    };
    for (auto root : roots) {
        for (int depth = 0; depth < 8; ++depth) {
            const auto candidate = root / relative_path;
            std::error_code error;
            if (std::filesystem::is_regular_file(
                    candidate,
                    error)) {
                return candidate;
            }
            if (!root.has_parent_path() ||
                root.parent_path() == root) {
                break;
            }
            root = root.parent_path();
        }
    }
    return {};
}

[[nodiscard]] auto material_pack_path()
    -> std::filesystem::path {
    return find_project_asset(
        "assets/visual/valcraft_visual_materials.vmp");
}

[[nodiscard]] auto icon_atlas_path()
    -> std::filesystem::path {
    return find_project_asset(
        "assets/visual/valcraft_model_icons.vmia");
}

[[nodiscard]] auto read_bytes(
    const std::filesystem::path& path)
    -> std::vector<std::uint8_t> {
    std::error_code error;
    const auto file_size =
        std::filesystem::file_size(path, error);
    if (error ||
        file_size >
            static_cast<std::uintmax_t>(
                (std::numeric_limits<std::size_t>::max)())) {
        return {};
    }
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(file_size));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!input ||
            input.gcount() !=
                static_cast<std::streamsize>(bytes.size())) {
            return {};
        }
    }
    return bytes;
}

void write_u64(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint64_t value) {
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
        bytes[offset + byte] =
            static_cast<std::uint8_t>(
                value >>
                static_cast<unsigned int>(byte * 8U));
    }
}

void refresh_global_checksum(
    std::vector<std::uint8_t>& bytes) {
    REQUIRE(bytes.size() >= kModelIconAtlasHeaderSize);
    write_u64(
        bytes,
        40U,
        model_icon_atlas_checksum(
            std::span<const std::uint8_t>(bytes)
                .subspan(kModelIconAtlasHeaderSize)));
}

[[nodiscard]] auto finite_matrix(
    const glm::mat4& matrix) noexcept -> bool {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE(
    "les recettes partagees couvrent tous les objets affichables sans toucher aux BlockId") {
    CHECK(kVisualItemModelCount == 36U);
    CHECK_FALSE(is_visual_item_displayable(
        to_block_id(BlockType::Air)));
    CHECK_FALSE(is_visual_item_displayable(
        static_cast<BlockId>(255U)));
    CHECK(visual_item_layer_index(
              to_block_id(BlockType::Air)) ==
          kVisualItemModelCount);
    CHECK(visual_item_layer_index(
              static_cast<BlockId>(255U)) ==
          kVisualItemModelCount);

    std::set<BlockId> canonical_ids;
    std::set<std::uint64_t> geometry_checksums;
    std::set<StylizedPrimitiveType> primitive_types;
    for (const auto item_id : kVisualItemCanonicalIds) {
        CAPTURE(static_cast<unsigned int>(item_id));
        CHECK(canonical_ids.insert(item_id).second);
        CHECK(is_visual_item_displayable(item_id));
        const auto model = build_visual_item_model(item_id);
        REQUIRE_FALSE(model.empty());
        CHECK(model.item_id == item_id);
        CHECK(
            model.model_class ==
            visual_item_model_class_for(item_id));
        CHECK(
            model.geometry_checksum ==
            visual_item_model_fingerprint(model));
        CHECK(model.geometry_checksum != 0U);
        CHECK(
            geometry_checksums
                .insert(model.geometry_checksum)
                .second);
        CHECK(model.bounds.min.x < model.bounds.max.x);
        CHECK(model.bounds.min.y < model.bounds.max.y);
        CHECK(model.bounds.min.z < model.bounds.max.z);

        for (const auto& primitive : model.primitives) {
            CHECK(finite_matrix(primitive.transform));
            CHECK(
                primitive.material !=
                VisualMaterialId::None);
            CHECK(is_known_visual_material_id(
                primitive.material));
            primitive_types.insert(primitive.primitive);
        }
    }
    CHECK(canonical_ids.size() == kVisualItemModelCount);
    CHECK(
        geometry_checksums.size() ==
        kVisualItemModelCount);
    CHECK(primitive_types.size() == 6U);

    // Je confirme que les orientations logiques d'une torche partagent
    // exactement la recette visuelle de l'objet inventaire.
    const auto torch_id = to_block_id(BlockType::Torch);
    const auto torch_model =
        build_visual_item_model(torch_id);
    for (const auto wall_torch : {
             BlockType::TorchWallPositiveX,
             BlockType::TorchWallNegativeX,
             BlockType::TorchWallPositiveZ,
             BlockType::TorchWallNegativeZ,
         }) {
        const auto wall_id = to_block_id(wall_torch);
        CHECK(canonical_visual_item_id(wall_id) == torch_id);
        CHECK(
            visual_item_layer_index(wall_id) ==
            visual_item_layer_index(torch_id));
        const auto wall_model =
            build_visual_item_model(wall_id);
        CHECK(
            wall_model.geometry_checksum ==
            torch_model.geometry_checksum);
    }
}

TEST_CASE("la recette canonique du fusil contient toutes ses pieces historiques") {
    std::array<std::size_t, 5> material_counts {};
    std::array<std::size_t, 8> part_counts {};
    for (const auto& part : musket_visual_parts()) {
        const auto material =
            static_cast<std::size_t>(part.material);
        const auto kind =
            static_cast<std::size_t>(part.kind);
        REQUIRE(material < material_counts.size());
        REQUIRE(kind < part_counts.size());
        ++material_counts[material];
        ++part_counts[kind];
        CHECK(std::isfinite(part.center.x));
        CHECK(std::isfinite(part.center.y));
        CHECK(std::isfinite(part.center.z));
        CHECK(std::isfinite(part.rotation_radians.x));
        CHECK(std::isfinite(part.rotation_radians.y));
        CHECK(std::isfinite(part.rotation_radians.z));
        CHECK(part.half_extent.x > 0.0F);
        CHECK(part.half_extent.y > 0.0F);
        CHECK(part.half_extent.z > 0.0F);
    }

    CHECK(musket_visual_parts().size() == 30U);
    CHECK(
        material_counts[
            static_cast<std::size_t>(
                MusketVisualMaterial::Walnut)] >=
        8U);
    CHECK(
        material_counts[
            static_cast<std::size_t>(
                MusketVisualMaterial::PatinatedSteel)] >=
        10U);
    CHECK(
        material_counts[
            static_cast<std::size_t>(
                MusketVisualMaterial::Brass)] >=
        3U);
    CHECK(
        material_counts[
            static_cast<std::size_t>(
                MusketVisualMaterial::Flint)] ==
        1U);
    CHECK(
        material_counts[
            static_cast<std::size_t>(
                MusketVisualMaterial::DarkBore)] ==
        1U);
    CHECK(
        part_counts[
            static_cast<std::size_t>(
                MusketVisualPartKind::Band)] ==
        3U);
    CHECK(
        part_counts[
            static_cast<std::size_t>(
                MusketVisualPartKind::Muzzle)] ==
        1U);

    CHECK(
        kMusketVisualSockets.front_sight.y ==
        doctest::Approx(
            kMusketVisualSockets.rear_sight.y));
    CHECK(
        kMusketVisualSockets.front_sight.z ==
        doctest::Approx(
            kMusketVisualSockets.rear_sight.z));
    CHECK(
        kMusketVisualSockets.front_sight.x >
        kMusketVisualSockets.rear_sight.x);
    CHECK(
        kMusketVisualSockets.muzzle.x >
        kMusketVisualSockets.front_sight.x);
    CHECK(
        kMusketVisualSockets.bayonet_tip.x >
        kMusketVisualSockets.muzzle.x);

    const auto first =
        build_visual_item_model(
            to_block_id(BlockType::Musket));
    const auto second =
        build_visual_item_model(
            to_block_id(BlockType::Musket));
    REQUIRE_FALSE(first.empty());
    CHECK(
        first.model_class ==
        VisualItemModelClass::Weapon);
    CHECK(
        first.primitives.size() ==
        musket_visual_parts().size());
    CHECK(
        first.geometry_checksum ==
        second.geometry_checksum);
    CHECK(
        first.geometry_checksum ==
        visual_item_model_fingerprint(first));
}

TEST_CASE(
    "l'atlas 128 px charge toutes les couches et leurs mipmaps alpha") {
    const auto loaded =
        load_model_icon_atlas(icon_atlas_path());
    REQUIRE_MESSAGE(loaded, loaded.message);
    REQUIRE(loaded.atlas.has_value());
    const auto& atlas = *loaded.atlas;

    CHECK(
        atlas.metadata.version ==
        kModelIconAtlasVersion);
    CHECK(atlas.metadata.width == kModelIconSize);
    CHECK(atlas.metadata.height == kModelIconSize);
    CHECK(
        atlas.metadata.mip_count ==
        kModelIconMipCount);
    CHECK(
        atlas.metadata.recipe_version ==
        kModelIconRecipeVersion);
    CHECK(
        atlas.metadata.source_material_checksum ==
        kExpectedMaterialChecksum);
    CHECK(
        atlas.metadata.content_checksum ==
        kExpectedIconContentChecksum);
    REQUIRE(
        atlas.layers.size() ==
        kVisualItemModelCount);
    REQUIRE(
        atlas.mip_levels.size() ==
        kModelIconMipCount);
    CHECK(atlas.mip_levels.front().width == 128U);
    CHECK(atlas.mip_levels.front().height == 128U);
    CHECK(atlas.mip_levels.back().width == 1U);
    CHECK(atlas.mip_levels.back().height == 1U);

    std::set<std::uint64_t> texel_checksums;
    for (std::size_t layer_index = 0U;
         layer_index < atlas.layers.size();
         ++layer_index) {
        const auto& layer = atlas.layers[layer_index];
        CAPTURE(layer_index);
        CAPTURE(static_cast<unsigned int>(layer.item_id));
        CHECK(
            layer.item_id ==
            kVisualItemCanonicalIds[layer_index]);
        CHECK(layer.primitive_count > 0U);
        CHECK(layer.geometry_checksum != 0U);
        CHECK(layer.texel_checksum != 0U);
        CHECK(
            texel_checksums
                .insert(layer.texel_checksum)
                .second);

        auto expected_dimension =
            std::size_t {kModelIconSize};
        for (std::uint16_t mip_level = 0U;
             mip_level < kModelIconMipCount;
             ++mip_level) {
            const auto texels = atlas.texels_for(
                layer.item_id,
                mip_level);
            CHECK(
                texels.size() ==
                expected_dimension *
                    expected_dimension *
                    kModelIconChannelCount);
            expected_dimension =
                std::max<std::size_t>(
                    1U,
                    expected_dimension / 2U);
        }

        const auto base = atlas.texels_for(
            layer.item_id,
            0U);
        auto covered_pixels = std::size_t {0U};
        auto transparent_pixels = std::size_t {0U};
        for (std::size_t offset = 3U;
             offset < base.size();
             offset += kModelIconChannelCount) {
            covered_pixels +=
                base[offset] != 0U ? 1U : 0U;
            transparent_pixels +=
                base[offset] == 0U ? 1U : 0U;
        }
        CHECK(covered_pixels > 64U);
        CHECK(transparent_pixels > 64U);
    }
    CHECK(
        texel_checksums.size() ==
        kVisualItemModelCount);

    const auto* glass_layer = atlas.layer_for(
        to_block_id(BlockType::Glass));
    const auto* water_layer = atlas.layer_for(
        to_block_id(BlockType::Water));
    const auto* leaves_layer = atlas.layer_for(
        to_block_id(BlockType::Leaves));
    const auto* torch_layer = atlas.layer_for(
        to_block_id(BlockType::Torch));
    const auto* stone_layer = atlas.layer_for(
        to_block_id(BlockType::Stone));
    REQUIRE(glass_layer != nullptr);
    REQUIRE(water_layer != nullptr);
    REQUIRE(leaves_layer != nullptr);
    REQUIRE(torch_layer != nullptr);
    REQUIRE(stone_layer != nullptr);
    CHECK(has_model_icon_layer_flag(
        glass_layer->flags,
        ModelIconLayerFlags::AlphaMaterial));
    CHECK(has_model_icon_layer_flag(
        glass_layer->flags,
        ModelIconLayerFlags::TwoSided));
    CHECK(has_model_icon_layer_flag(
        water_layer->flags,
        ModelIconLayerFlags::AlphaMaterial));
    CHECK(has_model_icon_layer_flag(
        leaves_layer->flags,
        ModelIconLayerFlags::AlphaMaterial));
    CHECK(has_model_icon_layer_flag(
        leaves_layer->flags,
        ModelIconLayerFlags::TwoSided));
    CHECK(has_model_icon_layer_flag(
        torch_layer->flags,
        ModelIconLayerFlags::Emissive));
    CHECK_FALSE(has_model_icon_layer_flag(
        stone_layer->flags,
        ModelIconLayerFlags::AlphaMaterial));

    CHECK(
        atlas.texels_for(
                 to_block_id(BlockType::Air),
                 0U)
            .empty());
    CHECK(
        atlas.texels_for(
                 static_cast<BlockId>(255U),
                 0U)
            .empty());
    CHECK(
        atlas.texels_for(
                 to_block_id(BlockType::Grass),
                 kModelIconMipCount)
            .empty());

    const auto torch = atlas.texels_for(
        to_block_id(BlockType::Torch),
        0U);
    for (const auto wall_torch : {
             BlockType::TorchWallPositiveX,
             BlockType::TorchWallNegativeX,
             BlockType::TorchWallPositiveZ,
             BlockType::TorchWallNegativeZ,
         }) {
        const auto wall = atlas.texels_for(
            to_block_id(wall_torch),
            0U);
        CHECK(std::ranges::equal(torch, wall));
    }

    const auto musket = atlas.texels_for(
        to_block_id(BlockType::Musket),
        0U);
    REQUIRE_FALSE(musket.empty());
    auto warm_wood_pixels = std::size_t {0U};
    auto neutral_steel_pixels = std::size_t {0U};
    auto dark_pixels = std::size_t {0U};
    auto transparent_pixels = std::size_t {0U};
    auto minimum_x = std::size_t {kModelIconSize};
    auto minimum_y = std::size_t {kModelIconSize};
    auto maximum_x = std::size_t {0U};
    auto maximum_y = std::size_t {0U};
    for (std::size_t y = 0U; y < kModelIconSize; ++y) {
        for (std::size_t x = 0U; x < kModelIconSize; ++x) {
            const auto offset =
                (y * kModelIconSize + x) *
                kModelIconChannelCount;
            const auto red = musket[offset + 0U];
            const auto green = musket[offset + 1U];
            const auto blue = musket[offset + 2U];
            const auto alpha = musket[offset + 3U];
            if (alpha == 0U) {
                ++transparent_pixels;
                continue;
            }
            minimum_x = std::min(minimum_x, x);
            minimum_y = std::min(minimum_y, y);
            maximum_x = std::max(maximum_x, x);
            maximum_y = std::max(maximum_y, y);
            warm_wood_pixels +=
                red > green + 8U &&
                        green > blue + 8U
                    ? 1U
                    : 0U;
            const auto maximum_channel =
                std::max({red, green, blue});
            const auto minimum_channel =
                std::min({red, green, blue});
            neutral_steel_pixels +=
                maximum_channel - minimum_channel < 34 &&
                        maximum_channel > 58U
                    ? 1U
                    : 0U;
            dark_pixels +=
                maximum_channel < 70U
                    ? 1U
                    : 0U;
        }
    }
    REQUIRE(minimum_x <= maximum_x);
    REQUIRE(minimum_y <= maximum_y);
    const auto silhouette_width =
        maximum_x - minimum_x + 1U;
    const auto silhouette_height =
        maximum_y - minimum_y + 1U;
    CHECK(transparent_pixels > 4'000U);
    CHECK(warm_wood_pixels > 16U);
    CHECK(neutral_steel_pixels > 16U);
    CHECK(dark_pixels > 0U);
    CHECK(
        std::max(
            silhouette_width,
            silhouette_height) >
        80U);
    CHECK(
        std::max(
            silhouette_width,
            silhouette_height) >
        std::min(
            silhouette_width,
            silhouette_height) *
            2U);
}

TEST_CASE(
    "la regeneration CPU reproduit exactement l'asset versionne") {
    const auto material_load =
        load_visual_material_pack(material_pack_path());
    REQUIRE_MESSAGE(material_load, material_load.message);
    REQUIRE(material_load.pack.has_value());

    const auto generated =
        generate_model_icon_atlas(*material_load.pack);
    REQUIRE_MESSAGE(generated, generated.message);
    REQUIRE(generated.atlas.has_value());
    CHECK(
        generated.atlas->metadata
            .source_material_checksum ==
        material_load.pack->content_checksum);
    CHECK(
        generated.atlas->metadata.content_checksum ==
        kExpectedIconContentChecksum);

    const auto generated_bytes =
        serialize_model_icon_atlas(*generated.atlas);
    const auto versioned_bytes =
        read_bytes(icon_atlas_path());
    REQUIRE_FALSE(generated_bytes.empty());
    REQUIRE_FALSE(versioned_bytes.empty());
    CHECK(generated_bytes == versioned_bytes);

    const auto reparsed =
        parse_model_icon_atlas(generated_bytes);
    REQUIRE_MESSAGE(reparsed, reparsed.message);
    REQUIRE(reparsed.atlas.has_value());
    CHECK(
        reparsed.atlas->metadata ==
        generated.atlas->metadata);
    CHECK(
        reparsed.atlas->layers ==
        generated.atlas->layers);
    CHECK(
        reparsed.atlas->mip_levels ==
        generated.atlas->mip_levels);
    CHECK(
        reparsed.atlas->texels ==
        generated.atlas->texels);
}

TEST_CASE(
    "le parseur rejette les schemas tables tailles et checksums corrompus") {
    const auto original = read_bytes(icon_atlas_path());
    REQUIRE(
        original.size() >
        kModelIconAtlasHeaderSize +
            kModelIconAtlasLayerRecordSize);

    auto truncated = original;
    truncated.resize(kModelIconAtlasHeaderSize - 1U);
    CHECK(
        parse_model_icon_atlas(truncated).error ==
        ModelIconAtlasError::Truncated);

    auto bad_magic = original;
    bad_magic[0U] ^= 0x7FU;
    CHECK(
        parse_model_icon_atlas(bad_magic).error ==
        ModelIconAtlasError::InvalidMagic);

    auto bad_version = original;
    bad_version[8U] = 0xFFU;
    bad_version[9U] = 0x7FU;
    CHECK(
        parse_model_icon_atlas(bad_version).error ==
        ModelIconAtlasError::UnsupportedVersion);

    auto bad_width = original;
    bad_width[12U] = 127U;
    bad_width[13U] = 0U;
    CHECK(
        parse_model_icon_atlas(bad_width).error ==
        ModelIconAtlasError::InvalidDimensions);

    auto bad_mips = original;
    bad_mips[18U] = 7U;
    bad_mips[19U] = 0U;
    CHECK(
        parse_model_icon_atlas(bad_mips).error ==
        ModelIconAtlasError::InvalidMipChain);

    auto bad_source = original;
    std::fill(
        bad_source.begin() + 48,
        bad_source.begin() + 56,
        std::uint8_t {0U});
    CHECK(
        parse_model_icon_atlas(bad_source).error ==
        ModelIconAtlasError::InvalidHeader);

    auto extra_byte = original;
    extra_byte.push_back(0U);
    CHECK(
        parse_model_icon_atlas(extra_byte).error ==
        ModelIconAtlasError::SizeMismatch);

    auto bad_payload = original;
    bad_payload.back() ^= 0x01U;
    CHECK(
        parse_model_icon_atlas(bad_payload).error ==
        ModelIconAtlasError::ChecksumMismatch);

    auto bad_item = original;
    bad_item[kModelIconAtlasHeaderSize] =
        to_block_id(BlockType::Dirt);
    refresh_global_checksum(bad_item);
    CHECK(
        parse_model_icon_atlas(bad_item).error ==
        ModelIconAtlasError::InvalidLayerTable);

    auto bad_layer_checksum = original;
    bad_layer_checksum[
        kModelIconAtlasHeaderSize + 16U] ^= 0x80U;
    refresh_global_checksum(bad_layer_checksum);
    CHECK(
        parse_model_icon_atlas(bad_layer_checksum).error ==
        ModelIconAtlasError::ChecksumMismatch);

    const auto missing = load_model_icon_atlas(
        icon_atlas_path().parent_path() /
        "atlas-icones-absent.vmia");
    CHECK(
        missing.error ==
        ModelIconAtlasError::IoFailure);
    CHECK_FALSE(missing.atlas.has_value());
    CHECK_FALSE(missing.message.empty());
}

TEST_CASE(
    "la generation refuse un catalogue de materiaux incomplet") {
    const auto generated =
        generate_model_icon_atlas(VisualMaterialPack {});
    CHECK_FALSE(generated.atlas.has_value());
    CHECK(
        generated.error ==
        ModelIconAtlasError::InvalidMaterialPack);
    CHECK_FALSE(generated.message.empty());
}

} // namespace valcraft
