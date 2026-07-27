#pragma once

#include "gameplay/OldGuard.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace valcraft {

inline constexpr std::size_t kPlayerMusketFlashCapacity = 2U;
inline constexpr std::size_t kPlayerMusketSmokeCapacity = 32U;

class PlayerMusketEffects {
public:
    void clear() noexcept;
    void clear_flashes() noexcept;
    void spawn(const glm::vec3& muzzle_position,
               const glm::vec3& direction,
               const glm::vec3& inherited_velocity,
               const glm::vec3& wind_velocity,
               std::uint64_t sequence) noexcept;
    void update(float dt, const glm::vec3& wind_velocity) noexcept;

    [[nodiscard]] auto flashes() const noexcept
        -> std::span<const OldGuardMuzzleFlashInstance>;
    [[nodiscard]] auto smoke() const noexcept
        -> std::span<const OldGuardSmokeInstance>;

private:
    std::array<OldGuardMuzzleFlashInstance, kPlayerMusketFlashCapacity> flashes_ {};
    std::array<OldGuardSmokeInstance, kPlayerMusketSmokeCapacity> smoke_ {};
    std::size_t flash_count_ = 0U;
    std::size_t smoke_count_ = 0U;
};

} // namespace valcraft
