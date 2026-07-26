#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace valcraft {

// Je conserve les positions et les UV en pleine precision, car ils definissent
// les raccords entre chunks. Je compacte uniquement les attributs bornes que le
// GPU sait normaliser sans modifier la geometrie de l'eau.
struct WaterVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    std::int8_t nx = 0;
    std::int8_t ny = 127;
    std::int8_t nz = 0;
    std::uint8_t normal_padding = 0U;
    std::uint16_t face_shade_half = 0U;
    std::uint8_t ao = 1U;
    std::uint8_t sky_light = 255U;
    std::uint8_t block_light = 0U;
    std::uint8_t material_class = 0U;
    std::uint8_t wave_weight = 0U;
    std::uint8_t reserved = 0U;

    auto operator==(const WaterVertex&) const -> bool = default;
};

static_assert(sizeof(WaterVertex) == 32U);
static_assert(alignof(WaterVertex) == alignof(float));
static_assert(std::is_standard_layout_v<WaterVertex>);
static_assert(std::is_trivially_copyable_v<WaterVertex>);

[[nodiscard]] constexpr auto pack_water_snorm(float value) noexcept -> std::int8_t {
    const auto clamped = std::clamp(value, -1.0F, 1.0F);
    return static_cast<std::int8_t>(
        clamped >= 0.0F
            ? clamped * 127.0F + 0.5F
            : clamped * 127.0F - 0.5F);
}

[[nodiscard]] constexpr auto unpack_water_snorm(std::int8_t value) noexcept -> float {
    return std::max(
        static_cast<float>(value) / 127.0F,
        -1.0F);
}

[[nodiscard]] constexpr auto pack_water_unorm(float value) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(
        std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
}

[[nodiscard]] constexpr auto unpack_water_unorm(std::uint8_t value) noexcept -> float {
    return static_cast<float>(value) / 255.0F;
}

[[nodiscard]] inline auto pack_water_half(float value) noexcept -> std::uint16_t {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto sign = static_cast<std::uint16_t>((bits >> 16U) & 0x8000U);
    auto exponent = static_cast<int>((bits >> 23U) & 0xFFU) - 127 + 15;
    auto mantissa = bits & 0x007FFFFFU;

    if (exponent <= 0) {
        if (exponent < -10) {
            return sign;
        }
        mantissa |= 0x00800000U;
        const auto shift = static_cast<unsigned int>(14 - exponent);
        const auto rounding_bias = (std::uint32_t {1U} << (shift - 1U)) - 1U;
        const auto tie = (mantissa >> shift) & 1U;
        return static_cast<std::uint16_t>(
            sign |
            static_cast<std::uint16_t>(
                (mantissa + rounding_bias + tie) >> shift));
    }

    if (exponent >= 31) {
        if (mantissa == 0U) {
            return static_cast<std::uint16_t>(sign | 0x7C00U);
        }
        return static_cast<std::uint16_t>(
            sign |
            0x7C00U |
            static_cast<std::uint16_t>(std::max(mantissa >> 13U, 1U)));
    }

    mantissa += 0x00000FFFU + ((mantissa >> 13U) & 1U);
    if ((mantissa & 0x00800000U) != 0U) {
        mantissa = 0U;
        ++exponent;
        if (exponent >= 31) {
            return static_cast<std::uint16_t>(sign | 0x7C00U);
        }
    }

    return static_cast<std::uint16_t>(
        sign |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(exponent) << 10U) |
        static_cast<std::uint16_t>(mantissa >> 13U));
}

[[nodiscard]] inline auto unpack_water_half(std::uint16_t value) noexcept -> float {
    const auto sign = static_cast<std::uint32_t>(value & 0x8000U) << 16U;
    const auto stored_exponent =
        static_cast<std::uint32_t>((value >> 10U) & 0x1FU);
    auto mantissa = static_cast<std::uint32_t>(value & 0x03FFU);
    std::uint32_t bits = 0U;

    if (stored_exponent == 0U) {
        if (mantissa == 0U) {
            bits = sign;
        } else {
            auto unbiased_exponent = -14;
            while ((mantissa & 0x0400U) == 0U) {
                mantissa <<= 1U;
                --unbiased_exponent;
            }
            mantissa &= 0x03FFU;
            bits =
                sign |
                (static_cast<std::uint32_t>(unbiased_exponent + 127) << 23U) |
                (mantissa << 13U);
        }
    } else if (stored_exponent == 31U) {
        bits = sign | 0x7F800000U | (mantissa << 13U);
    } else {
        bits =
            sign |
            ((stored_exponent + (127U - 15U)) << 23U) |
            (mantissa << 13U);
    }
    return std::bit_cast<float>(bits);
}

[[nodiscard]] inline auto make_water_vertex(
    float x,
    float y,
    float z,
    float u,
    float v,
    float nx,
    float ny,
    float nz,
    float face_shade,
    float ao,
    float sky_light,
    float block_light,
    float material_class,
    float wave_weight) noexcept -> WaterVertex {
    WaterVertex vertex {};
    vertex.x = x;
    vertex.y = y;
    vertex.z = z;
    vertex.u = u;
    vertex.v = v;
    vertex.nx = pack_water_snorm(nx);
    vertex.ny = pack_water_snorm(ny);
    vertex.nz = pack_water_snorm(nz);
    vertex.face_shade_half = pack_water_half(face_shade);
    vertex.ao = static_cast<std::uint8_t>(ao >= 0.5F ? 1U : 0U);
    vertex.sky_light = pack_water_unorm(sky_light);
    vertex.block_light = pack_water_unorm(block_light);
    vertex.material_class = static_cast<std::uint8_t>(
        std::clamp(
            std::lround(material_class),
            0L,
            static_cast<long>(std::numeric_limits<std::uint8_t>::max())));
    vertex.wave_weight =
        static_cast<std::uint8_t>(wave_weight >= 0.5F ? 1U : 0U);
    return vertex;
}

} // namespace valcraft
