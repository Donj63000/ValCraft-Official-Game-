#include "app/Audit.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace valcraft {

namespace {

auto format_timestamp_utc(const std::chrono::system_clock::time_point& time_point,
                          std::string_view format) -> std::string {
    const auto time = std::chrono::system_clock::to_time_t(time_point);
    std::tm utc_time {};
#ifdef _WIN32
    gmtime_s(&utc_time, &time);
#else
    gmtime_r(&time, &utc_time);
#endif

    std::ostringstream stream;
    const std::string format_text {format};
    stream << std::put_time(&utc_time, format_text.c_str());
    return stream.str();
}

auto format_iso_utc(const std::chrono::system_clock::time_point& time_point) -> std::string {
    return format_timestamp_utc(time_point, "%Y-%m-%dT%H:%M:%SZ");
}

auto make_session_id(const std::chrono::system_clock::time_point& started_at) -> std::string {
    static std::atomic<std::uint64_t> next_session_sequence {0};
    const auto seconds = format_timestamp_utc(started_at, "%Y%m%d-%H%M%S");
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  started_at.time_since_epoch())
                                  .count() %
                              1000LL;
#ifdef _WIN32
    const auto process_id = static_cast<std::uint64_t>(_getpid());
#else
    const auto process_id = static_cast<std::uint64_t>(getpid());
#endif
    const auto sequence = next_session_sequence.fetch_add(1, std::memory_order_relaxed);

    std::ostringstream stream;
    stream << seconds << '-' << std::setw(3) << std::setfill('0') << milliseconds
           << '-' << process_id << '-' << sequence;
    return stream.str();
}

auto run_directory_stamp(const std::chrono::system_clock::time_point& started_at) -> std::string {
    return format_timestamp_utc(started_at, "%Y%m%d-%H%M%S");
}

auto write_text_file(const std::filesystem::path& path, std::string_view content) -> void {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Unable to open audit output file: " + path.string());
    }

    output << content;
    if (!output.good()) {
        throw std::runtime_error("Unable to write audit output file: " + path.string());
    }
}

auto write_lines_file(const std::filesystem::path& path, const std::vector<std::string>& lines) -> void {
    std::ostringstream stream;
    for (const auto& line : lines) {
        stream << line << '\n';
    }
    write_text_file(path, stream.str());
}

auto total_count(const std::array<std::size_t, kAuditCategoryCount>& counts) -> std::size_t {
    return std::accumulate(counts.begin(), counts.end(), static_cast<std::size_t>(0));
}

auto indent_block(std::string text, std::string_view indent) -> std::string {
    std::string indented;
    indented.reserve(text.size() + indent.size() * 8U);

    bool at_line_start = true;
    for (const auto character : text) {
        if (at_line_start) {
            indented += indent;
            at_line_start = false;
        }
        indented.push_back(character);
        if (character == '\n') {
            at_line_start = true;
        }
    }

    if (indented.empty()) {
        indented += indent;
    }
    return indented;
}

auto frame_sample_to_json(const FramePerformanceSample& sample) -> std::string {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6);
    // Je conserve le schéma compact des événements d'audit : le détail v3
    // des catégories reste porté par le rapport de performance principal.
    append_sample_json(stream, sample, "", false);
    return stream.str();
}

auto audit_event_to_json(const AuditEvent& event) -> std::string {
    std::ostringstream stream;
    stream << "{"
           << "\"schema_version\": " << event.schema_version
           << ", \"session_id\": " << audit_json_string(event.session_id)
           << ", \"mode\": " << audit_json_string(audit_mode_name(event.mode))
           << ", \"t_us\": " << event.t_us
           << ", \"frame_index\": " << event.frame_index
           << ", \"second_index\": " << event.second_index
           << ", \"category\": " << audit_json_string(audit_event_category_name(event.category))
           << ", \"kind\": " << audit_json_string(event.kind)
           << ", \"severity\": " << audit_json_string(audit_severity_name(event.severity))
           << ", \"payload\": " << event.payload_json
           << "}";
    return stream.str();
}

auto audit_frame_to_json(const AuditFrameSample& sample) -> std::string {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6);
    stream << "{"
           << "\"schema_version\": " << sample.schema_version
           << ", \"session_id\": " << audit_json_string(sample.session_id)
           << ", \"mode\": " << audit_json_string(audit_mode_name(sample.mode))
           << ", \"t_us\": " << sample.t_us
           << ", \"frame_index\": " << sample.frame_index
           << ", \"second_index\": " << sample.second_index
           << ", \"fps\": " << sample.fps
           << ", \"ui_screen\": " << audit_json_string(sample.ui_screen)
           << ", \"mouse_captured\": " << audit_json_bool(sample.mouse_captured)
           << ", \"input_raw_events\": " << sample.input_raw_events
           << ", \"input_action_events\": " << sample.input_action_events
           << ", \"active_creatures\": " << sample.active_creatures
           << ", \"active_item_drops\": " << sample.active_item_drops
           << ", \"performance\": " << frame_sample_to_json(sample.performance)
           << "}";
    return stream.str();
}

