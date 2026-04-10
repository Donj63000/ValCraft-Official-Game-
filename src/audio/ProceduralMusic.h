#pragma once

#include "world/Environment.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace valcraft {

struct ProceduralMusicSnapshot {
    float day_presence = 1.0F;
    float night_presence = 0.0F;
    float tension = 0.0F;
    float master_gain = 0.11F;
    float beat_hz = 0.48F;
    float brightness = 0.40F;
    float sustain_seconds = 2.30F;
    float reverb_feedback = 0.42F;
};

class ProceduralMusicComposer {
public:
    explicit ProceduralMusicComposer(int sample_rate = 48000);

    void reset() noexcept;
    void set_environment(const EnvironmentState& environment,
                         const CreatureCycleState& cycle,
                         bool has_active_session,
                         bool front_end_visible) noexcept;

    [[nodiscard]] auto mood_snapshot() const noexcept -> ProceduralMusicSnapshot;
    [[nodiscard]] auto sample_rate() const noexcept -> int;

    void render_interleaved(std::span<float> stereo_samples);

private:
    struct PianoVoice {
        float frequency_hz = 0.0F;
        float phase = 0.0F;
        float age_seconds = 0.0F;
        float amplitude = 0.0F;
        float brightness = 0.40F;
        float sustain_seconds = 2.20F;
        float pan = 0.0F;
        bool active = false;
    };

    void resize_delay_lines();
    void schedule_due_notes() noexcept;
    void trigger_piano_note(int midi_note,
                            float amplitude,
                            float pan,
                            float brightness,
                            float sustain_seconds) noexcept;
    [[nodiscard]] auto render_voice(PianoVoice& voice) noexcept -> float;
    [[nodiscard]] auto next_random_unit() noexcept -> float;
    [[nodiscard]] auto next_random_bipolar() noexcept -> float;

    int sample_rate_ = 48000;
    double musical_time_seconds_ = 0.0;
    double next_melody_time_seconds_ = 0.0;
    double next_harmony_time_seconds_ = 0.0;
    double next_bass_time_seconds_ = 0.0;
    std::size_t melody_step_index_ = 0;
    std::size_t harmony_step_index_ = 0;
    std::size_t bass_step_index_ = 0;

    ProceduralMusicSnapshot target_snapshot_ {};
    ProceduralMusicSnapshot smoothed_snapshot_ {};
    bool mood_initialized_ = false;
    std::array<PianoVoice, 8> voices_ {};

    std::uint32_t noise_state_ = 0xC0FFEE11U;

    std::vector<float> delay_left_ {};
    std::vector<float> delay_right_ {};
    std::size_t delay_index_ = 0;
};

} // namespace valcraft
