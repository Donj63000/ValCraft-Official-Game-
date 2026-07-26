#include "render/ModelIconAtlas.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace valcraft {
namespace {

constexpr std::uint8_t kEncodingRgba8Srgb = 1U;
constexpr std::uint16_t kPackFlagCompleteMipChain = 1U << 0U;
constexpr std::uint16_t kPackFlagTransparentBackground = 1U << 1U;
constexpr std::uint16_t kPackFlags =
    kPackFlagCompleteMipChain | kPackFlagTransparentBackground;
constexpr std::uint16_t kKnownLayerFlagMask =
    static_cast<std::uint16_t>(ModelIconLayerFlags::AlphaMaterial) |
    static_cast<std::uint16_t>(ModelIconLayerFlags::TwoSided) |
    static_cast<std::uint16_t>(ModelIconLayerFlags::Emissive);
constexpr int kRasterSupersample = 2;
constexpr int kRasterDimension =
    static_cast<int>(kModelIconSize) * kRasterSupersample;
constexpr int kRasterPadding = 20 * kRasterSupersample;
constexpr std::int64_t kSubpixelScale = 256;
constexpr std::int64_t kSubpixelHalf = kSubpixelScale / 2;
constexpr std::uint64_t kMaximumIconAtlasSize =
    64ULL * 1024ULL * 1024ULL;

struct SourceVertex {
    glm::vec3 position {0.0F};
    glm::vec3 normal {0.0F, 1.0F, 0.0F};
    glm::vec2 uv {0.0F};
};

struct SourceTriangle {
    std::array<SourceVertex, 3> vertices {};
    VisualMaterialId material = VisualMaterialId::None;
    glm::vec4 tint {1.0F};
    bool two_sided = false;
};

struct RasterVertex {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t depth = 0;
    glm::vec3 normal {0.0F, 1.0F, 0.0F};
    glm::vec2 uv {0.0F};
};

[[nodiscard]] constexpr auto read_u16(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] constexpr auto read_u32(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept -> std::uint32_t {
    auto value = std::uint32_t {0U};
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        value |= static_cast<std::uint32_t>(bytes[offset + byte])
                 << static_cast<unsigned int>(byte * 8U);
    }
    return value;
}

[[nodiscard]] constexpr auto read_u64(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept -> std::uint64_t {
    auto value = std::uint64_t {0U};
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
        value |= static_cast<std::uint64_t>(bytes[offset + byte])
                 << static_cast<unsigned int>(byte * 8U);
    }
    return value;
}

void append_u16(
    std::vector<std::uint8_t>& bytes,
    std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_u32(
    std::vector<std::uint8_t>& bytes,
    std::uint32_t value) {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        bytes.push_back(
            static_cast<std::uint8_t>(
                value >> static_cast<unsigned int>(byte * 8U)));
    }
}

void append_u64(
    std::vector<std::uint8_t>& bytes,
    std::uint64_t value) {
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
        bytes.push_back(
            static_cast<std::uint8_t>(
                value >> static_cast<unsigned int>(byte * 8U)));
    }
}

[[nodiscard]] constexpr auto complete_mip_count(
    std::uint16_t width,
    std::uint16_t height) noexcept -> std::uint16_t {
    auto dimension = std::max(width, height);
    auto count = std::uint16_t {1U};
    while (dimension > 1U) {
        dimension = static_cast<std::uint16_t>(dimension / 2U);
        ++count;
    }
    return count;
}

[[nodiscard]] constexpr auto mip_dimension(
    std::uint16_t base,
    std::uint16_t level) noexcept -> std::uint16_t {
    auto value = base;
    for (std::uint16_t index = 0U;
         index < level && value > 1U;
         ++index) {
        value = static_cast<std::uint16_t>(value / 2U);
    }
    return std::max<std::uint16_t>(value, 1U);
}

[[nodiscard]] auto build_mip_descriptors(
    std::uint16_t width,
    std::uint16_t height,
    std::uint16_t mip_count)
    -> std::optional<std::vector<ModelIconMipLevel>> {
    std::vector<ModelIconMipLevel> mips;
    mips.reserve(mip_count);
    auto offset = std::size_t {0U};
    for (std::uint16_t level = 0U; level < mip_count; ++level) {
        const auto mip_width = mip_dimension(width, level);
        const auto mip_height = mip_dimension(height, level);
        const auto pixel_count =
            static_cast<std::size_t>(mip_width) *
            static_cast<std::size_t>(mip_height);
        if (pixel_count >
            (std::numeric_limits<std::size_t>::max)() /
                kModelIconChannelCount) {
            return std::nullopt;
        }
        const auto byte_count =
            pixel_count * kModelIconChannelCount;
        if (offset >
            (std::numeric_limits<std::size_t>::max)() -
                byte_count) {
            return std::nullopt;
        }
        mips.push_back(ModelIconMipLevel {
            mip_width,
            mip_height,
            offset,
            byte_count,
        });
        offset += byte_count;
    }
    return mips;
}

[[nodiscard]] auto failure(
    ModelIconAtlasError error,
    std::string message) -> ModelIconAtlasResult {
    ModelIconAtlasResult result {};
    result.error = error;
    result.message = std::move(message);
    return result;
}

[[nodiscard]] auto safe_normalize(
    const glm::vec3& value,
    const glm::vec3& fallback) noexcept -> glm::vec3 {
    const auto squared_length = glm::dot(value, value);
    if (!std::isfinite(squared_length) ||
        squared_length <= 1.0e-16F) {
        return fallback;
    }
    return value / std::sqrt(squared_length);
}

