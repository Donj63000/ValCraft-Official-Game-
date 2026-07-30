#include "world/WorldGenerator.h"

#include <FastNoiseLite.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace valcraft {

namespace {

constexpr auto kBaseStoneHeight = 42;
constexpr std::uint32_t kForestTreePlacementModulo = 16U;
constexpr std::uint32_t kMeadowTreePlacementModulo = 45U;
constexpr std::uint32_t kTaigaTreePlacementModulo = 18U;
constexpr std::uint32_t kMeadowDecorationPlacementModulo = 8U;
constexpr std::uint32_t kForestDecorationPlacementModulo = 7U;
constexpr std::uint32_t kDesertDecorationPlacementModulo = 9U;
constexpr std::uint32_t kTaigaDecorationPlacementModulo = 11U;
constexpr std::uint32_t kRockyPeaksDecorationPlacementModulo = 19U;
constexpr int kResourceOreSurfaceMargin = 5;
constexpr int kNavigationDecorationOverhang = 2;
constexpr int kSparseArchipelagoCellSize = 128;
constexpr std::uint32_t kSparseArchipelagoCellChancePercent = 42U;
constexpr float kSparseIslandEmergenceThreshold = 0.14F;

struct ResourceOreDefinition {
    BlockType block = BlockType::CoalOre;
    int min_y = kWorldMinY;
    int max_y = kWorldMaxY;
    int cell_size = 16;
    std::uint32_t chance_divisor = 1U;
    int radius_squared = 3;
    int salt = 0;
};

constexpr std::array<ResourceOreDefinition, 5> kResourceOreDefinitions {{
    {BlockType::MetallicAlloyOre, 2, 14, 20, 8U, 2, 1507},
    {BlockType::DiamondOre, 3, 22, 16, 6U, 3, 1409},
    {BlockType::GoldOre, 4, 36, 14, 4U, 3, 1301},
    {BlockType::IronOre, 6, 62, 12, 3U, 3, 1201},
    {BlockType::CoalOre, 8, 82, 12, 2U, 3, 1103},
}};

auto hash_column(int x, int z, int seed) noexcept -> std::uint32_t {
    auto value = static_cast<std::uint32_t>(x) * 374761393U;
    value ^= static_cast<std::uint32_t>(z) * 668265263U;
    value ^= static_cast<std::uint32_t>(seed) * 362437U;
    value = (value ^ (value >> 13U)) * 1274126177U;
    return value ^ (value >> 16U);
}

auto hash_resource_cell(int x, int y, int z, int seed, int salt) noexcept -> std::uint32_t {
    auto value = static_cast<std::uint32_t>(x) * 374761393U;
    value ^= static_cast<std::uint32_t>(y) * 668265263U;
    value ^= static_cast<std::uint32_t>(z) * 2246822519U;
    value ^= static_cast<std::uint32_t>(seed) * 3266489917U;
    value ^= static_cast<std::uint32_t>(salt) * 1274126177U;
    value = (value ^ (value >> 15U)) * 2246822519U;
    value = (value ^ (value >> 13U)) * 3266489917U;
    return value ^ (value >> 16U);
}

auto floor_div(int value, int divisor) noexcept -> int {
    auto quotient = value / divisor;
    const auto remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        --quotient;
    }
    return quotient;
}

auto chunk_storage_index(int local_x, int y, int local_z) noexcept -> std::size_t {
    return static_cast<std::size_t>((y * kChunkSizeZ + local_z) * kChunkSizeX + local_x);
}

auto saturate(float value) noexcept -> float {
    return std::clamp(value, 0.0F, 1.0F);
}

auto smoothstep(float value) noexcept -> float {
    const auto t = saturate(value);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] auto uses_sparse_archipelago(
    WorldGenerationVersion version) noexcept -> bool {
    return version == WorldGenerationVersion::SparseArchipelagoV2 ||
           version == WorldGenerationVersion::LivingOceanV3;
}

auto hash_grid_cell(int x, int z, int seed, std::uint32_t salt) noexcept -> std::uint32_t {
    return ocean_adventure_layout_hash(x, z, seed, salt);
}

auto ellipse_island_field(int world_x,
                          int world_z,
                          std::int64_t center_x,
                          std::int64_t center_z,
                          float radius_x,
                          float radius_z) noexcept -> float {
    const auto dx = static_cast<double>(static_cast<std::int64_t>(world_x) - center_x) /
                    static_cast<double>(radius_x);
    const auto dz = static_cast<double>(static_cast<std::int64_t>(world_z) - center_z) /
                    static_cast<double>(radius_z);
    const auto normalized_distance_squared = dx * dx + dz * dz;
    if (normalized_distance_squared > 1.35) {
        return -1.0F;
    }
    return 1.0F - static_cast<float>(normalized_distance_squared);
}

auto route_island_field(int world_x, int world_z, int seed) noexcept -> float {
    if (world_z < kOceanRouteFirstIslandWindowMinZ) {
        return -1.0F;
    }

    const auto approximate_sector =
        floor_div(world_z - kOceanRouteFirstIslandWindowMinZ, kOceanRouteMacroSectorLength);
    auto field = -1.0F;
    for (int sector_offset = -1; sector_offset <= 1; ++sector_offset) {
        const auto sector = approximate_sector + sector_offset;
        if (sector < 0) {
            continue;
        }

        const auto window_start = ocean_route_island_window_start_z(seed, sector);
        const auto side = ((sector + static_cast<int>(hash_grid_cell(0, 0, seed, 103U) & 1U)) & 1) == 0
                              ? 1
                              : -1;
        const auto main_hash = hash_grid_cell(sector, 0, seed, 107U);
        const auto main_center_x = static_cast<std::int64_t>(side) *
                                   (kOceanRouteIslandCenterMinAbsX +
                                    static_cast<int>(main_hash % static_cast<std::uint32_t>(
                                                                    kOceanRouteIslandCenterMaxAbsX -
                                                                    kOceanRouteIslandCenterMinAbsX + 1)));
        const auto main_center_z = window_start + kOceanRouteIslandWindowLength / 2;
        const auto main_radius_x = 31.0F + static_cast<float>((main_hash >> 8U) % 10U);
        const auto main_radius_z = 37.0F + static_cast<float>((main_hash >> 16U) % 11U);
        field = std::max(
            field,
            ellipse_island_field(
                world_x,
                world_z,
                main_center_x,
                main_center_z,
                main_radius_x,
                main_radius_z));

        // Je garde les deux ilots satellites dans la meme fenetre longitudinale
        // pour qu'ils enrichissent une rencontre sans en creer une nouvelle.
        for (int satellite = 0; satellite < 2; ++satellite) {
            const auto satellite_hash = hash_grid_cell(sector, satellite + 1, seed, 109U);
            const auto outward_offset = 18 + static_cast<int>(satellite_hash % 15U);
            const auto center_x = main_center_x + static_cast<std::int64_t>(side * outward_offset);
            const auto center_z = window_start + 20 +
                                  static_cast<int>((satellite_hash >> 8U) %
                                                   static_cast<std::uint32_t>(
                                                       kOceanRouteIslandWindowLength - 39));
            const auto radius_x = 13.0F + static_cast<float>((satellite_hash >> 16U) % 8U);
            const auto radius_z = 14.0F + static_cast<float>((satellite_hash >> 24U) % 9U);
            field = std::max(
                field,
                ellipse_island_field(
                    world_x,
                    world_z,
                    center_x,
                    center_z,
                    radius_x,
                    radius_z));
        }
    }
    return field;
}

