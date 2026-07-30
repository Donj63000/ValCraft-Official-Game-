#include "render/MarineVisualMesh.h"

#include "gameplay/SeaAdventure.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace valcraft {
namespace {

[[nodiscard]] auto material_for_kind(
    MarineDecorKind kind) noexcept -> VisualMaterialId {
    switch (kind) {
    case MarineDecorKind::Seagrass:
        return VisualMaterialId::MarineSeagrass;
    case MarineDecorKind::Kelp:
        return VisualMaterialId::MarineKelp;
    case MarineDecorKind::CoralFan:
        return VisualMaterialId::CoralFan;
    case MarineDecorKind::BranchCoralWarm:
        return VisualMaterialId::CoralWarm;
    case MarineDecorKind::BranchCoralLagoon:
        return VisualMaterialId::CoralLagoon;
    case MarineDecorKind::Shell:
        return VisualMaterialId::MarineShell;
    }
    return VisualMaterialId::None;
}

[[nodiscard]] auto make_decor(
    MarineDecorKind kind,
    float x,
    float z) -> MarineDecorInstance {
    return {
        x,
        32.0F,
        z,
        0.86F,
        kind == MarineDecorKind::Kelp
            ? 4.2F
            : 1.15F,
        0.78F,
        0.37F,
        1.24F,
        kind,
        0U,
        material_for_kind(kind),
    };
}

void check_mesh_is_finite_and_indexed(
    const OrganicTerrainMesh& mesh) {
    REQUIRE_FALSE(mesh.vertices.empty());
    REQUIRE_FALSE(mesh.indices.empty());
    CHECK(mesh.indices.size() % 3U == 0U);
    for (const auto& vertex : mesh.vertices) {
        CHECK(std::isfinite(vertex.x));
        CHECK(std::isfinite(vertex.y));
        CHECK(std::isfinite(vertex.z));
        CHECK(std::isfinite(vertex.nx));
        CHECK(std::isfinite(vertex.ny));
        CHECK(std::isfinite(vertex.nz));
        const auto normal_length =
            std::sqrt(
                vertex.nx * vertex.nx +
                vertex.ny * vertex.ny +
                vertex.nz * vertex.nz);
        CHECK(normal_length ==
              doctest::Approx(1.0F).epsilon(0.002));
        CHECK(
            (vertex.surface_flags &
             kTerrainSurfaceFlagDirectMaterial) != 0U);
    }
    for (const auto index : mesh.indices) {
        CHECK(index < mesh.vertices.size());
    }
    for (std::size_t index = 0U;
         index < mesh.indices.size();
         index += 3U) {
        const auto& first =
            mesh.vertices[mesh.indices[index]];
        const auto& second =
            mesh.vertices[mesh.indices[index + 1U]];
        const auto& third =
            mesh.vertices[mesh.indices[index + 2U]];
        const glm::vec3 a {
            first.x,
            first.y,
            first.z,
        };
        const glm::vec3 b {
            second.x,
            second.y,
            second.z,
        };
        const glm::vec3 c {
            third.x,
            third.y,
            third.z,
        };
        CHECK(
            glm::length(
                glm::cross(b - a, c - a)) >
            0.000001F);
    }
}

} // namespace

TEST_CASE("les budgets visuels marins bornent strictement chaque qualite") {
    CHECK((
        marine_visual_budget_for_quality(
            RendererQuality::High) ==
        MarineVisualBudget {
            96.0F,
            34.0F,
            1'000U,
        }));
    CHECK((
        marine_visual_budget_for_quality(
            RendererQuality::Medium) ==
        MarineVisualBudget {
            72.0F,
            26.0F,
            600U,
        }));
    CHECK((
        marine_visual_budget_for_quality(
            RendererQuality::Low) ==
        MarineVisualBudget {
            48.0F,
            18.0F,
            320U,
        }));
}

TEST_CASE("chaque famille de decor marin produit un maillage stable et valide") {
    constexpr std::array<MarineDecorKind, 6U> kinds {{
        MarineDecorKind::Seagrass,
        MarineDecorKind::Kelp,
        MarineDecorKind::CoralFan,
        MarineDecorKind::BranchCoralWarm,
        MarineDecorKind::BranchCoralLagoon,
        MarineDecorKind::Shell,
    }};
    std::vector<MarineDecorInstance> instances {};
    for (std::size_t index = 0U;
         index < kinds.size();
         ++index) {
        instances.push_back(
            make_decor(
                kinds[index],
                static_cast<float>(index) * 2.0F,
                static_cast<float>(index % 2U) * 1.5F));
    }

    const auto first =
        build_marine_decor_visual_mesh(
            instances,
            {0.0F, 49.0F, 0.0F},
            marine_visual_budget_for_quality(
                RendererQuality::High));
    const auto replay =
        build_marine_decor_visual_mesh(
            instances,
            {0.0F, 49.0F, 0.0F},
            marine_visual_budget_for_quality(
                RendererQuality::High));

    CHECK(first == replay);
    check_mesh_is_finite_and_indexed(first);
}

