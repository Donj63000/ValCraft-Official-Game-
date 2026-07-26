#pragma once

#include <string_view>

namespace valcraft {

inline constexpr std::string_view kModernTerrainVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in uvec4 a_material_ao;
layout(location = 3) in uvec2 a_lighting;
layout(location = 4) in uint a_surface_flags;

uniform mat4 u_model;
uniform mat4 u_view_projection;
uniform mat4 u_light_view_projection;
uniform vec3 u_camera_position;

out vec3 v_world_position;
out vec3 v_normal;
out vec4 v_light_position;
out float v_distance;
out float v_ao;
out float v_sky_light;
out float v_block_light;
out float v_material_blend;
flat out uint v_primary_block;
flat out uint v_secondary_block;
flat out uint v_surface_flags;

void main() {
    vec4 world_position = u_model * vec4(a_position, 1.0);
    mat3 normal_matrix = transpose(inverse(mat3(u_model)));
    v_world_position = world_position.xyz;
    v_normal = normalize(normal_matrix * a_normal);
    v_light_position = u_light_view_projection * world_position;
    v_distance = distance(world_position.xyz, u_camera_position);
    v_ao = float(a_material_ao.w) / 255.0;
    v_sky_light = float(a_lighting.x) / 15.0;
    v_block_light = float(a_lighting.y) / 15.0;
    v_material_blend = float(a_material_ao.z) / 255.0;
    v_primary_block = a_material_ao.x;
    v_secondary_block = a_material_ao.y;
    v_surface_flags = a_surface_flags;
    gl_Position = u_view_projection * world_position;
}
)";

inline constexpr std::string_view kModernArchitectureVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in uvec2 a_uv_fixed;
layout(location = 3) in uvec4 a_material_lighting_flags;

uniform mat4 u_model;
uniform mat4 u_view_projection;
uniform mat4 u_light_view_projection;
uniform vec3 u_camera_position;

out vec3 v_world_position;
out vec3 v_normal;
out vec4 v_light_position;
out float v_distance;
out float v_ao;
out float v_sky_light;
out float v_block_light;
out float v_material_blend;
flat out uint v_primary_block;
flat out uint v_secondary_block;
flat out uint v_surface_flags;

void main() {
    vec4 world_position = u_model * vec4(a_position, 1.0);
    mat3 normal_matrix = transpose(inverse(mat3(u_model)));
    v_world_position = world_position.xyz;
    v_normal = normalize(normal_matrix * a_normal);
    v_light_position = u_light_view_projection * world_position;
    v_distance = distance(world_position.xyz, u_camera_position);
    v_ao = 1.0;
    v_sky_light = float(a_material_lighting_flags.y) / 15.0;
    v_block_light = float(a_material_lighting_flags.z) / 15.0;
    v_material_blend = 0.0;
    v_primary_block = a_material_lighting_flags.x;
    v_secondary_block = 0u;

    // bit 1 : surface architecturale ; bit 2 : verre transparent ;
    // bit 3 : un contour exterieur peut recevoir un biseau visuel.
    uint flags = 2u;
    if ((a_material_lighting_flags.w & 16u) != 0u) flags |= 4u;
    if ((a_material_lighting_flags.w & 15u) != 0u) flags |= 8u;
    v_surface_flags = flags;
    gl_Position = u_view_projection * world_position;
}
)";

inline constexpr std::string_view kModernTerrainFragmentShaderSource = R"(#version 330 core
in vec3 v_world_position;
in vec3 v_normal;
in vec4 v_light_position;
in float v_distance;
in float v_ao;
in float v_sky_light;
in float v_block_light;
in float v_material_blend;
flat in uint v_primary_block;
flat in uint v_secondary_block;
flat in uint v_surface_flags;

uniform sampler2DArray u_material_albedo;
uniform sampler2DArray u_material_normal_height;
uniform sampler2DArray u_material_orm_emission;
uniform sampler2D u_shadow_map;
uniform sampler2D u_shadow_map_far;
uniform mat4 u_light_view_projection_far;
uniform vec3 u_camera_position;
uniform vec3 u_camera_forward;
uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec3 u_ambient_color;
uniform vec3 u_fog_color;
uniform vec3 u_distant_fog_color;
uniform vec3 u_night_tint_color;
uniform float u_daylight_factor;
uniform float u_sun_visibility;
uniform float u_precipitation_intensity;
uniform float u_storm_intensity;
uniform float u_lightning_intensity;
uniform float u_triplanar_sharpness;
uniform float u_material_detail_scale;
uniform int u_shadows_enabled;
uniform int u_shadow_cascade_count;
uniform float u_shadow_split_distance;
uniform float u_shadow_transition_width;

