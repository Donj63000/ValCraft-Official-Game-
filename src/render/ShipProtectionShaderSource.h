#pragma once

#include <string_view>

// Je conserve les declarations et les tests analytiques du navire dans un seul
// fragment GLSL afin que l'eau, l'humidite et la pluie utilisent exactement la
// meme enveloppe que celle transmise par le renderer.
#define VALCRAFT_SHIP_PROTECTION_GLSL_SOURCE R"VALCRAFT_GLSL(
uniform int u_ship_protection_enabled;
uniform mat4 u_ship_inverse_model;
uniform vec3 u_ship_bounds_min;
uniform vec3 u_ship_bounds_max;
uniform vec4 u_ship_profile_longitudinal;
uniform vec4 u_ship_profile_taper;
uniform vec4 u_ship_profile_heights;
uniform vec4 u_ship_profile_widths;
uniform float u_ship_sheltered_floor;

bool ship_world_bounds_contains(vec3 world_position) {
    return all(greaterThanEqual(world_position, u_ship_bounds_min)) &&
           all(lessThanEqual(world_position, u_ship_bounds_max));
}

float ship_half_width_at(float local_z) {
    float stern_z = u_ship_profile_longitudinal.x;
    float bow_z = u_ship_profile_longitudinal.y;
    float maximum_half_width = u_ship_profile_longitudinal.z;
    bool bow_side = local_z >= 0.0;
    float extent = bow_side ? bow_z : -stern_z;
    float width_loss = bow_side ? u_ship_profile_taper.y : u_ship_profile_taper.x;
    float exponent_value = bow_side ? u_ship_profile_taper.w : u_ship_profile_taper.z;
    float progression = clamp(abs(local_z) / max(extent, 0.0001), 0.0, 1.0);
    return max(
        maximum_half_width - width_loss,
        maximum_half_width - width_loss * pow(progression, max(exponent_value, 0.0001)));
}

bool ship_excludes_ocean(vec3 world_position) {
    if (u_ship_protection_enabled == 0 ||
        !ship_world_bounds_contains(world_position)) {
        return false;
    }

    vec3 local_position = (u_ship_inverse_model * vec4(world_position, 1.0)).xyz;
    float boundary_margin = u_ship_profile_longitudinal.w;
    if (local_position.z < u_ship_profile_longitudinal.x - boundary_margin ||
        local_position.z > u_ship_profile_longitudinal.y + boundary_margin ||
        local_position.y < u_ship_profile_heights.x ||
        local_position.y >= u_ship_profile_heights.w) {
        return false;
    }

    float half_width = ship_half_width_at(local_position.z);
    if (local_position.y < u_ship_profile_heights.y) {
        half_width = max(
            u_ship_profile_widths.z,
            half_width - u_ship_profile_widths.x);
    } else if (local_position.y < u_ship_profile_heights.z) {
        half_width = max(
            u_ship_profile_widths.w,
            half_width - u_ship_profile_widths.y);
    }
    return abs(local_position.x) <= half_width + boundary_margin;
}

bool ship_shelters_weather(vec3 world_position) {
    if (!ship_excludes_ocean(world_position)) {
        return false;
    }

    vec3 local_position = (u_ship_inverse_model * vec4(world_position, 1.0)).xyz;
    if (local_position.y < u_ship_sheltered_floor ||
        local_position.z < u_ship_profile_longitudinal.x ||
        local_position.z > u_ship_profile_longitudinal.y) {
        return false;
    }

    float sheltered_half_width = max(
        u_ship_profile_widths.w,
        ship_half_width_at(local_position.z) - u_ship_profile_widths.y);
    return abs(local_position.x) <=
           max(sheltered_half_width - u_ship_profile_longitudinal.w, 0.0);
}
)VALCRAFT_GLSL"

namespace valcraft {

inline constexpr std::string_view kShipProtectionGlslSource {
    VALCRAFT_SHIP_PROTECTION_GLSL_SOURCE,
};

} // namespace valcraft
