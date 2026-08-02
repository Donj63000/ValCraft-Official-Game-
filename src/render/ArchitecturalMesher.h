#pragma once

#include "world/Block.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <vector>

namespace valcraft {

enum class ArchitecturalFace : std::uint8_t {
    PositiveX = 0,
    NegativeX,
    PositiveY,
    NegativeY,
    PositiveZ,
    NegativeZ,
};

enum class ArchitecturalFixtureKind : std::uint8_t {
    FloorTorch = 0,
    WallTorchPositiveX,
    WallTorchNegativeX,
    WallTorchPositiveZ,
    WallTorchNegativeZ,
};

enum ArchitecturalSurfaceFlag : std::uint8_t {
    ArchitecturalBevelNegativeU = 1U << 0U,
    ArchitecturalBevelPositiveU = 1U << 1U,
    ArchitecturalBevelNegativeV = 1U << 2U,
    ArchitecturalBevelPositiveV = 1U << 3U,
    ArchitecturalTransparent = 1U << 4U,
};

// Je garde les positions et normales directement consommables par OpenGL,
// puis je compacte les UV, lumieres et indicateurs dans les huit derniers
// octets. Le budget reste ainsi strictement limite a 32 octets.
struct HardSurfaceVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
    std::uint16_t u_fixed = 0;
    std::uint16_t v_fixed = 0;
    BlockId material_block = to_block_id(BlockType::Air);
    std::uint8_t sky_light = 15;
    std::uint8_t block_light = 0;
    std::uint8_t surface_flags = 0;

    auto operator==(const HardSurfaceVertex&) const -> bool = default;
};

static_assert(sizeof(HardSurfaceVertex) == 32, "Je limite le sommet architectural a 32 octets");
static_assert(std::is_standard_layout_v<HardSurfaceVertex>);
static_assert(std::is_trivially_copyable_v<HardSurfaceVertex>);

struct ArchitecturalBounds {
    float min_x = 0.0F;
    float min_y = 0.0F;
    float min_z = 0.0F;
    float max_x = 0.0F;
    float max_y = 0.0F;
    float max_z = 0.0F;
    bool valid = false;

    auto operator==(const ArchitecturalBounds&) const -> bool = default;
};

struct ArchitecturalCellSample {
    BlockId block_id = to_block_id(BlockType::Air);
    std::uint8_t sky_light = 15;
    std::uint8_t block_light = 0;
};

using ArchitecturalSampler =
    std::function<ArchitecturalCellSample(int world_x, int world_y, int world_z)>;

struct ArchitecturalSection {
    // Je rends les bornes inclusives comme les sections verticales du monde.
    BlockCoord min {};
    BlockCoord max {
        kChunkSizeX - 1,
        15,
        kChunkSizeZ - 1,
    };
    int halo = 1;

    [[nodiscard]] auto valid() const noexcept -> bool;
    [[nodiscard]] auto contains(BlockCoord coordinate) const noexcept -> bool;
};

struct ArchitecturalQuad {
    std::uint32_t first_vertex = 0;
    std::uint32_t first_index = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    BlockCoord owner_cell {};
    ArchitecturalFace face = ArchitecturalFace::PositiveX;
    BlockId material_block = to_block_id(BlockType::Air);
    std::uint8_t surface_flags = 0;

    auto operator==(const ArchitecturalQuad&) const -> bool = default;
};

struct ArchitecturalFixtureInstance {
    float position_x = 0.0F;
    float position_y = 0.0F;
    float position_z = 0.0F;
    float direction_x = 0.0F;
    float direction_y = 1.0F;
    float direction_z = 0.0F;
    BlockCoord owner_cell {};
    ArchitecturalBounds bounds {};
    ArchitecturalFixtureKind kind = ArchitecturalFixtureKind::FloorTorch;
    BlockId source_block = to_block_id(BlockType::Torch);
    std::uint8_t sky_light = 15;
    std::uint8_t block_light = 14;

    auto operator==(const ArchitecturalFixtureInstance&) const -> bool = default;
};

struct ArchitecturalMesh {
    std::vector<HardSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<ArchitecturalQuad> quads;
    std::vector<ArchitecturalFixtureInstance> fixtures;
    ArchitecturalBounds bounds {};

