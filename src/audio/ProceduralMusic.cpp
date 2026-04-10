#include "audio/ProceduralMusic.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;

constexpr std::array<int, 4> kDayRoots {0, 5, 7, 9};
constexpr std::array<int, 4> kNightRoots {0, 3, 5, 8};
constexpr std::array<int, 12> kDayMelody {12, 16, 19, 16, 14, 12, 11, 14, 16, 19, 23, 19};
constexpr std::array<int, 12> kNightMelody {12, 15, 19, 15, 12, 10, 12, 15, 17, 15, 12, 7};
constexpr std::array<int, 8> kDayHarmony {7, 11, 14, 11, 16, 14, 11, 7};
constexpr std::array<int, 8> kNightHarmony {7, 10, 12, 10, 15, 12, 10, 7};
constexpr std::array<int, 4> kDayBass {0, 7, 5, 9};
constexpr std::array<int, 4> kNightBass {0, 3, 5, 8};

auto clamp01(float value) noexcept -> float {
    return std::clamp(value, 0.0F, 1.0F);
}

auto mix(float a, float b, float t) noexcept -> float {
    return a + (b - a) * clamp01(t);
}

auto wrap_phase(float phase) noexcept -> float {
    auto wrapped = phase;
    while (wrapped >= 1.0F) {
        wrapped -= 1.0F;
    }
    while (wrapped < 0.0F) {
        wrapped += 1.0F;
    }
    return wrapped;
}

void advance_phase(float& phase, float frequency_hz, float sample_rate) noexcept {
    phase = wrap_phase(phase + frequency_hz / sample_rate);
}

auto harmonic_sine(float phase, float multiplier, float offset_cycles) noexcept -> float {
    const auto angle =
        static_cast<double>(phase * multiplier + offset_cycles) * static_cast<double>(kTwoPi);
    return static_cast<float>(std::sin(angle));
}

auto soft_clip(float value) noexcept -> float {
    return value / (1.0F + std::abs(value));
}

auto midi_to_hz(int midi_note) noexcept -> float {
    return 440.0F * static_cast<float>(std::pow(2.0, static_cast<double>(midi_note - 69) / 12.0));
}

auto smoothing_factor(float response_seconds, float sample_rate) noexcept -> float {
    const auto seconds = std::max(response_seconds, 0.001F);
    const auto rate = std::max(sample_rate, 8000.0F);
    return 1.0F - static_cast<float>(std::exp(-1.0 / (static_cast<double>(seconds) * static_cast<double>(rate))));
}

} // namespace

ProceduralMusicComposer::ProceduralMusicComposer(int sample_rate)
    : sample_rate_(std::max(sample_rate, 8000)) {
    reset();
}

void ProceduralMusicComposer::reset() noexcept {
    target_snapshot_ = {};
    const auto base_interval = 1.0 / static_cast<double>(std::max(target_snapshot_.beat_hz, 0.15F));
    musical_time_seconds_ = 0.0;
    next_melody_time_seconds_ = 0.0;
    next_harmony_time_seconds_ = base_interval;
    next_bass_time_seconds_ = base_interval * 2.0;
    melody_step_index_ = 0U;
    harmony_step_index_ = 0U;
    bass_step_index_ = 0U;
    smoothed_snapshot_ = target_snapshot_;
    mood_initialized_ = false;
    voices_ = {};
    noise_state_ = 0xC0FFEE11U;

    resize_delay_lines();
}