auto audit_second_to_json(const AuditSecondSample& sample) -> std::string {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6);
    stream << "{"
           << "\"schema_version\": " << sample.schema_version
           << ", \"session_id\": " << audit_json_string(sample.session_id)
           << ", \"mode\": " << audit_json_string(audit_mode_name(sample.mode))
           << ", \"t_us\": " << sample.t_us
           << ", \"second_index\": " << sample.second_index
           << ", \"frame_count\": " << sample.frame_count
           << ", \"fps_avg\": " << sample.fps_avg
           << ", \"fps_min\": " << sample.fps_min
           << ", \"fps_max\": " << sample.fps_max
           << ", \"frame_ms_avg\": " << sample.frame_ms_avg
           << ", \"frame_ms_p95\": " << sample.frame_ms_p95
           << ", \"frame_ms_max\": " << sample.frame_ms_max
           << ", \"event_processing_ms_avg\": " << sample.event_processing_ms_avg
           << ", \"simulation_ms_avg\": " << sample.simulation_ms_avg
           << ", \"audio_ms_avg\": " << sample.audio_ms_avg
           << ", \"render_preparation_ms_avg\": " << sample.render_preparation_ms_avg
           << ", \"streaming_ms_avg\": " << sample.streaming_ms_avg
           << ", \"generation_ms_avg\": " << sample.generation_ms_avg
           << ", \"fluid_ms_avg\": " << sample.fluid_ms_avg
           << ", \"lighting_ms_avg\": " << sample.lighting_ms_avg
           << ", \"meshing_ms_avg\": " << sample.meshing_ms_avg
           << ", \"upload_ms_avg\": " << sample.upload_ms_avg
           << ", \"shadow_ms_avg\": " << sample.shadow_ms_avg
           << ", \"world_ms_avg\": " << sample.world_ms_avg
           << ", \"render_cpu_ms_avg\": " << sample.render_cpu_ms_avg
           << ", \"render_overhead_ms_avg\": " << sample.render_overhead_ms_avg
           << ", \"present_ms_avg\": " << sample.present_ms_avg
           << ", \"telemetry_ms_avg\": " << sample.telemetry_ms_avg
           << ", \"residual_ms_avg\": " << sample.residual_ms_avg
           << ", \"gpu_frame_ms_avg\": " << sample.gpu_frame_ms_avg
           << ", \"gpu_timing_samples\": " << sample.gpu_timing_samples
           << ", \"input_raw_events\": " << sample.input_raw_events
           << ", \"input_action_events\": " << sample.input_action_events
           << ", \"ui_events\": " << sample.ui_events
           << ", \"player_events\": " << sample.player_events
           << ", \"block_breaks\": " << sample.block_breaks
           << ", \"block_places\": " << sample.block_places
           << ", \"stream_chunk_changes\": " << sample.stream_chunk_changes
           << ", \"generation_enqueued\": " << sample.generation_enqueued
           << ", \"generation_pruned\": " << sample.generation_pruned
           << ", \"unloaded_chunks\": " << sample.unloaded_chunks
           << ", \"generated_chunks\": " << sample.generated_chunks
           << ", \"meshed_chunks\": " << sample.meshed_chunks
           << ", \"light_nodes_processed\": " << sample.light_nodes_processed
           << ", \"lighting_jobs_completed\": " << sample.lighting_jobs_completed
           << ", \"uploaded_meshes\": " << sample.uploaded_meshes
           << ", \"visible_chunks_max\": " << sample.visible_chunks_max
           << ", \"shadow_chunks_max\": " << sample.shadow_chunks_max
           << ", \"world_chunks_max\": " << sample.world_chunks_max
           << ", \"pending_generation_max\": " << sample.pending_generation_max
           << ", \"pending_mesh_max\": " << sample.pending_mesh_max
           << ", \"pending_lighting_max\": " << sample.pending_lighting_max
           << ", \"pending_fluid_max\": " << sample.pending_fluid_max
           << ", \"process_working_set_bytes_max\": " << sample.process_working_set_bytes_max
           << ", \"process_private_bytes_max\": " << sample.process_private_bytes_max
           << ", \"creature_spawns\": " << sample.creature_spawns
           << ", \"creature_despawns\": " << sample.creature_despawns
           << ", \"creature_attacks\": " << sample.creature_attacks
           << ", \"active_creatures_max\": " << sample.active_creatures_max
           << ", \"item_spawns\": " << sample.item_spawns
           << ", \"item_merges\": " << sample.item_merges
           << ", \"item_pickups\": " << sample.item_pickups
           << ", \"item_expired\": " << sample.item_expired
           << ", \"active_item_drops_max\": " << sample.active_item_drops_max
           << ", \"spike_frames\": " << sample.spike_frames
           << "}";
    return stream.str();
}

auto category_counts_json(const std::array<std::size_t, kAuditCategoryCount>& counts) -> std::string {
    std::ostringstream stream;
    stream << "{";
    for (std::size_t index = 0; index < kAuditCategoryCount; ++index) {
        const auto category = static_cast<AuditEventCategory>(index);
        stream << audit_json_string(audit_event_category_name(category))
               << ": " << counts[index];
        if (index + 1 != kAuditCategoryCount) {
            stream << ", ";
        }
    }
    stream << "}";
    return stream.str();
}

auto collect_frame_lines(const std::deque<AuditFrameSample>& samples,
                         const PerformanceRunReport& report,
                         const AuditOptions& options) -> std::vector<std::string> {
    std::vector<std::string> lines;
    if (samples.empty()) {
        return lines;
    }

    if (options.mode == AuditMode::Forensic || options.trace_frames) {
        lines.reserve(samples.size());
        for (const auto& sample : samples) {
            lines.push_back(audit_frame_to_json(sample));
        }
        return lines;
    }

    constexpr std::size_t kSpikeContextFrames = 2;
    for (const auto& sample : samples) {
        bool should_write = false;
        for (const auto& window : report.spike_windows) {
            const auto start_frame = window.start_frame > kSpikeContextFrames
                                         ? window.start_frame - kSpikeContextFrames
                                         : 0;
            const auto end_frame = window.end_frame + kSpikeContextFrames;
            if (sample.frame_index >= start_frame && sample.frame_index <= end_frame) {
                should_write = true;
                break;
            }
        }

        if (should_write) {
            lines.push_back(audit_frame_to_json(sample));
        }
    }
    return lines;
}

} // namespace