auto off_route_island_field(int world_x, int world_z, int seed) noexcept -> float {
    const auto absolute_x = std::abs(static_cast<std::int64_t>(world_x));
    if (absolute_x <= kOceanRouteReservedHalfWidth) {
        return -1.0F;
    }

    const auto cell_x = floor_div(world_x, kSparseArchipelagoCellSize);
    const auto cell_z = floor_div(world_z, kSparseArchipelagoCellSize);
    auto field = -1.0F;
    for (int offset_z = -1; offset_z <= 1; ++offset_z) {
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
            const auto candidate_x = cell_x + offset_x;
            const auto candidate_z = cell_z + offset_z;
            const auto placement_hash = hash_grid_cell(candidate_x, candidate_z, seed, 211U);
            if ((placement_hash % 100U) >= kSparseArchipelagoCellChancePercent) {
                continue;
            }

            const auto shape_hash = hash_grid_cell(candidate_x, candidate_z, seed, 223U);
            const auto center_x =
                static_cast<std::int64_t>(candidate_x) * kSparseArchipelagoCellSize + 10 +
                static_cast<int>(shape_hash % static_cast<std::uint32_t>(kSparseArchipelagoCellSize - 20));
            const auto center_z =
                static_cast<std::int64_t>(candidate_z) * kSparseArchipelagoCellSize + 10 +
                static_cast<int>((shape_hash >> 8U) %
                                 static_cast<std::uint32_t>(kSparseArchipelagoCellSize - 20));
            const auto radius_x = 38.0F + static_cast<float>((shape_hash >> 16U) % 17U);
            const auto radius_z = 40.0F + static_cast<float>((shape_hash >> 24U) % 19U);
            field = std::max(
                field,
                ellipse_island_field(
                    world_x,
                    world_z,
                    center_x,
                    center_z,
                    radius_x,
                    radius_z));
        }
    }

    // Je fonds la reserve de navigation dans l'archipel lointain plutot que de
    // laisser une couture verticale a sa limite.
    const auto reserve_blend = smoothstep(
        static_cast<float>(absolute_x - kOceanRouteReservedHalfWidth) / 32.0F);
    return -1.0F + (field + 1.0F) * reserve_blend;
}

auto is_resource_host_block(BlockId block_id) noexcept -> bool {
    return block_id == to_block_id(BlockType::Stone) ||
           block_id == to_block_id(BlockType::Cobblestone) ||
           block_id == to_block_id(BlockType::MossyStone);
}

auto make_noise(int seed, FastNoiseLite::NoiseType type, float frequency) -> std::unique_ptr<FastNoiseLite> {
    auto noise = std::make_unique<FastNoiseLite>(seed);
    noise->SetNoiseType(type);
    noise->SetFrequency(frequency);
    return noise;
}

} // namespace

WorldGenerator::WorldGenerator(int seed,
                               WorldGenerationProfile profile,
                               WorldGenerationVersion generation_version)
    : seed_(seed),
      profile_(profile),
      generation_version_(resolve_world_generation_version(profile, generation_version)),
      terrain_noise_(make_noise(seed, FastNoiseLite::NoiseType_OpenSimplex2, 0.0065F)),
      detail_noise_(make_noise(seed + 101, FastNoiseLite::NoiseType_Perlin, 0.018F)),
      temperature_noise_(make_noise(seed + 202, FastNoiseLite::NoiseType_OpenSimplex2, 0.0021F)),
      moisture_noise_(make_noise(seed + 257, FastNoiseLite::NoiseType_OpenSimplex2S, 0.0025F)),
      ridge_noise_(make_noise(seed + 303, FastNoiseLite::NoiseType_OpenSimplex2S, 0.009F)),
      cave_noise_(make_noise(seed + 404, FastNoiseLite::NoiseType_OpenSimplex2, 0.038F)) {
    ridge_noise_->SetFractalType(FastNoiseLite::FractalType_FBm);
    ridge_noise_->SetFractalOctaves(3);
    moisture_noise_->SetFractalType(FastNoiseLite::FractalType_FBm);
    moisture_noise_->SetFractalOctaves(2);

    if (generation_version_ != WorldGenerationVersion::LegacyV1 &&
        generation_version_ != WorldGenerationVersion::SparseArchipelagoV2 &&
        generation_version_ != WorldGenerationVersion::LivingOceanV3) {
        throw std::invalid_argument("Unknown world generation version");
    }
    if (profile_ != WorldGenerationProfile::OceanAdventure &&
        generation_version_ != WorldGenerationVersion::LegacyV1) {
        throw std::invalid_argument("Sparse archipelago generation is only valid for ocean adventures");
    }
}

WorldGenerator::~WorldGenerator() = default;

WorldGenerator::WorldGenerator(WorldGenerator&& other) noexcept = default;

auto WorldGenerator::operator=(WorldGenerator&& other) noexcept -> WorldGenerator& = default;

void WorldGenerator::generate_chunk(Chunk& chunk) const {
    auto state = begin_chunk_generation(chunk.coord());
    advance_chunk_generation(state, static_cast<std::size_t>(kChunkSizeX * kChunkSizeZ));
    chunk = std::move(state.chunk);
}

auto WorldGenerator::begin_chunk_generation(ChunkCoord coord, bool generate_decorations) const -> ChunkGenerationState {
    ChunkGenerationState state {coord, generate_decorations};
    state.resource_ore_blocks.fill(to_block_id(BlockType::Air));
    build_resource_ore_map(state);
    return state;
}

