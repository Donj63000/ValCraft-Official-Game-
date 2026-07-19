#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace valcraft {

enum class PerformanceStage : std::size_t {
    EventProcessing = 0,
    Simulation,
    Audio,
    RenderPreparation,
    Streaming,
    Generation,
    Fluid,
    Lighting,
    Meshing,
    Upload,
    Shadow,
    World,
    RenderOverhead,
    Present,
    Telemetry,
    Unattributed,
};

enum class PerformanceEventKind {
    BlockBreak = 0,
    BlockPlace,
};

constexpr auto kPerformanceLagThreshold16Ms = 16.7;
constexpr auto kPerformanceLagThreshold33Ms = 33.3;
constexpr auto kPerformanceLagThreshold50Ms = 50.0;

inline constexpr std::array<PerformanceStage, 16> kPerformanceStages {{
    PerformanceStage::EventProcessing,
    PerformanceStage::Simulation,
    PerformanceStage::Audio,
    PerformanceStage::RenderPreparation,
    PerformanceStage::Streaming,
    PerformanceStage::Generation,
    PerformanceStage::Fluid,
    PerformanceStage::Lighting,
    PerformanceStage::Meshing,
    PerformanceStage::Upload,
    PerformanceStage::Shadow,
    PerformanceStage::World,
    PerformanceStage::RenderOverhead,
    PerformanceStage::Present,
    PerformanceStage::Telemetry,
    PerformanceStage::Unattributed,
}};

struct FramePerformanceSample {
    std::size_t frame_index = 0;
    double frame_total_ms = 0.0;
    double streaming_ms = 0.0;
    double generation_ms = 0.0;
    double lighting_ms = 0.0;
    double meshing_ms = 0.0;
    double upload_ms = 0.0;
    double shadow_ms = 0.0;
    double world_ms = 0.0;
    std::size_t generated_chunks = 0;
    std::size_t meshed_chunks = 0;
    std::size_t light_nodes_processed = 0;
    std::size_t uploaded_meshes = 0;
    std::size_t pending_generation = 0;
    std::size_t pending_mesh = 0;
    std::size_t pending_lighting = 0;
    std::size_t stream_chunk_changes = 0;
    std::size_t generation_enqueued = 0;
    std::size_t generation_pruned = 0;
    std::size_t unloaded_chunks = 0;
    std::size_t lighting_jobs_completed = 0;
    std::size_t visible_chunks = 0;
    std::size_t shadow_chunks = 0;
    std::size_t world_chunks = 0;
    PerformanceStage dominant_stage = PerformanceStage::Unattributed;
    double event_processing_ms = 0.0;
    double simulation_ms = 0.0;
    double audio_ms = 0.0;
    double render_preparation_ms = 0.0;
    double fluid_ms = 0.0;
    double render_cpu_ms = 0.0;
    double render_overhead_ms = 0.0;
    double present_ms = 0.0;
    double telemetry_ms = 0.0;
    double residual_ms = 0.0;
    double gpu_shadow_ms = 0.0;
    double gpu_world_ms = 0.0;
    double gpu_sky_ms = 0.0;
    double gpu_water_ms = 0.0;
    double gpu_entities_ms = 0.0;
    double gpu_post_process_ms = 0.0;
    double gpu_hud_ms = 0.0;
    double gpu_frame_ms = 0.0;
    std::size_t processed_fluid_cells = 0;
    std::size_t pending_fluid = 0;
    std::size_t fixed_updates = 0;
    std::size_t dropped_fixed_updates = 0;
    std::size_t draw_calls = 0;
    std::uint64_t triangles = 0;
    std::uint64_t uploaded_bytes = 0;
    std::uint64_t process_working_set_bytes = 0;
    std::uint64_t process_private_bytes = 0;
    std::uint64_t world_cpu_bytes = 0;
    std::uint64_t mesh_cpu_bytes = 0;
    std::uint64_t override_bytes = 0;
    std::uint64_t gpu_buffer_bytes = 0;
    std::uint64_t gpu_texture_bytes = 0;
    std::size_t gpu_source_frame = 0;
    std::size_t gpu_latency_frames = 0;
    bool gpu_timing_valid = false;
    std::uint8_t resolved_quality = 0;
    double adaptive_frame_ema_ms = 0.0;
    double adaptive_frame_p95_ms = 0.0;
};

struct PerformanceEvent {
    std::size_t frame_index = 0;
    PerformanceEventKind kind = PerformanceEventKind::BlockBreak;
    std::string label {};
    int world_x = 0;
    int world_y = 0;
    int world_z = 0;
    int chunk_x = 0;
    int chunk_z = 0;
    std::size_t pending_generation = 0;
    std::size_t pending_mesh = 0;
    std::size_t pending_lighting = 0;
};

struct PerformanceReportMetadata {
    std::string platform = "unknown";
    std::string build_type = "unknown";
    std::string capture_mode = "interactive";
    std::size_t smoke_frames = 0;
    std::size_t warmup_frames = 0;
    int stream_radius = 0;
    bool shadows_enabled = true;
    int shadow_map_size = 2048;
    int viewport_width = 0;
    int viewport_height = 0;
    bool post_process_enabled = true;
    bool freeze_time = false;
    std::string scenario = "default";
    std::string quality_profile = "fixed";
    std::string vsync_mode = "unknown";
    bool trace_included = false;
};

