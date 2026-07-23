#include "audio/ProceduralMusic.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr float kHalfPi = 0.5F * kPi;

constexpr std::array<int, 4> kDayRoots {0, 5, 7, 9};
constexpr std::array<int, 4> kNightRoots {0, 3, 5, 8};
constexpr std::array<int, 12> kDayMelody {12, 16, 19, 16, 14, 12, 11, 14, 16, 19, 23, 19};
constexpr std::array<int, 12> kNightMelody {12, 15, 19, 15, 12, 10, 12, 15, 17, 15, 12, 7};
constexpr std::array<int, 8> kDayHarmony {7, 11, 14, 11, 16, 14, 11, 7};
constexpr std::array<int, 8> kNightHarmony {7, 10, 12, 10, 15, 12, 10, 7};
constexpr std::array<int, 4> kDayBass {0, 7, 5, 9};
constexpr std::array<int, 4> kNightBass {0, 3, 5, 8};

struct MaritimeChord {
    int bass_note = 45;
    std::array<int, 4> notes {57, 60, 64, 71};
};

// Je garde une progression originale en la dorien et je reserve la dominante
// majeure a la cadence afin d'obtenir une couleur de flibuste sans citer un theme existant.
constexpr std::array<MaritimeChord, 8> kMaritimeChords {{
    {45, {57, 60, 64, 71}}, // Am(add9)
    {43, {55, 59, 62, 67}}, // G
    {42, {50, 54, 57, 62}}, // D/F#
    {40, {52, 55, 59, 64}}, // Em
    {36, {48, 52, 55, 57}}, // Cmaj6
    {47, {55, 59, 62, 67}}, // G/B
    {38, {50, 55, 57, 62}}, // Dsus4 puis D
    {40, {52, 56, 59, 62}}, // E7
}};

constexpr std::array<int, 8> kMaritimeMotifIntervals {0, 5, 2, 9, 7, 3, 5, 0};
constexpr std::array<std::size_t, 6> kMaritimeArpeggio {0U, 2U, 1U, 3U, 2U, 1U};
constexpr std::array<bool, 16> kMaritimeLeadRhythm {
    true, true,
    true, false,
    true, true,
    false, false,
    true, false,
    true, true,
    false, true,
    true, false,
};

auto clamp01(float value) noexcept -> float {
    return std::clamp(value, 0.0F, 1.0F);
}

auto finite_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) ? value : fallback;
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

auto sanitize_scene(ProceduralMusicScene scene) noexcept -> ProceduralMusicScene {
    switch (scene) {
    case ProceduralMusicScene::Classic:
    case ProceduralMusicScene::SeaAdventure:
        return scene;
    }
    return ProceduralMusicScene::Classic;
}

} // namespace

ProceduralMusicComposer::ProceduralMusicComposer(int sample_rate)
    : sample_rate_(std::max(sample_rate, 8000)) {
    reset();
}

void ProceduralMusicComposer::reset() {
    target_snapshot_ = {};
    classic_target_snapshot_ = target_snapshot_;
    musical_time_seconds_ = 0.0;
    smoothed_snapshot_ = target_snapshot_;
    classic_smoothed_snapshot_ = classic_target_snapshot_;
    mood_initialized_ = false;
    requested_scene_ = ProceduralMusicScene::Classic;
    maritime_mix_ = 0.0F;
    target_danger_presence_ = 0.0F;
    noise_state_ = 0xC0FFEE11U;
    maritime_noise_state_ = 0x51A7C0DEU;

    for (std::size_t index = 0U; index < sine_table_.size(); ++index) {
        const auto phase = static_cast<float>(index) / static_cast<float>(sine_table_.size());
        sine_table_[index] = std::sin(phase * kTwoPi);
    }

    reset_classic_transport();
    reset_maritime_transport(0U);
    resize_delay_lines();
}

