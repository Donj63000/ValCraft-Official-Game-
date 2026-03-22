#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace valcraft {

enum class PerformanceStage : std::size_t {
    Streaming = 0,
    Generation,
    Lighting,
    Meshing,
    Upload,
    Shadow,
    World,
    Unattributed,
};

constexpr auto kPerformanceLagThreshold16Ms = 16.7;
constexpr auto kPerformanceLagThreshold33Ms = 33.3;
constexpr auto kPerformanceLagThreshold50Ms = 50.0;

inline constexpr std::array<PerformanceStage, 8> kPerformanceStages {{
    PerformanceStage::Streaming,
    PerformanceStage::Generation,
    PerformanceStage::Lighting,
    PerformanceStage::Meshing,
    PerformanceStage::Upload,
    PerformanceStage::Shadow,
    PerformanceStage::World,
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
};

struct PerformanceReportMetadata {
    std::string platform = "unknown";
    std::string build_type = "unknown";
    std::size_t smoke_frames = 0;
    int stream_radius = 0;
    bool shadows_enabled = true;
    int shadow_map_size = 2048;
    bool post_process_enabled = true;
    bool freeze_time = false;
    std::string scenario = "default";
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
    std::size_t maximum = 0;
};

struct PerformanceLagBuckets {
    std::size_t over_16_7_ms = 0;
    std::size_t over_33_3_ms = 0;
    std::size_t over_50_0_ms = 0;
};

struct PerformanceReportSummary {
    std::size_t frame_count = 0;
    PerformanceMetricSummary frame_total_ms {};
    PerformanceMetricSummary streaming_ms {};
    PerformanceMetricSummary generation_ms {};
    PerformanceMetricSummary lighting_ms {};
    PerformanceMetricSummary meshing_ms {};
    PerformanceMetricSummary upload_ms {};
    PerformanceMetricSummary shadow_ms {};
    PerformanceMetricSummary world_ms {};
    PerformanceCounterSummary pending_generation {};
    PerformanceCounterSummary pending_mesh {};
    PerformanceCounterSummary pending_lighting {};
    PerformanceCounterSummary visible_chunks {};
    PerformanceCounterSummary shadow_chunks {};
    PerformanceCounterSummary world_chunks {};
    PerformanceLagBuckets lag_buckets {};
    std::size_t max_generated_chunks = 0;
    std::size_t max_meshed_chunks = 0;
    std::size_t max_light_nodes_processed = 0;
    std::size_t max_uploaded_meshes = 0;
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

struct SpikeWindow {
    std::size_t start_frame = 0;
    std::size_t end_frame = 0;
    std::size_t peak_frame = 0;
    double peak_ms = 0.0;
    PerformanceStage dominant_stage = PerformanceStage::Unattributed;
};

struct PerformanceRunReport {
    static constexpr int kSchemaVersion = 1;

    int schema_version = kSchemaVersion;
    PerformanceReportMetadata metadata {};
    PerformanceReportSummary summary {};
    PerformanceHotspotSummary hotspots {};
    std::vector<FramePerformanceSample> worst_frames {};
    std::vector<SpikeWindow> spike_windows {};
    std::vector<FramePerformanceSample> frames {};
};

inline auto performance_stage_name(PerformanceStage stage) -> std::string_view {
    switch (stage) {
    case PerformanceStage::Streaming:
        return "streaming";
    case PerformanceStage::Generation:
        return "generation";
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
    case PerformanceStage::Unattributed:
    default:
        return "unattributed";
    }
}

inline auto detect_dominant_stage(const FramePerformanceSample& sample) -> PerformanceStage {
    const std::array<std::pair<PerformanceStage, double>, 7> measured_stages {{
        {PerformanceStage::Streaming, sample.streaming_ms},
        {PerformanceStage::Generation, sample.generation_ms},
        {PerformanceStage::Lighting, sample.lighting_ms},
        {PerformanceStage::Meshing, sample.meshing_ms},
        {PerformanceStage::Upload, sample.upload_ms},
        {PerformanceStage::Shadow, sample.shadow_ms},
        {PerformanceStage::World, sample.world_ms},
    }};

    auto best_stage = PerformanceStage::Unattributed;
    auto best_value = 0.0;
    for (const auto& [stage, value] : measured_stages) {
        if (value > best_value) {
            best_stage = stage;
            best_value = value;
        }
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

inline auto summarize_counter(const std::vector<std::size_t>& values) -> PerformanceCounterSummary {
    PerformanceCounterSummary summary {};
    if (values.empty()) {
        return summary;
    }

    const auto total = std::accumulate(values.begin(), values.end(), static_cast<std::size_t>(0));
    summary.average = static_cast<double>(total) / static_cast<double>(values.size());
    summary.maximum = *std::max_element(values.begin(), values.end());
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
                                     std::size_t worst_frame_count = 10) -> PerformanceRunReport {
    PerformanceRunReport report {};
    report.metadata = metadata;
    report.metadata.trace_included = include_full_trace;

    std::vector<FramePerformanceSample> samples = raw_samples;
    for (auto& sample : samples) {
        sample.dominant_stage = detect_dominant_stage(sample);
    }

    report.summary.frame_count = samples.size();
    if (samples.empty()) {
        return report;
    }

    std::vector<double> frame_total_values;
    std::vector<double> streaming_values;
    std::vector<double> generation_values;
    std::vector<double> lighting_values;
    std::vector<double> meshing_values;
    std::vector<double> upload_values;
    std::vector<double> shadow_values;
    std::vector<double> world_values;
    std::vector<std::size_t> pending_generation_values;
    std::vector<std::size_t> pending_mesh_values;
    std::vector<std::size_t> pending_lighting_values;
    std::vector<std::size_t> visible_chunk_values;
    std::vector<std::size_t> shadow_chunk_values;
    std::vector<std::size_t> world_chunk_values;

    frame_total_values.reserve(samples.size());
    streaming_values.reserve(samples.size());
    generation_values.reserve(samples.size());
    lighting_values.reserve(samples.size());
    meshing_values.reserve(samples.size());
    upload_values.reserve(samples.size());
    shadow_values.reserve(samples.size());
    world_values.reserve(samples.size());
    pending_generation_values.reserve(samples.size());
    pending_mesh_values.reserve(samples.size());
    pending_lighting_values.reserve(samples.size());
    visible_chunk_values.reserve(samples.size());
    shadow_chunk_values.reserve(samples.size());
    world_chunk_values.reserve(samples.size());

    for (const auto& sample : samples) {
        frame_total_values.push_back(sample.frame_total_ms);
        streaming_values.push_back(sample.streaming_ms);
        generation_values.push_back(sample.generation_ms);
        lighting_values.push_back(sample.lighting_ms);
        meshing_values.push_back(sample.meshing_ms);
        upload_values.push_back(sample.upload_ms);
        shadow_values.push_back(sample.shadow_ms);
        world_values.push_back(sample.world_ms);
        pending_generation_values.push_back(sample.pending_generation);
        pending_mesh_values.push_back(sample.pending_mesh);
        pending_lighting_values.push_back(sample.pending_lighting);
        visible_chunk_values.push_back(sample.visible_chunks);
        shadow_chunk_values.push_back(sample.shadow_chunks);
        world_chunk_values.push_back(sample.world_chunks);

        report.summary.max_generated_chunks = std::max(report.summary.max_generated_chunks, sample.generated_chunks);
        report.summary.max_meshed_chunks = std::max(report.summary.max_meshed_chunks, sample.meshed_chunks);
        report.summary.max_light_nodes_processed =
            std::max(report.summary.max_light_nodes_processed, sample.light_nodes_processed);
        report.summary.max_uploaded_meshes = std::max(report.summary.max_uploaded_meshes, sample.uploaded_meshes);
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
    report.summary.streaming_ms = summarize_metric(streaming_values);
    report.summary.generation_ms = summarize_metric(generation_values);
    report.summary.lighting_ms = summarize_metric(lighting_values);
    report.summary.meshing_ms = summarize_metric(meshing_values);
    report.summary.upload_ms = summarize_metric(upload_values);
    report.summary.shadow_ms = summarize_metric(shadow_values);
    report.summary.world_ms = summarize_metric(world_values);
    report.summary.pending_generation = summarize_counter(pending_generation_values);
    report.summary.pending_mesh = summarize_counter(pending_mesh_values);
    report.summary.pending_lighting = summarize_counter(pending_lighting_values);
    report.summary.visible_chunks = summarize_counter(visible_chunk_values);
    report.summary.shadow_chunks = summarize_counter(shadow_chunk_values);
    report.summary.world_chunks = summarize_counter(world_chunk_values);
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
           << ", \"streaming_ms\": " << sample.streaming_ms
           << ", \"generation_ms\": " << sample.generation_ms
           << ", \"lighting_ms\": " << sample.lighting_ms
           << ", \"meshing_ms\": " << sample.meshing_ms
           << ", \"upload_ms\": " << sample.upload_ms
           << ", \"shadow_ms\": " << sample.shadow_ms
           << ", \"world_ms\": " << sample.world_ms
           << ", \"generated_chunks\": " << sample.generated_chunks
           << ", \"meshed_chunks\": " << sample.meshed_chunks
           << ", \"light_nodes_processed\": " << sample.light_nodes_processed
           << ", \"uploaded_meshes\": " << sample.uploaded_meshes
           << ", \"pending_generation\": " << sample.pending_generation
           << ", \"pending_mesh\": " << sample.pending_mesh
           << ", \"pending_lighting\": " << sample.pending_lighting
           << ", \"stream_chunk_changes\": " << sample.stream_chunk_changes
           << ", \"generation_enqueued\": " << sample.generation_enqueued
           << ", \"generation_pruned\": " << sample.generation_pruned
           << ", \"unloaded_chunks\": " << sample.unloaded_chunks
           << ", \"lighting_jobs_completed\": " << sample.lighting_jobs_completed
           << ", \"visible_chunks\": " << sample.visible_chunks
           << ", \"shadow_chunks\": " << sample.shadow_chunks
           << ", \"world_chunks\": " << sample.world_chunks
           << ", \"dominant_stage\": \"" << performance_stage_name(sample.dominant_stage) << "\"}";
}

inline auto format_performance_json(const PerformanceRunReport& report) -> std::string {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6);
    stream << "{\n";
    stream << "  \"schema_version\": " << report.schema_version << ",\n";
    stream << "  \"metadata\": {\n";
    stream << "    \"platform\": \"" << json_escape(report.metadata.platform) << "\",\n";
    stream << "    \"build_type\": \"" << json_escape(report.metadata.build_type) << "\",\n";
    stream << "    \"smoke_frames\": " << report.metadata.smoke_frames << ",\n";
    stream << "    \"stream_radius\": " << report.metadata.stream_radius << ",\n";
    stream << "    \"shadows_enabled\": " << (report.metadata.shadows_enabled ? "true" : "false") << ",\n";
    stream << "    \"shadow_map_size\": " << report.metadata.shadow_map_size << ",\n";
    stream << "    \"post_process_enabled\": " << (report.metadata.post_process_enabled ? "true" : "false") << ",\n";
    stream << "    \"freeze_time\": " << (report.metadata.freeze_time ? "true" : "false") << ",\n";
    stream << "    \"scenario\": \"" << json_escape(report.metadata.scenario) << "\",\n";
    stream << "    \"trace_included\": " << (report.metadata.trace_included ? "true" : "false") << '\n';
    stream << "  },\n";
    stream << "  \"summary\": {\n";
    stream << "    \"frame_count\": " << report.summary.frame_count << ",\n";
    append_metric_json(stream, "frame_total_ms", report.summary.frame_total_ms);
    append_metric_json(stream, "streaming_ms", report.summary.streaming_ms);
    append_metric_json(stream, "generation_ms", report.summary.generation_ms);
    append_metric_json(stream, "lighting_ms", report.summary.lighting_ms);
    append_metric_json(stream, "meshing_ms", report.summary.meshing_ms);
    append_metric_json(stream, "upload_ms", report.summary.upload_ms);
    append_metric_json(stream, "shadow_ms", report.summary.shadow_ms);
    append_metric_json(stream, "world_ms", report.summary.world_ms);
    append_counter_json(stream, "pending_generation", report.summary.pending_generation);
    append_counter_json(stream, "pending_mesh", report.summary.pending_mesh);
    append_counter_json(stream, "pending_lighting", report.summary.pending_lighting);
    append_counter_json(stream, "visible_chunks", report.summary.visible_chunks);
    append_counter_json(stream, "shadow_chunks", report.summary.shadow_chunks);
    append_counter_json(stream, "world_chunks", report.summary.world_chunks);
    stream << "    \"lag_buckets\": {"
           << "\"over_16_7_ms\": " << report.summary.lag_buckets.over_16_7_ms
           << ", \"over_33_3_ms\": " << report.summary.lag_buckets.over_33_3_ms
           << ", \"over_50_0_ms\": " << report.summary.lag_buckets.over_50_0_ms
           << "},\n";
    stream << "    \"max_generated_chunks\": " << report.summary.max_generated_chunks << ",\n";
    stream << "    \"max_meshed_chunks\": " << report.summary.max_meshed_chunks << ",\n";
    stream << "    \"max_light_nodes_processed\": " << report.summary.max_light_nodes_processed << ",\n";
    stream << "    \"max_uploaded_meshes\": " << report.summary.max_uploaded_meshes << ",\n";
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
    stream << "ValCraft smoke performance summary";
    if (!report.metadata.scenario.empty()) {
        stream << " [" << report.metadata.scenario << "]";
    }
    stream << '\n';
    stream << "  render_flags shadows=" << (report.metadata.shadows_enabled ? "on" : "off")
           << " post_process=" << (report.metadata.post_process_enabled ? "on" : "off")
           << " shadow_map_size=" << report.metadata.shadow_map_size << '\n';
    stream << "  frame_total_ms_avg=" << report.summary.frame_total_ms.average
           << " p95=" << report.summary.frame_total_ms.p95
           << " p99=" << report.summary.frame_total_ms.p99
           << " max=" << report.summary.frame_total_ms.maximum << '\n';
    stream << "  streaming_ms_avg=" << report.summary.streaming_ms.average
           << " p95=" << report.summary.streaming_ms.p95
           << " max=" << report.summary.streaming_ms.maximum << '\n';
    stream << "  generation_ms_avg=" << report.summary.generation_ms.average
           << " p95=" << report.summary.generation_ms.p95
           << " max=" << report.summary.generation_ms.maximum
           << " chunks_max=" << report.summary.max_generated_chunks << '\n';
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
    stream << "  scheduler_stream_changes=" << report.summary.total_stream_chunk_changes
           << " generation_enqueued=" << report.summary.total_generation_enqueued
           << " generation_pruned=" << report.summary.total_generation_pruned
           << " unloaded_chunks=" << report.summary.total_unloaded_chunks << '\n';
    return stream.str();
}

} // namespace valcraft