struct PerformanceMetricSummary {
    double average = 0.0;
    double maximum = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

struct PerformanceCounterSummary {
    double average = 0.0;
    std::uint64_t maximum = 0;
};

struct PerformanceLagBuckets {
    std::size_t over_16_7_ms = 0;
    std::size_t over_33_3_ms = 0;
    std::size_t over_50_0_ms = 0;
};

struct PerformanceReportSummary {
    std::size_t frame_count = 0;
    PerformanceMetricSummary frame_total_ms {};
    PerformanceMetricSummary event_processing_ms {};
    PerformanceMetricSummary simulation_ms {};
    PerformanceMetricSummary audio_ms {};
    PerformanceMetricSummary render_preparation_ms {};
    PerformanceMetricSummary streaming_ms {};
    PerformanceMetricSummary generation_ms {};
    PerformanceMetricSummary fluid_ms {};
    PerformanceMetricSummary lighting_ms {};
    PerformanceMetricSummary meshing_ms {};
    PerformanceMetricSummary upload_ms {};
    PerformanceMetricSummary shadow_ms {};
    PerformanceMetricSummary world_ms {};
    PerformanceMetricSummary render_cpu_ms {};
    PerformanceMetricSummary render_overhead_ms {};
    PerformanceMetricSummary present_ms {};
    PerformanceMetricSummary telemetry_ms {};
    PerformanceMetricSummary residual_ms {};
    PerformanceMetricSummary gpu_shadow_ms {};
    PerformanceMetricSummary gpu_world_ms {};
    PerformanceMetricSummary gpu_sky_ms {};
    PerformanceMetricSummary gpu_water_ms {};
    PerformanceMetricSummary gpu_entities_ms {};
    PerformanceMetricSummary gpu_post_process_ms {};
    PerformanceMetricSummary gpu_hud_ms {};
    PerformanceMetricSummary gpu_frame_ms {};
    PerformanceCounterSummary pending_generation {};
    PerformanceCounterSummary pending_mesh {};
    PerformanceCounterSummary pending_lighting {};
    PerformanceCounterSummary pending_fluid {};
    PerformanceCounterSummary visible_chunks {};
    PerformanceCounterSummary shadow_chunks {};
    PerformanceCounterSummary world_chunks {};
    PerformanceCounterSummary process_working_set_bytes {};
    PerformanceCounterSummary process_private_bytes {};
    PerformanceCounterSummary world_cpu_bytes {};
    PerformanceCounterSummary mesh_cpu_bytes {};
    PerformanceCounterSummary override_bytes {};
    PerformanceCounterSummary gpu_buffer_bytes {};
    PerformanceCounterSummary gpu_texture_bytes {};
    PerformanceLagBuckets lag_buckets {};
    std::size_t max_generated_chunks = 0;
    std::size_t max_meshed_chunks = 0;
    std::size_t max_light_nodes_processed = 0;
    std::size_t max_uploaded_meshes = 0;
    std::size_t max_processed_fluid_cells = 0;
    std::size_t max_fixed_updates = 0;
    std::size_t total_dropped_fixed_updates = 0;
    std::size_t max_draw_calls = 0;
    std::uint64_t max_triangles = 0;
    std::uint64_t max_uploaded_bytes = 0;
    std::size_t gpu_timing_samples = 0;
    std::size_t total_stream_chunk_changes = 0;
    std::size_t total_generation_enqueued = 0;
    std::size_t total_generation_pruned = 0;
    std::size_t total_unloaded_chunks = 0;
    std::size_t total_lighting_jobs_completed = 0;
};

struct PerformanceHotspotSummary {
    std::array<std::size_t, kPerformanceStages.size()> dominant_stage_counts {};
    PerformanceStage worst_frame_stage = PerformanceStage::Unattributed;
};

struct PerformanceEventSummary {
    std::size_t total_events = 0;
    std::size_t block_breaks = 0;
    std::size_t block_places = 0;
};

struct SpikeWindow {
    std::size_t start_frame = 0;
    std::size_t end_frame = 0;
    std::size_t peak_frame = 0;
    double peak_ms = 0.0;
    PerformanceStage dominant_stage = PerformanceStage::Unattributed;
};

struct PerformanceRunReport {
    static constexpr int kSchemaVersion = 2;

