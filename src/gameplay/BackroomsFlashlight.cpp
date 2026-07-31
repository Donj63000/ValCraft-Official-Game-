#include "gameplay/BackroomsFlashlight.h"

#include <algorithm>
#include <cmath>

namespace valcraft {

auto sanitize_backrooms_flashlight_state(
    const BackroomsFlashlightState& state) noexcept
    -> BackroomsFlashlightState {
    BackroomsFlashlightState sanitized = state;
    if (!std::isfinite(sanitized.battery_charge)) {
        sanitized.battery_charge = 0.0F;
    }
    sanitized.battery_charge =
        std::clamp(sanitized.battery_charge, 0.0F, 1.0F);
    if (sanitized.battery_charge <= 0.0F) {
        sanitized.enabled = false;
    }
    return sanitized;
}

auto toggle_backrooms_flashlight(
    BackroomsFlashlightState& state) noexcept
    -> BackroomsFlashlightToggleResult {
    state = sanitize_backrooms_flashlight_state(state);
    if (state.enabled) {
        state.enabled = false;
        return BackroomsFlashlightToggleResult::Deactivated;
    }
    if (state.battery_charge <
        kBackroomsFlashlightMinimumActivationCharge) {
        return BackroomsFlashlightToggleResult::BatteryTooLow;
    }
    state.enabled = true;
    return BackroomsFlashlightToggleResult::Activated;
}

auto update_backrooms_flashlight(
    BackroomsFlashlightState& state,
    float dt) noexcept
    -> BackroomsFlashlightUpdateResult {
    const auto previous = sanitize_backrooms_flashlight_state(state);
    state = previous;
    if (!std::isfinite(dt) || dt <= 0.0F) {
        return {};
    }

    auto depleted = false;
    if (state.enabled) {
        const auto seconds_until_empty =
            state.battery_charge * kBackroomsFlashlightDrainSeconds;
        if (dt >= seconds_until_empty) {
            // Je coupe exactement au passage à zéro. Le temps restant peut
            // ensuite commencer la recharge comme il le ferait sur les frames
            // normales suivant l'extinction automatique.
            const auto recharge_seconds = dt - seconds_until_empty;
            state.battery_charge = std::clamp(
                recharge_seconds /
                    kBackroomsFlashlightRechargeSeconds,
                0.0F,
                1.0F);
            state.enabled = false;
            depleted = true;
        } else {
            state.battery_charge -=
                dt / kBackroomsFlashlightDrainSeconds;
        }
    } else {
        state.battery_charge = std::clamp(
            state.battery_charge +
                dt / kBackroomsFlashlightRechargeSeconds,
            0.0F,
            1.0F);
    }

    state = sanitize_backrooms_flashlight_state(state);
    return {
        state.battery_charge != previous.battery_charge ||
            state.enabled != previous.enabled,
        depleted,
    };
}

auto backrooms_flashlight_intensity(
    const BackroomsFlashlightState& state) noexcept -> float {
    const auto sanitized =
        sanitize_backrooms_flashlight_state(state);
    if (!sanitized.enabled) {
        return 0.0F;
    }

    // Je conserve une lumière utilisable jusqu'à la fin, avec une baisse
    // progressive seulement sur les derniers pourcents de batterie.
    const auto low_battery_ratio =
        std::clamp(sanitized.battery_charge / 0.12F, 0.0F, 1.0F);
    const auto smooth_ratio =
        low_battery_ratio * low_battery_ratio *
        (3.0F - 2.0F * low_battery_ratio);
    return 0.48F + smooth_ratio * 0.52F;
}

auto make_backrooms_flashlight_hud_view(
    const BackroomsFlashlightState& state,
    bool visible) noexcept -> BackroomsFlashlightHudView {
    const auto sanitized =
        sanitize_backrooms_flashlight_state(state);
    return {
        sanitized.battery_charge,
        backrooms_flashlight_intensity(
            sanitized),
        visible,
        sanitized.enabled,
    };
}

} // namespace valcraft
