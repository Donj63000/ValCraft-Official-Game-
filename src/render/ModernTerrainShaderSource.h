#pragma once

#include <string>
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
uniform float u_time_seconds;

out vec3 v_world_position;
out vec3 v_normal;
out vec4 v_light_position;
out float v_distance;
out float v_ao;
out float v_sky_light;
out float v_block_light;
out float v_material_blend;
out vec2 v_surface_uv;
flat out uint v_primary_block;
flat out uint v_secondary_block;
flat out uint v_surface_flags;

void main() {
    vec3 animated_position = a_position;
    if ((a_surface_flags & 64u) != 0u) {
        // Je fixe la base par le V canonique et je laisse seulement la cime
        // suivre deux houles lentes. Le décor reste ainsi soudé au fond.
        float flexibility =
            smoothstep(
                0.02,
                0.98,
                float(a_material_ao.z) / 255.0);
        float phase =
            dot(a_position.xz, vec2(0.31, 0.23)) +
            u_time_seconds * 0.72;
        vec2 drift = vec2(
            sin(phase) + sin(phase * 1.71 + 0.8) * 0.28,
            cos(phase * 0.83 + 0.4) * 0.72);
        animated_position.xz +=
            drift * (0.055 * flexibility);
    }

    vec4 world_position =
        u_model * vec4(animated_position, 1.0);
    mat3 normal_matrix = transpose(inverse(mat3(u_model)));
    v_world_position = world_position.xyz;
    v_normal = normalize(normal_matrix * a_normal);
    v_light_position = u_light_view_projection * world_position;
    v_distance = distance(world_position.xyz, u_camera_position);
    v_ao = float(a_material_ao.w) / 255.0;
    v_sky_light = float(a_lighting.x) / 15.0;
    v_block_light = float(a_lighting.y) / 15.0;

    // Pour une surface alpha-testée, le mesher compacte U dans l'ancien
    // identifiant secondaire et V dans l'ancien poids de mélange. Je restaure
    // ici le contrat matériau normal afin qu'aucun UV ne devienne un BlockId.
    bool cutout_surface = (a_surface_flags & 1u) != 0u;
    v_surface_uv = cutout_surface
        ? vec2(a_material_ao.yz) / 255.0
        : vec2(0.0);
    v_material_blend = cutout_surface
        ? 0.0
        : float(a_material_ao.z) / 255.0;
    v_primary_block = a_material_ao.x;
    v_secondary_block = cutout_surface ? 0u : a_material_ao.y;
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
out vec2 v_surface_uv;
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
    v_surface_uv = vec2(a_uv_fixed) / 256.0;
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

inline constexpr auto* kModernTerrainFragmentShaderSourcePart1 = R"(#version 330 core
in vec3 v_world_position;
in vec3 v_normal;
in vec4 v_light_position;
in float v_distance;
in float v_ao;
in float v_sky_light;
in float v_block_light;
in float v_material_blend;
in vec2 v_surface_uv;
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
uniform vec3 u_block_light_color;
uniform int u_enclosed_interior;
uniform int u_backrooms_flicker_count;
uniform vec4 u_backrooms_flicker_lights[6];
uniform float u_backrooms_flashlight_intensity;
uniform vec3 u_fog_color;
uniform vec3 u_distant_fog_color;
uniform vec2 u_interior_fog_range;
uniform vec3 u_night_tint_color;
uniform float u_daylight_factor;
uniform float u_sun_visibility;
uniform float u_cloud_intensity;
uniform float u_overcast_intensity;
uniform float u_precipitation_intensity;
uniform float u_storm_intensity;
uniform float u_lightning_intensity;
uniform float u_triplanar_sharpness;
uniform float u_material_detail_scale;
uniform int u_shadows_enabled;
uniform int u_shadow_cascade_count;
uniform float u_shadow_split_distance;
uniform float u_shadow_transition_width;
uniform int u_maritime_horizon_enabled;
uniform vec2 u_maritime_detail_transition_range;
uniform float u_maritime_sea_level;
uniform int u_maritime_submersion_active;
uniform float u_time_seconds;

out vec4 frag_color;

const float k_full_material_detail_threshold = 0.999;
const float k_normal_mapping_detail_threshold = 0.50;
const float k_quantized_material_blend_epsilon = 1.0 / 255.0;
const float k_submerged_caustic_max_energy = 0.085;

float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

bool is_backrooms_block(uint block_id) {
    return block_id >= 42u && block_id <= 63u;
}

vec3 backrooms_base_color(uint block_id) {
    if (block_id == 42u) return vec3(0.76, 0.69, 0.36);
    if (block_id == 43u) return vec3(0.46, 0.57, 0.38);
    if (block_id == 44u) return vec3(0.40, 0.53, 0.61);
    if (block_id == 45u) return vec3(0.61, 0.45, 0.47);
    if (block_id == 46u) return vec3(0.55, 0.34, 0.21);
    if (block_id == 47u) return vec3(0.43, 0.44, 0.41);
    if (block_id == 48u) return vec3(0.39, 0.34, 0.20);
    if (block_id == 49u) return vec3(0.67, 0.68, 0.58);
    if (block_id == 50u) return vec3(0.78, 0.94, 0.78);
    if (block_id == 51u) return vec3(0.24, 0.25, 0.22);
    if (block_id == 52u) return vec3(0.92, 0.13, 0.065);
    if (block_id == 53u) return vec3(0.79, 0.83, 0.80);
    if (block_id == 54u) return vec3(0.52, 0.67, 0.65);
    if (block_id == 55u) return vec3(0.24, 0.32, 0.33);
    if (block_id == 56u) return vec3(0.31, 0.39, 0.41);
    if (block_id == 57u) return vec3(0.08, 0.48, 0.55);
    if (block_id == 58u) return vec3(0.72, 1.00, 0.97);
    if (block_id == 59u) return vec3(0.18, 0.23, 0.24);
    if (block_id == 60u) return vec3(0.39, 0.31, 0.20);
    if (block_id == 61u) return vec3(0.13, 0.17, 0.18);
    if (block_id == 62u) return vec3(0.08, 0.29, 0.14);
    if (block_id == 63u) return vec3(0.88, 0.12, 0.045);
    return vec3(1.0);
}

vec2 poolrooms_face_coordinate() {
    vec3 normal_weight = abs(v_normal);
    if (normal_weight.y >= normal_weight.x &&
        normal_weight.y >= normal_weight.z) {
        return v_world_position.xz;
    }
    if (normal_weight.x >= normal_weight.z) {
        return v_world_position.zy;
    }
    return v_world_position.xy;
}

vec3 poolrooms_ceramic_color(uint block_id) {
    // Je pose des carreaux de cinquante centimètres : cette échelle reste
    // lisible à distance sans transformer les grandes salles en grille.
    vec2 tile_position =
        poolrooms_face_coordinate() * 2.0;
    vec2 tile_cell = floor(tile_position);
    vec2 centered =
        abs(fract(tile_position) - vec2(0.5));
    float edge_distance =
        max(centered.x, centered.y);
    vec2 derivative =
        max(fwidth(tile_position), vec2(0.0005));
    float antialias_width =
        clamp(max(derivative.x, derivative.y) * 0.35, 0.0015, 0.035);
    float grout =
        smoothstep(
            0.486 - antialias_width,
            0.496 + antialias_width,
            edge_distance);
    float tile_variation =
        fract(
            sin(
                dot(
                    tile_cell,
                    vec2(12.9898, 78.233))) *
            43758.5453);
    vec3 ceramic =
        backrooms_base_color(block_id) *
        mix(0.985, 1.015, tile_variation);
    vec3 grout_color =
        block_id == 55u
            ? vec3(0.165, 0.225, 0.230)
            : (block_id == 54u
                   ? vec3(0.390, 0.505, 0.495)
                   : vec3(0.585, 0.625, 0.605));
    return mix(ceramic, grout_color, grout);
}

vec3 tint_backrooms_material(vec3 sampled_albedo, uint block_id) {
    if (!is_backrooms_block(block_id)) {
        return sampled_albedo;
    }

    // Je conserve le micro-détail PBR du matériau source, mais sa luminance
    // module une couleur BackRooms stable. Les palettes restent ainsi nettes
    // dans le pipeline moderne sans ajouter onze textures GPU redondantes.
    float luminance = dot(
        sampled_albedo,
        vec3(0.2126, 0.7152, 0.0722));
    float detail = mix(
        0.70,
        1.24,
        smoothstep(0.08, 0.92, luminance));

    if (block_id >= 53u && block_id <= 55u) {
        // Je reconstruis le calepinage en espace monde pour garder des joints
        // continus. Le matériau d'atlas ne fournit plus qu'une variation de
        // trois pour cent afin de ne jamais superposer une seconde grille.
        float atlas_modulation =
            mix(
                0.985,
                1.015,
                smoothstep(0.08, 0.92, luminance));
        return clamp(
            poolrooms_ceramic_color(block_id) *
                atlas_modulation,
            vec3(0.012),
            vec3(1.0));
    }

    if (block_id >= 42u && block_id <= 46u) {
        // Une fibre verticale très discrète empêche les grands murs de devenir
        // des aplats propres ; elle évoque le papier peint jauni sans scintiller.
        float fiber = 0.975 + 0.025 * sin(
            v_world_position.y * 37.0 +
            v_world_position.x * 0.19 +
            v_world_position.z * 0.13);
        detail *= fiber;
    }

    return clamp(
        backrooms_base_color(block_id) * detail,
        vec3(0.015),
        vec3(1.0));
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
    if ((v_surface_flags & 32u) != 0u) {
        return float(clamp(block_id, 1u, 53u) - 1u);
    }
    // Les matériaux BackRooms réutilisent les familles PBR existantes ; leur
    // couleur propre est restaurée ensuite par tint_backrooms_material().
    if (block_id >= 42u && block_id <= 46u) return 3.0;
    if (block_id == 47u) return 2.0;
    if (block_id == 48u) return 24.0;
    if (block_id == 49u) return 11.0;
    if (block_id == 50u || block_id == 52u) return 6.0;
    if (block_id == 51u) return 23.0;
    if (block_id >= 53u && block_id <= 55u) return 45.0;
    if (block_id == 56u || block_id == 59u ||
        block_id == 61u) return 36.0;
    if (block_id == 57u || block_id == 63u) return 45.0;
    if (block_id == 58u) return 38.0;
    if (block_id == 60u) return 32.0;
    if (block_id == 62u) return 5.0;
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

)";

// Je fractionne le fragment shader pour rester sous la limite des littéraux
// MSVC sans modifier la source GLSL finalement assemblée.
inline constexpr auto*
    kModernTerrainFragmentShaderSourcePart1Materials = R"(
float material_scale(uint block_id) {
    if ((v_surface_flags & 32u) != 0u) {
        return 1.0;
    }
    // Je garde le detail sous la taille d'une cellule logique : les grandes
    // nappes floues ne doivent jamais redessiner les anciennes marches voxel.
    if (block_id == 1u) return 1.10;
    if (block_id == 2u) return 0.96;
    if (block_id == 3u) return 0.94;
    if (block_id == 4u) return 0.86;
    if (block_id == 10u || block_id == 11u) return 0.76;
    if (block_id == 12u) return 0.92;
    if (block_id >= 32u && block_id <= 36u) return 0.92;
    if (block_id >= 42u && block_id <= 46u) return 0.44;
    if (block_id == 47u) return 0.82;
    if (block_id == 48u) return 1.16;
    if (block_id == 49u) return 0.72;
    if (block_id >= 50u && block_id <= 52u) return 0.94;
    if (block_id >= 53u && block_id <= 55u) return 0.92;
    if (block_id == 56u || block_id == 59u) return 1.18;
    if (block_id == 57u || block_id == 63u) return 0.82;
    if (block_id == 58u) return 1.00;
    if (block_id == 60u) return 0.68;
    if (block_id == 61u) return 0.86;
    if (block_id == 62u) return 0.94;
    return 0.68;
}

float material_normal_strength(uint block_id) {
    if ((v_surface_flags & 32u) != 0u) {
        return 0.055;
    }
    // Je garde les sols souples volontairement subtils : leur relief de hauteur
    // contient des motifs directionnels qui ne doivent jamais devenir des stries.
    if (block_id == 1u) return 0.045;
    if (block_id == 2u) return 0.075;
    if (block_id == 3u) return 0.105;
    if (block_id == 4u) return 0.040;
    if (block_id == 10u || block_id == 11u) return 0.095;
    if (block_id == 12u) return 0.030;
    if (block_id >= 32u && block_id <= 36u) return 0.100;
    if (block_id >= 42u && block_id <= 46u) return 0.026;
    if (block_id == 47u) return 0.055;
    if (block_id == 48u) return 0.035;
    if (block_id == 49u) return 0.018;
    if (block_id >= 50u && block_id <= 52u) return 0.012;
    // Je neutralise presque entièrement le relief de la couche céramique
    // réutilisée : le seul calepinage lisible doit rester celui de 50 cm.
    if (block_id >= 53u && block_id <= 55u) return 0.008;
    if (block_id == 56u || block_id == 59u) return 0.055;
    if (block_id == 57u || block_id == 63u) return 0.018;
    if (block_id == 58u) return 0.010;
    if (block_id == 60u) return 0.050;
    if (block_id == 61u) return 0.035;
    if (block_id == 62u) return 0.045;
    return 0.085;
}

float backrooms_material_roughness(
    uint block_id,
    float sampled_roughness
) {
    if (block_id == 53u) return 0.27;
    if (block_id == 54u) return 0.095;
    if (block_id == 55u) return 0.34;
    if (block_id == 56u) return 0.24;
    if (block_id == 57u) return 0.23;
    if (block_id == 58u) return 0.16;
    if (block_id == 59u) return 0.49;
    if (block_id == 60u) return 0.52;
    if (block_id == 61u) return 0.31;
    if (block_id == 62u) return 0.74;
    if (block_id == 63u) return 0.20;
    return sampled_roughness;
}

float backrooms_material_metallic(
    uint block_id,
    float sampled_metallic
) {
    if (block_id == 56u) return 0.86;
    if (block_id == 59u) return 0.72;
    if (block_id == 61u) return 0.38;
    if (block_id >= 53u && block_id <= 63u) return 0.0;
    return sampled_metallic;
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

vec2 clamped_cutout_uv() {
    vec2 texture_size = max(
        vec2(textureSize(u_material_albedo, 0).xy),
        vec2(1.0));
    vec2 half_texel = 0.5 / texture_size;

    // Le tableau de matériaux reste en GL_REPEAT pour le terrain triplanaire.
    // Je garde donc les cartes 0..1 à l'intérieur de leur dernier texel afin
    // que U/V == 1 ne reboucle pas sur le bord opposé.
    return clamp(
        v_surface_uv,
        half_texel,
        vec2(1.0) - half_texel);
}

MaterialSurfaceSample sample_cutout_surface(float layer) {
    vec2 uv = clamped_cutout_uv();
    MaterialSurfaceSample result;
    result.albedo = texture(
        u_material_albedo,
        vec3(uv, layer));
    result.orm = texture(
        u_material_orm_emission,
        vec3(uv, layer));
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

)";

inline constexpr auto* kModernTerrainFragmentShaderSourcePart2 = R"(
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

    // Chaque projection doit conserver une base tangente de même chiralité
    // sur les faces positives et négatives. Sans ces signes, le canal vert de
    // la normale est inversé sur la moitié des pentes et crée une couture.
    vec3 axis_sign = vec3(
        geometric_normal.x < 0.0 ? -1.0 : 1.0,
        geometric_normal.y < 0.0 ? -1.0 : 1.0,
        geometric_normal.z < 0.0 ? -1.0 : 1.0);
    vec3 perturbation =
        vec3(0.0, -axis_sign.x * normal_x.y, normal_x.x) * weights.x +
        vec3(normal_y.x, 0.0, -axis_sign.y * normal_y.y) * weights.y +
        vec3(normal_z.x, axis_sign.z * normal_z.y, 0.0) * weights.z;
    return safe_normalize(
        geometric_normal + perturbation * detail_weight,
        geometric_normal);
}

float submerged_caustic_envelope(
    float water_depth,
    float daylight) {
    float safe_depth = max(water_depth, 0.0);
    float water_column_entry =
        smoothstep(0.10, 0.75, safe_depth);
    float depth_attenuation =
        1.0 - smoothstep(10.0, 36.0, safe_depth);
    float night_attenuation =
        mix(0.025, 1.0, saturate(daylight));
    float storm_attenuation =
        mix(1.0, 0.24, saturate(u_storm_intensity));
    float cloud_cover =
        saturate(
            max(
                u_cloud_intensity * 0.82,
                u_overcast_intensity));
    float cloud_attenuation =
        mix(1.0, 0.28, cloud_cover);
    float sky_transmission =
        mix(0.45, 1.0, saturate(u_sun_visibility));
    float quality_progress =
        smoothstep(
            k_normal_mapping_detail_threshold,
            k_full_material_detail_threshold,
            saturate(u_material_detail_scale));
    float quality_attenuation =
        mix(0.62, 1.0, quality_progress);
    return saturate(
        water_column_entry *
        depth_attenuation *
        night_attenuation *
        storm_attenuation *
        cloud_attenuation *
        sky_transmission *
        quality_attenuation);
}

)";