void ProceduralMusicComposer::set_environment(const EnvironmentState& environment,
                                              const CreatureCycleState& cycle,
                                              bool has_active_session,
                                              bool front_end_visible) noexcept {
    const auto daylight_presence = clamp01((environment.daylight_factor - 0.18F) / 0.82F);
    float cycle_night = 0.0F;
    float twilight_presence = 0.0F;

    switch (cycle.phase) {
    case CreaturePhase::Day:
        cycle_night = 0.0F;
        twilight_presence = 0.0F;
        break;
    case CreaturePhase::DuskMorph:
        cycle_night = mix(0.25F, 1.0F, cycle.morph_factor);
        twilight_presence = 1.0F - std::abs(cycle.morph_factor - 0.5F) * 2.0F;
        break;
    case CreaturePhase::Night:
        cycle_night = 1.0F;
        twilight_presence = 0.0F;
        break;
    case CreaturePhase::DawnRecover:
        cycle_night = mix(0.16F, 0.70F, cycle.morph_factor);
        twilight_presence = 1.0F - std::abs(cycle.morph_factor - 0.5F) * 2.0F;
        break;
    }

    const auto night_presence = clamp01(std::max(1.0F - daylight_presence, cycle_night));
    const auto day_presence = clamp01(daylight_presence + twilight_presence * 0.24F - night_presence * 0.12F);

    ProceduralMusicSnapshot snapshot {};
    snapshot.day_presence = day_presence;
    snapshot.night_presence = night_presence;
    snapshot.tension = clamp01(night_presence * 0.12F + cycle_night * 0.06F + twilight_presence * 0.04F);
    snapshot.master_gain = has_active_session ? 0.11F : 0.085F;
    if (front_end_visible) {
        snapshot.master_gain *= has_active_session ? 0.82F : 0.68F;
        snapshot.tension *= has_active_session ? 0.80F : 0.55F;
    }
    snapshot.beat_hz = mix(32.0F / 60.0F, 24.0F / 60.0F, night_presence);
    snapshot.brightness = mix(0.46F, 0.28F, night_presence);
    snapshot.sustain_seconds = mix(2.20F, 2.85F, night_presence);
    snapshot.reverb_feedback = mix(0.40F, 0.48F, night_presence);
    target_snapshot_ = snapshot;

    if (!mood_initialized_) {
        smoothed_snapshot_ = target_snapshot_;
        const auto base_interval = 1.0 / static_cast<double>(std::max(target_snapshot_.beat_hz, 0.15F));
        next_melody_time_seconds_ = 0.0;
        next_harmony_time_seconds_ = base_interval;
        next_bass_time_seconds_ = base_interval * 2.0;
        mood_initialized_ = true;
    }
}

auto ProceduralMusicComposer::mood_snapshot() const noexcept -> ProceduralMusicSnapshot {
    return target_snapshot_;
}

auto ProceduralMusicComposer::sample_rate() const noexcept -> int {
    return sample_rate_;
}

void ProceduralMusicComposer::render_interleaved(std::span<float> stereo_samples) {
    if (stereo_samples.size() < 2) {
        return;
    }

    const auto frame_count = stereo_samples.size() / 2U;
    const auto mood_response = smoothing_factor(0.45F, static_cast<float>(sample_rate_));

    for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        smoothed_snapshot_.day_presence += (target_snapshot_.day_presence - smoothed_snapshot_.day_presence) * mood_response;
        smoothed_snapshot_.night_presence +=
            (target_snapshot_.night_presence - smoothed_snapshot_.night_presence) * mood_response;
        smoothed_snapshot_.tension += (target_snapshot_.tension - smoothed_snapshot_.tension) * mood_response;
        smoothed_snapshot_.master_gain += (target_snapshot_.master_gain - smoothed_snapshot_.master_gain) * mood_response;
        smoothed_snapshot_.beat_hz += (target_snapshot_.beat_hz - smoothed_snapshot_.beat_hz) * mood_response;
        smoothed_snapshot_.brightness += (target_snapshot_.brightness - smoothed_snapshot_.brightness) * mood_response;
        smoothed_snapshot_.sustain_seconds +=
            (target_snapshot_.sustain_seconds - smoothed_snapshot_.sustain_seconds) * mood_response;
        smoothed_snapshot_.reverb_feedback +=
            (target_snapshot_.reverb_feedback - smoothed_snapshot_.reverb_feedback) * mood_response;

        schedule_due_notes();

        auto dry_left = 0.0F;
        auto dry_right = 0.0F;
        for (auto& voice : voices_) {
            const auto sample = render_voice(voice);
            const auto pan = std::clamp(voice.pan, -0.95F, 0.95F);
            dry_left += sample * (0.5F * (1.0F - pan));
            dry_right += sample * (0.5F * (1.0F + pan));
        }

        dry_left *= smoothed_snapshot_.master_gain;
        dry_right *= smoothed_snapshot_.master_gain;

        const auto wet_left = delay_left_[delay_index_];
        const auto wet_right = delay_right_[delay_index_];
        delay_left_[delay_index_] = dry_left * 0.26F + wet_right * smoothed_snapshot_.reverb_feedback;
        delay_right_[delay_index_] = dry_right * 0.26F + wet_left * smoothed_snapshot_.reverb_feedback;
        delay_index_ = (delay_index_ + 1U) % delay_left_.size();

        stereo_samples[frame_index * 2U] = soft_clip(dry_left + wet_left * 0.52F);
        stereo_samples[frame_index * 2U + 1U] = soft_clip(dry_right + wet_right * 0.52F);

        musical_time_seconds_ += 1.0 / static_cast<double>(sample_rate_);
    }
}

