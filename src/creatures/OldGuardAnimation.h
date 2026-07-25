#pragma once

#include "gameplay/OldGuard.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>

namespace valcraft {

enum class OldGuardReloadStage : std::uint8_t {
    RecoilAndHalfCock = 0,
    Cartridge = 1,
    Prime = 2,
    Powder = 3,
    Ramrod = 4,
    ReturnRamrod = 5,
    Shoulder = 6,
};

struct OldGuardPose {
    glm::mat4 body_root {1.0F};
    glm::mat4 musket_transform {1.0F};
    std::array<glm::vec3, 2> shoulders {};
    std::array<glm::vec3, 2> elbows {};
    std::array<glm::vec3, 2> hands {};
    std::array<glm::vec3, 2> hips {};
    std::array<glm::vec3, 2> knees {};
    std::array<glm::vec3, 2> ankles {};
    std::array<glm::vec3, 2> feet {};
    glm::vec3 pelvis {0.0F};
    glm::vec3 chest {0.0F};
    glm::vec3 neck {0.0F};
    glm::vec3 head {0.0F};
    glm::vec3 muzzle_position {0.0F};
    glm::vec3 muzzle_forward {1.0F, 0.0F, 0.0F};
    glm::vec3 bayonet_base {0.0F};
    glm::vec3 bayonet_tip {0.0F};
    float stature_scale = 1.0F;
    float recoil = 0.0F;
    float ramrod_offset = 0.0F;
    OldGuardReloadStage reload_stage = OldGuardReloadStage::Shoulder;
};

// Je calcule une pose unique pour le rendu et les sockets de combat afin
// qu'aucun tir ou estoc ne puisse se detacher visuellement du mousquet.
[[nodiscard]] auto sample_old_guard_pose(const OldGuardRenderInstance& guard) noexcept
    -> OldGuardPose;

} // namespace valcraft