    int schema_version = kSchemaVersion;
    PerformanceReportMetadata metadata {};
    PerformanceReportSummary summary {};
    PerformanceHotspotSummary hotspots {};
    PerformanceEventSummary event_summary {};
    std::vector<FramePerformanceSample> worst_frames {};
    std::vector<SpikeWindow> spike_windows {};
    std::vector<FramePerformanceSample> frames {};
    std::vector<PerformanceEvent> events {};
};

inline auto performance_stage_name(PerformanceStage stage) -> std::string_view {
    switch (stage) {
    case PerformanceStage::EventProcessing:
        return "event_processing";
    case PerformanceStage::Simulation:
        return "simulation";
    case PerformanceStage::Audio:
        return "audio";
    case PerformanceStage::RenderPreparation:
        return "render_preparation";
    case PerformanceStage::Streaming:
        return "streaming";
    case PerformanceStage::Generation:
        return "generation";
    case PerformanceStage::Fluid:
        return "fluid";
    case PerformanceStage::Lighting:
        return "lighting";
    case PerformanceStage::Meshing:
        return "meshing";
    case PerformanceStage::Upload:
        return "upload";
    case PerformanceStage::Shadow:
        return "shadow";
    case PerformanceStage::World:
        return "world";
    case PerformanceStage::RenderOverhead:
        return "render_overhead";
    case PerformanceStage::Present:
        return "present";
    case PerformanceStage::Telemetry:
        return "telemetry";
    case PerformanceStage::Unattributed:
    default:
        return "unattributed";
    }
}

inline auto performance_event_kind_name(PerformanceEventKind kind) -> std::string_view {
    switch (kind) {
    case PerformanceEventKind::BlockBreak:
        return "block_break";
    case PerformanceEventKind::BlockPlace:
        return "block_place";
    default:
        return "unknown";
    }
}

inline auto detect_dominant_stage(const FramePerformanceSample& sample) -> PerformanceStage {
    const std::array<std::pair<PerformanceStage, double>, 15> measured_stages {{
        {PerformanceStage::EventProcessing, sample.event_processing_ms},
        {PerformanceStage::Simulation, sample.simulation_ms},
        {PerformanceStage::Audio, sample.audio_ms},
        {PerformanceStage::RenderPreparation, sample.render_preparation_ms},
        {PerformanceStage::Streaming, sample.streaming_ms},
        {PerformanceStage::Generation, sample.generation_ms},
        {PerformanceStage::Fluid, sample.fluid_ms},
        {PerformanceStage::Lighting, sample.lighting_ms},
        {PerformanceStage::Meshing, sample.meshing_ms},
        {PerformanceStage::Upload, sample.upload_ms},
        {PerformanceStage::Shadow, sample.shadow_ms},
        {PerformanceStage::World, sample.world_ms},
        {PerformanceStage::RenderOverhead, sample.render_overhead_ms},
        {PerformanceStage::Present, sample.present_ms},
        {PerformanceStage::Telemetry, sample.telemetry_ms},
    }};

    auto best_stage = PerformanceStage::Unattributed;
    auto best_value = 0.0;
    for (const auto& [stage, value] : measured_stages) {
        if (value > best_value) {
            best_stage = stage;
            best_value = value;
        }
    }
    if (sample.residual_ms > best_value) {
        return PerformanceStage::Unattributed;
    }
    return best_value > 0.0 ? best_stage : PerformanceStage::Unattributed;
}

inline auto percentile(std::vector<double> values, double fraction) -> double {
    if (values.empty()) {
        return 0.0;
    }

    std::sort(values.begin(), values.end());
    if (values.size() == 1) {
        return values.front();
    }

    const auto position = std::clamp(fraction, 0.0, 1.0) * static_cast<double>(values.size() - 1);
    const auto lower_index = static_cast<std::size_t>(std::floor(position));
    const auto upper_index = static_cast<std::size_t>(std::ceil(position));
    if (lower_index == upper_index) {
        return values[lower_index];
    }

    const auto weight = position - static_cast<double>(lower_index);
    return values[lower_index] + (values[upper_index] - values[lower_index]) * weight;
}

inline auto summarize_metric(const std::vector<double>& values) -> PerformanceMetricSummary {
    PerformanceMetricSummary summary {};
    if (values.empty()) {
        return summary;
    }

    summary.average = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    summary.maximum = *std::max_element(values.begin(), values.end());
    summary.p50 = percentile(values, 0.50);
    summary.p95 = percentile(values, 0.95);
    summary.p99 = percentile(values, 0.99);
    return summary;
}

template <typename Counter>
inline auto summarize_counter(const std::vector<Counter>& values) -> PerformanceCounterSummary {
    static_assert(std::is_integral_v<Counter> && std::is_unsigned_v<Counter>);
    PerformanceCounterSummary summary {};
    if (values.empty()) {
        return summary;
    }

    auto total = static_cast<long double>(0.0L);
    for (const auto value : values) {
        total += static_cast<long double>(value);
    }
    summary.average = static_cast<double>(total / static_cast<long double>(values.size()));
    summary.maximum = static_cast<std::uint64_t>(*std::max_element(values.begin(), values.end()));
    return summary;
}

inline auto build_spike_windows(const std::vector<FramePerformanceSample>& samples) -> std::vector<SpikeWindow> {
    std::vector<SpikeWindow> windows;
    std::size_t index = 0;
    while (index < samples.size()) {
        if (samples[index].frame_total_ms <= kPerformanceLagThreshold16Ms) {
            ++index;
            continue;
        }

        SpikeWindow window {};
        window.start_frame = samples[index].frame_index;
        window.end_frame = samples[index].frame_index;
        window.peak_frame = samples[index].frame_index;
        window.peak_ms = samples[index].frame_total_ms;
        window.dominant_stage = samples[index].dominant_stage;

        std::size_t current = index;
        while (current < samples.size() && samples[current].frame_total_ms > kPerformanceLagThreshold16Ms) {
            window.end_frame = samples[current].frame_index;
            if (samples[current].frame_total_ms > window.peak_ms) {
                window.peak_frame = samples[current].frame_index;
                window.peak_ms = samples[current].frame_total_ms;
                window.dominant_stage = samples[current].dominant_stage;
            }
            ++current;
        }

        windows.push_back(window);
        index = current;
    }

    return windows;
}

inline auto build_performance_report(const PerformanceReportMetadata& metadata,
                                     const std::vector<FramePerformanceSample>& raw_samples,
                                     bool include_full_trace,
                                     std::size_t worst_frame_count = 10,
                                     const std::vector<PerformanceEvent>& raw_events = {}) -> PerformanceRunReport {
    PerformanceRunReport report {};
    report.metadata = metadata;
    report.metadata.trace_included = include_full_trace;
    report.events = raw_events;
    report.event_summary.total_events = raw_events.size();
    for (const auto& event : raw_events) {
        switch (event.kind) {
        case PerformanceEventKind::BlockBreak:
            ++report.event_summary.block_breaks;
            break;
        case PerformanceEventKind::BlockPlace:
            ++report.event_summary.block_places;
            break;
        default:
            break;
        }
    }

    std::vector<FramePerformanceSample> samples = raw_samples;
    for (auto& sample : samples) {
        sample.dominant_stage = detect_dominant_stage(sample);
    }

    report.summary.frame_count = samples.size();
    if (samples.empty()) {
        return report;
    }

    std::vector<double> frame_total_values;
    std::vector<double> event_processing_values;
    std::vector<double> simulation_values;
    std::vector<double> audio_values;
    std::vector<double> render_preparation_values;
    std::vector<double> streaming_values;
    std::vector<double> generation_values;
    std::vector<double> fluid_values;
    std::vector<double> lighting_values;
    std::vector<double> meshing_values;
    std::vector<double> upload_values;
    std::vector<double> shadow_values;
    std::vector<double> world_values;
    std::vector<double> render_cpu_values;
    std::vector<double> render_overhead_values;
    std::vector<double> present_values;
    std::vector<double> telemetry_values;
    std::vector<double> residual_values;
    std::vector<double> gpu_shadow_values;
    std::vector<double> gpu_world_values;
    std::vector<double> gpu_sky_values;
    std::vector<double> gpu_water_values;
    std::vector<double> gpu_entities_values;
    std::vector<double> gpu_post_process_values;
    std::vector<double> gpu_hud_values;
    std::vector<double> gpu_frame_values;
    std::vector<std::size_t> pending_generation_values;
    std::vector<std::size_t> pending_mesh_values;
    std::vector<std::size_t> pending_lighting_values;
    std::vector<std::size_t> pending_fluid_values;
    std::vector<std::size_t> visible_chunk_values;
    std::vector<std::size_t> shadow_chunk_values;
    std::vector<std::size_t> world_chunk_values;
    std::vector<std::uint64_t> process_working_set_values;
    std::vector<std::uint64_t> process_private_values;
    std::vector<std::uint64_t> world_cpu_values;
    std::vector<std::uint64_t> mesh_cpu_values;
    std::vector<std::uint64_t> override_values;
    std::vector<std::uint64_t> gpu_buffer_values;
    std::vector<std::uint64_t> gpu_texture_values;

    frame_total_values.reserve(samples.size());
    event_processing_values.reserve(samples.size());
    simulation_values.reserve(samples.size());
    audio_values.reserve(samples.size());
    render_preparation_values.reserve(samples.size());
    streaming_values.reserve(samples.size());
    generation_values.reserve(samples.size());
    fluid_values.reserve(samples.size());
    lighting_values.reserve(samples.size());
    meshing_values.reserve(samples.size());
    upload_values.reserve(samples.size());
    shadow_values.reserve(samples.size());
    world_values.reserve(samples.size());
    render_cpu_values.reserve(samples.size());
    render_overhead_values.reserve(samples.size());
    present_values.reserve(samples.size());
    telemetry_values.reserve(samples.size());
    residual_values.reserve(samples.size());
    gpu_shadow_values.reserve(samples.size());
    gpu_world_values.reserve(samples.size());
    gpu_sky_values.reserve(samples.size());
    gpu_water_values.reserve(samples.size());
    gpu_entities_values.reserve(samples.size());
    gpu_post_process_values.reserve(samples.size());
    gpu_hud_values.reserve(samples.size());
    gpu_frame_values.reserve(samples.size());
    pending_generation_values.reserve(samples.size());
    pending_mesh_values.reserve(samples.size());
    pending_lighting_values.reserve(samples.size());
    pending_fluid_values.reserve(samples.size());
    visible_chunk_values.reserve(samples.size());
    shadow_chunk_values.reserve(samples.size());
    world_chunk_values.reserve(samples.size());
    process_working_set_values.reserve(samples.size());
    process_private_values.reserve(samples.size());
    world_cpu_values.reserve(samples.size());
    mesh_cpu_values.reserve(samples.size());
    override_values.reserve(samples.size());
    gpu_buffer_values.reserve(samples.size());
    gpu_texture_values.reserve(samples.size());

    for (const auto& sample : samples) {
        frame_total_values.push_back(sample.frame_total_ms);
        event_processing_values.push_back(sample.event_processing_ms);
        simulation_values.push_back(sample.simulation_ms);
        audio_values.push_back(sample.audio_ms);
        render_preparation_values.push_back(sample.render_preparation_ms);
        streaming_values.push_back(sample.streaming_ms);
        generation_values.push_back(sample.generation_ms);
        fluid_values.push_back(sample.fluid_ms);
        lighting_values.push_back(sample.lighting_ms);
        meshing_values.push_back(sample.meshing_ms);
        upload_values.push_back(sample.upload_ms);
        shadow_values.push_back(sample.shadow_ms);
        world_values.push_back(sample.world_ms);
        render_cpu_values.push_back(sample.render_cpu_ms);
        render_overhead_values.push_back(sample.render_overhead_ms);
        present_values.push_back(sample.present_ms);
        telemetry_values.push_back(sample.telemetry_ms);
        residual_values.push_back(sample.residual_ms);
        if (sample.gpu_timing_valid) {
            gpu_shadow_values.push_back(sample.gpu_shadow_ms);
            gpu_world_values.push_back(sample.gpu_world_ms);
            gpu_sky_values.push_back(sample.gpu_sky_ms);
            gpu_water_values.push_back(sample.gpu_water_ms);
            gpu_entities_values.push_back(sample.gpu_entities_ms);
            gpu_post_process_values.push_back(sample.gpu_post_process_ms);
            gpu_hud_values.push_back(sample.gpu_hud_ms);
            gpu_frame_values.push_back(sample.gpu_frame_ms);
            ++report.summary.gpu_timing_samples;
        }
        pending_generation_values.push_back(sample.pending_generation);
        pending_mesh_values.push_back(sample.pending_mesh);
        pending_lighting_values.push_back(sample.pending_lighting);
        pending_fluid_values.push_back(sample.pending_fluid);
        visible_chunk_values.push_back(sample.visible_chunks);
        shadow_chunk_values.push_back(sample.shadow_chunks);
        world_chunk_values.push_back(sample.world_chunks);
        process_working_set_values.push_back(sample.process_working_set_bytes);
        process_private_values.push_back(sample.process_private_bytes);
        world_cpu_values.push_back(sample.world_cpu_bytes);
        mesh_cpu_values.push_back(sample.mesh_cpu_bytes);
        override_values.push_back(sample.override_bytes);
        gpu_buffer_values.push_back(sample.gpu_buffer_bytes);
        gpu_texture_values.push_back(sample.gpu_texture_bytes);

        report.summary.max_generated_chunks = std::max(report.summary.max_generated_chunks, sample.generated_chunks);
        report.summary.max_meshed_chunks = std::max(report.summary.max_meshed_chunks, sample.meshed_chunks);
        report.summary.max_light_nodes_processed =
            std::max(report.summary.max_light_nodes_processed, sample.light_nodes_processed);
        report.summary.max_uploaded_meshes = std::max(report.summary.max_uploaded_meshes, sample.uploaded_meshes);
        report.summary.max_processed_fluid_cells =
            std::max(report.summary.max_processed_fluid_cells, sample.processed_fluid_cells);
        report.summary.max_fixed_updates = std::max(report.summary.max_fixed_updates, sample.fixed_updates);
        report.summary.total_dropped_fixed_updates += sample.dropped_fixed_updates;
        report.summary.max_draw_calls = std::max(report.summary.max_draw_calls, sample.draw_calls);
        report.summary.max_triangles = std::max(report.summary.max_triangles, sample.triangles);
        report.summary.max_uploaded_bytes = std::max(report.summary.max_uploaded_bytes, sample.uploaded_bytes);
        report.summary.total_stream_chunk_changes += sample.stream_chunk_changes;
        report.summary.total_generation_enqueued += sample.generation_enqueued;
        report.summary.total_generation_pruned += sample.generation_pruned;
        report.summary.total_unloaded_chunks += sample.unloaded_chunks;
        report.summary.total_lighting_jobs_completed += sample.lighting_jobs_completed;

        if (sample.frame_total_ms > kPerformanceLagThreshold16Ms) {
            ++report.summary.lag_buckets.over_16_7_ms;
        }
        if (sample.frame_total_ms > kPerformanceLagThreshold33Ms) {
            ++report.summary.lag_buckets.over_33_3_ms;
        }
        if (sample.frame_total_ms > kPerformanceLagThreshold50Ms) {
            ++report.summary.lag_buckets.over_50_0_ms;
        }

        ++report.hotspots.dominant_stage_counts[static_cast<std::size_t>(sample.dominant_stage)];
    }

    report.summary.frame_total_ms = summarize_metric(frame_total_values);
    report.summary.event_processing_ms = summarize_metric(event_processing_values);
    report.summary.simulation_ms = summarize_metric(simulation_values);
    report.summary.audio_ms = summarize_metric(audio_values);
    report.summary.render_preparation_ms = summarize_metric(render_preparation_values);
    report.summary.streaming_ms = summarize_metric(streaming_values);
    report.summary.generation_ms = summarize_metric(generation_values);
    report.summary.fluid_ms = summarize_metric(fluid_values);
    report.summary.lighting_ms = summarize_metric(lighting_values);
    report.summary.meshing_ms = summarize_metric(meshing_values);
    report.summary.upload_ms = summarize_metric(upload_values);
    report.summary.shadow_ms = summarize_metric(shadow_values);
    report.summary.world_ms = summarize_metric(world_values);
    report.summary.render_cpu_ms = summarize_metric(render_cpu_values);
    report.summary.render_overhead_ms = summarize_metric(render_overhead_values);
    report.summary.present_ms = summarize_metric(present_values);
    report.summary.telemetry_ms = summarize_metric(telemetry_values);
    report.summary.residual_ms = summarize_metric(residual_values);
    report.summary.gpu_shadow_ms = summarize_metric(gpu_shadow_values);
    report.summary.gpu_world_ms = summarize_metric(gpu_world_values);
    report.summary.gpu_sky_ms = summarize_metric(gpu_sky_values);
    report.summary.gpu_water_ms = summarize_metric(gpu_water_values);
    report.summary.gpu_entities_ms = summarize_metric(gpu_entities_values);
    report.summary.gpu_post_process_ms = summarize_metric(gpu_post_process_values);
    report.summary.gpu_hud_ms = summarize_metric(gpu_hud_values);
    report.summary.gpu_frame_ms = summarize_metric(gpu_frame_values);
    report.summary.pending_generation = summarize_counter(pending_generation_values);
    report.summary.pending_mesh = summarize_counter(pending_mesh_values);
    report.summary.pending_lighting = summarize_counter(pending_lighting_values);
    report.summary.pending_fluid = summarize_counter(pending_fluid_values);
    report.summary.visible_chunks = summarize_counter(visible_chunk_values);
    report.summary.shadow_chunks = summarize_counter(shadow_chunk_values);
    report.summary.world_chunks = summarize_counter(world_chunk_values);
    report.summary.process_working_set_bytes = summarize_counter(process_working_set_values);
    report.summary.process_private_bytes = summarize_counter(process_private_values);
    report.summary.world_cpu_bytes = summarize_counter(world_cpu_values);
    report.summary.mesh_cpu_bytes = summarize_counter(mesh_cpu_values);
    report.summary.override_bytes = summarize_counter(override_values);
    report.summary.gpu_buffer_bytes = summarize_counter(gpu_buffer_values);
    report.summary.gpu_texture_bytes = summarize_counter(gpu_texture_values);
    const auto worst_frame_iterator = std::max_element(samples.begin(), samples.end(), [](const FramePerformanceSample& lhs, const FramePerformanceSample& rhs) {
        return lhs.frame_total_ms < rhs.frame_total_ms;
    });
    report.hotspots.worst_frame_stage = worst_frame_iterator->dominant_stage;

    report.worst_frames = samples;
    std::sort(report.worst_frames.begin(), report.worst_frames.end(), [](const FramePerformanceSample& lhs, const FramePerformanceSample& rhs) {
        if (lhs.frame_total_ms != rhs.frame_total_ms) {
            return lhs.frame_total_ms > rhs.frame_total_ms;
        }
        return lhs.frame_index < rhs.frame_index;
    });
    if (report.worst_frames.size() > worst_frame_count) {
        report.worst_frames.resize(worst_frame_count);
    }

    report.spike_windows = build_spike_windows(samples);
    if (include_full_trace) {
        report.frames = samples;
    }

    return report;
}

inline auto json_escape(std::string_view text) -> std::string {
    std::string escaped;
    escaped.reserve(text.size());
    for (const auto character : text) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }
    return escaped;
}

