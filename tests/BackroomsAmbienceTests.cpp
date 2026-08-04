#include "app/GameMusic.h"
#include "audio/BackroomsAmbience.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace valcraft {

// Je garde cet acces limite aux tests afin de verrouiller l'independance des
// deux machines d'etat sans exposer leurs details dans l'API audio publique.
struct BackroomsAmbienceTestAccess {
    struct ChannelSnapshot {
        std::uint32_t noise_state = 0U;
        float noise_lowpass = 0.0F;
        float noise_bandpass = 0.0F;
        float previous_noise_lowpass = 0.0F;
    };

    [[nodiscard]] static auto channel(
        const BackroomsAmbience& ambience,
        std::size_t index) noexcept -> ChannelSnapshot {
        const auto& state = ambience.channels_[index];
        return {
            state.noise_state,
            state.noise_lowpass,
            state.noise_bandpass,
            state.previous_noise_lowpass,
        };
    }
};

namespace {

auto render_backrooms_ambience(
    std::uint32_t seed,
    float darkness,
    float anomaly,
    int sample_rate = 8'000,
    std::size_t channel_count = 2U,
    std::size_t duration_seconds = 3U) -> std::vector<float> {

    auto samples =
        std::vector<float>(
            static_cast<std::size_t>(sample_rate) *
                duration_seconds *
                channel_count,
            0.0F);

    BackroomsAmbience ambience(sample_rate);
    ambience.set_context({
        .active = true,
        .seed = seed,
        .darkness = darkness,
        .anomaly = anomaly,
    });
    ambience.mix_interleaved(samples, channel_count);
    return samples;
}

auto tail_mean(
    const std::vector<float>& samples,
    std::size_t tail_sample_count) -> double {

    const auto count =
        std::min(tail_sample_count, samples.size());
    const auto begin = samples.size() - count;
    auto sum = 0.0;
    for (std::size_t index = begin;
         index < samples.size();
         ++index) {
        sum += static_cast<double>(samples[index]);
    }
    return count == 0U
               ? 0.0
               : sum / static_cast<double>(count);
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

auto tail_channel_mean(
    const std::vector<float>& samples,
    std::size_t channel_count,
    std::size_t channel,
    std::size_t tail_frame_count) -> double {

    if (channel_count == 0U || channel >= channel_count) {
        return 0.0;
    }
    const auto frame_count = samples.size() / channel_count;
    const auto count = std::min(tail_frame_count, frame_count);
    const auto begin = frame_count - count;
    auto sum = 0.0;
    for (auto frame = begin; frame < frame_count; ++frame) {
        sum += static_cast<double>(
            samples[frame * channel_count + channel]);
    }
    return count == 0U
               ? 0.0
               : sum / static_cast<double>(count);
}

auto stereo_side_band_energy(
    const std::vector<float>& samples,
    int sample_rate,
    std::size_t tail_frame_count) -> double {

    constexpr double kTwoPi = 6.28318530717958647692;
    const auto frame_count = samples.size() / 2U;
    const auto count = std::min(tail_frame_count, frame_count);
    if (count < 2U) {
        return 0.0;
    }
    const auto begin = frame_count - count;
    auto windowed_side = std::vector<double>(count, 0.0);
    auto window_energy = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const auto window =
            0.5 - 0.5 * std::cos(
                kTwoPi * static_cast<double>(index) /
                static_cast<double>(count - 1U));
        const auto frame = begin + index;
        windowed_side[index] =
            (static_cast<double>(samples[frame * 2U]) -
             static_cast<double>(samples[frame * 2U + 1U])) *
            window;
        window_energy += window * window;
    }

    // Je mesure une bande sans les tons de secteur, ventilation, ballast ni
    // leurs trois harmoniques. Le signal lateral y isole le bruit decorrele.
    auto accumulated_energy = 0.0;
    auto bin_count = std::size_t {0U};
    for (auto frequency_hz = 175; frequency_hz <= 225;
         frequency_hz += 2) {
        const auto omega =
            kTwoPi * static_cast<double>(frequency_hz) /
            static_cast<double>(sample_rate);
        const auto coefficient = 2.0 * std::cos(omega);
        auto previous = 0.0;
        auto previous_previous = 0.0;
        for (const auto sample : windowed_side) {
            const auto current =
                sample + coefficient * previous - previous_previous;
            previous_previous = previous;
            previous = current;
        }
        const auto power =
            previous * previous +
            previous_previous * previous_previous -
            coefficient * previous * previous_previous;
        accumulated_energy += std::max(power, 0.0) / window_energy;
        ++bin_count;
    }
    return accumulated_energy / static_cast<double>(bin_count);
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

TEST_CASE("BackRooms mono is the stable downmix of the two stereo channels") {
    constexpr auto sample_rate = 48'000;
    constexpr auto frame_count = 4'096U;
    const BackroomsAmbienceContext context {
        .active = true,
        .seed = 0x53544552U,
        .darkness = 0.61F,
        .anomaly = 0.84F,
    };

    BackroomsAmbience mono_ambience(sample_rate);
    BackroomsAmbience stereo_ambience(sample_rate);
    mono_ambience.set_context(context);
    stereo_ambience.set_context(context);

    auto mono = std::vector<float>(frame_count, 0.0F);
    auto stereo = std::vector<float>(frame_count * 2U, 0.0F);
    mono_ambience.mix_interleaved(mono, 1U);
    stereo_ambience.mix_interleaved(stereo, 2U);

    for (std::size_t frame = 0U;
         frame < frame_count;
         ++frame) {
        const auto expected =
            (stereo[frame * 2U] +
             stereo[frame * 2U + 1U]) *
            0.5F;
        CHECK(std::abs(mono[frame] - expected) <= 1.0e-7F);
    }

    // Je repasse les deux instances en stéréo : leur égalité prouve que le
    // rendu mono a fait avancer les deux RNG et les deux filtres une fois par
    // frame, exactement comme le rendu stéréo.
    auto mono_tail = std::vector<float>(2'048U, 0.0F);
    auto stereo_tail = std::vector<float>(2'048U, 0.0F);
    mono_ambience.mix_interleaved(mono_tail, 2U);
    stereo_ambience.mix_interleaved(stereo_tail, 2U);
    CHECK(mono_tail == stereo_tail);
}

TEST_CASE("BackRooms stereo advances two independent noise filter states") {
    BackroomsAmbience ambience(48'000);
    ambience.set_context({
        .active = true,
        .seed = 0x494E4450U,
        .darkness = 0.63F,
        .anomaly = 0.91F,
    });
    const auto left_before =
        BackroomsAmbienceTestAccess::channel(ambience, 0U);
    const auto right_before =
        BackroomsAmbienceTestAccess::channel(ambience, 1U);

    auto stereo = std::vector<float>(8'192U, 0.0F);
    ambience.mix_interleaved(stereo, 2U);

    const auto left_after =
        BackroomsAmbienceTestAccess::channel(ambience, 0U);
    const auto right_after =
        BackroomsAmbienceTestAccess::channel(ambience, 1U);
    CHECK(left_before.noise_state != right_before.noise_state);
    CHECK(left_after.noise_state != left_before.noise_state);
    CHECK(right_after.noise_state != right_before.noise_state);
    CHECK(left_after.noise_state != right_after.noise_state);
    CHECK(left_after.noise_lowpass != 0.0F);
    CHECK(right_after.noise_lowpass != 0.0F);
    CHECK(left_after.noise_bandpass != 0.0F);
    CHECK(right_after.noise_bandpass != 0.0F);
    CHECK(left_after.noise_lowpass != right_after.noise_lowpass);
    CHECK(left_after.noise_bandpass != right_after.noise_bandpass);
}

TEST_CASE("BackRooms ambience rejects unsupported or incomplete channel layouts") {
    BackroomsAmbience ambience(48'000);
    ambience.set_context({
        .active = true,
        .seed = 0x464D5421U,
        .darkness = 0.5F,
        .anomaly = 0.5F,
    });

    auto surround = std::vector<float>(12U, 0.25F);
    const auto unchanged_surround = surround;
    ambience.mix_interleaved(surround, 3U);
    CHECK(surround == unchanged_surround);
    CHECK(ambience.current_mix() == doctest::Approx(0.0F));

    auto incomplete_stereo = std::vector<float>(5U, -0.25F);
    const auto unchanged_incomplete = incomplete_stereo;
    ambience.mix_interleaved(incomplete_stereo, 2U);
    CHECK(incomplete_stereo == unchanged_incomplete);
    CHECK(ambience.current_mix() == doctest::Approx(0.0F));
}

TEST_CASE("BackRooms ambience is block deterministic") {
    constexpr auto sample_rate = 48'000;
    constexpr auto frame_count = 8'192U;
    const BackroomsAmbienceContext context {
        .active = true,
        .seed = 0x424C4F43U,
        .darkness = 0.77F,
        .anomaly = 0.69F,
    };

    BackroomsAmbience contiguous(sample_rate);
    BackroomsAmbience partitioned(sample_rate);
    contiguous.set_context(context);
    partitioned.set_context(context);
    auto whole = std::vector<float>(frame_count * 2U, 0.0F);
    auto chunks = std::vector<float>(frame_count * 2U, 0.0F);

    contiguous.mix_interleaved(whole, 2U);
    auto chunk_span = std::span<float> {chunks};
    partitioned.mix_interleaved(
        chunk_span.first(2'222U),
        2U);
    partitioned.mix_interleaved(
        chunk_span.subspan(2'222U, 6'666U),
        2U);
    partitioned.mix_interleaved(
        chunk_span.subspan(8'888U),
        2U);
    CHECK(chunks == whole);
}

TEST_CASE("BackRooms ambience keeps tonal level channel DC and noise spectrum across sample rates") {
    constexpr std::array sample_rates {44'100, 48'000, 96'000};
    std::array<double, sample_rates.size()> rms_values {};
    std::array<double, sample_rates.size()> side_band_energies {};

    for (std::size_t index = 0U;
         index < sample_rates.size();
         ++index) {
        const auto sample_rate = sample_rates[index];
        const auto samples = render_backrooms_ambience(
            0x52415445U,
            0.74F,
            0.92F,
            sample_rate,
            2U,
            4U);
        const auto tail_frame_count =
            static_cast<std::size_t>(sample_rate) * 2U;
        const auto tail_sample_count = tail_frame_count * 2U;
        rms_values[index] =
            tail_rms(samples, tail_sample_count);
        CHECK(std::abs(tail_mean(samples, tail_sample_count)) < 0.0015);
        CHECK(std::abs(tail_channel_mean(
                  samples, 2U, 0U, tail_frame_count)) < 0.0015);
        CHECK(std::abs(tail_channel_mean(
                  samples, 2U, 1U, tail_frame_count)) < 0.0015);
        side_band_energies[index] = stereo_side_band_energy(
            samples,
            sample_rate,
            tail_frame_count);
        // Je rends les deux flux aleatoires observables separement : un etat
        // partage ferait presque disparaitre cette energie laterale.
        CHECK(side_band_energies[index] > 5.0e-11);
    }

    const auto [minimum, maximum] =
        std::minmax_element(
            rms_values.begin(),
            rms_values.end());
    REQUIRE(minimum != rms_values.end());
    REQUIRE(*minimum > 0.0);
    CHECK(*maximum / *minimum < 1.08);

    const auto [minimum_side, maximum_side] =
        std::minmax_element(
            side_band_energies.begin(),
            side_band_energies.end());
    REQUIRE(minimum_side != side_band_energies.end());
    REQUIRE(*minimum_side > 0.0);
    CHECK(*maximum_side / *minimum_side < 2.5);
}

} // namespace valcraft
