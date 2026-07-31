#include "audio/ProceduralSfx.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace valcraft {

namespace {

constexpr auto kSampleRate = 12'000;
constexpr std::array<GameSfxKind, 5U> kJackKinds{{
    GameSfxKind::JackBootStep,
    GameSfxKind::JackPegStep,
    GameSfxKind::JackNotice,
    GameSfxKind::JackChase,
    GameSfxKind::JackScreamer,
}};

struct RenderedJackEffect {
    std::vector<float> samples{};
    std::size_t active_voice_count = 0U;
};

[[nodiscard]] auto render_jack_effect(
    GameSfxKind kind,
    std::span<const std::size_t> block_sizes,
    float pan = 0.0F,
    float attenuation = 1.0F) -> RenderedJackEffect {
    ProceduralSfxMixer mixer{kSampleRate};
    mixer.play({
        .kind = kind,
        .volume = 0.82F,
        .pan = pan,
        .attenuation = attenuation,
        .seed = 0x4A41434BU,
    });

    RenderedJackEffect rendered{};
    for (const auto frame_count : block_sizes) {
        auto block =
            std::vector<float>(
                frame_count * 2U,
                0.0F);
        mixer.mix_interleaved(block, 2U);
        rendered.samples.insert(
            rendered.samples.end(),
            block.begin(),
            block.end());
    }
    rendered.active_voice_count =
        mixer.active_voice_count();
    return rendered;
}

[[nodiscard]] auto sample_fingerprint(
    std::span<const float> samples) noexcept
    -> std::uint64_t {
    auto hash =
        std::uint64_t{1469598103934665603ULL};
    for (std::size_t index = 0U;
         index < samples.size();
         index += 13U) {
        const auto quantized =
            static_cast<std::int32_t>(
                std::lround(
                    samples[index] *
                    1'000'000.0F));
        hash ^=
            static_cast<std::uint32_t>(
                quantized);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] auto channel_energy(
    std::span<const float> samples,
    std::size_t channel) noexcept -> double {
    auto energy = 0.0;
    for (auto index = channel;
         index < samples.size();
         index += 2U) {
        const auto sample =
            static_cast<double>(
                samples[index]);
        energy += sample * sample;
    }
    return energy;
}

[[nodiscard]] auto maximum_amplitude(
    std::span<const float> samples) noexcept -> float {
    auto peak = 0.0F;
    for (const auto sample : samples) {
        peak =
            std::max(
                peak,
                std::abs(sample));
    }
    return peak;
}

[[nodiscard]] auto high_frequency_activity(
    std::span<const float> samples) noexcept -> double {
    auto activity = 0.0;
    for (std::size_t index = 2U;
         index < samples.size();
         index += 2U) {
        activity +=
            std::abs(
                static_cast<double>(
                    samples[index] -
                    samples[index - 2U]));
    }
    return activity;
}

} // namespace

TEST_CASE("les signatures audio de Jack restent en fin d'enumeration") {
    CHECK(
        static_cast<std::uint8_t>(
            GameSfxKind::JackBootStep) ==
        static_cast<std::uint8_t>(
            GameSfxKind::SeaLeviathan) +
            1U);
    CHECK(
        static_cast<std::uint8_t>(
            GameSfxKind::JackPegStep) ==
        static_cast<std::uint8_t>(
            GameSfxKind::JackBootStep) +
            1U);
    CHECK(
        static_cast<std::uint8_t>(
            GameSfxKind::JackScreamer) ==
        static_cast<std::uint8_t>(
            GameSfxKind::JackBootStep) +
            4U);
}

TEST_CASE("les cinq sons de Jack sont distincts audibles finis et bornes") {
    auto fingerprints =
        std::array<
            std::uint64_t,
            kJackKinds.size()>{};

    for (std::size_t index = 0U;
         index < kJackKinds.size();
         ++index) {
        const auto kind = kJackKinds[index];
        const auto duration =
            ProceduralSfxMixer::
                effect_duration(kind);
        const auto frame_count =
            static_cast<std::size_t>(
                std::ceil(
                    duration *
                    static_cast<float>(
                        kSampleRate))) +
            64U;
        const std::array<std::size_t, 1U>
            blocks{{frame_count}};
        const auto rendered =
            render_jack_effect(
                kind,
                blocks);
        CAPTURE(static_cast<int>(kind));
        REQUIRE_FALSE(rendered.samples.empty());
        CHECK(
            rendered.active_voice_count ==
            0U);
        CHECK(
            std::all_of(
                rendered.samples.begin(),
                rendered.samples.end(),
                [](float sample) noexcept {
                    return
                        std::isfinite(sample) &&
                        sample > -1.0F &&
                        sample < 1.0F;
                }));
        CHECK(
            maximum_amplitude(
                rendered.samples) >
            0.025F);
        fingerprints[index] =
            sample_fingerprint(
                rendered.samples);
    }

    std::sort(
        fingerprints.begin(),
        fingerprints.end());
    CHECK(
        std::adjacent_find(
            fingerprints.begin(),
            fingerprints.end()) ==
        fingerprints.end());
}

TEST_CASE("les sons de Jack ne dependent pas de la taille du callback SDL") {
    constexpr std::array<std::size_t, 1U>
        contiguous_blocks{{20'000U}};
    constexpr std::array<std::size_t, 5U>
        split_blocks{{
            23U,
            251U,
            997U,
            3'001U,
            15'728U,
        }};

    for (const auto kind : kJackKinds) {
        const auto contiguous =
            render_jack_effect(
                kind,
                contiguous_blocks);
        const auto split =
            render_jack_effect(
                kind,
                split_blocks);
        CAPTURE(static_cast<int>(kind));
        REQUIRE(
            contiguous.samples.size() ==
            split.samples.size());
        CHECK(
            contiguous.samples ==
            split.samples);
        CHECK(
            contiguous.active_voice_count ==
            split.active_voice_count);
    }
}

TEST_CASE("la jambe de bois est spatialisee et son attenuation reste monotone") {
    constexpr std::array<std::size_t, 1U>
        blocks{{6'240U}};
    const auto hard_left =
        render_jack_effect(
            GameSfxKind::JackPegStep,
            blocks,
            -1.0F);
    const auto hard_right =
        render_jack_effect(
            GameSfxKind::JackPegStep,
            blocks,
            1.0F);
    const auto near_rendered =
        render_jack_effect(
            GameSfxKind::JackPegStep,
            blocks,
            0.0F,
            0.80F);
    const auto far_rendered =
        render_jack_effect(
            GameSfxKind::JackPegStep,
            blocks,
            0.0F,
            0.25F);

    CHECK(
        channel_energy(
            hard_left.samples,
            0U) >
        channel_energy(
            hard_left.samples,
            1U) *
            1'000.0);
    CHECK(
        channel_energy(
            hard_right.samples,
            1U) >
        channel_energy(
            hard_right.samples,
            0U) *
            1'000.0);
    CHECK(
        channel_energy(
            near_rendered.samples,
            0U) >
        channel_energy(
            far_rendered.samples,
            0U) *
            6.0);
}

TEST_CASE("le screamer est plus brutal et plus aigu que les pas") {
    constexpr std::array<std::size_t, 1U>
        blocks{{14'400U}};
    const auto boot =
        render_jack_effect(
            GameSfxKind::JackBootStep,
            blocks);
    const auto screamer =
        render_jack_effect(
            GameSfxKind::JackScreamer,
            blocks);

    CHECK(
        maximum_amplitude(
            screamer.samples) >
        maximum_amplitude(
            boot.samples));
    CHECK(
        high_frequency_activity(
            screamer.samples) >
        high_frequency_activity(
            boot.samples) *
            2.0);
    CHECK(
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::JackBootStep) <
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::JackPegStep));
    CHECK(
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::JackNotice) <
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::JackScreamer));
    CHECK(
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::JackScreamer) <
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::JackChase));
}

} // namespace valcraft