inline constexpr auto*
    kModernTerrainFragmentShaderSourcePart2Backrooms = R"(
float backrooms_flashlight_irradiance(
    vec3 world_position) {
    float intensity =
        max(
            u_backrooms_flashlight_intensity,
            0.0);
    if (u_enclosed_interior == 0 ||
        intensity <= 0.0001) {
        return 0.0;
    }

    vec3 camera_to_fragment =
        world_position -
        u_camera_position;
    float distance_squared =
        dot(
            camera_to_fragment,
            camera_to_fragment);
    const float inverse_range_squared =
        1.0 / (34.0 * 34.0);
    float normalized_distance_squared =
        distance_squared *
        inverse_range_squared;
    if (normalized_distance_squared >= 1.0) {
        return 0.0;
    }

    vec3 ray_direction =
        camera_to_fragment *
        inversesqrt(
            max(
                distance_squared,
                0.000001));
    float angle_cosine =
        dot(
            ray_direction,
            safe_normalize(
                u_camera_forward,
                vec3(0.0, 0.0, -1.0)));
    const float outer_cone_cosine = 0.913545;
    const float inner_cone_cosine = 0.974370;
    const float hotspot_cosine = 0.994522;
    const float penumbra_cone_cosine = 0.887011;
    if (angle_cosine <= penumbra_cone_cosine) {
        return 0.0;
    }

    float penumbra =
        smoothstep(
            penumbra_cone_cosine,
            outer_cone_cosine,
            angle_cosine);
    float spill =
        smoothstep(
            outer_cone_cosine,
            inner_cone_cosine,
            angle_cosine);
    float hotspot =
        smoothstep(
            inner_cone_cosine,
            hotspot_cosine,
            angle_cosine);
    float angular_profile =
        penumbra *
        mix(
            0.035,
            mix(
                0.30,
                1.0,
                hotspot),
            spill);
    float distance_attenuation =
        clamp(
            1.0 -
                normalized_distance_squared *
                normalized_distance_squared,
            0.0,
            1.0);
    distance_attenuation *= distance_attenuation;
    distance_attenuation /=
        1.0 +
        0.012 *
        distance_squared;
    return
        intensity *
        angular_profile *
        distance_attenuation;
}

