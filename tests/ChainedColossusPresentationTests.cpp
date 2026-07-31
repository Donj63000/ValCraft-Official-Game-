#include "render/creatures/ChainedColossusPresentation.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto count_visual_part(
    const std::vector<ChainedColossusPartInstance>& parts,
    ColossusVisualPart visual_part) noexcept -> std::size_t {
    return static_cast<std::size_t>(std::count_if(
        parts.begin(), parts.end(),
        [visual_part](
            const ChainedColossusPartInstance& part) noexcept {
            return part.visual_part == visual_part;
        }));
}

[[nodiscard]] auto all_colossus_parts_are_finite(
    const std::vector<ChainedColossusPartInstance>& parts) noexcept
    -> bool {
    return std::all_of(
        parts.begin(), parts.end(),
        [](const ChainedColossusPartInstance& part) noexcept {
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    if (!std::isfinite(
                            part.geometry.transform[column][row])) {
                        return false;
                    }
                }
            }
            return std::isfinite(part.geometry.sky_light) &&
                   std::isfinite(part.geometry.block_light);
        });
}

[[nodiscard]] auto traces_match(
    std::span<const ColossusBloodTrace> left,
    std::span<const ColossusBloodTrace> right) noexcept -> bool {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (left[index].position != right[index].position ||
            left[index].normal != right[index].normal ||
            left[index].radius != right[index].radius ||
            left[index].opacity != right[index].opacity ||
            left[index].seed != right[index].seed ||
            left[index].muted != right[index].muted) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("le colosse procedural mesure entre quatre et cinq blocs") {
    ChainedColossusVisualInput input {};
    input.phase = ChainedColossusPhase::PhaseOne;
    const auto parts = build_chained_colossus_parts(input);
    const auto bounds = chained_colossus_visual_bounds(parts);
    const auto height = bounds.maximum.y - bounds.minimum.y;

    REQUIRE_FALSE(parts.empty());
    CHECK(height == doctest::Approx(kColossusVisualHeightBlocks)
                        .epsilon(0.001));
    CHECK(height >= 4.0F);
    CHECK(height <= 5.0F);
    CHECK((bounds.maximum.x - bounds.minimum.x) > 2.0F);
    CHECK(count_visual_part(parts, ColossusVisualPart::Torso) >= 2U);
    CHECK(count_visual_part(parts, ColossusVisualPart::Armor) >= 5U);
    CHECK(all_colossus_parts_are_finite(parts));
}

TEST_CASE("les armures brisees disparaissent et les plaies restent locales") {
    ChainedColossusVisualInput input {};
    input.phase = ChainedColossusPhase::PhaseTwo;
    input.armor_states.fill(ColossusArmorState::Intact);
    input.wounded_zones_mask =
        (std::uint32_t {1U} << kColossusLeftArmZone);
    const auto intact = build_chained_colossus_parts(input);

    input.armor_states[2U] = ColossusArmorState::Broken;
    const auto broken = build_chained_colossus_parts(input);
    CHECK(
        count_visual_part(broken, ColossusVisualPart::Armor) + 1U ==
        count_visual_part(intact, ColossusVisualPart::Armor));
    CHECK(std::all_of(
        broken.begin(), broken.end(),
        [](const ChainedColossusPartInstance& part) {
            return !part.wound_overlay ||
                   part.zone_id == kColossusLeftArmZone;
        }));
}

TEST_CASE("un membre sectionne est masque dans tous les modes de gore") {
    ChainedColossusVisualInput input {};
    input.phase = ChainedColossusPhase::PhaseThree;
    input.hidden_parts_mask = static_cast<std::uint32_t>(
        ColossusHiddenPart::LeftArm |
        ColossusHiddenPart::Horn);
    input.wounded_zones_mask =
        (std::uint32_t {1U} << kColossusLeftArmZone) |
        (std::uint32_t {1U} << kColossusTorsoZone);

    for (const auto gore : {
             GorePresentationMode::Full,
             GorePresentationMode::Reduced,
             GorePresentationMode::Disabled,
         }) {
        input.gore_mode = gore;
        const auto parts = build_chained_colossus_parts(input);
        CAPTURE(static_cast<int>(gore));
        CHECK(std::none_of(
            parts.begin(), parts.end(),
            [](const ChainedColossusPartInstance& part) {
                return part.visual_part ==
                           ColossusVisualPart::LeftArm ||
                       part.zone_id == kColossusLeftArmZone;
            }));
        CHECK(std::none_of(
            parts.begin(), parts.end(),
            [](const ChainedColossusPartInstance& part) {
                return part.visual_part ==
                       ColossusVisualPart::Horn;
            }));
    }
}

TEST_CASE("les modes de gore sont strictement identiques pour le gameplay") {
    ChainedColossusVisualInput input {};
    input.health_ratio = 0.32F;
    input.stagger_ratio = 0.78F;
    input.hidden_parts_mask = static_cast<std::uint32_t>(
        ColossusHiddenPart::RightLeg);

    input.gore_mode = GorePresentationMode::Full;
    const auto full =
        colossus_presentation_gameplay_signature(input);
    input.gore_mode = GorePresentationMode::Reduced;
    const auto reduced =
        colossus_presentation_gameplay_signature(input);
    input.gore_mode = GorePresentationMode::Disabled;
    const auto disabled =
        colossus_presentation_gameplay_signature(input);

    CHECK(full == reduced);
    CHECK(reduced == disabled);
}

TEST_CASE("le tampon de sang reste borne a vingt huit traces") {
    ColossusBloodTraceBuffer buffer {};
    for (std::uint32_t impact = 0U; impact < 80U; ++impact) {
        buffer.add_impact(
            {static_cast<float>(impact), 0.0F, 0.0F},
            {0.0F, 1.0F, 0.0F}, 1.0F, impact + 1U,
            GorePresentationMode::Full);
        CHECK(buffer.traces().size() <=
              kColossusMaximumBloodTraces);
    }
    CHECK(buffer.traces().size() ==
          kColossusMaximumBloodTraces);
    CHECK(buffer.accepted_impact_count() == 80U);
}

TEST_CASE("les traces sont deterministes et le mode reduit reste discret") {
    ColossusBloodTraceBuffer first {};
    ColossusBloodTraceBuffer second {};
    for (std::uint32_t impact = 0U; impact < 5U; ++impact) {
        first.add_impact(
            {1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 1.0F},
            0.70F, 42U + impact,
            GorePresentationMode::Full);
        second.add_impact(
            {1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 1.0F},
            0.70F, 42U + impact,
            GorePresentationMode::Full);
    }
    CHECK(traces_match(first.traces(), second.traces()));

    ColossusBloodTraceBuffer reduced {};
    reduced.add_impact(
        {}, {}, 1.0F, 9U, GorePresentationMode::Reduced);
    REQUIRE(reduced.traces().size() == 1U);
    CHECK(reduced.traces().front().muted);
    CHECK(reduced.traces().front().opacity <= 0.28F);
}

TEST_CASE("le gore desactive ne cree rien mais accepte le meme impact") {
    ColossusBloodTraceBuffer buffer {};
    buffer.add_impact(
        {}, {}, 1.0F, 1U, GorePresentationMode::Disabled);
    CHECK(buffer.traces().empty());
    CHECK(buffer.accepted_impact_count() == 1U);

    buffer.add_impact(
        {}, {}, 1.0F, 2U, GorePresentationMode::Reduced);
    REQUIRE_FALSE(buffer.traces().empty());
    buffer.update(10.0F);
    CHECK(buffer.traces().empty());
}

TEST_CASE("les entrees colosse non finies sont assainies") {
    ChainedColossusVisualInput input {};
    input.position = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    input.yaw_radians =
        std::numeric_limits<float>::quiet_NaN();
    input.animation_seconds =
        std::numeric_limits<float>::infinity();
    input.health_ratio =
        std::numeric_limits<float>::quiet_NaN();
    input.sky_light =
        std::numeric_limits<float>::infinity();

    const auto parts = build_chained_colossus_parts(input);
    CHECK_FALSE(parts.empty());
    CHECK(all_colossus_parts_are_finite(parts));
}

} // namespace valcraft
