#include "app/GameMusic.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace valcraft {

namespace {

auto rms_level(std::span<const float> samples) -> float {
    if (samples.empty()) {
        return 0.0F;
    }

    double energy = 0.0;
    for (const auto sample : samples) {
        energy += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return static_cast<float>(std::sqrt(energy / static_cast<double>(samples.size())));
}

auto mean_absolute_difference(std::span<const float> lhs, std::span<const float> rhs) -> float {
    const auto sample_count = std::min(lhs.size(), rhs.size());
    if (sample_count == 0U) {
        return 0.0F;
    }

    double total = 0.0;
    for (std::size_t index = 0; index < sample_count; ++index) {
        total += std::abs(static_cast<double>(lhs[index]) - static_cast<double>(rhs[index]));
    }
    return static_cast<float>(total / static_cast<double>(sample_count));
}

auto max_absolute_difference(std::span<const float> lhs, std::span<const float> rhs) -> float {
    const auto sample_count = std::min(lhs.size(), rhs.size());
    float maximum = 0.0F;
    for (std::size_t index = 0; index < sample_count; ++index) {
        maximum = std::max(maximum, std::abs(lhs[index] - rhs[index]));
    }
    return maximum;
}

auto maximum_channel_step(std::span<const float> samples) -> float {
    float maximum = 0.0F;
    for (std::size_t index = 2U; index < samples.size(); ++index) {
        maximum = std::max(maximum, std::abs(samples[index] - samples[index - 2U]));
    }
    return maximum;
}

auto stereo_width(std::span<const float> samples) -> float {
    const auto frame_count = samples.size() / 2U;
    if (frame_count == 0U) {
        return 0.0F;
    }

    double total = 0.0;
    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
        total += std::abs(static_cast<double>(samples[frame * 2U]) -
                          static_cast<double>(samples[frame * 2U + 1U]));
    }
    return static_cast<float>(total / static_cast<double>(frame_count));
}

auto samples_are_finite_and_bounded(std::span<const float> samples) -> bool {
    return std::all_of(samples.begin(), samples.end(), [](float sample) {
        return std::isfinite(sample) && std::abs(sample) <= 1.0F;
    });
}

auto snapshot_is_finite(const ProceduralMusicSnapshot& snapshot) -> bool {
    return std::isfinite(snapshot.day_presence) && std::isfinite(snapshot.night_presence) &&
           std::isfinite(snapshot.tension) && std::isfinite(snapshot.master_gain) &&
           std::isfinite(snapshot.beat_hz) && std::isfinite(snapshot.brightness) &&
           std::isfinite(snapshot.sustain_seconds) && std::isfinite(snapshot.reverb_feedback) &&
           std::isfinite(snapshot.maritime_presence) && std::isfinite(snapshot.voyage_motion) &&
           std::isfinite(snapshot.tempo_bpm) && std::isfinite(snapshot.storm_presence) &&
           std::isfinite(snapshot.percussion_presence);
}

auto classic_context(std::uint32_t seed = 0x12345678U) -> ProceduralMusicContext {
    ProceduralMusicContext context {};
    context.scene = ProceduralMusicScene::Classic;
    context.voyage_motion = 0.0F;
    context.danger = 0.0F;
    context.seed = seed;
    return context;
}

auto sea_context(float voyage_motion, float danger, std::uint32_t seed = 0x12345678U)
    -> ProceduralMusicContext {
    ProceduralMusicContext context {};
    context.scene = ProceduralMusicScene::SeaAdventure;
    context.voyage_motion = voyage_motion;
    context.danger = danger;
    context.seed = seed;
    return context;
}

constexpr float kLegacyPi = 3.14159265358979323846F;
constexpr float kLegacyTwoPi = 2.0F * kLegacyPi;

constexpr std::array<int, 4> kLegacyDayRoots {0, 5, 7, 9};
constexpr std::array<int, 4> kLegacyNightRoots {0, 3, 5, 8};
constexpr std::array<int, 12> kLegacyDayMelody {12, 16, 19, 16, 14, 12, 11, 14, 16, 19, 23, 19};
constexpr std::array<int, 12> kLegacyNightMelody {12, 15, 19, 15, 12, 10, 12, 15, 17, 15, 12, 7};
constexpr std::array<int, 8> kLegacyDayHarmony {7, 11, 14, 11, 16, 14, 11, 7};
constexpr std::array<int, 8> kLegacyNightHarmony {7, 10, 12, 10, 15, 12, 10, 7};
constexpr std::array<int, 4> kLegacyDayBass {0, 7, 5, 9};
constexpr std::array<int, 4> kLegacyNightBass {0, 3, 5, 8};

auto legacy_clamp01(float value) noexcept -> float {
    return std::clamp(value, 0.0F, 1.0F);
}

auto legacy_mix(float a, float b, float t) noexcept -> float {
    return a + (b - a) * legacy_clamp01(t);
}

auto legacy_wrap_phase(float phase) noexcept -> float {
    auto wrapped = phase;
    while (wrapped >= 1.0F) {
        wrapped -= 1.0F;
    }
    while (wrapped < 0.0F) {
        wrapped += 1.0F;
    }
    return wrapped;
}

void legacy_advance_phase(float& phase, float frequency_hz, float sample_rate) noexcept {
    phase = legacy_wrap_phase(phase + frequency_hz / sample_rate);
}

auto legacy_harmonic_sine(float phase, float multiplier, float offset_cycles) noexcept -> float {
    const auto angle = static_cast<double>(phase * multiplier + offset_cycles) *
                       static_cast<double>(kLegacyTwoPi);
    return static_cast<float>(std::sin(angle));
}

auto legacy_soft_clip(float value) noexcept -> float {
    return value / (1.0F + std::abs(value));
}

auto legacy_midi_to_hz(int midi_note) noexcept -> float {
    return 440.0F *
           static_cast<float>(std::pow(2.0, static_cast<double>(midi_note - 69) / 12.0));
}

auto legacy_smoothing_factor(float response_seconds, float sample_rate) noexcept -> float {
    const auto seconds = std::max(response_seconds, 0.001F);
    const auto rate = std::max(sample_rate, 8000.0F);
    return 1.0F - static_cast<float>(
                      std::exp(-1.0 / (static_cast<double>(seconds) * static_cast<double>(rate))));
}

// Je garde ici un oracle fidele au moteur classique de HEAD pour verrouiller son rendu historique.
class LegacyClassicMusicOracle {
public:
    explicit LegacyClassicMusicOracle(int sample_rate)
        : sample_rate_(std::max(sample_rate, 8000)) {
        reset();
    }

