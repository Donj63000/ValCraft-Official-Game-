#include "gameplay/scenarios/IssouArenaLayout.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace valcraft {

namespace {

constexpr int kCombatHalfWidth = 26;
constexpr int kCombatHalfDepth = 20;
constexpr int kArenaHalfWidth = 34;
constexpr int kArenaHalfDepth = 28;
constexpr int kWallHeight = 12;
constexpr int kClearanceHeight = 17;

auto deterministic_variant(int seed, int x, int z) noexcept -> std::uint32_t {
    auto value =
        static_cast<std::uint32_t>(seed) ^
        static_cast<std::uint32_t>(x) * 0x9E3779B9U ^
        static_cast<std::uint32_t>(z) * 0x85EBCA6BU;
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    return value;
}

auto masonry_for(int seed, int x, int z) noexcept -> BlockId {
    const auto variant = deterministic_variant(seed, x, z) % 11U;
    if (variant == 0U) {
        return to_block_id(BlockType::MossyStone);
    }
    if (variant <= 3U) {
        return to_block_id(BlockType::Cobblestone);
    }
    return to_block_id(BlockType::Stone);
}

void fill_box(
    World& world,
    const IssouArenaBounds& bounds,
    BlockId block_id) {
    for (auto y = bounds.min_y; y <= bounds.max_y; ++y) {
        for (auto z = bounds.min_z; z <= bounds.max_z; ++z) {
            for (auto x = bounds.min_x; x <= bounds.max_x; ++x) {
                world.set_block(x, y, z, block_id);
            }
        }
    }
}

} // namespace

IssouArenaLayoutGenerator::IssouArenaLayoutGenerator(
    int seed,
    int center_x,
    int center_z,
    int floor_y) noexcept
    : seed_(seed),
      center_x_(center_x),
      center_z_(center_z),
      floor_y_(std::clamp(
          floor_y,
          kWorldMinY + 8,
          kWorldMaxY - kClearanceHeight - 1)) {}

auto IssouArenaLayoutGenerator::build_layout() const
    -> IssouArenaLayout {
    IssouArenaLayout layout {};
    layout.seed = seed_;
    layout.floor_y = floor_y_;
    layout.protected_bounds = {
        center_x_ - kArenaHalfWidth - 2,
        center_x_ + kArenaHalfWidth + 2,
        floor_y_ - 4,
        floor_y_ + kClearanceHeight,
        center_z_ - kArenaHalfDepth - 2,
        center_z_ + kArenaHalfDepth + 2,
    };
    layout.combat_bounds = {
        center_x_ - kCombatHalfWidth,
        center_x_ + kCombatHalfWidth - 1,
        floor_y_,
        floor_y_ + kWallHeight,
        center_z_ - kCombatHalfDepth,
        center_z_ + kCombatHalfDepth - 1,
    };
    layout.player_spawn = {
        static_cast<float>(center_x_) + 0.5F,
        static_cast<float>(floor_y_) + 1.01F,
        static_cast<float>(center_z_ + 15) + 0.5F,
    };
    layout.colossus_spawn = {
        static_cast<float>(center_x_) + 0.5F,
        static_cast<float>(floor_y_) + 1.01F,
        static_cast<float>(center_z_ - 6) + 0.5F,
    };

    constexpr std::array<std::array<int, 2>, 8> kBrazierOffsets {{
        {{-23, -17}},
        {{0, -17}},
        {{23, -17}},
        {{-23, 17}},
        {{0, 17}},
        {{23, 17}},
        {{-30, 0}},
        {{30, 0}},
    }};
    layout.braziers.reserve(kBrazierOffsets.size());
    for (const auto& offset : kBrazierOffsets) {
        layout.braziers.push_back({
            center_x_ + offset[0],
            floor_y_ + 3,
            center_z_ + offset[1],
        });
    }

    for (auto y = floor_y_ + 1; y <= floor_y_ + 6; ++y) {
        for (auto x = center_x_ - 3; x <= center_x_ + 3; ++x) {
            layout.gate_cells.push_back({
                x,
                y,
                center_z_ - kCombatHalfDepth,
            });
        }
    }

    layout.chain_anchors = {
        {
            static_cast<float>(center_x_ - 5) + 0.5F,
            static_cast<float>(floor_y_ + 7),
            static_cast<float>(center_z_ - 10) + 0.5F,
        },
        {
            static_cast<float>(center_x_ + 5) + 0.5F,
            static_cast<float>(floor_y_ + 7),
            static_cast<float>(center_z_ - 10) + 0.5F,
        },
        {
            static_cast<float>(center_x_ - 4) + 0.5F,
            static_cast<float>(floor_y_ + 2),
            static_cast<float>(center_z_ - 11) + 0.5F,
        },
        {
            static_cast<float>(center_x_ + 4) + 0.5F,
            static_cast<float>(floor_y_ + 2),
            static_cast<float>(center_z_ - 11) + 0.5F,
        },
    };
    return layout;
}