TEST_CASE("le LOD lointain simplifie les plantes sans changer leur ancrage") {
    const std::array instances {
        make_decor(
            MarineDecorKind::Seagrass,
            24.0F,
            0.0F),
        make_decor(
            MarineDecorKind::Kelp,
            25.0F,
            0.0F),
        make_decor(
            MarineDecorKind::BranchCoralWarm,
            26.0F,
            0.0F),
    };
    const auto near_mesh =
        build_marine_decor_visual_mesh(
            instances,
            {24.0F, 49.0F, 0.0F},
            {80.0F, 40.0F, 32U});
    const auto far_mesh =
        build_marine_decor_visual_mesh(
            instances,
            {0.0F, 49.0F, 0.0F},
            {80.0F, 8.0F, 32U});

    CHECK(
        far_mesh.vertices.size() <
        near_mesh.vertices.size());
    CHECK(
        far_mesh.indices.size() <
        near_mesh.indices.size());
    check_mesh_is_finite_and_indexed(far_mesh);
}

TEST_CASE("le rayon et le plafond d instances empechent tout budget hostile") {
    std::vector<MarineDecorInstance> instances {};
    for (int index = 0;
         index < 100;
         ++index) {
        instances.push_back(
            make_decor(
                MarineDecorKind::Shell,
                static_cast<float>(index % 10),
                static_cast<float>(index / 10)));
    }

    const auto bounded =
        build_marine_decor_visual_mesh(
            instances,
            {0.0F, 49.0F, 0.0F},
            {30.0F, 30.0F, 5U});
    CHECK(bounded.vertices.size() <= 50U);
    CHECK(bounded.indices.size() <= 240U);

    const auto outside =
        build_marine_decor_visual_mesh(
            instances,
            {500.0F, 49.0F, 500.0F},
            {10.0F, 5.0F, 100U});
    CHECK(outside.empty());

    const auto invalid =
        build_marine_decor_visual_mesh(
            instances,
            {
                std::numeric_limits<float>::quiet_NaN(),
                0.0F,
                0.0F,
            },
            {30.0F, 10.0F, 100U});
    CHECK(invalid.empty());
}

TEST_CASE("la cle stable borne les coordonnees finies hors de la plage int32") {
    const std::array instances {
        make_decor(
            MarineDecorKind::Shell,
            300'000'000.0F,
            300'000'000.0F),
        make_decor(
            MarineDecorKind::Shell,
            -300'000'000.0F,
            -300'000'000.0F),
    };
    const MarineVisualBudget budget {
        1'000'000'000.0F,
        0.0F,
        instances.size(),
    };

    const auto first =
        build_marine_decor_visual_mesh(
            instances,
            {0.0F, 49.0F, 0.0F},
            budget);
    const auto replay =
        build_marine_decor_visual_mesh(
            instances,
            {0.0F, 49.0F, 0.0F},
            budget);

    CHECK(first == replay);
    REQUIRE_FALSE(first.vertices.empty());
    for (const auto& vertex : first.vertices) {
        CHECK(std::isfinite(vertex.x));
        CHECK(std::isfinite(vertex.y));
        CHECK(std::isfinite(vertex.z));
    }
}