auto audit_mode_name(AuditMode mode) noexcept -> std::string_view {
    switch (mode) {
    case AuditMode::Measure:
        return "measure";
    case AuditMode::Forensic:
        return "forensic";
    default:
        return "measure";
    }
}

auto audit_event_category_name(AuditEventCategory category) noexcept -> std::string_view {
    switch (category) {
    case AuditEventCategory::Session:
        return "session";
    case AuditEventCategory::InputRaw:
        return "input_raw";
    case AuditEventCategory::InputAction:
        return "input_action";
    case AuditEventCategory::Ui:
        return "ui";
    case AuditEventCategory::Player:
        return "player";
    case AuditEventCategory::World:
        return "world";
    case AuditEventCategory::Render:
        return "render";
    case AuditEventCategory::Performance:
        return "performance";
    case AuditEventCategory::Creatures:
        return "creatures";
    case AuditEventCategory::Items:
        return "items";
    default:
        return "session";
    }
}

auto audit_severity_name(AuditSeverity severity) noexcept -> std::string_view {
    switch (severity) {
    case AuditSeverity::Trace:
        return "trace";
    case AuditSeverity::Info:
        return "info";
    case AuditSeverity::Warning:
        return "warning";
    case AuditSeverity::Error:
        return "error";
    default:
        return "info";
    }
}

auto audit_run_status_name(AuditRunStatus status) noexcept -> std::string_view {
    switch (status) {
    case AuditRunStatus::Completed:
        return "completed";
    case AuditRunStatus::Aborted:
        return "aborted";
    case AuditRunStatus::AuditDisabled:
        return "audit_disabled";
    default:
        return "audit_disabled";
    }
}

auto parse_audit_mode(std::string_view text) -> std::optional<AuditMode> {
    if (text == "measure") {
        return AuditMode::Measure;
    }
    if (text == "forensic") {
        return AuditMode::Forensic;
    }
    return std::nullopt;
}

auto sanitize_audit_label(std::string_view label) -> std::string {
    std::string sanitized;
    sanitized.reserve(label.size());

    for (const auto character : label) {
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' ||
            character == '_') {
            sanitized.push_back(static_cast<char>(character));
            continue;
        }
        if (character == ' ' || character == '/' || character == '\\' || character == '.') {
            sanitized.push_back('-');
        }
    }

    while (!sanitized.empty() && sanitized.front() == '-') {
        sanitized.erase(sanitized.begin());
    }
    while (!sanitized.empty() && sanitized.back() == '-') {
        sanitized.pop_back();
    }

    if (sanitized.empty()) {
        return "interactive";
    }
    return sanitized;
}

auto make_audit_run_paths(const AuditOptions& options,
                          std::string_view session_id,
                          const std::chrono::system_clock::time_point& started_at) -> AuditRunPaths {
    AuditRunPaths paths {};
    const auto root_directory = std::filesystem::absolute(options.root_directory);
    const auto run_directory_name = run_directory_stamp(started_at) + "-" +
                                    sanitize_audit_label(session_id) + "-" +
                                    std::string(audit_mode_name(options.mode)) + "-" +
                                    sanitize_audit_label(options.label);

    paths.root_directory = root_directory;
    paths.run_directory = root_directory / "runs" / run_directory_name;
    paths.manifest_path = paths.run_directory / "manifest.json";
    paths.summary_json_path = paths.run_directory / "summary.json";
    paths.summary_text_path = paths.run_directory / "summary.txt";
    paths.events_path = paths.run_directory / "events.jsonl";
    paths.seconds_path = paths.run_directory / "seconds.jsonl";
    paths.frames_path = paths.run_directory / "frames.jsonl";
    paths.spikes_path = paths.run_directory / "spikes.json";
    return paths;
}

auto audit_json_string(std::string_view text) -> std::string {
    return "\"" + json_escape(text) + "\"";
}

auto audit_json_object(std::initializer_list<AuditJsonField> fields) -> std::string {
    std::ostringstream stream;
    stream << '{';
    auto iterator = fields.begin();
    while (iterator != fields.end()) {
        stream << audit_json_string(iterator->key) << ": " << iterator->value_json;
        ++iterator;
        if (iterator != fields.end()) {
            stream << ", ";
        }
    }
    stream << '}';
    return stream.str();
}

auto audit_json_array(const std::vector<std::string>& values_json) -> std::string {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < values_json.size(); ++index) {
        stream << values_json[index];
        if (index + 1 != values_json.size()) {
            stream << ", ";
        }
    }
    stream << ']';
    return stream.str();
}

struct AuditWriter::Impl {
    enum class Target : std::uint8_t {
        Events = 0,
        Seconds,
        Frames,
    };

    struct PendingLine {
        Target target = Target::Events;
        std::string line {};
        AuditPriority priority = AuditPriority::Normal;
        AuditEventCategory category = AuditEventCategory::Session;
    };

