#pragma once

#include "app/PerformanceReport.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <initializer_list>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace valcraft {

inline constexpr int kAuditSchemaVersion = 1;
inline constexpr std::size_t kAuditCategoryCount = 10;

enum class AuditMode : std::uint8_t {
    Measure = 0,
    Forensic,
};

enum class AuditEventCategory : std::uint8_t {
    Session = 0,
    InputRaw,
    InputAction,
    Ui,
    Player,
    World,
    Render,
    Performance,
    Creatures,
    Items,
};

enum class AuditSeverity : std::uint8_t {
    Trace = 0,
    Info,
    Warning,
    Error,
};

enum class AuditPriority : std::uint8_t {
    Low = 0,
    Normal,
    High,
    Critical,
};

enum class AuditRunStatus : std::uint8_t {
    Completed = 0,
    Aborted,
    AuditDisabled,
};

struct AuditOptions {
    bool enabled = false;
    AuditMode mode = AuditMode::Measure;
    std::filesystem::path root_directory = "performancesaudit";
    std::string label = "interactive";
    bool trace_frames = false;
    bool console_summary = false;
    std::string compatibility_json_path {};
    std::size_t writer_queue_capacity = 8192;
    std::size_t writer_batch_size = 512;
    int writer_flush_interval_ms = 100;
    int writer_shutdown_timeout_ms = 2000;
};

struct AuditRunPaths {
    std::filesystem::path root_directory {};
    std::filesystem::path run_directory {};
    std::filesystem::path manifest_path {};
    std::filesystem::path summary_json_path {};
    std::filesystem::path summary_text_path {};
    std::filesystem::path events_path {};
    std::filesystem::path seconds_path {};
    std::filesystem::path frames_path {};
    std::filesystem::path spikes_path {};
};

struct AuditEvent {
    int schema_version = kAuditSchemaVersion;
    std::string session_id {};
    AuditMode mode = AuditMode::Measure;
    std::uint64_t t_us = 0;
    std::size_t frame_index = 0;
    std::size_t second_index = 0;
    AuditEventCategory category = AuditEventCategory::Session;
    std::string kind {};
    AuditSeverity severity = AuditSeverity::Info;
    std::string payload_json = "{}";
};

struct AuditFrameSample {
    int schema_version = kAuditSchemaVersion;
    std::string session_id {};
    AuditMode mode = AuditMode::Measure;
    std::uint64_t t_us = 0;
    std::size_t frame_index = 0;
    std::size_t second_index = 0;
    double fps = 0.0;
    std::string ui_screen = "gameplay";
    bool mouse_captured = true;
    std::size_t input_raw_events = 0;
    std::size_t input_action_events = 0;
    std::size_t active_creatures = 0;
    std::size_t active_item_drops = 0;
    FramePerformanceSample performance {};
};

struct AuditSecondSample {
    int schema_version = kAuditSchemaVersion;
    std::string session_id {};
    AuditMode mode = AuditMode::Measure;
    std::uint64_t t_us = 0;
    std::size_t second_index = 0;
    std::size_t frame_count = 0;
    double fps_avg = 0.0;
    double fps_min = 0.0;
    double fps_max = 0.0;
    double frame_ms_avg = 0.0;
    double frame_ms_p95 = 0.0;
    double frame_ms_max = 0.0;
    double event_processing_ms_avg = 0.0;
    double simulation_ms_avg = 0.0;
    double audio_ms_avg = 0.0;
    double render_preparation_ms_avg = 0.0;
    double streaming_ms_avg = 0.0;
    double generation_ms_avg = 0.0;
    double fluid_ms_avg = 0.0;
    double lighting_ms_avg = 0.0;
    double meshing_ms_avg = 0.0;
    double upload_ms_avg = 0.0;
    double shadow_ms_avg = 0.0;
    double world_ms_avg = 0.0;
    double render_cpu_ms_avg = 0.0;
    double render_overhead_ms_avg = 0.0;
    double present_ms_avg = 0.0;
    double telemetry_ms_avg = 0.0;
    double residual_ms_avg = 0.0;
    double gpu_frame_ms_avg = 0.0;
    std::size_t gpu_timing_samples = 0;
    std::size_t input_raw_events = 0;
    std::size_t input_action_events = 0;
    std::size_t ui_events = 0;
    std::size_t player_events = 0;
    std::size_t block_breaks = 0;
    std::size_t block_places = 0;
    std::size_t stream_chunk_changes = 0;
    std::size_t generation_enqueued = 0;
    std::size_t generation_pruned = 0;
    std::size_t unloaded_chunks = 0;
    std::size_t generated_chunks = 0;
    std::size_t meshed_chunks = 0;
    std::size_t light_nodes_processed = 0;
    std::size_t lighting_jobs_completed = 0;
    std::size_t uploaded_meshes = 0;
    std::size_t visible_chunks_max = 0;
    std::size_t shadow_chunks_max = 0;
    std::size_t world_chunks_max = 0;
    std::size_t pending_generation_max = 0;
    std::size_t pending_mesh_max = 0;
    std::size_t pending_lighting_max = 0;
    std::size_t pending_fluid_max = 0;
    std::uint64_t process_working_set_bytes_max = 0;
    std::uint64_t process_private_bytes_max = 0;
    std::size_t creature_spawns = 0;
    std::size_t creature_despawns = 0;
    std::size_t creature_attacks = 0;
    std::size_t active_creatures_max = 0;
    std::size_t item_spawns = 0;
    std::size_t item_merges = 0;
    std::size_t item_pickups = 0;
    std::size_t item_expired = 0;
    std::size_t active_item_drops_max = 0;
    std::size_t spike_frames = 0;
};

