#pragma once

#include "render/OceanVisualShaderSource.h"

#include <string>

namespace valcraft {

inline constexpr auto* kSkyVertexShaderSource = R"(#version 330 core
out vec2 v_uv;

void main() {
    vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 clip = positions[gl_VertexID];
    // Je place le ciel au fond du depth buffer pour ne colorer que les pixels encore vides.
    gl_Position = vec4(clip, 1.0, 1.0);
    v_uv = clip * 0.5 + 0.5;
}
)";

inline constexpr auto* kSkyFragmentShaderSourcePart1 = R"(#version 330 core
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
uniform float u_violent_storm_intensity;
uniform float u_lightning_intensity;
uniform float u_lightning_bolt_intensity;
uniform vec3 u_lightning_direction;
uniform float u_lightning_shape_seed;
uniform float u_weather_time;
uniform int u_cloud_steps;
uniform float u_cloud_detail;
uniform sampler2D u_accent_atlas;
uniform int u_maritime_horizon_enabled;
uniform vec3 u_maritime_camera_position;
uniform float u_maritime_sea_level;
uniform int u_maritime_submersion_active;
uniform vec4 u_ocean_horizon_waves[2];
uniform vec2 u_ocean_horizon_wave_phases[2];
uniform float u_ocean_horizon_severity;
uniform float u_ocean_horizon_tempest_factor;
uniform vec3 u_ocean_horizon_sun_color;
uniform vec2 u_maritime_far_fog_range;
uniform vec3 u_fog_color;
uniform vec3 u_distant_fog_color;

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

float lightning_path_noise(float segment, float salt) {
    return hash13(
        vec3(
            segment,
            u_lightning_shape_seed * 173.0 + salt,
            u_lightning_shape_seed * 317.0 - salt));
}

float lightning_bolt_mask(vec3 view_direction) {
    // Je conserve uniquement ce court-circuit uniforme : les dérivées de
    // fragment restent ainsi définies sur tout le quad OpenGL.
    if (u_lightning_bolt_intensity <= 0.001) {
        return 0.0;
    }

    vec2 bolt_horizontal = u_lightning_direction.xz;
    vec2 view_horizontal = view_direction.xz;
    float bolt_length = length(bolt_horizontal);
    float view_length = length(view_horizontal);
    bolt_horizontal /= max(bolt_length, 0.0001);
    view_horizontal /= max(view_length, 0.0001);
    float forward_alignment = dot(view_horizontal, bolt_horizontal);
    vec2 lateral_axis =
        vec2(-bolt_horizontal.y, bolt_horizontal.x);
    float lateral = dot(view_horizontal, lateral_axis);
    float antialias_width = max(fwidth(lateral), 0.00035);

    // Je fais varier le sommet dans la couche nuageuse avec l'élévation
    // déterministe calculée côté CPU, au lieu de jeter sa composante Y.
    float bolt_top =
        clamp(
            u_lightning_direction.y + 0.22,
            0.34,
            0.72);
    float vertical_window =
        smoothstep(0.015, 0.055, view_direction.y) *
        (1.0 -
         smoothstep(
             bolt_top - 0.055,
             bolt_top + 0.015,
             view_direction.y));
    float azimuth_window =
        smoothstep(0.94, 0.995, forward_alignment);
    float horizontal_validity =
        step(0.0001, bolt_length) *
        step(0.0001, view_length);
    float bolt_window =
        vertical_window *
        azimuth_window *
        horizontal_validity;

    // Je coupe les calculs de forme coûteux après fwidth : ce branchement peut
    // varier par fragment sans rendre la dérivée utilisée plus haut indéfinie.
    if (bolt_window <= 0.0) {
        return 0.0;
    }

    float height =
        clamp(
            (view_direction.y - 0.025) /
                max(bolt_top - 0.025, 0.10),
            0.0,
            1.0);
    float segment_position = height * 18.0;
    float segment = floor(segment_position);
    float segment_blend = smoothstep(0.0, 1.0, fract(segment_position));
    float path_a = lightning_path_noise(segment, 11.0) - 0.5;
    float path_b = lightning_path_noise(segment + 1.0, 11.0) - 0.5;
    float jagged_path =
        mix(path_a, path_b, segment_blend) *
        mix(0.030, 0.009, height);

    float distance_to_main = abs(lateral - jagged_path);
    float main_core =
        1.0 -
        smoothstep(
            antialias_width * 0.65 + 0.00045,
            antialias_width * 1.90 + 0.00180,
            distance_to_main);
    float main_glow =
        1.0 -
        smoothstep(
            0.0035,
            0.0170,
            distance_to_main);

    float fork_start =
        0.40 +
        lightning_path_noise(3.0, 29.0) *
            0.24;
    float fork_progress =
        clamp(
            (fork_start - height) /
                max(fork_start, 0.10),
            0.0,
            1.0);
    float fork_side =
        lightning_path_noise(7.0, 43.0) < 0.5
            ? -1.0
            : 1.0;
    float fork_jitter =
        (lightning_path_noise(segment, 59.0) - 0.5) *
        0.007;
    float fork_path =
        jagged_path +
        fork_side *
            fork_progress *
            0.040 +
        fork_jitter;
    float distance_to_fork =
        abs(lateral - fork_path);
    float fork_core =
        (1.0 -
         smoothstep(
             antialias_width * 0.75 + 0.00055,
             antialias_width * 2.10 + 0.00170,
             distance_to_fork)) *
        step(height, fork_start) *
        smoothstep(0.02, 0.18, fork_progress);

    return
        (main_core * 1.45 +
         main_glow * 0.30 +
         fork_core * 0.82) *
        bolt_window;
}