    [[nodiscard]] auto empty() const noexcept -> bool {
        return indices.empty() && fixtures.empty();
    }

    [[nodiscard]] auto triangle_count() const noexcept -> std::size_t {
        return indices.size() / 3U;
    }

    auto operator==(const ArchitecturalMesh&) const -> bool = default;
};

struct ArchitecturalMesherSettings {
    // Je fournis les informations de biseau au shader uniquement sur les
    // contours exposes. Aucune subdivision n'est ajoutee entre deux cellules.
    bool mark_silhouette_bevels = true;
};

[[nodiscard]] constexpr auto is_backrooms_architectural_block(
    BlockId block_id) noexcept -> bool {
    // Je garde la simulation sur sa grille historique, mais je route toutes
    // les enveloppes solides des Backrooms vers les grandes nappes PBR. Les
    // colonnes de marche sont fusionnees comme du beton continu; les meubles,
    // plantes, bouees et rampes gardent leurs silhouettes specialisees.
    return
        (block_id >= to_block_id(BlockType::BackroomsWallYellow) &&
         block_id <= to_block_id(BlockType::PoolroomsFailedLight)) ||
        block_id == to_block_id(BlockType::BackroomsConnectorStep);
}

[[nodiscard]] constexpr auto is_architectural_solid_block(BlockId block_id) noexcept -> bool {
    if (is_backrooms_architectural_block(block_id)) {
        return true;
    }
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Wood:
    case BlockType::Cobblestone:
    case BlockType::Planks:
    case BlockType::PineWood:
    case BlockType::Glass:
        return true;
    case BlockType::Air:
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Stone:
    case BlockType::Sand:
    case BlockType::Leaves:
    case BlockType::Torch:
    case BlockType::Gravel:
    case BlockType::MossyStone:
    case BlockType::Snow:
    case BlockType::PineLeaves:
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::DeadShrub:
    case BlockType::Cactus:
    case BlockType::Water:
    case BlockType::TorchWallPositiveX:
    case BlockType::TorchWallNegativeX:
    case BlockType::TorchWallPositiveZ:
    case BlockType::TorchWallNegativeZ:
    case BlockType::Pastron:
    case BlockType::RoundShield:
    case BlockType::Sword:
    case BlockType::Spear:
    case BlockType::Shoes:
    case BlockType::Pants:
    case BlockType::CoalOre:
    case BlockType::IronOre:
    case BlockType::GoldOre:
    case BlockType::DiamondOre:
    case BlockType::MetallicAlloyOre:
    case BlockType::Pickaxe:
    case BlockType::Axe:
    case BlockType::Shovel:
    default:
        return false;
    }
}

[[nodiscard]] constexpr auto is_architectural_fixture_block(BlockId block_id) noexcept -> bool {
    return is_torch_block(block_id);
}

class ArchitecturalMesher {
public:
    explicit ArchitecturalMesher(ArchitecturalMesherSettings settings = {}) noexcept;

    [[nodiscard]] auto build_mesh(
        const ArchitecturalSection& section,
        const ArchitecturalSampler& sampler,
        std::size_t vertex_reserve_hint = 0,
        std::size_t index_reserve_hint = 0) const -> ArchitecturalMesh;

    [[nodiscard]] auto settings() const noexcept -> const ArchitecturalMesherSettings&;

private:
    ArchitecturalMesherSettings settings_ {};
};

// Je hache chaque champ dans un ordre canonique et ne depens donc pas du
// padding des structures ou de l'implementation de std::vector.
[[nodiscard]] auto architectural_mesh_deterministic_hash(
    const ArchitecturalMesh& mesh) noexcept -> std::uint64_t;

// Je reconstruis l'EBO par nature de primitive au lieu de supposer que les
// triangles libres viennent apres tous les quads. Cette hypothese devient
// fausse des que plusieurs tranches ou sections sont concatenees.
[[nodiscard]] auto order_architectural_indices_for_render(
    const ArchitecturalMesh& mesh,
    std::vector<std::uint32_t>& ordered_indices,
    std::vector<std::uint8_t>& quad_index_coverage) -> std::size_t;

} // namespace valcraft