[[nodiscard]] auto collect_triangles(
    const VisualItemModel& model) -> std::vector<SourceTriangle> {
    std::vector<SourceTriangle> triangles;
    for (const auto& part : model.primitives) {
        const auto mesh = build_stylized_primitive(
            part.primitive,
            StylizedPrimitiveLod::Medium);
        const auto normal_transform =
            glm::inverseTranspose(glm::mat3 {part.transform});
        triangles.reserve(
            triangles.size() + mesh.indices.size() / 3U);

        for (std::size_t index = 0U;
             index < mesh.indices.size();
             index += 3U) {
            SourceTriangle triangle {};
            triangle.material = part.material;
            triangle.tint = part.albedo_tint;
            triangle.two_sided =
                part.two_sided ||
                visual_material_definition(part.material).two_sided;

            for (std::size_t corner = 0U;
                 corner < 3U;
                 ++corner) {
                const auto vertex_index =
                    mesh.indices[index + corner];
                if (vertex_index >= mesh.vertices.size()) {
                    return {};
                }
                const auto& vertex = mesh.vertices[vertex_index];
                const auto position = part.transform *
                    glm::vec4 {
                        vertex.x,
                        vertex.y,
                        vertex.z,
                        1.0F,
                    };
                auto normal = normal_transform *
                    glm::vec3 {
                        vertex.nx,
                        vertex.ny,
                        vertex.nz,
                    };
                normal = safe_normalize(
                    normal,
                    glm::vec3 {0.0F, 1.0F, 0.0F});
                triangle.vertices[corner] = SourceVertex {
                    glm::vec3 {position},
                    normal,
                    glm::vec2 {vertex.u, vertex.v},
                };
            }
            triangles.push_back(triangle);
        }
    }
    return triangles;
}

[[nodiscard]] auto edge_function(
    const RasterVertex& a,
    const RasterVertex& b,
    std::int64_t x,
    std::int64_t y) noexcept -> std::int64_t {
    return (b.x - a.x) * (y - a.y) -
           (b.y - a.y) * (x - a.x);
}

[[nodiscard]] auto sample_material_albedo(
    const VisualMaterialPack& materials,
    VisualMaterialId material,
    const glm::vec2& uv) noexcept
    -> std::array<std::uint8_t, 4> {
    const auto texels = materials.texels_for(
        material,
        VisualMaterialTexture::Albedo,
        0U);
    const auto expected_size =
        static_cast<std::size_t>(materials.width) *
        static_cast<std::size_t>(materials.height) *
        kVisualMaterialPackChannelCount;
    if (texels.size() != expected_size ||
        materials.width == 0U ||
        materials.height == 0U) {
        return {255U, 0U, 255U, 255U};
    }

    const auto clamped_u = std::clamp(uv.x, 0.0F, 1.0F);
    const auto clamped_v = std::clamp(uv.y, 0.0F, 1.0F);
    const auto x = static_cast<std::size_t>(std::llround(
        static_cast<double>(clamped_u) *
        static_cast<double>(materials.width - 1U)));
    const auto y = static_cast<std::size_t>(std::llround(
        static_cast<double>(clamped_v) *
        static_cast<double>(materials.height - 1U)));
    const auto offset =
        (y * static_cast<std::size_t>(materials.width) + x) *
        kVisualMaterialPackChannelCount;
    return {
        texels[offset + 0U],
        texels[offset + 1U],
        texels[offset + 2U],
        texels[offset + 3U],
    };
}

[[nodiscard]] auto downsample_alpha_aware(
    std::span<const std::uint8_t> source,
    std::uint16_t source_width,
    std::uint16_t source_height) -> std::vector<std::uint8_t> {
    const auto destination_width =
        std::max<std::uint16_t>(1U, source_width / 2U);
    const auto destination_height =
        std::max<std::uint16_t>(1U, source_height / 2U);
    std::vector<std::uint8_t> destination(
        static_cast<std::size_t>(destination_width) *
        static_cast<std::size_t>(destination_height) *
        kModelIconChannelCount,
        0U);

    for (std::uint16_t y = 0U; y < destination_height; ++y) {
        for (std::uint16_t x = 0U; x < destination_width; ++x) {
            auto alpha_sum = std::uint32_t {0U};
            std::array<std::uint64_t, 3> premultiplied_sum {
                0U,
                0U,
                0U,
            };
            for (std::uint16_t offset_y = 0U;
                 offset_y < 2U;
                 ++offset_y) {
                for (std::uint16_t offset_x = 0U;
                     offset_x < 2U;
                     ++offset_x) {
                    const auto source_x = std::min<std::uint16_t>(
                        static_cast<std::uint16_t>(x * 2U + offset_x),
                        static_cast<std::uint16_t>(source_width - 1U));
                    const auto source_y = std::min<std::uint16_t>(
                        static_cast<std::uint16_t>(y * 2U + offset_y),
                        static_cast<std::uint16_t>(source_height - 1U));
                    const auto source_offset =
                        (static_cast<std::size_t>(source_y) *
                             source_width +
                         source_x) *
                        kModelIconChannelCount;
                    const auto alpha =
                        static_cast<std::uint32_t>(
                            source[source_offset + 3U]);
                    alpha_sum += alpha;
                    for (std::size_t channel = 0U;
                         channel < 3U;
                         ++channel) {
                        premultiplied_sum[channel] +=
                            static_cast<std::uint64_t>(
                                source[source_offset + channel]) *
                            alpha;
                    }
                }
            }

            const auto destination_offset =
                (static_cast<std::size_t>(y) *
                     destination_width +
                 x) *
                kModelIconChannelCount;
            destination[destination_offset + 3U] =
                static_cast<std::uint8_t>(
                    (alpha_sum + 2U) / 4U);
            if (alpha_sum == 0U) {
                continue;
            }
            for (std::size_t channel = 0U;
                 channel < 3U;
                 ++channel) {
                destination[destination_offset + channel] =
                    static_cast<std::uint8_t>(
                        (premultiplied_sum[channel] +
                         alpha_sum / 2U) /
                        alpha_sum);
            }
        }
    }
    return destination;
}