void ProceduralMusicComposer::resize_delay_lines() {
    const auto delay_frames = static_cast<std::size_t>(std::max(sample_rate_ / 4, 2048));
    delay_left_.assign(delay_frames, 0.0F);
    delay_right_.assign(delay_frames, 0.0F);
    delay_index_ = 0U;
}

void ProceduralMusicComposer::schedule_due_notes() noexcept {
    const auto safe_beat = std::max(smoothed_snapshot_.beat_hz, 0.15F);
    const auto melody_interval = 1.0 / static_cast<double>(safe_beat);
    const auto harmony_interval = melody_interval * 2.0;
    const auto bass_interval = melody_interval * 4.0;
    const auto night_palette = smoothed_snapshot_.night_presence > smoothed_snapshot_.day_presence;

    // Je declenche les notes sur une grille lente pour garder un vrai piano d'ambiance.
    while (musical_time_seconds_ >= next_melody_time_seconds_) {
        const auto& roots = night_palette ? kNightRoots : kDayRoots;
        const auto& melody = night_palette ? kNightMelody : kDayMelody;
        const auto root_index = (melody_step_index_ / 6U) % roots.size();
        const auto root_midi = (night_palette ? 45 : 52) + roots[root_index];
        const auto note = root_midi + melody[melody_step_index_ % melody.size()];
        const auto velocity =
            (0.20F + smoothed_snapshot_.brightness * 0.06F) * (0.92F + next_random_unit() * 0.12F);
        trigger_piano_note(note,
                           velocity,
                           next_random_bipolar() * 0.18F,
                           smoothed_snapshot_.brightness,
                           smoothed_snapshot_.sustain_seconds);
        next_melody_time_seconds_ += melody_interval;
        ++melody_step_index_;
    }

    while (musical_time_seconds_ >= next_harmony_time_seconds_) {
        const auto& roots = night_palette ? kNightRoots : kDayRoots;
        const auto& harmony = night_palette ? kNightHarmony : kDayHarmony;
        const auto root_index = (melody_step_index_ / 6U) % roots.size();
        const auto root_midi = (night_palette ? 45 : 52) + roots[root_index];
        const auto note = root_midi + harmony[harmony_step_index_ % harmony.size()];
        trigger_piano_note(note,
                           0.11F * (0.94F + next_random_unit() * 0.10F),
                           next_random_bipolar() * 0.12F,
                           smoothed_snapshot_.brightness * 0.82F,
                           smoothed_snapshot_.sustain_seconds * 1.10F);
        next_harmony_time_seconds_ += harmony_interval;
        ++harmony_step_index_;
    }

    while (musical_time_seconds_ >= next_bass_time_seconds_) {
        const auto& roots = night_palette ? kNightRoots : kDayRoots;
        const auto& bass = night_palette ? kNightBass : kDayBass;
        const auto root_index = (bass_step_index_ / 2U) % roots.size();
        const auto root_midi = (night_palette ? 33 : 40) + roots[root_index];
        const auto note = root_midi + bass[bass_step_index_ % bass.size()];
        trigger_piano_note(note,
                           0.13F * (0.95F + next_random_unit() * 0.10F),
                           -0.10F + next_random_bipolar() * 0.04F,
                           smoothed_snapshot_.brightness * 0.60F,
                           smoothed_snapshot_.sustain_seconds * 1.35F);
        next_bass_time_seconds_ += bass_interval;
        ++bass_step_index_;
    }
}

