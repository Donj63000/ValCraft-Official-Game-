#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace valcraft {

// Je decris ici le mousquet dans un repere neutre : +X suit le canon, +Y
// pointe vers le haut et +Z vers le cote droit de l'arme. Les gardes, le
// viewmodel et l'icone emploient ainsi exactement les memes proportions.
enum class MusketVisualMaterial : std::uint8_t {
    Walnut = 0,
    PatinatedSteel,
    Brass,
    Flint,
    DarkBore,
};

enum class MusketVisualPartKind : std::uint8_t {
    Stock = 0,
    Barrel,
    Band,
    Ramrod,
    Lock,
    Trigger,
    Sight,
    Muzzle,
};

struct MusketVisualPart {
    glm::vec3 center {0.0F};
    glm::vec3 half_extent {0.0F};
    glm::vec3 rotation_radians {0.0F};
    MusketVisualMaterial material = MusketVisualMaterial::Walnut;
    MusketVisualPartKind kind = MusketVisualPartKind::Stock;
};

struct MusketVisualSockets {
    glm::vec3 rear_hand {-0.12F, -0.04F, 0.04F};
    glm::vec3 forward_hand {0.48F, -0.035F, -0.035F};
    glm::vec3 muzzle {1.252F, 0.018F, 0.0F};
    glm::vec3 rear_sight {0.05F, 0.092F, 0.0F};
    glm::vec3 front_sight {1.09F, 0.092F, 0.0F};
    glm::vec3 bayonet_base {1.10F, 0.035F, 0.0F};
    glm::vec3 bayonet_tip {1.76F, 0.035F, 0.0F};
};

inline constexpr MusketVisualSockets kMusketVisualSockets {};