void WorldGenerator::advance_chunk_generation(ChunkGenerationState& state, std::size_t column_budget) const {
    constexpr auto column_count = static_cast<std::size_t>(kChunkSizeX * kChunkSizeZ);
    if (state.finalized || column_budget == 0U) {
        return;
    }

    const auto coord = state.chunk.coord();
    const auto base_world_x = coord.x * kChunkSizeX;
    const auto base_world_z = coord.z * kChunkSizeZ;
    auto processed_columns = std::size_t {0};

    while (state.next_column < column_count && processed_columns < column_budget) {
        const auto local_x = static_cast<int>(state.next_column % static_cast<std::size_t>(kChunkSizeX));
        const auto local_z = static_cast<int>(state.next_column / static_cast<std::size_t>(kChunkSizeX));
        const auto world_x = base_world_x + local_x;
        const auto world_z = base_world_z + local_z;
        const auto column = sample_column(world_x, world_z);
        const auto column_max_y = std::min(std::max(column.surface_height, column.water_level), kWorldMaxY);

        for (int y = kWorldMinY; y <= column_max_y; ++y) {
            auto block = choose_base_terrain_block(column, world_x, y, world_z);
            const auto ore_block = state.resource_ore_blocks[chunk_storage_index(local_x, y, local_z)];
            if (ore_block != to_block_id(BlockType::Air) &&
                is_resource_host_block(block) &&
                y <= column.surface_height - kResourceOreSurfaceMargin) {
                block = ore_block;
            }

            if (block == to_block_id(BlockType::Air)) {
                // Je reutilise la colonne deja calculee au lieu de relancer cinq bruits
                // 3D/2D pour chaque voxel d'eau de la mer.
                if (column.water_level > column.surface_height &&
                    y > column.surface_height &&
                    y <= column.water_level) {
                    state.chunk.set_water_state_local(
                        local_x,
                        y,
                        local_z,
                        make_water_state(kMaxWaterLevel, true, true));
                }
            } else {
                state.chunk.set_local(local_x, y, local_z, block);
            }
        }

        const auto column_hash = hash_column(world_x, world_z, seed_);
        const auto port_layout_reserved =
            profile_ == WorldGenerationProfile::OceanAdventure &&
            uses_sparse_archipelago(generation_version_) &&
            is_starting_port_bounds_column(world_x, world_z);
        if (state.generate_decorations && !port_layout_reserved &&
            column.water_level <= column.surface_height) {
            if (should_place_tree(column.biome, column.surface_height, column_hash)) {
                place_tree(state.chunk, local_x, column.surface_height, local_z, column.biome, column_hash);
            } else if (should_place_decoration(column.biome, column_hash)) {
                place_decoration(state.chunk, local_x, column.surface_height, local_z, column.biome, column_hash);
            }
        }

        ++state.next_column;
        ++processed_columns;
    }

    if (state.next_column == column_count) {
        finalize_ocean_navigation_corridor(state.chunk);
        state.chunk.clear_dirty();
        state.chunk.mark_dirty();
        state.finalized = true;
    }
}

auto WorldGenerator::is_chunk_generation_complete(const ChunkGenerationState& state) const noexcept -> bool {
    return state.finalized;
}

auto WorldGenerator::seed() const noexcept -> int {
    return seed_;
}

auto WorldGenerator::profile() const noexcept -> WorldGenerationProfile {
    return profile_;
}

auto WorldGenerator::generation_version() const noexcept -> WorldGenerationVersion {
    return generation_version_;
}

auto WorldGenerator::biome_at(int world_x, int world_z) const noexcept -> BiomeType {
    return sample_column(world_x, world_z).biome;
}

auto WorldGenerator::sample_surface(int world_x, int world_z) const noexcept -> TerrainSurfaceSample {
    const auto column = sample_column(world_x, world_z);
    return {
        column.biome,
        column.surface_height,
        column.water_level,
        column.surface_block,
    };
}

auto WorldGenerator::sample_block(int world_x, int y, int world_z) const noexcept -> BlockId {
    if (!is_world_y_valid(y)) {
        return to_block_id(BlockType::Air);
    }

    const auto column = sample_column(world_x, world_z);
    return choose_terrain_block(column, world_x, y, world_z);
}

auto WorldGenerator::sample_water_state(int world_x, int y, int world_z) const noexcept -> WaterState {
    if (!is_world_y_valid(y)) {
        return 0;
    }

    const auto column = sample_column(world_x, world_z);
    if (column.water_level > column.surface_height && y > column.surface_height && y <= column.water_level) {
        return make_water_state(kMaxWaterLevel, true, true);
    }
    return 0;
}

auto WorldGenerator::sample_column(int world_x, int world_z) const noexcept -> TerrainColumnSample {
    if (profile_ == WorldGenerationProfile::OceanAdventure) {
        if (uses_sparse_archipelago(generation_version_)) {
            return sample_archipelago_ocean_column(
                world_x,
                world_z,
                generation_version_ == WorldGenerationVersion::LivingOceanV3);
        }
        return sample_ocean_column(world_x, world_z);
    }

    const auto base = terrain_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
    const auto detail = detail_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
    const auto temperature = temperature_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
    const auto moisture = moisture_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
    const auto ridge = std::abs(ridge_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z)));
    const auto biome = classify_biome(temperature, moisture, ridge, base);

    TerrainColumnSample sample {};
    sample.biome = biome;
    sample.base_noise = base;
    sample.detail_noise = detail;
    sample.ridge_noise = ridge;
    sample.temperature = temperature;
    sample.moisture = moisture;
    sample.surface_height = choose_surface_height(biome, base, detail, ridge);
    sample.surface_block = choose_surface_block(biome, world_x, world_z, sample.surface_height);
    sample.filler_block = choose_filler_block(biome, world_x, world_z);
    sample.water_level = choose_water_level(sample.surface_height);
    return sample;
}

