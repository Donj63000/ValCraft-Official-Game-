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
constexpr std::array<GameSfxKind, 6U> kMarlowKinds{{
    GameSfxKind::MarlowWaterSignal,
    GameSfxKind::MarlowSurface,
    GameSfxKind::MarlowSubmerge,
    GameSfxKind::MarlowGrab,
    GameSfxKind::MarlowScreamer,
    GameSfxKind::MarlowDistantSplash,
}};

struct RenderedMarlowEffect {
    std::vector<float> samples{};
    std::size_t active_voice_count = 0U;
};

[[nodiscard]] auto render_marlow_effect(
    GameSfxKind kind,
    std::span<const std::size_t> block_sizes,
    std::uint32_t seed = 0x4D41524CU,
    float pan = 0.0F,
    float attenuation = 1.0F) -> RenderedMarlowEffect {
    // Je recree un mixer par rendu pour isoler strictement la graine, le
    // decoupage des callbacks et les parametres de spatialisation testes.
    ProceduralSfxMixer mixer{kSampleRate};
    mixer.play({
        .kind = kind,
        .volume = 0.82F,
        .pan = pan,
        .attenuation = attenuation,
        .seed = seed,
    });

    RenderedMarlowEffect rendered{};
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
    // Je quantifie un sous-ensemble stable du signal afin de detecter deux
    // effets accidentellement identiques sans figer chaque echantillon ici.
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

} // namespace

TEST_CASE("les signatures audio de Marlow restent contigues en fin d'enumeration") {
    CHECK(
        static_cast<std::uint8_t>(
            GameSfxKind::MarlowWaterSignal) ==
        static_cast<std::uint8_t>(
            GameSfxKind::JackScreamer) +
            1U);
    CHECK(
        static_cast<std::uint8_t>(
            GameSfxKind::MarlowDistantSplash) ==
        static_cast<std::uint8_t>(
            GameSfxKind::MarlowWaterSignal) +
            5U);
}

TEST_CASE("les six sons de Marlow ont une duree valide et le screamer dure exactement 0.85 seconde") {
    for (const auto kind : kMarlowKinds) {
        CAPTURE(static_cast<int>(kind));
        CHECK(
            ProceduralSfxMixer::effect_duration(kind) >
            0.0F);
    }

    CHECK(
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::MarlowScreamer) ==
        doctest::Approx(0.85F).epsilon(1.0e-6));
}

TEST_CASE("les six sons de Marlow sont distincts audibles finis et bornes") {
    auto fingerprints =
        std::array<
            std::uint64_t,
            kMarlowKinds.size()>{};

    for (std::size_t index = 0U;
         index < kMarlowKinds.size();
         ++index) {
        const auto kind = kMarlowKinds[index];
        const auto duration =
            ProceduralSfxMixer::effect_duration(kind);
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
            render_marlow_effect(
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

TEST_CASE("les sons de Marlow sont deterministes pour une meme graine") {
    constexpr std::array<std::size_t, 1U>
        blocks{{20'000U}};

    for (const auto kind : kMarlowKinds) {
        const auto first =
            render_marlow_effect(
                kind,
                blocks,
                0xC0FFEE12U);
        const auto second =
            render_marlow_effect(
                kind,
                blocks,
                0xC0FFEE12U);
        CAPTURE(static_cast<int>(kind));
        CHECK(first.samples == second.samples);
        CHECK(
            first.active_voice_count ==
            second.active_voice_count);
    }
}

TEST_CASE("les sons de Marlow ne dependent pas de la taille du callback SDL") {
    constexpr std::array<std::size_t, 1U>
        contiguous_blocks{{20'000U}};
    constexpr std::array<std::size_t, 6U>
        split_blocks{{
            17U,
            127U,
            511U,
            2'047U,
            4'099U,
            13'199U,
        }};

    for (const auto kind : kMarlowKinds) {
        const auto contiguous =
            render_marlow_effect(
                kind,
                contiguous_blocks);
        const auto split =
            render_marlow_effect(
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

TEST_CASE("les sons de Marlow respectent le panoramique et une attenuation monotone") {
    constexpr std::array<std::size_t, 1U>
        blocks{{20'000U}};

    for (const auto kind : kMarlowKinds) {
        const auto hard_left =
            render_marlow_effect(
                kind,
                blocks,
                0x4D41524CU,
                -1.0F);
        const auto hard_right =
            render_marlow_effect(
                kind,
                blocks,
                0x4D41524CU,
                1.0F);
        const auto near_rendered =
            render_marlow_effect(
                kind,
                blocks,
                0x4D41524CU,
                0.0F,
                0.80F);
        const auto far_rendered =
            render_marlow_effect(
                kind,
                blocks,
                0x4D41524CU,
                0.0F,
                0.25F);
        CAPTURE(static_cast<int>(kind));

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
}

} // namespace valcraft
