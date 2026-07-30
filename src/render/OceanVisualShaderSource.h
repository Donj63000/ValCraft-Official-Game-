#pragma once

#include <string_view>

namespace valcraft {

// Je centralise ici les briques optiques communes à l'eau moderne et à
// l'océan analytique. Le fragment inséré ne déclare ni uniforme ni ressource :
// chaque programme reste libre de fournir son environnement sans nouvelle
// texture, passe ou version GLSL.
inline constexpr std::string_view kOceanVisualShaderSource = R"(
const float k_ocean_visual_water_f0 = 0.020;
const vec3 k_ocean_visual_absorption =
    vec3(0.120, 0.055, 0.025);

vec3 ocean_visual_safe_normalize(
    vec3 value,
    vec3 fallback) {

    float length_squared =
        dot(
            value,
            value);
    if (length_squared > 1.0e-8 &&
        length_squared < 1.0e20) {
        return
            value *
            inversesqrt(
                length_squared);
    }

    return fallback;
}

float ocean_visual_fresnel_schlick(float normal_to_view) {
    float grazing =
        1.0 -
        clamp(
            normal_to_view,
            0.0,
            1.0);
    float grazing_squared =
        grazing *
        grazing;
    float grazing_fifth =
        grazing_squared *
        grazing_squared *
        grazing;

    return
        k_ocean_visual_water_f0 +
        (1.0 - k_ocean_visual_water_f0) *
            grazing_fifth;
}

vec3 ocean_visual_atlantic_body_color(
    float daylight_factor,
    float overcast_factor,
    float storm_factor,
    float violent_storm_factor) {

    float daylight =
        clamp(
            daylight_factor,
            0.0,
            1.0);
    float overcast =
        clamp(
            overcast_factor,
            0.0,
            1.0);
    float storm =
        clamp(
            storm_factor,
            0.0,
            1.0);
    float violent_storm =
        clamp(
            violent_storm_factor,
            0.0,
            1.0);

    vec3 night_atlantic =
        vec3(0.008, 0.035, 0.060);
    vec3 day_atlantic =
        vec3(0.018, 0.105, 0.155);
    vec3 body_color =
        mix(
            night_atlantic,
            day_atlantic,
            daylight);

    vec3 overcast_atlantic =
        mix(
            vec3(0.014, 0.050, 0.070),
            vec3(0.030, 0.095, 0.115),
            daylight);
    body_color =
        mix(
            body_color,
            overcast_atlantic,
            overcast * 0.52);
    body_color =
        mix(
            body_color,
            vec3(0.018, 0.045, 0.060),
            storm * 0.70);
    body_color =
        mix(
            body_color,
            vec3(0.010, 0.026, 0.038),
            violent_storm * 0.78);

    return
        max(
            body_color,
            vec3(0.0));
}

vec3 ocean_visual_atlantic_shallow_color(
    float daylight_factor,
    float overcast_factor,
    float storm_factor,
    float violent_storm_factor) {

    float daylight =
        clamp(
            daylight_factor,
            0.0,
            1.0);
    float overcast =
        clamp(
            overcast_factor,
            0.0,
            1.0);
    float storm =
        clamp(
            storm_factor,
            0.0,
            1.0);
    float violent_storm =
        clamp(
            violent_storm_factor,
            0.0,
            1.0);
    vec3 shallow_color =
        mix(
            vec3(0.018, 0.085, 0.105),
            vec3(0.035, 0.240, 0.285),
            daylight);
    vec3 covered_shallow =
        mix(
            vec3(0.016, 0.065, 0.080),
            vec3(0.040, 0.165, 0.185),
            daylight);
    shallow_color =
        mix(
            shallow_color,
            covered_shallow,
            overcast * 0.52);
    shallow_color =
        mix(
            shallow_color,
            vec3(0.024, 0.075, 0.090),
            storm * 0.66);
    shallow_color =
        mix(
            shallow_color,
            vec3(0.014, 0.040, 0.052),
            violent_storm * 0.76);

    return
        max(
            shallow_color,
            vec3(0.0));
}

vec3 ocean_visual_reflected_sky(
    vec3 reflection_direction,
    vec3 sun_direction,
    vec3 zenith_color,
    vec3 horizon_color,
    vec3 sun_color,
    vec3 moon_color,
    float daylight_factor,
    float night_factor,
    float overcast_factor,
    float storm_factor,
    float violent_storm_factor,
    float lightning_intensity) {

    vec3 reflection =
        ocean_visual_safe_normalize(
            reflection_direction,
            vec3(0.0, 1.0, 0.0));
    vec3 safe_sun_direction =
        ocean_visual_safe_normalize(
            sun_direction,
            vec3(0.0, 1.0, 0.0));
    float daylight =
        clamp(
            daylight_factor,
            0.0,
            1.0);
    float night =
        clamp(
            night_factor,
            0.0,
            1.0);
    float overcast =
        clamp(
            overcast_factor,
            0.0,
            1.0);
    float storm =
        clamp(
            storm_factor,
            0.0,
            1.0);
    float violent_storm =
        clamp(
            violent_storm_factor,
            0.0,
            1.0);

    float sky_height =
        pow(
            smoothstep(
                -0.08,
                0.92,
                reflection.y),
            0.72);
    vec3 reflected_sky =
        mix(
            horizon_color,
            zenith_color,
            sky_height);

    float weather_cover =
        clamp(
            overcast * 0.72 +
            storm * 0.24 +
            violent_storm * 0.20,
            0.0,
            0.96);
    vec3 covered_sky =
        mix(
            vec3(0.060, 0.085, 0.115),
            vec3(0.270, 0.315, 0.350),
            daylight);
    reflected_sky =
        mix(
            reflected_sky,
            covered_sky,
            weather_cover);

    float roughness =
        mix(
            0.055,
            0.220,
            clamp(
                max(
                    storm,
                    violent_storm),
                0.0,
                1.0));
    float sun_exponent =
        mix(
            720.0,
            70.0,
            smoothstep(
                0.055,
                0.220,
                roughness));
    float moon_exponent =
        sun_exponent *
        0.72;
    float disk_visibility =
        1.0 -
        weather_cover;
    float sun_glint =
        pow(
            max(
                dot(
                    reflection,
                    safe_sun_direction),
                0.0),
            sun_exponent);
    float moon_glint =
        pow(
            max(
                dot(
                    reflection,
                    -safe_sun_direction),
                0.0),
            moon_exponent);

    reflected_sky +=
        sun_color *
        sun_glint *
        daylight *
        disk_visibility *
        mix(
            1.10,
            0.62,
            storm);
    reflected_sky +=
        moon_color *
        moon_glint *
        night *
        disk_visibility *
        0.62;
    reflected_sky +=
        vec3(0.62, 0.74, 1.00) *
        clamp(
            lightning_intensity,
            0.0,
            1.0) *
        (0.16 +
         0.20 *
             (1.0 - reflection.y)) *
        (0.65 +
         violent_storm * 0.35);

    return
        max(
            reflected_sky,
            vec3(0.0));
}

vec3 ocean_visual_far_wave_normal(
    vec2 world_position,
    vec4 wave_a,
    vec2 phase_data_a,
    vec4 wave_b,
    vec2 phase_data_b) {

    float phase_a =
        dot(
            wave_a.xy,
            world_position) *
            wave_a.z +
        phase_data_a.x;
    float phase_b =
        dot(
            wave_b.xy,
            world_position) *
            wave_b.z +
        phase_data_b.x;
    float harmonic_a =
        0.14 *
        clamp(
            phase_data_a.y,
            0.0,
            1.0);
    float harmonic_b =
        0.14 *
        clamp(
            phase_data_b.y,
            0.0,
            1.0);

    vec2 gradient =
        wave_a.xy *
            wave_a.w *
            wave_a.z *
            (cos(phase_a) +
             2.0 * harmonic_a *
                 cos(2.0 * phase_a)) +
        wave_b.xy *
            wave_b.w *
            wave_b.z *
            (cos(phase_b) +
             2.0 * harmonic_b *
                 cos(2.0 * phase_b));

    return
        ocean_visual_safe_normalize(
            vec3(
                -gradient.x,
                1.0,
                -gradient.y),
            vec3(0.0, 1.0, 0.0));
}

vec3 ocean_visual_surface_radiance(
    vec3 body_color,
    vec3 reflected_sky,
    vec3 surface_normal,
    vec3 view_direction) {

    float fresnel =
        ocean_visual_fresnel_schlick(
            dot(
                ocean_visual_safe_normalize(
                    surface_normal,
                    vec3(0.0, 1.0, 0.0)),
                ocean_visual_safe_normalize(
                    view_direction,
                    vec3(0.0, 1.0, 0.0))));
    float reflected_energy =
        clamp(
            0.025 +
            fresnel * 0.955,
            0.025,
            0.980);

    return
        mix(
            max(
                body_color,
                vec3(0.0)),
            max(
                reflected_sky,
                vec3(0.0)),
            reflected_energy);
}
)";

} // namespace valcraft