void ProceduralMusicComposer::set_environment(const EnvironmentState& environment,
                                              const CreatureCycleState& cycle,
                                              bool has_active_session,
                                              bool front_end_visible,
                                              const ProceduralMusicContext& context) noexcept {
    const auto daylight_factor = finite_or(environment.daylight_factor, 1.0F);
    const auto daylight_presence = clamp01((daylight_factor - 0.18F) / 0.82F);
    const auto morph_factor = clamp01(finite_or(cycle.morph_factor, 0.0F));
    float cycle_night = 0.0F;
    float twilight_presence = 0.0F;

    switch (cycle.phase) {
    case CreaturePhase::Day:
        cycle_night = 0.0F;
        twilight_presence = 0.0F;
        break;
    case CreaturePhase::DuskMorph:
        cycle_night = mix(0.25F, 1.0F, morph_factor);
        twilight_presence = 1.0F - std::abs(morph_factor - 0.5F) * 2.0F;
        break;
    case CreaturePhase::Night:
        cycle_night = 1.0F;
        twilight_presence = 0.0F;
        break;
    case CreaturePhase::DawnRecover:
        cycle_night = mix(0.16F, 0.70F, morph_factor);
        twilight_presence = 1.0F - std::abs(morph_factor - 0.5F) * 2.0F;
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
    snapshot.scene = sanitize_scene(context.scene);
    snapshot.tempo_bpm = snapshot.beat_hz * 60.0F;

    // Je conserve les parametres historiques du piano sur un bus separe afin
    // que le fondu maritime ne modifie ni son tempo ni sa couleur.
    classic_target_snapshot_ = snapshot;
    classic_target_snapshot_.scene = ProceduralMusicScene::Classic;

    const auto voyage_motion = clamp01(finite_or(context.voyage_motion, 0.0F));
    const auto storm_presence = clamp01(finite_or(environment.storm_intensity, 0.0F));
    const auto danger_presence = clamp01(finite_or(context.danger, 0.0F));
    target_danger_presence_ = danger_presence;

    if (snapshot.scene == ProceduralMusicScene::SeaAdventure) {
        const auto hazard_presence = std::max(storm_presence, danger_presence);
        const auto calm_tempo = mix(96.0F, 84.0F, night_presence);
        snapshot.maritime_presence = 1.0F;
        snapshot.voyage_motion = voyage_motion;
        snapshot.storm_presence = storm_presence;
        snapshot.tempo_bpm = std::clamp(calm_tempo + hazard_presence * (106.0F - calm_tempo), 84.0F, 106.0F);
        snapshot.beat_hz = snapshot.tempo_bpm / 60.0F;
        snapshot.percussion_presence = clamp01(0.04F + voyage_motion * 0.66F + hazard_presence * 0.30F);
        snapshot.tension = clamp01(0.08F + night_presence * 0.16F + hazard_presence * 0.62F);
        snapshot.brightness = mix(0.62F, 0.34F, night_presence) - storm_presence * 0.08F;
        snapshot.sustain_seconds = mix(1.80F, 2.60F, night_presence);
        snapshot.reverb_feedback = mix(0.38F, 0.52F, night_presence) + storm_presence * 0.03F;
        snapshot.master_gain = has_active_session ? 0.125F : 0.085F;
        if (front_end_visible) {
            snapshot.master_gain *= has_active_session ? 0.82F : 0.68F;
            snapshot.percussion_presence *= has_active_session ? 0.78F : 0.55F;
        }
    }

    target_snapshot_ = snapshot;
    const auto next_scene = snapshot.scene;
    const auto scene_changed = next_scene != requested_scene_;
    const auto seed_changed = next_scene == ProceduralMusicScene::SeaAdventure && context.seed != maritime_seed_;
    if (scene_changed) {
        requested_scene_ = next_scene;
        if (next_scene == ProceduralMusicScene::SeaAdventure) {
            if (maritime_mix_ <= 0.0001F) {
                reset_maritime_transport(context.seed);
            } else if (seed_changed) {
                reseed_maritime_variation(context.seed);
            }
        } else if (maritime_mix_ >= 0.9999F) {
            reset_classic_transport();
        }
    } else if (seed_changed) {
        // Je change la suite de variations sans couper les voix deja audibles.
        reseed_maritime_variation(context.seed);
    }

    if (!mood_initialized_) {
        smoothed_snapshot_ = target_snapshot_;
        classic_smoothed_snapshot_ = classic_target_snapshot_;
        maritime_mix_ = requested_scene_ == ProceduralMusicScene::SeaAdventure ? 1.0F : 0.0F;
        if (requested_scene_ == ProceduralMusicScene::Classic) {
            // Je cale le premier accompagnement sur le tempo reel du jour ou de la nuit.
            reset_classic_transport();
        }
        mood_initialized_ = true;
    }
}

auto ProceduralMusicComposer::mood_snapshot() const noexcept -> ProceduralMusicSnapshot {
    auto snapshot = target_snapshot_;
    snapshot.maritime_presence = maritime_mix_;
    return snapshot;
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
    const auto crossfade_step = 1.0F / (2.0F * static_cast<float>(sample_rate_));

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
        smoothed_snapshot_.maritime_presence +=
            (target_snapshot_.maritime_presence - smoothed_snapshot_.maritime_presence) * mood_response;
        smoothed_snapshot_.voyage_motion +=
            (target_snapshot_.voyage_motion - smoothed_snapshot_.voyage_motion) * mood_response;
        smoothed_snapshot_.tempo_bpm +=
            (target_snapshot_.tempo_bpm - smoothed_snapshot_.tempo_bpm) * mood_response;
        smoothed_snapshot_.storm_presence +=
            (target_snapshot_.storm_presence - smoothed_snapshot_.storm_presence) * mood_response;
        smoothed_snapshot_.percussion_presence +=
            (target_snapshot_.percussion_presence - smoothed_snapshot_.percussion_presence) * mood_response;
        smoothed_snapshot_.scene = target_snapshot_.scene;

        classic_smoothed_snapshot_.day_presence +=
            (classic_target_snapshot_.day_presence - classic_smoothed_snapshot_.day_presence) * mood_response;
        classic_smoothed_snapshot_.night_presence +=
            (classic_target_snapshot_.night_presence - classic_smoothed_snapshot_.night_presence) * mood_response;
        classic_smoothed_snapshot_.tension +=
            (classic_target_snapshot_.tension - classic_smoothed_snapshot_.tension) * mood_response;
        classic_smoothed_snapshot_.master_gain +=
            (classic_target_snapshot_.master_gain - classic_smoothed_snapshot_.master_gain) * mood_response;
        classic_smoothed_snapshot_.beat_hz +=
            (classic_target_snapshot_.beat_hz - classic_smoothed_snapshot_.beat_hz) * mood_response;
        classic_smoothed_snapshot_.brightness +=
            (classic_target_snapshot_.brightness - classic_smoothed_snapshot_.brightness) * mood_response;
        classic_smoothed_snapshot_.sustain_seconds +=
            (classic_target_snapshot_.sustain_seconds - classic_smoothed_snapshot_.sustain_seconds) * mood_response;
        classic_smoothed_snapshot_.reverb_feedback +=
            (classic_target_snapshot_.reverb_feedback - classic_smoothed_snapshot_.reverb_feedback) * mood_response;
        classic_smoothed_snapshot_.scene = ProceduralMusicScene::Classic;

        const auto target_maritime_mix =
            requested_scene_ == ProceduralMusicScene::SeaAdventure ? 1.0F : 0.0F;
        if (maritime_mix_ < target_maritime_mix) {
            maritime_mix_ = std::min(target_maritime_mix, maritime_mix_ + crossfade_step);
        } else if (maritime_mix_ > target_maritime_mix) {
            maritime_mix_ = std::max(target_maritime_mix, maritime_mix_ - crossfade_step);
        }

        const auto classic_gain = std::cos(maritime_mix_ * kHalfPi);
        const auto maritime_gain = std::sin(maritime_mix_ * kHalfPi);
        if (classic_gain > 0.0001F || requested_scene_ == ProceduralMusicScene::Classic) {
            schedule_classic_notes();
        }
        if (maritime_gain > 0.0001F || requested_scene_ == ProceduralMusicScene::SeaAdventure) {
            schedule_maritime_steps();
        }

        auto classic_dry_left = 0.0F;
        auto classic_dry_right = 0.0F;
        if (classic_gain > 0.0001F) {
            for (auto& voice : voices_) {
                const auto sample = render_voice(voice);
                const auto pan = std::clamp(voice.pan, -0.95F, 0.95F);
                classic_dry_left += sample * (0.5F * (1.0F - pan));
                classic_dry_right += sample * (0.5F * (1.0F + pan));
            }
        }

        auto maritime_dry_left = 0.0F;
        auto maritime_dry_right = 0.0F;
        if (maritime_gain > 0.0001F) {
            for (auto& voice : maritime_voices_) {
                const auto sample = render_maritime_voice(voice);
                const auto pan = std::clamp(voice.pan, -0.95F, 0.95F);
                maritime_dry_left += sample * (0.5F * (1.0F - pan));
                maritime_dry_right += sample * (0.5F * (1.0F + pan));
            }
            for (auto& voice : percussion_voices_) {
                const auto sample = render_percussion_voice(voice);
                const auto pan = std::clamp(voice.pan, -0.95F, 0.95F);
                maritime_dry_left += sample * (0.5F * (1.0F - pan));
                maritime_dry_right += sample * (0.5F * (1.0F + pan));
            }
        }

        classic_dry_left *= classic_smoothed_snapshot_.master_gain;
        classic_dry_right *= classic_smoothed_snapshot_.master_gain;
        maritime_dry_left *= smoothed_snapshot_.master_gain;
        maritime_dry_right *= smoothed_snapshot_.master_gain;

        // Je conserve ce delai et ses coefficients historiques pour que le
        // piano classique reste strictement identique hors aventure maritime.
        const auto classic_wet_left = delay_left_[delay_index_];
        const auto classic_wet_right = delay_right_[delay_index_];
        delay_left_[delay_index_] =
            classic_dry_left * 0.26F +
            classic_wet_right * classic_smoothed_snapshot_.reverb_feedback;
        delay_right_[delay_index_] =
            classic_dry_right * 0.26F +
            classic_wet_left * classic_smoothed_snapshot_.reverb_feedback;
        delay_index_ = (delay_index_ + 1U) % delay_left_.size();

        const auto maritime_wet_left = maritime_delay_left_[maritime_delay_index_];
        const auto maritime_wet_right = maritime_delay_right_[maritime_delay_index_];
        const auto diffuse_left = diffusion_left_[diffusion_index_];
        const auto diffuse_right = diffusion_right_[diffusion_index_];

        delay_damping_left_ = delay_damping_left_ * 0.46F + maritime_wet_left * 0.54F;
        delay_damping_right_ = delay_damping_right_ * 0.46F + maritime_wet_right * 0.54F;
        diffusion_damping_left_ = diffusion_damping_left_ * 0.58F + diffuse_left * 0.42F;
        diffusion_damping_right_ = diffusion_damping_right_ * 0.58F + diffuse_right * 0.42F;

        maritime_delay_left_[maritime_delay_index_] =
            maritime_dry_left * 0.24F + delay_damping_right_ * smoothed_snapshot_.reverb_feedback;
        maritime_delay_right_[maritime_delay_index_] =
            maritime_dry_right * 0.24F + delay_damping_left_ * smoothed_snapshot_.reverb_feedback;
        diffusion_left_[diffusion_index_] =
            maritime_dry_left * 0.15F +
            diffusion_damping_right_ * smoothed_snapshot_.reverb_feedback * 0.82F;
        diffusion_right_[diffusion_index_] =
            maritime_dry_right * 0.15F +
            diffusion_damping_left_ * smoothed_snapshot_.reverb_feedback * 0.82F;
        maritime_delay_index_ = (maritime_delay_index_ + 1U) % maritime_delay_left_.size();
        diffusion_index_ = (diffusion_index_ + 1U) % diffusion_left_.size();

        const auto classic_output_left = classic_dry_left + classic_wet_left * 0.52F;
        const auto classic_output_right = classic_dry_right + classic_wet_right * 0.52F;
        const auto maritime_output_left =
            maritime_dry_left + maritime_wet_left * 0.46F + diffuse_left * 0.31F;
        const auto maritime_output_right =
            maritime_dry_right + maritime_wet_right * 0.46F + diffuse_right * 0.31F;
        const auto mixed_left = finite_or(
            classic_output_left * classic_gain + maritime_output_left * maritime_gain,
            0.0F);
        const auto mixed_right = finite_or(
            classic_output_right * classic_gain + maritime_output_right * maritime_gain,
            0.0F);

        stereo_samples[frame_index * 2U] = std::clamp(soft_clip(mixed_left), -1.0F, 1.0F);
        stereo_samples[frame_index * 2U + 1U] = std::clamp(soft_clip(mixed_right), -1.0F, 1.0F);

        musical_time_seconds_ += 1.0 / static_cast<double>(sample_rate_);
    }
}