    void set_environment(const EnvironmentState& environment,
                         const CreatureCycleState& cycle,
                         bool has_active_session,
                         bool front_end_visible) noexcept {
        const auto daylight_presence = legacy_clamp01((environment.daylight_factor - 0.18F) / 0.82F);
        float cycle_night = 0.0F;
        float twilight_presence = 0.0F;

        switch (cycle.phase) {
        case CreaturePhase::Day:
            cycle_night = 0.0F;
            twilight_presence = 0.0F;
            break;
        case CreaturePhase::DuskMorph:
            cycle_night = legacy_mix(0.25F, 1.0F, cycle.morph_factor);
            twilight_presence = 1.0F - std::abs(cycle.morph_factor - 0.5F) * 2.0F;
            break;
        case CreaturePhase::Night:
            cycle_night = 1.0F;
            twilight_presence = 0.0F;
            break;
        case CreaturePhase::DawnRecover:
            cycle_night = legacy_mix(0.16F, 0.70F, cycle.morph_factor);
            twilight_presence = 1.0F - std::abs(cycle.morph_factor - 0.5F) * 2.0F;
            break;
        }

        const auto night_presence =
            legacy_clamp01(std::max(1.0F - daylight_presence, cycle_night));
        const auto day_presence = legacy_clamp01(
            daylight_presence + twilight_presence * 0.24F - night_presence * 0.12F);

        Snapshot snapshot {};
        snapshot.day_presence = day_presence;
        snapshot.night_presence = night_presence;
        snapshot.tension = legacy_clamp01(
            night_presence * 0.12F + cycle_night * 0.06F + twilight_presence * 0.04F);
        snapshot.master_gain = has_active_session ? 0.11F : 0.085F;
        if (front_end_visible) {
            snapshot.master_gain *= has_active_session ? 0.82F : 0.68F;
            snapshot.tension *= has_active_session ? 0.80F : 0.55F;
        }
        snapshot.beat_hz = legacy_mix(32.0F / 60.0F, 24.0F / 60.0F, night_presence);
        snapshot.brightness = legacy_mix(0.46F, 0.28F, night_presence);
        snapshot.sustain_seconds = legacy_mix(2.20F, 2.85F, night_presence);
        snapshot.reverb_feedback = legacy_mix(0.40F, 0.48F, night_presence);
        target_snapshot_ = snapshot;

        if (!mood_initialized_) {
            smoothed_snapshot_ = target_snapshot_;
            const auto base_interval =
                1.0 / static_cast<double>(std::max(target_snapshot_.beat_hz, 0.15F));
            next_melody_time_seconds_ = 0.0;
            next_harmony_time_seconds_ = base_interval;
            next_bass_time_seconds_ = base_interval * 2.0;
            mood_initialized_ = true;
        }
    }

    [[nodiscard]] auto sample_rate() const noexcept -> int {
        return sample_rate_;
    }

    void render_interleaved(std::span<float> stereo_samples) {
        if (stereo_samples.size() < 2U) {
            return;
        }

        const auto frame_count = stereo_samples.size() / 2U;
        const auto mood_response =
            legacy_smoothing_factor(0.45F, static_cast<float>(sample_rate_));

        for (std::size_t frame_index = 0U; frame_index < frame_count; ++frame_index) {
            smoothed_snapshot_.day_presence +=
                (target_snapshot_.day_presence - smoothed_snapshot_.day_presence) * mood_response;
            smoothed_snapshot_.night_presence +=
                (target_snapshot_.night_presence - smoothed_snapshot_.night_presence) * mood_response;
            smoothed_snapshot_.tension +=
                (target_snapshot_.tension - smoothed_snapshot_.tension) * mood_response;
            smoothed_snapshot_.master_gain +=
                (target_snapshot_.master_gain - smoothed_snapshot_.master_gain) * mood_response;
            smoothed_snapshot_.beat_hz +=
                (target_snapshot_.beat_hz - smoothed_snapshot_.beat_hz) * mood_response;
            smoothed_snapshot_.brightness +=
                (target_snapshot_.brightness - smoothed_snapshot_.brightness) * mood_response;
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
            delay_left_[delay_index_] =
                dry_left * 0.26F + wet_right * smoothed_snapshot_.reverb_feedback;
            delay_right_[delay_index_] =
                dry_right * 0.26F + wet_left * smoothed_snapshot_.reverb_feedback;
            delay_index_ = (delay_index_ + 1U) % delay_left_.size();

            stereo_samples[frame_index * 2U] = legacy_soft_clip(dry_left + wet_left * 0.52F);
            stereo_samples[frame_index * 2U + 1U] =
                legacy_soft_clip(dry_right + wet_right * 0.52F);

            musical_time_seconds_ += 1.0 / static_cast<double>(sample_rate_);
        }
    }

private:
    struct Snapshot {
        float day_presence = 1.0F;
        float night_presence = 0.0F;
        float tension = 0.0F;
        float master_gain = 0.11F;
        float beat_hz = 0.48F;
        float brightness = 0.40F;
        float sustain_seconds = 2.30F;
        float reverb_feedback = 0.42F;
    };

    struct Voice {
        float frequency_hz = 0.0F;
        float phase = 0.0F;
        float age_seconds = 0.0F;
        float amplitude = 0.0F;
        float brightness = 0.40F;
        float sustain_seconds = 2.20F;
        float pan = 0.0F;
        bool active = false;
    };

    void reset() {
        target_snapshot_ = {};
        const auto base_interval =
            1.0 / static_cast<double>(std::max(target_snapshot_.beat_hz, 0.15F));
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

        const auto delay_frames = static_cast<std::size_t>(std::max(sample_rate_ / 4, 2048));
        delay_left_.assign(delay_frames, 0.0F);
        delay_right_.assign(delay_frames, 0.0F);
        delay_index_ = 0U;
    }