out vec4 frag_color;

const float k_full_material_detail_threshold = 0.999;
const float k_normal_mapping_detail_threshold = 0.50;
const float k_quantized_material_blend_epsilon = 1.0 / 255.0;

float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

vec3 safe_normalize(vec3 value, vec3 fallback) {
    float value_length_squared = dot(value, value);
    if (!(value_length_squared > 0.00000001) ||
        any(isnan(value)) ||
        any(isinf(value))) {
        float fallback_length_squared = dot(fallback, fallback);
        if (!(fallback_length_squared > 0.00000001) ||
            any(isnan(fallback)) ||
            any(isinf(fallback))) {
            return vec3(0.0, 1.0, 0.0);
        }
        return fallback * inversesqrt(fallback_length_squared);
    }
    return value * inversesqrt(value_length_squared);
}

float material_layer(uint block_id) {
    if (block_id >= 1u && block_id <= 20u) {
        return float(block_id - 1u);
    }
    if (block_id == 25u) return 20.0;
    if (block_id == 26u) return 21.0;
    if (block_id == 27u) return 22.0;
    if (block_id == 28u || block_id == 29u) return 23.0;
    if (block_id == 30u || block_id == 31u) return 24.0;
    if (block_id >= 32u && block_id <= 36u) return float(block_id - 7u);
    if (block_id >= 37u && block_id <= 39u) return 30.0;
    return 2.0;
}

float material_scale(uint block_id) {
    // Je garde le detail sous la taille d'une cellule logique : les grandes
    // nappes floues ne doivent jamais redessiner les anciennes marches voxel.
    if (block_id == 1u) return 1.10;
    if (block_id == 2u) return 0.96;
    if (block_id == 3u) return 0.94;
    if (block_id == 4u) return 0.86;
    if (block_id == 10u || block_id == 11u) return 0.76;
    if (block_id == 12u) return 0.92;
    if (block_id >= 32u && block_id <= 36u) return 0.92;
    return 0.68;
}

float material_normal_strength(uint block_id) {
    // Je garde les sols souples volontairement subtils : leur relief de hauteur
    // contient des motifs directionnels qui ne doivent jamais devenir des stries.
    if (block_id == 1u) return 0.045;
    if (block_id == 2u) return 0.075;
    if (block_id == 3u) return 0.105;
    if (block_id == 4u) return 0.040;
    if (block_id == 10u || block_id == 11u) return 0.095;
    if (block_id == 12u) return 0.030;
    if (block_id >= 32u && block_id <= 36u) return 0.100;
    return 0.085;
}

bool is_geological_block(uint block_id) {
    return (block_id >= 1u && block_id <= 4u) ||
           (block_id >= 10u && block_id <= 12u) ||
           (block_id >= 32u && block_id <= 36u);
}

bool is_vegetation_block(uint block_id) {
    return block_id == 5u ||
           block_id == 6u ||
           (block_id >= 13u && block_id <= 19u);
}

bool is_translucent_foliage(uint block_id) {
    return block_id == 6u ||
           block_id == 14u ||
           (block_id >= 15u && block_id <= 18u);
}

vec3 triplanar_weights(vec3 normal) {
    vec3 weights = pow(max(abs(normal), vec3(0.0001)), vec3(max(u_triplanar_sharpness, 1.0)));
    return weights / max(weights.x + weights.y + weights.z, 0.0001);
}

vec4 sample_triplanar(sampler2DArray source, float layer, float scale, vec3 weights) {
    vec3 coordinate = v_world_position * scale * max(u_material_detail_scale, 0.05);
    vec4 x_projection = texture(source, vec3(coordinate.zy, layer));
    vec4 y_projection = texture(source, vec3(coordinate.xz, layer));
    vec4 z_projection = texture(source, vec3(coordinate.xy, layer));
    return x_projection * weights.x + y_projection * weights.y + z_projection * weights.z;
}