void ProceduralMusicComposer::resize_delay_lines() {
    const auto delay_frames = static_cast<std::size_t>(std::max(sample_rate_ / 4, 2048));
    const auto diffusion_frames = static_cast<std::size_t>(std::max(sample_rate_ * 17 / 100, 1536));
    delay_left_.assign(delay_frames, 0.0F);
    delay_right_.assign(delay_frames, 0.0F);
    maritime_delay_left_.assign(delay_frames, 0.0F);
    maritime_delay_right_.assign(delay_frames, 0.0F);
    diffusion_left_.assign(diffusion_frames, 0.0F);
    diffusion_right_.assign(diffusion_frames, 0.0F);
    delay_index_ = 0U;
    maritime_delay_index_ = 0U;
    diffusion_index_ = 0U;
    delay_damping_left_ = 0.0F;
    delay_damping_right_ = 0.0F;
    diffusion_damping_left_ = 0.0F;
    diffusion_damping_right_ = 0.0F;
}

void ProceduralMusicComposer::reset_classic_transport() noexcept {
    const auto base_interval =
        1.0 / static_cast<double>(std::max(classic_target_snapshot_.beat_hz, 0.15F));
    next_melody_time_seconds_ = musical_time_seconds_;
    next_harmony_time_seconds_ = musical_time_seconds_ + base_interval;
    next_bass_time_seconds_ = musical_time_seconds_ + base_interval * 2.0;
    melody_step_index_ = 0U;
    harmony_step_index_ = 0U;
    bass_step_index_ = 0U;
    voices_ = {};
}