inline void append_metric_json(std::ostringstream& stream, std::string_view name, const PerformanceMetricSummary& summary, bool trailing_comma = true) {
    stream << "    \"" << name << "\": {"
           << "\"avg\": " << summary.average
           << ", \"max\": " << summary.maximum
           << ", \"p50\": " << summary.p50
           << ", \"p95\": " << summary.p95
           << ", \"p99\": " << summary.p99
           << "}";
    if (trailing_comma) {
        stream << ',';
    }
    stream << '\n';
}

inline void append_counter_json(std::ostringstream& stream, std::string_view name, const PerformanceCounterSummary& summary, bool trailing_comma = true) {
    stream << "    \"" << name << "\": {"
           << "\"avg\": " << summary.average
           << ", \"max\": " << summary.maximum
           << "}";
    if (trailing_comma) {
        stream << ',';
    }
    stream << '\n';
}

inline void append_sample_json(std::ostringstream& stream, const FramePerformanceSample& sample, std::string_view indent) {
    stream << indent << "{"
           << "\"frame_index\": " << sample.frame_index
           << ", \"frame_total_ms\": " << sample.frame_total_ms
           << ", \"event_processing_ms\": " << sample.event_processing_ms
           << ", \"simulation_ms\": " << sample.simulation_ms
           << ", \"audio_ms\": " << sample.audio_ms
           << ", \"render_preparation_ms\": " << sample.render_preparation_ms
           << ", \"streaming_ms\": " << sample.streaming_ms
           << ", \"generation_ms\": " << sample.generation_ms
           << ", \"fluid_ms\": " << sample.fluid_ms
           << ", \"lighting_ms\": " << sample.lighting_ms
           << ", \"meshing_ms\": " << sample.meshing_ms
           << ", \"upload_ms\": " << sample.upload_ms
           << ", \"shadow_ms\": " << sample.shadow_ms
           << ", \"world_ms\": " << sample.world_ms
           << ", \"render_cpu_ms\": " << sample.render_cpu_ms
           << ", \"render_overhead_ms\": " << sample.render_overhead_ms
           << ", \"present_ms\": " << sample.present_ms
           << ", \"telemetry_ms\": " << sample.telemetry_ms
           << ", \"residual_ms\": " << sample.residual_ms
           << ", \"gpu_shadow_ms\": " << sample.gpu_shadow_ms
           << ", \"gpu_world_ms\": " << sample.gpu_world_ms
           << ", \"gpu_sky_ms\": " << sample.gpu_sky_ms
           << ", \"gpu_water_ms\": " << sample.gpu_water_ms
           << ", \"gpu_entities_ms\": " << sample.gpu_entities_ms
           << ", \"gpu_post_process_ms\": " << sample.gpu_post_process_ms
           << ", \"gpu_hud_ms\": " << sample.gpu_hud_ms
           << ", \"gpu_frame_ms\": " << sample.gpu_frame_ms
           << ", \"gpu_source_frame\": " << sample.gpu_source_frame
           << ", \"gpu_latency_frames\": " << sample.gpu_latency_frames
           << ", \"gpu_timing_valid\": " << (sample.gpu_timing_valid ? "true" : "false")
           << ", \"resolved_quality\": " << static_cast<unsigned int>(sample.resolved_quality)
           << ", \"adaptive_frame_ema_ms\": " << sample.adaptive_frame_ema_ms
           << ", \"adaptive_frame_p95_ms\": " << sample.adaptive_frame_p95_ms
           << ", \"generated_chunks\": " << sample.generated_chunks
           << ", \"meshed_chunks\": " << sample.meshed_chunks
           << ", \"light_nodes_processed\": " << sample.light_nodes_processed
           << ", \"uploaded_meshes\": " << sample.uploaded_meshes
           << ", \"pending_generation\": " << sample.pending_generation
           << ", \"pending_mesh\": " << sample.pending_mesh
           << ", \"pending_lighting\": " << sample.pending_lighting
           << ", \"processed_fluid_cells\": " << sample.processed_fluid_cells
           << ", \"pending_fluid\": " << sample.pending_fluid
           << ", \"fixed_updates\": " << sample.fixed_updates
           << ", \"dropped_fixed_updates\": " << sample.dropped_fixed_updates
           << ", \"draw_calls\": " << sample.draw_calls
           << ", \"triangles\": " << sample.triangles
           << ", \"uploaded_bytes\": " << sample.uploaded_bytes
           << ", \"stream_chunk_changes\": " << sample.stream_chunk_changes
           << ", \"generation_enqueued\": " << sample.generation_enqueued
           << ", \"generation_pruned\": " << sample.generation_pruned
           << ", \"unloaded_chunks\": " << sample.unloaded_chunks
           << ", \"lighting_jobs_completed\": " << sample.lighting_jobs_completed
           << ", \"visible_chunks\": " << sample.visible_chunks
           << ", \"shadow_chunks\": " << sample.shadow_chunks
           << ", \"world_chunks\": " << sample.world_chunks
           << ", \"memory\": {\"process_working_set_bytes\": " << sample.process_working_set_bytes
           << ", \"process_private_bytes\": " << sample.process_private_bytes
           << ", \"world_cpu_bytes\": " << sample.world_cpu_bytes
           << ", \"mesh_cpu_bytes\": " << sample.mesh_cpu_bytes
           << ", \"override_bytes\": " << sample.override_bytes
           << ", \"gpu_buffer_bytes\": " << sample.gpu_buffer_bytes
           << ", \"gpu_texture_bytes\": " << sample.gpu_texture_bytes << "}"
           << ", \"dominant_stage\": \"" << performance_stage_name(sample.dominant_stage) << "\"}";
}

