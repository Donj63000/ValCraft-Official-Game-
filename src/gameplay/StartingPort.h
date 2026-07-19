#pragma once

#include "world/World.h"

#include <cstdint>
#include <vector>

namespace valcraft {

struct StartingPortArea {
    int min_x = 0;
    int max_x = 0;
    int min_z = 0;
    int max_z = 0;
    int surface_y = 0;

    [[nodiscard]] constexpr auto contains(int x, int z) const noexcept -> bool {
        return x >= min_x && x <= max_x && z >= min_z && z <= max_z;
    }

    auto operator==(const StartingPortArea&) const -> bool = default;
};

enum class StartingPortBuildingRole : std::uint8_t {
    HarborMasterOffice = 0,
    Warehouse = 1,
};

struct StartingPortBuilding {
    StartingPortBuildingRole role = StartingPortBuildingRole::HarborMasterOffice;
    StartingPortArea footprint {};
    BlockCoord door {};
    BlockCoord interior {};
    std::uint32_t variant_seed = 0;

    auto operator==(const StartingPortBuilding&) const -> bool = default;
};

struct StartingPortLayout {
    int seed = 1337;
    int min_x = 0;
    int max_x = 0;
    int min_z = 0;
    int max_z = 0;
    int quay_surface_y = 0;
    int ship_sweep_min_x = 0;
    int ship_sweep_max_x = 0;
    StartingPortArea stone_quay {};
    StartingPortArea gangway {};
    StartingPortArea wooden_pier {};
    StartingPortArea breakwater_west {};
    StartingPortArea breakwater_east {};
    BlockCoord lighthouse_base {};
    BlockCoord crane_base {};
    std::vector<StartingPortBuilding> buildings {};
    std::vector<BlockCoord> cargo_anchors {};
    std::vector<BlockCoord> bollards {};
    std::vector<BlockCoord> lantern_posts {};

    auto operator==(const StartingPortLayout&) const -> bool = default;
};

class StartingPortGenerator {
public:
    explicit StartingPortGenerator(int seed = 1337);

    [[nodiscard]] auto build_layout() const -> StartingPortLayout;
    void apply(World& world, const StartingPortLayout& layout) const;

private:
    int seed_ = 1337;
};

} // namespace valcraft