vec2 backrooms_flicker_scales(
    vec3 world_position) {
    vec2 scales = vec2(1.0);
    if (u_enclosed_interior == 0) {
        return scales;
    }
    for (int light_index = 0;
         light_index < 6;
         ++light_index) {
        if (light_index >=
            u_backrooms_flicker_count) {
            break;
        }
        vec2 light_delta =
            world_position.xz -
            u_backrooms_flicker_lights[
                light_index].xz;
        float light_distance_squared =
            dot(
                light_delta,
                light_delta);
        float light_influence =
            1.0 -
            smoothstep(
                9.0,
                100.0,
                light_distance_squared);
        float source_influence =
            1.0 -
            smoothstep(
                4.0,
                12.25,
                light_distance_squared);
        float flicker_intensity =
            clamp(
                u_backrooms_flicker_lights[
                    light_index].w,
                0.05,
                1.0);
        scales.x =
            min(
                scales.x,
                mix(
                    1.0,
                    flicker_intensity,
                    light_influence));
        scales.y =
            min(
                scales.y,
                mix(
                    1.0,
                    flicker_intensity,
                    source_influence));
    }
    return scales;
}

float backrooms_darkness_visibility(
    float local_light,
    float flashlight_energy) {
    if (u_enclosed_interior == 0) {
        return 1.0;
    }

    // Je ne conserve aucune luminance artificielle lorsqu'aucun photon voxel
    // ni aucun rayon de la Maglite n'atteint ce fragment. La transition douce
    // evite toutefois de dessiner les marches discretes de la propagation.
    float safe_local_light =
        (isnan(local_light) ||
         isinf(local_light))
            ? 0.0
            : clamp(
                  local_light,
                  0.0,
                  1.0);
    float safe_flashlight_energy =
        (isnan(flashlight_energy) ||
         isinf(flashlight_energy))
            ? 0.0
            : clamp(
                  flashlight_energy,
                  0.0,
                  1.0);
    float fixture_visibility =
        smoothstep(
            0.000,
            0.620,
            safe_local_light);
    float flashlight_visibility =
        smoothstep(
            0.000,
            0.180,
            safe_flashlight_energy);
    return clamp(
        1.0 -
            (1.0 - fixture_visibility) *
            (1.0 - flashlight_visibility),
        0.0,
        1.0);
}

