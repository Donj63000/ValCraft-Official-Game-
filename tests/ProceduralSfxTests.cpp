#include "audio/ProceduralSfx.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace valcraft {

namespace {

constexpr std::array<GameSfxKind, 8U> kLegendaryKinds {{
    GameSfxKind::HeavySwing,
    GameSfxKind::BoneImpact,
    GameSfxKind::MetalImpact,
    GameSfxKind::PerfectGuard,
    GameSfxKind::ChainBreak,
    GameSfxKind::ColossusRoar,
    GameSfxKind::Crowd,
    GameSfxKind::SeaLeviathan,
}};

auto render_musket(std::span<const std::size_t> block_sizes) -> std::vector<float> {
    ProceduralSfxMixer mixer {48'000};
    mixer.play({
        .kind = GameSfxKind::MusketShot,
        .volume = 0.95F,
        .pan = -0.30F,
        .attenuation = 0.80F,
        .seed = 0x1234ABCDU,
    });

    std::vector<float> rendered {};
    for (const auto frames : block_sizes) {
        auto block = std::vector<float>(frames * 2U, 0.0F);
        mixer.mix_interleaved(block, 2U);
        rendered.insert(rendered.end(), block.begin(), block.end());
    }
    return rendered;
}

auto render_legendary(GameSfxKind kind,
                      std::span<const std::size_t> block_sizes,
                      float pan = 0.0F,
                      float attenuation = 1.0F) -> std::vector<float> {
    ProceduralSfxMixer mixer {48'000};
    mixer.play({
        .kind = kind,
        .volume = 0.72F,
        .pan = pan,
        .attenuation = attenuation,
        .seed = 0x4C455649U,
    });

    std::vector<float> rendered {};
    for (const auto frames : block_sizes) {
        auto block = std::vector<float>(frames * 2U, 0.0F);
        mixer.mix_interleaved(block, 2U);
        rendered.insert(rendered.end(), block.begin(), block.end());
    }
    return rendered;
}

[[nodiscard]] auto sample_fingerprint(
    std::span<const float> samples) noexcept -> std::uint64_t {
    auto hash = std::uint64_t {1469598103934665603ULL};
    for (std::size_t index = 0U;
         index < samples.size();
         index += 17U) {
        const auto quantized = static_cast<std::int32_t>(
            std::lround(samples[index] * 1'000'000.0F));
        hash ^= static_cast<std::uint32_t>(quantized);
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
        const auto sample = static_cast<double>(samples[index]);
        energy += sample * sample;
    }
    return energy;
}

} // namespace

TEST_CASE("la synthese du mousquet reste deterministe quelle que soit la taille des callbacks") {
    constexpr std::array<std::size_t, 1U> one_block {4'096U};
    constexpr std::array<std::size_t, 4U> split_blocks {
        127U,
        509U,
        1'024U,
        2'436U,
    };
    const auto contiguous = render_musket(one_block);
    const auto split = render_musket(split_blocks);

    REQUIRE(contiguous.size() == split.size());
    CHECK(contiguous == split);
    CHECK(std::any_of(
        contiguous.begin(),
        contiguous.end(),
        [](float sample) noexcept {
            return std::abs(sample) > 0.20F;
        }));
}

TEST_CASE("six mousquets simultanes restent spatialises et limites sans valeur invalide") {
    ProceduralSfxMixer mixer {48'000};
    for (std::uint32_t index = 0U; index < 6U; ++index) {
        mixer.play({
            .kind = GameSfxKind::MusketShot,
            .volume = 1.0F,
            .pan = -0.85F + static_cast<float>(index) * 0.34F,
            .attenuation = 1.0F,
            .seed = 100U + index,
        });
    }
    CHECK(mixer.active_voice_count() == 6U);

    auto samples = std::vector<float>(48'000U * 2U, 0.0F);
    mixer.mix_interleaved(samples, 2U);

    auto peak = 0.0F;
    for (const auto sample : samples) {
        CHECK(std::isfinite(sample));
        CHECK(sample < 1.0F);
        CHECK(sample > -1.0F);
        peak = std::max(peak, std::abs(sample));
    }
    CHECK(peak > 0.80F);
    CHECK(mixer.active_voice_count() > 0U);
}

TEST_CASE("attenuation nulle ne reserve aucune voix") {
    ProceduralSfxMixer mixer {};
    mixer.play({
        .kind = GameSfxKind::MusketShot,
        .volume = 1.0F,
        .attenuation = 0.0F,
    });
    CHECK(mixer.active_voice_count() == 0U);
}

TEST_CASE("les huit signatures legendaires sont audibles bornees et distinctes") {
    constexpr std::array<std::size_t, 1U> block_sizes {16'384U};
    auto fingerprints = std::array<std::uint64_t, kLegendaryKinds.size()> {};

    for (std::size_t index = 0U;
         index < kLegendaryKinds.size();
         ++index) {
        const auto samples =
            render_legendary(kLegendaryKinds[index], block_sizes);
        CAPTURE(index);
        REQUIRE_FALSE(samples.empty());
        CHECK(std::all_of(
            samples.begin(),
            samples.end(),
            [](float sample) noexcept {
                return std::isfinite(sample) &&
                       sample > -1.0F &&
                       sample < 1.0F;
            }));
        CHECK(std::any_of(
            samples.begin(),
            samples.end(),
            [](float sample) noexcept {
                return std::abs(sample) > 0.025F;
            }));
        fingerprints[index] = sample_fingerprint(samples);
    }

    std::sort(fingerprints.begin(), fingerprints.end());
    CHECK(
        std::adjacent_find(
            fingerprints.begin(),
            fingerprints.end()) == fingerprints.end());
}

TEST_CASE("les sons legendaires restent deterministes entre les callbacks SDL") {
    constexpr std::array<std::size_t, 1U> contiguous_blocks {8'192U};
    constexpr std::array<std::size_t, 5U> split_blocks {
        31U,
        257U,
        1'009U,
        2'047U,
        4'848U,
    };

    for (const auto kind : kLegendaryKinds) {
        const auto contiguous =
            render_legendary(kind, contiguous_blocks);
        const auto split =
            render_legendary(kind, split_blocks);
        CAPTURE(static_cast<int>(kind));
        REQUIRE(contiguous.size() == split.size());
        CHECK(contiguous == split);
    }
}

TEST_CASE("la spatialisation et l attenuation legendaires sont respectees") {
    constexpr std::array<std::size_t, 1U> block_sizes {8'192U};
    const auto hard_left = render_legendary(
        GameSfxKind::HeavySwing,
        block_sizes,
        -1.0F);
    const auto hard_right = render_legendary(
        GameSfxKind::HeavySwing,
        block_sizes,
        1.0F);
    const auto full = render_legendary(
        GameSfxKind::BoneImpact,
        block_sizes,
        0.0F,
        0.50F);
    const auto attenuated = render_legendary(
        GameSfxKind::BoneImpact,
        block_sizes,
        0.0F,
        0.20F);

    CHECK(channel_energy(hard_left, 0U) >
          channel_energy(hard_left, 1U) * 1'000.0);
    CHECK(channel_energy(hard_right, 1U) >
          channel_energy(hard_right, 0U) * 1'000.0);
    CHECK(channel_energy(full, 0U) >
          channel_energy(attenuated, 0U) * 4.0);
}

TEST_CASE("le budget de voix reste fixe sous une rafale legendaire") {
    ProceduralSfxMixer mixer {48'000};
    for (std::uint32_t index = 0U; index < 96U; ++index) {
        mixer.play({
            .kind =
                kLegendaryKinds[
                    index %
                    static_cast<std::uint32_t>(
                        kLegendaryKinds.size())],
            .volume = 0.80F,
            .pan =
                -1.0F +
                static_cast<float>(index % 9U) * 0.25F,
            .attenuation = 1.0F,
            .seed = index + 1U,
        });
    }

    CHECK(
        mixer.active_voice_count() ==
        ProceduralSfxMixer::maximum_voice_count());
    auto output = std::array<float, 2'048U> {};
    mixer.mix_interleaved(output, 2U);
    CHECK(std::all_of(
        output.begin(),
        output.end(),
        [](float sample) noexcept {
            return std::isfinite(sample) &&
                   sample > -1.0F &&
                   sample < 1.0F;
        }));
}

TEST_CASE("les durees legendaires couvrent impacts ambiances et leviathan") {
    CHECK(
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::BoneImpact) <
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::HeavySwing));
    CHECK(
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::HeavySwing) <
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::ChainBreak));
    CHECK(
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::ChainBreak) <
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::ColossusRoar));
    CHECK(
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::ColossusRoar) <
        ProceduralSfxMixer::effect_duration(
            GameSfxKind::SeaLeviathan));
}

} // namespace valcraft
