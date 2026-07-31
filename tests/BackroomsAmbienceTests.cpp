#include "app/GameMusic.h"
#include "audio/BackroomsAmbience.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace valcraft {

namespace {

auto render_backrooms_ambience(
    std::uint32_t seed,
    float darkness,
    float anomaly) -> std::vector<float> {

    constexpr auto sample_rate = 8'000;
    constexpr auto duration_seconds = 3U;
    auto samples =
        std::vector<float>(
            static_cast<std::size_t>(sample_rate) *
                duration_seconds *
                2U,
            0.0F);

    BackroomsAmbience ambience(sample_rate);
    ambience.set_context({
        .active = true,
        .seed = seed,
        .darkness = darkness,
        .anomaly = anomaly,
    });
    ambience.mix_interleaved(samples, 2U);
    return samples;
}

auto tail_rms(
    const std::vector<float>& samples,
    std::size_t tail_sample_count) -> double {

    const auto count =
        std::min(tail_sample_count, samples.size());
    const auto begin =
        samples.size() - count;
    auto energy = 0.0;
    for (std::size_t index = begin;
         index < samples.size();
         ++index) {
        const auto sample =
            static_cast<double>(samples[index]);
        energy += sample * sample;
    }
    return count == 0U
               ? 0.0
               : std::sqrt(
                     energy /
                     static_cast<double>(count));
}

} // namespace

TEST_CASE("BackRooms music context selects the dedicated ambience") {
    GameMusicContextInput input {
        .has_active_session = true,
        .front_end_visible = false,
        .maritime_gameplay_active = false,
        .backrooms_gameplay_active = true,
        .voyage_motion = 0.0F,
        .danger = 0.0F,
        .world_seed = 424242,
    };

    const auto gameplay =
        make_game_music_context(input);
    CHECK(
        gameplay.scene ==
        ProceduralMusicScene::Backrooms);
    CHECK(gameplay.seed != 0U);

    input.front_end_visible = true;
    const auto menu =
        make_game_music_context(input);
    CHECK(
        menu.scene ==
        ProceduralMusicScene::Classic);

    input.front_end_visible = false;
    input.has_active_session = false;
    const auto no_session =
        make_game_music_context(input);
    CHECK(
        no_session.scene ==
        ProceduralMusicScene::Classic);
}

TEST_CASE("BackRooms ambience is deterministic finite and bounded") {
    const auto first =
        render_backrooms_ambience(
            0xB4C30015U,
            0.72F,
            0.38F);
    const auto second =
        render_backrooms_ambience(
            0xB4C30015U,
            0.72F,
            0.38F);
    const auto other =
        render_backrooms_ambience(
            0xB4C30016U,
            0.72F,
            0.38F);

    REQUIRE(first.size() == second.size());
    REQUIRE(first.size() == other.size());
    CHECK(first == second);
    CHECK(first != other);

    for (const auto sample : first) {
        CHECK(std::isfinite(sample));
        CHECK(sample >= -1.0F);
        CHECK(sample <= 1.0F);
    }

    constexpr auto tail_frames =
        static_cast<std::size_t>(8'000 / 2);
    const auto rms =
        tail_rms(first, tail_frames * 2U);
    CHECK(rms > 0.008);
    CHECK(rms < 0.090);
}

TEST_CASE("BackRooms ambience crossfades without discontinuity") {
    constexpr auto sample_rate = 8'000;
    BackroomsAmbience ambience(sample_rate);
    ambience.set_context({
        .active = true,
        .seed = 1337U,
        .darkness = 0.5F,
        .anomaly = 0.5F,
    });

    auto attack =
        std::vector<float>(
            static_cast<std::size_t>(sample_rate) *
                2U *
                2U,
            0.015F);
    ambience.mix_interleaved(attack, 2U);
    CHECK(ambience.current_mix() > 0.99F);

    auto maximum_delta = 0.0F;
    for (std::size_t index = 2U;
         index < attack.size();
         ++index) {
        maximum_delta =
            std::max(
                maximum_delta,
                std::abs(
                    attack[index] -
                    attack[index - 2U]));
    }
    CHECK(maximum_delta < 0.08F);

    ambience.set_context({
        .active = false,
        .seed = 1337U,
        .darkness = 0.0F,
        .anomaly = 0.0F,
    });
    auto release =
        std::vector<float>(
            static_cast<std::size_t>(sample_rate) *
                2U *
                2U,
            0.015F);
    ambience.mix_interleaved(release, 2U);
    CHECK(ambience.current_mix() < 0.01F);
    CHECK(
        release.back() ==
        doctest::Approx(0.015F)
            .epsilon(0.02));
}

} // namespace valcraft