void main() {
    float maritime_horizon_haze = 0.0;
    if (u_maritime_horizon_enabled != 0 &&
        v_world_position.y >=
            u_maritime_sea_level +
                0.25 &&
        u_maritime_detail_transition_range.y >
            u_maritime_detail_transition_range.x) {
        // Je garde toujours le vrai relief opaque. Le proxy reste une
        // sous-couche : je masque seulement son raccord par une brume légère,
        // sans jamais découper l'île, ses arbres ou sa silhouette.
        float horizontal_distance =
            length(
                v_world_position.xz -
                u_camera_position.xz);
        maritime_horizon_haze =
            smoothstep(
                u_maritime_detail_transition_range.x,
                u_maritime_detail_transition_range.y,
                horizontal_distance);
    }

    vec3 geometric_normal = safe_normalize(v_normal, vec3(0.0, 1.0, 0.0));
    bool cutout_surface = (v_surface_flags & 1u) != 0u;
    bool architectural_surface = (v_surface_flags & 2u) != 0u;
    bool transparent_surface = (v_surface_flags & 4u) != 0u;
    bool silhouette_bevel = (v_surface_flags & 8u) != 0u;
    bool direct_material = (v_surface_flags & 32u) != 0u;
    bool underwater_sway = (v_surface_flags & 64u) != 0u;
    bool marine_fish = (v_surface_flags & 128u) != 0u;
    bool marine_surface =
        direct_material &&
        v_primary_block >= 47u &&
        v_primary_block <= 53u;
    bool submerged_surface =
        u_maritime_horizon_enabled != 0 &&
        !architectural_surface &&
        v_world_position.y <
            u_maritime_sea_level - 0.05;
    float water_depth =
        submerged_surface
            ? max(
                  u_maritime_sea_level -
                      v_world_position.y,
                  0.0)
            : 0.0;
    bool geological_surface =
        (v_surface_flags & 16u) != 0u &&
        is_geological_block(v_primary_block);
    bool vegetation_surface =
        underwater_sway ||
        is_vegetation_block(v_primary_block) ||
        is_vegetation_block(v_secondary_block);
    bool translucent_foliage =
        underwater_sway ||
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
    if (cutout_surface) {
        // Les panneaux et rubans possèdent déjà des UV canoniques. Un
        // échantillonnage triplanaire en espace monde mélangeait trois masques
        // alpha différents et faisait nager ou cribler leur silhouette.
        primary_sample = sample_cutout_surface(primary_layer);
        secondary_sample = primary_sample;
        blend = 0.0;
    } else if (primary_material_only) {
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

    // Je garde la luminance brute du texel avant la teinte Backrooms : elle
    // distingue le tube clair de son cadre sombre pour le repli d'émission.
    float primary_source_peak = max(
        max(primary_sample.albedo.r, primary_sample.albedo.g),
        primary_sample.albedo.b);
    vec4 primary_albedo = primary_sample.albedo;
    vec4 secondary_albedo = secondary_sample.albedo;
    primary_albedo.rgb = tint_backrooms_material(
        primary_albedo.rgb,
        v_primary_block);
    secondary_albedo.rgb = tint_backrooms_material(
        secondary_albedo.rgb,
        v_secondary_block);
    vec4 primary_orm = primary_sample.orm;
    vec4 secondary_orm = secondary_sample.orm;
    vec3 albedo = mix(primary_albedo.rgb, secondary_albedo.rgb, blend);
    vec4 orm = mix(primary_orm, secondary_orm, blend);
    if (marine_fish) {
        float palette_index =
            floor(saturate(v_block_light) * 3.0 + 0.5);
        vec3 palette_tint =
            palette_index < 0.5
                ? vec3(0.72, 1.04, 1.08)
                : (palette_index < 1.5
                       ? vec3(1.04, 1.00, 0.82)
                       : (palette_index < 2.5
                              ? vec3(1.08, 0.76, 0.62)
                              : vec3(0.76, 0.84, 1.12)));
        albedo *= palette_tint;
    }
    if (submerged_surface) {
        float depth_tint =
            smoothstep(2.0, 36.0, water_depth) *
            smoothstep(0.02, 0.30, saturate(v_sky_light));
        float depth_tint_strength =
            marine_surface ? 0.42 : 0.30;
        albedo *= mix(
            vec3(0.98, 1.02, 1.01),
            vec3(0.62, 0.84, 0.88),
            depth_tint * depth_tint_strength);
    }
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
    if (cutout_surface && coverage < 0.46) {
        discard;
    }
)";

inline constexpr auto* kModernTerrainFragmentShaderSourcePart3 = R"(
    // Je désactive entièrement les lectures de normales en Low. En Medium et
    // High, je garde le relief existant mais je n'échantillonne qu'une seule
    // couche lorsque le fast path matériau a déjà établi un poids extrême.
    vec3 normal = geometric_normal;
    if (!cutout_surface &&
        u_material_detail_scale >= k_normal_mapping_detail_threshold) {
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
    float poolrooms_wetness =
        mix(
            v_primary_block == 54u ? 1.0 : 0.0,
            v_secondary_block == 54u ? 1.0 : 0.0,
            blend);
    wetness = max(wetness, poolrooms_wetness);
    float enclosed_interior =
        u_enclosed_interior != 0 ? 1.0 : 0.0;
    float raw_occlusion = saturate(orm.r * v_ao);
    float occlusion_floor = architectural_surface ? 0.24 : 0.30;
    if (geological_surface) {
        occlusion_floor = 0.34;
    }
    if (vegetation_surface) {
        occlusion_floor = 0.52;
    }
    // Je conserve des contacts lisibles dans un espace ferme : l'occlusion
    // structure les angles sans supprimer toute l'irradiance des plafonniers.
    occlusion_floor = mix(
        occlusion_floor,
        max(occlusion_floor, 0.56),
        enclosed_interior);
    float occlusion = mix(occlusion_floor, 1.0, raw_occlusion);
    float sampled_roughness =
        mix(orm.g, orm.g * 0.48, wetness);
    float roughness =
        clamp(
            mix(
                backrooms_material_roughness(
                    v_primary_block,
                    sampled_roughness),
                backrooms_material_roughness(
                    v_secondary_block,
                    sampled_roughness),
                blend),
            0.06,
            1.0);
    float metallic =
        saturate(
            mix(
                backrooms_material_metallic(
                    v_primary_block,
                    orm.b),
                backrooms_material_metallic(
                    v_secondary_block,
                    orm.b),
                blend));
    float emission = saturate(orm.a);
    if (v_primary_block == 50u || v_primary_block == 52u) {
        // Je garde les tubes actifs lisibles lorsque la mip lointaine filtre
        // leur émission. Le masque du texel empêche le cadre de rayonner; le
        // plafond (49) et le tube cassé (51) restent entièrement éteints.
        float source_mask =
            smoothstep(0.52, 0.82, primary_source_peak);
        emission = max(
            emission,
            (v_primary_block == 50u ? 0.92 : 0.82) *
                source_mask);
    } else if (v_primary_block == 58u) {
        float source_mask =
            smoothstep(0.48, 0.78, primary_source_peak);
        emission =
            max(
                emission,
                0.96 * source_mask);
    }

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
    vec2 backrooms_flicker =
        backrooms_flicker_scales(
            v_world_position);
    float local_light =
        saturate(v_block_light) *
        backrooms_flicker.x;
    float smooth_local_light =
        local_light * local_light * (3.0 - 2.0 * local_light);
    float softened_local_light = mix(
        local_light,
        smooth_local_light,
        enclosed_interior);
    vec3 ambient_albedo = albedo;
    if (vegetation_surface) {
        // Je relève uniquement le terme diffus des matières végétales sombres :
        // leur albédo et leur identité restent inchangés sous la lumière directe.
        ambient_albedo = max(ambient_albedo, vec3(0.055));
    }
    float hemisphere = saturate(geometric_normal.y * 0.5 + 0.5);
    float ambient_distribution = mix(
        mix(0.50, 1.08, sky_exposure),
        mix(0.92, 1.04, hemisphere),
        enclosed_interior);
    vec3 ambient = u_ambient_color * ambient_albedo *
                   ambient_distribution * occlusion;
    vec3 bounce_tint = mix(
        vec3(0.22, 0.13, 0.075),
        max(u_ambient_color, vec3(0.055, 0.065, 0.085)),
        hemisphere);
    vec3 bounce = ambient_albedo * bounce_tint *
                  (0.075 + 0.075 * daylight) *
                  mix(0.72, 1.0, sky_exposure) *
                  occlusion;
    float interior_distribution =
        mix(0.72, 1.12, smoothstep(-0.2, 1.0, geometric_normal.y));
    vec3 interior_bounce =
        ambient_albedo * u_block_light_color *
        enclosed_interior *
        (0.090 * softened_local_light) *
        interior_distribution *
        mix(0.78, 1.0, occlusion);
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
    vec3 torch = marine_fish
        ? vec3(0.0)
        : u_block_light_color * softened_local_light *
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
    vec3 color =
        ambient + bounce + interior_bounce + direct + torch + rim_light;
    float flashlight_energy =
        backrooms_flashlight_irradiance(
            v_world_position);
    if (flashlight_energy > 0.0) {
        float flashlight_incidence =
            max(
                dot(
                    normal,
                    view_direction),
                0.0);
        float contact_visibility =
            mix(
                0.78,
                1.0,
                occlusion);
        vec3 flashlight_radiance =
            vec3(1.00, 0.92, 0.76) *
            (3.60 * flashlight_energy);
        // Je rends le faisceau F identique dans les deux pipelines : son coeur
        // revele franchement la piece, tandis que le spill reste localise.
        color +=
            albedo *
            flashlight_radiance *
            mix(
                0.20,
                1.0,
                flashlight_incidence) *
            contact_visibility;
    }
    if (submerged_surface &&
        u_material_detail_scale >=
            k_normal_mapping_detail_threshold &&
        u_storm_intensity < 0.82) {
        // Je reserve les caustiques aux surfaces ouvertes sur la colonne d'eau.
        // La lumiere du ciel les annule dans les grottes et la qualite Low
        // evite entierement leurs oscillations. Sous une forte tempete, je les
        // supprime : le couvert nuageux les rend imperceptibles et j'epargne
        // les fonctions trigonometriques sur tout le fond visible.
        float caustic_a =
            sin(
                v_world_position.x * 0.72 +
                v_world_position.z * 0.43 +
                u_time_seconds * 0.82);
        float caustic_b =
            sin(
                v_world_position.x * -0.37 +
                v_world_position.z * 0.81 -
                u_time_seconds * 0.61);
        float caustic_pattern =
            smoothstep(
                0.58,
                1.55,
                clamp(
                    caustic_a + caustic_b,
                    -2.0,
                    2.0));
        float caustic =
            clamp(
                caustic_pattern *
                    submerged_caustic_envelope(
                        water_depth,
                        daylight) *
                    saturate(v_sky_light) *
                    (marine_fish ? 0.035 : 0.085),
                0.0,
                k_submerged_caustic_max_energy);
        color +=
            albedo *
            vec3(0.42, 0.72, 0.68) *
            caustic;
    }
    vec3 emission_color =
        albedo * vec3(1.35, 0.74, 0.28);
    if (v_primary_block == 50u) {
        emission_color = vec3(0.72, 1.02, 0.76);
    } else if (v_primary_block == 52u) {
        emission_color = vec3(1.12, 0.055, 0.025);
    } else if (v_primary_block == 58u) {
        emission_color = vec3(0.54, 0.98, 1.04);
    }
    color +=
        emission_color *
        emission *
        mix(
            1.0,
            backrooms_flicker.y,
            enclosed_interior);
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
    readability_energy = mix(
        readability_energy,
        0.15 + 0.08 * softened_local_light,
        enclosed_interior);
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
    fog = max(fog, maritime_horizon_haze * 0.08);
    if (u_interior_fog_range.x >= 0.0) {
        // Je rends le fond ferme totalement opaque avant le premier anneau
        // qui n'est pas encore garanti sur le GPU. Une plage nulle signifie
        // que je masque immediatement toute geometrie non garantie.
        float interior_distance =
            length(v_world_position.xz - u_camera_position.xz);
        float interior_terminal_fog =
            u_interior_fog_range.y > u_interior_fog_range.x
                ? smoothstep(
                      u_interior_fog_range.x,
                      u_interior_fog_range.y,
                      interior_distance)
                : 1.0;
        fog = max(fog, interior_terminal_fog);
    }
    float horizon = smoothstep(0.0, 1.0, 1.0 - abs(normalize(v_world_position - u_camera_position).y));
    vec3 atmospheric_color = mix(u_fog_color, u_distant_fog_color, saturate(fog + horizon * 0.08));
    bool underwater_volume =
        u_maritime_horizon_enabled != 0 &&
        u_maritime_submersion_active != 0 &&
        v_world_position.y < u_maritime_sea_level + 0.25;
    if (underwater_volume) {
        // Je termine le vrai fond marin dans un volume coloré avant sa limite
        // de streaming. Aucun proxy grossier ni bord de chunk ne peut émerger.
        float underwater_fog_end =
            u_material_detail_scale <
                    k_normal_mapping_detail_threshold
                ? 24.0
                : 40.0;
        float underwater_fog_start =
            max(
                underwater_fog_end - 24.0,
                0.0);
        float underwater_distance =
            length(
                v_world_position.xz -
                    u_camera_position.xz);
        float underwater_terminal_fog =
            smoothstep(
                underwater_fog_start,
                underwater_fog_end,
                underwater_distance);
        fog =
            max(
                fog,
                underwater_terminal_fog);
        vec3 deep_water =
            vec3(0.012, 0.060, 0.085);
        vec3 lit_water =
            vec3(0.025, 0.112, 0.140);
        atmospheric_color =
            mix(
                deep_water,
                lit_water,
                daylight * 0.42);
        atmospheric_color =
            mix(
                atmospheric_color,
                deep_water,
                saturate(u_storm_intensity) * 0.24);
    }
    color = mix(color, atmospheric_color, saturate(fog));
    float output_alpha = 1.0;
    if (transparent_surface) {
        color = mix(color, u_fog_color + vec3(0.08, 0.13, 0.16), 0.30);
        output_alpha = clamp(0.30 + coverage * 0.28, 0.30, 0.58);
    }
    color *=
        backrooms_darkness_visibility(
            local_light,
            flashlight_energy);
    frag_color = vec4(max(color, vec3(0.0)), output_alpha);
}
)";

