#pragma once

#include "world/Environment.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace valcraft {

enum class ProceduralMusicScene : std::uint8_t {
    Classic = 0,
    SeaAdventure = 1,
    Backrooms = 2,
};

struct ProceduralMusicContext {
    ProceduralMusicScene scene = ProceduralMusicScene::Classic;
    float voyage_motion = 0.0F;
    float danger = 0.0F;
    std::uint32_t seed = 0U;
};

struct ProceduralMusicSnapshot {
    float day_presence = 1.0F;
    float night_presence = 0.0F;
    float tension = 0.0F;
    float master_gain = 0.11F;
    float beat_hz = 0.48F;
    float brightness = 0.40F;
    float sustain_seconds = 2.30F;
    float reverb_feedback = 0.42F;
    ProceduralMusicScene scene = ProceduralMusicScene::Classic;
    float maritime_presence = 0.0F;
    float voyage_motion = 0.0F;
    float tempo_bpm = 32.0F;
    float storm_presence = 0.0F;
    float percussion_presence = 0.0F;
};

class ProceduralMusicComposer {
public:
    explicit ProceduralMusicComposer(int sample_rate = 48000);

    void reset();
    void set_environment(const EnvironmentState& environment,
                         const CreatureCycleState& cycle,
                         bool has_active_session,
                         bool front_end_visible,
                         const ProceduralMusicContext& context = {}) noexcept;

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

    enum class MaritimeInstrument : std::uint8_t {
        LowStrings = 0,
        Lute,
        Concertina,
        Fiddle,
        Whistle,
        Horn,
    };

    struct MaritimeVoice {
        MaritimeInstrument instrument = MaritimeInstrument::LowStrings;
        float frequency_hz = 0.0F;
        float phase = 0.0F;
        float modulation_phase = 0.0F;
        float age_seconds = 0.0F;
        float duration_seconds = 1.0F;
        float amplitude = 0.0F;
        float brightness = 0.5F;
        float pan = 0.0F;
        bool active = false;
    };

    enum class PercussionKind : std::uint8_t {
        FrameDrum = 0,
        LowTom,
        Shaker,
    };

    struct PercussionVoice {
        PercussionKind kind = PercussionKind::FrameDrum;
        float phase = 0.0F;
        float age_seconds = 0.0F;
        float duration_seconds = 0.2F;
        float amplitude = 0.0F;
        float pan = 0.0F;
        float filter_state = 0.0F;
        std::uint32_t noise_state = 1U;
        bool active = false;
    };

    void resize_delay_lines();
    void schedule_classic_notes() noexcept;
    void schedule_maritime_steps() noexcept;
    void schedule_maritime_step(std::size_t step_index) noexcept;
    void reset_classic_transport() noexcept;
    void reset_maritime_transport(std::uint32_t seed) noexcept;
    void reseed_maritime_variation(std::uint32_t seed) noexcept;
    void trigger_piano_note(int midi_note,
                            float amplitude,
                            float pan,
                            float brightness,
                            float sustain_seconds) noexcept;
    [[nodiscard]] auto render_voice(PianoVoice& voice) noexcept -> float;
    void trigger_maritime_note(MaritimeInstrument instrument,
                               int midi_note,
                               float amplitude,
                               float duration_seconds,
                               float pan,
                               float brightness) noexcept;
    [[nodiscard]] auto render_maritime_voice(MaritimeVoice& voice) noexcept -> float;
    void trigger_percussion(PercussionKind kind,
                            float amplitude,
                            float pan) noexcept;
    [[nodiscard]] auto render_percussion_voice(PercussionVoice& voice) noexcept -> float;
    [[nodiscard]] auto wavetable_sine(float phase) const noexcept -> float;
    [[nodiscard]] auto next_random_unit() noexcept -> float;
    [[nodiscard]] auto next_random_bipolar() noexcept -> float;
    [[nodiscard]] auto next_maritime_random() noexcept -> std::uint32_t;

    int sample_rate_ = 48000;
    double musical_time_seconds_ = 0.0;
    double next_melody_time_seconds_ = 0.0;
    double next_harmony_time_seconds_ = 0.0;
    double next_bass_time_seconds_ = 0.0;
    std::size_t melody_step_index_ = 0;
    std::size_t harmony_step_index_ = 0;
    std::size_t bass_step_index_ = 0;
    double next_maritime_step_time_seconds_ = 0.0;
    std::size_t maritime_step_index_ = 0U;
    std::size_t maritime_motif_step_index_ = 0U;
    std::size_t maritime_variant_ = 0U;

    ProceduralMusicSnapshot target_snapshot_ {};
    ProceduralMusicSnapshot smoothed_snapshot_ {};
    ProceduralMusicSnapshot classic_target_snapshot_ {};
    ProceduralMusicSnapshot classic_smoothed_snapshot_ {};
    bool mood_initialized_ = false;
    std::array<PianoVoice, 8> voices_ {};
    std::array<MaritimeVoice, 24> maritime_voices_ {};
    std::array<PercussionVoice, 8> percussion_voices_ {};

    ProceduralMusicScene requested_scene_ = ProceduralMusicScene::Classic;
    float maritime_mix_ = 0.0F;
    float quantized_voyage_motion_ = 0.0F;
    float quantized_storm_presence_ = 0.0F;
    float quantized_danger_presence_ = 0.0F;
    float quantized_percussion_presence_ = 0.0F;
    float quantized_tempo_bpm_ = 92.0F;
    float target_danger_presence_ = 0.0F;
    std::uint32_t maritime_seed_ = 0U;

    std::uint32_t noise_state_ = 0xC0FFEE11U;
    std::uint32_t maritime_noise_state_ = 0x51A7C0DEU;

    static constexpr std::size_t kWaveTableSize = 2048U;
    std::array<float, kWaveTableSize> sine_table_ {};

    std::vector<float> delay_left_ {};
    std::vector<float> delay_right_ {};
    std::size_t delay_index_ = 0;
    std::vector<float> maritime_delay_left_ {};
    std::vector<float> maritime_delay_right_ {};
    std::size_t maritime_delay_index_ = 0U;
    std::vector<float> diffusion_left_ {};
    std::vector<float> diffusion_right_ {};
    std::size_t diffusion_index_ = 0U;
    float delay_damping_left_ = 0.0F;
    float delay_damping_right_ = 0.0F;
    float diffusion_damping_left_ = 0.0F;
    float diffusion_damping_right_ = 0.0F;
};

} // namespace valcraft
