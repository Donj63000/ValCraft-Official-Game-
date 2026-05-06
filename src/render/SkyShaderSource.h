#pragma once

namespace valcraft {

inline constexpr auto* kSkyFragmentShaderSource = R"(#version 330 core
in vec2 v_uv;

uniform mat4 u_inverse_view_projection;
uniform vec3 u_sun_direction;
uniform float u_daylight_factor;
uniform float u_time_of_day;
uniform vec3 u_sky_zenith_color;
uniform vec3 u_sky_horizon_color;
uniform vec3 u_horizon_glow_color;
uniform vec3 u_sun_disk_color;
uniform vec3 u_moon_disk_color;
uniform float u_star_intensity;
uniform float u_cloud_intensity;
uniform float u_overcast_intensity;
uniform float u_precipitation_intensity;
uniform float u_storm_intensity;
uniform float u_lightning_intensity;
uniform float u_weather_time;
uniform sampler2D u_accent_atlas;

out vec4 frag_color;

float angular_falloff(float alignment, float radius, float softness) {
    const float minimum_softness = 0.0001;
    float inner = cos(radius);
    float outer = cos(radius + max(softness, minimum_softness));
    return smoothstep(outer, inner, alignment);
}

float hash13(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 191.9))) * 43758.5453123);
}