auto WorldGenerator::sample_ocean_column(int world_x, int world_z) const noexcept -> TerrainColumnSample {
    const auto base = terrain_noise_->GetNoise(static_cast<float>(world_x) * 0.42F, static_cast<float>(world_z) * 0.42F);
    const auto detail = detail_noise_->GetNoise(static_cast<float>(world_x) * 0.72F, static_cast<float>(world_z) * 0.72F);
    const auto temperature = temperature_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
    const auto moisture = moisture_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
    const auto ridge = std::abs(ridge_noise_->GetNoise(static_cast<float>(world_x) * 0.58F, static_cast<float>(world_z) * 0.58F));
    const auto island_score = base * 0.76F + detail * 0.20F + ridge * 0.44F;
    const auto island_strength = saturate((island_score - 0.42F) / 0.42F);
    const auto reef_strength = saturate((island_score - 0.27F) / 0.20F) * (1.0F - island_strength);

    TerrainColumnSample sample {};
    sample.base_noise = base;
    sample.detail_noise = detail;
    sample.ridge_noise = ridge;
    sample.temperature = temperature;
    sample.moisture = moisture;
    sample.biome = classify_biome(temperature, moisture, ridge, base);

    if (island_strength > 0.0F) {
        const auto island_height =
            static_cast<int>(std::round(static_cast<float>(kSeaLevel - 2) +
                                        island_strength * 29.0F +
                                        detail * 4.5F +
                                        ridge * island_strength * 8.0F));
        sample.surface_height = std::clamp(island_height, kSeaLevel - 2, kWorldMaxY - 6);
    } else {
        const auto seabed =
            static_cast<int>(std::round(33.0F + base * 4.0F + detail * 2.5F + reef_strength * 12.0F));
        sample.surface_height = std::clamp(seabed, 24, kSeaLevel - 1);
    }

    if (is_ocean_navigation_corridor_column(world_x, world_z)) {
        // Je borne le fond avant le choix des materiaux et de l'eau: le point
        // sampling, la generation incrementale et la restauration des saves
        // partagent ainsi exactement le meme terrain de reference.
        sample.surface_height = std::min(sample.surface_height, kOceanNavigationCorridorMaxSeabedY);
    }

    if (sample.surface_height <= kSeaLevel + 2) {
        sample.surface_block = to_block_id(BlockType::Sand);
        sample.filler_block = to_block_id(BlockType::Sand);
    } else {
        sample.surface_block = choose_surface_block(sample.biome, world_x, world_z, sample.surface_height);
        sample.filler_block = choose_filler_block(sample.biome, world_x, world_z);
    }
    sample.water_level = choose_water_level(sample.surface_height);
    return sample;
}

auto WorldGenerator::sample_archipelago_ocean_column(
    int world_x,
    int world_z,
    bool living_seabed) const noexcept -> TerrainColumnSample {
    // Je reserve les bruits lents a la matiere et au relief interne. La
    // repartition geographique vient des cellules et macro-secteurs ci-dessus,
    // ce qui empeche un bruit rapide de semer des iles tout le long du trajet.
    const auto base = terrain_noise_->GetNoise(
        static_cast<float>(world_x) * 0.18F,
        static_cast<float>(world_z) * 0.18F);
    const auto detail = detail_noise_->GetNoise(
        static_cast<float>(world_x) * 0.58F,
        static_cast<float>(world_z) * 0.58F);
    const auto temperature = temperature_noise_->GetNoise(
        static_cast<float>(world_x),
        static_cast<float>(world_z));
    const auto moisture = moisture_noise_->GetNoise(
        static_cast<float>(world_x),
        static_cast<float>(world_z));
    const auto ridge = std::abs(ridge_noise_->GetNoise(
        static_cast<float>(world_x) * 0.34F,
        static_cast<float>(world_z) * 0.34F));

    TerrainColumnSample sample {};
    sample.base_noise = base;
    sample.detail_noise = detail;
    sample.ridge_noise = ridge;
    sample.temperature = temperature;
    sample.moisture = moisture;
    sample.biome = classify_biome(temperature, moisture, ridge, base);

    if (is_starting_port_terrain_foundation_column(world_x, world_z)) {
        sample.surface_height = kStartingPortSurfaceY;
        if (is_starting_port_quay_foundation_column(world_x, world_z)) {
            sample.surface_block = to_block_id(BlockType::Stone);
            sample.filler_block = to_block_id(BlockType::Stone);
        } else {
            sample.biome = BiomeType::Meadow;
            sample.surface_block = to_block_id(BlockType::Grass);
            sample.filler_block = to_block_id(BlockType::Dirt);
        }
        sample.water_level = kWorldMinY - 1;
        return sample;
    }

    auto base_seabed = std::clamp(
        static_cast<int>(std::round(33.0F + base * 3.2F + detail * 1.6F)),
        27,
        kSeaLevel - 5);

    if (is_starting_port_basin_column(world_x, world_z)) {
        sample.surface_height = std::min(base_seabed, kOceanNavigationCorridorMaxSeabedY);
        sample.surface_block = to_block_id(BlockType::Sand);
        sample.filler_block = to_block_id(BlockType::Sand);
        sample.water_level = kSeaLevel;
        return sample;
    }

    if (living_seabed) {
        // Je garde le bassin du port sur son ancien plateau, puis je creuse
        // seulement la pleine mer. Les mêmes bruits suffisent à former des
        // bassins, des dorsales et des ravines sans coût de sampling en plus.
        base_seabed = std::clamp(
            static_cast<int>(std::round(
                24.0F +
                base * 7.0F +
                detail * 4.0F -
                ridge * 5.0F)),
            kLivingOceanMinimumSeabedY,
            kLivingOceanMaximumDeepSeabedY);
    }

    auto island_field = std::max(
        route_island_field(world_x, world_z, seed_),
        off_route_island_field(world_x, world_z, seed_));
    if (island_field > -0.95F) {
        island_field += base * 0.07F + detail * 0.10F + ridge * 0.04F;
    }

    const auto island_strength =
        saturate((island_field - kSparseIslandEmergenceThreshold) / 0.72F);
    const auto reef_strength =
        saturate((island_field + 0.08F) / (kSparseIslandEmergenceThreshold + 0.08F)) *
        (1.0F - island_strength);
    if (island_strength > 0.0F) {
        const auto island_height = static_cast<int>(std::round(
            static_cast<float>(kSeaLevel) + island_strength * 23.0F +
            std::max(detail, 0.0F) * 2.5F + ridge * island_strength * 3.5F));
        sample.surface_height = std::clamp(island_height, kSeaLevel, kWorldMaxY - 6);
    } else {
        const auto seabed = static_cast<int>(std::round(
            static_cast<float>(base_seabed) + reef_strength * 12.0F));
        sample.surface_height = std::clamp(
            seabed,
            living_seabed ? kLivingOceanMinimumSeabedY : 24,
            kSeaLevel - 1);
    }

    const auto absolute_x = std::abs(static_cast<std::int64_t>(world_x));
    if (world_z >= kOceanNavigationCorridorStartZ &&
        absolute_x < kOceanNavigationTransitionOuterHalfWidth) {
        const auto deep_surface = std::min(base_seabed, kOceanNavigationCorridorMaxSeabedY);
        const auto corridor_blend = smoothstep(
            static_cast<float>(absolute_x - kOceanNavigationCorridorHalfWidth) /
            static_cast<float>(kOceanNavigationTransitionOuterHalfWidth -
                               kOceanNavigationCorridorHalfWidth));
        sample.surface_height = static_cast<int>(std::round(
            static_cast<float>(deep_surface) +
            static_cast<float>(sample.surface_height - deep_surface) * corridor_blend));

        if (absolute_x <= kOceanNavigationCorridorHalfWidth) {
            sample.surface_height = std::min(
                sample.surface_height,
                kOceanNavigationCorridorMaxSeabedY);
        }
        if (absolute_x < kOceanNaturalLandExclusionHalfWidth) {
            sample.surface_height = std::min(sample.surface_height, kSeaLevel - 1);
        }
    }

    if (sample.surface_height <= kSeaLevel + 2) {
        if (living_seabed && sample.surface_height < kSeaLevel) {
            const auto material_hash =
                ocean_adventure_layout_hash(
                    world_x,
                    world_z,
                    seed_,
                    0x4D41544CU);
            const auto water_depth =
                kSeaLevel - sample.surface_height;
            if (water_depth >= 23) {
                sample.surface_block =
                    (material_hash % 5U) == 0U
                        ? to_block_id(BlockType::MossyStone)
                        : to_block_id(BlockType::Gravel);
                sample.filler_block = to_block_id(BlockType::Stone);
            } else if (water_depth >= 13 &&
                       (material_hash % 4U) == 0U) {
                sample.surface_block =
                    to_block_id(BlockType::MossyStone);
                sample.filler_block =
                    to_block_id(BlockType::Gravel);
            } else {
                sample.surface_block = to_block_id(BlockType::Sand);
                sample.filler_block =
                    (material_hash % 7U) == 0U
                        ? to_block_id(BlockType::Gravel)
                        : to_block_id(BlockType::Sand);
            }
        } else {
            sample.surface_block = to_block_id(BlockType::Sand);
            sample.filler_block = to_block_id(BlockType::Sand);
        }
    } else {
        sample.surface_block = choose_surface_block(
            sample.biome,
            world_x,
            world_z,
            sample.surface_height);
        sample.filler_block = choose_filler_block(sample.biome, world_x, world_z);
    }
    sample.water_level = choose_water_level(sample.surface_height);
    return sample;
}

