#include "app/PerformanceReport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

using namespace valcraft;

TEST_CASE("performance report schema v3 exposes stable render categories") {
    CHECK(PerformanceRunReport::kSchemaVersion == 3);
    CHECK(PerformanceRunReport::kMinimumSupportedSchemaVersion == 2);
    CHECK_FALSE(is_supported_performance_report_schema(1));
    CHECK(is_supported_performance_report_schema(2));
    CHECK(is_supported_performance_report_schema(3));
    CHECK_FALSE(is_supported_performance_report_schema(4));

    constexpr std::array<std::string_view, 9> expected_names {{
        "terrain",
        "vegetation",
        "entities",
        "ship",
        "water",
        "atmosphere",
        "post_process",
        "ui",
        "shadows",
    }};
    for (std::size_t index = 0; index < expected_names.size(); ++index) {
        CHECK(
            performance_render_category_name(
                kPerformanceRenderCategories[index]) ==
            expected_names[index]);
    }
    CHECK(
        performance_render_category_name(
            PerformanceRenderCategory::Count) ==
        "unknown");
    CHECK(
        performance_render_category_name(
            static_cast<PerformanceRenderCategory>(99U)) ==
        "unknown");
    CHECK(
        format_performance_checksum(0x0123456789ABCDEFULL) ==
        "0x0123456789abcdef");
}

TEST_CASE("performance report v3 summarizes every render category deterministically") {
    PerformanceReportMetadata metadata {};
    metadata.platform = "test";
    metadata.scenario = "render-categories";
    metadata.visual_pipeline = "modern";
    metadata.material_pack_version = 7U;
    metadata.material_pack_checksum = 0x0123456789ABCDEFULL;

    std::vector<FramePerformanceSample> samples(3);
    for (std::size_t sample_index = 0;
         sample_index < samples.size();
         ++sample_index) {
        auto& sample = samples[sample_index];
        sample.frame_index = sample_index + 10U;
        sample.frame_total_ms =
            8.0 + static_cast<double>(sample_index);
        sample.world_ms =
            3.0 + 2.0 * static_cast<double>(sample_index);
        sample.gpu_world_ms =
            4.0 + 2.0 * static_cast<double>(sample_index);
        sample.gpu_frame_ms =
            6.0 + 2.0 * static_cast<double>(sample_index);
        const auto split_sample =
            static_cast<double>(sample_index + 1U);
        sample.gpu_water_resolve_ms =
            0.10 * split_sample;
        sample.gpu_water_surface_ms =
            0.50 + 0.25 * static_cast<double>(sample_index);
        sample.gpu_transparent_weather_ms =
            0.05 * split_sample;
        sample.gpu_water_ms =
            sample.gpu_water_resolve_ms +
            sample.gpu_water_surface_ms +
            sample.gpu_transparent_weather_ms;
        sample.gpu_timing_valid = true;

        for (std::size_t category_index = 0;
             category_index < kPerformanceRenderCategories.size();
             ++category_index) {
            const auto category =
                kPerformanceRenderCategories[category_index];
            const auto factor =
                static_cast<double>(category_index + 1U);
            const auto sample_number =
                static_cast<double>(sample_index + 1U);
            sample.render_category_cpu_ms[category] =
                factor * sample_number;
            sample.render_category_gpu_ms[category] =
                factor * sample_number * 2.0;
        }
    }

    const auto report =
        build_performance_report(metadata, samples, true, 3U);
    const auto repeated_report =
        build_performance_report(metadata, samples, true, 3U);

    CHECK(report.schema_version == 3);
    CHECK(report.metadata.visual_pipeline == "modern");
    CHECK(report.metadata.material_pack_version == 7U);
    CHECK(
        report.metadata.material_pack_checksum ==
        0x0123456789ABCDEFULL);
    CHECK(report.summary.gpu_timing_samples == 3U);
    CHECK(
        report.summary.render_categories
                .cpu(PerformanceRenderCategory::Terrain)
                .average ==
        doctest::Approx(2.0));
    CHECK(
        report.summary.render_categories
                .cpu(PerformanceRenderCategory::Terrain)
                .p50 ==
        doctest::Approx(2.0));
    CHECK(
        report.summary.render_categories
                .cpu(PerformanceRenderCategory::Terrain)
                .p95 ==
        doctest::Approx(2.9));
    CHECK(
        report.summary.render_categories
                .cpu(PerformanceRenderCategory::Terrain)
                .p99 ==
        doctest::Approx(2.98));
    CHECK(
        report.summary.render_categories
                .gpu(PerformanceRenderCategory::Shadows)
                .average ==
        doctest::Approx(36.0));
    CHECK(
        report.summary.render_categories
                .gpu(PerformanceRenderCategory::Shadows)
                .maximum ==
        doctest::Approx(54.0));

    // Je garde aussi les statistiques historiques pour comparer les anciens
    // rapports sans traduction ni perte de precision.
    CHECK(report.summary.world_ms.average == doctest::Approx(5.0));
    CHECK(report.summary.gpu_world_ms.average == doctest::Approx(6.0));
    CHECK(
        report.summary.gpu_water_ms.average ==
        doctest::Approx(1.05));
    CHECK(
        report.summary.gpu_water_resolve_ms.average ==
        doctest::Approx(0.20));
    CHECK(
        report.summary.gpu_water_surface_ms.average ==
        doctest::Approx(0.75));
    CHECK(
        report.summary.gpu_water_surface_ms.p95 ==
        doctest::Approx(0.975));
    CHECK(
        report.summary.gpu_transparent_weather_ms.average ==
        doctest::Approx(0.10));

    const auto json = format_performance_json(report);
    CHECK(json == format_performance_json(repeated_report));
    CHECK(json.find("\"schema_version\": 3") != std::string::npos);
    CHECK(
        json.find("\"visual_pipeline\": \"modern\"") !=
        std::string::npos);
    CHECK(
        json.find("\"material_pack_version\": 7") !=
        std::string::npos);
    CHECK(
        json.find(
            "\"material_pack_checksum\": "
            "\"0x0123456789abcdef\"") !=
        std::string::npos);
    CHECK(
        json.find("\"render_categories\": {") !=
        std::string::npos);
    CHECK(json.find("\"gpu_world_ms\": {") != std::string::npos);
    CHECK(
        json.find("\"gpu_water_resolve_ms\": {") !=
        std::string::npos);
    CHECK(
        json.find("\"gpu_water_surface_ms\": {") !=
        std::string::npos);
    CHECK(
        json.find("\"gpu_transparent_weather_ms\": {") !=
        std::string::npos);
    CHECK(
        json.find("\"gpu_water_surface_ms\": 0.500") !=
        std::string::npos);
    for (const auto category : kPerformanceRenderCategories) {
        const auto category_key =
            "\"" +
            std::string(performance_render_category_name(category)) +
            "\": {\"cpu_ms\":";
        CHECK(json.find(category_key) != std::string::npos);
    }

    const auto text = format_performance_report(report);
    CHECK(text.find("visual_pipeline=modern") != std::string::npos);
    CHECK(
        text.find("material_pack_checksum=0x0123456789abcdef") !=
        std::string::npos);
    CHECK(
        text.find("render_categories_cpu_ms_avg") !=
        std::string::npos);
    CHECK(
        text.find("render_categories_gpu_ms_avg") !=
        std::string::npos);
    CHECK(text.find("water_resolve=") != std::string::npos);
    CHECK(text.find("water_surface=") != std::string::npos);
    CHECK(text.find("transparent_weather=") != std::string::npos);
}