inline void append_event_json(std::ostringstream& stream, const PerformanceEvent& event, std::string_view indent) {
    stream << indent << "{"
           << "\"frame_index\": " << event.frame_index
           << ", \"kind\": \"" << performance_event_kind_name(event.kind) << "\""
           << ", \"label\": \"" << json_escape(event.label) << "\""
           << ", \"world_x\": " << event.world_x
           << ", \"world_y\": " << event.world_y
           << ", \"world_z\": " << event.world_z
           << ", \"chunk_x\": " << event.chunk_x
           << ", \"chunk_z\": " << event.chunk_z
           << ", \"pending_generation\": " << event.pending_generation
           << ", \"pending_mesh\": " << event.pending_mesh
           << ", \"pending_lighting\": " << event.pending_lighting
           << "}";
}

inline auto format_performance_json(const PerformanceRunReport& report) -> std::string {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6);
    stream << "{\n";
    stream << "  \"schema_version\": " << report.schema_version << ",\n";
    stream << "  \"metadata\": {\n";
    stream << "    \"platform\": \"" << json_escape(report.metadata.platform) << "\",\n";
    stream << "    \"build_type\": \"" << json_escape(report.metadata.build_type) << "\",\n";
    stream << "    \"capture_mode\": \"" << json_escape(report.metadata.capture_mode) << "\",\n";
    stream << "    \"smoke_frames\": " << report.metadata.smoke_frames << ",\n";
    stream << "    \"warmup_frames\": " << report.metadata.warmup_frames << ",\n";
    stream << "    \"stream_radius\": " << report.metadata.stream_radius << ",\n";
    stream << "    \"shadows_enabled\": " << (report.metadata.shadows_enabled ? "true" : "false") << ",\n";
    stream << "    \"shadow_map_size\": " << report.metadata.shadow_map_size << ",\n";
    stream << "    \"viewport_width\": " << report.metadata.viewport_width << ",\n";
    stream << "    \"viewport_height\": " << report.metadata.viewport_height << ",\n";
    stream << "    \"post_process_enabled\": " << (report.metadata.post_process_enabled ? "true" : "false") << ",\n";
    stream << "    \"freeze_time\": " << (report.metadata.freeze_time ? "true" : "false") << ",\n";
    stream << "    \"scenario\": \"" << json_escape(report.metadata.scenario) << "\",\n";
    stream << "    \"quality_profile\": \"" << json_escape(report.metadata.quality_profile) << "\",\n";
    stream << "    \"vsync_mode\": \"" << json_escape(report.metadata.vsync_mode) << "\",\n";
    stream << "    \"trace_included\": " << (report.metadata.trace_included ? "true" : "false") << '\n';
    stream << "  },\n";
    stream << "  \"summary\": {\n";
    stream << "    \"frame_count\": " << report.summary.frame_count << ",\n";
    append_metric_json(stream, "frame_total_ms", report.summary.frame_total_ms);
    append_metric_json(stream, "event_processing_ms", report.summary.event_processing_ms);
    append_metric_json(stream, "simulation_ms", report.summary.simulation_ms);
    append_metric_json(stream, "audio_ms", report.summary.audio_ms);
    append_metric_json(stream, "render_preparation_ms", report.summary.render_preparation_ms);
    append_metric_json(stream, "streaming_ms", report.summary.streaming_ms);
    append_metric_json(stream, "generation_ms", report.summary.generation_ms);
    append_metric_json(stream, "fluid_ms", report.summary.fluid_ms);
    append_metric_json(stream, "lighting_ms", report.summary.lighting_ms);
    append_metric_json(stream, "meshing_ms", report.summary.meshing_ms);
    append_metric_json(stream, "upload_ms", report.summary.upload_ms);
    append_metric_json(stream, "shadow_ms", report.summary.shadow_ms);
    append_metric_json(stream, "world_ms", report.summary.world_ms);
    append_metric_json(stream, "render_cpu_ms", report.summary.render_cpu_ms);
    append_metric_json(stream, "render_overhead_ms", report.summary.render_overhead_ms);
    append_metric_json(stream, "present_ms", report.summary.present_ms);
    append_metric_json(stream, "telemetry_ms", report.summary.telemetry_ms);
    append_metric_json(stream, "residual_ms", report.summary.residual_ms);
    append_metric_json(stream, "gpu_shadow_ms", report.summary.gpu_shadow_ms);
    append_metric_json(stream, "gpu_world_ms", report.summary.gpu_world_ms);
    append_metric_json(stream, "gpu_sky_ms", report.summary.gpu_sky_ms);
    append_metric_json(stream, "gpu_water_ms", report.summary.gpu_water_ms);
    append_metric_json(stream, "gpu_entities_ms", report.summary.gpu_entities_ms);
    append_metric_json(stream, "gpu_post_process_ms", report.summary.gpu_post_process_ms);
    append_metric_json(stream, "gpu_hud_ms", report.summary.gpu_hud_ms);
    append_metric_json(stream, "gpu_frame_ms", report.summary.gpu_frame_ms);
    append_counter_json(stream, "pending_generation", report.summary.pending_generation);
    append_counter_json(stream, "pending_mesh", report.summary.pending_mesh);
    append_counter_json(stream, "pending_lighting", report.summary.pending_lighting);
    append_counter_json(stream, "pending_fluid", report.summary.pending_fluid);
    append_counter_json(stream, "visible_chunks", report.summary.visible_chunks);
    append_counter_json(stream, "shadow_chunks", report.summary.shadow_chunks);
    append_counter_json(stream, "world_chunks", report.summary.world_chunks);
    append_counter_json(stream, "process_working_set_bytes", report.summary.process_working_set_bytes);
    append_counter_json(stream, "process_private_bytes", report.summary.process_private_bytes);
    append_counter_json(stream, "world_cpu_bytes", report.summary.world_cpu_bytes);
    append_counter_json(stream, "mesh_cpu_bytes", report.summary.mesh_cpu_bytes);
    append_counter_json(stream, "override_bytes", report.summary.override_bytes);
    append_counter_json(stream, "gpu_buffer_bytes", report.summary.gpu_buffer_bytes);
    append_counter_json(stream, "gpu_texture_bytes", report.summary.gpu_texture_bytes);
    stream << "    \"lag_buckets\": {"
           << "\"over_16_7_ms\": " << report.summary.lag_buckets.over_16_7_ms
           << ", \"over_33_3_ms\": " << report.summary.lag_buckets.over_33_3_ms
           << ", \"over_50_0_ms\": " << report.summary.lag_buckets.over_50_0_ms
           << "},\n";
    stream << "    \"max_generated_chunks\": " << report.summary.max_generated_chunks << ",\n";
    stream << "    \"max_meshed_chunks\": " << report.summary.max_meshed_chunks << ",\n";
    stream << "    \"max_light_nodes_processed\": " << report.summary.max_light_nodes_processed << ",\n";
    stream << "    \"max_uploaded_meshes\": " << report.summary.max_uploaded_meshes << ",\n";
    stream << "    \"max_processed_fluid_cells\": " << report.summary.max_processed_fluid_cells << ",\n";
    stream << "    \"max_fixed_updates\": " << report.summary.max_fixed_updates << ",\n";
    stream << "    \"total_dropped_fixed_updates\": " << report.summary.total_dropped_fixed_updates << ",\n";
    stream << "    \"max_draw_calls\": " << report.summary.max_draw_calls << ",\n";
    stream << "    \"max_triangles\": " << report.summary.max_triangles << ",\n";
    stream << "    \"max_uploaded_bytes\": " << report.summary.max_uploaded_bytes << ",\n";
    stream << "    \"gpu_timing_samples\": " << report.summary.gpu_timing_samples << ",\n";
    stream << "    \"scheduler_totals\": {"
           << "\"stream_chunk_changes\": " << report.summary.total_stream_chunk_changes
           << ", \"generation_enqueued\": " << report.summary.total_generation_enqueued
           << ", \"generation_pruned\": " << report.summary.total_generation_pruned
           << ", \"unloaded_chunks\": " << report.summary.total_unloaded_chunks
           << ", \"lighting_jobs_completed\": " << report.summary.total_lighting_jobs_completed
           << "}\n";
    stream << "  },\n";
    stream << "  \"hotspots\": {\n";
    stream << "    \"worst_frame_stage\": \"" << performance_stage_name(report.hotspots.worst_frame_stage) << "\",\n";
    stream << "    \"dominant_stage_counts\": {\n";
    for (std::size_t index = 0; index < kPerformanceStages.size(); ++index) {
        const auto stage = kPerformanceStages[index];
        stream << "      \"" << performance_stage_name(stage) << "\": "
               << report.hotspots.dominant_stage_counts[index];
        if (index + 1 != kPerformanceStages.size()) {
            stream << ',';
        }
        stream << '\n';
    }
    stream << "    }\n";
    stream << "  },\n";
    stream << "  \"event_summary\": {"
           << "\"total\": " << report.event_summary.total_events
           << ", \"block_breaks\": " << report.event_summary.block_breaks
           << ", \"block_places\": " << report.event_summary.block_places
           << "},\n";
    stream << "  \"worst_frames\": [\n";
    for (std::size_t index = 0; index < report.worst_frames.size(); ++index) {
        append_sample_json(stream, report.worst_frames[index], "    ");
        if (index + 1 != report.worst_frames.size()) {
            stream << ',';
        }
        stream << '\n';
    }
    stream << "  ],\n";
    stream << "  \"spike_windows\": [\n";
    for (std::size_t index = 0; index < report.spike_windows.size(); ++index) {
        const auto& window = report.spike_windows[index];
        stream << "    {"
               << "\"start_frame\": " << window.start_frame
               << ", \"end_frame\": " << window.end_frame
               << ", \"peak_frame\": " << window.peak_frame
               << ", \"peak_ms\": " << window.peak_ms
               << ", \"dominant_stage\": \"" << performance_stage_name(window.dominant_stage) << "\"}";
        if (index + 1 != report.spike_windows.size()) {
            stream << ',';
        }
        stream << '\n';
    }
    stream << "  ],\n";
    stream << "  \"events\": [\n";
    for (std::size_t index = 0; index < report.events.size(); ++index) {
        append_event_json(stream, report.events[index], "    ");
        if (index + 1 != report.events.size()) {
            stream << ',';
        }
        stream << '\n';
    }
    stream << "  ]";
    if (report.metadata.trace_included) {
        stream << ",\n";
        stream << "  \"frames\": [\n";
        for (std::size_t index = 0; index < report.frames.size(); ++index) {
            append_sample_json(stream, report.frames[index], "    ");
            if (index + 1 != report.frames.size()) {
                stream << ',';
            }
            stream << '\n';
        }
        stream << "  ]\n";
    } else {
        stream << '\n';
    }
    stream << "}\n";
    return stream.str();
}