void WorldGenerator::finalize_ocean_navigation_corridor(Chunk& chunk) const {
    if (profile_ != WorldGenerationProfile::OceanAdventure) {
        return;
    }

    const auto coord = chunk.coord();
    const auto base_world_x = coord.x * kChunkSizeX;
    const auto base_world_z = coord.z * kChunkSizeZ;
    if (uses_sparse_archipelago(generation_version_)) {
        for (int local_z = 0; local_z < kChunkSizeZ; ++local_z) {
            const auto world_z = base_world_z + local_z;
            if (world_z < kOceanNavigationCorridorStartZ) {
                continue;
            }
            for (int local_x = 0; local_x < kChunkSizeX; ++local_x) {
                const auto world_x = base_world_x + local_x;
                if (std::abs(static_cast<std::int64_t>(world_x)) >=
                        kOceanNaturalLandExclusionHalfWidth ||
                    is_starting_port_terrain_foundation_column(world_x, world_z)) {
                    continue;
                }

                const auto column = sample_archipelago_ocean_column(
                    world_x,
                    world_z,
                    generation_version_ ==
                        WorldGenerationVersion::LivingOceanV3);
                for (int y = column.surface_height + 1; y <= kWorldMaxY; ++y) {
                    const auto expected_water =
                        y <= column.water_level
                            ? make_water_state(kMaxWaterLevel, true, true)
                            : WaterState {0};
                    if (chunk.get_local(local_x, y, local_z) != to_block_id(BlockType::Air)) {
                        chunk.set_local(local_x, y, local_z, to_block_id(BlockType::Air));
                    }
                    if (chunk.get_water_state_local(local_x, y, local_z) != expected_water) {
                        chunk.set_water_state_local(local_x, y, local_z, expected_water);
                    }
                }
            }
        }
        return;
    }

    for (int local_z = 0; local_z < kChunkSizeZ; ++local_z) {
        const auto world_z = base_world_z + local_z;
        for (int local_x = 0; local_x < kChunkSizeX; ++local_x) {
            const auto world_x = base_world_x + local_x;
            if (!is_ocean_navigation_corridor_column(world_x, world_z)) {
                continue;
            }

            const auto corridor_min_x =
                kOceanNavigationCorridorCenterX - kOceanNavigationCorridorHalfWidth;
            const auto corridor_max_x =
                kOceanNavigationCorridorCenterX + kOceanNavigationCorridorHalfWidth;
            const auto can_receive_external_decoration =
                world_x < corridor_min_x + kNavigationDecorationOverhang ||
                world_x > corridor_max_x - kNavigationDecorationOverhang ||
                world_z < kOceanNavigationCorridorStartZ + kNavigationDecorationOverhang;
            if (!can_receive_external_decoration) {
                continue;
            }

            // Une decoration issue d'une colonne voisine peut deborder apres
            // le passage de cette colonne. Je ne rescane que les deux blocs
            // interieurs exposes, soit le rayon maximal des canopees.
            for (int y = kOceanNavigationCorridorMaxSeabedY + 1; y <= kWorldMaxY; ++y) {
                const auto expected_water =
                    y <= kSeaLevel ? make_water_state(kMaxWaterLevel, true, true) : WaterState {0};
                if (chunk.get_local(local_x, y, local_z) != to_block_id(BlockType::Air)) {
                    chunk.set_local(local_x, y, local_z, to_block_id(BlockType::Air));
                }
                if (chunk.get_water_state_local(local_x, y, local_z) != expected_water) {
                    chunk.set_water_state_local(local_x, y, local_z, expected_water);
                }
            }
        }
    }
}

auto WorldGenerator::classify_biome(float temperature, float moisture, float ridge_noise, float base_noise) const noexcept -> BiomeType {
    if (ridge_noise > 0.66F && base_noise > 0.02F) {
        return BiomeType::RockyPeaks;
    }
    if (temperature < -0.24F) {
        return BiomeType::Taiga;
    }
    if (temperature > 0.34F && moisture < -0.06F) {
        return BiomeType::Desert;
    }
    if (moisture > 0.20F) {
        return BiomeType::Forest;
    }
    return BiomeType::Meadow;
}

