#pragma once

#include "world/World.h"

#include <cstdint>
#include <vector>

namespace valcraft {

struct IssouArenaBounds {
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
    int min_z = 0;
    int max_z = 0;

    [[nodiscard]] constexpr auto contains(
        int x,
        int y,
        int z) const noexcept -> bool {
        return x >= min_x && x <= max_x &&
               y >= min_y && y <= max_y &&
               z >= min_z && z <= max_z;
    }

    auto operator==(const IssouArenaBounds&) const -> bool = default;
};

struct IssouArenaLayout {
    int seed = 1337;
    int floor_y = 72;
    IssouArenaBounds protected_bounds {};
    IssouArenaBounds combat_bounds {};
    glm::vec3 player_spawn {0.5F, 73.01F, 15.5F};
    glm::vec3 colossus_spawn {0.5F, 73.01F, -5.5F};
    std::vector<BlockCoord> braziers {};
    std::vector<BlockCoord> gate_cells {};
    std::vector<glm::vec3> chain_anchors {};

    auto operator==(const IssouArenaLayout&) const -> bool = default;
};

class IssouArenaLayoutGenerator {
public:
    explicit IssouArenaLayoutGenerator(
        int seed = 1337,
        int center_x = 0,
        int center_z = 0,
        int floor_y = 72) noexcept;

    [[nodiscard]] auto build_layout() const -> IssouArenaLayout;
    void apply(World& world, const IssouArenaLayout& layout) const;

private:
    int seed_ = 1337;
    int center_x_ = 0;
    int center_z_ = 0;
    int floor_y_ = 72;
};

} // namespace valcraft
