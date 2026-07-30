#pragma once

#include "render/OceanVisualShaderSource.h"
#include "render/ShipProtectionShaderSource.h"

#include <string>
#include <string_view>

namespace valcraft {

inline constexpr std::string_view kModernWaterVertexShaderSource = R"VALCRAFT_GLSL(
#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec3 a_normal;
layout(location = 5) in float a_sky_light;
layout(location = 6) in float a_block_light;
layout(location = 8) in float a_wave_weight;

uniform mat4 u_model;
uniform mat4 u_view_projection;
uniform vec3 u_camera_position;
uniform vec4 u_ocean_waves[6];
uniform vec2 u_ocean_wave_phases[6];
uniform int u_ocean_wave_count;

out vec3 v_normal;
out float v_sky_light;
out float v_block_light;
out float v_wave_weight;
out vec3 v_world_position;
out vec3 v_ocean_normal;
out float v_ocean_crest;
out float v_distance;

void sample_ocean(
    vec2 world_xz,
    out float height,
    out vec2 gradient,
    out float crest
) {
    height = 0.0;
    gradient = vec2(0.0);
    crest = 0.0;

    for (int index = 0; index < 6; ++index) {
        if (index >= u_ocean_wave_count) {
            break;
        }

        vec4 geometry = u_ocean_waves[index];
        vec2 phase_data = u_ocean_wave_phases[index];
        vec2 direction = geometry.xy;
        float wave_number = geometry.z;
        float amplitude = geometry.w;
        float theta =
            dot(direction, world_xz) *
                wave_number +
            phase_data.x;
        float harmonic =
            0.14 *
            clamp(phase_data.y, 0.0, 1.0);
        float sine = sin(theta);
        float cosine = cos(theta);
        float double_sine = sin(theta * 2.0);
        float double_cosine = cos(theta * 2.0);

        height +=
            amplitude *
            (sine + harmonic * double_sine);
        float derivative =
            amplitude *
            wave_number *
            (cosine + 2.0 * harmonic * double_cosine);
        gradient += direction * derivative;

        float normalized_wave_height =
            clamp(
                0.5 +
                    0.5 *
                        (sine + harmonic * double_sine) /
                        (1.0 + harmonic),
                0.0,
                1.0);
        crest = max(
            crest,
            normalized_wave_height *
                normalized_wave_height *
                normalized_wave_height);
    }
}

void main() {
    vec4 world_position =
        u_model * vec4(a_position, 1.0);
    float wave_weight =
        clamp(a_wave_weight, 0.0, 1.0);
    vec2 ocean_gradient = vec2(0.0);
    float ocean_crest = 0.0;
    if (wave_weight > 0.0) {
        float ocean_height = 0.0;
        sample_ocean(
            world_position.xz,
            ocean_height,
            ocean_gradient,
            ocean_crest);
        // Je conserve exactement le déplacement vertical historique afin que
        // le rendu, la nage et la flottabilité restent synchronisés.
        world_position.y +=
            ocean_height * wave_weight;
    }

    v_normal = normalize(mat3(u_model) * a_normal);
    v_ocean_normal =
        normalize(
            vec3(
                -ocean_gradient.x,
                1.0,
                -ocean_gradient.y));
    v_ocean_crest = ocean_crest;
    v_sky_light = a_sky_light;
    v_block_light = a_block_light;
    v_wave_weight = wave_weight;
    v_world_position = world_position.xyz;
    v_distance =
        distance(
            world_position.xyz,
            u_camera_position);
    gl_Position =
        u_view_projection * world_position;
}
)VALCRAFT_GLSL";