struct MaterialSurfaceSample {
    vec4 albedo;
    vec4 orm;
};

MaterialSurfaceSample sample_material_surface(
    float layer,
    float scale,
    vec3 weights) {
    MaterialSurfaceSample result;
    result.albedo = sample_triplanar(
        u_material_albedo, layer, scale, weights);
    result.orm = sample_triplanar(
        u_material_orm_emission, layer, scale, weights);
    return result;
}

float hash21(vec2 position) {
    vec3 value = fract(vec3(position.xyx) * vec3(0.1031, 0.1030, 0.0973));
    value += dot(value, value.yzx + 33.33);
    return fract((value.x + value.y) * value.z);
}

float value_noise_2d(vec2 position) {
    vec2 cell = floor(position);
    vec2 local = fract(position);
    vec2 curve = local * local * (3.0 - 2.0 * local);
    float southwest = hash21(cell);
    float southeast = hash21(cell + vec2(1.0, 0.0));
    float northwest = hash21(cell + vec2(0.0, 1.0));
    float northeast = hash21(cell + vec2(1.0, 1.0));
    return mix(
        mix(southwest, southeast, curve.x),
        mix(northwest, northeast, curve.x),
        curve.y);
}

float shadow_depth_visibility(
    vec3 projected,
    vec2 texel,
    float bias,
    bool far_cascade,
    vec2 texel_offset) {
    vec2 uv = projected.xy + texel_offset * texel;
    float depth = far_cascade
        ? texture(u_shadow_map_far, uv).r
        : texture(u_shadow_map, uv).r;
    return projected.z - bias <= depth ? 1.0 : 0.0;
}

float shadow_visibility_for_cascade(vec3 normal, bool far_cascade) {
    vec4 light_position = far_cascade
        ? u_light_view_projection_far * vec4(v_world_position, 1.0)
        : v_light_position;
    vec3 projected = light_position.xyz / max(light_position.w, 0.0001);
    projected = projected * 0.5 + 0.5;
    if (projected.z < 0.0 || projected.z > 1.0 ||
        any(lessThan(projected.xy, vec2(0.0))) ||
        any(greaterThan(projected.xy, vec2(1.0)))) {
        return 1.0;
    }
    vec2 texel = far_cascade
        ? 1.0 / vec2(textureSize(u_shadow_map_far, 0))
        : 1.0 / vec2(textureSize(u_shadow_map, 0));
    float ndotl = max(dot(normal, normalize(u_sun_direction)), 0.0);
    float cascade_bias = far_cascade ? 1.35 : 1.0;
    float bias = max(0.00062 * (1.0 - ndotl), 0.00010) * cascade_bias;

    // Je réserve le PCF 3x3 inchangé à la qualité High. Les deux branches
    // réduites sont uniformes pour tout le draw et économisent respectivement
    // cinq puis huit lectures de profondeur par cascade.
    if (u_material_detail_scale < k_normal_mapping_detail_threshold) {
        return shadow_depth_visibility(
            projected, texel, bias, far_cascade, vec2(0.0));
    }

    if (u_material_detail_scale < k_full_material_detail_threshold) {
        float reduced_visibility = 0.0;
        reduced_visibility += shadow_depth_visibility(
            projected, texel, bias, far_cascade, vec2(-0.5, -0.5));
        reduced_visibility += shadow_depth_visibility(
            projected, texel, bias, far_cascade, vec2(0.5, -0.5));
        reduced_visibility += shadow_depth_visibility(
            projected, texel, bias, far_cascade, vec2(-0.5, 0.5));
        reduced_visibility += shadow_depth_visibility(
            projected, texel, bias, far_cascade, vec2(0.5, 0.5));
        return reduced_visibility * 0.25;
    }

    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            visibility += shadow_depth_visibility(
                projected,
                texel,
                bias,
                far_cascade,
                vec2(x, y));
        }
    }
    return visibility / 9.0;
}