TEST_CASE("les poissons utilisent le materiau direct et conservent leurs palettes") {
    std::array<OceanLifeInstance, 4U> fish {};
    for (std::size_t index = 0U;
         index < fish.size();
         ++index) {
        fish[index].position = {
            static_cast<float>(index),
            46.0F,
            2.0F,
        };
        fish[index].scale = 0.32F;
        fish[index].heading_radians =
            static_cast<float>(index) * 0.41F;
        fish[index].fade = 1.0F;
        fish[index].packed_visual =
            static_cast<std::uint32_t>(index)
            << 30U;
    }

    const auto mesh =
        build_ocean_life_visual_mesh(fish);
    REQUIRE(mesh.vertices.size() == 16U);
    REQUIRE(mesh.indices.size() == 24U);
    check_mesh_is_finite_and_indexed(mesh);
    for (std::size_t fish_index = 0U;
         fish_index < fish.size();
         ++fish_index) {
        for (std::size_t vertex = 0U;
             vertex < 4U;
             ++vertex) {
            const auto& value =
                mesh.vertices[
                    fish_index * 4U +
                    vertex];
            CHECK(
                value.primary_block_id ==
                direct_visual_material_token(
                    VisualMaterialId::ReefFish));
            CHECK(
                (value.surface_flags &
                 kTerrainSurfaceFlagCutout) != 0U);
            CHECK(
                (value.surface_flags &
                 kTerrainSurfaceFlagMarineFish) != 0U);
            CHECK(
                value.block_light ==
                fish_index * 5U);
        }
    }
}

TEST_CASE("les poissons qui croisent les cales et la coque ne sont jamais envoyes au GPU") {
    ShipEntity ship {};
    ship.set_position(
        {12.5F, 49.0F, 23.5F});
    const auto state =
        ship.render_state(true);
    REQUIRE(state.blueprint != nullptr);
    const auto& profile =
        state.blueprint
            ->protection_profile;

    const auto make_fish =
        [&ship](const glm::vec3& local_position) {
            OceanLifeInstance instance {};
            instance.position =
                ship.local_to_world_point(
                    local_position);
            instance.scale = 0.32F;
            instance.heading_radians = 0.0F;
            instance.animation_phase = 1.25F;
            instance.fade = 1.0F;
            return instance;
        };
    const auto side_limit =
        profile.half_width_at(0.0F) +
        profile.boundary_margin;
    const std::array<OceanLifeInstance, 3U>
        fish {{
        make_fish(
            state.blueprint
                ->anchors.cargo_hold),
        make_fish({
            side_limit + 0.12F,
            1.50F,
            0.0F,
        }),
        make_fish({
            side_limit + 1.0F,
            1.50F,
            0.0F,
        }),
    }};
    const auto inverse_model =
        glm::inverse(
            state.model_matrix);

    CHECK(
        ocean_life_instance_intersects_ship_protection(
            fish[0],
            inverse_model,
            profile));
    CHECK(
        ocean_life_instance_intersects_ship_protection(
            fish[1],
            inverse_model,
            profile));
    CHECK_FALSE(
        ocean_life_instance_intersects_ship_protection(
            fish[2],
            inverse_model,
            profile));

    const auto filtered =
        build_ocean_life_visual_mesh(
            fish,
            inverse_model,
            profile);
    const auto expected =
        build_ocean_life_visual_mesh(
            std::span<const OceanLifeInstance> {
                fish,
            }.subspan(
                2U,
                1U));
    CHECK(filtered == expected);
    CHECK(filtered.vertices.size() == 4U);
    CHECK(filtered.indices.size() == 6U);
    CHECK(filtered.quad_count == 1U);
}

TEST_CASE("le filtre des poissons suit la pose mobile du navire sans modifier la faune") {
    ShipEntity ship {};
    ship.set_position(
        {-18.5F, 49.0F, 41.5F});
    ship.set_ocean_pose(
        0.42F,
        0.122173048F,
        -0.191986218F);
    const auto state =
        ship.render_state(true);
    REQUIRE(state.blueprint != nullptr);

    OceanLifeInstance cargo_fish {};
    cargo_fish.position =
        ship.local_to_world_point(
            state.blueprint
                ->anchors.cargo_hold);
    cargo_fish.scale = 0.38F;
    cargo_fish.heading_radians = 1.12F;
    cargo_fish.fade = 1.0F;

    const auto original =
        cargo_fish;
    const auto inverse_model =
        glm::inverse(
            state.model_matrix);
    CHECK(
        ocean_life_instance_intersects_ship_protection(
            cargo_fish,
            inverse_model,
            state.blueprint
                ->protection_profile));
    CHECK(
        build_ocean_life_visual_mesh(
            std::span {&cargo_fish, 1U},
            inverse_model,
            state.blueprint
                ->protection_profile)
            .empty());
    CHECK(cargo_fish == original);

    auto invalid_inverse =
        inverse_model;
    invalid_inverse[0][0] =
        std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(
        ocean_life_instance_intersects_ship_protection(
            cargo_fish,
            invalid_inverse,
            state.blueprint
                ->protection_profile));
}

} // namespace valcraft