void ProceduralMusicComposer::reset_maritime_transport(std::uint32_t seed) noexcept {
    reseed_maritime_variation(seed);

    next_maritime_step_time_seconds_ = musical_time_seconds_;
    maritime_step_index_ = 0U;
    maritime_motif_step_index_ = 0U;
    maritime_variant_ = static_cast<std::size_t>(next_maritime_random() % 4U);
    maritime_voices_ = {};
    percussion_voices_ = {};
    quantized_voyage_motion_ = clamp01(target_snapshot_.voyage_motion);
    quantized_storm_presence_ = clamp01(target_snapshot_.storm_presence);
    quantized_danger_presence_ = clamp01(target_danger_presence_);
    quantized_percussion_presence_ = clamp01(target_snapshot_.percussion_presence);
    quantized_tempo_bpm_ = std::clamp(target_snapshot_.tempo_bpm, 84.0F, 106.0F);
}

void ProceduralMusicComposer::reseed_maritime_variation(std::uint32_t seed) noexcept {
    maritime_seed_ = seed;
    maritime_noise_state_ = seed ^ 0x51A7C0DEU;
    if (maritime_noise_state_ == 0U) {
        maritime_noise_state_ = 0x9E3779B9U;
    }
}

void ProceduralMusicComposer::schedule_maritime_steps() noexcept {
    while (musical_time_seconds_ >= next_maritime_step_time_seconds_) {
        schedule_maritime_step(maritime_step_index_);
        maritime_step_index_ = (maritime_step_index_ + 1U) % (32U * 6U);

        // Je mesure le tempo maritime a la noire pointee : une croche occupe
        // donc exactement un tiers du battement dans la mesure en 6/8.
        const auto safe_tempo = std::clamp(quantized_tempo_bpm_, 60.0F, 140.0F);
        const auto step_seconds = 60.0 / static_cast<double>(safe_tempo) / 3.0;
        next_maritime_step_time_seconds_ += step_seconds;
    }
}

