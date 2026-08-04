#pragma once

#include "creatures/CreatureGeometry.h"
#include "gameplay/BackroomsMarlow.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace valcraft {

inline constexpr std::size_t kBackroomsMarlowVisualPartBudget = 96U;
inline constexpr std::size_t kBackroomsMarlowBuoyVisualPartBudget = 20U;
// Je conserve ce nom visuel comme alias de compatibilite, mais la navigation
// et le renderer lisent desormais une seule dimension partagee du rig.
inline constexpr float kBackroomsMarlowVisualStandingHeight =
    kBackroomsMarlowRigStandingHeight;
inline constexpr float kBackroomsMarlowMaximumSubmersion = 3.35F;

enum class BackroomsMarlowVisualPresentation : std::uint8_t {
    FullBody = 0,
    ProgressiveReveal = 1,
    HeadOnlyPeek = 2,
};

struct BackroomsMarlowVisualPose {
    glm::vec3 position {0.0F};
    float yaw_radians = 0.0F;
    float animation_time = 0.0F;
    float motion_amount = 0.0F;
    float submersion_ratio = 0.0F;
    // Le gameplay fournit la profondeur libre sous l'ancre. La valeur par
    // défaut conserve le comportement des poses visuelles autonomes et tests.
    float maximum_submersion = kBackroomsMarlowMaximumSubmersion;
    // Je sépare la révélation géométrique de l'immersion : une eau peu
    // profonde ne peut ainsi plus rendre tout le corps visible d'un coup.
    float reveal_amount = 1.0F;
    BackroomsMarlowVisualPresentation presentation =
        BackroomsMarlowVisualPresentation::FullBody;
    // Je signe cette valeur : -1 et +1 font sortir Marlow des deux cotes
    // possibles d'un angle sans dupliquer le rig.
    float peek_amount = 0.0F;
    // Je reçois une normale horizontale en espace monde. L'offset permet de
    // placer la tête au bord du mur sans réintroduire le corps derrière elle.
    glm::vec3 wall_normal {0.0F};
    float wall_offset = 0.0F;
    float head_scan_radians = 0.0F;
    float reach_amount = 0.0F;
    bool jumpscare = false;
    float sky_light = 0.0F;
    float block_light = 0.0F;
};

struct BackroomsMarlowBuoyVisualPose {
    // La coordonnee Y est la surface reelle de l'eau, pas le fond du bassin.
    glm::vec3 water_surface_position {0.0F};
    float yaw_radians = 0.0F;
    float animation_time = 0.0F;
    float disturbance = 0.0F;
    float sky_light = 0.0F;
    float block_light = 0.0F;
};

// Je garde la meme convention que Jack : le gameplay regarde vers -Z, tandis
// que le visage procedural est modele vers +X.
[[nodiscard]] auto backrooms_marlow_visual_body_yaw_radians(
    float gameplay_yaw_degrees) noexcept -> float;

[[nodiscard]] auto backrooms_marlow_visual_head_yaw_radians(
    float gameplay_head_yaw_degrees) noexcept -> float;

// Je transforme la progression d'apparition en immersion reelle : a zero,
// Marlow reste sous la surface ; a un, il rejoint son immersion de phase.
[[nodiscard]] auto backrooms_marlow_visual_submersion_ratio(
    float phase_immersion_ratio,
    float reveal_amount) noexcept -> float;

[[nodiscard]] auto build_backrooms_marlow_visual_parts(
    const BackroomsMarlowVisualPose& pose)
    -> std::vector<CreaturePartInstance>;

// Je peux remplir directement un buffer persistant du renderer. La fonction
// ajoute les pièces sans effacer celles déjà présentes et borne son propre lot.
void append_backrooms_marlow_visual_parts(
    const BackroomsMarlowVisualPose& pose,
    std::vector<CreaturePartInstance>& parts);

[[nodiscard]] auto build_backrooms_marlow_buoy_visual_parts(
    const BackroomsMarlowBuoyVisualPose& pose)
    -> std::vector<CreaturePartInstance>;

void append_backrooms_marlow_buoy_visual_parts(
    const BackroomsMarlowBuoyVisualPose& pose,
    std::vector<CreaturePartInstance>& parts);

} // namespace valcraft