    AuditOptions options {};
    AuditRunPaths paths {};
    std::ofstream events_stream {};
    std::ofstream seconds_stream {};
    std::ofstream frames_stream {};
    std::deque<PendingLine> queue {};
    std::mutex mutex {};
    std::condition_variable wake_condition {};
    std::thread worker {};
    std::vector<std::string> errors {};
    AuditWriterCounters counters {};
    bool started = false;
    bool stopping = false;
    bool stop_requested = false;
    bool failed = false;

    [[nodiscard]] auto is_active_unlocked() const noexcept -> bool {
        return started && !stop_requested && !failed;
    }

    void append_errors_unlocked(std::vector<std::string>* output) const {
        if (output != nullptr && !errors.empty()) {
            output->insert(output->end(), errors.begin(), errors.end());
        }
    }

    [[nodiscard]] auto open_stream(std::ofstream& stream, const std::filesystem::path& path) -> bool {
        stream.open(path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            errors.push_back("Unable to open audit stream: " + path.string());
            failed = true;
            return false;
        }
        return true;
    }

    void close_streams_unlocked() {
        if (events_stream.is_open()) {
            events_stream.close();
        }
        if (seconds_stream.is_open()) {
            seconds_stream.close();
        }
        if (frames_stream.is_open()) {
            frames_stream.close();
        }
        events_stream.clear();
        seconds_stream.clear();
        frames_stream.clear();
    }

    void count_written_unlocked(const PendingLine& entry) noexcept {
        switch (entry.target) {
        case Target::Events:
            ++counters.written_event_counts[audit_event_category_index(entry.category)];
            break;
        case Target::Seconds:
            ++counters.written_second_samples;
            break;
        case Target::Frames:
            ++counters.written_frames;
            break;
        }
    }

    void count_dropped_unlocked(const PendingLine& entry) noexcept {
        switch (entry.target) {
        case Target::Events:
            ++counters.dropped_event_counts[audit_event_category_index(entry.category)];
            break;
        case Target::Seconds:
            ++counters.dropped_second_samples;
            break;
        case Target::Frames:
            ++counters.dropped_frames;
            break;
        }
    }

    [[nodiscard]] auto flush_batch(const std::deque<PendingLine>& batch) -> bool {
        std::size_t written_count = 0;
        for (const auto& entry : batch) {
            std::ofstream* target = nullptr;
            switch (entry.target) {
            case Target::Events:
                target = &events_stream;
                break;
            case Target::Seconds:
                target = &seconds_stream;
                break;
            case Target::Frames:
                target = &frames_stream;
                break;
            }

            (*target) << entry.line << '\n';
            if (!target->good()) {
                std::lock_guard lock(mutex);
                for (std::size_t index = 0; index < written_count; ++index) {
                    count_written_unlocked(batch[index]);
                }
                for (std::size_t index = written_count; index < batch.size(); ++index) {
                    count_dropped_unlocked(batch[index]);
                }
                for (const auto& queued_entry : queue) {
                    count_dropped_unlocked(queued_entry);
                }
                queue.clear();
                failed = true;
                errors.emplace_back("Audit writer failed while flushing JSONL output");
                wake_condition.notify_all();
                return false;
            }
            ++written_count;
        }

        {
            std::lock_guard lock(mutex);
            for (const auto& entry : batch) {
                count_written_unlocked(entry);
            }
        }
        return true;
    }

    void worker_main() {
        const auto flush_interval =
            std::chrono::milliseconds(std::max(options.writer_flush_interval_ms, 1));
        const auto max_batch_size =
            std::max<std::size_t>(options.writer_batch_size, static_cast<std::size_t>(1));

        for (;;) {
            std::deque<PendingLine> batch;
            {
                std::unique_lock lock(mutex);
                wake_condition.wait_for(lock, flush_interval, [&] {
                    return stop_requested || failed || queue.size() >= max_batch_size;
                });

                if (queue.empty()) {
                    if (stop_requested || failed) {
                        break;
                    }
                    continue;
                }

                const auto batch_count = std::min(queue.size(), max_batch_size);
                for (std::size_t index = 0; index < batch_count; ++index) {
                    batch.push_back(std::move(queue.front()));
                    queue.pop_front();
                }
            }

            if (!flush_batch(batch)) {
                break;
            }
        }
    }

    [[nodiscard]] auto enqueue_line(PendingLine line) -> bool {
        std::lock_guard lock(mutex);
        if (!is_active_unlocked()) {
            return false;
        }

        if (options.writer_queue_capacity == 0) {
            count_dropped_unlocked(line);
            return false;
        }

        if (queue.size() >= options.writer_queue_capacity) {
            const auto droppable = std::min_element(queue.begin(), queue.end(), [](const PendingLine& left,
                                                                                   const PendingLine& right) {
                return static_cast<std::uint8_t>(left.priority) < static_cast<std::uint8_t>(right.priority);
            });

            if (droppable == queue.end() ||
                static_cast<std::uint8_t>(droppable->priority) >= static_cast<std::uint8_t>(line.priority)) {
                count_dropped_unlocked(line);
                return false;
            }

            count_dropped_unlocked(*droppable);
            queue.erase(droppable);
        }

        queue.push_back(std::move(line));
        wake_condition.notify_one();
        return true;
    }
};

AuditWriter::AuditWriter()
    : impl_(std::make_unique<Impl>()) {
}

AuditWriter::~AuditWriter() {
    stop(nullptr);
}