    void schedule_due_notes() noexcept {
        const auto safe_beat = std::max(smoothed_snapshot_.beat_hz, 0.15F);
        const auto melody_interval = 1.0 / static_cast<double>(safe_beat);
        const auto harmony_interval = melody_interval * 2.0;
        const auto bass_interval = melody_interval * 4.0;
        const auto night_palette =
            smoothed_snapshot_.night_presence > smoothed_snapshot_.day_presence;

        while (musical_time_seconds_ >= next_melody_time_seconds_) {
            const auto& roots = night_palette ? kLegacyNightRoots : kLegacyDayRoots;
            const auto& melody = night_palette ? kLegacyNightMelody : kLegacyDayMelody;
            const auto root_index = (melody_step_index_ / 6U) % roots.size();
            const auto root_midi = (night_palette ? 45 : 52) + roots[root_index];
            const auto note = root_midi + melody[melody_step_index_ % melody.size()];
            const auto velocity =
                (0.20F + smoothed_snapshot_.brightness * 0.06F) *
                (0.92F + next_random_unit() * 0.12F);
            trigger_note(note,
                         velocity,
                         next_random_bipolar() * 0.18F,
                         smoothed_snapshot_.brightness,
                         smoothed_snapshot_.sustain_seconds);
            next_melody_time_seconds_ += melody_interval;
            ++melody_step_index_;
        }

        while (musical_time_seconds_ >= next_harmony_time_seconds_) {
            const auto& roots = night_palette ? kLegacyNightRoots : kLegacyDayRoots;
            const auto& harmony = night_palette ? kLegacyNightHarmony : kLegacyDayHarmony;
            const auto root_index = (melody_step_index_ / 6U) % roots.size();
            const auto root_midi = (night_palette ? 45 : 52) + roots[root_index];
            const auto note = root_midi + harmony[harmony_step_index_ % harmony.size()];
            trigger_note(note,
                         0.11F * (0.94F + next_random_unit() * 0.10F),
                         next_random_bipolar() * 0.12F,
                         smoothed_snapshot_.brightness * 0.82F,
                         smoothed_snapshot_.sustain_seconds * 1.10F);
            next_harmony_time_seconds_ += harmony_interval;
            ++harmony_step_index_;
        }

        while (musical_time_seconds_ >= next_bass_time_seconds_) {
            const auto& roots = night_palette ? kLegacyNightRoots : kLegacyDayRoots;
            const auto& bass = night_palette ? kLegacyNightBass : kLegacyDayBass;
            const auto root_index = (bass_step_index_ / 2U) % roots.size();
            const auto root_midi = (night_palette ? 33 : 40) + roots[root_index];
            const auto note = root_midi + bass[bass_step_index_ % bass.size()];
            trigger_note(note,
                         0.13F * (0.95F + next_random_unit() * 0.10F),
                         -0.10F + next_random_bipolar() * 0.04F,
                         smoothed_snapshot_.brightness * 0.60F,
                         smoothed_snapshot_.sustain_seconds * 1.35F);
            next_bass_time_seconds_ += bass_interval;
            ++bass_step_index_;
        }
    }

    void trigger_note(int midi_note,
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

        selected_voice->frequency_hz = legacy_midi_to_hz(midi_note);
        selected_voice->phase = 0.0F;
        selected_voice->age_seconds = 0.0F;
        selected_voice->amplitude = std::clamp(amplitude, 0.02F, 0.42F);
        selected_voice->brightness = std::clamp(brightness, 0.12F, 0.70F);
        selected_voice->sustain_seconds = std::clamp(sustain_seconds, 0.60F, 4.50F);
        selected_voice->pan = std::clamp(pan, -0.30F, 0.30F);
        selected_voice->active = true;
    }

    [[nodiscard]] auto render_voice(Voice& voice) noexcept -> float {
        if (!voice.active || voice.frequency_hz <= 0.0F) {
            return 0.0F;
        }

        const auto dt = 1.0F / static_cast<float>(sample_rate_);
        const auto attack = legacy_clamp01(voice.age_seconds / 0.010F);
        const auto body_decay = static_cast<float>(
            std::exp(-static_cast<double>(voice.age_seconds) /
                     static_cast<double>(std::max(voice.sustain_seconds, 0.20F))));
        const auto overtone_decay = static_cast<float>(
            std::exp(-static_cast<double>(voice.age_seconds) *
                     static_cast<double>(legacy_mix(2.8F, 4.8F, voice.brightness))));
        const auto hammer_decay = static_cast<float>(
            std::exp(-static_cast<double>(voice.age_seconds) *
                     static_cast<double>(legacy_mix(18.0F, 30.0F, voice.brightness))));

        const auto fundamental = legacy_harmonic_sine(voice.phase, 1.0F, 0.0F);
        const auto second = legacy_harmonic_sine(voice.phase, 2.0F, 0.07F);
        const auto third = legacy_harmonic_sine(voice.phase, 3.0F, 0.13F);
        const auto fourth = legacy_harmonic_sine(voice.phase, 4.0F, 0.19F);
        const auto hammer =
            legacy_harmonic_sine(voice.phase, 8.0F, 0.31F) * hammer_decay;

        const auto piano_tone =
            fundamental * 0.78F +
            second * (0.16F + voice.brightness * 0.12F) * overtone_decay +
            third * (0.08F + voice.brightness * 0.08F) * overtone_decay +
            fourth * (0.04F + voice.brightness * 0.05F) * overtone_decay +
            hammer * (0.05F + voice.brightness * 0.07F);
        const auto sample = voice.amplitude * attack * body_decay * piano_tone;

        legacy_advance_phase(voice.phase, voice.frequency_hz, static_cast<float>(sample_rate_));
        voice.age_seconds += dt;
        if (body_decay * voice.amplitude < 0.0006F &&
            voice.age_seconds > voice.sustain_seconds * 2.6F) {
            voice.active = false;
        }

        return sample;
    }

    [[nodiscard]] auto next_random_unit() noexcept -> float {
        noise_state_ = noise_state_ * 1664525U + 1013904223U;
        return static_cast<float>((noise_state_ >> 8U) & 0x00FFFFFFU) / 16777216.0F;
    }

    [[nodiscard]] auto next_random_bipolar() noexcept -> float {
        return next_random_unit() * 2.0F - 1.0F;
    }

