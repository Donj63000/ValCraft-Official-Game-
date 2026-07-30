#include "render/ModernWaterShaderSource.h"
#include "render/OceanVisualShaderSource.h"
#include "render/OceanVisuals.h"
#include "render/SkyShaderSource.h"

#include <doctest/doctest.h>

#include <limits>
#include <string>
#include <string_view>

namespace valcraft {

TEST_CASE("le module oceanique reste partageable sans ressource GPU") {
    const auto source =
        kOceanVisualShaderSource;

    CHECK(
        source.find(
            "#version") ==
        std::string_view::npos);
    CHECK(
        source.find(
            "uniform ") ==
        std::string_view::npos);
    CHECK(
        source.find(
            "sampler") ==
        std::string_view::npos);
    CHECK(
        source.find(
            "texture(") ==
        std::string_view::npos);
    CHECK(
        source.find(
            "k_ocean_visual_water_f0 = 0.020") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "vec3(0.120, 0.055, 0.025)") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "ocean_visual_fresnel_schlick") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "ocean_visual_safe_normalize") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "ocean_visual_atlantic_body_color") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "ocean_visual_atlantic_shallow_color") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "ocean_visual_reflected_sky") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "ocean_visual_surface_radiance") !=
        std::string_view::npos);
}

TEST_CASE("la normale lointaine reprend exactement deux houles du CPU") {
    const auto source =
        kOceanVisualShaderSource;

    CHECK(
        source.find(
            "vec4 wave_a") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "vec2 phase_data_a") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "vec4 wave_b") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "vec2 phase_data_b") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "0.14 *") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "2.0 * harmonic_a") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "2.0 * harmonic_b") !=
        std::string_view::npos);
}

TEST_CASE("le ciel maritime compose le meme ocean Atlantique avant main") {
    const std::string_view source {
        kSkyFragmentShaderSource,
    };
    const auto shared_begin =
        source.find(
            "const float k_ocean_visual_water_f0");
    const auto main_begin =
        source.find(
            "void main()");

    REQUIRE(
        shared_begin !=
        std::string_view::npos);
    REQUIRE(
        main_begin !=
        std::string_view::npos);
    CHECK(
        shared_begin <
        main_begin);
    CHECK(
        source.find(
            "ocean_visual_far_wave_normal(") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "ocean_visual_reflected_sky(") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "ocean_visual_surface_radiance(") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "u_sun_disk_color") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "u_moon_disk_color") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "uniform vec4 u_ocean_horizon_waves[2]") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "uniform vec2 u_ocean_horizon_wave_phases[2]") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "u_ocean_horizon_severity") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "u_ocean_horizon_tempest_factor") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "u_ocean_horizon_sun_color") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "violent_storm_factor") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "u_lightning_intensity") !=
        std::string_view::npos);
    CHECK(
        source.find(
            "vec3 clear_ocean") ==
        std::string_view::npos);
    CHECK(
        source.find(
            "u_distant_fog_color,\n"
            "                terminal_fog") !=
        std::string_view::npos);
}

TEST_CASE("les profils de qualite bornent les lectures de normales d'eau") {
    CHECK(water_detail_sample_count(1.0F) == 2);
    CHECK(water_detail_sample_count(0.70F) == 1);
    CHECK(water_detail_sample_count(0.30F) == 0);
    CHECK(
        water_detail_sample_count(
            std::numeric_limits<float>::quiet_NaN()) ==
        0);
    CHECK(water_wake_enabled(1.0F));
    CHECK(water_wake_enabled(0.70F));
    CHECK_FALSE(water_wake_enabled(0.30F));
}