auto AuditWriter::start(const AuditOptions& options,
                        const AuditRunPaths& paths,
                        std::vector<std::string>* errors) -> bool {
    std::unique_lock lock(impl_->mutex);
    if (impl_->started || impl_->stopping || impl_->worker.joinable()) {
        impl_->errors.emplace_back("Audit writer is already running or stopping");
        impl_->append_errors_unlocked(errors);
        return false;
    }

    impl_->options = options;
    impl_->paths = paths;
    impl_->errors.clear();
    impl_->queue.clear();
    impl_->counters = {};
    impl_->stop_requested = false;
    impl_->failed = false;
    impl_->close_streams_unlocked();

    try {
        std::filesystem::create_directories(paths.run_directory);
    } catch (const std::exception& exception) {
        impl_->failed = true;
        impl_->errors.push_back(std::string("Unable to create audit run directory: ") + exception.what());
        impl_->append_errors_unlocked(errors);
        return false;
    }

    if (!impl_->open_stream(impl_->events_stream, paths.events_path) ||
        !impl_->open_stream(impl_->seconds_stream, paths.seconds_path) ||
        !impl_->open_stream(impl_->frames_stream, paths.frames_path)) {
        impl_->close_streams_unlocked();
        impl_->append_errors_unlocked(errors);
        return false;
    }

    impl_->started = true;
    try {
        impl_->worker = std::thread([state = impl_.get()] { state->worker_main(); });
    } catch (const std::exception& exception) {
        impl_->started = false;
        impl_->failed = true;
        impl_->close_streams_unlocked();
        impl_->errors.push_back(std::string("Unable to start audit writer thread: ") + exception.what());
        impl_->append_errors_unlocked(errors);
        return false;
    }
    return true;
}

void AuditWriter::stop(std::vector<std::string>* errors) {
    std::thread worker;
    {
        std::unique_lock lock(impl_->mutex);
        if (impl_->stopping) {
            impl_->wake_condition.wait(lock, [&] { return !impl_->stopping; });
            impl_->append_errors_unlocked(errors);
            return;
        }
        if (!impl_->started) {
            impl_->append_errors_unlocked(errors);
            return;
        }

        impl_->stop_requested = true;
        impl_->started = false;
        impl_->stopping = true;
        worker = std::move(impl_->worker);
        impl_->wake_condition.notify_all();
    }

    if (worker.joinable()) {
        worker.join();
    }

    {
        std::lock_guard lock(impl_->mutex);
        impl_->close_streams_unlocked();
        impl_->stopping = false;
        impl_->wake_condition.notify_all();
        impl_->append_errors_unlocked(errors);
    }
}

auto AuditWriter::enqueue_event(std::string line, AuditPriority priority, AuditEventCategory category) -> bool {
    return impl_->enqueue_line({Impl::Target::Events, std::move(line), priority, category});
}

auto AuditWriter::enqueue_second(std::string line, AuditPriority priority) -> bool {
    return impl_->enqueue_line({Impl::Target::Seconds, std::move(line), priority, AuditEventCategory::Performance});
}

auto AuditWriter::enqueue_frame(std::string line, AuditPriority priority) -> bool {
    return impl_->enqueue_line({Impl::Target::Frames, std::move(line), priority, AuditEventCategory::Performance});
}

auto AuditWriter::active() const noexcept -> bool {
    std::lock_guard lock(impl_->mutex);
    return impl_->is_active_unlocked();
}

auto AuditWriter::counters() const -> AuditWriterCounters {
    std::lock_guard lock(impl_->mutex);
    return impl_->counters;
}

auto AuditWriter::dropped_event_counts() const -> std::array<std::size_t, kAuditCategoryCount> {
    return counters().dropped_event_counts;
}

auto build_summary_text(const AuditManifest& manifest,
                        const PerformanceRunReport& report,
                        std::size_t total_written_events,
                        std::size_t total_dropped_events) -> std::string {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3);
    stream << "ValCraft audit summary";
    if (!manifest.label.empty()) {
        stream << " [" << manifest.label << "]";
    }
    stream << '\n';
    stream << "  session_id=" << manifest.session_id
           << " mode=" << audit_mode_name(manifest.mode)
           << " status=" << audit_run_status_name(manifest.status) << '\n';
    stream << "  duration_seconds=" << manifest.duration_seconds
           << " recorded_frames=" << manifest.recorded_frames
           << " written_frames=" << manifest.written_frames
           << " dropped_frames=" << manifest.dropped_frames
           << " second_samples=" << manifest.second_samples
           << " dropped_second_samples=" << manifest.dropped_second_samples << '\n';
    stream << "  events_written=" << total_written_events
           << " events_dropped=" << total_dropped_events
           << " spike_windows=" << report.spike_windows.size() << '\n';
    stream << "  run_directory=" << manifest.paths.run_directory.string() << '\n';

    const auto performance_summary = format_performance_report(report);
    if (!performance_summary.empty()) {
        stream << performance_summary;
    }
    return stream.str();
}