    int sample_rate_ = 8000;
    double musical_time_seconds_ = 0.0;
    double next_melody_time_seconds_ = 0.0;
    double next_harmony_time_seconds_ = 0.0;
    double next_bass_time_seconds_ = 0.0;
    std::size_t melody_step_index_ = 0U;
    std::size_t harmony_step_index_ = 0U;
    std::size_t bass_step_index_ = 0U;
    Snapshot target_snapshot_ {};
    Snapshot smoothed_snapshot_ {};
    bool mood_initialized_ = false;
    std::array<Voice, 8> voices_ {};
    std::uint32_t noise_state_ = 0xC0FFEE11U;
    std::vector<float> delay_left_ {};
    std::vector<float> delay_right_ {};
    std::size_t delay_index_ = 0U;
};

auto render_seconds(LegacyClassicMusicOracle& oracle, double seconds) -> std::vector<float> {
    const auto frame_count = static_cast<std::size_t>(
        std::ceil(seconds * static_cast<double>(oracle.sample_rate())));
    std::vector<float> samples(frame_count * 2U, 0.0F);
    oracle.render_interleaved(samples);
    return samples;
}

void check_samplewise_close(std::span<const float> expected,
                            std::span<const float> actual,
                            float tolerance) {
    REQUIRE(expected.size() == actual.size());

    float maximum_difference = 0.0F;
    std::size_t first_mismatch = expected.size();
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        const auto difference = std::abs(expected[index] - actual[index]);
        maximum_difference = std::max(maximum_difference, difference);
        if (first_mismatch == expected.size() && difference > tolerance) {
            first_mismatch = index;
        }
    }

    CAPTURE(first_mismatch);
    CAPTURE(maximum_difference);
    CHECK(maximum_difference <= tolerance);
}

auto render_seconds(ProceduralMusicComposer& composer, double seconds) -> std::vector<float> {
    const auto frame_count = static_cast<std::size_t>(
        std::ceil(seconds * static_cast<double>(composer.sample_rate())));
    std::vector<float> samples(frame_count * 2U, 0.0F);
    composer.render_interleaved(samples);
    return samples;
}

auto has_no_silent_blocks(std::span<const float> samples,
                          std::size_t block_frames,
                          float minimum_rms) -> bool {
    const auto block_samples = block_frames * 2U;
    if (block_samples == 0U || samples.size() < block_samples) {
        return false;
    }

    for (std::size_t offset = 0U; offset + block_samples <= samples.size(); offset += block_samples) {
        const auto block = samples.subspan(offset, block_samples);
        if (rms_level(block) <= minimum_rms) {
            return false;
        }
    }
    return true;
}

}

TEST_CASE("application music context keeps the classic scene in menus") {
    const GameMusicContextInput retained_sea_session {
        .has_active_session = true,
        .front_end_visible = true,
        .maritime_gameplay_active = true,
        .voyage_motion = 1.0F,
        .danger = 1.0F,
        .world_seed = 4217,
    };

    const auto menu_context = make_game_music_context(retained_sea_session);
    CHECK(menu_context.scene == ProceduralMusicScene::Classic);
    CHECK(menu_context.voyage_motion == doctest::Approx(0.0F));
    CHECK(menu_context.danger == doctest::Approx(0.0F));

    auto gameplay_input = retained_sea_session;
    gameplay_input.front_end_visible = false;
    const auto gameplay_context = make_game_music_context(gameplay_input);
    CHECK(gameplay_context.scene == ProceduralMusicScene::SeaAdventure);
    CHECK(gameplay_context.seed == menu_context.seed);
}

TEST_CASE("application music context only enables an active maritime session") {
    GameMusicContextInput input {
        .has_active_session = false,
        .front_end_visible = false,
        .maritime_gameplay_active = true,
        .voyage_motion = 0.5F,
        .danger = 0.4F,
        .world_seed = -73,
    };

    const auto missing_session = make_game_music_context(input);
    CHECK(missing_session.scene == ProceduralMusicScene::Classic);

    input.has_active_session = true;
    input.maritime_gameplay_active = false;
    const auto classic_gameplay = make_game_music_context(input);
    CHECK(classic_gameplay.scene == ProceduralMusicScene::Classic);
    CHECK(classic_gameplay.voyage_motion == doctest::Approx(0.0F));
    CHECK(classic_gameplay.danger == doctest::Approx(0.0F));

    input.maritime_gameplay_active = true;
    const auto maritime_gameplay = make_game_music_context(input);
    CHECK(maritime_gameplay.scene == ProceduralMusicScene::SeaAdventure);
    CHECK(maritime_gameplay.voyage_motion == doctest::Approx(0.5F));
    CHECK(maritime_gameplay.danger == doctest::Approx(0.4F));
    CHECK(maritime_gameplay.seed != 0U);
}

TEST_CASE("application music context maps voyage phases and sanitizes its inputs") {
    GameMusicContextInput input {
        .has_active_session = true,
        .front_end_visible = false,
        .maritime_gameplay_active = true,
        .voyage_motion = 0.0F,
        .danger = 0.0F,
        .world_seed = 90210,
    };

    const auto moored = make_game_music_context(input);
    CHECK(moored.voyage_motion == doctest::Approx(0.0F));

    input.voyage_motion = 0.45F;
    const auto departing = make_game_music_context(input);
    CHECK(departing.voyage_motion == doctest::Approx(0.45F));

    input.voyage_motion = 1.0F;
    const auto underway = make_game_music_context(input);
    CHECK(underway.voyage_motion == doctest::Approx(1.0F));
    CHECK(underway.seed == moored.seed);

    input.voyage_motion = std::numeric_limits<float>::quiet_NaN();
    input.danger = std::numeric_limits<float>::infinity();
    const auto non_finite = make_game_music_context(input);
    CHECK(non_finite.voyage_motion == doctest::Approx(0.0F));
    CHECK(non_finite.danger == doctest::Approx(0.0F));

    input.voyage_motion = -2.0F;
    input.danger = 4.0F;
    const auto bounded = make_game_music_context(input);
    CHECK(bounded.voyage_motion == doctest::Approx(0.0F));
    CHECK(bounded.danger == doctest::Approx(1.0F));

    input.world_seed = 90211;
    const auto other_seed = make_game_music_context(input);
    CHECK(other_seed.seed != bounded.seed);
}

