#pragma once

#include "render/RendererQuality.h"
#include "render/ShadowCulling.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace valcraft {

inline constexpr std::size_t kMaximumShadowCascadeCount = 2U;

// Je décris ici uniquement les données de caméra dont le calcul des cascades a
// besoin. Le module reste ainsi testable sans fenêtre, contexte OpenGL ou état
// mutable du renderer.
struct ShadowCascadeBuildParameters {
    RendererQuality quality = RendererQuality::High;
    // Zéro applique le nombre prévu par la qualité. Une valeur explicite permet
    // au renderer de transmettre directement RendererQualitySettings.
    int cascade_count = 0;
    glm::vec3 camera_position {0.0F};
    glm::vec3 camera_forward {0.0F, 0.0F, -1.0F};
    glm::vec3 camera_up {0.0F, 1.0F, 0.0F};
    float vertical_fov_radians = 1.30899694F;
    float aspect_ratio = 16.0F / 9.0F;
    float near_distance = 0.1F;
    float far_distance = 320.0F;
    // Je définis cette direction du monde vers le soleil, comme celle déjà
    // utilisée par le renderer historique.
    glm::vec3 sun_direction {0.35F, 0.85F, 0.25F};
    int shadow_map_resolution = 2048;
    float split_lambda = 0.65F;
    float caster_depth_padding = 24.0F;
};

struct ShadowCascadeBounds {
    std::array<glm::vec3, 8> world_frustum_corners {};
    glm::vec3 world_min {0.0F};
    glm::vec3 world_max {0.0F};
    glm::vec3 world_center {0.0F};
    glm::vec3 light_space_min {0.0F};
    glm::vec3 light_space_max {0.0F};
    glm::vec3 unsnapped_light_space_center {0.0F};
    glm::vec3 stabilized_light_space_center {0.0F};
    float bounding_radius = 0.0F;
};

struct ShadowCascade {
    float near_distance = 0.1F;
    float far_distance = 1.0F;
    float world_units_per_texel = 1.0F;
    glm::mat4 light_view {1.0F};
    glm::mat4 light_projection {1.0F};
    glm::mat4 light_view_projection {1.0F};
    std::array<FrustumPlane, 6> frustum {};
    ShadowCascadeBounds bounds {};
};

struct ShadowCascadeSet {
    std::array<ShadowCascade, kMaximumShadowCascadeCount> cascades {};
    std::array<float, kMaximumShadowCascadeCount + 1U> split_distances {};
    std::size_t cascade_count = 0U;
    int shadow_map_resolution = 1;
    // Je conserve la largeur totale de la zone commune aux deux cascades afin
    // que le CPU et les shaders appliquent exactement le même raccord.
    float transition_width = 0.0F;
    glm::vec3 light_right_world {1.0F, 0.0F, 0.0F};
    glm::vec3 light_up_world {0.0F, 1.0F, 0.0F};
    glm::vec3 light_forward_world {0.0F, 0.0F, -1.0F};
    bool input_was_sanitized = false;
};

[[nodiscard]] constexpr auto shadow_cascade_count_for_quality(
    RendererQuality quality) noexcept -> std::size_t {
    switch (quality) {
    case RendererQuality::Medium:
    case RendererQuality::Low:
        return 1U;
    case RendererQuality::High:
    case RendererQuality::Dynamic:
    default:
        return 2U;
    }
}

[[nodiscard]] auto build_shadow_cascade_set(
    const ShadowCascadeBuildParameters& parameters) noexcept
    -> ShadowCascadeSet;

// Je retourne nullopt au-delà de la portée ombrée et derrière la caméra. La
// valeur reçue est une distance positive le long de l'axe de vue.
[[nodiscard]] auto select_shadow_cascade(
    const ShadowCascadeSet& cascades,
    float positive_view_distance) noexcept -> std::optional<std::size_t>;

// Je calcule un raccord borné par les deux tranches de caméra. Il reste assez
// large pour masquer la coupure sans réduire sensiblement la résolution proche.
[[nodiscard]] auto shadow_cascade_transition_width(
    const ShadowCascadeSet& cascades) noexcept -> float;

// Je reproduis ici le smoothstep des shaders pour tester le raccord sans
// contexte OpenGL. Zéro sélectionne la cascade proche, un la cascade lointaine.
[[nodiscard]] auto shadow_cascade_blend_factor(
    const ShadowCascadeSet& cascades,
    float positive_view_distance) noexcept -> float;

[[nodiscard]] auto shadow_cascade_is_finite(
    const ShadowCascade& cascade) noexcept -> bool;

} // namespace valcraft