TEST_CASE("performance report v3 maps historical render passes without double counting") {
    FramePerformanceSample sample {};
    sample.frame_total_ms = 12.0;
    sample.world_ms = 2.0;
    sample.shadow_ms = 0.5;
    sample.gpu_world_ms = 3.0;
    sample.gpu_entities_ms = 1.0;
    sample.gpu_water_ms = 2.0;
    sample.gpu_sky_ms = 0.7;
    sample.gpu_post_process_ms = 0.4;
    sample.gpu_hud_ms = 0.2;
    sample.gpu_shadow_ms = 0.6;
    sample.gpu_timing_valid = true;

    const auto report =
        build_performance_report({}, {sample}, true, 1U);

    CHECK(
        report.summary.render_categories
                .cpu(PerformanceRenderCategory::Terrain)
                .average ==
        doctest::Approx(2.0));
    CHECK(
        report.summary.render_categories
                .cpu(PerformanceRenderCategory::Shadows)
                .average ==
        doctest::Approx(0.5));
    CHECK(
        report.summary.render_categories
                .gpu(PerformanceRenderCategory::Terrain)
                .average ==
        doctest::Approx(3.0));
    CHECK(
        report.summary.render_categories
                .gpu(PerformanceRenderCategory::Entities)
                .average ==
        doctest::Approx(1.0));
    CHECK(
        report.summary.render_categories
                .gpu(PerformanceRenderCategory::Water)
                .average ==
        doctest::Approx(2.0));
    CHECK(
        report.summary.gpu_water_ms.average ==
        doctest::Approx(2.0));
    CHECK(
        report.summary.gpu_water_resolve_ms.average ==
        doctest::Approx(0.0));
    CHECK(
        report.summary.gpu_water_surface_ms.average ==
        doctest::Approx(0.0));
    CHECK(
        report.summary.gpu_transparent_weather_ms.average ==
        doctest::Approx(0.0));
    CHECK(
        report.summary.render_categories
                .gpu(PerformanceRenderCategory::Atmosphere)
                .average ==
        doctest::Approx(0.7));
    CHECK(
        report.summary.render_categories
                .gpu(PerformanceRenderCategory::PostProcess)
                .average ==
        doctest::Approx(0.4));
    CHECK(
        report.summary.render_categories
                .gpu(PerformanceRenderCategory::Ui)
                .average ==
        doctest::Approx(0.2));
    CHECK(
        report.summary.render_categories
                .gpu(PerformanceRenderCategory::Shadows)
                .average ==
        doctest::Approx(0.6));
    CHECK(
        report.summary.render_categories
                .gpu(PerformanceRenderCategory::Vegetation)
                .average ==
        doctest::Approx(0.0));
    CHECK(
        report.summary.render_categories
                .gpu(PerformanceRenderCategory::Ship)
                .average ==
        doctest::Approx(0.0));
}

