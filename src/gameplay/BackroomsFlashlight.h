#pragma once

#include <cstdint>

namespace valcraft {

// Je garde toute la logique de batterie indépendante du rendu et de SDL afin
// de pouvoir la vérifier précisément, y compris aux limites de décharge.
constexpr float kBackroomsFlashlightDrainSeconds = 90.0F;
constexpr float kBackroomsFlashlightRechargeSeconds = 240.0F;
constexpr float kBackroomsFlashlightMinimumActivationCharge = 0.02F;

struct BackroomsFlashlightState {
    float battery_charge = 1.0F;
    bool enabled = false;

    auto operator==(const BackroomsFlashlightState&) const -> bool = default;
};

struct BackroomsFlashlightHudView {
    float battery_ratio = 1.0F;
    float beam_intensity = 0.0F;
    bool visible = false;
    bool active = false;
};

enum class BackroomsFlashlightToggleResult : std::uint8_t {
    Activated,
    Deactivated,
    BatteryTooLow,
};

struct BackroomsFlashlightUpdateResult {
    bool battery_changed = false;
    bool depleted = false;
};

[[nodiscard]] auto sanitize_backrooms_flashlight_state(
    const BackroomsFlashlightState& state) noexcept
    -> BackroomsFlashlightState;

[[nodiscard]] auto toggle_backrooms_flashlight(
    BackroomsFlashlightState& state) noexcept
    -> BackroomsFlashlightToggleResult;

[[nodiscard]] auto update_backrooms_flashlight(
    BackroomsFlashlightState& state,
    float dt) noexcept
    -> BackroomsFlashlightUpdateResult;

[[nodiscard]] auto backrooms_flashlight_intensity(
    const BackroomsFlashlightState& state) noexcept -> float;

[[nodiscard]] auto make_backrooms_flashlight_hud_view(
    const BackroomsFlashlightState& state,
    bool visible) noexcept -> BackroomsFlashlightHudView;

} // namespace valcraft
