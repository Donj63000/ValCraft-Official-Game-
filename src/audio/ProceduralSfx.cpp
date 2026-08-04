#include "audio/ProceduralSfx.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;

[[nodiscard]] auto finite_unit(float value, float fallback = 0.0F) noexcept -> float {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : fallback;
}

[[nodiscard]] auto finite_pan(float value) noexcept -> float {
    return std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
}

} // namespace

ProceduralSfxMixer::ProceduralSfxMixer(int sample_rate) noexcept {
    set_sample_rate(sample_rate);
}

void ProceduralSfxMixer::set_sample_rate(int sample_rate) noexcept {
    sample_rate_ = std::clamp(sample_rate, 8'000, 192'000);
}

void ProceduralSfxMixer::reset() noexcept {
    for (auto& voice : voices_) {
        voice = {};
    }
    next_seed_ = 0x9E3779B9U;
}

void ProceduralSfxMixer::play(const ProceduralSfxRequest& request) noexcept {
    const auto volume = finite_unit(request.volume) * finite_unit(request.attenuation, 1.0F);
    if (volume <= 0.0F) {
        return;
    }

    auto* selected = static_cast<Voice*>(nullptr);
    auto oldest_ratio = -1.0F;
    for (auto& voice : voices_) {
        if (!voice.active) {
            selected = &voice;
            break;
        }
        const auto age_ratio =
            voice.duration > 1.0e-5F
                ? voice.age / voice.duration
                : std::numeric_limits<float>::infinity();
        if (age_ratio > oldest_ratio) {
            oldest_ratio = age_ratio;
            selected = &voice;
        }
    }
    if (selected == nullptr) {
        return;
    }

    next_seed_ = next_seed_ * 1664525U + 1013904223U;
    auto seed = request.seed != 0U ? request.seed : next_seed_;
    if (seed == 0U) {
        seed = 1U;
    }

    *selected = {};
    selected->kind = request.kind;
    selected->duration = effect_duration(request.kind);
    selected->volume = volume;
    selected->pan = finite_pan(request.pan);
    selected->seed = seed;
    // Je decale legerement la phase grave par graine sans changer la duree.
    selected->phase =
        static_cast<float>((seed >> 9U) & 0x3FFU) /
        1024.0F * kTwoPi;
    selected->secondary_phase =
        static_cast<float>((seed >> 19U) & 0x3FFU) /
        1024.0F * kTwoPi;
    selected->active = true;
}

void ProceduralSfxMixer::mix_interleaved(std::span<float> output,
                                         std::size_t channel_count) noexcept {
    if (output.empty() || channel_count == 0U) {
        return;
    }
    const auto frame_count = output.size() / channel_count;
    if (frame_count == 0U) {
        return;
    }

    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
        auto left = 0.0F;
        auto right = 0.0F;
        auto rendered_voice = false;
        for (auto& voice : voices_) {
            if (!voice.active) {
                continue;
            }

            const auto sample = render_voice_sample(voice);
            const auto angle = (voice.pan + 1.0F) * (kPi * 0.25F);
            left += sample * std::cos(angle);
            right += sample * std::sin(angle);
            rendered_voice = true;
        }
        if (!rendered_voice) {
            continue;
        }

        const auto base = frame * channel_count;
        if (channel_count == 1U) {
            output[base] = soft_limit(output[base] + (left + right) * 0.70710678F);
            continue;
        }

        output[base] = soft_limit(output[base] + left);
        output[base + 1U] = soft_limit(output[base + 1U] + right);
        const auto center = (left + right) * 0.70710678F;
        for (std::size_t channel = 2U; channel < channel_count; ++channel) {
            output[base + channel] = soft_limit(output[base + channel] + center);
        }
    }
}

auto ProceduralSfxMixer::active_voice_count() const noexcept -> std::size_t {
    return static_cast<std::size_t>(
        std::count_if(
            std::begin(voices_),
            std::end(voices_),
            [](const Voice& voice) noexcept {
                return voice.active;
            }));
}

auto ProceduralSfxMixer::maximum_voice_count() noexcept -> std::size_t {
    return kMaximumVoices;
}