float cloud_density(vec3 sample_position, float cloud_intensity, float overcast_intensity, float storm_intensity) {
    float layer = smoothstep(0.22, 0.34, sample_position.y) * (1.0 - smoothstep(0.64, 0.82, sample_position.y));
    if (layer <= 0.0) {
        return 0.0;
    }
    float base = fbm(sample_position * 0.82);
    float detail = base;
    // Je garde les branches uniformes pour supprimer deux FBM complets sur les profils plus légers.
    if (u_cloud_detail > 0.01) {
        detail = fbm(sample_position * 1.56 + vec3(2.7, 5.1, 1.9));
    }
    float weather_cover = clamp(max(cloud_intensity, overcast_intensity * 0.92), 0.0, 1.0);
    float coverage = mix(0.80, 0.50, weather_cover);
    float detail_weight = 0.24 * clamp(u_cloud_detail, 0.0, 1.0);
    float base_weight = 0.98 - detail_weight;
    float erosion_weight = u_cloud_detail > 0.75 ? mix(0.18, 0.08, overcast_intensity) : 0.0;
    float weather_bias = overcast_intensity * 0.07 - storm_intensity * 0.02;
    float shape = base * base_weight + detail * detail_weight;
    // Je peux rejeter exactement cette densité avant le troisième FBM :
    // l'érosion suivante est toujours positive et uniquement soustractive.
    if (shape + weather_bias <= coverage) {
        return 0.0;
    }
    if (u_cloud_detail > 0.75) {
        float erosion = fbm(sample_position * 2.60 + vec3(8.3, 1.4, 6.2));
        shape -= erosion * erosion_weight;
    }
    shape += weather_bias;
    return smoothstep(coverage, coverage + mix(0.16, 0.09, overcast_intensity), shape) * layer;
}
)";