TEST_CASE("procedural music keeps the existing discreet piano outside sea adventure") {
    ProceduralMusicComposer composer {};

    const auto day_environment = EnvironmentClock::compute_state(12.0F);
    const auto day_cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    composer.set_environment(day_environment, day_cycle, true, false, classic_context());
    const auto day_snapshot = composer.mood_snapshot();

    CHECK(day_snapshot.scene == ProceduralMusicScene::Classic);
    CHECK(day_snapshot.maritime_presence == doctest::Approx(0.0F));
    CHECK(day_snapshot.voyage_motion == doctest::Approx(0.0F));
    CHECK(day_snapshot.percussion_presence == doctest::Approx(0.0F));
    CHECK(day_snapshot.day_presence > 0.80F);
    CHECK(day_snapshot.night_presence < 0.25F);
    CHECK(day_snapshot.tension < 0.15F);
    CHECK(day_snapshot.master_gain == doctest::Approx(0.11F));
    CHECK(day_snapshot.brightness > 0.40F);
    CHECK(day_snapshot.sustain_seconds > 2.0F);
    CHECK(snapshot_is_finite(day_snapshot));

    composer.set_environment(day_environment, day_cycle, true, true, classic_context());
    const auto active_menu_snapshot = composer.mood_snapshot();
    composer.set_environment(day_environment, day_cycle, false, false, classic_context());
    const auto inactive_game_snapshot = composer.mood_snapshot();
    composer.set_environment(day_environment, day_cycle, false, true, classic_context());
    const auto inactive_menu_snapshot = composer.mood_snapshot();

    CHECK(active_menu_snapshot.master_gain == doctest::Approx(0.11F * 0.82F));
    CHECK(inactive_game_snapshot.master_gain == doctest::Approx(0.085F));
    CHECK(inactive_menu_snapshot.master_gain == doctest::Approx(0.085F * 0.68F));

    const auto night_environment = EnvironmentClock::compute_state(23.0F);
    const auto night_cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    composer.set_environment(night_environment, night_cycle, true, false, classic_context());
    const auto night_snapshot = composer.mood_snapshot();

    CHECK(night_snapshot.scene == ProceduralMusicScene::Classic);
    CHECK(night_snapshot.maritime_presence == doctest::Approx(0.0F));
    CHECK(night_snapshot.night_presence > 0.80F);
    CHECK(night_snapshot.day_presence < 0.20F);
    CHECK(night_snapshot.tension < 0.22F);
    CHECK(night_snapshot.brightness < day_snapshot.brightness);
    CHECK(night_snapshot.sustain_seconds > day_snapshot.sustain_seconds);
    CHECK(night_snapshot.beat_hz < day_snapshot.beat_hz);
    CHECK(snapshot_is_finite(night_snapshot));
}

TEST_CASE("classic procedural piano stays soft bounded and distinct by day and night") {
    ProceduralMusicComposer day_composer {};
    ProceduralMusicComposer alternate_seed_composer {};
    ProceduralMusicComposer night_composer {};

    const auto day_environment = EnvironmentClock::compute_state(10.5F);
    const auto day_cycle = EnvironmentClock::classify_creature_cycle(10.5F);
    day_composer.set_environment(day_environment, day_cycle, true, false, classic_context());
    const auto day_samples = render_seconds(day_composer, 2.5);
    alternate_seed_composer.set_environment(
        day_environment, day_cycle, true, false, classic_context(0x87654321U));
    const auto alternate_seed_samples = render_seconds(alternate_seed_composer, 2.5);

    const auto night_environment = EnvironmentClock::compute_state(22.0F);
    const auto night_cycle = EnvironmentClock::classify_creature_cycle(22.0F);
    night_composer.set_environment(night_environment, night_cycle, true, false, classic_context());
    const auto night_samples = render_seconds(night_composer, 2.5);

    CHECK(rms_level(day_samples) > 0.001F);
    CHECK(rms_level(night_samples) > 0.001F);
    CHECK(rms_level(day_samples) < 0.08F);
    CHECK(rms_level(night_samples) < 0.08F);
    CHECK(mean_absolute_difference(day_samples, night_samples) > 0.002F);
    CHECK(max_absolute_difference(day_samples, alternate_seed_samples) < 1.0e-7F);
    CHECK(samples_are_finite_and_bounded(day_samples));
    CHECK(samples_are_finite_and_bounded(night_samples));
}

TEST_CASE("classic piano matches the historical renderer sample by sample") {
    constexpr auto kSampleRate = 8000;
    constexpr auto kTolerance = 1.0e-6F;

    SUBCASE("day gameplay covers harmony bass delay returns and ignores the maritime seed") {
        const auto environment = EnvironmentClock::compute_state(10.5F);
        const auto cycle = EnvironmentClock::classify_creature_cycle(10.5F);
        ProceduralMusicComposer composer {kSampleRate};
        ProceduralMusicComposer alternate_seed_composer {kSampleRate};
        LegacyClassicMusicOracle oracle {kSampleRate};
        composer.set_environment(
            environment, cycle, true, false, classic_context(0xA17E5EEDU));
        alternate_seed_composer.set_environment(
            environment, cycle, true, false, classic_context(0x51A7C0DEU));
        oracle.set_environment(environment, cycle, true, false);

        const auto actual = render_seconds(composer, 6.0);
        const auto alternate_seed = render_seconds(alternate_seed_composer, 6.0);
        const auto expected = render_seconds(oracle, 6.0);

        check_samplewise_close(expected, actual, kTolerance);
        check_samplewise_close(actual, alternate_seed, 0.0F);
    }

    SUBCASE("night menu covers harmony bass and delay returns") {
        const auto environment = EnvironmentClock::compute_state(23.0F);
        const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);
        ProceduralMusicComposer composer {kSampleRate};
        LegacyClassicMusicOracle oracle {kSampleRate};
        composer.set_environment(
            environment, cycle, false, true, classic_context(0x51A7C0DEU));
        oracle.set_environment(environment, cycle, false, true);

        const auto actual = render_seconds(composer, 6.5);
        const auto expected = render_seconds(oracle, 6.5);

        check_samplewise_close(expected, actual, kTolerance);
    }
}

