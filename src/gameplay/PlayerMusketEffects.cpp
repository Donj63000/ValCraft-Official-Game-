#include "gameplay/PlayerMusketEffects.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace valcraft {

namespace {

constexpr float kTwoPi = 6.28318530717958647692F;

[[nodiscard]] auto hash_u32(std::uint32_t value) noexcept -> std::uint32_t {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] auto random_unit(std::uint32_t seed) noexcept -> float {
    return static_cast<float>(hash_u32(seed) & 0x00FFFFFFU) /
           static_cast<float>(0x01000000U);
}

[[nodiscard]] auto random_signed(std::uint32_t seed) noexcept -> float {
    return random_unit(seed) * 2.0F - 1.0F;
}

[[nodiscard]] auto finite_vec3_or_zero(const glm::vec3& value) noexcept -> glm::vec3 {
    return {
        std::isfinite(value.x) ? value.x : 0.0F,
        std::isfinite(value.y) ? value.y : 0.0F,
        std::isfinite(value.z) ? value.z : 0.0F,
    };
}

} // namespace

void PlayerMusketEffects::clear() noexcept {
    flash_count_ = 0U;
    smoke_count_ = 0U;
}

void PlayerMusketEffects::clear_flashes() noexcept {
    flash_count_ = 0U;
}

void PlayerMusketEffects::spawn(
    const glm::vec3& muzzle_position,
    const glm::vec3& direction,
    const glm::vec3& inherited_velocity,
    const glm::vec3& wind_velocity,
    std::uint64_t sequence) noexcept {

    const auto position =
        finite_vec3_or_zero(muzzle_position);
    auto forward =
        finite_vec3_or_zero(direction);
    if (glm::dot(forward, forward) <= 1.0e-6F) {
        forward = {0.0F, 0.0F, -1.0F};
    } else {
        forward = glm::normalize(forward);
    }

    const auto base_seed =
        hash_u32(
            static_cast<std::uint32_t>(sequence) ^
            static_cast<std::uint32_t>(sequence >> 32U) ^
            0xA511E9B3U);
    OldGuardMuzzleFlashInstance flash {};
    flash.position = position;
    flash.direction = forward;
    flash.lifetime = 0.065F;
    flash.size = 0.36F;
    flash.intensity = 1.0F;
    flash.seed = base_seed;
    if (flash_count_ < flashes_.size()) {
        flashes_[flash_count_++] = flash;
    } else {
        flashes_[0] = flash;
    }

    const auto inherited =
        finite_vec3_or_zero(inherited_velocity);
    const auto wind =
        finite_vec3_or_zero(wind_velocity);
    constexpr std::size_t kSpawnCount = 18U;
    for (std::size_t index = 0U; index < kSpawnCount; ++index) {
        const auto seed =
            hash_u32(
                base_seed +
                static_cast<std::uint32_t>(index + 1U) *
                    0x85EBCA6BU);
        const auto lateral =
            glm::vec3 {
                random_signed(seed + 1U),
                random_signed(seed + 2U),
                random_signed(seed + 3U),
            };

        OldGuardSmokeInstance puff {};
        puff.position =
            position +
            lateral *
                (0.02F +
                 random_unit(seed + 4U) * 0.055F);
        puff.velocity =
            inherited +
            wind * 0.18F +
            forward *
                (0.35F +
                 random_unit(seed + 5U) * 0.62F) +
            lateral * 0.20F +
            glm::vec3 {0.0F, 0.18F, 0.0F};
        puff.lifetime =
            1.25F +
            random_unit(seed + 6U) * 0.55F;
        puff.size =
            0.11F +
            random_unit(seed + 7U) * 0.11F;
        puff.rotation_radians =
            random_unit(seed + 8U) * kTwoPi;
        puff.angular_velocity =
            random_signed(seed + 9U) * 1.6F;
        puff.opacity =
            0.70F +
            random_unit(seed + 10U) * 0.20F;
        puff.seed = seed;

        if (smoke_count_ < smoke_.size()) {
            smoke_[smoke_count_++] = puff;
        } else {
            const auto oldest =
                static_cast<std::size_t>(
                    std::max_element(
                        smoke_.begin(),
                        smoke_.end(),
                        [](const auto& first, const auto& second) {
                            return first.age / first.lifetime <
                                   second.age / second.lifetime;
                        }) -
                    smoke_.begin());
            smoke_[oldest] = puff;
        }
    }
}

void PlayerMusketEffects::update(
    float dt,
    const glm::vec3& wind_velocity) noexcept {

    const auto safe_dt =
        std::clamp(
            std::isfinite(dt) ? dt : 0.0F,
            0.0F,
            0.10F);
    const auto wind =
        finite_vec3_or_zero(wind_velocity);

    auto flash_write = std::size_t {0U};
    for (std::size_t index = 0U;
         index < flash_count_;
         ++index) {
        auto flash = flashes_[index];
        flash.age += safe_dt;
        if (flash.age < flash.lifetime) {
            flashes_[flash_write++] = flash;
        }
    }
    flash_count_ = flash_write;

    auto smoke_write = std::size_t {0U};
    for (std::size_t index = 0U;
         index < smoke_count_;
         ++index) {
        auto puff = smoke_[index];
        puff.age += safe_dt;
        if (puff.age >= puff.lifetime) {
            continue;
        }
        puff.velocity +=
            (wind * 0.20F +
             glm::vec3 {0.0F, 0.08F, 0.0F}) *
            safe_dt;
        puff.velocity *=
            std::exp(-0.75F * safe_dt);
        puff.position +=
            puff.velocity * safe_dt;
        puff.rotation_radians +=
            puff.angular_velocity * safe_dt;
        smoke_[smoke_write++] = puff;
    }
    smoke_count_ = smoke_write;
}

auto PlayerMusketEffects::flashes() const noexcept
    -> std::span<const OldGuardMuzzleFlashInstance> {
    return {
        flashes_.data(),
        flash_count_,
    };
}

auto PlayerMusketEffects::smoke() const noexcept
    -> std::span<const OldGuardSmokeInstance> {
    return {
        smoke_.data(),
        smoke_count_,
    };
}

} // namespace valcraft