[[nodiscard]] auto rasterize_model(
    const VisualItemModel& model,
    const VisualMaterialPack& materials)
    -> std::optional<std::vector<std::uint8_t>> {
    const auto triangles = collect_triangles(model);
    if (triangles.empty()) {
        return std::nullopt;
    }

    const auto camera_axis = safe_normalize(
        glm::vec3 {1.35F, 1.08F, 1.70F},
        glm::vec3 {0.0F, 0.0F, 1.0F});
    const auto right_axis = safe_normalize(
        glm::cross(
            glm::vec3 {0.0F, 1.0F, 0.0F},
            camera_axis),
        glm::vec3 {1.0F, 0.0F, 0.0F});
    const auto up_axis = safe_normalize(
        glm::cross(camera_axis, right_axis),
        glm::vec3 {0.0F, 1.0F, 0.0F});

    auto minimum_x = (std::numeric_limits<float>::max)();
    auto maximum_x = (std::numeric_limits<float>::lowest)();
    auto minimum_y = (std::numeric_limits<float>::max)();
    auto maximum_y = (std::numeric_limits<float>::lowest)();
    for (const auto& triangle : triangles) {
        for (const auto& vertex : triangle.vertices) {
            const auto projected_x =
                glm::dot(vertex.position, right_axis);
            const auto projected_y =
                glm::dot(vertex.position, up_axis);
            minimum_x = std::min(minimum_x, projected_x);
            maximum_x = std::max(maximum_x, projected_x);
            minimum_y = std::min(minimum_y, projected_y);
            maximum_y = std::max(maximum_y, projected_y);
        }
    }

    const auto range_x = maximum_x - minimum_x;
    const auto range_y = maximum_y - minimum_y;
    const auto maximum_range = std::max(range_x, range_y);
    if (!std::isfinite(maximum_range) ||
        maximum_range <= 1.0e-6F) {
        return std::nullopt;
    }
    const auto available_pixels = static_cast<float>(
        kRasterDimension - kRasterPadding * 2);
    const auto pixel_scale = available_pixels / maximum_range;
    const auto center_x = (minimum_x + maximum_x) * 0.5F;
    const auto center_y = (minimum_y + maximum_y) * 0.5F;

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(kRasterDimension) *
        static_cast<std::size_t>(kRasterDimension) *
        kModelIconChannelCount,
        0U);
    std::vector<std::int64_t> depth_buffer(
        static_cast<std::size_t>(kRasterDimension) *
            static_cast<std::size_t>(kRasterDimension),
        (std::numeric_limits<std::int64_t>::max)());

    const auto light_direction = safe_normalize(
        glm::vec3 {-0.35F, 0.88F, 0.42F},
        glm::vec3 {0.0F, 1.0F, 0.0F});

    for (const auto& triangle : triangles) {
        const auto geometric_normal = glm::cross(
            triangle.vertices[1].position -
                triangle.vertices[0].position,
            triangle.vertices[2].position -
                triangle.vertices[0].position);
        const auto facing =
            glm::dot(geometric_normal, camera_axis);
        if (!triangle.two_sided && facing <= 1.0e-10F) {
            continue;
        }

        std::array<RasterVertex, 3> vertices {};
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            const auto& source = triangle.vertices[corner];
            const auto projected_x =
                glm::dot(source.position, right_axis);
            const auto projected_y =
                glm::dot(source.position, up_axis);
            const auto screen_x =
                static_cast<float>(kRasterDimension) * 0.5F +
                (projected_x - center_x) * pixel_scale;
            const auto screen_y =
                static_cast<float>(kRasterDimension) * 0.5F -
                (projected_y - center_y) * pixel_scale;
            vertices[corner] = RasterVertex {
                static_cast<std::int64_t>(std::llround(
                    static_cast<double>(screen_x) *
                    static_cast<double>(kSubpixelScale))),
                static_cast<std::int64_t>(std::llround(
                    static_cast<double>(screen_y) *
                    static_cast<double>(kSubpixelScale))),
                static_cast<std::int64_t>(std::llround(
                    -static_cast<double>(
                        glm::dot(source.position, camera_axis)) *
                    1'000'000.0)),
                source.normal,
                source.uv,
            };
        }

        auto area = edge_function(
            vertices[0],
            vertices[1],
            vertices[2].x,
            vertices[2].y);
        if (area == 0) {
            continue;
        }
        if (area < 0) {
            std::swap(vertices[1], vertices[2]);
            area = -area;
        }

        const auto minimum_fixed_x = std::min({
            vertices[0].x,
            vertices[1].x,
            vertices[2].x,
        });
        const auto maximum_fixed_x = std::max({
            vertices[0].x,
            vertices[1].x,
            vertices[2].x,
        });
        const auto minimum_fixed_y = std::min({
            vertices[0].y,
            vertices[1].y,
            vertices[2].y,
        });
        const auto maximum_fixed_y = std::max({
            vertices[0].y,
            vertices[1].y,
            vertices[2].y,
        });
        const auto minimum_pixel_x = std::clamp(
            static_cast<int>(
                (minimum_fixed_x - kSubpixelHalf) /
                kSubpixelScale),
            0,
            kRasterDimension - 1);
        const auto maximum_pixel_x = std::clamp(
            static_cast<int>(
                (maximum_fixed_x + kSubpixelHalf) /
                kSubpixelScale),
            0,
            kRasterDimension - 1);
        const auto minimum_pixel_y = std::clamp(
            static_cast<int>(
                (minimum_fixed_y - kSubpixelHalf) /
                kSubpixelScale),
            0,
            kRasterDimension - 1);
        const auto maximum_pixel_y = std::clamp(
            static_cast<int>(
                (maximum_fixed_y + kSubpixelHalf) /
                kSubpixelScale),
            0,
            kRasterDimension - 1);

        for (int pixel_y = minimum_pixel_y;
             pixel_y <= maximum_pixel_y;
             ++pixel_y) {
            const auto sample_y =
                static_cast<std::int64_t>(pixel_y) *
                    kSubpixelScale +
                kSubpixelHalf;
            for (int pixel_x = minimum_pixel_x;
                 pixel_x <= maximum_pixel_x;
                 ++pixel_x) {
                const auto sample_x =
                    static_cast<std::int64_t>(pixel_x) *
                        kSubpixelScale +
                    kSubpixelHalf;
                const auto weight_0 = edge_function(
                    vertices[1],
                    vertices[2],
                    sample_x,
                    sample_y);
                const auto weight_1 = edge_function(
                    vertices[2],
                    vertices[0],
                    sample_x,
                    sample_y);
                const auto weight_2 = edge_function(
                    vertices[0],
                    vertices[1],
                    sample_x,
                    sample_y);
                if (weight_0 < 0 ||
                    weight_1 < 0 ||
                    weight_2 < 0) {
                    continue;
                }

                const auto depth_numerator =
                    weight_0 * vertices[0].depth +
                    weight_1 * vertices[1].depth +
                    weight_2 * vertices[2].depth;
                const auto depth =
                    (depth_numerator + area / 2) / area;
                const auto pixel_index =
                    static_cast<std::size_t>(pixel_y) *
                        kRasterDimension +
                    static_cast<std::size_t>(pixel_x);
                if (depth >= depth_buffer[pixel_index]) {
                    continue;
                }

                const auto inverse_area =
                    1.0 / static_cast<double>(area);
                const auto barycentric_0 =
                    static_cast<double>(weight_0) * inverse_area;
                const auto barycentric_1 =
                    static_cast<double>(weight_1) * inverse_area;
                const auto barycentric_2 =
                    static_cast<double>(weight_2) * inverse_area;
                auto normal =
                    vertices[0].normal *
                        static_cast<float>(barycentric_0) +
                    vertices[1].normal *
                        static_cast<float>(barycentric_1) +
                    vertices[2].normal *
                        static_cast<float>(barycentric_2);
                normal = safe_normalize(
                    normal,
                    geometric_normal);
                if (glm::dot(normal, camera_axis) < 0.0F) {
                    normal = -normal;
                }
                const auto uv =
                    vertices[0].uv *
                        static_cast<float>(barycentric_0) +
                    vertices[1].uv *
                        static_cast<float>(barycentric_1) +
                    vertices[2].uv *
                        static_cast<float>(barycentric_2);
                const auto albedo = sample_material_albedo(
                    materials,
                    triangle.material,
                    uv);
                const auto alpha = std::clamp(
                    static_cast<int>(std::llround(
                        static_cast<double>(albedo[3]) *
                        static_cast<double>(
                            std::clamp(
                                triangle.tint.a,
                                0.0F,
                                1.0F)))),
                    0,
                    255);
                if (alpha == 0) {
                    continue;
                }

                const auto& material =
                    visual_material_definition(
                        triangle.material);
                const auto diffuse = std::max(
                    glm::dot(normal, light_direction),
                    0.0F);
                const auto view_alignment = std::max(
                    glm::dot(normal, camera_axis),
                    0.0F);
                const auto rim =
                    (1.0F - view_alignment) *
                    (1.0F - view_alignment);
                const auto shade = material.emissive
                    ? 1.08F
                    : std::clamp(
                          0.38F + diffuse * 0.58F +
                              rim * 0.10F,
                          0.24F,
                          1.08F);

                const auto pixel_offset =
                    pixel_index * kModelIconChannelCount;
                for (std::size_t channel = 0U;
                     channel < 3U;
                     ++channel) {
                    const auto value =
                        static_cast<double>(albedo[channel]) *
                        static_cast<double>(std::max(
                            triangle.tint[
                                static_cast<glm::vec4::length_type>(
                                    channel)],
                            0.0F)) *
                        static_cast<double>(shade);
                    pixels[pixel_offset + channel] =
                        static_cast<std::uint8_t>(
                            std::clamp(
                                static_cast<int>(
                                    std::llround(value)),
                                0,
                                255));
                }
                pixels[pixel_offset + 3U] =
                    static_cast<std::uint8_t>(alpha);
                depth_buffer[pixel_index] = depth;
            }
        }
    }

    auto base_level = downsample_alpha_aware(
        pixels,
        static_cast<std::uint16_t>(kRasterDimension),
        static_cast<std::uint16_t>(kRasterDimension));
    const auto has_coverage = std::any_of(
        base_level.begin() + 3,
        base_level.end(),
        [&base_level](const std::uint8_t& alpha) {
            const auto index = static_cast<std::size_t>(
                &alpha - base_level.data());
            return index % kModelIconChannelCount == 3U &&
                   alpha != 0U;
        });
    if (!has_coverage) {
        return std::nullopt;
    }
    return base_level;
}