auto ProceduralSfxMixer::effect_duration(GameSfxKind kind) noexcept -> float {
    switch (kind) {
    case GameSfxKind::SwordSwing:
        return 0.18F;
    case GameSfxKind::CreatureHit:
        return 0.22F;
    case GameSfxKind::CreatureDeath:
        return 0.72F;
    case GameSfxKind::CreatureAttack:
        return 0.30F;
    case GameSfxKind::MusketShot:
        return 1.08F;
    case GameSfxKind::HeavySwing:
        return 0.58F;
    case GameSfxKind::BoneImpact:
        return 0.36F;
    case GameSfxKind::MetalImpact:
        return 0.82F;
    case GameSfxKind::PerfectGuard:
        return 0.62F;
    case GameSfxKind::ChainBreak:
        return 1.10F;
    case GameSfxKind::ColossusRoar:
        return 1.85F;
    case GameSfxKind::Crowd:
        return 1.65F;
    case GameSfxKind::SeaLeviathan:
        return 2.25F;
    case GameSfxKind::JackBootStep:
        return 0.34F;
    case GameSfxKind::JackPegStep:
        return 0.52F;
    case GameSfxKind::JackNotice:
        return 1.10F;
    case GameSfxKind::JackChase:
        return 1.55F;
    case GameSfxKind::JackScreamer:
        return 1.20F;
    case GameSfxKind::MarlowWaterSignal:
        return 1.35F;
    case GameSfxKind::MarlowSurface:
        return 1.05F;
    case GameSfxKind::MarlowSubmerge:
        return 0.92F;
    case GameSfxKind::MarlowGrab:
        return 1.15F;
    case GameSfxKind::MarlowScreamer:
        return 0.85F;
    case GameSfxKind::MarlowDistantSplash:
        return 0.78F;
    }
    return 0.10F;
}

auto ProceduralSfxMixer::next_noise_unit(std::uint32_t& seed) noexcept -> float {
    seed = seed * 1664525U + 1013904223U;
    const auto value =
        static_cast<float>((seed >> 8U) & 0x00FFFFFFU) /
        static_cast<float>(0x01000000U);
    return value * 2.0F - 1.0F;
}