inline auto modern_water_fragment_shader_source() -> const std::string& {
    static const std::string source =
        std::string {R"VALCRAFT_GLSL(
#version 330 core
in vec3 v_normal;
in float v_sky_light;
in float v_block_light;
in float v_wave_weight;
in vec3 v_world_position;
in vec3 v_ocean_normal;
in float v_ocean_crest;
in float v_distance;

uniform sampler2D u_scene_color;
uniform sampler2D u_scene_depth;
uniform sampler2DArray u_material_normal_height;
uniform mat4 u_inverse_view_projection;
uniform vec3 u_camera_position;
uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec3 u_moon_disk_color;
uniform vec3 u_ambient_color;
uniform vec3 u_fog_color;
uniform vec3 u_distant_fog_color;
uniform vec3 u_horizon_glow_color;
uniform vec3 u_night_tint_color;
uniform vec3 u_sky_zenith_color;
uniform vec3 u_sky_horizon_color;
uniform float u_daylight_factor;
uniform float u_sun_visibility;
uniform float u_cloud_intensity;
uniform float u_overcast_intensity;
uniform float u_precipitation_intensity;
uniform float u_storm_intensity;
uniform float u_lightning_intensity;
uniform float u_ocean_foam_threshold;
uniform float u_ocean_detail_strength;
uniform float u_ocean_detail_phase;
uniform float u_water_animation_time;
uniform vec4 u_ocean_waves[6];
uniform vec2 u_ocean_wave_phases[6];
uniform float u_ocean_severity;
uniform float u_ocean_tempest_factor;
uniform float u_ocean_open_sea;
uniform float u_water_surface_detail;
uniform int u_water_detail_samples;
uniform int u_has_water_material;
uniform float u_water_normal_layer;
uniform float u_ship_speed;
uniform int u_maritime_horizon_enabled;
uniform vec2 u_maritime_water_blend_range;
uniform vec2 u_maritime_far_fog_range;
uniform float u_maritime_sea_level;

out vec4 frag_color;
)VALCRAFT_GLSL"} +
        VALCRAFT_SHIP_PROTECTION_GLSL_SOURCE +
        std::string {kOceanVisualShaderSource} +
        R"VALCRAFT_GLSL(

float hash12(vec2 position) {
    return fract(
        sin(
            dot(
                position,
                vec2(127.1, 311.7))) *
        43758.5453123);
}

float value_noise2(vec2 position) {
    vec2 cell = floor(position);
    vec2 local = fract(position);
    vec2 curve =
        local * local * (3.0 - 2.0 * local);
    float southwest = hash12(cell);
    float southeast =
        hash12(cell + vec2(1.0, 0.0));
    float northwest =
        hash12(cell + vec2(0.0, 1.0));
    float northeast =
        hash12(cell + vec2(1.0, 1.0));
    return mix(
        mix(southwest, southeast, curve.x),
        mix(northwest, northeast, curve.x),
        curve.y);
}

vec3 reconstruct_world_position(
    vec2 screen_uv,
    float depth_sample
) {
    vec4 clip_position =
        vec4(
            screen_uv * 2.0 - 1.0,
            depth_sample * 2.0 - 1.0,
            1.0);
    vec4 world_position =
        u_inverse_view_projection * clip_position;
    return
        world_position.xyz /
        max(world_position.w, 0.0001);
}

vec2 analytic_detail_gradient(
    vec2 world_xz
) {
    float phase_a =
        world_xz.x * 1.08 -
        world_xz.y * 0.74 +
        u_ocean_detail_phase * 2.0;
    float phase_b =
        world_xz.x * 0.72 +
        world_xz.y * 1.16 -
        u_ocean_detail_phase * 3.0;
    return
        vec2(
            cos(phase_a) * 1.08 +
                cos(phase_b) * 0.4896,
            -cos(phase_a) * 0.74 +
                cos(phase_b) * 0.7888) *
        max(u_ocean_detail_strength, 0.0);
}

vec2 material_detail_gradient(
    vec2 world_xz
) {
    if (u_has_water_material == 0 ||
        u_water_detail_samples <= 0) {
        return vec2(0.0);
    }

    float time_phase =
        u_water_animation_time;
    vec2 first_uv =
        world_xz * 0.055 +
        vec2(time_phase * 0.018, -time_phase * 0.013);
    vec3 first_normal =
        texture(
            u_material_normal_height,
            vec3(
                first_uv,
                u_water_normal_layer))
            .xyz *
            2.0 -
        1.0;
    vec2 gradient =
        first_normal.xy /
        max(first_normal.z, 0.22);

    if (u_water_detail_samples > 1) {
        mat2 rotation =
            mat2(
                0.6157, -0.7880,
                0.7880, 0.6157);
        vec2 second_uv =
            rotation * world_xz * 0.083 +
            vec2(-time_phase * 0.011, time_phase * 0.016);
        vec3 second_normal =
            texture(
                u_material_normal_height,
                vec3(
                    second_uv,
                    u_water_normal_layer))
                .xyz *
                2.0 -
            1.0;
        vec2 second_gradient =
            second_normal.xy /
            max(second_normal.z, 0.22);
        gradient =
            gradient * 0.58 +
            rotation * second_gradient * 0.42;
    }
    return gradient * 0.19;
}

vec2 rain_dimple_gradient(
    vec2 world_xz
) {
    if (u_water_detail_samples <= 0) {
        return vec2(0.0);
    }
    float rain =
        clamp(
            u_precipitation_intensity,
            0.0,
            1.0) *
        clamp(
            u_water_surface_detail,
            0.0,
            1.0);
    if (rain <= 0.001) {
        return vec2(0.0);
    }
    vec2 cell_position =
        world_xz * 1.85;
    vec2 cell = floor(cell_position);
    vec2 local =
        fract(cell_position) - vec2(0.5);
    float random_phase =
        hash12(cell + vec2(43.0, 17.0));
    float age =
        fract(
            u_water_animation_time * 0.42 +
            random_phase);
    float radius = length(local);
    float front = age * 0.58;
    float ring =
        exp(
            -pow(
                (radius - front) * 22.0,
                2.0));
    float pulse =
        cos((radius - front) * 46.0) *
        (1.0 - age);
    return
        local /
        max(radius, 0.035) *
        ring *
        pulse *
        rain *
        0.085;
}

vec3 atlantic_volume_color(
    float water_depth
) {
    float visual_overcast =
        clamp(
            max(
                u_cloud_intensity * 0.82,
                u_overcast_intensity),
            0.0,
            1.0);
    float visual_storm =
        clamp(
            max(
                u_storm_intensity,
                u_ocean_severity),
            0.0,
            1.0);
    float visual_tempest =
        clamp(
            u_ocean_tempest_factor,
            0.0,
            1.0);
    vec3 shallow_color =
        ocean_visual_atlantic_shallow_color(
            u_daylight_factor,
            visual_overcast,
            visual_storm,
            visual_tempest);
    vec3 deep_color =
        ocean_visual_atlantic_body_color(
            u_daylight_factor,
            visual_overcast,
            visual_storm,
            visual_tempest);
    vec3 clear_color =
        mix(
            shallow_color,
            deep_color,
            smoothstep(0.35, 9.0, water_depth));
    return clear_color;
}

)VALCRAFT_GLSL" +
        R"VALCRAFT_GLSL(