float shadow_visibility(vec3 normal) {
    if (u_shadows_enabled == 0 || u_sun_visibility < 0.5) {
        return 1.0;
    }
    if (u_shadow_cascade_count <= 1) {
        return shadow_visibility_for_cascade(normal, false);
    }

    float view_depth = max(
        dot(v_world_position - u_camera_position, u_camera_forward),
        0.0);
    float transition_width = max(u_shadow_transition_width, 0.0);
    if (transition_width <= 0.0001) {
        return shadow_visibility_for_cascade(
            normal,
            view_depth > u_shadow_split_distance);
    }

    float half_width = transition_width * 0.5;
    if (view_depth <= u_shadow_split_distance - half_width) {
        return shadow_visibility_for_cascade(normal, false);
    }
    if (view_depth >= u_shadow_split_distance + half_width) {
        return shadow_visibility_for_cascade(normal, true);
    }

    float blend = smoothstep(
        u_shadow_split_distance - half_width,
        u_shadow_split_distance + half_width,
        view_depth);
    return mix(
        shadow_visibility_for_cascade(normal, false),
        shadow_visibility_for_cascade(normal, true),
        blend);
}

)" R"(
float distribution_ggx(vec3 normal, vec3 halfway, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float ndoth = max(dot(normal, halfway), 0.0);
    float denominator = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(3.14159265 * denominator * denominator, 0.0001);
}

float geometry_schlick_ggx(float ndotv, float roughness) {
    float radius = roughness + 1.0;
    float k = (radius * radius) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, 0.0001);
}

vec3 fresnel_schlick(float cosine, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - cosine, 5.0);
}

vec3 detail_normal(
    float layer,
    float scale,
    vec3 weights,
    vec3 geometric_normal,
    float strength) {
    vec3 coordinate = v_world_position * scale * max(u_material_detail_scale, 0.05);
    vec3 normal_x = texture(u_material_normal_height, vec3(coordinate.zy, layer)).xyz * 2.0 - 1.0;
    vec3 normal_y = texture(u_material_normal_height, vec3(coordinate.xz, layer)).xyz * 2.0 - 1.0;
    vec3 normal_z = texture(u_material_normal_height, vec3(coordinate.xy, layer)).xyz * 2.0 - 1.0;

    // Je réduis progressivement le relief lorsque plusieurs détails de texture
    // couvrent un pixel. Le filtrage des mipmaps traite la couleur, mais une
    // normale renormalisée peut sinon conserver une énergie excessive et créer
    // du moiré sur les pentes rasantes.
    float projection_footprint = max(
        max(length(fwidth(coordinate.zy)), length(fwidth(coordinate.xz))),
        length(fwidth(coordinate.xy)));
    float footprint_fade = 1.0 - smoothstep(0.018, 0.105, projection_footprint);
    float distance_fade = 1.0 - smoothstep(42.0, 118.0, v_distance);
    float detail_weight = clamp(strength, 0.0, 0.14) *
                          footprint_fade *
                          mix(0.34, 1.0, distance_fade);

    vec3 perturbation =
        vec3(0.0, normal_x.y, normal_x.x) * weights.x +
        vec3(normal_y.x, 0.0, normal_y.y) * weights.y +
        vec3(normal_z.x, normal_z.y, 0.0) * weights.z;
    return safe_normalize(
        geometric_normal + perturbation * detail_weight,
        geometric_normal);
}