void IssouArenaLayoutGenerator::apply(
    World& world,
    const IssouArenaLayout& layout) const {
    // Je nettoie tout le volume temporaire avant de construire l'arene afin
    // qu'aucun relief genere ne traverse les gradins ou la piste.
    fill_box(
        world,
        {
            layout.protected_bounds.min_x,
            layout.protected_bounds.max_x,
            layout.floor_y + 1,
            layout.protected_bounds.max_y,
            layout.protected_bounds.min_z,
            layout.protected_bounds.max_z,
        },
        to_block_id(BlockType::Air));

    for (auto z = layout.protected_bounds.min_z;
         z <= layout.protected_bounds.max_z;
         ++z) {
        for (auto x = layout.protected_bounds.min_x;
             x <= layout.protected_bounds.max_x;
             ++x) {
            for (auto y = layout.floor_y - 3; y < layout.floor_y; ++y) {
                world.set_block(
                    x,
                    y,
                    z,
                    masonry_for(layout.seed, x, z));
            }
            world.set_block(
                x,
                layout.floor_y,
                z,
                layout.combat_bounds.contains(
                    x,
                    layout.floor_y,
                    z)
                    ? to_block_id(BlockType::Sand)
                    : masonry_for(layout.seed, x, z));
        }
    }

    const auto is_combat_edge =
        [&layout](int x, int z) noexcept {
            return x == layout.combat_bounds.min_x ||
                   x == layout.combat_bounds.max_x ||
                   z == layout.combat_bounds.min_z ||
                   z == layout.combat_bounds.max_z;
        };
    for (auto z = layout.combat_bounds.min_z;
         z <= layout.combat_bounds.max_z;
         ++z) {
        for (auto x = layout.combat_bounds.min_x;
             x <= layout.combat_bounds.max_x;
             ++x) {
            if (!is_combat_edge(x, z)) {
                continue;
            }
            for (auto y = layout.floor_y + 1;
                 y <= layout.floor_y + kWallHeight;
                 ++y) {
                const auto gate =
                    z == layout.combat_bounds.min_z &&
                    x >= (layout.combat_bounds.min_x +
                          layout.combat_bounds.max_x) /
                             2 -
                             3 &&
                    x <= (layout.combat_bounds.min_x +
                          layout.combat_bounds.max_x) /
                             2 +
                             3 &&
                    y <= layout.floor_y + 6;
                world.set_block(
                    x,
                    y,
                    z,
                    gate
                        ? to_block_id(BlockType::Planks)
                        : masonry_for(layout.seed, x, z));
            }
        }
    }

    // Je construis quatre niveaux de gradins continus. Les marches restent
    // volontairement massives pour conserver un budget de maillage stable.
    for (auto tier = 0; tier < 4; ++tier) {
        const auto inset = tier * 2;
        const auto y = layout.floor_y + 2 + tier * 2;
        const auto min_x = layout.protected_bounds.min_x + 2 + inset;
        const auto max_x = layout.protected_bounds.max_x - 2 - inset;
        const auto min_z = layout.protected_bounds.min_z + 2 + inset;
        const auto max_z = layout.protected_bounds.max_z - 2 - inset;
        for (auto z = min_z; z <= max_z; ++z) {
            for (auto x = min_x; x <= max_x; ++x) {
                const auto outside_combat =
                    x < layout.combat_bounds.min_x - 1 ||
                    x > layout.combat_bounds.max_x + 1 ||
                    z < layout.combat_bounds.min_z - 1 ||
                    z > layout.combat_bounds.max_z + 1;
                if (!outside_combat) {
                    continue;
                }
                world.set_block(
                    x,
                    y,
                    z,
                    to_block_id(BlockType::Cobblestone));
            }
        }
    }

    for (const auto& brazier : layout.braziers) {
        world.set_block(
            brazier.x,
            brazier.y - 2,
            brazier.z,
            to_block_id(BlockType::Cobblestone));
        world.set_block(
            brazier.x,
            brazier.y - 1,
            brazier.z,
            to_block_id(BlockType::IronOre));
        world.set_block(
            brazier.x,
            brazier.y,
            brazier.z,
            to_block_id(BlockType::Torch));
    }
}

} // namespace valcraft
