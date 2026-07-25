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

} // namespace valcraft