void ProceduralMusicComposer::schedule_maritime_step(std::size_t step_index) noexcept {
    const auto bar = (step_index / 6U) % 32U;
    const auto step_in_bar = step_index % 6U;
    const auto phrase = bar / 8U;
    const auto& chord = kMaritimeChords[bar % kMaritimeChords.size()];
    const auto chord_note = [&](std::size_t note_index) noexcept {
        auto note = chord.notes[note_index];
        if (bar % kMaritimeChords.size() == 6U && step_in_bar >= 3U && note_index == 1U) {
            // Je resous le sol du Dsus4 vers le fa diese dans la seconde moitie de la mesure.
            --note;
        }
        return note;
    };

    if (step_in_bar == 0U) {
        if (bar == 0U) {
            maritime_motif_step_index_ = 0U;
        }
        quantized_voyage_motion_ = clamp01(target_snapshot_.voyage_motion);
        quantized_storm_presence_ = clamp01(target_snapshot_.storm_presence);
        quantized_danger_presence_ = clamp01(target_danger_presence_);
        quantized_percussion_presence_ = clamp01(target_snapshot_.percussion_presence);
        quantized_tempo_bpm_ = std::clamp(target_snapshot_.tempo_bpm, 84.0F, 106.0F);

        if (bar % 8U == 0U) {
            auto next_variant = static_cast<std::size_t>(next_maritime_random() % 4U);
            if (next_variant == maritime_variant_) {
                next_variant = (next_variant + 1U) % 4U;
            }
            maritime_variant_ = next_variant;
        }
    }

    const auto dotted_beat_seconds = 60.0F / std::max(quantized_tempo_bpm_, 1.0F);
    const auto eighth_seconds = dotted_beat_seconds / 3.0F;
    const auto hazard = std::max(quantized_storm_presence_, quantized_danger_presence_);
    const auto motion = quantized_voyage_motion_;
    const auto brightness = std::clamp(smoothed_snapshot_.brightness, 0.18F, 0.70F);

    if (step_in_bar == 0U) {
        const auto bass_octave = hazard > 0.60F ? -12 : 0;
        trigger_maritime_note(
            MaritimeInstrument::LowStrings,
            chord.bass_note + bass_octave,
            0.075F + motion * 0.035F + hazard * 0.025F,
            dotted_beat_seconds * 1.85F,
            -0.10F,
            brightness * 0.72F);
    }

    const auto arpeggio_index = kMaritimeArpeggio[step_in_bar];
    const auto phrase_lift = phrase == 2U && step_in_bar >= 3U ? 12 : 0;
    trigger_maritime_note(
        MaritimeInstrument::Lute,
        chord_note(arpeggio_index) + phrase_lift,
        0.085F + (1.0F - motion) * 0.030F,
        eighth_seconds * 0.92F,
        -0.34F + static_cast<float>(step_in_bar) * 0.11F,
        brightness);

    if (step_in_bar == 0U || step_in_bar == 3U) {
        const auto chord_gain = 0.030F + (1.0F - motion) * 0.018F;
        for (std::size_t note_index = 0U; note_index < 3U; ++note_index) {
            trigger_maritime_note(
                MaritimeInstrument::Concertina,
                chord_note(note_index),
                chord_gain,
                dotted_beat_seconds * 0.82F,
                -0.16F + static_cast<float>(note_index) * 0.16F,
                brightness * 0.88F);
        }
    }

    const auto is_lead_pulse = step_in_bar == 0U || step_in_bar == 3U;
    const auto lead_rhythm_index =
        (bar * 2U + (step_in_bar == 3U ? 1U : 0U)) % kMaritimeLeadRhythm.size();
    if (motion > 0.30F && is_lead_pulse && kMaritimeLeadRhythm[lead_rhythm_index]) {
        const auto motif_index = maritime_motif_step_index_ % kMaritimeMotifIntervals.size();
        ++maritime_motif_step_index_;
        auto lead_note = 69 + kMaritimeMotifIntervals[motif_index];
        if ((maritime_variant_ == 1U && motif_index >= 4U) ||
            (maritime_variant_ == 3U && phrase == 3U)) {
            lead_note += 12;
        }
        const auto is_response = bar % 8U >= 4U;
        if (is_response && maritime_variant_ == 2U) {
            lead_note -= 12;
        }
        const auto lead_instrument =
            !is_response && (maritime_variant_ + phrase) % 3U == 0U
                ? MaritimeInstrument::Whistle
                : MaritimeInstrument::Fiddle;
        trigger_maritime_note(
            lead_instrument,
            lead_note,
            0.060F + motion * 0.045F + hazard * 0.015F,
            dotted_beat_seconds * (step_in_bar == 0U ? 0.92F : 0.74F),
            is_response
                ? -0.16F
                : (lead_instrument == MaritimeInstrument::Whistle ? 0.24F : 0.12F),
            brightness);
    }

    if (step_in_bar == 0U && bar % 8U == 0U && motion > 0.62F) {
        trigger_maritime_note(
            MaritimeInstrument::Horn,
            chord.notes[0U] - 12,
            0.055F + hazard * 0.030F,
            dotted_beat_seconds * 2.40F,
            0.18F,
            brightness * 0.68F);
        trigger_maritime_note(
            MaritimeInstrument::Horn,
            chord.notes[2U] - 12,
            0.045F + hazard * 0.024F,
            dotted_beat_seconds * 2.20F,
            0.30F,
            brightness * 0.66F);
    }

    const auto percussion = quantized_percussion_presence_;
    if (percussion > 0.08F) {
        if (step_in_bar == 0U || step_in_bar == 3U) {
            trigger_percussion(
                PercussionKind::FrameDrum,
                percussion * (step_in_bar == 0U ? 0.17F : 0.12F),
                step_in_bar == 0U ? -0.12F : 0.12F);
        }
        if (step_in_bar == 0U && (motion > 0.48F || hazard > 0.45F)) {
            trigger_percussion(PercussionKind::LowTom, percussion * 0.14F, 0.0F);
        }
        if (motion > 0.42F || hazard > 0.30F) {
            const auto alternating_pan = step_in_bar % 2U == 0U ? -0.28F : 0.28F;
            trigger_percussion(PercussionKind::Shaker, percussion * 0.035F, alternating_pan);
        }
    }
}

