#include "gameplay/combat/ColossalSweep.h"
#include "gameplay/combat/WorldProtectionRegistry.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace valcraft {
namespace {

[[nodiscard]] auto sweep_query(
    std::uint64_t sequence = 1U) noexcept
    -> ColossalSweepQuery {
    ColossalSweepQuery query {};
    query.attack_sequence = sequence;
    query.previous_pose.hilt =
        {0.0F, 0.0F, 0.0F};
    query.previous_pose.tip =
        {-2.0F, 0.0F, 2.0F};
    query.current_pose.hilt =
        {0.0F, 0.0F, 0.0F};
    query.current_pose.tip =
        {2.0F, 0.0F, 2.0F};
    query.attack_origin =
        {0.0F, 0.0F, 0.0F};
    query.forward =
        {0.0F, 0.0F, 1.0F};
    query.blade_radius = 0.10F;
    query.maximum_range = 3.25F;
    query.arc_degrees = 150.0F;
    query.maximum_targets = 6U;
    return query;
}

struct OcclusionContext {
    ColossalCombatTargetId blocked_id = 0U;
    std::size_t call_count = 0U;
};

auto occlusion_probe(
    void* user_data,
    const ColossalSweepOcclusionRequest& request) noexcept
    -> bool {
    auto& context =
        *static_cast<OcclusionContext*>(user_data);
    ++context.call_count;
    return request.target_id ==
           context.blocked_id;
}

TEST_CASE(
    "le quadrilatère balayé touche une cible entre deux poses sans tunneling") {
    ColossalHitLedger ledger {};
    const auto query = sweep_query();
    const std::array candidates {
        ColossalSweepCandidate {
            10U,
            1U,
            {0.0F, 0.0F, 1.50F},
            0.20F,
            0U,
            true,
            false,
        },
        ColossalSweepCandidate {
            11U,
            1U,
            {0.0F, 0.0F, -1.0F},
            0.20F,
            0U,
            true,
            false,
        },
    };
    const auto result =
        resolve_colossal_sweep(
            query,
            candidates,
            ledger);
    CHECK(result.error == ColossalSweepError::None);
    REQUIRE(result.hit_count == 1U);
    CHECK(result.hits[0U].target_id == 10U);
    CHECK(result.geometric_contact_count == 1U);
    CHECK(ledger.contains(10U));
}

TEST_CASE(
    "l'ordre des entrées ne change ni la sélection ni l'ordre des impacts") {
    auto query = sweep_query();
    query.maximum_targets = 3U;
    const std::array forward {
        ColossalSweepCandidate {
            30U,
            1U,
            {1.20F, 0.0F, 1.50F},
            0.50F,
        },
        ColossalSweepCandidate {
            10U,
            1U,
            {0.0F, 0.0F, 0.80F},
            0.20F,
        },
        ColossalSweepCandidate {
            20U,
            1U,
            {-0.50F, 0.0F, 1.20F},
            0.40F,
        },
    };
    auto reverse = forward;
    std::reverse(
        reverse.begin(),
        reverse.end());
    ColossalHitLedger first_ledger {};
    ColossalHitLedger second_ledger {};
    const auto first =
        resolve_colossal_sweep(
            query,
            forward,
            first_ledger);
    const auto second =
        resolve_colossal_sweep(
            query,
            reverse,
            second_ledger);
    REQUIRE(first.hit_count == 3U);
    REQUIRE(second.hit_count == 3U);
    for (std::size_t index = 0U;
         index < first.hit_count;
         ++index) {
        CHECK(
            first.hits[index].target_id ==
            second.hits[index].target_id);
    }
    CHECK(first.hits[0U].target_id == 10U);
}

TEST_CASE(
    "les zones d'une même cible sont dédupliquées en favorisant leur priorité") {
    ColossalHitLedger ledger {};
    const auto query = sweep_query();
    const std::array candidates {
        ColossalSweepCandidate {
            50U,
            3U,
            {0.0F, 0.0F, 1.0F},
            0.20F,
            1U,
        },
        ColossalSweepCandidate {
            50U,
            9U,
            {0.10F, 0.0F, 1.0F},
            0.20F,
            9U,
        },
    };
    const auto result =
        resolve_colossal_sweep(
            query,
            candidates,
            ledger);
    REQUIRE(result.hit_count == 1U);
    CHECK(result.hits[0U].target_id == 50U);
    CHECK(result.hits[0U].zone_id == 9U);
    CHECK(result.duplicate_count == 1U);
}

TEST_CASE(
    "l'occlusion les alliés la limite et le registre interdisent les doubles dégâts") {
    auto query = sweep_query();
    query.maximum_targets = 2U;
    const std::array candidates {
        ColossalSweepCandidate {
            1U,
            1U,
            {-0.20F, 0.0F, 0.70F},
            0.25F,
        },
        ColossalSweepCandidate {
            2U,
            1U,
            {0.20F, 0.0F, 0.90F},
            0.25F,
        },
        ColossalSweepCandidate {
            3U,
            1U,
            {-0.40F, 0.0F, 1.10F},
            0.25F,
        },
        ColossalSweepCandidate {
            4U,
            1U,
            {0.40F, 0.0F, 1.20F},
            0.25F,
            0U,
            true,
            true,
        },
    };
    OcclusionContext occlusion {};
    occlusion.blocked_id = 2U;
    ColossalHitLedger ledger {};
    const auto first =
        resolve_colossal_sweep(
            query,
            candidates,
            ledger,
            {
                &occlusion,
                &occlusion_probe,
            });
    REQUIRE(first.hit_count == 2U);
    CHECK(first.hits[0U].target_id == 1U);
    CHECK(first.hits[1U].target_id == 3U);
    CHECK(first.occluded_count == 1U);
    CHECK(first.friendly_ignored_count == 1U);

    const auto repeated =
        resolve_colossal_sweep(
            query,
            candidates,
            ledger);
    CHECK(repeated.hit_count == 1U);
    CHECK(repeated.hits[0U].target_id == 2U);
    CHECK(repeated.duplicate_count >= 2U);

    query.attack_sequence = 2U;
    const auto next_attack =
        resolve_colossal_sweep(
            query,
            candidates,
            ledger);
    CHECK(next_attack.hit_count == 2U);
}

TEST_CASE(
    "les requêtes invalides et les broadphases trop grandes échouent sans résultat partiel") {
    ColossalHitLedger ledger {};
    auto invalid = sweep_query();
    invalid.blade_radius =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(
        resolve_colossal_sweep(
            invalid,
            {},
            ledger)
                .error ==
        ColossalSweepError::InvalidQuery);

    const std::array<
        ColossalSweepCandidate,
        kMaximumColossalSweepCandidates + 1U>
        too_many {};
    CHECK(
        resolve_colossal_sweep(
            sweep_query(),
            too_many,
            ledger)
                .error ==
        ColossalSweepError::CandidateCapacityExceeded);
}

TEST_CASE(
    "un tunnel trop étroit remplace seulement les balayages horizontaux") {
    ColossalHitLedger shockwave_ledger {};
    shockwave_ledger.begin_attack(77U);
    REQUIRE(shockwave_ledger.try_register(1U));
    const std::array shockwave_candidates {
        ColossalSweepCandidate {
            1U, 1U, {0.0F, 0.0F, 0.5F}, 0.2F},
        ColossalSweepCandidate {
            2U, 1U, {2.6F, 0.0F, 0.0F}, 0.2F},
        ColossalSweepCandidate {
            3U, 1U, {3.4F, 0.0F, 0.0F}, 0.2F},
        ColossalSweepCandidate {
            4U, 1U, {1.0F, 0.0F, 0.0F}, 0.2F},
    };
    OcclusionContext shockwave_occlusion {};
    shockwave_occlusion.blocked_id = 4U;
    const auto shockwave =
        resolve_colossal_shockwave(
            {
                77U,
                {0.0F, 0.0F, 0.0F},
                2.5F,
                6U,
                false,
            },
            shockwave_candidates,
            shockwave_ledger,
            {&shockwave_occlusion, &occlusion_probe});
    REQUIRE(shockwave.error == ColossalSweepError::None);
    REQUIRE(shockwave.hit_count == 1U);
    CHECK(shockwave.hits[0U].target_id == 2U);
    CHECK(shockwave.duplicate_count == 1U);
    CHECK(shockwave.occluded_count == 1U);
    CHECK_FALSE(shockwave_ledger.contains(3U));

    CHECK(
        resolve_colossal_shockwave(
            {
                78U,
                {},
                std::numeric_limits<float>::quiet_NaN(),
                1U,
                false,
            },
            shockwave_candidates,
            shockwave_ledger)
            .error == ColossalSweepError::InvalidQuery);
}

TEST_CASE("le choix d attaque respecte les degagements du tunnel") {
    const ColossalTunnelClearance narrow {
        0.50F,
        0.50F,
        3.0F,
        2.0F,
    };
    const auto replaced =
        choose_colossal_tunnel_attack(
            ColossalAttackShape::HorizontalArc,
            narrow);
    CHECK(replaced.replaced_with_vertical);
    CHECK(
        replaced.shape ==
        ColossalAttackShape::VerticalArc);
    CHECK(replaced.attack_has_clearance);

    const auto vertical =
        choose_colossal_tunnel_attack(
            ColossalAttackShape::VerticalArc,
            narrow);
    CHECK_FALSE(vertical.replaced_with_vertical);
    CHECK(vertical.attack_has_clearance);

    auto blocked = narrow;
    blocked.overhead_blocks = 1.0F;
    CHECK_FALSE(
        choose_colossal_tunnel_attack(
            ColossalAttackShape::HorizontalArc,
            blocked)
            .attack_has_clearance);
}

TEST_CASE(
    "le registre agrège les régions et refuse toute configuration ambiguë") {
    WorldProtectionRegistry registry {};
    CHECK_FALSE(
        registry
            .register_region({
                0U,
                {0, 0, 0},
                {1, 1, 1},
                WorldProtectionFlag::ImportantStructure,
            })
            .registered);
    CHECK(
        registry
            .register_region({
                1U,
                {2, 2, 2},
                {1, 1, 1},
                WorldProtectionFlag::ImportantStructure,
            })
            .error ==
        WorldProtectionRegistrationError::InvalidBounds);

    REQUIRE(
        registry
            .register_region({
                10U,
                {-2, 0, -2},
                {2, 5, 2},
                WorldProtectionFlag::ImportantStructure,
            })
            .registered);
    REQUIRE(
        registry
            .register_region({
                11U,
                {0, 0, 0},
                {4, 4, 4},
                WorldProtectionFlag::QuestStructure,
            })
            .registered);
    CHECK(
        registry
            .register_region({
                10U,
                {10, 0, 10},
                {11, 1, 11},
                WorldProtectionFlag::Ship,
            })
            .error ==
        WorldProtectionRegistrationError::DuplicateId);

    const auto flags =
        registry.protection_at({1, 1, 1});
    CHECK(
        world_protection_contains(
            flags,
            WorldProtectionFlag::ImportantStructure));
    CHECK(
        world_protection_contains(
            flags,
            WorldProtectionFlag::QuestStructure));
    CHECK(registry.unregister_region(10U));
    CHECK_FALSE(registry.unregister_region(10U));
    CHECK(registry.region_count() == 1U);
}

TEST_CASE(
    "le plan chargé ne retient que les cellules fragiles chargées et non protégées") {
    WorldProtectionRegistry registry {};
    REQUIRE(
        registry
            .register_region({
                1U,
                {5, 0, 0},
                {5, 2, 0},
                WorldProtectionFlag::QuestStructure,
            })
            .registered);
    const std::array candidates {
        ColossalFragileCellCandidate {
            {0, 0, 0},
            ColossalCellMaterial::FragileGrass,
            1.0F,
            10U,
            true,
        },
        ColossalFragileCellCandidate {
            {1, 0, 0},
            ColossalCellMaterial::Stone,
            2.0F,
            11U,
            true,
        },
        ColossalFragileCellCandidate {
            {2, 0, 0},
            ColossalCellMaterial::FragileGlass,
            3.0F,
            12U,
            false,
        },
        ColossalFragileCellCandidate {
            {3, 0, 0},
            ColossalCellMaterial::FragileLeaves,
            4.0F,
            13U,
            true,
            true,
        },
        ColossalFragileCellCandidate {
            {4, 0, 0},
            ColossalCellMaterial::LightDecoration,
            5.0F,
            14U,
            true,
            false,
            true,
        },
        ColossalFragileCellCandidate {
            {5, 0, 0},
            ColossalCellMaterial::FragileFlower,
            6.0F,
            15U,
            true,
        },
        ColossalFragileCellCandidate {
            {6, 0, 0},
            ColossalCellMaterial::FragileGlass,
            7.0F,
            16U,
            true,
        },
    };
    const auto plan =
        build_colossal_fragile_impact_plan(
            {
                99U,
                12U,
                true,
            },
            candidates,
            registry);
    CHECK(plan.error == ColossalCellRejection::None);
    REQUIRE(plan.edit_count == 2U);
    CHECK(plan.edits[0U].cell ==
          ColossalWorldCell {0, 0, 0});
    CHECK(plan.edits[0U].expected_block_token == 10U);
    CHECK(plan.edits[1U].cell ==
          ColossalWorldCell {6, 0, 0});
    CHECK(plan.non_fragile_count == 1U);
    CHECK(plan.unloaded_count == 1U);
    CHECK(plan.protected_count == 3U);
}

TEST_CASE(
    "la limite de douze cellules reste déterministe et déduplique les coordonnées") {
    WorldProtectionRegistry registry {};
    std::array<
        ColossalFragileCellCandidate,
        20U>
        candidates {};
    for (std::size_t index = 0U;
         index < candidates.size();
         ++index) {
        const auto reversed =
            candidates.size() - index - 1U;
        candidates[index].cell = {
            static_cast<std::int32_t>(reversed),
            0,
            0,
        };
        candidates[index].material =
            ColossalCellMaterial::FragileLeaves;
        candidates[index].impact_distance_squared =
            static_cast<float>(reversed);
        candidates[index].block_token =
            static_cast<std::uint16_t>(100U + reversed);
        candidates[index].loaded = true;
    }
    const auto plan =
        build_colossal_fragile_impact_plan(
            {
                7U,
                20U,
                true,
            },
            candidates,
            registry);
    REQUIRE(plan.edit_count == 12U);
    CHECK(plan.edit_limit_reached);
    for (std::size_t index = 0U;
         index < plan.edit_count;
         ++index) {
        CHECK(
            plan.edits[index].cell.x ==
            static_cast<std::int32_t>(index));
    }

    const std::array duplicates {
        candidates[19U],
        candidates[19U],
    };
    const auto deduplicated =
        build_colossal_fragile_impact_plan(
            {
                8U,
                12U,
                true,
            },
            duplicates,
            registry);
    CHECK(deduplicated.edit_count == 1U);
    CHECK(deduplicated.duplicate_count == 1U);
}

TEST_CASE(
    "seule l'exécution chargée peut produire un plan de destruction") {
    WorldProtectionRegistry registry {};
    const std::array candidates {
        ColossalFragileCellCandidate {
            {0, 0, 0},
            ColossalCellMaterial::FragileGlass,
            0.0F,
            1U,
            true,
        },
    };
    const auto invalid =
        build_colossal_fragile_impact_plan(
            {
                1U,
                12U,
                false,
            },
            candidates,
            registry);
    CHECK(
        invalid.error ==
        ColossalCellRejection::InvalidQuery);
    CHECK(invalid.edit_count == 0U);
    CHECK(
        colossal_cell_impact_material(
            ColossalCellMaterial::FragileGlass) ==
        ColossalImpactMaterial::Glass);
    CHECK_FALSE(
        colossal_cell_is_fragile(
            ColossalCellMaterial::Ore));
}

} // namespace
} // namespace valcraft
