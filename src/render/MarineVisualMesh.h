#pragma once

#include "render/MarineDecor.h"
#include "render/OceanLifeField.h"
#include "render/VisualMesh.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <span>

namespace valcraft {

struct ShipProtectionProfile;

struct MarineVisualBudget {
    float radius = 96.0F;
    float near_detail_radius = 34.0F;
    std::size_t maximum_instances = 1'000U;

    auto operator==(const MarineVisualBudget&) const -> bool = default;
};

[[nodiscard]] constexpr auto marine_visual_budget_for_quality(
    RendererQuality quality) noexcept -> MarineVisualBudget {
    switch (quality) {
    case RendererQuality::Medium:
        return {72.0F, 26.0F, 600U};
    case RendererQuality::Low:
        return {48.0F, 18.0F, 320U};
    case RendererQuality::High:
    case RendererQuality::Dynamic:
    default:
        return {96.0F, 34.0F, 1'000U};
    }
}

// Je construis un seul maillage compact pour toute la végétation visible.
// Les formes lointaines sont volontairement simplifiées avant l'envoi GPU.
[[nodiscard]] auto build_marine_decor_visual_mesh(
    std::span<const MarineDecorInstance> instances,
    const glm::vec3& camera_position,
    const MarineVisualBudget& budget) -> OrganicTerrainMesh;

// Je garde les poissons dans un second petit maillage dynamique : leurs
// positions peuvent ainsi évoluer sans réenvoyer les milliers de plantes.
[[nodiscard]] auto build_ocean_life_visual_mesh(
    std::span<const OceanLifeInstance> instances) -> OrganicTerrainMesh;

// Je teste toute l'emprise visuelle du poisson, avec une petite marge autour
// du plan, afin qu'aucune partie de son corps ne traverse la coque.
[[nodiscard]] auto ocean_life_instance_intersects_ship_protection(
    const OceanLifeInstance& instance,
    const glm::mat4& inverse_ship_model,
    const ShipProtectionProfile& protection_profile) noexcept -> bool;

// Je réutilise le profil étanche du navire avant de construire les quads. Les
// poissons continuent ainsi d'exister en mer, mais ne sont jamais envoyés au
// GPU lorsqu'ils croisent une cale ou une paroi de la coque.
[[nodiscard]] auto build_ocean_life_visual_mesh(
    std::span<const OceanLifeInstance> instances,
    const glm::mat4& inverse_ship_model,
    const ShipProtectionProfile& protection_profile) -> OrganicTerrainMesh;

} // namespace valcraft
