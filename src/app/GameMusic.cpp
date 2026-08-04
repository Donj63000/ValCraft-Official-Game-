#include "app/GameMusic.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <span>

namespace valcraft {

namespace {

constexpr int kPreferredSampleRate = 48000;
constexpr std::uint16_t kPreferredChannels = 2;
constexpr std::uint16_t kPreferredBufferFrames = 2048;
} // namespace

auto GameMusic::initialize() -> bool {
    if (device_id_ != 0) {
        return true;
    }

    if (SDL_WasInit(SDL_INIT_AUDIO) == 0U) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            return false;
        }
        owns_audio_subsystem_ = true;
    }

    SDL_AudioSpec desired_spec {};
    desired_spec.freq = kPreferredSampleRate;
    desired_spec.format = AUDIO_F32SYS;
    desired_spec.channels = kPreferredChannels;
    desired_spec.samples = kPreferredBufferFrames;
    desired_spec.callback = &GameMusic::audio_callback;
    desired_spec.userdata = this;

    std::memset(&obtained_spec_, 0, sizeof(obtained_spec_));
    device_id_ = SDL_OpenAudioDevice(
        nullptr,
        0,
        &desired_spec,
        &obtained_spec_,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (device_id_ == 0) {
        shutdown();
        return false;
    }

    composer_ = ProceduralMusicComposer(obtained_spec_.freq);
    backrooms_ambience_.set_sample_rate(obtained_spec_.freq);
    backrooms_ambience_.reset();
    sfx_mixer_.set_sample_rate(obtained_spec_.freq);
    sfx_mixer_.reset();
    drowning_filter_state_.fill(0.0F);
    drowning_filter_intensity_.store(0.0F, std::memory_order_relaxed);
    SDL_PauseAudioDevice(device_id_, 0);
    return true;
}

void GameMusic::shutdown() noexcept {
    if (device_id_ != 0) {
        SDL_PauseAudioDevice(device_id_, 1);
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
    }

    backrooms_ambience_.reset();
    sfx_mixer_.reset();
    drowning_filter_state_.fill(0.0F);
    drowning_filter_intensity_.store(0.0F, std::memory_order_relaxed);

    if (owns_audio_subsystem_ && SDL_WasInit(SDL_INIT_AUDIO) != 0U) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    owns_audio_subsystem_ = false;
}

void GameMusic::sync_environment(const EnvironmentState& environment,
                                 const CreatureCycleState& cycle,
                                 bool has_active_session,
                                 bool front_end_visible,
                                 const ProceduralMusicContext& context) noexcept {
    if (device_id_ == 0) {
        return;
    }

    SDL_LockAudioDevice(device_id_);
    const auto finite_unit = [](float value) noexcept {
        return std::isfinite(value)
                   ? std::clamp(value, 0.0F, 1.0F)
                   : 0.0F;
    };
    const auto backrooms_active =
        has_active_session &&
        !front_end_visible &&
        context.scene ==
            ProceduralMusicScene::Backrooms;
    backrooms_ambience_.set_context({
        .active = backrooms_active,
        .seed = context.seed,
        .darkness =
            finite_unit(
                1.0F -
                environment.exposure),
        .anomaly =
            finite_unit(
                (environment.vignette_strength -
                 0.16F) /
                0.24F),
    });

    // Le compositeur musical prépare son bus classique pendant le fondu, mais
    // l'ambiance BackRooms le remplace progressivement dans le callback.
    auto composer_context = context;
    if (composer_context.scene ==
        ProceduralMusicScene::Backrooms) {
        composer_context.scene =
            ProceduralMusicScene::Classic;
    }
    composer_.set_environment(
        environment,
        cycle,
        has_active_session,
        front_end_visible,
        composer_context);
    SDL_UnlockAudioDevice(device_id_);
}

void GameMusic::pump() {
    // Je garde cette methode pour ne pas toucher a la boucle de jeu, mais le
    // rendu audio vit maintenant dans le callback SDL et reste fluide en cas de frame lente.
}