void ProceduralMusicComposer::schedule_classic_notes() noexcept {
    const auto safe_beat = std::max(classic_smoothed_snapshot_.beat_hz, 0.15F);
    const auto melody_interval = 1.0 / static_cast<double>(safe_beat);
    const auto harmony_interval = melody_interval * 2.0;
    const auto bass_interval = melody_interval * 4.0;
    const auto night_palette =
        classic_smoothed_snapshot_.night_presence > classic_smoothed_snapshot_.day_presence;

    // Je declenche les notes sur une grille lente pour garder un vrai piano d'ambiance.
    while (musical_time_seconds_ >= next_melody_time_seconds_) {
        const auto& roots = night_palette ? kNightRoots : kDayRoots;
        const auto& melody = night_palette ? kNightMelody : kDayMelody;
        const auto root_index = (melody_step_index_ / 6U) % roots.size();
        const auto root_midi = (night_palette ? 45 : 52) + roots[root_index];
        const auto note = root_midi + melody[melody_step_index_ % melody.size()];
        const auto velocity =
            (0.20F + classic_smoothed_snapshot_.brightness * 0.06F) *
            (0.92F + next_random_unit() * 0.12F);
        trigger_piano_note(note,
                           velocity,
                           next_random_bipolar() * 0.18F,
                           classic_smoothed_snapshot_.brightness,
                           classic_smoothed_snapshot_.sustain_seconds);
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
                           classic_smoothed_snapshot_.brightness * 0.82F,
                           classic_smoothed_snapshot_.sustain_seconds * 1.10F);
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
                           classic_smoothed_snapshot_.brightness * 0.60F,
                           classic_smoothed_snapshot_.sustain_seconds * 1.35F);
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