auto WorldGenerator::choose_surface_block(BiomeType biome, int world_x, int world_z, int surface_height) const noexcept -> BlockId {
    const auto column_hash = hash_column(world_x, world_z, seed_);
    switch (biome) {
    case BiomeType::Desert:
        return to_block_id(BlockType::Sand);
    case BiomeType::RockyPeaks:
        if (surface_height > 78 && (column_hash % 3U) != 0U) {
            return to_block_id(BlockType::Snow);
        }
        switch (column_hash % 4U) {
        case 0U:
            return to_block_id(BlockType::Stone);
        case 1U:
            return to_block_id(BlockType::Cobblestone);
        case 2U:
            return to_block_id(BlockType::Gravel);
        default:
            return to_block_id(BlockType::MossyStone);
        }
    case BiomeType::Taiga:
        return surface_height > 60 ? to_block_id(BlockType::Snow) : to_block_id(BlockType::Grass);
    case BiomeType::Forest:
        return surface_height <= kSeaLevel + 1 ? to_block_id(BlockType::Sand) : to_block_id(BlockType::Grass);
    case BiomeType::Meadow:
        return surface_height <= kSeaLevel + 1 ? to_block_id(BlockType::Sand) : to_block_id(BlockType::Grass);
    default:
        return to_block_id(BlockType::Grass);
    }
}

auto WorldGenerator::choose_filler_block(BiomeType biome, int world_x, int world_z) const noexcept -> BlockId {
    const auto column_hash = hash_column(world_x, world_z, seed_ + 61);
    switch (biome) {
    case BiomeType::Desert:
        return to_block_id(BlockType::Sand);
    case BiomeType::RockyPeaks:
        return (column_hash % 3U) == 0U ? to_block_id(BlockType::Gravel) : to_block_id(BlockType::Stone);
    case BiomeType::Taiga:
    case BiomeType::Forest:
    case BiomeType::Meadow:
    default:
        return to_block_id(BlockType::Dirt);
    }
}

auto WorldGenerator::choose_surface_height(BiomeType biome, float base_noise, float detail_noise, float ridge_noise) const noexcept -> int {
    float height = 50.0F + base_noise * 9.0F + detail_noise * 3.5F;
    switch (biome) {
    case BiomeType::Meadow:
        height = 50.0F + base_noise * 8.0F + detail_noise * 4.0F;
        break;
    case BiomeType::Forest:
        height = 53.0F + base_noise * 8.5F + detail_noise * 5.0F;
        break;
    case BiomeType::Desert:
        height = 46.0F + base_noise * 4.0F + detail_noise * 2.0F + ridge_noise * 3.0F;
        break;
    case BiomeType::RockyPeaks:
        height = 61.0F + base_noise * 14.0F + ridge_noise * 18.0F + detail_noise * 3.0F;
        break;
    case BiomeType::Taiga:
        height = 55.0F + base_noise * 9.0F + detail_noise * 4.5F + ridge_noise * 5.0F;
        break;
    }
    const auto rounded = static_cast<int>(std::round(height));
    return std::clamp(rounded, kBaseStoneHeight, kWorldMaxY - 6);
}

auto WorldGenerator::choose_water_level(int surface_height) const noexcept -> int {
    // La ligne d'eau doit rester globale et independante du biome. Sinon deux
    // colonnes voisines situees sous le niveau de la mer peuvent recevoir des
    // decisions contradictoires, ce qui cree des falaises d'eau et des mers
    // coupees net aux frontieres de biome.
    return surface_height < kSeaLevel ? kSeaLevel : (kWorldMinY - 1);
}

auto WorldGenerator::choose_terrain_block(const TerrainColumnSample& column, int world_x, int y, int world_z) const noexcept -> BlockId {
    const auto base_block = choose_base_terrain_block(column, world_x, y, world_z);
    return choose_resource_ore_block(column, base_block, world_x, y, world_z);
}

auto WorldGenerator::choose_base_terrain_block(const TerrainColumnSample& column,
                                               int world_x,
                                               int y,
                                               int world_z) const noexcept -> BlockId {
    if (y <= column.surface_height) {
        auto block = to_block_id(BlockType::Stone);
        if (column.biome == BiomeType::RockyPeaks && y >= column.surface_height - 7 && y < column.surface_height - 2) {
            const auto layer_hash = hash_column(world_x, world_z + y, seed_);
            switch (layer_hash % 5U) {
            case 0U:
                block = to_block_id(BlockType::Gravel);
                break;
            case 1U:
                block = to_block_id(BlockType::Cobblestone);
                break;
            case 2U:
                block = to_block_id(BlockType::MossyStone);
                break;
            default:
                block = to_block_id(BlockType::Stone);
                break;
            }
        }
        if (y == column.surface_height) {
            block = column.surface_block;
        } else if (y >= column.surface_height - 3) {
            block = column.filler_block;
        }

        if (profile_ == WorldGenerationProfile::OceanAdventure &&
            uses_sparse_archipelago(generation_version_) &&
            is_starting_port_terrain_foundation_column(world_x, world_z)) {
            return block;
        }

        const auto cave_noise = cave_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(y), static_cast<float>(world_z));
        if (y > 6 && y < column.surface_height - 4 && cave_noise > 0.58F) {
            return to_block_id(BlockType::Air);
        }

        return block;
    }

    return to_block_id(BlockType::Air);
}

