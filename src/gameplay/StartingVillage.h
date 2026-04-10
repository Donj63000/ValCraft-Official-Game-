#pragma once

#include "creatures/CreatureTypes.h"
#include "world/World.h"

#include <cstdint>
#include <vector>

namespace valcraft {

enum class VillageBuildingRole : std::uint8_t {
    House = 0,
    Workshop = 1,
    Storehouse = 2,
    Lodge = 3,
};

enum class VillageFacing : std::uint8_t {
    North = 0,
    South = 1,
    West = 2,
    East = 3,
};

struct StartingVillageBuilding {
    VillageBuildingRole role = VillageBuildingRole::House;
    VillageFacing facing = VillageFacing::North;
    int min_x = 0;
    int max_x = 0;
    int min_z = 0;
    int max_z = 0;
    int base_y = 0;
    int door_x = 0;
    int door_z = 0;
    int yard_x = 0;
    int yard_z = 0;
    int interior_x = 0;
    int interior_z = 0;
    std::uint32_t variant_seed = 0;
};

struct StartingVillageLayout {
    int seed = 1337;
    int center_x = 0;
    int center_z = 0;
    int base_y = 0;
    int min_x = 0;
    int max_x = 0;
    int min_z = 0;
    int max_z = 0;
    glm::vec3 player_spawn {0.5F, 70.0F, 0.5F};
    std::vector<StartingVillageBuilding> buildings {};
    std::vector<CreatureSpawnAnchor> residents {};
};

class StartingVillageGenerator {
public:
    explicit StartingVillageGenerator(int seed = 1337);

    [[nodiscard]] auto build_layout() const -> StartingVillageLayout;
    void apply(World& world, const StartingVillageLayout& layout) const;

private:
    int seed_ = 1337;
};

} // namespace valcraft