auto build_summary_json(const AuditManifest& manifest,
                        const PerformanceRunReport& report,
                        std::size_t total_written_events,
                        std::size_t total_dropped_events) -> std::string {
    std::vector<std::string> arguments_json;
    arguments_json.reserve(manifest.arguments.size());
    for (const auto& argument : manifest.arguments) {
        arguments_json.push_back(audit_json_string(argument));
    }

    std::vector<std::string> errors_json;
    errors_json.reserve(manifest.errors.size());
    for (const auto& error : manifest.errors) {
        errors_json.push_back(audit_json_string(error));
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6);
    stream << "{\n";
    stream << "  \"schema_version\": " << manifest.schema_version << ",\n";
    stream << "  \"session_id\": " << audit_json_string(manifest.session_id) << ",\n";
    stream << "  \"label\": " << audit_json_string(manifest.label) << ",\n";
    stream << "  \"mode\": " << audit_json_string(audit_mode_name(manifest.mode)) << ",\n";
    stream << "  \"status\": " << audit_json_string(audit_run_status_name(manifest.status)) << ",\n";
    stream << "  \"platform\": " << audit_json_string(manifest.platform) << ",\n";
    stream << "  \"build_type\": " << audit_json_string(manifest.build_type) << ",\n";
    stream << "  \"working_directory\": " << audit_json_string(manifest.working_directory.string()) << ",\n";
    stream << "  \"arguments\": " << audit_json_array(arguments_json) << ",\n";
    stream << "  \"started_at_utc\": " << audit_json_string(manifest.started_at_utc) << ",\n";
    stream << "  \"ended_at_utc\": " << audit_json_string(manifest.ended_at_utc) << ",\n";
    stream << "  \"duration_seconds\": " << manifest.duration_seconds << ",\n";
    stream << "  \"counts\": {\n";
    stream << "    \"events_written\": " << total_written_events << ",\n";
    stream << "    \"events_dropped\": " << total_dropped_events << ",\n";
    stream << "    \"second_samples\": " << manifest.second_samples << ",\n";
    stream << "    \"dropped_second_samples\": " << manifest.dropped_second_samples << ",\n";
    stream << "    \"recorded_frames\": " << manifest.recorded_frames << ",\n";
    stream << "    \"written_frames\": " << manifest.written_frames << ",\n";
    stream << "    \"dropped_frames\": " << manifest.dropped_frames << ",\n";
    stream << "    \"spike_windows\": " << report.spike_windows.size() << ",\n";
    stream << "    \"written_event_counts\": " << category_counts_json(manifest.written_event_counts) << ",\n";
    stream << "    \"dropped_event_counts\": " << category_counts_json(manifest.dropped_event_counts) << '\n';
    stream << "  },\n";
    stream << "  \"files\": {\n";
    stream << "    \"manifest\": " << audit_json_string(manifest.paths.manifest_path.string()) << ",\n";
    stream << "    \"summary_json\": " << audit_json_string(manifest.paths.summary_json_path.string()) << ",\n";
    stream << "    \"summary_txt\": " << audit_json_string(manifest.paths.summary_text_path.string()) << ",\n";
    stream << "    \"events\": " << audit_json_string(manifest.paths.events_path.string()) << ",\n";
    stream << "    \"seconds\": " << audit_json_string(manifest.paths.seconds_path.string()) << ",\n";
    stream << "    \"frames\": " << audit_json_string(manifest.paths.frames_path.string()) << ",\n";
    stream << "    \"spikes\": " << audit_json_string(manifest.paths.spikes_path.string()) << '\n';
    stream << "  },\n";
    stream << "  \"errors\": " << audit_json_array(errors_json) << ",\n";
    stream << "  \"performance_report\": " << indent_block(format_performance_json(report), "  ");
    stream << "}\n";
    return stream.str();
}

auto build_manifest_json(const AuditManifest& manifest) -> std::string {
    std::vector<std::string> arguments_json;
    arguments_json.reserve(manifest.arguments.size());
    for (const auto& argument : manifest.arguments) {
        arguments_json.push_back(audit_json_string(argument));
    }

    std::vector<std::string> errors_json;
    errors_json.reserve(manifest.errors.size());
    for (const auto& error : manifest.errors) {
        errors_json.push_back(audit_json_string(error));
    }

    std::vector<std::string> files_json;
    files_json.reserve(manifest.produced_files.size());
    for (const auto& file : manifest.produced_files) {
        files_json.push_back(audit_json_string(file));
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6);
    stream << "{\n";
    stream << "  \"schema_version\": " << manifest.schema_version << ",\n";
    stream << "  \"session_id\": " << audit_json_string(manifest.session_id) << ",\n";
    stream << "  \"label\": " << audit_json_string(manifest.label) << ",\n";
    stream << "  \"mode\": " << audit_json_string(audit_mode_name(manifest.mode)) << ",\n";
    stream << "  \"status\": " << audit_json_string(audit_run_status_name(manifest.status)) << ",\n";
    stream << "  \"platform\": " << audit_json_string(manifest.platform) << ",\n";
    stream << "  \"build_type\": " << audit_json_string(manifest.build_type) << ",\n";
    stream << "  \"working_directory\": " << audit_json_string(manifest.working_directory.string()) << ",\n";
    stream << "  \"arguments\": " << audit_json_array(arguments_json) << ",\n";
    stream << "  \"started_at_utc\": " << audit_json_string(manifest.started_at_utc) << ",\n";
    stream << "  \"ended_at_utc\": " << audit_json_string(manifest.ended_at_utc) << ",\n";
    stream << "  \"duration_seconds\": " << manifest.duration_seconds << ",\n";
    stream << "  \"written_event_counts\": " << category_counts_json(manifest.written_event_counts) << ",\n";
    stream << "  \"dropped_event_counts\": " << category_counts_json(manifest.dropped_event_counts) << ",\n";
    stream << "  \"second_samples\": " << manifest.second_samples << ",\n";
    stream << "  \"dropped_second_samples\": " << manifest.dropped_second_samples << ",\n";
    stream << "  \"recorded_frames\": " << manifest.recorded_frames << ",\n";
    stream << "  \"written_frames\": " << manifest.written_frames << ",\n";
    stream << "  \"dropped_frames\": " << manifest.dropped_frames << ",\n";
    stream << "  \"errors\": " << audit_json_array(errors_json) << ",\n";
    stream << "  \"produced_files\": " << audit_json_array(files_json) << '\n';
    stream << "}\n";
    return stream.str();
}