void WorldGenerator::build_resource_ore_map(ChunkGenerationState& state) const {
    const auto coord = state.chunk.coord();
    const auto min_world_x = coord.x * kChunkSizeX;
    const auto max_world_x = min_world_x + kChunkSizeX - 1;
    const auto min_world_z = coord.z * kChunkSizeZ;
    const auto max_world_z = min_world_z + kChunkSizeZ - 1;

    // Je rasterise chaque petit filon une seule fois dans le chunk. Le test de
    // priorite conserve l'ordre historique (alliage vers charbon), mais evite
    // de rehacher 27 cellules voisines pour chacun des 32 768 voxels.
    for (const auto& definition : kResourceOreDefinitions) {
        const auto axis_radius = static_cast<int>(std::floor(std::sqrt(static_cast<float>(definition.radius_squared))));
        const auto min_cell_x = floor_div(min_world_x - axis_radius, definition.cell_size);
        const auto max_cell_x = floor_div(max_world_x + axis_radius, definition.cell_size);
        const auto min_cell_y = floor_div(definition.min_y - axis_radius, definition.cell_size);
        const auto max_cell_y = floor_div(definition.max_y + axis_radius, definition.cell_size);
        const auto min_cell_z = floor_div(min_world_z - axis_radius, definition.cell_size);
        const auto max_cell_z = floor_div(max_world_z + axis_radius, definition.cell_size);
        const auto cell_size = static_cast<std::uint32_t>(definition.cell_size);

        for (int cell_z = min_cell_z; cell_z <= max_cell_z; ++cell_z) {
            for (int cell_y = min_cell_y; cell_y <= max_cell_y; ++cell_y) {
                for (int cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x) {
                    const auto vein_hash = hash_resource_cell(cell_x, cell_y, cell_z, seed_, definition.salt);
                    if ((vein_hash % definition.chance_divisor) != 0U) {
                        continue;
                    }

                    const auto center_x =
                        cell_x * definition.cell_size +
                        static_cast<int>(hash_resource_cell(cell_x, cell_y, cell_z, seed_, definition.salt + 11) % cell_size);
                    const auto center_y =
                        cell_y * definition.cell_size +
                        static_cast<int>(hash_resource_cell(cell_x, cell_y, cell_z, seed_, definition.salt + 23) % cell_size);
                    const auto center_z =
                        cell_z * definition.cell_size +
                        static_cast<int>(hash_resource_cell(cell_x, cell_y, cell_z, seed_, definition.salt + 37) % cell_size);

                    for (int offset_z = -axis_radius; offset_z <= axis_radius; ++offset_z) {
                        for (int offset_y = -axis_radius; offset_y <= axis_radius; ++offset_y) {
                            for (int offset_x = -axis_radius; offset_x <= axis_radius; ++offset_x) {
                                const auto distance_squared =
                                    offset_x * offset_x + offset_y * offset_y + offset_z * offset_z;
                                if (distance_squared > definition.radius_squared) {
                                    continue;
                                }

                                const auto world_x = center_x + offset_x;
                                const auto y = center_y + offset_y;
                                const auto world_z = center_z + offset_z;
                                if (world_x < min_world_x || world_x > max_world_x ||
                                    world_z < min_world_z || world_z > max_world_z ||
                                    y < definition.min_y || y > definition.max_y ||
                                    !is_world_y_valid(y)) {
                                    continue;
                                }

                                if (distance_squared == definition.radius_squared) {
                                    const auto edge_hash =
                                        hash_resource_cell(world_x, y, world_z, seed_, definition.salt + 53);
                                    if ((edge_hash % 100U) > 58U) {
                                        continue;
                                    }
                                }

                                const auto local_x = world_x - min_world_x;
                                const auto local_z = world_z - min_world_z;
                                auto& ore_block = state.resource_ore_blocks[chunk_storage_index(local_x, y, local_z)];
                                if (ore_block == to_block_id(BlockType::Air)) {
                                    ore_block = to_block_id(definition.block);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

auto WorldGenerator::choose_resource_ore_block(const TerrainColumnSample& column,
                                               BlockId base_block,
                                               int world_x,
                                               int y,
                                               int world_z) const noexcept -> BlockId {
    if (!is_resource_host_block(base_block) || y > column.surface_height - kResourceOreSurfaceMargin) {
        return base_block;
    }

    for (const auto& definition : kResourceOreDefinitions) {
        if (y < definition.min_y || y > std::min(definition.max_y, column.surface_height - kResourceOreSurfaceMargin)) {
            continue;
        }

        const auto cell_x = floor_div(world_x, definition.cell_size);
        const auto cell_y = floor_div(y, definition.cell_size);
        const auto cell_z = floor_div(world_z, definition.cell_size);
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto candidate_cell_x = cell_x + dx;
                    const auto candidate_cell_y = cell_y + dy;
                    const auto candidate_cell_z = cell_z + dz;
                    const auto vein_hash =
                        hash_resource_cell(candidate_cell_x, candidate_cell_y, candidate_cell_z, seed_, definition.salt);
                    if ((vein_hash % definition.chance_divisor) != 0U) {
                        continue;
                    }

                    const auto cell_size = static_cast<std::uint32_t>(definition.cell_size);
                    const auto center_x =
                        candidate_cell_x * definition.cell_size +
                        static_cast<int>(hash_resource_cell(candidate_cell_x, candidate_cell_y, candidate_cell_z, seed_, definition.salt + 11) % cell_size);
                    const auto center_y =
                        candidate_cell_y * definition.cell_size +
                        static_cast<int>(hash_resource_cell(candidate_cell_x, candidate_cell_y, candidate_cell_z, seed_, definition.salt + 23) % cell_size);
                    const auto center_z =
                        candidate_cell_z * definition.cell_size +
                        static_cast<int>(hash_resource_cell(candidate_cell_x, candidate_cell_y, candidate_cell_z, seed_, definition.salt + 37) % cell_size);

                    const auto offset_x = world_x - center_x;
                    const auto offset_y = y - center_y;
                    const auto offset_z = world_z - center_z;
                    const auto distance_squared =
                        offset_x * offset_x + offset_y * offset_y + offset_z * offset_z;
                    if (distance_squared > definition.radius_squared) {
                        continue;
                    }

                    const auto edge_hash =
                        hash_resource_cell(world_x, y, world_z, seed_, definition.salt + 53);
                    // Je casse legerement le bord des filons pour eviter des boules trop regulieres.
                    if (distance_squared == definition.radius_squared && (edge_hash % 100U) > 58U) {
                        continue;
                    }
                    return to_block_id(definition.block);
                }
            }
        }
    }

    return base_block;
}

auto WorldGenerator::should_place_tree(BiomeType biome, int surface_y, std::uint32_t column_hash) const noexcept -> bool {
    if (surface_y < 48 || surface_y > kWorldMaxY - 10) {
        return false;
    }
    switch (biome) {
    case BiomeType::Forest:
        return (column_hash % kForestTreePlacementModulo) == 0U;
    case BiomeType::Meadow:
        return (column_hash % kMeadowTreePlacementModulo) == 0U;
    case BiomeType::Taiga:
        return (column_hash % kTaigaTreePlacementModulo) == 0U;
    case BiomeType::Desert:
    case BiomeType::RockyPeaks:
    default:
        return false;
    }
}

auto WorldGenerator::should_place_decoration(BiomeType biome, std::uint32_t column_hash) const noexcept -> bool {
    switch (biome) {
    case BiomeType::Meadow:
        return (column_hash % kMeadowDecorationPlacementModulo) == 0U;
    case BiomeType::Forest:
        return (column_hash % kForestDecorationPlacementModulo) == 0U;
    case BiomeType::Desert:
        return (column_hash % kDesertDecorationPlacementModulo) == 0U;
    case BiomeType::Taiga:
        return (column_hash % kTaigaDecorationPlacementModulo) == 0U;
    case BiomeType::RockyPeaks:
        return (column_hash % kRockyPeaksDecorationPlacementModulo) == 0U;
    default:
        return false;
    }
}

void WorldGenerator::place_tree(Chunk& chunk, int local_x, int surface_y, int local_z, BiomeType biome, std::uint32_t column_hash) const {
    switch (biome) {
    case BiomeType::Taiga:
        place_pine_tree(chunk, local_x, surface_y, local_z, column_hash);
        break;
    case BiomeType::Forest:
    case BiomeType::Meadow:
        place_oak_tree(chunk, local_x, surface_y, local_z, column_hash);
        break;
    case BiomeType::Desert:
    case BiomeType::RockyPeaks:
    default:
        break;
    }
}

void WorldGenerator::place_decoration(Chunk& chunk,
                                      int local_x,
                                      int surface_y,
                                      int local_z,
                                      BiomeType biome,
                                      std::uint32_t column_hash) const {
    if (!chunk.in_bounds_local(local_x, surface_y + 1, local_z)) {
        return;
    }
    if (chunk.get_local(local_x, surface_y + 1, local_z) != to_block_id(BlockType::Air)) {
        return;
    }

    const auto surface_block = chunk.get_local(local_x, surface_y, local_z);
    BlockId decoration = to_block_id(BlockType::Air);
    switch (biome) {
    case BiomeType::Meadow:
        if (surface_block == to_block_id(BlockType::Sand)) {
            return;
        }
        if ((column_hash % 23U) == 0U) {
            decoration = to_block_id(BlockType::RedFlower);
        } else if ((column_hash % 29U) == 0U) {
            decoration = to_block_id(BlockType::YellowFlower);
        } else {
            decoration = to_block_id(BlockType::TallGrass);
        }
        break;
    case BiomeType::Forest:
        if (surface_block == to_block_id(BlockType::Sand)) {
            return;
        }
        if ((column_hash % 37U) == 0U) {
            decoration = to_block_id(BlockType::YellowFlower);
        } else {
            decoration = to_block_id(BlockType::TallGrass);
        }
        break;
    case BiomeType::Desert:
        if ((column_hash % 31U) == 0U) {
            place_cactus(chunk, local_x, surface_y, local_z, column_hash);
            return;
        }
        decoration = to_block_id(BlockType::DeadShrub);
        break;
    case BiomeType::Taiga:
        decoration = (column_hash % 4U) == 0U ? to_block_id(BlockType::TallGrass) : to_block_id(BlockType::DeadShrub);
        break;
    case BiomeType::RockyPeaks:
        if ((column_hash % 3U) == 0U) {
            decoration = to_block_id(BlockType::DeadShrub);
        }
        break;
    default:
        break;
    }

    if (decoration != to_block_id(BlockType::Air)) {
        chunk.set_local(local_x, surface_y + 1, local_z, decoration);
    }
}

void WorldGenerator::place_oak_tree(Chunk& chunk, int local_x, int surface_y, int local_z, std::uint32_t column_hash) const {
    if (local_x < 2 || local_x > kChunkSizeX - 3 || local_z < 2 || local_z > kChunkSizeZ - 3) {
        return;
    }

    const auto trunk_base = surface_y + 1;
    const auto trunk_height = 4 + static_cast<int>(column_hash % 2U);
    if (trunk_base + trunk_height + 3 >= kChunkHeight) {
        return;
    }

    for (int y = 0; y < trunk_height; ++y) {
        chunk.set_local(local_x, trunk_base + y, local_z, to_block_id(BlockType::Wood));
    }

    const auto canopy_center_y = trunk_base + trunk_height - 1;
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dy = -1; dy <= 2; ++dy) {
                const auto distance = std::abs(dx) + std::abs(dz) + std::abs(dy);
                if (distance > 4 || (std::abs(dx) == 2 && std::abs(dz) == 2 && dy > 0)) {
                    continue;
                }

                const auto x = local_x + dx;
                const auto y = canopy_center_y + dy;
                const auto z = local_z + dz;
                if (!chunk.in_bounds_local(x, y, z)) {
                    continue;
                }
                if (chunk.get_local(x, y, z) == to_block_id(BlockType::Air)) {
                    chunk.set_local(x, y, z, to_block_id(BlockType::Leaves));
                }
            }
        }
    }
}

void WorldGenerator::place_pine_tree(Chunk& chunk, int local_x, int surface_y, int local_z, std::uint32_t column_hash) const {
    if (local_x < 3 || local_x > kChunkSizeX - 4 || local_z < 3 || local_z > kChunkSizeZ - 4) {
        return;
    }

    const auto trunk_base = surface_y + 1;
    const auto trunk_height = 5 + static_cast<int>(column_hash % 3U);
    if (trunk_base + trunk_height + 3 >= kChunkHeight) {
        return;
    }

    for (int y = 0; y < trunk_height; ++y) {
        chunk.set_local(local_x, trunk_base + y, local_z, to_block_id(BlockType::PineWood));
    }

    for (int level = 0; level < 4; ++level) {
        const auto radius = 2 - level / 2;
        const auto canopy_y = trunk_base + trunk_height - 1 - level;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) + std::abs(dz) > radius + 1) {
                    continue;
                }

                const auto x = local_x + dx;
                const auto z = local_z + dz;
                if (!chunk.in_bounds_local(x, canopy_y, z)) {
                    continue;
                }
                if (chunk.get_local(x, canopy_y, z) == to_block_id(BlockType::Air)) {
                    chunk.set_local(x, canopy_y, z, to_block_id(BlockType::PineLeaves));
                }
            }
        }
    }

    if (chunk.in_bounds_local(local_x, trunk_base + trunk_height, local_z)) {
        chunk.set_local(local_x, trunk_base + trunk_height, local_z, to_block_id(BlockType::PineLeaves));
    }
}

void WorldGenerator::place_cactus(Chunk& chunk, int local_x, int surface_y, int local_z, std::uint32_t column_hash) const {
    if (local_x < 1 || local_x > kChunkSizeX - 2 || local_z < 1 || local_z > kChunkSizeZ - 2) {
        return;
    }
    const auto height = 2 + static_cast<int>(column_hash % 2U);
    if (surface_y + height >= kChunkHeight) {
        return;
    }
    for (int y = 1; y <= height; ++y) {
        if (chunk.get_local(local_x, surface_y + y, local_z) != to_block_id(BlockType::Air)) {
            return;
        }
    }
    for (int y = 1; y <= height; ++y) {
        chunk.set_local(local_x, surface_y + y, local_z, to_block_id(BlockType::Cactus));
    }
}

} // namespace valcraft