void GameMusic::play_sfx(GameSfxKind kind,
                         float volume,
                         float pan,
                         float attenuation,
                         std::uint32_t deterministic_seed) noexcept {
    if (device_id_ == 0 || volume <= 0.0F) {
        return;
    }

    SDL_LockAudioDevice(device_id_);
    sfx_mixer_.play({
        .kind = kind,
        .volume = volume,
        .pan = pan,
        .attenuation = attenuation,
        .seed = deterministic_seed,
    });
    SDL_UnlockAudioDevice(device_id_);
}

void GameMusic::set_backrooms_drowning_filter(float intensity) noexcept {
    const auto sanitized =
        std::isfinite(intensity)
            ? std::clamp(intensity, 0.0F, 1.0F)
            : 0.0F;
    // Je publie uniquement une cible scalaire au callback. Son propre thread
    // reste l'unique proprietaire de l'historique du filtre et le thread jeu
    // ne bloque donc plus le peripherique a chaque frame de noyade.
    drowning_filter_intensity_.store(
        sanitized,
        std::memory_order_relaxed);
}

auto GameMusic::backrooms_drowning_filter() const noexcept -> float {
    return drowning_filter_intensity_.load(std::memory_order_relaxed);
}

auto GameMusic::available() const noexcept -> bool {
    return device_id_ != 0;
}

void GameMusic::audio_callback(void* userdata, Uint8* stream, int len) noexcept {
    if (stream == nullptr || len <= 0) {
        return;
    }

    std::memset(stream, 0, static_cast<std::size_t>(len));
    auto* music = static_cast<GameMusic*>(userdata);
    if (music == nullptr) {
        return;
    }

    music->render_callback_stream(stream, len);
}

void GameMusic::render_callback_stream(Uint8* stream, int len) noexcept {
    if (stream == nullptr || len <= 0 || obtained_spec_.channels == 0) {
        return;
    }

    auto sample_count = static_cast<std::size_t>(len) / sizeof(float);
    const auto channel_count = static_cast<std::size_t>(obtained_spec_.channels);
    sample_count -= sample_count % channel_count;
    if (sample_count == 0U) {
        return;
    }

    auto* output = reinterpret_cast<float*>(stream);
    auto output_samples =
        std::span<float> {output, sample_count};
    composer_.render_interleaved(output_samples);
    backrooms_ambience_.mix_interleaved(
        output_samples,
        channel_count);
    sfx_mixer_.mix_interleaved(
        std::span<float> {output, sample_count},
        channel_count);

    // Je place le filtre apres tous les bus : pendant la saisie de Marlow,
    // musique, ambiance et effets semblent traverser la meme masse d'eau.
    const auto drowning =
        std::clamp(
            drowning_filter_intensity_.load(std::memory_order_relaxed),
            0.0F,
            1.0F);
    const auto tracked_channels =
        std::min(channel_count, drowning_filter_state_.size());
    const auto cutoff_hz =
        18'000.0F +
        (720.0F - 18'000.0F) * drowning;
    constexpr auto kTwoPi = 6.28318530717958647692F;
    const auto alpha =
        1.0F -
        std::exp(
            -kTwoPi * cutoff_hz /
            static_cast<float>(
                std::max(obtained_spec_.freq, 1)));
    const auto wet_gain = 1.0F - drowning * 0.24F;
    for (std::size_t sample_index = 0U;
         sample_index < sample_count;
         ++sample_index) {
        const auto channel = sample_index % channel_count;
        if (channel >= tracked_channels) {
            output_samples[sample_index] *= wet_gain;
            continue;
        }
        const auto dry = output_samples[sample_index];
        auto& filtered = drowning_filter_state_[channel];
        filtered += (dry - filtered) * alpha;
        output_samples[sample_index] =
            (dry + (filtered - dry) * drowning) *
            wet_gain;
    }
}

} // namespace valcraft