void main() {
    vec3 geometric_normal = safe_normalize(v_normal, vec3(0.0, 1.0, 0.0));
    bool architectural_surface = (v_surface_flags & 2u) != 0u;
    bool transparent_surface = (v_surface_flags & 4u) != 0u;
    bool silhouette_bevel = (v_surface_flags & 8u) != 0u;
    bool geological_surface =
        (v_surface_flags & 16u) != 0u &&
        is_geological_block(v_primary_block);
    bool vegetation_surface =
        is_vegetation_block(v_primary_block) ||
        is_vegetation_block(v_secondary_block);
    bool translucent_foliage =
        is_translucent_foliage(v_primary_block) ||
        is_translucent_foliage(v_secondary_block);
    vec3 weights = triplanar_weights(geometric_normal);
    float primary_layer = material_layer(v_primary_block);
    float secondary_layer = material_layer(v_secondary_block);
    float primary_scale = material_scale(v_primary_block);
    float secondary_scale = material_scale(v_secondary_block);
    float blend = v_secondary_block == 0u ? 0.0 : saturate(v_material_blend);
    float macro_noise = value_noise_2d(v_world_position.xz * 0.038);

    if (geological_surface) {
        // Je combine le poids continu préparé par le mesher avec des critères
        // visuels continus. Aucun échantillon de texture supplémentaire n'est
        // nécessaire et les anciennes strates de BlockId ne réapparaissent pas.
        float upward = saturate(geometric_normal.y);
        float slope_rock = smoothstep(0.20, 0.82, 1.0 - upward);
        float sheltered_rock = 1.0 - smoothstep(0.08, 0.92, v_sky_light);
        float altitude_threshold = mix(82.0, 96.0, macro_noise);
        float altitude_rock = smoothstep(
            altitude_threshold,
            altitude_threshold + 30.0,
            v_world_position.y);
        float environmental_rock = saturate(
            slope_rock * 0.82 +
            sheltered_rock * 0.22 +
            altitude_rock * 0.12);
        blend = saturate(
            mix(blend, environmental_rock, 0.22) +
            (macro_noise - 0.5) * 0.045);

        bool mineral_surface =
            v_primary_block >= 32u && v_primary_block <= 36u;
        if (mineral_surface) {
            blend = min(blend, 0.46);
        }
    }

    // Je conserve exactement les poids interpolés en qualité High. Pour les
    // qualités réduites, je pince seulement le premier pas quantifié du mesher ;
    // la branche reste cohérente sur les grandes zones mono-matériau et évite
    // six lectures triplanaires inutiles pour la couche imperceptible.
    bool full_material_detail =
        u_material_detail_scale >= k_full_material_detail_threshold;
    float blend_epsilon = full_material_detail
        ? 0.0
        : k_quantized_material_blend_epsilon;
    bool primary_material_only =
        v_secondary_block == 0u || blend <= blend_epsilon;
    bool secondary_material_only =
        !primary_material_only && blend >= 1.0 - blend_epsilon;

    MaterialSurfaceSample primary_sample;
    MaterialSurfaceSample secondary_sample;
    if (primary_material_only) {
        primary_sample = sample_material_surface(
            primary_layer, primary_scale, weights);
        secondary_sample = primary_sample;
        blend = 0.0;
    } else if (secondary_material_only) {
        secondary_sample = sample_material_surface(
            secondary_layer, secondary_scale, weights);
        primary_sample = secondary_sample;
        blend = 1.0;
    } else {
        primary_sample = sample_material_surface(
            primary_layer, primary_scale, weights);
        secondary_sample = sample_material_surface(
            secondary_layer, secondary_scale, weights);
    }

    vec4 primary_albedo = primary_sample.albedo;
    vec4 secondary_albedo = secondary_sample.albedo;
    vec4 primary_orm = primary_sample.orm;
    vec4 secondary_orm = secondary_sample.orm;
    vec3 albedo = mix(primary_albedo.rgb, secondary_albedo.rgb, blend);
    vec4 orm = mix(primary_orm, secondary_orm, blend);
    if (geological_surface && v_primary_block == 1u) {
        // La terre est la transition perceptive entre végétation et roche. Je
        // la dérive analytiquement de la paire existante afin de garder deux
        // couches triplanaires seulement et une bande parfaitement continue.
        float loam_band =
            1.0 - smoothstep(0.12, 0.35, abs(blend - 0.43));
        float loam_luminance = dot(
            mix(primary_albedo.rgb, secondary_albedo.rgb, 0.44),
            vec3(0.2126, 0.7152, 0.0722));
        vec3 loam_color = loam_luminance * vec3(0.94, 0.72, 0.56);
        albedo = mix(albedo, loam_color, loam_band * 0.62);
        orm.g = mix(orm.g, max(orm.g, 0.82), loam_band * 0.55);
    }
    float coverage = mix(primary_albedo.a, secondary_albedo.a, blend);
    bool cutout_surface = (v_surface_flags & 1u) != 0u;
    if (cutout_surface && coverage < 0.46) {
        discard;
    }

    // Je désactive entièrement les lectures de normales en Low. En Medium et
    // High, je garde le relief existant mais je n'échantillonne qu'une seule
    // couche lorsque le fast path matériau a déjà établi un poids extrême.
    vec3 normal = geometric_normal;
    if (u_material_detail_scale >= k_normal_mapping_detail_threshold) {
        if (primary_material_only) {
            normal = detail_normal(
                primary_layer,
                primary_scale,
                weights,
                geometric_normal,
                material_normal_strength(v_primary_block));
        } else if (secondary_material_only) {
            normal = detail_normal(
                secondary_layer,
                secondary_scale,
                weights,
                geometric_normal,
                material_normal_strength(v_secondary_block));
        } else {
            vec3 primary_normal = detail_normal(
                primary_layer,
                primary_scale,
                weights,
                geometric_normal,
                material_normal_strength(v_primary_block));
            vec3 secondary_normal = detail_normal(
                secondary_layer,
                secondary_scale,
                weights,
                geometric_normal,
                material_normal_strength(v_secondary_block));
            normal = safe_normalize(
                mix(primary_normal, secondary_normal, blend),
                geometric_normal);
        }
    }
    if (architectural_surface) {
        normal = safe_normalize(
            mix(geometric_normal, normal, 0.38),
            geometric_normal);
    }

    float slope = 1.0 - saturate(geometric_normal.y);
    float altitude = smoothstep(36.0, 94.0, v_world_position.y);
    vec3 cool_tint = vec3(0.86, 0.96, 1.08);
    vec3 warm_tint = vec3(1.10, 0.98, 0.82);
    if (architectural_surface) {
        albedo *= mix(0.975, 1.025, macro_noise);
    } else {
        albedo *= mix(warm_tint, cool_tint, saturate(altitude * 0.72 + slope * 0.28));
        albedo *= mix(0.955, 1.045, macro_noise);
        if (geological_surface) {
            float luminance = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
            albedo = mix(vec3(luminance), albedo, 0.86);
        }
    }

    float wetness = saturate(u_precipitation_intensity) *
                    saturate(v_sky_light) *
                    smoothstep(-0.15, 0.9, normal.y);
    float raw_occlusion = saturate(orm.r * v_ao);
    float occlusion_floor = architectural_surface ? 0.24 : 0.30;
    if (geological_surface) {
        occlusion_floor = 0.34;
    }
    if (vegetation_surface) {
        occlusion_floor = 0.52;
    }
    float occlusion = mix(occlusion_floor, 1.0, raw_occlusion);
    float roughness = clamp(mix(orm.g, orm.g * 0.48, wetness), 0.08, 1.0);
    float metallic = saturate(orm.b);
    float emission = saturate(orm.a);

    vec3 view_direction = safe_normalize(
        u_camera_position - v_world_position,
        geometric_normal);
    vec3 light_direction = safe_normalize(
        u_sun_direction,
        vec3(0.0, 1.0, 0.0));
    vec3 halfway = safe_normalize(
        view_direction + light_direction,
        normal);
    float signed_ndotl = dot(normal, light_direction);
    float ndotl = max(signed_ndotl, 0.0);
    float wrapped_ndotl = saturate((signed_ndotl + 0.24) / 1.24);
    float diffuse_ndotl = mix(
        ndotl,
        wrapped_ndotl,
        vegetation_surface ? 0.72 : 0.18);
    float ndotv = max(dot(normal, view_direction), 0.001);
    float visibility = shadow_visibility(normal);

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    float distribution = distribution_ggx(normal, halfway, roughness);
    float geometry = geometry_schlick_ggx(ndotv, roughness) *
                     geometry_schlick_ggx(max(dot(normal, light_direction), 0.0), roughness);
    vec3 fresnel = fresnel_schlick(max(dot(halfway, view_direction), 0.0), f0);
    vec3 specular = distribution * geometry * fresnel /
                    max(4.0 * ndotv * max(ndotl, 0.001), 0.001);
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * albedo / 3.14159265;

    float daylight = saturate(u_daylight_factor);
    float sky_exposure = smoothstep(0.0, 1.0, saturate(v_sky_light));
    vec3 ambient_albedo = albedo;
    if (vegetation_surface) {
        // Je relève uniquement le terme diffus des matières végétales sombres :
        // leur albédo et leur identité restent inchangés sous la lumière directe.
        ambient_albedo = max(ambient_albedo, vec3(0.055));
    }
    vec3 ambient = u_ambient_color * ambient_albedo *
                   mix(0.50, 1.08, sky_exposure) * occlusion;
    float hemisphere = saturate(geometric_normal.y * 0.5 + 0.5);
    vec3 bounce_tint = mix(
        vec3(0.22, 0.13, 0.075),
        max(u_ambient_color, vec3(0.055, 0.065, 0.085)),
        hemisphere);
    vec3 bounce = ambient_albedo * bounce_tint *
                  (0.075 + 0.075 * daylight) *
                  mix(0.72, 1.0, sky_exposure) *
                  occlusion;
    vec3 direct =
        (diffuse * diffuse_ndotl + specular * ndotl) *
        u_sun_color * visibility * u_sun_visibility * daylight;
    if (cutout_surface || vegetation_surface) {
        float transmitted = max(dot(-normal, light_direction), 0.0);
        float transmission_strength = translucent_foliage
            ? 0.30
            : (cutout_surface ? 0.20 : 0.075);
        direct += ambient_albedo * u_sun_color *
                  transmitted * transmission_strength *
                  mix(0.38, 1.0, visibility) *
                  u_sun_visibility * daylight;
    }
    vec3 torch = vec3(1.18, 0.63, 0.25) * saturate(v_block_light) *
                 (albedo * 0.72 + vec3(0.18));
    float rim = pow(1.0 - ndotv, 2.6);
    float rim_strength = geological_surface
        ? (0.006 + 0.018 * daylight)
        : (vegetation_surface
               ? (0.018 + 0.040 * daylight)
               : (0.020 + 0.050 * daylight));
    vec3 rim_light =
        mix(u_fog_color, u_sun_color, 0.45) *
        rim * rim_strength;
    if (architectural_surface && silhouette_bevel) {
        rim_light += u_sun_color * rim * 0.045 * daylight;
    }
    vec3 color = ambient + bounce + direct + torch + rim_light;
    color += albedo * emission * vec3(1.35, 0.74, 0.28);
    color = mix(color, color * vec3(0.69, 0.77, 0.86), wetness * 0.22);
    color += albedo * vec3(0.64, 0.74, 1.0) *
             saturate(u_lightning_intensity) * saturate(v_sky_light) * 0.42;
    color += u_night_tint_color * (1.0 - daylight) * (0.045 + 0.05 * v_sky_light);
    // Je calibre un vrai minimum d'eclairage indirect dans l'espace lineaire.
    // Le tone mapping du projet etait historiquement regle pour l'atlas voxel;
    // un simple plancher sur la couleur ambiante laissait donc les faces a
    // l'ombre presque noires avec des albedos sRGB physiquement corrects.
    float readability_energy =
        mix(0.105, 0.30, daylight) *
        mix(0.42, 1.0, sky_exposure);
    if (vegetation_surface) {
        readability_energy += mix(0.055, 0.18, daylight);
    }
    vec3 readability_floor =
        ambient_albedo * readability_energy *
            mix(0.78, 1.0, occlusion) +
        mix(
            vec3(0.006, 0.007, 0.011),
            vec3(0.018, 0.020, 0.022),
            daylight);
    color = max(color, readability_floor);

    float weather_fog = 1.0 + saturate(u_precipitation_intensity) * 0.40 +
                        saturate(u_storm_intensity) * 0.36;
    float fog = 1.0 - exp(-v_distance * v_distance * 0.000008 * weather_fog);
    float horizon = smoothstep(0.0, 1.0, 1.0 - abs(normalize(v_world_position - u_camera_position).y));
    vec3 atmospheric_color = mix(u_fog_color, u_distant_fog_color, saturate(fog + horizon * 0.08));
    color = mix(color, atmospheric_color, saturate(fog));
    float output_alpha = 1.0;
    if (transparent_surface) {
        color = mix(color, u_fog_color + vec3(0.08, 0.13, 0.16), 0.30);
        output_alpha = clamp(0.30 + coverage * 0.28, 0.30, 0.58);
    }
    frag_color = vec4(max(color, vec3(0.0)), output_alpha);
}
)";

inline constexpr std::string_view kModernTerrainShadowVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 a_position;
uniform mat4 u_model;
uniform mat4 u_light_view_projection;
void main() {
    gl_Position = u_light_view_projection * u_model * vec4(a_position, 1.0);
}
)";

inline constexpr std::string_view kModernTerrainShadowFragmentShaderSource = R"(#version 330 core
void main() {
}
)";

} // namespace valcraft