TEST_CASE("l'immersion maritime reste finie et exclut les espaces secs") {
    CHECK(
        resolve_maritime_submersion_state(
            false,
            true,
            true,
            48.0F,
            49.0F) ==
        MaritimeSubmersionState {});
    CHECK(
        resolve_maritime_submersion_state(
            true,
            false,
            true,
            48.0F,
            49.0F) ==
        MaritimeSubmersionState {});
    CHECK(
        resolve_maritime_submersion_state(
            true,
            true,
            false,
            48.0F,
            49.0F) ==
        MaritimeSubmersionState {});
    CHECK(
        resolve_maritime_submersion_state(
            true,
            true,
            true,
            49.0F,
            48.9F) ==
        MaritimeSubmersionState {});
    CHECK(
        resolve_maritime_submersion_state(
            true,
            true,
            true,
            49.0F,
            49.005F) ==
        MaritimeSubmersionState {});

    const auto submerged =
        resolve_maritime_submersion_state(
            true,
            true,
            true,
            46.5F,
            49.0F);
    CHECK(submerged.active);
    CHECK(submerged.depth == doctest::Approx(2.5F));
    CHECK(submerged.blend == doctest::Approx(1.0F));
    CHECK(sanitized_ship_speed(-4.0F) == doctest::Approx(0.0F));
    CHECK(sanitized_ship_speed(40.0F) == doctest::Approx(24.0F));
    CHECK(
        sanitized_ship_speed(
            std::numeric_limits<float>::infinity()) ==
        doctest::Approx(0.0F));
}

TEST_CASE("le shader d'eau moderne garde ses invariants visuels et physiques") {
    const std::string_view vertex =
        kModernWaterVertexShaderSource;
    const std::string fragment =
        modern_water_fragment_shader_source();
    const auto shared_begin =
        fragment.find(
            "const float k_ocean_visual_water_f0");
    const auto main_begin =
        fragment.find(
            "void main()");

    CHECK(
        vertex.find(
            "uniform vec4 u_ocean_waves[6]") !=
        std::string_view::npos);
    CHECK(
        vertex.find(
            "harmonic =\n            0.14") !=
        std::string_view::npos);
    REQUIRE(
        shared_begin !=
        std::string::npos);
    REQUIRE(
        main_begin !=
        std::string::npos);
    CHECK(
        shared_begin <
        main_begin);
    CHECK(
        fragment.find(
            "ocean_visual_fresnel_schlick(\n"
            "            view_alignment)") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "-k_ocean_visual_absorption *") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "ocean_visual_atlantic_shallow_color(") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "ocean_visual_atlantic_body_color(") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "ocean_visual_reflected_sky(") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "ocean_visual_surface_radiance(") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "const vec3 k_atlantic_") ==
        std::string::npos);
    CHECK(
        fragment.find(
            "vec3 atlantic_sky_reflection(") ==
        std::string::npos);
    CHECK(
        fragment.find(
            "sampler2DArray u_material_normal_height") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "u_water_detail_samples > 1") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "if (u_water_detail_samples <= 0)") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "uniform float u_water_animation_time") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "u_water_animation_time * 0.42") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "u_time_of_day") ==
        std::string::npos);
    CHECK(
        fragment.find(
            "uniform vec4 u_ocean_waves[6]") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "uniform vec2 u_ocean_wave_phases[6]") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "mix(\n"
            "                geometric_normal,\n"
            "                ocean_surface_normal,\n"
            "                surface_mask)") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "ocean_visual_far_wave_normal(\n"
            "                v_world_position.xz,\n"
            "                u_ocean_waves[0],\n"
            "                u_ocean_wave_phases[0],\n"
            "                u_ocean_waves[1],\n"
            "                u_ocean_wave_phases[1])") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "float ship_wake_mask") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "ship_excludes_ocean(v_world_position)") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "u_ship_speed <= 0.05") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "smoothstep(0.15, 2.40, u_ship_speed)") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "if (u_water_surface_detail < 0.45)") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "if (u_water_surface_detail > 0.85)") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "if (foam_detail >= 0.45)") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "float rain_impact_foam = 0.0") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "fwidth(rain_signal)") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "rain_impact_foam * 0.26") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "(0.78 + 0.34 * daylight)") !=
        std::string::npos);
    CHECK(
        fragment.find(
            "u_maritime_water_blend_range") !=
        std::string::npos);
}

} // namespace valcraft