struct AuditManifest {
    int schema_version = kAuditSchemaVersion;
    std::string session_id {};
    std::string label {};
    AuditMode mode = AuditMode::Measure;
    AuditRunStatus status = AuditRunStatus::Completed;
    std::string platform = "unknown";
    std::string build_type = "unknown";
    std::filesystem::path working_directory {};
    std::vector<std::string> arguments {};
    std::string started_at_utc {};
    std::string ended_at_utc {};
    double duration_seconds = 0.0;
    AuditRunPaths paths {};
    std::array<std::size_t, kAuditCategoryCount> written_event_counts {};
    std::array<std::size_t, kAuditCategoryCount> dropped_event_counts {};
    std::size_t second_samples = 0;
    std::size_t dropped_second_samples = 0;
    std::size_t recorded_frames = 0;
    std::size_t written_frames = 0;
    std::size_t dropped_frames = 0;
    std::vector<std::string> errors {};
    std::vector<std::string> produced_files {};
};

struct AuditStartContext {
    std::string platform = "unknown";
    std::string build_type = "unknown";
    std::filesystem::path working_directory {};
    std::vector<std::string> arguments {};
    bool smoke_test = false;
};

struct AuditFinalizeContext {
    AuditRunStatus status = AuditRunStatus::Completed;
    PerformanceRunReport performance_report {};
};

struct AuditJsonField {
    std::string_view key {};
    std::string value_json {};
};

struct AuditWriterCounters {
    std::array<std::size_t, kAuditCategoryCount> written_event_counts {};
    std::array<std::size_t, kAuditCategoryCount> dropped_event_counts {};
    std::size_t written_second_samples = 0;
    std::size_t dropped_second_samples = 0;
    std::size_t written_frames = 0;
    std::size_t dropped_frames = 0;
};

class AuditWriter {
public:
    AuditWriter();
    ~AuditWriter();

    AuditWriter(const AuditWriter&) = delete;
    auto operator=(const AuditWriter&) -> AuditWriter& = delete;

    [[nodiscard]] auto start(const AuditOptions& options,
                             const AuditRunPaths& paths,
                             std::vector<std::string>* errors) -> bool;
    void stop(std::vector<std::string>* errors);
    [[nodiscard]] auto enqueue_event(std::string line, AuditPriority priority, AuditEventCategory category) -> bool;
    [[nodiscard]] auto enqueue_second(std::string line, AuditPriority priority) -> bool;
    [[nodiscard]] auto enqueue_frame(std::string line, AuditPriority priority) -> bool;
    [[nodiscard]] auto active() const noexcept -> bool;
    [[nodiscard]] auto counters() const -> AuditWriterCounters;
    [[nodiscard]] auto dropped_event_counts() const -> std::array<std::size_t, kAuditCategoryCount>;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_ {};
};

class AuditRecorder {
public:
    AuditRecorder(AuditOptions options, AuditStartContext start_context);
    ~AuditRecorder();

    AuditRecorder(const AuditRecorder&) = delete;
    auto operator=(const AuditRecorder&) -> AuditRecorder& = delete;

    [[nodiscard]] auto enabled() const noexcept -> bool;
    [[nodiscard]] auto options() const noexcept -> const AuditOptions&;
    [[nodiscard]] auto session_id() const noexcept -> std::string_view;
    [[nodiscard]] auto paths() const noexcept -> const AuditRunPaths&;

    void record_event(AuditEvent event, AuditPriority priority = AuditPriority::Normal);
    void record_frame(AuditFrameSample sample, AuditPriority priority = AuditPriority::Low);
    void record_second(AuditSecondSample sample, AuditPriority priority = AuditPriority::High);
    void record_error(std::string message);
    void finalize(const AuditFinalizeContext& context);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_ {};
};

[[nodiscard]] auto audit_mode_name(AuditMode mode) noexcept -> std::string_view;
[[nodiscard]] auto audit_event_category_name(AuditEventCategory category) noexcept -> std::string_view;
[[nodiscard]] auto audit_severity_name(AuditSeverity severity) noexcept -> std::string_view;
[[nodiscard]] auto audit_run_status_name(AuditRunStatus status) noexcept -> std::string_view;
[[nodiscard]] auto parse_audit_mode(std::string_view text) -> std::optional<AuditMode>;
[[nodiscard]] auto sanitize_audit_label(std::string_view label) -> std::string;
[[nodiscard]] auto make_audit_run_paths(const AuditOptions& options,
                                        std::string_view session_id,
                                        const std::chrono::system_clock::time_point& started_at) -> AuditRunPaths;

[[nodiscard]] inline auto audit_event_category_index(AuditEventCategory category) noexcept -> std::size_t {
    return static_cast<std::size_t>(category);
}

[[nodiscard]] inline auto audit_json_bool(bool value) -> std::string {
    return value ? "true" : "false";
}

[[nodiscard]] inline auto audit_json_null() -> std::string {
    return "null";
}

[[nodiscard]] auto audit_json_string(std::string_view text) -> std::string;
[[nodiscard]] auto audit_json_object(std::initializer_list<AuditJsonField> fields) -> std::string;
[[nodiscard]] auto audit_json_array(const std::vector<std::string>& values_json) -> std::string;

template <typename Number>
[[nodiscard]] auto audit_json_number(Number value) -> std::string {
    if constexpr (std::is_floating_point_v<Number>) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(6) << value;
        return stream.str();
    } else if constexpr (std::is_enum_v<Number>) {
        using Underlying = std::underlying_type_t<Number>;
        return std::to_string(static_cast<Underlying>(value));
    } else {
        return std::to_string(value);
    }
}

} // namespace valcraft