inline const std::string kModernTerrainFragmentShaderSource = [] {
    std::string source;
    source.reserve(
        std::char_traits<char>::length(
            kModernTerrainFragmentShaderSourcePart1) +
        std::char_traits<char>::length(
            kModernTerrainFragmentShaderSourcePart1Materials) +
        std::char_traits<char>::length(
            kModernTerrainFragmentShaderSourcePart2) +
        std::char_traits<char>::length(
            kModernTerrainFragmentShaderSourcePart2Backrooms) +
        std::char_traits<char>::length(
            kModernTerrainFragmentShaderSourcePart3));
    source +=
        kModernTerrainFragmentShaderSourcePart1;
    source +=
        kModernTerrainFragmentShaderSourcePart1Materials;
    source +=
        kModernTerrainFragmentShaderSourcePart2;
    source +=
        kModernTerrainFragmentShaderSourcePart2Backrooms;
    source +=
        kModernTerrainFragmentShaderSourcePart3;
    return source;
}();

inline constexpr std::string_view kModernTerrainShadowVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 2) in uvec4 a_material_ao;
layout(location = 4) in uint a_surface_flags;

uniform mat4 u_model;
uniform mat4 u_light_view_projection;

out vec2 v_surface_uv;
flat out uint v_primary_block;
flat out uint v_surface_flags;