TEST_CASE("sea adventure orchestration grows from mooring to departure and open sea") {
    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);

    ProceduralMusicComposer moored_composer {12000};
    ProceduralMusicComposer departing_composer {12000};
    ProceduralMusicComposer underway_composer {12000};
    moored_composer.set_environment(environment, cycle, true, false, sea_context(0.0F, 0.0F));
    departing_composer.set_environment(environment, cycle, true, false, sea_context(0.5F, 0.0F));
    underway_composer.set_environment(environment, cycle, true, false, sea_context(1.0F, 0.0F));

    const auto moored_snapshot = moored_composer.mood_snapshot();
    const auto departing_snapshot = departing_composer.mood_snapshot();
    const auto underway_snapshot = underway_composer.mood_snapshot();

    CHECK(moored_snapshot.scene == ProceduralMusicScene::SeaAdventure);
    CHECK(departing_snapshot.scene == ProceduralMusicScene::SeaAdventure);
    CHECK(underway_snapshot.scene == ProceduralMusicScene::SeaAdventure);
    CHECK(moored_snapshot.maritime_presence > 0.90F);
    CHECK(departing_snapshot.maritime_presence > 0.90F);
    CHECK(underway_snapshot.maritime_presence > 0.90F);
    CHECK(moored_snapshot.voyage_motion < 0.05F);
    CHECK(departing_snapshot.voyage_motion == doctest::Approx(0.5F).epsilon(0.02));
    CHECK(underway_snapshot.voyage_motion > 0.95F);
    CHECK(moored_snapshot.percussion_presence < departing_snapshot.percussion_presence);
    CHECK(departing_snapshot.percussion_presence <= underway_snapshot.percussion_presence);
    CHECK(moored_snapshot.tempo_bpm <= underway_snapshot.tempo_bpm);

    const auto moored_samples = render_seconds(moored_composer, 6.0);
    const auto departing_samples = render_seconds(departing_composer, 6.0);
    const auto underway_samples = render_seconds(underway_composer, 6.0);

    CHECK(rms_level(moored_samples) > 0.0005F);
    CHECK(rms_level(departing_samples) > 0.0005F);
    CHECK(rms_level(underway_samples) > 0.0005F);
    CHECK(rms_level(moored_samples) < 0.25F);
    CHECK(rms_level(departing_samples) < 0.25F);
    CHECK(rms_level(underway_samples) < 0.25F);
    CHECK(mean_absolute_difference(moored_samples, departing_samples) > 0.0001F);
    CHECK(mean_absolute_difference(departing_samples, underway_samples) > 0.0001F);
    CHECK(samples_are_finite_and_bounded(moored_samples));
    CHECK(samples_are_finite_and_bounded(departing_samples));
    CHECK(samples_are_finite_and_bounded(underway_samples));
}

TEST_CASE("sea adventure reacts to night storms and danger") {
    auto clear_day = EnvironmentClock::compute_state(12.0F);
    clear_day.storm_intensity = 0.0F;
    auto clear_night = EnvironmentClock::compute_state(23.0F);
    clear_night.storm_intensity = 0.0F;
    auto storm = clear_day;
    storm.storm_intensity = 1.0F;

    const auto day_cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    const auto night_cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    ProceduralMusicComposer day_composer {12000};
    ProceduralMusicComposer night_composer {12000};
    ProceduralMusicComposer storm_composer {12000};
    ProceduralMusicComposer danger_composer {12000};
    day_composer.set_environment(clear_day, day_cycle, true, false, sea_context(1.0F, 0.0F));
    night_composer.set_environment(clear_night, night_cycle, true, false, sea_context(1.0F, 0.0F));
    storm_composer.set_environment(storm, day_cycle, true, false, sea_context(1.0F, 0.0F));
    danger_composer.set_environment(clear_day, day_cycle, true, false, sea_context(1.0F, 1.0F));

    const auto day_snapshot = day_composer.mood_snapshot();
    const auto night_snapshot = night_composer.mood_snapshot();
    const auto storm_snapshot = storm_composer.mood_snapshot();
    const auto danger_snapshot = danger_composer.mood_snapshot();

    CHECK(day_snapshot.tempo_bpm >= 90.0F);
    CHECK(day_snapshot.tempo_bpm <= 98.0F);
    CHECK(night_snapshot.tempo_bpm >= 80.0F);
    CHECK(night_snapshot.tempo_bpm < day_snapshot.tempo_bpm);
    CHECK(storm_snapshot.tempo_bpm > day_snapshot.tempo_bpm);
    CHECK(danger_snapshot.tempo_bpm > day_snapshot.tempo_bpm);
    CHECK(storm_snapshot.tempo_bpm <= 106.5F);
    CHECK(danger_snapshot.tempo_bpm <= 106.5F);
    CHECK(day_snapshot.storm_presence < 0.05F);
    CHECK(storm_snapshot.storm_presence > 0.90F);
    CHECK(storm_snapshot.percussion_presence > day_snapshot.percussion_presence);
    CHECK(danger_snapshot.percussion_presence > day_snapshot.percussion_presence);
    CHECK(storm_snapshot.tension > day_snapshot.tension);
    CHECK(danger_snapshot.tension > day_snapshot.tension);

    const auto day_samples = render_seconds(day_composer, 6.0);
    const auto night_samples = render_seconds(night_composer, 6.0);
    const auto storm_samples = render_seconds(storm_composer, 6.0);
    const auto danger_samples = render_seconds(danger_composer, 6.0);

    CHECK(mean_absolute_difference(day_samples, night_samples) > 0.0001F);
    CHECK(mean_absolute_difference(day_samples, storm_samples) > 0.0001F);
    CHECK(mean_absolute_difference(day_samples, danger_samples) > 0.0001F);
    CHECK(samples_are_finite_and_bounded(day_samples));
    CHECK(samples_are_finite_and_bounded(night_samples));
    CHECK(samples_are_finite_and_bounded(storm_samples));
    CHECK(samples_are_finite_and_bounded(danger_samples));
}