void ProceduralMusicComposer::trigger_maritime_note(MaritimeInstrument instrument,
                                                    int midi_note,
                                                    float amplitude,
                                                    float duration_seconds,
                                                    float pan,
                                                    float brightness) noexcept {
    auto* selected_voice = &maritime_voices_.front();
    for (auto& voice : maritime_voices_) {
        if (!voice.active) {
            selected_voice = &voice;
            break;
        }
        if (voice.age_seconds > selected_voice->age_seconds) {
            selected_voice = &voice;
        }
    }

    selected_voice->instrument = instrument;
    selected_voice->frequency_hz = midi_to_hz(std::clamp(midi_note, 24, 96));
    selected_voice->phase = 0.0F;
    selected_voice->modulation_phase = static_cast<float>(next_maritime_random() & 0xFFFFU) / 65536.0F;
    selected_voice->age_seconds = 0.0F;
    selected_voice->duration_seconds = std::clamp(finite_or(duration_seconds, 1.0F), 0.05F, 10.0F);
    selected_voice->amplitude = std::clamp(finite_or(amplitude, 0.0F), 0.0F, 0.36F);
    selected_voice->brightness = std::clamp(finite_or(brightness, 0.4F), 0.12F, 0.75F);
    selected_voice->pan = std::clamp(finite_or(pan, 0.0F), -0.72F, 0.72F);
    selected_voice->active = selected_voice->amplitude > 0.0F;
}

auto ProceduralMusicComposer::render_maritime_voice(MaritimeVoice& voice) noexcept -> float {
    if (!voice.active || voice.frequency_hz <= 0.0F || voice.duration_seconds <= 0.0F) {
        return 0.0F;
    }

    const auto dt = 1.0F / static_cast<float>(sample_rate_);
    auto attack_seconds = 0.035F;
    auto release_seconds = 0.18F;
    switch (voice.instrument) {
    case MaritimeInstrument::LowStrings:
        attack_seconds = 0.12F;
        release_seconds = 0.42F;
        break;
    case MaritimeInstrument::Lute:
        attack_seconds = 0.003F;
        release_seconds = 0.12F;
        break;
    case MaritimeInstrument::Concertina:
        attack_seconds = 0.07F;
        release_seconds = 0.22F;
        break;
    case MaritimeInstrument::Fiddle:
        attack_seconds = 0.045F;
        release_seconds = 0.18F;
        break;
    case MaritimeInstrument::Whistle:
        attack_seconds = 0.025F;
        release_seconds = 0.16F;
        break;
    case MaritimeInstrument::Horn:
        attack_seconds = 0.14F;
        release_seconds = 0.46F;
        break;
    }

    const auto attack = clamp01(voice.age_seconds / attack_seconds);
    const auto release_start = std::max(voice.duration_seconds - release_seconds, attack_seconds);
    const auto release = voice.age_seconds <= release_start
                             ? 1.0F
                             : clamp01((voice.duration_seconds - voice.age_seconds) /
                                       std::max(voice.duration_seconds - release_start, 0.001F));
    auto body = 1.0F;
    auto tone = 0.0F;
    const auto fundamental = wavetable_sine(voice.phase);
    const auto second = wavetable_sine(voice.phase * 2.0F + 0.03F);
    const auto third = wavetable_sine(voice.phase * 3.0F + 0.07F);
    const auto fifth = wavetable_sine(voice.phase * 5.0F + 0.11F);
    const auto normalized_brightness = clamp01((voice.brightness - 0.12F) / 0.63F);
    const auto harmonic_presence = mix(0.38F, 1.0F, normalized_brightness);
    const auto upper_harmonic_presence = harmonic_presence * harmonic_presence;

    switch (voice.instrument) {
    case MaritimeInstrument::LowStrings:
        tone = fundamental * 0.78F +
               second * 0.17F * harmonic_presence +
               third * 0.08F * upper_harmonic_presence;
        break;
    case MaritimeInstrument::Lute:
        body = static_cast<float>(std::exp(-static_cast<double>(voice.age_seconds) * 4.8));
        tone = fundamental * 0.68F +
               second * 0.24F * harmonic_presence +
               third * 0.13F * harmonic_presence +
               fifth * 0.06F * upper_harmonic_presence;
        break;
    case MaritimeInstrument::Concertina:
        tone = fundamental * 0.72F +
               third * (0.16F + voice.brightness * 0.08F) * harmonic_presence +
               fifth * 0.07F * upper_harmonic_presence;
        break;
    case MaritimeInstrument::Fiddle:
        tone = fundamental * 0.64F +
               second * 0.20F * harmonic_presence +
               third * 0.12F * harmonic_presence +
               fifth * 0.05F * upper_harmonic_presence;
        break;
    case MaritimeInstrument::Whistle:
        tone = fundamental * 0.90F +
               second * 0.08F * harmonic_presence +
               third * 0.035F * upper_harmonic_presence;
        break;
    case MaritimeInstrument::Horn:
        tone = fundamental * 0.76F +
               second * 0.22F * harmonic_presence +
               third * 0.10F * upper_harmonic_presence;
        break;
    }

    const auto sample = voice.amplitude * attack * release * body * tone;
    const auto vibrato_depth =
        voice.instrument == MaritimeInstrument::Fiddle
            ? 0.0045F
            : (voice.instrument == MaritimeInstrument::Whistle ? 0.0022F : 0.0F);
    const auto vibrato = wavetable_sine(voice.modulation_phase) * vibrato_depth;
    advance_phase(
        voice.phase,
        voice.frequency_hz * (1.0F + vibrato),
        static_cast<float>(sample_rate_));
    advance_phase(
        voice.modulation_phase,
        voice.instrument == MaritimeInstrument::Fiddle ? 5.2F : 4.4F,
        static_cast<float>(sample_rate_));
    voice.age_seconds += dt;
    if (voice.age_seconds >= voice.duration_seconds) {
        voice.active = false;
    }
    return sample;
}