auto ProceduralSfxMixer::render_voice_sample(Voice& voice) const noexcept -> float {
    if (!voice.active || voice.duration <= 1.0e-5F || sample_rate_ <= 1) {
        voice.active = false;
        return 0.0F;
    }

    const auto sample_rate = static_cast<float>(sample_rate_);
    const auto normalized_age = std::clamp(voice.age / voice.duration, 0.0F, 1.0F);
    const auto decay = 1.0F - normalized_age;
    const auto noise = next_noise_unit(voice.seed);
    auto sample = 0.0F;

    switch (voice.kind) {
    case GameSfxKind::SwordSwing: {
        const auto envelope = std::sin(normalized_age * kPi) * decay;
        const auto frequency = 310.0F - 190.0F * normalized_age;
        voice.phase += kTwoPi * frequency / sample_rate;
        sample =
            (std::sin(voice.phase) * 0.28F + noise * 0.16F) *
            envelope * 0.80F;
        break;
    }
    case GameSfxKind::CreatureHit: {
        const auto envelope = decay * decay;
        const auto frequency = 118.0F - 36.0F * normalized_age;
        voice.phase += kTwoPi * frequency / sample_rate;
        sample =
            (std::sin(voice.phase) * 0.42F +
             std::sin(voice.phase * 2.17F) * 0.18F +
             noise * 0.22F) *
            envelope;
        break;
    }
    case GameSfxKind::CreatureDeath: {
        const auto envelope =
            decay * decay *
            (0.75F + 0.25F * std::sin(normalized_age * kPi));
        const auto frequency = 150.0F - 86.0F * normalized_age;
        voice.phase += kTwoPi * frequency / sample_rate;
        sample =
            (std::sin(voice.phase) * 0.46F +
             std::sin(voice.phase * 0.51F) * 0.18F +
             noise * 0.10F) *
            envelope;
        break;
    }
    case GameSfxKind::CreatureAttack: {
        const auto envelope =
            decay *
            (0.65F + 0.35F * std::sin(normalized_age * kPi));
        const auto frequency =
            86.0F + 26.0F * std::sin(normalized_age * kTwoPi);
        voice.phase += kTwoPi * frequency / sample_rate;
        sample =
            (std::sin(voice.phase) * 0.38F + noise * 0.18F) *
            envelope * 0.82F;
        break;
    }
    case GameSfxKind::MusketShot: {
        // Je superpose un claquement tres bref, le corps grave de la charge
        // noire puis une queue filtree qui donne de la masse sans asset WAV.
        const auto t = voice.age;
        const auto high_noise = noise - voice.previous_noise * 0.72F;
        voice.previous_noise = noise;
        voice.filtered_noise += (noise - voice.filtered_noise) * 0.055F;

        const auto crack_envelope = std::exp(-t * 115.0F);
        const auto boom_envelope =
            (1.0F - std::exp(-t * 150.0F)) *
            std::exp(-t * 6.0F);
        const auto tail_envelope =
            (1.0F - std::exp(-t * 24.0F)) *
            std::exp(-t * 3.35F);
        const auto boom_frequency = 79.0F - 28.0F * normalized_age;
        voice.phase += kTwoPi * boom_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * (43.0F - 9.0F * normalized_age) /
            sample_rate;

        const auto crack = high_noise * crack_envelope * 1.18F;
        const auto boom =
            (std::sin(voice.phase) * 0.72F +
             std::sin(voice.secondary_phase) * 0.34F) *
            boom_envelope;
        const auto tail =
            (voice.filtered_noise * 0.62F +
             std::sin(voice.phase * 0.37F) * 0.13F) *
            tail_envelope;
        sample = crack + boom + tail;
        break;
    }
    case GameSfxKind::HeavySwing: {
        // Je donne au balayage de l'Echine une masse grave, un souffle large
        // et une pointe osseuse sans charger le moindre fichier externe.
        const auto high_noise =
            noise - voice.previous_noise * 0.62F;
        voice.previous_noise = noise;
        voice.filtered_noise +=
            (noise - voice.filtered_noise) * 0.075F;
        const auto body_envelope =
            std::sin(normalized_age * kPi) *
            std::sqrt(std::max(decay, 0.0F));
        const auto whoosh_envelope =
            std::sin(normalized_age * kPi) *
            std::sin(normalized_age * kPi);
        const auto body_frequency =
            124.0F - 78.0F * normalized_age;
        const auto edge_frequency =
            392.0F - 218.0F * normalized_age;
        voice.phase +=
            kTwoPi * body_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * edge_frequency / sample_rate;
        const auto body =
            (std::sin(voice.phase) * 0.42F +
             std::sin(voice.phase * 0.51F) * 0.19F) *
            body_envelope;
        const auto air =
            (voice.filtered_noise * 0.48F +
             high_noise * 0.12F) *
            whoosh_envelope;
        const auto edge =
            std::sin(voice.secondary_phase) *
            decay * 0.10F;
        sample = body + air + edge;
        break;
    }
    case GameSfxKind::BoneImpact: {
        // Je separe le craquement sec de l'impact grave pour garder un retour
        // immediatement reconnaissable sur la chair et les os.
        const auto t = voice.age;
        const auto transient = std::exp(-t * 52.0F);
        const auto body_envelope = std::exp(-t * 9.0F);
        const auto body_frequency =
            102.0F - 54.0F * normalized_age;
        const auto crack_frequency =
            684.0F - 376.0F * normalized_age;
        voice.phase +=
            kTwoPi * body_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * crack_frequency / sample_rate;
        sample =
            noise * transient * 0.46F +
            std::sin(voice.secondary_phase) *
                transient * 0.24F +
            (std::sin(voice.phase) * 0.48F +
             std::sin(voice.phase * 1.93F) * 0.17F) *
                body_envelope;
        break;
    }
    case GameSfxKind::MetalImpact: {
        // Je construis la resonance metallique avec deux partiels
        // volontairement inharmoniques et une attaque tres breve.
        const auto t = voice.age;
        const auto strike = std::exp(-t * 92.0F);
        const auto resonance = std::exp(-t * 4.6F);
        const auto primary_frequency =
            486.0F - 172.0F * normalized_age;
        const auto secondary_frequency =
            731.0F - 238.0F * normalized_age;
        voice.phase +=
            kTwoPi * primary_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * secondary_frequency / sample_rate;
        sample =
            noise * strike * 0.38F +
            (std::sin(voice.phase) * 0.43F +
             std::sin(voice.secondary_phase) * 0.31F +
             std::sin(voice.phase * 1.4142F) * 0.18F) *
                resonance;
        break;
    }
    case GameSfxKind::PerfectGuard: {
        // Je rends la garde parfaite plus brillante que les autres impacts,
        // avec une cloche courte posee sur un choc grave.
        const auto t = voice.age;
        const auto strike = std::exp(-t * 105.0F);
        const auto chime = std::exp(-t * 6.2F);
        const auto low_frequency =
            164.0F - 72.0F * normalized_age;
        const auto chime_frequency =
            910.0F - 322.0F * normalized_age;
        voice.phase +=
            kTwoPi * low_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * chime_frequency / sample_rate;
        sample =
            noise * strike * 0.30F +
            std::sin(voice.phase) * strike * 0.48F +
            (std::sin(voice.secondary_phase) * 0.45F +
             std::sin(voice.secondary_phase * 1.503F) * 0.22F) *
                chime;
        break;
    }
    case GameSfxKind::ChainBreak: {
        // Je combine la rupture initiale, la traction grave et les maillons
        // qui retombent; la modulation evite une simple copie de l'impact metal.
        const auto t = voice.age;
        const auto rupture = std::exp(-t * 76.0F);
        const auto resonance = std::exp(-t * 3.7F);
        const auto rattle =
            std::max(
                0.0F,
                std::sin(t * kTwoPi * 17.0F)) *
            std::exp(-t * 3.1F);
        const auto low_frequency =
            138.0F - 76.0F * normalized_age;
        const auto link_frequency =
            438.0F - 244.0F * normalized_age;
        voice.phase +=
            kTwoPi * low_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * link_frequency / sample_rate;
        voice.filtered_noise +=
            (noise - voice.filtered_noise) * 0.16F;
        sample =
            noise * rupture * 0.52F +
            std::sin(voice.phase) * resonance * 0.38F +
            (std::sin(voice.secondary_phase) * 0.35F +
             voice.filtered_noise * 0.22F) *
                (resonance + rattle * 0.55F);
        break;
    }
    case GameSfxKind::ColossusRoar: {
        // Je reserve au colosse une voix basse et granuleuse dont l'attaque
        // lente conserve sa presence meme au milieu du combat.
        const auto t = voice.age;
        const auto attack =
            1.0F - std::exp(-t * 11.0F);
        const auto release =
            std::min(1.0F, decay * 5.0F);
        const auto envelope = attack * release;
        const auto growl_frequency =
            66.0F - 21.0F * normalized_age +
            std::sin(t * kTwoPi * 4.2F) * 5.0F;
        const auto formant_frequency =
            124.0F - 32.0F * normalized_age;
        voice.phase +=
            kTwoPi * growl_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * formant_frequency / sample_rate;
        voice.filtered_noise +=
            (noise - voice.filtered_noise) * 0.035F;
        sample =
            (std::sin(voice.phase) * 0.50F +
             std::sin(voice.phase * 0.49F) * 0.27F +
             std::sin(voice.secondary_phase) * 0.20F +
             voice.filtered_noise * 0.30F) *
            envelope;
        break;
    }
    case GameSfxKind::Crowd: {
        // Je simule la foule comme une nappe de voix filtrees et modulees:
        // une seule voix du mixeur suffit, donc le budget reste previsible.
        const auto t = voice.age;
        const auto attack =
            std::min(1.0F, t * 8.0F);
        const auto release =
            std::min(1.0F, decay * 4.0F);
        const auto envelope = attack * release;
        voice.filtered_noise +=
            (noise - voice.filtered_noise) * 0.018F;
        const auto chant_frequency =
            102.0F +
            std::sin(t * kTwoPi * 1.7F) * 13.0F;
        const auto group_frequency =
            171.0F +
            std::sin(t * kTwoPi * 1.13F) * 19.0F;
        voice.phase +=
            kTwoPi * chant_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * group_frequency / sample_rate;
        sample =
            (voice.filtered_noise * 0.48F +
             std::sin(voice.phase) * 0.17F +
             std::sin(voice.secondary_phase) * 0.14F +
             std::sin(voice.phase * 1.37F) * 0.09F) *
            envelope;
        break;
    }
    case GameSfxKind::SeaLeviathan: {
        // Je fais emerger le leviathan marin par une sub-basse, un grondement
        // humide et une longue expiration, sans allocation dans le callback.
        const auto t = voice.age;
        const auto attack =
            1.0F - std::exp(-t * 6.5F);
        const auto release =
            std::min(1.0F, decay * 4.5F);
        const auto surge =
            0.72F +
            std::sin(t * kTwoPi * 1.45F) * 0.28F;
        const auto envelope = attack * release;
        const auto sub_frequency =
            39.0F - 13.0F * normalized_age;
        const auto call_frequency =
            88.0F - 31.0F * normalized_age +
            std::sin(t * kTwoPi * 2.1F) * 4.0F;
        voice.phase +=
            kTwoPi * sub_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * call_frequency / sample_rate;
        voice.filtered_noise +=
            (noise - voice.filtered_noise) * 0.024F;
        sample =
            (std::sin(voice.phase) * 0.55F +
             std::sin(voice.secondary_phase) * 0.25F +
             std::sin(voice.secondary_phase * 0.503F) * 0.18F +
             voice.filtered_noise * 0.34F) *
            envelope * surge;
        break;
    }
    case GameSfxKind::JackBootStep: {
        // Je separe le choc mat de la semelle et son frottement afin que la
        // botte reste identifiable lorsqu'elle alterne avec la jambe de bois.
        const auto t = voice.age;
        const auto impact = std::exp(-t * 34.0F);
        const auto body = std::exp(-t * 10.0F);
        const auto scrape =
            (1.0F - std::exp(-t * 75.0F)) *
            std::exp(-t * 18.0F);
        const auto body_frequency =
            74.0F - 31.0F * normalized_age;
        const auto sole_frequency =
            168.0F - 76.0F * normalized_age;
        voice.phase +=
            kTwoPi * body_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * sole_frequency / sample_rate;
        voice.filtered_noise +=
            (noise - voice.filtered_noise) * 0.11F;
        sample =
            std::sin(voice.phase) * body * 0.46F +
            std::sin(voice.secondary_phase) * impact * 0.13F +
            noise * impact * 0.18F +
            voice.filtered_noise * scrape * 0.20F;
        break;
    }
    case GameSfxKind::JackPegStep: {
        // Je donne a la jambe de bois un claquement sec, deux resonances
        // inharmoniques et un retour grave transmis par le sol.
        const auto t = voice.age;
        const auto high_noise =
            noise - voice.previous_noise * 0.58F;
        voice.previous_noise = noise;
        const auto strike = std::exp(-t * 76.0F);
        const auto wood_resonance = std::exp(-t * 8.2F);
        const auto floor_resonance = std::exp(-t * 5.4F);
        const auto wood_frequency =
            244.0F - 91.0F * normalized_age;
        const auto knock_frequency =
            526.0F - 184.0F * normalized_age;
        voice.phase +=
            kTwoPi * wood_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * knock_frequency / sample_rate;
        sample =
            high_noise * strike * 0.38F +
            std::sin(voice.secondary_phase) *
                (strike * 0.31F +
                 wood_resonance * 0.20F) +
            std::sin(voice.phase) *
                wood_resonance * 0.43F +
            std::sin(voice.phase * 0.286F) *
                floor_resonance * 0.29F;
        break;
    }
    case GameSfxKind::JackNotice: {
        // Je fais entendre son cou qui craque puis une respiration retenue :
        // le signal avertit le joueur sans employer une voix enregistree.
        const auto t = voice.age;
        const auto attack =
            1.0F - std::exp(-t * 18.0F);
        const auto release =
            std::min(1.0F, decay * 5.0F);
        const auto envelope = attack * release;
        const auto neck_offset =
            (t - 0.16F) / 0.037F;
        const auto neck_snap =
            std::exp(
                -(neck_offset * neck_offset));
        const auto breath_frequency =
            48.0F +
            std::sin(t * kTwoPi * 2.3F) * 4.5F;
        const auto throat_frequency =
            119.0F -
            24.0F * normalized_age;
        voice.phase +=
            kTwoPi * breath_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * throat_frequency / sample_rate;
        voice.filtered_noise +=
            (noise - voice.filtered_noise) * 0.026F;
        sample =
            (std::sin(voice.phase) * 0.32F +
             std::sin(voice.secondary_phase) * 0.17F +
             voice.filtered_noise * 0.31F) *
                envelope +
            (noise * 0.24F +
             std::sin(voice.secondary_phase * 2.17F) * 0.20F) *
                neck_snap;
        break;
    }
    case GameSfxKind::JackChase: {
        // Je construis le depart de traque avec un ralement granuleux, deux
        // formants instables et une montee rapide qui ne masque pas ses pas.
        const auto t = voice.age;
        const auto attack =
            1.0F - std::exp(-t * 13.0F);
        const auto release =
            std::min(1.0F, decay * 4.5F);
        const auto pulse =
            0.78F +
            std::sin(t * kTwoPi * 5.7F) * 0.22F;
        const auto envelope =
            attack * release * pulse;
        const auto growl_frequency =
            67.0F -
            19.0F * normalized_age +
            std::sin(t * kTwoPi * 3.6F) * 6.0F;
        const auto formant_frequency =
            153.0F -
            47.0F * normalized_age +
            std::sin(t * kTwoPi * 2.1F) * 9.0F;
        voice.phase +=
            kTwoPi * growl_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * formant_frequency / sample_rate;
        voice.filtered_noise +=
            (noise - voice.filtered_noise) * 0.045F;
        sample =
            (std::sin(voice.phase) * 0.48F +
             std::sin(voice.phase * 0.51F) * 0.22F +
             std::sin(voice.secondary_phase) * 0.25F +
             voice.filtered_noise * 0.34F) *
            envelope;
        break;
    }
    case GameSfxKind::JackScreamer: {
        // Je reserve au screamer une attaque tres rapide, un cri descendant
        // et un coup subgrave. Le limiteur commun garde le sursaut sans clip.
        const auto t = voice.age;
        const auto high_noise =
            noise - voice.previous_noise * 0.78F;
        voice.previous_noise = noise;
        const auto attack =
            1.0F - std::exp(-t * 180.0F);
        const auto release =
            std::min(1.0F, decay * 6.0F);
        const auto scream_envelope =
            attack * release *
            (0.84F +
             std::sin(t * kTwoPi * 21.0F) * 0.16F);
        const auto primary_frequency =
            1'180.0F -
            720.0F * normalized_age;
        const auto secondary_frequency =
            1'760.0F -
            1'030.0F * normalized_age;
        voice.phase +=
            kTwoPi * primary_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * secondary_frequency / sample_rate;
        const auto sub_frequency =
            61.0F -
            20.0F * normalized_age;
        const auto sub_impact =
            std::sin(t * kTwoPi * sub_frequency) *
            std::exp(-t * 6.0F);
        sample =
            (std::sin(voice.phase) * 0.44F +
             std::sin(voice.secondary_phase) * 0.25F +
             high_noise * 0.50F) *
                scream_envelope +
            sub_impact * 0.36F;
        break;
    }
    case GameSfxKind::MarlowWaterSignal:
    case GameSfxKind::MarlowDistantSplash: {
        // Je compose ici les avertissements aquatiques de Marlow : une masse
        // grave transmise par le bassin et des gouttes plus claires, sans
        // confondre ce signal juste avec un depart de poursuite de Jack.
        const auto t = voice.age;
        const auto attack = 1.0F - std::exp(-t * 28.0F);
        const auto release = decay * decay;
        const auto distant =
            voice.kind == GameSfxKind::MarlowDistantSplash;
        voice.filtered_noise +=
            (noise - voice.filtered_noise) *
            (distant ? 0.018F : 0.032F);
        voice.phase +=
            kTwoPi *
            (distant ? 58.0F : 46.0F) /
            sample_rate;
        const auto droplets =
            std::max(
                std::sin(
                    t * kTwoPi *
                    (distant ? 7.0F : 11.0F)),
                0.0F);
        sample =
            (std::sin(voice.phase) * 0.36F +
             voice.filtered_noise * 0.45F) *
                attack * release +
            noise * droplets * release *
                (distant ? 0.08F : 0.14F);
        break;
    }
    case GameSfxKind::MarlowSurface:
    case GameSfxKind::MarlowSubmerge: {
        // Je differencie l'emergence et la replongee par le sens du glissando,
        // tout en conservant la meme signature de remous epais et humide.
        const auto t = voice.age;
        const auto submerge =
            voice.kind == GameSfxKind::MarlowSubmerge;
        const auto attack = 1.0F - std::exp(-t * 70.0F);
        const auto envelope = attack * decay * decay;
        const auto frequency =
            submerge
                ? 96.0F - 58.0F * normalized_age
                : 39.0F + 74.0F * normalized_age;
        voice.phase +=
            kTwoPi * frequency / sample_rate;
        voice.filtered_noise +=
            (noise - voice.filtered_noise) * 0.075F;
        sample =
            (std::sin(voice.phase) * 0.32F +
             voice.filtered_noise * 0.62F) *
            envelope;
        break;
    }
    case GameSfxKind::MarlowGrab: {
        // Je rends la saisie immediate avec un impact sourd, une aspiration
        // liquide et un frottement qui prepare la noyade sans faux screamer.
        const auto t = voice.age;
        const auto impact = std::exp(-t * 34.0F);
        const auto suction =
            (1.0F - std::exp(-t * 16.0F)) *
            std::exp(-t * 2.7F);
        voice.phase +=
            kTwoPi * (72.0F - 25.0F * normalized_age) /
            sample_rate;
        voice.filtered_noise +=
            (noise - voice.filtered_noise) * 0.055F;
        sample =
            std::sin(voice.phase) * impact * 0.48F +
            voice.filtered_noise * suction * 0.52F;
        break;
    }
    case GameSfxKind::MarlowScreamer: {
        // Je donne a Marlow un cri plus etouffe et hydrophonique que celui de
        // Jack, avec deux formants instables et une percussion subaquatique.
        const auto t = voice.age;
        const auto attack = 1.0F - std::exp(-t * 150.0F);
        const auto release = std::min(1.0F, decay * 7.0F);
        const auto envelope =
            attack * release *
            (0.86F + 0.14F * std::sin(t * kTwoPi * 17.0F));
        voice.phase +=
            kTwoPi *
            (910.0F - 510.0F * normalized_age) /
            sample_rate;
        voice.secondary_phase +=
            kTwoPi *
            (1'390.0F - 810.0F * normalized_age) /
            sample_rate;
        voice.filtered_noise +=
            (noise - voice.filtered_noise) * 0.12F;
        const auto pressure =
            std::sin(t * kTwoPi * 48.0F) *
            std::exp(-t * 5.2F);
        sample =
            (std::sin(voice.phase) * 0.38F +
             std::sin(voice.secondary_phase) * 0.24F +
             voice.filtered_noise * 0.46F) *
                envelope +
            pressure * 0.34F;
        break;
    }
    }

    if (voice.phase > kTwoPi) {
        voice.phase = std::fmod(voice.phase, kTwoPi);
    }
    if (voice.secondary_phase > kTwoPi) {
        voice.secondary_phase = std::fmod(voice.secondary_phase, kTwoPi);
    }

    voice.age += 1.0F / sample_rate;
    if (voice.age >= voice.duration) {
        voice.active = false;
    }

    const auto finite_sample = std::isfinite(sample) ? sample : 0.0F;
    return finite_sample * voice.volume;
}

auto ProceduralSfxMixer::soft_limit(float sample) noexcept -> float {
    if (!std::isfinite(sample)) {
        return 0.0F;
    }
    const auto magnitude = std::abs(sample);
    if (magnitude <= 0.82F) {
        return sample;
    }

    // Je garde les petits signaux intacts et je compresse seulement la reserve
    // haute, ce qui autorise six mousquets simultanes sans ecretage brutal.
    const auto excess = magnitude - 0.82F;
    const auto limited =
        0.82F +
        0.179F *
            (1.0F - std::exp(-excess * 4.5F));
    return std::copysign(std::min(limited, 0.999F), sample);
}

} // namespace valcraft