TEST_CASE("sea adventure reset and seed produce deterministic controlled variations") {
    const auto environment = EnvironmentClock::compute_state(14.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(14.0F);
    constexpr auto kSeed = std::uint32_t {0xCAFEBABEU};

    ProceduralMusicComposer reset_composer {8000};
    reset_composer.set_environment(environment, cycle, true, false, sea_context(1.0F, 0.35F, kSeed));
    const auto first_render = render_seconds(reset_composer, 12.0);
    reset_composer.reset();
    reset_composer.set_environment(environment, cycle, true, false, sea_context(1.0F, 0.35F, kSeed));
    const auto reset_render = render_seconds(reset_composer, 12.0);

    ProceduralMusicComposer same_seed_composer {8000};
    same_seed_composer.set_environment(environment, cycle, true, false, sea_context(1.0F, 0.35F, kSeed));
    const auto same_seed_render = render_seconds(same_seed_composer, 12.0);

    ProceduralMusicComposer other_seed_composer {8000};
    other_seed_composer.set_environment(
        environment, cycle, true, false, sea_context(1.0F, 0.35F, kSeed + 1U));
    const auto other_seed_render = render_seconds(other_seed_composer, 12.0);

    CHECK(max_absolute_difference(first_render, reset_render) < 1.0e-7F);
    CHECK(max_absolute_difference(first_render, same_seed_render) < 1.0e-7F);
    CHECK(mean_absolute_difference(first_render, other_seed_render) > 1.0e-6F);
}

TEST_CASE("sea adventure rendering is independent from SDL callback chunk sizes") {
    ProceduralMusicComposer contiguous_composer {12000};
    ProceduralMusicComposer chunked_composer {12000};

    const auto environment = EnvironmentClock::compute_state(21.5F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(21.5F);
    const auto context = sea_context(1.0F, 0.55F, 0xA5A55A5AU);
    contiguous_composer.set_environment(environment, cycle, true, false, context);
    chunked_composer.set_environment(environment, cycle, true, false, context);

    std::vector<float> contiguous_samples(192000U, 0.0F);
    std::vector<float> chunked_samples(contiguous_samples.size(), 0.0F);
    contiguous_composer.render_interleaved(contiguous_samples);

    constexpr std::array<std::size_t, 7> kChunkFrames {17U, 127U, 257U, 384U, 511U, 733U, 1024U};
    std::size_t sample_offset = 0U;
    std::size_t chunk_index = 0U;
    while (sample_offset < chunked_samples.size()) {
        auto chunk_sample_count = kChunkFrames[chunk_index % kChunkFrames.size()] * 2U;
        chunk_sample_count = std::min(chunk_sample_count, chunked_samples.size() - sample_offset);
        chunk_sample_count -= chunk_sample_count % 2U;
        REQUIRE(chunk_sample_count > 0U);

        chunked_composer.render_interleaved(std::span<float> {
            chunked_samples.data() + static_cast<std::ptrdiff_t>(sample_offset),
            chunk_sample_count,
        });
        sample_offset += chunk_sample_count;
        ++chunk_index;
    }

    CHECK(mean_absolute_difference(contiguous_samples, chunked_samples) < 1.0e-7F);
    CHECK(max_absolute_difference(contiguous_samples, chunked_samples) < 1.0e-6F);
}

TEST_CASE("procedural music sanitizes invalid context and always emits safe samples") {
    auto environment = EnvironmentClock::compute_state(12.0F);
    environment.daylight_factor = std::numeric_limits<float>::quiet_NaN();
    environment.storm_intensity = std::numeric_limits<float>::infinity();
    auto cycle = EnvironmentClock::classify_creature_cycle(18.5F);
    cycle.morph_factor = -std::numeric_limits<float>::infinity();
    auto context = sea_context(
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0x5EEDU);

    ProceduralMusicComposer composer {48000};
    composer.set_environment(environment, cycle, true, false, context);
    const auto snapshot = composer.mood_snapshot();

    CHECK(snapshot_is_finite(snapshot));
    CHECK(snapshot.maritime_presence >= 0.0F);
    CHECK(snapshot.maritime_presence <= 1.0F);
    CHECK(snapshot.voyage_motion >= 0.0F);
    CHECK(snapshot.voyage_motion <= 1.0F);
    CHECK(snapshot.storm_presence >= 0.0F);
    CHECK(snapshot.storm_presence <= 1.0F);
    CHECK(snapshot.percussion_presence >= 0.0F);
    CHECK(snapshot.percussion_presence <= 1.0F);
    CHECK(snapshot.tempo_bpm > 0.0F);
    CHECK(snapshot.tempo_bpm <= 160.0F);

    const auto samples = render_seconds(composer, 2.5);
    CHECK(samples_are_finite_and_bounded(samples));
    CHECK(maximum_channel_step(samples) < 0.02F);
}

TEST_CASE("sea adventure renderer is stable at supported audio sample rates") {
    constexpr std::array<int, 3> kSampleRates {44100, 48000, 96000};
    const auto environment = EnvironmentClock::compute_state(10.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(10.0F);

    for (const auto sample_rate : kSampleRates) {
        CAPTURE(sample_rate);
        ProceduralMusicComposer composer {sample_rate};
        composer.set_environment(environment, cycle, true, false, sea_context(1.0F, 0.25F));
        const auto samples = render_seconds(composer, 2.5);

        CHECK(composer.sample_rate() == sample_rate);
        CHECK(samples_are_finite_and_bounded(samples));
        CHECK(rms_level(samples) > 0.0001F);
        CHECK(rms_level(samples) < 0.25F);
        CHECK(stereo_width(samples) > 1.0e-6F);
        CHECK(maximum_channel_step(samples) < 0.02F);
    }
}

TEST_CASE("a complete sea form stays audible spatial and continuous") {
    constexpr auto kSlowTempoBpm = 84.0;
    constexpr auto kFormDurationSeconds = 32.0 * 2.0 * 60.0 / kSlowTempoBpm;
    constexpr auto kSampleRate = 8000;
    const auto environment = EnvironmentClock::compute_state(23.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);

    ProceduralMusicComposer composer {kSampleRate};
    composer.set_environment(environment, cycle, true, false, sea_context(1.0F, 0.0F, 0x31415926U));
    const auto samples = render_seconds(composer, kFormDurationSeconds + 4.0);

    CHECK(samples_are_finite_and_bounded(samples));
    CHECK(rms_level(samples) > 0.0005F);
    CHECK(rms_level(samples) < 0.25F);
    CHECK(stereo_width(samples) > 1.0e-5F);
    CHECK(maximum_channel_step(samples) < 0.03F);
    CHECK(has_no_silent_blocks(samples, static_cast<std::size_t>(kSampleRate) * 4U, 1.0e-5F));
}

TEST_CASE("classic and maritime buses crossfade over two seconds in both directions") {
    const auto environment = EnvironmentClock::compute_state(16.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(16.0F);
    ProceduralMusicComposer composer {8000};
    composer.set_environment(environment, cycle, true, false, classic_context());
    const auto classic_samples = render_seconds(composer, 2.0);

    composer.set_environment(environment, cycle, true, false, sea_context(1.0F, 0.2F));
    CHECK(composer.mood_snapshot().scene == ProceduralMusicScene::SeaAdventure);
    CHECK(composer.mood_snapshot().maritime_presence == doctest::Approx(0.0F));

    const auto first_half_second = render_seconds(composer, 0.5);
    CHECK(composer.mood_snapshot().maritime_presence == doctest::Approx(0.25F).epsilon(0.01));
    const auto midpoint_samples = render_seconds(composer, 0.5);
    CHECK(composer.mood_snapshot().maritime_presence == doctest::Approx(0.50F).epsilon(0.01));
    const auto final_second = render_seconds(composer, 1.0);
    CHECK(composer.mood_snapshot().maritime_presence == doctest::Approx(1.0F).epsilon(0.002));

    REQUIRE(classic_samples.size() >= 2U);
    REQUIRE(first_half_second.size() >= 2U);
    CHECK(std::abs(classic_samples[classic_samples.size() - 2U] - first_half_second[0]) < 0.015F);
    CHECK(std::abs(classic_samples[classic_samples.size() - 1U] - first_half_second[1]) < 0.015F);
    CHECK(rms_level(midpoint_samples) > 0.0005F);
    CHECK(samples_are_finite_and_bounded(first_half_second));
    CHECK(samples_are_finite_and_bounded(midpoint_samples));
    CHECK(samples_are_finite_and_bounded(final_second));
    CHECK(maximum_channel_step(first_half_second) < 0.03F);
    CHECK(maximum_channel_step(midpoint_samples) < 0.03F);
    CHECK(maximum_channel_step(final_second) < 0.03F);

    composer.set_environment(environment, cycle, true, false, classic_context());
    CHECK(composer.mood_snapshot().scene == ProceduralMusicScene::Classic);
    CHECK(composer.mood_snapshot().maritime_presence == doctest::Approx(1.0F).epsilon(0.002));
    const auto return_midpoint = render_seconds(composer, 1.0);
    CHECK(composer.mood_snapshot().maritime_presence == doctest::Approx(0.50F).epsilon(0.01));
    const auto classic_return = render_seconds(composer, 1.0);
    CHECK(composer.mood_snapshot().maritime_presence == doctest::Approx(0.0F).epsilon(0.002));
    CHECK(rms_level(return_midpoint) > 0.0005F);
    CHECK(samples_are_finite_and_bounded(classic_return));
    CHECK(maximum_channel_step(return_midpoint) < 0.03F);
    CHECK(maximum_channel_step(classic_return) < 0.03F);
}

TEST_CASE("rapid scene reversals preserve every bus that is still audible") {
    const auto environment = EnvironmentClock::compute_state(17.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(17.0F);
    ProceduralMusicComposer composer {8000};
    composer.set_environment(environment, cycle, true, false, classic_context());
    render_seconds(composer, 2.0);

    composer.set_environment(environment, cycle, true, false, sea_context(1.0F, 0.25F));
    const auto toward_sea = render_seconds(composer, 0.8);
    composer.set_environment(environment, cycle, true, false, classic_context());
    const auto toward_classic = render_seconds(composer, 0.25);
    composer.set_environment(environment, cycle, true, false, sea_context(1.0F, 0.25F));
    const auto reversed_again = render_seconds(composer, 0.25);

    REQUIRE(toward_sea.size() >= 2U);
    REQUIRE(toward_classic.size() >= 2U);
    REQUIRE(reversed_again.size() >= 2U);
    CHECK(std::abs(toward_sea[toward_sea.size() - 2U] - toward_classic[0]) < 0.015F);
    CHECK(std::abs(toward_sea[toward_sea.size() - 1U] - toward_classic[1]) < 0.015F);
    CHECK(std::abs(toward_classic[toward_classic.size() - 2U] - reversed_again[0]) < 0.015F);
    CHECK(std::abs(toward_classic[toward_classic.size() - 1U] - reversed_again[1]) < 0.015F);
    CHECK(maximum_channel_step(toward_classic) < 0.03F);
    CHECK(maximum_channel_step(reversed_again) < 0.03F);
    CHECK(rms_level(reversed_again) > 0.0005F);
}

TEST_CASE("changing the maritime seed waits for future notes without cutting active voices") {
    constexpr auto kInitialSeed = std::uint32_t {0xA51CE55U};
    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    ProceduralMusicComposer changed_seed {8000};
    ProceduralMusicComposer unchanged_seed {8000};
    changed_seed.set_environment(
        environment, cycle, true, false, sea_context(1.0F, 0.2F, kInitialSeed));
    unchanged_seed.set_environment(
        environment, cycle, true, false, sea_context(1.0F, 0.2F, kInitialSeed));

    const auto changed_warmup = render_seconds(changed_seed, 2.07);
    const auto unchanged_warmup = render_seconds(unchanged_seed, 2.07);
    CHECK(max_absolute_difference(changed_warmup, unchanged_warmup) == doctest::Approx(0.0F));

    changed_seed.set_environment(
        environment, cycle, true, false, sea_context(1.0F, 0.2F, kInitialSeed + 1U));
    const auto changed_tail = render_seconds(changed_seed, 0.10);
    const auto unchanged_tail = render_seconds(unchanged_seed, 0.10);
    CHECK(max_absolute_difference(changed_tail, unchanged_tail) == doctest::Approx(0.0F));

    const auto changed_future = render_seconds(changed_seed, 0.50);
    const auto unchanged_future = render_seconds(unchanged_seed, 0.50);
    CHECK(mean_absolute_difference(changed_future, unchanged_future) > 1.0e-7F);
    CHECK(samples_are_finite_and_bounded(changed_future));
    CHECK(maximum_channel_step(changed_future) < 0.03F);
}

TEST_CASE("maritime orchestration changes are quantized at the next measure") {
    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    const auto moored = sea_context(0.0F, 0.0F, 0xBADC0DEU);
    ProceduralMusicComposer adaptive {8000};
    ProceduralMusicComposer control {8000};
    adaptive.set_environment(environment, cycle, true, false, moored);
    control.set_environment(environment, cycle, true, false, moored);
    render_seconds(adaptive, 0.30);
    render_seconds(control, 0.30);

    adaptive.set_environment(
        environment, cycle, true, false, sea_context(1.0F, 0.0F, moored.seed));
    const auto before_measure = render_seconds(adaptive, 0.80);
    const auto control_before_measure = render_seconds(control, 0.80);
    CHECK(max_absolute_difference(before_measure, control_before_measure) < 1.0e-7F);

    const auto after_measure = render_seconds(adaptive, 0.50);
    const auto control_after_measure = render_seconds(control, 0.50);
    CHECK(mean_absolute_difference(after_measure, control_after_measure) > 1.0e-6F);
    CHECK(samples_are_finite_and_bounded(after_measure));
}

TEST_CASE("le filtre de noyade publie toujours une cible atomique bornee") {
    GameMusic music {};

    music.set_backrooms_drowning_filter(0.63F);
    CHECK(music.backrooms_drowning_filter() == doctest::Approx(0.63F));

    music.set_backrooms_drowning_filter(-4.0F);
    CHECK(music.backrooms_drowning_filter() == doctest::Approx(0.0F));

    music.set_backrooms_drowning_filter(4.0F);
    CHECK(music.backrooms_drowning_filter() == doctest::Approx(1.0F));

    music.set_backrooms_drowning_filter(
        std::numeric_limits<float>::quiet_NaN());
    CHECK(music.backrooms_drowning_filter() == doctest::Approx(0.0F));
}

}