void ProceduralMusicComposer::trigger_piano_note(int midi_note,
                                                 float amplitude,
                                                 float pan,
                                                 float brightness,
                                                 float sustain_seconds) noexcept {
    auto* selected_voice = &voices_.front();
    for (auto& voice : voices_) {
        if (!voice.active) {
            selected_voice = &voice;
            break;
        }
        if (voice.age_seconds > selected_voice->age_seconds) {
            selected_voice = &voice;
        }
    }

    // Je recycle la voix la plus avancee pour garder une polyphonie stable sans allocation audio.
    selected_voice->frequency_hz = midi_to_hz(midi_note);
    selected_voice->phase = 0.0F;
    selected_voice->age_seconds = 0.0F;
    selected_voice->amplitude = std::clamp(amplitude, 0.02F, 0.42F);
    selected_voice->brightness = std::clamp(brightness, 0.12F, 0.70F);
    selected_voice->sustain_seconds = std::clamp(sustain_seconds, 0.60F, 4.50F);
    selected_voice->pan = std::clamp(pan, -0.30F, 0.30F);
    selected_voice->active = true;
}

auto ProceduralMusicComposer::render_voice(PianoVoice& voice) noexcept -> float {
    if (!voice.active || voice.frequency_hz <= 0.0F) {
        return 0.0F;
    }

    const auto dt = 1.0F / static_cast<float>(sample_rate_);
    const auto attack = clamp01(voice.age_seconds / 0.010F);
    const auto body_decay = static_cast<float>(
        std::exp(-static_cast<double>(voice.age_seconds) /
                 static_cast<double>(std::max(voice.sustain_seconds, 0.20F))));
    const auto overtone_decay = static_cast<float>(
        std::exp(-static_cast<double>(voice.age_seconds) *
                 static_cast<double>(mix(2.8F, 4.8F, voice.brightness))));
    const auto hammer_decay = static_cast<float>(
        std::exp(-static_cast<double>(voice.age_seconds) *
                 static_cast<double>(mix(18.0F, 30.0F, voice.brightness))));

    const auto fundamental = harmonic_sine(voice.phase, 1.0F, 0.0F);
    const auto second = harmonic_sine(voice.phase, 2.0F, 0.07F);
    const auto third = harmonic_sine(voice.phase, 3.0F, 0.13F);
    const auto fourth = harmonic_sine(voice.phase, 4.0F, 0.19F);
    const auto hammer = harmonic_sine(voice.phase, 8.0F, 0.31F) * hammer_decay;

    const auto piano_tone = fundamental * 0.78F +
                            second * (0.16F + voice.brightness * 0.12F) * overtone_decay +
                            third * (0.08F + voice.brightness * 0.08F) * overtone_decay +
                            fourth * (0.04F + voice.brightness * 0.05F) * overtone_decay +
                            hammer * (0.05F + voice.brightness * 0.07F);

    const auto sample = voice.amplitude * attack * body_decay * piano_tone;

    advance_phase(voice.phase, voice.frequency_hz, static_cast<float>(sample_rate_));
    voice.age_seconds += dt;

    if (body_decay * voice.amplitude < 0.0006F && voice.age_seconds > voice.sustain_seconds * 2.6F) {
        voice.active = false;
    }

    return sample;
}

auto ProceduralMusicComposer::next_random_unit() noexcept -> float {
    noise_state_ = noise_state_ * 1664525U + 1013904223U;
    return static_cast<float>((noise_state_ >> 8U) & 0x00FFFFFFU) / 16777216.0F;
}

auto ProceduralMusicComposer::next_random_bipolar() noexcept -> float {
    return next_random_unit() * 2.0F - 1.0F;
}

} // namespace valcraft