// Je conserve les volumes suffisamment epais pour rester lisibles dans une
// icone de 128 px, tout en detaillant les pieces caracteristiques d'une
// platine a silex historique.
inline constexpr std::array<MusketVisualPart, 30> kMusketVisualParts {{
    // Crosse profilee, plaque de couche, joue et poignee.
    {{-0.655F, -0.088F, 0.000F}, {0.155F, 0.115F, 0.083F}, {0.0F, 0.0F, -0.12F},
     MusketVisualMaterial::Walnut, MusketVisualPartKind::Stock},
    {{-0.455F, -0.048F, 0.000F}, {0.105F, 0.083F, 0.070F}, {0.0F, 0.0F, 0.08F},
     MusketVisualMaterial::Walnut, MusketVisualPartKind::Stock},
    {{-0.285F, -0.026F, 0.000F}, {0.090F, 0.061F, 0.058F}, {0.0F, 0.0F, 0.03F},
     MusketVisualMaterial::Walnut, MusketVisualPartKind::Stock},
    {{-0.805F, -0.105F, 0.000F}, {0.018F, 0.125F, 0.090F}, {0.0F, 0.0F, -0.10F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Stock},
    {{-0.590F, -0.052F, 0.087F}, {0.132F, 0.065F, 0.010F}, {0.0F, 0.0F, -0.10F},
     MusketVisualMaterial::Walnut, MusketVisualPartKind::Stock},
    // Long fût sous le canon, aminci vers la bouche.
    {{-0.070F, -0.020F, 0.000F}, {0.165F, 0.054F, 0.054F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::Walnut, MusketVisualPartKind::Stock},
    {{0.250F, -0.022F, 0.000F}, {0.165F, 0.047F, 0.047F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::Walnut, MusketVisualPartKind::Stock},
    {{0.575F, -0.024F, 0.000F}, {0.165F, 0.040F, 0.041F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::Walnut, MusketVisualPartKind::Stock},
    {{0.895F, -0.025F, 0.000F}, {0.155F, 0.034F, 0.036F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::Walnut, MusketVisualPartKind::Stock},
    // Canon, tonnerre, bouche et trois bandes.
    {{0.080F, 0.026F, 0.000F}, {0.385F, 0.030F, 0.030F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Barrel},
    {{0.695F, 0.022F, 0.000F}, {0.315F, 0.025F, 0.025F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Barrel},
    {{1.095F, 0.019F, 0.000F}, {0.120F, 0.022F, 0.022F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Barrel},
    {{1.225F, 0.018F, 0.000F}, {0.020F, 0.032F, 0.032F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Barrel},
    {{1.247F, 0.018F, 0.000F}, {0.003F, 0.023F, 0.023F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::DarkBore, MusketVisualPartKind::Muzzle},
    {{0.105F, -0.002F, 0.000F}, {0.018F, 0.058F, 0.054F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Band},
    {{0.505F, -0.006F, 0.000F}, {0.018F, 0.052F, 0.049F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Band},
    {{0.930F, -0.008F, 0.000F}, {0.018F, 0.046F, 0.044F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Band},
    // Baguette de chargement sous le fût.
    {{0.600F, -0.071F, 0.000F}, {0.525F, 0.009F, 0.009F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Ramrod},
    {{1.135F, -0.071F, 0.000F}, {0.020F, 0.014F, 0.014F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::Brass, MusketVisualPartKind::Ramrod},
    // Platine, bassinet, chien, machoires, silex et batterie.
    {{-0.105F, 0.035F, 0.052F}, {0.115F, 0.052F, 0.012F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Lock},
    {{-0.010F, 0.075F, 0.058F}, {0.050F, 0.012F, 0.035F}, {0.0F, 0.0F, -0.10F},
     MusketVisualMaterial::Brass, MusketVisualPartKind::Lock},
    {{-0.135F, 0.112F, 0.060F}, {0.024F, 0.080F, 0.020F}, {0.18F, 0.0F, -0.43F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Lock},
    {{-0.088F, 0.174F, 0.060F}, {0.047F, 0.020F, 0.024F}, {0.0F, 0.0F, 0.12F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Lock},
    {{-0.050F, 0.174F, 0.060F}, {0.025F, 0.015F, 0.021F}, {0.0F, 0.0F, 0.08F},
     MusketVisualMaterial::Flint, MusketVisualPartKind::Lock},
    {{0.055F, 0.135F, 0.058F}, {0.019F, 0.075F, 0.028F}, {0.0F, 0.0F, 0.20F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Lock},
    // Détente et pontet courbe suggere par trois petits volumes.
    {{-0.155F, -0.102F, 0.000F}, {0.012F, 0.042F, 0.012F}, {0.0F, 0.0F, -0.30F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Trigger},
    {{-0.220F, -0.139F, 0.000F}, {0.070F, 0.010F, 0.017F}, {0.0F, 0.0F, -0.10F},
     MusketVisualMaterial::Brass, MusketVisualPartKind::Trigger},
    {{-0.285F, -0.107F, 0.000F}, {0.011F, 0.040F, 0.017F}, {0.0F, 0.0F, 0.20F},
     MusketVisualMaterial::Brass, MusketVisualPartKind::Trigger},
    // Organes de visee sobres mais réellement alignes sur le canon.
    {{0.050F, 0.086F, 0.000F}, {0.028F, 0.012F, 0.030F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::PatinatedSteel, MusketVisualPartKind::Sight},
    {{1.090F, 0.076F, 0.000F}, {0.010F, 0.016F, 0.010F}, {0.0F, 0.0F, 0.0F},
     MusketVisualMaterial::Brass, MusketVisualPartKind::Sight},
}};

[[nodiscard]] constexpr auto musket_visual_parts() noexcept
    -> std::span<const MusketVisualPart> {
    return kMusketVisualParts;
}

inline constexpr std::array<float, 6> kMusketReloadStageBoundaries {{
    0.12F,
    0.24F,
    0.38F,
    0.52F,
    0.72F,
    0.86F,
}};

// Je partage exactement le même découpage entre le joueur et la Vieille
// Garde afin que les sept gestes restent synchronisés avec la baguette.
[[nodiscard]] constexpr auto musket_reload_stage(float reload_progress) noexcept
    -> std::uint8_t {
    if (!(reload_progress >= 0.0F && reload_progress <= 1.0F)) {
        return 0U;
    }
    for (std::size_t index = 0U;
         index < kMusketReloadStageBoundaries.size();
         ++index) {
        if (reload_progress < kMusketReloadStageBoundaries[index]) {
            return static_cast<std::uint8_t>(index);
        }
    }
    return static_cast<std::uint8_t>(
        kMusketReloadStageBoundaries.size());
}

[[nodiscard]] constexpr auto musket_ramrod_offset(float reload_progress) noexcept
    -> float {
    if (!(reload_progress >= 0.0F && reload_progress <= 1.0F)) {
        return 0.0F;
    }
    if (reload_progress >= 0.52F && reload_progress < 0.72F) {
        const auto phase = (reload_progress - 0.52F) / 0.20F;
        return phase <= 0.55F
                   ? 0.52F * (phase / 0.55F)
                   : 0.52F * ((1.0F - phase) / 0.45F);
    }
    if (reload_progress >= 0.72F && reload_progress < 0.86F) {
        return 0.34F * (1.0F - (reload_progress - 0.72F) / 0.14F);
    }
    return 0.0F;
}

} // namespace valcraft