auto build_spikes_json(const AuditManifest& manifest,
                       const PerformanceRunReport& report,
                       const std::deque<AuditEvent>& events) -> std::string {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6);
    stream << "{\n";
    stream << "  \"schema_version\": " << kAuditSchemaVersion << ",\n";
    stream << "  \"session_id\": " << audit_json_string(manifest.session_id) << ",\n";
    stream << "  \"mode\": " << audit_json_string(audit_mode_name(manifest.mode)) << ",\n";
    stream << "  \"spikes\": [\n";
    for (std::size_t index = 0; index < report.spike_windows.size(); ++index) {
        const auto& window = report.spike_windows[index];
        std::vector<std::string> window_events;
        for (const auto& event : events) {
            if (event.frame_index >= window.start_frame && event.frame_index <= window.end_frame) {
                window_events.push_back(audit_event_to_json(event));
            }
        }

        const auto peak_frame = std::find_if(
            report.frames.begin(),
            report.frames.end(),
            [&](const FramePerformanceSample& sample) { return sample.frame_index == window.peak_frame; });

        stream << "    {\n";
        stream << "      \"start_frame\": " << window.start_frame << ",\n";
        stream << "      \"end_frame\": " << window.end_frame << ",\n";
        stream << "      \"peak_frame\": " << window.peak_frame << ",\n";
        stream << "      \"peak_ms\": " << window.peak_ms << ",\n";
        stream << "      \"dominant_stage\": " << audit_json_string(performance_stage_name(window.dominant_stage)) << ",\n";
        if (peak_frame != report.frames.end()) {
            stream << "      \"peak_frame_sample\": " << frame_sample_to_json(*peak_frame) << ",\n";
        } else {
            stream << "      \"peak_frame_sample\": null,\n";
        }
        stream << "      \"events\": " << audit_json_array(window_events) << '\n';
        stream << "    }";
        if (index + 1 != report.spike_windows.size()) {
            stream << ',';
        }
        stream << '\n';
    }
    stream << "  ]\n";
    stream << "}\n";
    return stream.str();
}

struct AuditRecorder::Impl {
    static constexpr std::size_t kRetainedEventContext = 4'096U;
    static constexpr std::size_t kRetainedMeasureFrames = 36'000U;

    AuditOptions options {};
    AuditStartContext start_context {};
    AuditRunPaths paths {};
    AuditWriter writer {};
    std::vector<std::string> errors {};
    std::deque<AuditEvent> events {};
    std::deque<AuditFrameSample> frames {};
    std::size_t recorded_frame_count = 0;
    std::string session_id {};
    std::chrono::system_clock::time_point started_at_system {};
    std::chrono::steady_clock::time_point started_at_steady {};
    bool enabled = false;
    bool finalized = false;

    [[nodiscard]] auto elapsed_microseconds() const -> std::uint64_t {
        const auto elapsed = std::chrono::steady_clock::now() - started_at_steady;
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
    }

    void disable(std::string message) {
        if (!message.empty()) {
            errors.push_back(std::move(message));
        }
        enabled = false;
    }
};

AuditRecorder::AuditRecorder(AuditOptions options, AuditStartContext start_context)
    : impl_(std::make_unique<Impl>()) {
    impl_->options = std::move(options);
    impl_->start_context = std::move(start_context);
    impl_->started_at_system = std::chrono::system_clock::now();
    impl_->started_at_steady = std::chrono::steady_clock::now();
    impl_->options.label = sanitize_audit_label(impl_->options.label);
    impl_->session_id = make_session_id(impl_->started_at_system);
    impl_->paths = make_audit_run_paths(impl_->options, impl_->session_id, impl_->started_at_system);

    try {
        impl_->enabled = impl_->options.enabled &&
                         impl_->writer.start(impl_->options, impl_->paths, &impl_->errors);
    } catch (const std::exception& exception) {
        impl_->disable(std::string("Audit startup failed: ") + exception.what());
    }
}

AuditRecorder::~AuditRecorder() {
    if (impl_ != nullptr && !impl_->finalized) {
        impl_->writer.stop(&impl_->errors);
    }
}

auto AuditRecorder::enabled() const noexcept -> bool {
    return impl_ != nullptr && impl_->enabled;
}

auto AuditRecorder::options() const noexcept -> const AuditOptions& {
    return impl_->options;
}

auto AuditRecorder::session_id() const noexcept -> std::string_view {
    return impl_->session_id;
}

auto AuditRecorder::paths() const noexcept -> const AuditRunPaths& {
    return impl_->paths;
}

void AuditRecorder::record_event(AuditEvent event, AuditPriority priority) {
    if (!enabled()) {
        return;
    }

    event.schema_version = kAuditSchemaVersion;
    event.session_id = impl_->session_id;
    event.mode = impl_->options.mode;
    event.t_us = impl_->elapsed_microseconds();
    const auto accepted = impl_->writer.enqueue_event(audit_event_to_json(event), priority, event.category);
    if (!accepted) {
        if (!impl_->writer.active()) {
            impl_->disable("Audit writer disabled while recording events");
        }
        return;
    }

    if (impl_->events.size() >= Impl::kRetainedEventContext) {
        impl_->events.pop_front();
    }
    impl_->events.push_back(std::move(event));
}