float fragmented_foam(
    vec2 world_xz,
    float scale,
    float threshold
) {
    if (u_water_surface_detail < 0.45) {
        return 0.0;
    }

    vec2 flow =
        vec2(
            u_water_animation_time * 0.085,
            -u_water_animation_time * 0.057);
    float first =
        value_noise2(world_xz * scale + flow);
    float noise = first;
    if (u_water_surface_detail > 0.85) {
        float second =
            value_noise2(
                world_xz * scale * 2.07 -
                flow.yx * 0.73 +
                vec2(9.7, 3.1));
        noise =
            first * 0.68 +
            second * 0.32;
    }
    float width =
        max(
            fwidth(noise) * 1.35,
            0.018);
    return
        smoothstep(
            threshold - width,
            threshold + width,
            noise);
}

float ship_wake_mask(
    vec3 world_position
) {
    if (u_ship_protection_enabled == 0 ||
        u_ship_speed <= 0.05 ||
        u_water_surface_detail < 0.45) {
        return 0.0;
    }

    // Je calibre la réponse sur la vitesse réelle de l'Amélie (environ
    // 1,18 m/s en croisière), pas sur une vitesse théorique jamais atteinte.
    float speed_ratio =
        smoothstep(0.15, 2.40, u_ship_speed);
    float wake_length =
        mix(9.0, 44.0, speed_ratio);
    float padding = wake_length + 5.0;
    if (world_position.x <
            u_ship_bounds_min.x - padding ||
        world_position.x >
            u_ship_bounds_max.x + padding ||
        world_position.z <
            u_ship_bounds_min.z - padding ||
        world_position.z >
            u_ship_bounds_max.z + padding) {
        return 0.0;
    }

    vec3 local_position =
        (u_ship_inverse_model *
         vec4(world_position, 1.0))
            .xyz;
    float stern_z =
        u_ship_profile_longitudinal.x;
    float bow_z =
        u_ship_profile_longitudinal.y;
    float maximum_half_width =
        u_ship_profile_longitudinal.z;
    float aft_distance =
        stern_z - local_position.z;
    float aft_valid =
        step(0.0, aft_distance) *
        (1.0 - step(wake_length, aft_distance));
    float wake_fade =
        1.0 -
        smoothstep(
            wake_length * 0.48,
            wake_length,
            aft_distance);
    float ribbon_center =
        maximum_half_width * 0.28 +
        aft_distance * 0.17;
    float ribbon_width =
        mix(0.24, 0.68, speed_ratio);
    float ribbon_distance =
        abs(
            abs(local_position.x) -
            ribbon_center);
    float ribbons =
        1.0 -
        smoothstep(
            ribbon_width,
            ribbon_width +
                max(fwidth(ribbon_distance), 0.08),
            ribbon_distance);
    float center_churn =
        (1.0 -
         smoothstep(
             maximum_half_width * 0.16,
             maximum_half_width * 0.62 +
                 aft_distance * 0.06,
             abs(local_position.x))) *
        (1.0 -
         smoothstep(
             0.0,
             wake_length * 0.58,
             aft_distance));

    float bow_distance =
        length(
            vec2(
                max(
                    abs(local_position.x) -
                        maximum_half_width * 0.72,
                    0.0),
                local_position.z - bow_z));
    float bow_foam =
        1.0 -
        smoothstep(
            mix(0.55, 1.10, speed_ratio),
            mix(1.05, 2.25, speed_ratio),
            bow_distance);
    float breakup =
        fragmented_foam(
            local_position.xz,
            0.42,
            0.44);
    return
        clamp(
            (
                (ribbons * 0.82 +
                 center_churn * 0.42) *
                    aft_valid *
                    wake_fade +
                bow_foam * 0.72
            ) *
                mix(0.24, 1.0, speed_ratio) *
                mix(0.72, 1.0, breakup),
            0.0,
            1.0);
}

)VALCRAFT_GLSL" +
        R"VALCRAFT_GLSL(