TEST_CASE("performance report v3 sanitizes invalid category durations") {
    PerformanceReportMetadata metadata {};
    metadata.visual_pipeline = "untrusted-pipeline";

    FramePerformanceSample sample {};
    sample.frame_total_ms = 1.0;
    sample.gpu_timing_valid = true;
    sample.render_category_cpu_ms[
        PerformanceRenderCategory::Terrain] =
        std::numeric_limits<double>::quiet_NaN();
    sample.render_category_cpu_ms[
        PerformanceRenderCategory::Vegetation] =
        std::numeric_limits<double>::infinity();
    sample.render_category_cpu_ms[
        PerformanceRenderCategory::Entities] = -12.0;
    sample.render_category_gpu_ms[
        PerformanceRenderCategory::Terrain] =
        std::numeric_limits<double>::quiet_NaN();
    sample.render_category_gpu_ms[
        PerformanceRenderCategory::Vegetation] =
        std::numeric_limits<double>::infinity();
    sample.render_category_gpu_ms[
        PerformanceRenderCategory::Entities] = -4.0;

    const auto report =
        build_performance_report(metadata, {sample}, true, 1U);

    CHECK(report.metadata.visual_pipeline == "unknown");
    REQUIRE(report.frames.size() == 1U);
    for (const auto category : kPerformanceRenderCategories) {
        const auto& cpu =
            report.summary.render_categories.cpu(category);
        const auto& gpu =
            report.summary.render_categories.gpu(category);
        CHECK(std::isfinite(cpu.average));
        CHECK(std::isfinite(cpu.maximum));
        CHECK(std::isfinite(gpu.average));
        CHECK(std::isfinite(gpu.maximum));
        CHECK(cpu.average == doctest::Approx(0.0));
        CHECK(gpu.average == doctest::Approx(0.0));
        CHECK(
            report.frames.front()
                    .render_category_cpu_ms[category] ==
            doctest::Approx(0.0));
        CHECK(
            report.frames.front()
                    .render_category_gpu_ms[category] ==
            doctest::Approx(0.0));
    }

    auto json = format_performance_json(report);
    std::transform(
        json.begin(),
        json.end(),
        json.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    CHECK(json.find(": nan") == std::string::npos);
    CHECK(json.find(": inf") == std::string::npos);
    CHECK(json.find(": -inf") == std::string::npos);
}

TEST_CASE("schema v2 formatting preserves historical fields and omits v3 additions") {
    PerformanceReportMetadata metadata {};
    metadata.visual_pipeline = "modern";
    metadata.material_pack_version = 9U;
    metadata.material_pack_checksum = 0xFEDCBA9876543210ULL;

    FramePerformanceSample sample {};
    sample.frame_total_ms = 4.0;
    sample.gpu_world_ms = 1.25;
    sample.gpu_hud_ms = 0.25;
    sample.gpu_timing_valid = true;
    sample.render_category_cpu_ms[
        PerformanceRenderCategory::Terrain] = 2.0;
    sample.render_category_gpu_ms[
        PerformanceRenderCategory::Terrain] = 1.0;

    auto report =
        build_performance_report(metadata, {sample}, true, 1U);
    report.schema_version = 2;

    const auto json = format_performance_json(report);
    CHECK(json.find("\"schema_version\": 2") != std::string::npos);
    CHECK(json.find("\"gpu_world_ms\": {") != std::string::npos);
    CHECK(json.find("\"gpu_hud_ms\": {") != std::string::npos);
    CHECK(json.find("\"gpu_frame_ms\": {") != std::string::npos);
    CHECK(json.find("\"visual_pipeline\"") == std::string::npos);
    CHECK(json.find("\"material_pack_version\"") == std::string::npos);
    CHECK(json.find("\"material_pack_checksum\"") == std::string::npos);
    CHECK(json.find("\"render_categories\"") == std::string::npos);

    const auto text = format_performance_report(report);
    CHECK(text.find("gpu_ms_avg") != std::string::npos);
    CHECK(text.find("visual_pipeline=") == std::string::npos);
    CHECK(
        text.find("render_categories_cpu_ms_avg") ==
        std::string::npos);
}