void AuditRecorder::record_frame(AuditFrameSample sample, AuditPriority priority) {
    if (!enabled()) {
        return;
    }

    sample.schema_version = kAuditSchemaVersion;
    sample.session_id = impl_->session_id;
    sample.mode = impl_->options.mode;
    sample.t_us = impl_->elapsed_microseconds();
    ++impl_->recorded_frame_count;

    if (impl_->options.mode == AuditMode::Forensic || impl_->options.trace_frames) {
        if (!impl_->writer.enqueue_frame(audit_frame_to_json(sample), priority) && !impl_->writer.active()) {
            impl_->disable("Audit writer disabled while recording frame traces");
        }
        return;
    }

    if (impl_->frames.size() >= Impl::kRetainedMeasureFrames) {
        impl_->frames.pop_front();
    }
    impl_->frames.push_back(std::move(sample));
}

void AuditRecorder::record_second(AuditSecondSample sample, AuditPriority priority) {
    if (!enabled()) {
        return;
    }

    sample.schema_version = kAuditSchemaVersion;
    sample.session_id = impl_->session_id;
    sample.mode = impl_->options.mode;
    sample.t_us = impl_->elapsed_microseconds();
    if (!impl_->writer.enqueue_second(audit_second_to_json(sample), priority) && !impl_->writer.active()) {
        impl_->disable("Audit writer disabled while recording second aggregates");
    }
}

void AuditRecorder::record_error(std::string message) {
    if (impl_ == nullptr) {
        return;
    }
    impl_->errors.push_back(std::move(message));
}

void AuditRecorder::finalize(const AuditFinalizeContext& context) {
    if (impl_ == nullptr || impl_->finalized) {
        return;
    }

    impl_->finalized = true;
    impl_->writer.stop(&impl_->errors);
    const auto writer_counters = impl_->writer.counters();
    if (!impl_->options.enabled) {
        return;
    }

    const auto ended_at = std::chrono::system_clock::now();
    auto manifest = AuditManifest {};
    manifest.session_id = impl_->session_id;
    manifest.label = impl_->options.label;
    manifest.mode = impl_->options.mode;
    manifest.status = impl_->enabled ? context.status : AuditRunStatus::AuditDisabled;
    manifest.platform = impl_->start_context.platform;
    manifest.build_type = impl_->start_context.build_type;
    manifest.working_directory = impl_->start_context.working_directory;
    manifest.arguments = impl_->start_context.arguments;
    manifest.started_at_utc = format_iso_utc(impl_->started_at_system);
    manifest.ended_at_utc = format_iso_utc(ended_at);
    manifest.duration_seconds = std::chrono::duration<double>(ended_at - impl_->started_at_system).count();
    manifest.paths = impl_->paths;
    manifest.written_event_counts = writer_counters.written_event_counts;
    manifest.dropped_event_counts = writer_counters.dropped_event_counts;
    manifest.second_samples = writer_counters.written_second_samples;
    manifest.dropped_second_samples = writer_counters.dropped_second_samples;
    manifest.recorded_frames = impl_->recorded_frame_count;
    manifest.errors = impl_->errors;

    const auto frame_lines = collect_frame_lines(impl_->frames, context.performance_report, impl_->options);
    const auto streams_contain_frame_trace = impl_->options.mode == AuditMode::Forensic || impl_->options.trace_frames;
    manifest.written_frames = streams_contain_frame_trace ? writer_counters.written_frames : frame_lines.size();
    manifest.dropped_frames = streams_contain_frame_trace ? writer_counters.dropped_frames : 0;

    const auto should_write_compatibility_json = !impl_->options.compatibility_json_path.empty();
    try {
        if (!streams_contain_frame_trace) {
            write_lines_file(impl_->paths.frames_path, frame_lines);
        }
        write_text_file(impl_->paths.spikes_path, build_spikes_json(manifest, context.performance_report, impl_->events));

        const auto summary_text = build_summary_text(
            manifest,
            context.performance_report,
            total_count(manifest.written_event_counts),
            total_count(manifest.dropped_event_counts));
        write_text_file(impl_->paths.summary_text_path, summary_text);
        write_text_file(
            impl_->paths.summary_json_path,
            build_summary_json(
                manifest,
                context.performance_report,
                total_count(manifest.written_event_counts),
                total_count(manifest.dropped_event_counts)));

        if (should_write_compatibility_json) {
            write_text_file(impl_->options.compatibility_json_path, format_performance_json(context.performance_report));
        }
    } catch (const std::exception& exception) {
        manifest.status = AuditRunStatus::AuditDisabled;
        manifest.errors.push_back(std::string("Audit finalize failed: ") + exception.what());
    }

    manifest.produced_files = {
        impl_->paths.manifest_path.string(),
        impl_->paths.summary_json_path.string(),
        impl_->paths.summary_text_path.string(),
        impl_->paths.events_path.string(),
        impl_->paths.seconds_path.string(),
        impl_->paths.frames_path.string(),
        impl_->paths.spikes_path.string(),
    };
    if (should_write_compatibility_json) {
        manifest.produced_files.push_back(impl_->options.compatibility_json_path);
    }

    try {
        write_text_file(impl_->paths.manifest_path, build_manifest_json(manifest));
    } catch (const std::exception& exception) {
        impl_->errors.push_back(std::string("Audit manifest write failed: ") + exception.what());
    }
}

} // namespace valcraft