void ProceduralMusicComposer::trigger_percussion(PercussionKind kind,
                                                 float amplitude,
                                                 float pan) noexcept {
    auto* selected_voice = &percussion_voices_.front();
    for (auto& voice : percussion_voices_) {
        if (!voice.active) {
            selected_voice = &voice;
            break;
        }
        if (voice.age_seconds > selected_voice->age_seconds) {
            selected_voice = &voice;
        }
    }

    selected_voice->kind = kind;
    selected_voice->phase = 0.0F;
    selected_voice->age_seconds = 0.0F;
    selected_voice->duration_seconds =
        kind == PercussionKind::LowTom
            ? 0.42F
            : (kind == PercussionKind::FrameDrum ? 0.28F : 0.09F);
    selected_voice->amplitude = std::clamp(finite_or(amplitude, 0.0F), 0.0F, 0.40F);
    selected_voice->pan = std::clamp(finite_or(pan, 0.0F), -0.72F, 0.72F);
    selected_voice->filter_state = 0.0F;
    selected_voice->noise_state = next_maritime_random();
    if (selected_voice->noise_state == 0U) {
        selected_voice->noise_state = 1U;
    }
    selected_voice->active = selected_voice->amplitude > 0.0F;
}

auto ProceduralMusicComposer::render_percussion_voice(PercussionVoice& voice) noexcept -> float {
    if (!voice.active || voice.duration_seconds <= 0.0F) {
        return 0.0F;
    }

    const auto dt = 1.0F / static_cast<float>(sample_rate_);
    const auto ratio = clamp01(voice.age_seconds / voice.duration_seconds);
    const auto decay = (1.0F - ratio) * (1.0F - ratio);
    voice.noise_state = voice.noise_state * 1664525U + 1013904223U;
    const auto noise =
        static_cast<float>((voice.noise_state >> 8U) & 0x00FFFFFFU) / 8388608.0F - 1.0F;
    auto sample = 0.0F;

    switch (voice.kind) {
    case PercussionKind::FrameDrum: {
        const auto frequency = 112.0F - ratio * 54.0F;
        sample = (wavetable_sine(voice.phase) * 0.78F + noise * 0.18F) * decay;
        advance_phase(voice.phase, frequency, static_cast<float>(sample_rate_));
        break;
    }
    case PercussionKind::LowTom: {
        const auto frequency = 82.0F - ratio * 38.0F;
        sample = (wavetable_sine(voice.phase) * 0.88F + noise * 0.08F) * decay;
        advance_phase(voice.phase, frequency, static_cast<float>(sample_rate_));
        break;
    }
    case PercussionKind::Shaker: {
        voice.filter_state += (noise - voice.filter_state) * 0.18F;
        const auto high_pass = noise - voice.filter_state;
        sample = high_pass * (1.0F - ratio);
        break;
    }
    }

    voice.age_seconds += dt;
    if (voice.age_seconds >= voice.duration_seconds) {
        voice.active = false;
    }
    return sample * voice.amplitude;
}

auto ProceduralMusicComposer::wavetable_sine(float phase) const noexcept -> float {
    if (!std::isfinite(phase)) {
        return 0.0F;
    }
    const auto wrapped = wrap_phase(phase);
    const auto position = wrapped * static_cast<float>(sine_table_.size());
    const auto index = static_cast<std::size_t>(position) % sine_table_.size();
    const auto next_index = (index + 1U) % sine_table_.size();
    const auto fraction = position - static_cast<float>(index);
    return sine_table_[index] + (sine_table_[next_index] - sine_table_[index]) * fraction;
}

auto ProceduralMusicComposer::next_random_unit() noexcept -> float {
    noise_state_ = noise_state_ * 1664525U + 1013904223U;
    return static_cast<float>((noise_state_ >> 8U) & 0x00FFFFFFU) / 16777216.0F;
}

auto ProceduralMusicComposer::next_random_bipolar() noexcept -> float {
    return next_random_unit() * 2.0F - 1.0F;
}

auto ProceduralMusicComposer::next_maritime_random() noexcept -> std::uint32_t {
    auto value = maritime_noise_state_;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    maritime_noise_state_ = value == 0U ? 0x9E3779B9U : value;
    return maritime_noise_state_;
}

} // namespace valcraft