[[nodiscard]] auto material_pack_supports_models(
    const VisualMaterialPack& materials) noexcept -> bool {
    if (materials.width < 16U ||
        materials.height < 16U ||
        materials.content_checksum == 0U) {
        return false;
    }
    for (std::size_t material_index = 1U;
         material_index < kVisualMaterialCount;
         ++material_index) {
        const auto material =
            static_cast<VisualMaterialId>(material_index);
        const auto texels = materials.texels_for(
            material,
            VisualMaterialTexture::Albedo,
            0U);
        const auto expected =
            static_cast<std::size_t>(materials.width) *
            static_cast<std::size_t>(materials.height) *
            kVisualMaterialPackChannelCount;
        if (texels.size() != expected) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto layer_flags_for(
    const VisualItemModel& model) noexcept -> ModelIconLayerFlags {
    auto flags = ModelIconLayerFlags::None;
    for (const auto& primitive : model.primitives) {
        const auto& material =
            visual_material_definition(primitive.material);
        if (material.alpha_tested ||
            material.surface_class == VisualSurfaceClass::Liquid ||
            primitive.material == VisualMaterialId::ClearGlass ||
            primitive.albedo_tint.a < 0.999F) {
            flags = flags |
                ModelIconLayerFlags::AlphaMaterial;
        }
        if (primitive.two_sided || material.two_sided) {
            flags = flags | ModelIconLayerFlags::TwoSided;
        }
        if (material.emissive) {
            flags = flags | ModelIconLayerFlags::Emissive;
        }
    }
    return flags;
}

void append_layer_record(
    std::vector<std::uint8_t>& bytes,
    const ModelIconLayer& layer) {
    bytes.push_back(layer.item_id);
    bytes.push_back(
        static_cast<std::uint8_t>(layer.model_class));
    append_u16(
        bytes,
        static_cast<std::uint16_t>(
            layer.primary_material));
    append_u16(bytes, layer.primitive_count);
    append_u16(
        bytes,
        static_cast<std::uint16_t>(layer.flags));
    append_u64(bytes, layer.geometry_checksum);
    append_u64(bytes, layer.texel_checksum);
    append_u64(bytes, 0U);
}

[[nodiscard]] auto build_content_bytes(
    const ModelIconAtlas& atlas)
    -> std::vector<std::uint8_t> {
    const auto table_size =
        atlas.layers.size() *
        kModelIconAtlasLayerRecordSize;
    if (table_size >
        (std::numeric_limits<std::size_t>::max)() -
            atlas.texels.size()) {
        return {};
    }
    std::vector<std::uint8_t> content;
    content.reserve(table_size + atlas.texels.size());
    for (const auto& layer : atlas.layers) {
        append_layer_record(content, layer);
    }
    content.insert(
        content.end(),
        atlas.texels.begin(),
        atlas.texels.end());
    return content;
}

[[nodiscard]] auto validate_atlas_for_serialization(
    const ModelIconAtlas& atlas) noexcept -> bool {
    if (atlas.metadata.version != kModelIconAtlasVersion ||
        atlas.metadata.width != kModelIconSize ||
        atlas.metadata.height != kModelIconSize ||
        atlas.metadata.mip_count != kModelIconMipCount ||
        atlas.metadata.recipe_version !=
            kModelIconRecipeVersion ||
        atlas.metadata.source_material_checksum == 0U ||
        atlas.layers.size() != kVisualItemModelCount ||
        atlas.mip_levels.size() != kModelIconMipCount) {
        return false;
    }
    const auto expected_mips = build_mip_descriptors(
        atlas.metadata.width,
        atlas.metadata.height,
        atlas.metadata.mip_count);
    if (!expected_mips.has_value() ||
        *expected_mips != atlas.mip_levels) {
        return false;
    }
    const auto layer_stride =
        atlas.mip_levels.back().offset_within_layer +
        atlas.mip_levels.back().byte_count;
    if (layer_stride >
        (std::numeric_limits<std::size_t>::max)() /
            atlas.layers.size() ||
        atlas.texels.size() !=
            layer_stride * atlas.layers.size()) {
        return false;
    }
    for (std::size_t index = 0U;
         index < atlas.layers.size();
         ++index) {
        const auto& layer = atlas.layers[index];
        if (layer.item_id != kVisualItemCanonicalIds[index] ||
            layer.model_class !=
                visual_item_model_class_for(layer.item_id) ||
            layer.primary_material !=
                visual_material_for_block(layer.item_id) ||
            layer.primitive_count == 0U ||
            layer.geometry_checksum == 0U ||
            layer.texel_checksum == 0U ||
            (static_cast<std::uint16_t>(layer.flags) &
             static_cast<std::uint16_t>(
                 ~kKnownLayerFlagMask)) != 0U) {
            return false;
        }
        const auto layer_texels =
            std::span<const std::uint8_t>(atlas.texels)
                .subspan(index * layer_stride, layer_stride);
        if (model_icon_atlas_checksum(layer_texels) !=
            layer.texel_checksum) {
            return false;
        }
    }
    return true;
}

} // namespace

auto ModelIconAtlas::layer_for(
    BlockId block_id) const noexcept -> const ModelIconLayer* {
    const auto layer_index =
        visual_item_layer_index(block_id);
    if (layer_index >= layers.size() ||
        layers[layer_index].item_id !=
            canonical_visual_item_id(block_id)) {
        return nullptr;
    }
    return &layers[layer_index];
}

auto ModelIconAtlas::texels_for(
    BlockId block_id,
    std::uint16_t mip_level) const noexcept
    -> std::span<const std::uint8_t> {
    const auto layer_index =
        visual_item_layer_index(block_id);
    if (layer_index >= layers.size() ||
        mip_level >= mip_levels.size() ||
        layers[layer_index].item_id !=
            canonical_visual_item_id(block_id)) {
        return {};
    }
    const auto& mip = mip_levels[mip_level];
    const auto layer_stride =
        mip_levels.back().offset_within_layer +
        mip_levels.back().byte_count;
    const auto offset =
        layer_index * layer_stride +
        mip.offset_within_layer;
    if (offset > texels.size() ||
        mip.byte_count > texels.size() - offset) {
        return {};
    }
    return std::span<const std::uint8_t>(texels)
        .subspan(offset, mip.byte_count);
}

auto model_icon_atlas_checksum(
    std::span<const std::uint8_t> bytes) noexcept
    -> std::uint64_t {
    auto checksum = std::uint64_t {14695981039346656037ULL};
    for (const auto byte : bytes) {
        checksum ^= static_cast<std::uint64_t>(byte);
        checksum *= 1099511628211ULL;
    }
    return checksum;
}

auto generate_model_icon_atlas(
    const VisualMaterialPack& material_pack)
    -> ModelIconAtlasResult {
    if (!material_pack_supports_models(material_pack)) {
        return failure(
            ModelIconAtlasError::InvalidMaterialPack,
            "Le pack de materiaux ne couvre pas toutes les recettes d'icones.");
    }

    ModelIconAtlas atlas {};
    atlas.metadata = ModelIconAtlasMetadata {
        kModelIconAtlasVersion,
        kModelIconSize,
        kModelIconSize,
        kModelIconMipCount,
        kModelIconRecipeVersion,
        material_pack.content_checksum,
        0U,
    };
    const auto mips = build_mip_descriptors(
        kModelIconSize,
        kModelIconSize,
        kModelIconMipCount);
    if (!mips.has_value()) {
        return failure(
            ModelIconAtlasError::SizeMismatch,
            "La chaine de mipmaps des icones depasse les limites de la plateforme.");
    }
    atlas.mip_levels = *mips;
    const auto layer_stride =
        atlas.mip_levels.back().offset_within_layer +
        atlas.mip_levels.back().byte_count;
    atlas.layers.reserve(kVisualItemModelCount);
    atlas.texels.reserve(
        layer_stride * kVisualItemModelCount);

    const auto models = build_all_visual_item_models();
    for (std::size_t model_index = 0U;
         model_index < models.size();
         ++model_index) {
        const auto& model = models[model_index];
        if (model.empty() ||
            model.item_id !=
                kVisualItemCanonicalIds[model_index] ||
            model.geometry_checksum == 0U ||
            model.primitives.size() >
                (std::numeric_limits<std::uint16_t>::max)()) {
            return failure(
                ModelIconAtlasError::InvalidModel,
                "Une recette d'icone ne produit pas de geometrie valide.");
        }

        auto level_pixels =
            rasterize_model(model, material_pack);
        if (!level_pixels.has_value() ||
            level_pixels->size() !=
                atlas.mip_levels.front().byte_count) {
            return failure(
                ModelIconAtlasError::InvalidModel,
                "Le rasterizer n'a produit aucun pixel pour une recette d'icone.");
        }

        const auto layer_begin = atlas.texels.size();
        atlas.texels.insert(
            atlas.texels.end(),
            level_pixels->begin(),
            level_pixels->end());
        auto level_width = kModelIconSize;
        auto level_height = kModelIconSize;
        for (std::uint16_t level = 1U;
             level < kModelIconMipCount;
             ++level) {
            auto next_level = downsample_alpha_aware(
                *level_pixels,
                level_width,
                level_height);
            level_width =
                std::max<std::uint16_t>(
                    1U,
                    level_width / 2U);
            level_height =
                std::max<std::uint16_t>(
                    1U,
                    level_height / 2U);
            if (next_level.size() !=
                atlas.mip_levels[level].byte_count) {
                return failure(
                    ModelIconAtlasError::InvalidMipChain,
                    "Une mipmap d'icone a une taille invalide.");
            }
            atlas.texels.insert(
                atlas.texels.end(),
                next_level.begin(),
                next_level.end());
            level_pixels = std::move(next_level);
        }
        const auto produced_layer_bytes =
            atlas.texels.size() - layer_begin;
        if (produced_layer_bytes != layer_stride) {
            return failure(
                ModelIconAtlasError::SizeMismatch,
                "La couche d'icone rasterisee a une taille incoherente.");
        }
        const auto layer_texels =
            std::span<const std::uint8_t>(atlas.texels)
                .subspan(layer_begin, layer_stride);
        atlas.layers.push_back(ModelIconLayer {
            model.item_id,
            model.model_class,
            visual_material_for_block(model.item_id),
            static_cast<std::uint16_t>(
                model.primitives.size()),
            layer_flags_for(model),
            model.geometry_checksum,
            model_icon_atlas_checksum(layer_texels),
        });
    }

    const auto content = build_content_bytes(atlas);
    if (content.empty()) {
        return failure(
            ModelIconAtlasError::SizeMismatch,
            "Le contenu de l'atlas d'icones n'a pas pu etre assemble.");
    }
    atlas.metadata.content_checksum =
        model_icon_atlas_checksum(content);

    ModelIconAtlasResult result {};
    result.atlas.emplace(std::move(atlas));
    return result;
}

auto serialize_model_icon_atlas(
    const ModelIconAtlas& atlas)
    -> std::vector<std::uint8_t> {
    if (!validate_atlas_for_serialization(atlas)) {
        return {};
    }
    const auto content = build_content_bytes(atlas);
    if (content.empty()) {
        return {};
    }
    const auto table_byte_count =
        static_cast<std::uint32_t>(
            atlas.layers.size() *
            kModelIconAtlasLayerRecordSize);
    const auto payload_byte_count =
        static_cast<std::uint64_t>(atlas.texels.size());
    const auto content_checksum =
        model_icon_atlas_checksum(content);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(
        kModelIconAtlasHeaderSize + content.size());
    bytes.insert(
        bytes.end(),
        kModelIconAtlasMagic.begin(),
        kModelIconAtlasMagic.end());
    append_u16(bytes, kModelIconAtlasVersion);
    append_u16(bytes, kModelIconAtlasHeaderSize);
    append_u16(bytes, atlas.metadata.width);
    append_u16(bytes, atlas.metadata.height);
    append_u16(
        bytes,
        static_cast<std::uint16_t>(atlas.layers.size()));
    append_u16(bytes, atlas.metadata.mip_count);
    bytes.push_back(kModelIconChannelCount);
    bytes.push_back(kEncodingRgba8Srgb);
    append_u16(bytes, kPackFlags);
    append_u32(bytes, kModelIconAtlasLayerRecordSize);
    append_u32(bytes, table_byte_count);
    append_u64(bytes, payload_byte_count);
    append_u64(bytes, content_checksum);
    append_u64(
        bytes,
        atlas.metadata.source_material_checksum);
    append_u32(bytes, atlas.metadata.recipe_version);
    append_u32(bytes, 0U);
    if (bytes.size() != kModelIconAtlasHeaderSize) {
        return {};
    }
    bytes.insert(
        bytes.end(),
        content.begin(),
        content.end());
    return bytes;
}

auto parse_model_icon_atlas(
    std::span<const std::uint8_t> bytes)
    -> ModelIconAtlasResult {
    if (bytes.size() < kModelIconAtlasHeaderSize) {
        return failure(
            ModelIconAtlasError::Truncated,
            "L'atlas d'icones est plus court que son en-tete.");
    }
    if (!std::equal(
            kModelIconAtlasMagic.begin(),
            kModelIconAtlasMagic.end(),
            bytes.begin())) {
        return failure(
            ModelIconAtlasError::InvalidMagic,
            "La signature de l'atlas d'icones est invalide.");
    }

    const auto version = read_u16(bytes, 8U);
    if (version != kModelIconAtlasVersion) {
        return failure(
            ModelIconAtlasError::UnsupportedVersion,
            "La version de l'atlas d'icones n'est pas prise en charge.");
    }
    const auto header_size = read_u16(bytes, 10U);
    const auto width = read_u16(bytes, 12U);
    const auto height = read_u16(bytes, 14U);
    const auto layer_count = read_u16(bytes, 16U);
    const auto mip_count = read_u16(bytes, 18U);
    const auto channel_count = bytes[20U];
    const auto encoding = bytes[21U];
    const auto flags = read_u16(bytes, 22U);
    const auto layer_record_size = read_u32(bytes, 24U);
    const auto table_byte_count = read_u32(bytes, 28U);
    const auto payload_byte_count = read_u64(bytes, 32U);
    const auto declared_checksum = read_u64(bytes, 40U);
    const auto source_material_checksum = read_u64(bytes, 48U);
    const auto recipe_version = read_u32(bytes, 56U);
    const auto reserved = read_u32(bytes, 60U);

    if (header_size != kModelIconAtlasHeaderSize ||
        channel_count != kModelIconChannelCount ||
        encoding != kEncodingRgba8Srgb ||
        flags != kPackFlags ||
        layer_record_size !=
            kModelIconAtlasLayerRecordSize ||
        recipe_version != kModelIconRecipeVersion ||
        source_material_checksum == 0U ||
        reserved != 0U) {
        return failure(
            ModelIconAtlasError::InvalidHeader,
            "Les caracteristiques de l'atlas d'icones sont invalides.");
    }
    if (width != kModelIconSize ||
        height != kModelIconSize) {
        return failure(
            ModelIconAtlasError::InvalidDimensions,
            "Les icones doivent conserver leur resolution canonique de 128 pixels.");
    }
    if (mip_count != complete_mip_count(width, height) ||
        mip_count != kModelIconMipCount) {
        return failure(
            ModelIconAtlasError::InvalidMipChain,
            "L'atlas d'icones ne contient pas une chaine complete de mipmaps.");
    }
    if (layer_count != kVisualItemModelCount ||
        table_byte_count !=
            static_cast<std::uint32_t>(layer_count) *
                kModelIconAtlasLayerRecordSize) {
        return failure(
            ModelIconAtlasError::InvalidLayerTable,
            "La table des icones ne couvre pas tous les objets affichables.");
    }
    const auto mips =
        build_mip_descriptors(width, height, mip_count);
    if (!mips.has_value()) {
        return failure(
            ModelIconAtlasError::SizeMismatch,
            "La taille des mipmaps d'icones depasse les limites de la plateforme.");
    }
    const auto layer_stride =
        mips->back().offset_within_layer +
        mips->back().byte_count;
    if (layer_stride >
        (std::numeric_limits<std::uint64_t>::max)() /
            layer_count ||
        payload_byte_count !=
            static_cast<std::uint64_t>(layer_stride) *
                layer_count ||
        payload_byte_count > kMaximumIconAtlasSize) {
        return failure(
            ModelIconAtlasError::SizeMismatch,
            "La taille declaree des texels d'icones est incoherente.");
    }
    const auto expected_size =
        static_cast<std::uint64_t>(header_size) +
        table_byte_count +
        payload_byte_count;
    if (expected_size != bytes.size() ||
        expected_size > kMaximumIconAtlasSize) {
        return failure(
            ModelIconAtlasError::SizeMismatch,
            "La taille reelle de l'atlas d'icones ne correspond pas a son en-tete.");
    }
    const auto content = bytes.subspan(header_size);
    if (model_icon_atlas_checksum(content) !=
        declared_checksum) {
        return failure(
            ModelIconAtlasError::ChecksumMismatch,
            "Le checksum global de l'atlas d'icones est invalide.");
    }

    ModelIconAtlas atlas {};
    atlas.metadata = ModelIconAtlasMetadata {
        version,
        width,
        height,
        mip_count,
        recipe_version,
        source_material_checksum,
        declared_checksum,
    };
    atlas.mip_levels = *mips;
    atlas.layers.reserve(layer_count);
    for (std::size_t index = 0U;
         index < layer_count;
         ++index) {
        const auto offset =
            static_cast<std::size_t>(header_size) +
            index * kModelIconAtlasLayerRecordSize;
        const auto item_id = bytes[offset + 0U];
        const auto model_class =
            static_cast<VisualItemModelClass>(
                bytes[offset + 1U]);
        const auto primary_material =
            static_cast<VisualMaterialId>(
                read_u16(bytes, offset + 2U));
        const auto primitive_count =
            read_u16(bytes, offset + 4U);
        const auto layer_flags =
            read_u16(bytes, offset + 6U);
        const auto geometry_checksum =
            read_u64(bytes, offset + 8U);
        const auto texel_checksum =
            read_u64(bytes, offset + 16U);
        const auto layer_reserved =
            read_u64(bytes, offset + 24U);

        if (item_id != kVisualItemCanonicalIds[index] ||
            model_class !=
                visual_item_model_class_for(item_id) ||
            primary_material !=
                visual_material_for_block(item_id) ||
            primitive_count == 0U ||
            geometry_checksum == 0U ||
            texel_checksum == 0U ||
            (layer_flags &
             static_cast<std::uint16_t>(
                 ~kKnownLayerFlagMask)) != 0U ||
            layer_reserved != 0U) {
            return failure(
                ModelIconAtlasError::InvalidLayerTable,
                "Une entree de l'atlas ne correspond pas au catalogue d'objets.");
        }
        atlas.layers.push_back(ModelIconLayer {
            item_id,
            model_class,
            primary_material,
            primitive_count,
            static_cast<ModelIconLayerFlags>(
                layer_flags),
            geometry_checksum,
            texel_checksum,
        });
    }

    const auto payload_offset =
        static_cast<std::size_t>(header_size) +
        table_byte_count;
    const auto payload = bytes.subspan(
        payload_offset,
        static_cast<std::size_t>(payload_byte_count));
    atlas.texels.assign(
        payload.begin(),
        payload.end());
    for (std::size_t index = 0U;
         index < atlas.layers.size();
         ++index) {
        const auto layer_texels =
            std::span<const std::uint8_t>(atlas.texels)
                .subspan(index * layer_stride, layer_stride);
        if (model_icon_atlas_checksum(layer_texels) !=
            atlas.layers[index].texel_checksum) {
            return failure(
                ModelIconAtlasError::ChecksumMismatch,
                "Le checksum d'une couche d'icone est invalide.");
        }
    }

    ModelIconAtlasResult result {};
    result.atlas.emplace(std::move(atlas));
    return result;
}

auto load_model_icon_atlas(
    const std::filesystem::path& path)
    -> ModelIconAtlasResult {
    std::error_code error;
    const auto file_size =
        std::filesystem::file_size(path, error);
    if (error) {
        return failure(
            ModelIconAtlasError::IoFailure,
            "Impossible de lire la taille de l'atlas d'icones : " +
                error.message());
    }
    if (file_size > kMaximumIconAtlasSize ||
        file_size >
            static_cast<std::uintmax_t>(
                (std::numeric_limits<std::size_t>::max)())) {
        return failure(
            ModelIconAtlasError::SizeMismatch,
            "L'atlas d'icones depasse la taille maximale autorisee.");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return failure(
            ModelIconAtlasError::IoFailure,
            "Impossible d'ouvrir l'atlas d'icones.");
    }
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(file_size));
    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!input ||
            input.gcount() !=
                static_cast<std::streamsize>(bytes.size())) {
            return failure(
                ModelIconAtlasError::IoFailure,
                "L'atlas d'icones n'a pas pu etre lu entierement.");
        }
    }
    return parse_model_icon_atlas(bytes);
}

auto write_model_icon_atlas(
    const std::filesystem::path& path,
    const ModelIconAtlas& atlas,
    std::string* error_message) -> bool {
    const auto bytes =
        serialize_model_icon_atlas(atlas);
    if (bytes.empty()) {
        if (error_message != nullptr) {
            *error_message =
                "L'atlas d'icones n'est pas serialisable.";
        }
        return false;
    }
    std::ofstream output(
        path,
        std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error_message != nullptr) {
            *error_message =
                "Impossible d'ouvrir la destination de l'atlas d'icones.";
        }
        return false;
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        if (error_message != nullptr) {
            *error_message =
                "L'atlas d'icones n'a pas pu etre ecrit entierement.";
        }
        return false;
    }
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

} // namespace valcraft