inline constexpr auto* kSkyFragmentShaderSourcePart2 = R"(
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
    float violent_storm_factor = clamp(u_violent_storm_intensity, 0.0, 1.0);
    const float volumetric_cloud_minimum = 0.12;
    float disk_visibility = 1.0 - clamp(
        overcast_factor * 0.68 +
        precipitation_factor * 0.18 +
        storm_factor * 0.18 +
        violent_storm_factor * 0.12,
        0.0,
        0.97);
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

    if (night_factor > 0.001 && direction.y > 0.02) {
        vec3 star_position = normalize(direction + vec3(0.0, 0.025, 0.0)) * 220.0;
        vec3 star_cell = floor(star_position);
        vec3 star_local = fract(star_position) - vec3(0.5);
        float star_seed = hash13(star_cell);
        float star_spawn = step(0.9962, star_seed);
        float star_core = 1.0 - smoothstep(0.014, 0.060, length(star_local.xy));
        float star_twinkle = 0.72 + 0.28 * sin(u_weather_time * (0.55 + star_seed * 1.35) + star_seed * 37.0);
        float star_altitude = smoothstep(0.05, 0.34, direction.y);
        float star_weather_visibility =
            1.0 - clamp(overcast_factor * 0.86 + precipitation_factor * 0.72 + storm_factor * 0.65 + cloud_factor * 0.18, 0.0, 0.96);
        vec3 star_color = mix(vec3(0.70, 0.82, 1.00), vec3(1.00, 0.92, 0.76), star_seed);
        color += star_color * star_spawn * star_core * star_twinkle * night_factor * star_altitude * star_weather_visibility * 0.42;
    }

    float overcast_band = smoothstep(-0.04, 0.16, direction.y) * (1.0 - smoothstep(0.90, 1.0, direction.y));
    if (overcast_band > 0.001 && overcast_factor > 0.01) {
        vec3 blanket_flow = direction * vec3(3.8, 2.2, 3.8) + vec3(u_weather_time * 0.010, 0.0, -u_weather_time * 0.007);
        float blanket_noise = fbm(blanket_flow) * 0.78 + fbm(blanket_flow * 1.82 + vec3(9.1, 4.7, 2.3)) * 0.22;
        float blanket_alpha = smoothstep(0.28, 0.72, blanket_noise + overcast_factor * 0.34) * overcast_band;
        blanket_alpha *= overcast_factor * (
            0.24 +
            0.38 * day_factor +
            0.20 * storm_factor +
            0.12 * violent_storm_factor);
        vec3 blanket_color = mix(vec3(0.62, 0.67, 0.72), vec3(0.24, 0.28, 0.36), storm_factor);
        blanket_color = mix(blanket_color, vec3(0.12, 0.14, 0.18), violent_storm_factor * 0.68);
        blanket_color = mix(blanket_color, u_moon_disk_color * 0.40, night_factor * 0.22);
        color = mix(color, blanket_color, clamp(blanket_alpha, 0.0, mix(0.84, 0.94, violent_storm_factor)));
    }

    float cloud_view_band = smoothstep(0.04, 0.18, direction.y) * (1.0 - smoothstep(0.66, 0.90, direction.y));
    // Je saute le raymarch des nuages quand le ciel est vraiment degage.
    if (cloud_view_band > 0.001 && max(cloud_factor, overcast_factor) > volumetric_cloud_minimum) {
        vec3 wind = vec3(cos(u_time_of_day * 0.21 + u_weather_time * 0.013), 0.0, sin(u_time_of_day * 0.17 + u_weather_time * 0.011)) *
                    (0.36 + storm_factor * 0.32);
        vec3 cloud_shadow_color = mix(u_sky_zenith_color, u_sky_horizon_color, 0.24);
        cloud_shadow_color = mix(cloud_shadow_color, vec3(0.18, 0.22, 0.30), overcast_factor * (0.28 + storm_factor * 0.48));
        cloud_shadow_color = mix(cloud_shadow_color, vec3(0.10, 0.12, 0.16), violent_storm_factor * 0.54);
        vec3 cloud_light_color = mix(vec3(0.82, 0.87, 0.94), vec3(0.98, 0.99, 1.00), day_factor);
        cloud_light_color = mix(cloud_light_color, u_horizon_glow_color, 0.12 + 0.18 * horizon_band);
        cloud_light_color = mix(cloud_light_color, u_moon_disk_color, 0.12 * night_factor);
        cloud_light_color = mix(cloud_light_color, vec3(0.56, 0.61, 0.68), overcast_factor * 0.42);

        vec3 cloud_premul = vec3(0.0);
        float cloud_alpha = 0.0;
        int cloud_steps = clamp(u_cloud_steps, 1, 7);
        float cloud_step_stride = cloud_steps > 1 ? 2.04 / float(cloud_steps - 1) : 1.02;
        float cloud_alpha_scale = 7.0 / float(cloud_steps);
        // Je fonds continûment l'éclairage directionnel avant de supprimer son
        // second échantillonnage volumétrique sous une couverture opaque : les
        // transitions météo restent continues et la tempête évite ce coût.
        float directional_cloud_light =
            smoothstep(0.12, 0.24, disk_visibility);
        bool sample_directional_cloud_light =
            u_cloud_detail > 0.01 &&
            directional_cloud_light > 0.001;
        for (int step = 0; step < 7; ++step) {
            if (step >= cloud_steps) {
                break;
            }
            float distance_along_ray = 1.35 + float(step) * cloud_step_stride;
            vec3 sample_position = direction * distance_along_ray * vec3(1.28, 0.88, 1.28) + wind;
            float density = cloud_density(sample_position, cloud_factor, overcast_factor, storm_factor) * cloud_view_band;
            if (density > 0.001) {
                float light_density = density;
                if (sample_directional_cloud_light) {
                    light_density = cloud_density(
                        sample_position + sun_direction * 0.28 + vec3(0.0, 0.05, 0.0),
                        cloud_factor,
                        overcast_factor,
                        storm_factor);
                }
                float self_light = clamp(
                    0.42 +
                    (light_density - density) *
                        2.6 *
                        directional_cloud_light +
                    0.22 * day_factor,
                    0.0,
                    1.0);
                float top_light = clamp(sample_position.y * 1.25 - 0.16, 0.0, 1.0);
                vec3 sample_color = mix(cloud_shadow_color, cloud_light_color, clamp(self_light * 0.72 + top_light * 0.28, 0.0, 1.0));
                float sample_alpha = density * (0.20 + 0.18 * day_factor + overcast_factor * 0.12) *
                                     cloud_alpha_scale * (1.0 - cloud_alpha);
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
    float bolt = lightning_bolt_mask(direction);
    color +=
        vec3(0.78, 0.86, 1.00) *
        clamp(u_lightning_bolt_intensity, 0.0, 1.0) *
        bolt *
        (0.78 + violent_storm_factor * 0.72);

    if (u_maritime_horizon_enabled != 0) {
        bool maritime_underwater_camera =
            u_maritime_submersion_active != 0;
        if (maritime_underwater_camera) {
            // Je remplace le ciel par un volume sous-marin continu. L'ancien
            // plan océanique analytique passait devant le fond et créait une
            // immense plaque cyan dès que la caméra descendait sous l'eau.
            float water_depth =
                max(
                    u_maritime_sea_level -
                        u_maritime_camera_position.y,
                    0.0);
            float upward_scatter =
                smoothstep(
                    -0.72,
                    0.78,
                    direction.y);
            vec3 deep_water =
                ocean_visual_atlantic_body_color(
                    0.0,
                    overcast_factor,
                    storm_factor,
                    violent_storm_factor) *
                0.78;
            vec3 shallow_water =
                mix(
                    deep_water,
                    ocean_visual_atlantic_body_color(
                        day_factor,
                        overcast_factor,
                        storm_factor,
                        violent_storm_factor),
                    0.72);
            vec3 underwater_color =
                mix(
                    deep_water,
                    shallow_water,
                    upward_scatter *
                        exp(-water_depth * 0.045));
            underwater_color =
                mix(
                    underwater_color,
                    deep_water,
                    storm_factor * 0.30);
            color =
                mix(
                    color,
                    underwater_color,
                    0.985);
        } else {
        // Je prolonge l'océan dans le ciel lui-même : les chunks d'eau réels
        // le recouvrent par la profondeur, tandis qu'aucune géométrie lointaine
        // ne consomme de sommets, de draw call ou de fill-rate supplémentaire.
        float ocean_visibility =
            1.0 -
            smoothstep(
                -0.002,
                0.003,
                direction.y);
        if (ocean_visibility > 0.0001) {
            float eye_height =
                max(
                    u_maritime_camera_position.y -
                        u_maritime_sea_level,
                    0.35);
            float plane_distance =
                min(
                    eye_height /
                        max(
                            -direction.y,
                            0.001),
                    4096.0);
            vec2 ocean_position =
                u_maritime_camera_position.xz +
                direction.xz *
                    plane_distance;
            vec3 ocean_normal =
                ocean_visual_far_wave_normal(
                    ocean_position,
                    u_ocean_horizon_waves[0],
                    u_ocean_horizon_wave_phases[0],
                    u_ocean_horizon_waves[1],
                    u_ocean_horizon_wave_phases[1]);
            vec3 view_direction =
                normalize(
                    -direction);
            vec3 reflection_direction =
                reflect(
                    direction,
                    ocean_normal);
            float horizon_overcast =
                clamp(
                    max(
                        cloud_factor * 0.82,
                        overcast_factor),
                    0.0,
                    1.0);
            float horizon_storm =
                clamp(
                    max(
                        storm_factor,
                        u_ocean_horizon_severity),
                    0.0,
                    1.0);
            float horizon_tempest =
                clamp(
                    u_ocean_horizon_tempest_factor,
                    0.0,
                    1.0);
            vec3 ocean_body =
                ocean_visual_atlantic_body_color(
                    day_factor,
                    horizon_overcast,
                    horizon_storm,
                    horizon_tempest);
            vec3 reflected_ocean_sky =
                ocean_visual_reflected_sky(
                    reflection_direction,
                    sun_direction,
                    u_sky_zenith_color,
                    u_sky_horizon_color,
                    u_ocean_horizon_sun_color,
                    u_moon_disk_color,
                    day_factor,
                    1.0 - day_factor,
                    horizon_overcast,
                    horizon_storm,
                    horizon_tempest,
                    u_lightning_intensity);
            vec3 ocean_color =
                ocean_visual_surface_radiance(
                    ocean_body,
                    reflected_ocean_sky,
                    ocean_normal,
                    view_direction);

            float weather_fog =
                1.0 +
                precipitation_factor *
                    0.42 +
                storm_factor *
                    0.38;
            float atmospheric_fog =
                1.0 -
                exp(
                    -plane_distance *
                    plane_distance *
                    0.000008 *
                    weather_fog);
            float terminal_fog =
                smoothstep(
                    u_maritime_far_fog_range.x,
                    max(
                        u_maritime_far_fog_range.y,
                        u_maritime_far_fog_range.x +
                            0.001),
                    plane_distance);
            float ocean_fog =
                clamp(
                    max(
                        atmospheric_fog,
                        terminal_fog),
                    0.0,
                    1.0);
            vec3 maritime_haze =
                mix(
                    u_fog_color,
                    u_distant_fog_color,
                    sqrt(
                        ocean_fog));
            ocean_color =
                mix(
                    ocean_color,
                    maritime_haze,
                    ocean_fog);

            // Je termine exactement sur la couleur du brouillard lointain :
            // un chunk qui arrive entre deux images reste imperceptible.
            ocean_color =
                mix(
                    ocean_color,
                    u_distant_fog_color,
                terminal_fog);
            color =
                mix(
                    color,
                    ocean_color,
                    ocean_visibility);
        }
        }
    }

    frag_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

inline const std::string kSkyFragmentShaderSource = [] {
    std::string source;
    source.reserve(
        std::char_traits<char>::length(
            kSkyFragmentShaderSourcePart1) +
        kOceanVisualShaderSource.size() +
        std::char_traits<char>::length(
            kSkyFragmentShaderSourcePart2));
    source += kSkyFragmentShaderSourcePart1;
    source.append(
        kOceanVisualShaderSource.data(),
        kOceanVisualShaderSource.size());
    source += kSkyFragmentShaderSourcePart2;
    return source;
}();

} // namespace valcraft
