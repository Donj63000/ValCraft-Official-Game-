#include "gameplay/combat/DismembermentSystem.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>

namespace valcraft {

namespace {

inline constexpr std::uint64_t kLeftSweepCapability =
    1U << 0U;
inline constexpr std::uint64_t kVerticalStrikeCapability =
    1U << 1U;
inline constexpr std::uint64_t kChargeCapability =
    1U << 2U;
inline constexpr std::uint64_t kHeadCapability =
    1U << 3U;

[[nodiscard]] constexpr auto colossus_parts()
    -> std::array<DismembermentPartDefinition, 4> {
    return {{
        {1U, 40.0F, kLeftSweepCapability, false, 0.10F},
        {2U, 40.0F, kVerticalStrikeCapability, false, 0.10F},
        {3U, 70.0F, kChargeCapability, false, 0.10F},
        {4U, 40.0F, kHeadCapability, true, 0.10F},
    }};
}

[[nodiscard]] constexpr auto valid_section_request(
    DamageZoneId zone_id,
    GorePresentationMode gore_mode =
        GorePresentationMode::Full)
    -> DismembermentRequest {
    return {
        zone_id,
        DamageZoneCondition::Depleted,
        70.0F,
        0.50F,
        true,
        false,
        true,
        false,
        false,
        gore_mode,
    };
}

} // namespace

TEST_CASE("dismemberment configuration is transactional and validates every part") {
    DismembermentSystem system {};
    const auto valid = colossus_parts();
    REQUIRE(system.configure(valid).configured);

    auto duplicate = valid;
    duplicate[3].zone_id = duplicate[0].zone_id;
    const auto duplicate_result =
        system.configure(duplicate);
    CHECK_FALSE(duplicate_result.configured);
    CHECK(
        duplicate_result.error ==
        DismembermentConfigureError::DuplicateZoneId);
    CHECK(duplicate_result.failing_definition_index == 3U);
    CHECK(system.part_count() == valid.size());
    CHECK(system.part(4U).has_value());

    auto invalid_power = valid;
    invalid_power[0].minimum_severing_power =
        std::numeric_limits<float>::infinity();
    CHECK(
        system.configure(invalid_power).error ==
        DismembermentConfigureError::
            InvalidSeveringPower);

    auto invalid_ratio = valid;
    invalid_ratio[0].execution_maximum_health_ratio =
        1.01F;
    CHECK(
        system.configure(invalid_ratio).error ==
        DismembermentConfigureError::
            InvalidExecutionHealthRatio);

    std::array<
        DismembermentPartDefinition,
        kMaximumDismembermentParts + 1U>
        too_many {};
    for (std::size_t index = 0U;
         index < too_many.size();
         ++index) {
        too_many[index].zone_id =
            static_cast<DamageZoneId>(index + 1U);
    }
    CHECK(
        system.configure(too_many).error ==
        DismembermentConfigureError::
            CapacityExceeded);
}

TEST_CASE("a limb becomes sectionable only after all gameplay gates pass") {
    DismembermentSystem system {};
    const auto parts = colossus_parts();
    REQUIRE(system.configure(parts).configured);

    auto request = valid_section_request(1U);
    request.local_condition =
        DamageZoneCondition::Wounded;
    auto result = system.try_section(request);
    CHECK(result.accepted);
    CHECK_FALSE(result.sectionable);
    CHECK(
        result.reason ==
        DismembermentBlockReason::
            LocalResistanceRemaining);
    CHECK(
        result.state ==
        DismembermentPartState::Wounded);

    request.local_condition =
        DamageZoneCondition::Depleted;
    request.phase_allows_severing = false;
    result = system.try_section(request);
    CHECK(
        result.reason ==
        DismembermentBlockReason::PhaseLocked);

    request.phase_allows_severing = true;
    request.armor_intact = true;
    result = system.try_section(request);
    CHECK(
        result.reason ==
        DismembermentBlockReason::ArmorIntact);

    request.armor_intact = false;
    request.blade_crossed_zone = false;
    result = system.try_section(request);
    CHECK(result.sectionable);
    CHECK(
        result.reason ==
        DismembermentBlockReason::BladeDidNotCross);
    CHECK(
        result.state ==
        DismembermentPartState::Sectionable);

    request.blade_crossed_zone = true;
    request.severing_power = 20.0F;
    result = system.try_section(request);
    CHECK(result.sectionable);
    CHECK_FALSE(result.severed_now);
    CHECK(
        result.reason ==
        DismembermentBlockReason::
            InsufficientSeveringPower);

    request.severing_power = 40.0F;
    result = system.try_section(request);
    REQUIRE(result.severed_now);
    CHECK(result.gameplay_neutralized);
    CHECK(
        result.visual_action ==
        DismembermentVisualAction::DetachWithBlood);
    CHECK(
        result.state ==
        DismembermentPartState::Severed);
    CHECK(system.severed_part_count() == 1U);
    CHECK(
        system.capability_is_disabled(
            kLeftSweepCapability));

    const auto repeated = system.try_section(request);
    CHECK(repeated.accepted);
    CHECK_FALSE(repeated.severed_now);
    CHECK(repeated.gameplay_neutralized);
    CHECK(
        repeated.reason ==
        DismembermentBlockReason::AlreadySevered);
    CHECK(system.severed_part_count() == 1U);
}

TEST_CASE("head execution requires low health stagger and a compatible attack") {
    DismembermentSystem system {};
    const auto parts = colossus_parts();
    REQUIRE(system.configure(parts).configured);
    auto request = valid_section_request(4U);
    request.severing_power = 40.0F;
    request.target_health_ratio = 0.11F;
    request.target_staggered = true;
    request.execution_attack = true;

    CHECK(
        system.try_section(request).reason ==
        DismembermentBlockReason::
            ExecutionHealthTooHigh);

    request.target_health_ratio = 0.10F;
    request.target_staggered = false;
    CHECK(
        system.try_section(request).reason ==
        DismembermentBlockReason::
            ExecutionRequiresStagger);

    request.target_staggered = true;
    request.execution_attack = false;
    const auto wrong_attack =
        system.try_section(request);
    CHECK(wrong_attack.sectionable);
    CHECK(
        wrong_attack.reason ==
        DismembermentBlockReason::
            ExecutionAttackRequired);

    request.execution_attack = true;
    request.blade_crossed_zone = false;
    CHECK(
        system.try_section(request).reason ==
        DismembermentBlockReason::
            BladeDidNotCross);

    request.blade_crossed_zone = true;
    request.severing_power = 39.999F;
    CHECK(
        system.try_section(request).reason ==
        DismembermentBlockReason::
            InsufficientSeveringPower);

    request.severing_power = 40.0F;
    const auto execution =
        system.try_section(request);
    CHECK(execution.severed_now);
    CHECK(execution.gameplay_neutralized);
    CHECK(
        system.capability_is_disabled(
            kHeadCapability));
}

TEST_CASE("gore preference changes presentation but never gameplay") {
    DismembermentSystem full {};
    DismembermentSystem reduced {};
    DismembermentSystem disabled {};
    const auto parts = colossus_parts();
    REQUIRE(full.configure(parts).configured);
    REQUIRE(reduced.configure(parts).configured);
    REQUIRE(disabled.configure(parts).configured);

    const auto full_result =
        full.try_section(
            valid_section_request(
                3U,
                GorePresentationMode::Full));
    const auto reduced_result =
        reduced.try_section(
            valid_section_request(
                3U,
                GorePresentationMode::Reduced));
    const auto disabled_result =
        disabled.try_section(
            valid_section_request(
                3U,
                GorePresentationMode::Disabled));

    REQUIRE(full_result.severed_now);
    REQUIRE(reduced_result.severed_now);
    REQUIRE(disabled_result.severed_now);
    CHECK(full_result.gameplay_neutralized);
    CHECK(reduced_result.gameplay_neutralized);
    CHECK(disabled_result.gameplay_neutralized);
    CHECK(
        full_result.visual_action ==
        DismembermentVisualAction::DetachWithBlood);
    CHECK(
        reduced_result.visual_action ==
        DismembermentVisualAction::
            HideWithMutedEffect);
    CHECK(
        disabled_result.visual_action ==
        DismembermentVisualAction::
            HideWithDarkEffect);
    CHECK(
        full.disabled_capabilities() ==
        reduced.disabled_capabilities());
    CHECK(
        reduced.disabled_capabilities() ==
        disabled.disabled_capabilities());
    CHECK(
        disabled.capability_is_disabled(
            kChargeCapability));
}

TEST_CASE("damage zone output feeds dismemberment without duplicated local health") {
    DamageZones damage_zones {};
    const std::array definitions {
        DamageZoneDefinition {
            1U,
            DamageZoneKind::LeftArm,
            65.0F,
            1.0F,
            1.0F,
            1.0F,
        },
    };
    REQUIRE(
        damage_zones.configure(definitions).configured);

    DismembermentSystem dismemberment {};
    const std::array parts {
        DismembermentPartDefinition {
            1U,
            40.0F,
            kLeftSweepCapability,
            false,
            0.10F,
        },
    };
    REQUIRE(dismemberment.configure(parts).configured);

    const auto first_hit =
        damage_zones.apply_hit({1U, 30.0F, 0.0F});
    REQUIRE(first_hit.accepted);
    auto request = valid_section_request(1U);
    request.local_condition = first_hit.condition;
    CHECK_FALSE(
        dismemberment.try_section(request).severed_now);

    const auto second_hit =
        damage_zones.apply_hit({1U, 35.0F, 0.0F});
    REQUIRE(second_hit.depleted_now);
    request.local_condition = second_hit.condition;
    request.severing_power = 40.0F;
    CHECK(
        dismemberment.try_section(request).severed_now);
}

TEST_CASE("dismemberment reset restores capabilities and invalid requests never mutate state") {
    DismembermentSystem system {};
    const auto parts = colossus_parts();
    REQUIRE(system.configure(parts).configured);

    auto invalid = valid_section_request(2U);
    invalid.severing_power =
        std::numeric_limits<float>::quiet_NaN();
    const auto invalid_result =
        system.try_section(invalid);
    CHECK_FALSE(invalid_result.accepted);
    CHECK(
        invalid_result.reason ==
        DismembermentBlockReason::InvalidRequest);
    REQUIRE(system.part(2U).has_value());
    CHECK(
        system.part(2U)->state ==
        DismembermentPartState::Intact);

    const auto severed =
        system.try_section(
            valid_section_request(2U));
    REQUIRE(severed.severed_now);
    CHECK(
        system.capability_is_disabled(
            kVerticalStrikeCapability));

    system.reset();
    CHECK(system.severed_part_count() == 0U);
    CHECK(system.disabled_capabilities() == 0U);
    REQUIRE(system.part(2U).has_value());
    CHECK(
        system.part(2U)->state ==
        DismembermentPartState::Intact);

    system.clear();
    CHECK(system.part_count() == 0U);
    CHECK_FALSE(system.part(2U).has_value());
}

} // namespace valcraft
