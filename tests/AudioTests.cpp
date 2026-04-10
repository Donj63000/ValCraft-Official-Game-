#include "audio/ProceduralMusic.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <span>
#include <vector>

namespace valcraft {

namespace {

auto rms_level(const std::vector<float>& samples) -> float {
    if (samples.empty()) {
        return 0.0F;
    }

    double energy = 0.0;
    for (const auto sample : samples) {
        energy += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return static_cast<float>(std::sqrt(energy / static_cast<double>(samples.size())));
}

auto mean_absolute_difference(const std::vector<float>& lhs, const std::vector<float>& rhs) -> float {
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

auto max_absolute_difference(const std::vector<float>& lhs, const std::vector<float>& rhs) -> float {
    const auto sample_count = std::min(lhs.size(), rhs.size());
    float maximum = 0.0F;
    for (std::size_t index = 0; index < sample_count; ++index) {
        maximum = std::max(maximum, std::abs(lhs[index] - rhs[index]));
    }
    return maximum;
}

} // namespace

TEST_CASE("procedural music keeps a slow discreet piano mood by day and by night") {
    ProceduralMusicComposer composer {};

    const auto day_environment = EnvironmentClock::compute_state(12.0F);
    const auto day_cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    composer.set_environment(day_environment, day_cycle, true, false);
    const auto day_snapshot = composer.mood_snapshot();

    CHECK(day_snapshot.day_presence > 0.80F);
    CHECK(day_snapshot.night_presence < 0.25F);
    CHECK(day_snapshot.tension < 0.15F);
    CHECK(day_snapshot.master_gain < 0.15F);
    CHECK(day_snapshot.brightness > 0.40F);
    CHECK(day_snapshot.sustain_seconds > 2.0F);

    const auto night_environment = EnvironmentClock::compute_state(23.0F);
    const auto night_cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    composer.set_environment(night_environment, night_cycle, true, false);
    const auto night_snapshot = composer.mood_snapshot();

    CHECK(night_snapshot.night_presence > 0.80F);
    CHECK(night_snapshot.day_presence < 0.20F);
    CHECK(night_snapshot.tension < 0.22F);
    CHECK(night_snapshot.brightness < day_snapshot.brightness);
    CHECK(night_snapshot.sustain_seconds > day_snapshot.sustain_seconds);
    CHECK(night_snapshot.beat_hz < day_snapshot.beat_hz);
}

TEST_CASE("procedural music renderer stays soft bounded and distinct with piano melodies") {
    ProceduralMusicComposer day_composer {};
    ProceduralMusicComposer night_composer {};

    std::vector<float> day_samples(240000U, 0.0F);
    std::vector<float> night_samples(240000U, 0.0F);

    const auto day_environment = EnvironmentClock::compute_state(10.5F);
    const auto day_cycle = EnvironmentClock::classify_creature_cycle(10.5F);
    day_composer.set_environment(day_environment, day_cycle, true, false);
    day_composer.render_interleaved(day_samples);

    const auto night_environment = EnvironmentClock::compute_state(22.0F);
    const auto night_cycle = EnvironmentClock::classify_creature_cycle(22.0F);
    night_composer.set_environment(night_environment, night_cycle, true, false);
    night_composer.render_interleaved(night_samples);

    CHECK(rms_level(day_samples) > 0.001F);
    CHECK(rms_level(night_samples) > 0.001F);
    CHECK(rms_level(day_samples) < 0.08F);
    CHECK(rms_level(night_samples) < 0.08F);
    CHECK(mean_absolute_difference(day_samples, night_samples) > 0.002F);
    CHECK(std::all_of(day_samples.begin(), day_samples.end(), [](float sample) {
        return std::abs(sample) <= 1.0F;
    }));
    CHECK(std::all_of(night_samples.begin(), night_samples.end(), [](float sample) {
        return std::abs(sample) <= 1.0F;
    }));
}

TEST_CASE("procedural music stays continuous when rendered in callback-sized chunks") {
    ProceduralMusicComposer contiguous_composer {};
    ProceduralMusicComposer chunked_composer {};

    const auto environment = EnvironmentClock::compute_state(21.5F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(21.5F);
    contiguous_composer.set_environment(environment, cycle, true, false);
    chunked_composer.set_environment(environment, cycle, true, false);

    std::vector<float> contiguous_samples(262144U, 0.0F);
    std::vector<float> chunked_samples(262144U, 0.0F);
    contiguous_composer.render_interleaved(contiguous_samples);

    constexpr std::array<std::size_t, 5> kChunkFrames {257U, 384U, 511U, 733U, 1024U};
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

    CHECK(mean_absolute_difference(contiguous_samples, chunked_samples) < 1.0e-6F);
    CHECK(max_absolute_difference(contiguous_samples, chunked_samples) < 1.0e-5F);
}

} // namespace valcraft