inline auto format_performance_report(const PerformanceRunReport& report) -> std::string {
    if (report.summary.frame_count == 0) {
        return {};
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3);
    stream << "ValCraft performance summary";
    if (!report.metadata.scenario.empty()) {
        stream << " [" << report.metadata.scenario << "]";
    }
    stream << '\n';
    stream << "  capture_mode=" << report.metadata.capture_mode << '\n';
    stream << "  render_flags shadows=" << (report.metadata.shadows_enabled ? "on" : "off")
           << " post_process=" << (report.metadata.post_process_enabled ? "on" : "off")
           << " shadow_map_size=" << report.metadata.shadow_map_size << '\n';
    stream << "  frame_total_ms_avg=" << report.summary.frame_total_ms.average
           << " p95=" << report.summary.frame_total_ms.p95
           << " p99=" << report.summary.frame_total_ms.p99
           << " max=" << report.summary.frame_total_ms.maximum << '\n';
    stream << "  cpu_stages_ms_avg events=" << report.summary.event_processing_ms.average
           << " simulation=" << report.summary.simulation_ms.average
           << " audio=" << report.summary.audio_ms.average
           << " render_prep=" << report.summary.render_preparation_ms.average
           << " render=" << report.summary.render_cpu_ms.average
           << " render_overhead=" << report.summary.render_overhead_ms.average
           << " present=" << report.summary.present_ms.average
           << " telemetry=" << report.summary.telemetry_ms.average
           << " residual=" << report.summary.residual_ms.average << '\n';
    stream << "  streaming_ms_avg=" << report.summary.streaming_ms.average
           << " p95=" << report.summary.streaming_ms.p95
           << " max=" << report.summary.streaming_ms.maximum << '\n';
    stream << "  generation_ms_avg=" << report.summary.generation_ms.average
           << " p95=" << report.summary.generation_ms.p95
           << " max=" << report.summary.generation_ms.maximum
           << " chunks_max=" << report.summary.max_generated_chunks << '\n';
    stream << "  fluid_ms_avg=" << report.summary.fluid_ms.average
           << " p95=" << report.summary.fluid_ms.p95
           << " max=" << report.summary.fluid_ms.maximum
           << " cells_max=" << report.summary.max_processed_fluid_cells
           << " pending_max=" << report.summary.pending_fluid.maximum << '\n';
    stream << "  lighting_ms_avg=" << report.summary.lighting_ms.average
           << " p95=" << report.summary.lighting_ms.p95
           << " max=" << report.summary.lighting_ms.maximum
           << " nodes_max=" << report.summary.max_light_nodes_processed
           << " jobs_total=" << report.summary.total_lighting_jobs_completed << '\n';
    stream << "  meshing_ms_avg=" << report.summary.meshing_ms.average
           << " p95=" << report.summary.meshing_ms.p95
           << " max=" << report.summary.meshing_ms.maximum
           << " chunks_max=" << report.summary.max_meshed_chunks << '\n';
    stream << "  upload_ms_avg=" << report.summary.upload_ms.average
           << " p95=" << report.summary.upload_ms.p95
           << " max=" << report.summary.upload_ms.maximum
           << " uploads_max=" << report.summary.max_uploaded_meshes << '\n';
    stream << "  shadow_ms_avg=" << report.summary.shadow_ms.average
           << " p95=" << report.summary.shadow_ms.p95
           << " max=" << report.summary.shadow_ms.maximum
           << " chunks_max=" << report.summary.shadow_chunks.maximum << '\n';
    stream << "  world_ms_avg=" << report.summary.world_ms.average
           << " p95=" << report.summary.world_ms.p95
           << " max=" << report.summary.world_ms.maximum
           << " chunks_max=" << report.summary.world_chunks.maximum << '\n';
    stream << "  gpu_ms_avg frame=" << report.summary.gpu_frame_ms.average
           << " shadow=" << report.summary.gpu_shadow_ms.average
           << " world=" << report.summary.gpu_world_ms.average
           << " sky=" << report.summary.gpu_sky_ms.average
           << " water=" << report.summary.gpu_water_ms.average
           << " entities=" << report.summary.gpu_entities_ms.average
           << " post=" << report.summary.gpu_post_process_ms.average
           << " hud=" << report.summary.gpu_hud_ms.average << '\n';
    constexpr auto bytes_per_mebibyte = 1024.0 * 1024.0;
    stream << "  memory_peak_mib working_set="
           << static_cast<double>(report.summary.process_working_set_bytes.maximum) / bytes_per_mebibyte
           << " private=" << static_cast<double>(report.summary.process_private_bytes.maximum) / bytes_per_mebibyte
           << " world=" << static_cast<double>(report.summary.world_cpu_bytes.maximum) / bytes_per_mebibyte
           << " mesh=" << static_cast<double>(report.summary.mesh_cpu_bytes.maximum) / bytes_per_mebibyte
           << " overrides=" << static_cast<double>(report.summary.override_bytes.maximum) / bytes_per_mebibyte
           << " gpu_buffers=" << static_cast<double>(report.summary.gpu_buffer_bytes.maximum) / bytes_per_mebibyte
           << " gpu_textures=" << static_cast<double>(report.summary.gpu_texture_bytes.maximum) / bytes_per_mebibyte << '\n';
    stream << "  pending_generation_avg=" << report.summary.pending_generation.average
           << " max=" << report.summary.pending_generation.maximum << '\n';
    stream << "  pending_mesh_avg=" << report.summary.pending_mesh.average
           << " max=" << report.summary.pending_mesh.maximum << '\n';
    stream << "  pending_lighting_avg=" << report.summary.pending_lighting.average
           << " max=" << report.summary.pending_lighting.maximum << '\n';
    stream << "  visible_chunks_avg=" << report.summary.visible_chunks.average
           << " max=" << report.summary.visible_chunks.maximum << '\n';
    stream << "  lag_frames_16_7=" << report.summary.lag_buckets.over_16_7_ms
           << " lag_frames_33_3=" << report.summary.lag_buckets.over_33_3_ms
           << " lag_frames_50_0=" << report.summary.lag_buckets.over_50_0_ms << '\n';
    stream << "  hotspot_worst_frame=" << performance_stage_name(report.hotspots.worst_frame_stage)
           << " spike_windows=" << report.spike_windows.size() << '\n';
    stream << "  events_total=" << report.event_summary.total_events
           << " block_breaks=" << report.event_summary.block_breaks
           << " block_places=" << report.event_summary.block_places << '\n';
    stream << "  scheduler_stream_changes=" << report.summary.total_stream_chunk_changes
           << " generation_enqueued=" << report.summary.total_generation_enqueued
           << " generation_pruned=" << report.summary.total_generation_pruned
           << " unloaded_chunks=" << report.summary.total_unloaded_chunks << '\n';
    return stream.str();
}

} // namespace valcraft