float value_noise3(vec3 p) {
    vec3 cell = floor(p);
    vec3 local = fract(p);
    vec3 blend = local * local * (3.0 - 2.0 * local);

    float n000 = hash13(cell + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(cell + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(cell + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(cell + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(cell + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(cell + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(cell + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(cell + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, blend.x);
    float nx10 = mix(n010, n110, blend.x);
    float nx01 = mix(n001, n101, blend.x);
    float nx11 = mix(n011, n111, blend.x);
    float nxy0 = mix(nx00, nx10, blend.y);
    float nxy1 = mix(nx01, nx11, blend.y);
    return mix(nxy0, nxy1, blend.z);
}

float fbm(vec3 p) {
    float value = 0.0;
    float amplitude = 0.55;
    for (int octave = 0; octave < 4; ++octave) {
        value += value_noise3(p) * amplitude;
        p = p * 2.03 + vec3(13.2, 7.1, 17.3);
        amplitude *= 0.52;
    }
    return value;
}

float cloud_density(vec3 sample_position, float cloud_intensity, float overcast_intensity, float storm_intensity) {
    float layer = smoothstep(0.22, 0.34, sample_position.y) * (1.0 - smoothstep(0.64, 0.82, sample_position.y));
    float base = fbm(sample_position * 0.82);
    float detail = fbm(sample_position * 1.56 + vec3(2.7, 5.1, 1.9));
    float erosion = fbm(sample_position * 2.60 + vec3(8.3, 1.4, 6.2));
    float weather_cover = clamp(max(cloud_intensity, overcast_intensity * 0.92), 0.0, 1.0);
    float coverage = mix(0.80, 0.50, weather_cover);
    float shape = base * 0.74 + detail * 0.24 - erosion * mix(0.18, 0.08, overcast_intensity);
    shape += overcast_intensity * 0.07 - storm_intensity * 0.02;
    return smoothstep(coverage, coverage + mix(0.16, 0.09, overcast_intensity), shape) * layer;
}

void main() {
    vec4 far_point = u_inverse_view_projection * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(far_point.xyz / max(far_point.w, 0.0001));
    vec3 sun_direction = normalize(u_sun_direction);
    float day_factor = clamp(u_daylight_factor, 0.0, 1.0);
    float night_factor = clamp(u_star_intensity, 0.0, 1.0);
    float cloud_factor = clamp(u_cloud_intensity, 0.0, 1.0);
    float overcast_factor = clamp(u_overcast_intensity, 0.0, 1.0);
    float precipitation_factor = clamp(u_precipitation_intensity, 0.0, 1.0);
    float storm_factor = clamp(u_storm_intensity, 0.0, 1.0);
    const float volumetric_cloud_minimum = 0.12;
    float disk_visibility = 1.0 - clamp(overcast_factor * 0.68 + precipitation_factor * 0.18 + storm_factor * 0.18, 0.0, 0.92);
    float zenith_mix = pow(smoothstep(-0.18, 0.82, direction.y), 0.78);
    float horizon_band = exp(-abs(direction.y) * 6.5);
    float sun_alignment = max(dot(direction, sun_direction), 0.0);
    float moon_alignment = max(dot(direction, -sun_direction), 0.0);

    vec3 color = mix(u_sky_horizon_color, u_sky_zenith_color, zenith_mix);
    vec3 haze_color = mix(u_sky_horizon_color, u_horizon_glow_color, 0.42 + 0.18 * (1.0 - day_factor));
    color = mix(color, haze_color, horizon_band * (0.16 + 0.18 * (1.0 - day_factor)));

    float sun_disc = angular_falloff(sun_alignment, 0.022, 0.020);
    float sun_core = angular_falloff(sun_alignment, 0.010, 0.014);
    float sun_halo = angular_falloff(sun_alignment, 0.060, 0.30);
    vec3 sun_halo_color = mix(u_horizon_glow_color, u_sun_disk_color, 0.58);
    color += sun_halo_color * pow(sun_halo, 1.55) * (0.05 + 0.13 * day_factor) * disk_visibility;
    color += u_sun_disk_color * sun_disc * (0.18 + 0.58 * day_factor) * disk_visibility;
    color += vec3(1.00, 0.98, 0.90) * sun_core * (0.08 + 0.18 * day_factor) * disk_visibility;

    float moon_disc = angular_falloff(moon_alignment, 0.020, 0.017);
    float moon_core = angular_falloff(moon_alignment, 0.010, 0.012);
    float moon_halo = angular_falloff(moon_alignment, 0.050, 0.22);
    vec3 moon_halo_color = mix(u_sky_zenith_color, u_moon_disk_color, 0.62);
    color += moon_halo_color * pow(moon_halo, 1.75) * (0.02 + 0.10 * night_factor) * disk_visibility;
    color += u_moon_disk_color * moon_disc * (0.05 + 0.55 * night_factor) * disk_visibility;
    color += vec3(1.0) * moon_core * (0.02 + 0.10 * night_factor) * disk_visibility;

    float overcast_band = smoothstep(-0.04, 0.16, direction.y) * (1.0 - smoothstep(0.90, 1.0, direction.y));
    if (overcast_band > 0.001 && overcast_factor > 0.01) {
        vec3 blanket_flow = direction * vec3(3.8, 2.2, 3.8) + vec3(u_weather_time * 0.010, 0.0, -u_weather_time * 0.007);
        float blanket_noise = fbm(blanket_flow) * 0.78 + fbm(blanket_flow * 1.82 + vec3(9.1, 4.7, 2.3)) * 0.22;
        float blanket_alpha = smoothstep(0.28, 0.72, blanket_noise + overcast_factor * 0.34) * overcast_band;
        blanket_alpha *= overcast_factor * (0.24 + 0.38 * day_factor + 0.20 * storm_factor);
        vec3 blanket_color = mix(vec3(0.62, 0.67, 0.72), vec3(0.24, 0.28, 0.36), storm_factor);
        blanket_color = mix(blanket_color, u_moon_disk_color * 0.40, night_factor * 0.22);
        color = mix(color, blanket_color, clamp(blanket_alpha, 0.0, 0.84));
    }

    float cloud_view_band = smoothstep(0.04, 0.18, direction.y) * (1.0 - smoothstep(0.66, 0.90, direction.y));
    // Je saute le raymarch des nuages quand le ciel est vraiment degage.
    if (cloud_view_band > 0.001 && max(cloud_factor, overcast_factor) > volumetric_cloud_minimum) {
        vec3 wind = vec3(cos(u_time_of_day * 0.21 + u_weather_time * 0.013), 0.0, sin(u_time_of_day * 0.17 + u_weather_time * 0.011)) *
                    (0.36 + storm_factor * 0.32);
        vec3 cloud_shadow_color = mix(u_sky_zenith_color, u_sky_horizon_color, 0.24);
        cloud_shadow_color = mix(cloud_shadow_color, vec3(0.18, 0.22, 0.30), overcast_factor * (0.28 + storm_factor * 0.48));
        vec3 cloud_light_color = mix(vec3(0.82, 0.87, 0.94), vec3(0.98, 0.99, 1.00), day_factor);
        cloud_light_color = mix(cloud_light_color, u_horizon_glow_color, 0.12 + 0.18 * horizon_band);
        cloud_light_color = mix(cloud_light_color, u_moon_disk_color, 0.12 * night_factor);
        cloud_light_color = mix(cloud_light_color, vec3(0.56, 0.61, 0.68), overcast_factor * 0.42);

        vec3 cloud_premul = vec3(0.0);
        float cloud_alpha = 0.0;
        for (int step = 0; step < 7; ++step) {
            float distance_along_ray = 1.35 + float(step) * 0.34;
            vec3 sample_position = direction * distance_along_ray * vec3(1.28, 0.88, 1.28) + wind;
            float density = cloud_density(sample_position, cloud_factor, overcast_factor, storm_factor) * cloud_view_band;
            if (density > 0.001) {
                float light_density =
                    cloud_density(sample_position + sun_direction * 0.28 + vec3(0.0, 0.05, 0.0), cloud_factor, overcast_factor, storm_factor);
                float self_light = clamp(0.42 + (light_density - density) * 2.6 + 0.22 * day_factor, 0.0, 1.0);
                float top_light = clamp(sample_position.y * 1.25 - 0.16, 0.0, 1.0);
                vec3 sample_color = mix(cloud_shadow_color, cloud_light_color, clamp(self_light * 0.72 + top_light * 0.28, 0.0, 1.0));
                float sample_alpha = density * (0.20 + 0.18 * day_factor + overcast_factor * 0.12) * (1.0 - cloud_alpha);
                cloud_premul += sample_color * sample_alpha;
                cloud_alpha += sample_alpha;
            }
        }

        float silver_lining = angular_falloff(sun_alignment, 0.11, 0.18) * cloud_alpha;
        cloud_premul += u_sun_disk_color * silver_lining * (0.03 + 0.04 * day_factor) * disk_visibility;
        color = color * (1.0 - cloud_alpha) + cloud_premul;
    }

    float twilight_softening = (1.0 - day_factor) * (1.0 - night_factor);
    vec3 twilight_wash = mix(u_sky_horizon_color, u_horizon_glow_color, 0.62);
    float twilight_wash_strength = horizon_band * twilight_softening * disk_visibility * (0.08 + 0.05 * (1.0 - overcast_factor));
    color = mix(color, twilight_wash, twilight_wash_strength);
    color += u_horizon_glow_color * horizon_band * twilight_softening * disk_visibility * 0.045;
    color += vec3(0.62, 0.72, 1.00) * clamp(u_lightning_intensity, 0.0, 1.0) * (0.12 + horizon_band * 0.22 + storm_factor * 0.18);

    frag_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

} // namespace valcraft