void main() {
    v_surface_uv = vec2(a_material_ao.yz) / 255.0;
    v_primary_block = a_material_ao.x;
    v_surface_flags = a_surface_flags;
    gl_Position =
        u_light_view_projection *
        u_model *
        vec4(a_position, 1.0);
}
)";

inline constexpr std::string_view kModernTerrainShadowFragmentShaderSource = R"(#version 330 core
in vec2 v_surface_uv;
flat in uint v_primary_block;
flat in uint v_surface_flags;

uniform sampler2DArray u_material_albedo;

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
    if (block_id >= 53u && block_id <= 55u) return 45.0;
    if (block_id == 56u || block_id == 59u ||
        block_id == 61u) return 36.0;
    if (block_id == 57u || block_id == 63u) return 45.0;
    if (block_id == 58u) return 38.0;
    if (block_id == 60u) return 32.0;
    if (block_id == 62u) return 5.0;
    return 2.0;
}

vec2 clamped_cutout_uv() {
    vec2 texture_size = max(
        vec2(textureSize(u_material_albedo, 0).xy),
        vec2(1.0));
    vec2 half_texel = 0.5 / texture_size;
    return clamp(
        v_surface_uv,
        half_texel,
        vec2(1.0) - half_texel);
}

void main() {
    if ((v_surface_flags & 1u) == 0u) {
        return;
    }

    float coverage = texture(
        u_material_albedo,
        vec3(
            clamped_cutout_uv(),
            material_layer(v_primary_block))).a;
    if (coverage < 0.46) {
        discard;
    }
}
)";

} // namespace valcraft