void main() {
    if (ship_excludes_ocean(v_world_position)) {
        // Je rejette l'eau sous la coque avant toute lecture de réfraction.
        discard;
    }

    float surface_mask =
        clamp(
            max(v_normal.y, 0.0),
            0.0,
            1.0);
    float detail_fade = 1.0;
    if (u_maritime_horizon_enabled != 0 &&
        u_maritime_water_blend_range.y >
            u_maritime_water_blend_range.x) {
        detail_fade =
            1.0 -
            smoothstep(
                u_maritime_water_blend_range.x,
                u_maritime_water_blend_range.y,
                v_distance);
    }

    vec2 detail_gradient = vec2(0.0);
    if (u_ocean_detail_strength > 0.000001) {
        detail_gradient +=
            analytic_detail_gradient(
                v_world_position.xz);
    }
    detail_gradient +=
        material_detail_gradient(
            v_world_position.xz);
    vec2 rain_gradient =
        rain_dimple_gradient(
            v_world_position.xz);
    detail_gradient +=
        rain_gradient;
    detail_gradient *=
        detail_fade;

    // Je conserve la normale géométrique sur les parois verticales et je
    // réserve la houle ainsi que ses micro-détails à la surface de l'eau.
    vec3 geometric_normal =
        ocean_visual_safe_normalize(
            v_normal,
            vec3(0.0, 1.0, 0.0));
    vec3 ocean_surface_normal =
        ocean_visual_safe_normalize(
            vec3(
                v_ocean_normal.x - detail_gradient.x,
                max(v_ocean_normal.y, 0.08),
                v_ocean_normal.z - detail_gradient.y),
            geometric_normal);
    vec3 normal =
        ocean_visual_safe_normalize(
            mix(
                geometric_normal,
                ocean_surface_normal,
                surface_mask),
            geometric_normal);
    if (!gl_FrontFacing) {
        normal = -normal;
    }

    vec3 view_direction =
        normalize(
            u_camera_position -
            v_world_position);
    float view_alignment =
        clamp(
            abs(
                dot(
                    view_direction,
                    normal)),
            0.0,
            1.0);
    float fresnel =
        ocean_visual_fresnel_schlick(
            view_alignment);

    vec2 scene_texel =
        1.0 /
        vec2(
            textureSize(
                u_scene_color,
                0));
    vec2 scene_uv =
        gl_FragCoord.xy * scene_texel;
    vec2 refraction_offset =
        normal.xz *
        mix(0.018, 0.004, fresnel) *
        surface_mask;
    vec2 refracted_uv =
        clamp(
            scene_uv + refraction_offset,
            scene_texel * 0.5,
            vec2(1.0) - scene_texel * 0.5);

    float base_scene_depth =
        texture(
            u_scene_depth,
            scene_uv)
            .r;
    float refracted_scene_depth =
        texture(
            u_scene_depth,
            refracted_uv)
            .r;
    if (refracted_scene_depth + 0.00005 <
        gl_FragCoord.z) {
        refracted_uv = scene_uv;
        refracted_scene_depth =
            base_scene_depth;
    }

    vec3 scene_color =
        texture(
            u_scene_color,
            refracted_uv)
            .rgb;
    float water_depth = 0.0;
    if (refracted_scene_depth < 0.9999) {
        vec3 background_position =
            reconstruct_world_position(
                refracted_uv,
                refracted_scene_depth);
        water_depth =
            max(
                distance(
                    background_position,
                    v_world_position),
                0.0);
    } else {
        water_depth =
            10.0 +
            fresnel * 16.0;
    }
    water_depth =
        clamp(
            water_depth,
            0.0,
            64.0);
    float body_depth =
        max(
            water_depth,
            0.24 + 0.18 * surface_mask);

    vec3 transmittance =
        exp(
            -k_ocean_visual_absorption *
            body_depth);
    vec3 volume_color =
        atlantic_volume_color(
            body_depth);
    float daylight =
        clamp(
            u_daylight_factor,
            0.0,
            1.0);
    float sky_light =
        clamp(
            v_sky_light,
            0.0,
            1.0);
    vec3 water_light =
        u_ambient_color *
            mix(0.62, 1.05, sky_light) +
        u_sun_color *
            daylight *
            clamp(u_sun_visibility, 0.0, 1.0) *
            0.18 +
        vec3(1.00, 0.62, 0.30) *
            clamp(v_block_light, 0.0, 1.0) *
            0.26;
    vec3 water_body =
        scene_color * transmittance +
        volume_color *
            water_light *
            (vec3(1.0) - transmittance);

    vec3 reflected_view =
        reflect(
            -view_direction,
            normal);
    float reflection_overcast =
        clamp(
            max(
                u_cloud_intensity * 0.82,
                u_overcast_intensity),
            0.0,
            1.0);
    float reflection_storm =
        clamp(
            max(
                u_storm_intensity,
                u_ocean_severity),
            0.0,
            1.0);
    float reflection_tempest =
        clamp(
            u_ocean_tempest_factor,
            0.0,
            1.0);
    vec3 reflected_sky =
        ocean_visual_reflected_sky(
            reflected_view,
            u_sun_direction,
            u_sky_zenith_color,
            u_sky_horizon_color,
            u_sun_color *
                clamp(
                    u_sun_visibility,
                    0.0,
                    1.0),
            u_moon_disk_color,
            daylight,
            1.0 - daylight,
            reflection_overcast,
            reflection_storm,
            reflection_tempest,
            u_lightning_intensity);

    float shallow_contact =
        1.0 -
        smoothstep(
            0.08,
            0.95,
            body_depth);
    float slope_energy =
        clamp(
            (1.0 - normal.y) * 3.5,
            0.0,
            1.0);
    float foam_detail =
        clamp(
            u_water_surface_detail,
            0.0,
            1.0);
    float foam_breakup = 0.0;
    float crest_foam = 0.0;
    float breaking_foam = 0.0;
    float rain_impact_foam = 0.0;
    if (foam_detail >= 0.45) {
        foam_breakup =
            fragmented_foam(
                v_world_position.xz,
                0.48,
                0.47);
        float crest_signal =
            v_ocean_crest * 0.80 +
            slope_energy * 0.30 +
            foam_breakup * 0.10;
        float derivative_width =
            max(
                fwidth(crest_signal) * 1.45,
                0.012);
        float detailed_foam_strength =
            smoothstep(
                0.45,
                1.0,
                foam_detail);
        crest_foam =
            smoothstep(
                clamp(
                    u_ocean_foam_threshold,
                    0.55,
                    0.98) -
                    derivative_width,
                1.04 + derivative_width,
                crest_signal) *
            surface_mask *
            clamp(
                u_ocean_severity,
                0.0,
                1.0) *
            mix(
                0.68,
                1.0,
                detailed_foam_strength);
        breaking_foam =
            smoothstep(
                0.38,
                0.90,
                slope_energy * 0.72 +
                    v_ocean_crest * 0.46 +
                    foam_breakup * 0.12) *
            clamp(
                u_ocean_tempest_factor,
                0.0,
                1.0) *
            clamp(
                u_ocean_open_sea,
                0.0,
                1.0) *
            detailed_foam_strength;

        // Je fais blanchir brièvement les anneaux les plus énergiques des
        // impacts de pluie, avec fwidth pour éviter tout scintillement crénelé.
        float rain_signal =
            length(
                rain_gradient);
        float rain_width =
            max(
                fwidth(rain_signal) * 1.35,
                0.002);
        rain_impact_foam =
            smoothstep(
                0.018 - rain_width,
                0.055 + rain_width,
                rain_signal) *
            clamp(
                u_precipitation_intensity,
                0.0,
                1.0) *
            surface_mask *
            detailed_foam_strength;
    }
    float wake =
        ship_wake_mask(
            v_world_position);
    float foam_strength =
        clamp(
            shallow_contact * 0.58 +
                crest_foam * 0.72 +
                breaking_foam * 0.82 +
                rain_impact_foam * 0.26 +
                wake,
            0.0,
            1.0) *
        detail_fade;
    vec3 foam_color =
        mix(
            u_fog_color,
            vec3(0.82, 0.90, 0.89),
            0.70);

    vec3 color =
        ocean_visual_surface_radiance(
            water_body,
            reflected_sky,
            normal,
            view_direction);
    color =
        mix(
            color,
            foam_color *
                (0.78 + 0.34 * daylight),
            foam_strength);
    color +=
        volume_color *
        (0.012 + 0.024 * daylight) *
        surface_mask *
        detail_fade;
    color +=
        u_night_tint_color *
        (1.0 - daylight) *
        0.025;
    color +=
        u_horizon_glow_color *
        pow(
            1.0 - view_alignment,
            3.0) *
        0.025;

    float weather_fog =
        1.0 +
        clamp(
            u_precipitation_intensity,
            0.0,
            1.0) *
            0.42 +
        clamp(
            u_storm_intensity,
            0.0,
            1.0) *
            0.38;
    float distance_fog =
        1.0 -
        exp(
            -v_distance *
            v_distance *
            0.000008 *
            weather_fog);
    float terminal_fog =
        u_maritime_horizon_enabled != 0
            ? smoothstep(
                  u_maritime_far_fog_range.x,
                  max(
                      u_maritime_far_fog_range.y,
                      u_maritime_far_fog_range.x +
                          0.001),
                  v_distance)
            : 0.0;
    float fog =
        clamp(
            max(
                distance_fog,
                terminal_fog),
            0.0,
            1.0);
    vec3 fog_color =
        mix(
            u_fog_color,
            u_distant_fog_color,
            sqrt(fog));
    color =
        mix(
            color,
            fog_color,
            fog);

    if (u_maritime_horizon_enabled != 0 &&
        u_camera_position.y >=
            u_maritime_sea_level &&
        u_maritime_water_blend_range.y >
            u_maritime_water_blend_range.x) {
        float far_blend =
            smoothstep(
                u_maritime_water_blend_range.x,
                u_maritime_water_blend_range.y,
                  v_distance);
        vec3 far_normal =
            ocean_visual_far_wave_normal(
                v_world_position.xz,
                u_ocean_waves[0],
                u_ocean_wave_phases[0],
                u_ocean_waves[1],
                u_ocean_wave_phases[1]);
        vec3 far_reflected_view =
            reflect(
                -view_direction,
                far_normal);
        vec3 far_ocean_body =
            ocean_visual_atlantic_body_color(
                daylight,
                reflection_overcast,
                reflection_storm,
                reflection_tempest);
        vec3 far_ocean_reflection =
            ocean_visual_reflected_sky(
                far_reflected_view,
                u_sun_direction,
                u_sky_zenith_color,
                u_sky_horizon_color,
                u_sun_color *
                    clamp(
                        u_sun_visibility,
                        0.0,
                        1.0),
                u_moon_disk_color,
                daylight,
                1.0 - daylight,
                reflection_overcast,
                reflection_storm,
                reflection_tempest,
                u_lightning_intensity);
        vec3 far_ocean =
            ocean_visual_surface_radiance(
                far_ocean_body,
                far_ocean_reflection,
                far_normal,
                view_direction);
        far_ocean =
            mix(
                far_ocean,
                fog_color,
                fog);
        color =
            mix(
                color,
                far_ocean,
                far_blend);
    }

    frag_color =
        vec4(
            max(color, vec3(0.0)),
            1.0);
}
)VALCRAFT_GLSL";
    return source;
}

} // namespace valcraft
