#include "audio/LeviathanProceduralAudio.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto sample_fingerprint(
    const std::vector<float>& samples) noexcept -> std::uint64_t {
    auto hash = std::uint64_t {1469598103934665603ULL};
    for (std::size_t index = 0U;
         index < samples.size();
         index += 37U) {
        const auto quantized = static_cast<std::int32_t>(
            std::lround(samples[index] * 1'000'000.0F));
        hash ^= static_cast<std::uint32_t>(quantized);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] auto samples_are_bounded(
    const std::vector<float>& samples) noexcept -> bool {
    return std::all_of(
        samples.begin(), samples.end(),
        [](float sample) noexcept {
            return std::isfinite(sample) &&
                   sample >= -1.0F &&
                   sample <= 1.0F;
        });
}

} // namespace

TEST_CASE("chaque signal de l echine produit une recette valide") {
    for (std::uint8_t value = 0U;
         value <= static_cast<std::uint8_t>(
             LeviathanAudioCue::ChainBreak);
         ++value) {
        LeviathanAudioRequest request {};
        request.cue = static_cast<LeviathanAudioCue>(value);
        request.seed = 17U;
        request.variant = 3U;
        const auto recipe =
            build_leviathan_audio_recipe(request);
        CAPTURE(static_cast<int>(value));
        CHECK(leviathan_audio_recipe_is_valid(recipe));
        CHECK(recipe.layer_count >= 1U);
        CHECK(recipe.layer_count <=
              kMaximumLeviathanAudioLayers);
        CHECK(recipe.duration_seconds > 0.0F);
        CHECK(recipe.duration_seconds <= 3.0F);
        CHECK(recipe.resolved_variant == 3U);
    }
}

TEST_CASE("la synthese est deterministe bornee et sans entree sortie") {
    LeviathanAudioRequest request {};
    request.cue = LeviathanAudioCue::ChargedExecution;
    request.awakening = LegendaryWeaponAwakening::Awakened;
    request.seed = 0x51A7U;
    request.variant = 2U;
    const auto recipe = build_leviathan_audio_recipe(request);
    const auto first = synthesize_leviathan_audio(recipe, 16'000);
    const auto second = synthesize_leviathan_audio(recipe, 16'000);

    REQUIRE_FALSE(first.empty());
    CHECK(first == second);
    CHECK(samples_are_bounded(first));
    CHECK(
        first.size() ==
        static_cast<std::size_t>(
            std::ceil(recipe.duration_seconds * 16'000.0F)));
    CHECK(std::any_of(
        first.begin(), first.end(),
        [](float sample) {
            return std::abs(sample) > 0.02F;
        }));
}

TEST_CASE("les six materiaux d impact ont des timbres distincts") {
    auto fingerprints = std::array<std::uint64_t, 6U> {};
    for (std::uint8_t value = 0U;
         value < fingerprints.size();
         ++value) {
        LeviathanAudioRequest request {};
        request.cue = LeviathanAudioCue::Impact;
        request.impact_material =
            static_cast<LeviathanImpactAudioMaterial>(value);
        request.seed = 42U;
        const auto recipe =
            build_leviathan_audio_recipe(request);
        const auto samples =
            synthesize_leviathan_audio(recipe, 12'000);
        CAPTURE(static_cast<int>(value));
        REQUIRE(samples_are_bounded(samples));
        fingerprints[value] = sample_fingerprint(samples);
    }
    std::sort(fingerprints.begin(), fingerprints.end());
    CHECK(
        std::adjacent_find(
            fingerprints.begin(), fingerprints.end()) ==
        fingerprints.end());
}

TEST_CASE("les variantes changent subtilement le son sans changer le budget") {
    auto fingerprints = std::array<std::uint64_t, 4U> {};
    auto lengths = std::array<std::size_t, 4U> {};
    for (std::uint8_t variant = 0U; variant < 4U; ++variant) {
        LeviathanAudioRequest request {};
        request.cue = LeviathanAudioCue::FirstSweep;
        request.variant = variant;
        request.seed = 99U;
        const auto recipe =
            build_leviathan_audio_recipe(request);
        const auto samples =
            synthesize_leviathan_audio(recipe, 12'000);
        fingerprints[variant] = sample_fingerprint(samples);
        lengths[variant] = samples.size();
    }
    std::sort(fingerprints.begin(), fingerprints.end());
    CHECK(
        std::adjacent_find(
            fingerprints.begin(), fingerprints.end()) ==
        fingerprints.end());
    CHECK(std::all_of(
        lengths.begin(), lengths.end(),
        [first = lengths.front()](std::size_t length) {
            return length == first;
        }));
}

TEST_CASE("la foule les cors et les tambours restent legers et distincts") {
    auto fingerprints = std::array<std::uint64_t, 4U> {};
    constexpr std::array<LeviathanAudioCue, 4U> cues {{
        LeviathanAudioCue::CrowdMurmur,
        LeviathanAudioCue::CrowdCheer,
        LeviathanAudioCue::ArenaHorn,
        LeviathanAudioCue::ArenaDrum,
    }};
    for (std::size_t index = 0U; index < cues.size(); ++index) {
        LeviathanAudioRequest request {};
        request.cue = cues[index];
        request.seed = 8U;
        const auto recipe =
            build_leviathan_audio_recipe(request);
        CHECK(recipe.layer_count <= 3U);
        fingerprints[index] = sample_fingerprint(
            synthesize_leviathan_audio(recipe, 8'000));
    }
    std::sort(fingerprints.begin(), fingerprints.end());
    CHECK(
        std::adjacent_find(
            fingerprints.begin(), fingerprints.end()) ==
        fingerprints.end());
}

TEST_CASE("les requetes mal formees sont assainies et les recettes invalides refusees") {
    LeviathanAudioRequest request {};
    request.cue = LeviathanAudioCue::Impact;
    request.intensity =
        std::numeric_limits<float>::quiet_NaN();
    request.seed = 0U;
    request.variant = 255U;
    request.awakening =
        static_cast<LegendaryWeaponAwakening>(255U);
    request.impact_material =
        static_cast<LeviathanImpactAudioMaterial>(255U);
    const auto safe = build_leviathan_audio_recipe(request);
    CHECK(leviathan_audio_recipe_is_valid(safe));
    CHECK(safe.seed == 1U);
    CHECK(safe.resolved_variant == 3U);
    CHECK_FALSE(synthesize_leviathan_audio(safe, 1).empty());

    auto invalid = safe;
    invalid.duration_seconds =
        std::numeric_limits<float>::infinity();
    CHECK_FALSE(leviathan_audio_recipe_is_valid(invalid));
    CHECK(synthesize_leviathan_audio(invalid).empty());

    invalid = safe;
    invalid.layer_count =
        kMaximumLeviathanAudioLayers + 1U;
    CHECK_FALSE(leviathan_audio_recipe_is_valid(invalid));

    request.cue = static_cast<LeviathanAudioCue>(255U);
    CHECK(leviathan_audio_recipe_is_valid(
        build_leviathan_audio_recipe(request)));
}

} // namespace valcraft
