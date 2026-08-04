#include "world/BackroomsGenerator.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace valcraft {

namespace {

constexpr int kPortalMinimumCenter = 8;
constexpr int kPortalMaximumCenter = kBackroomsModuleSize - 9;
constexpr int kRouteHalfWidth = 2;
constexpr int kHubHalfWidth = 4;
constexpr int kConnectorRoomHalfWidth = 3;
constexpr int kPoolroomsMinimumCeilingHeight = 8;
constexpr std::uint8_t kPoolroomsShallowWaterLevel = 5U;
constexpr int kPoolroomsBasinCellSize = 32;
constexpr int kPoolroomsRouteShoreWidth = 4;
constexpr int kFloodedDistrictModules = 4;
constexpr int kFloodedDistrictCoreFirstModule = 1;
constexpr int kFloodedDistrictCoreLastModule = 2;
constexpr int kFloodedTransitionWidth = 2;
constexpr int kFloodedDeepPoolRouteMargin = 2;
constexpr int kFloodedDeepPoolConnectorMargin = 8;

[[nodiscard]] constexpr auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] auto hash_position(
    int seed,
    std::int64_t x,
    std::int64_t z,
    std::uint64_t salt) noexcept -> std::uint32_t {

    auto value = mix64(
        static_cast<std::uint64_t>(static_cast<std::int64_t>(seed)) ^
        salt);
    value ^= mix64(static_cast<std::uint64_t>(x) + 0x632BE59BD9B4E019ULL);
    value ^= mix64(static_cast<std::uint64_t>(z) + 0x8CB92BA72F3D8DD7ULL);
    return static_cast<std::uint32_t>(mix64(value) >> 32U);
}

[[nodiscard]] constexpr auto floor_division(int value, int divisor) noexcept -> int {
    auto quotient = value / divisor;
    const auto remainder = value % divisor;
    if (remainder < 0) {
        --quotient;
    }
    return quotient;
}

[[nodiscard]] constexpr auto positive_modulo(int value, int divisor) noexcept -> int {
    const auto remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

[[nodiscard]] constexpr auto fits_in_int(std::int64_t value) noexcept -> bool {
    return value >=
               static_cast<std::int64_t>(std::numeric_limits<int>::lowest()) &&
           value <=
               static_cast<std::int64_t>(std::numeric_limits<int>::max());
}

[[nodiscard]] constexpr auto checked_int(std::int64_t value) noexcept
    -> std::optional<int> {
    if (!fits_in_int(value)) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

[[nodiscard]] constexpr auto floor_division64(
    std::int64_t value,
    std::int64_t divisor) noexcept -> std::int64_t {
    auto quotient = value / divisor;
    if (value % divisor < 0) {
        --quotient;
    }
    return quotient;
}

[[nodiscard]] constexpr auto positive_modulo64(
    std::int64_t value,
    int divisor) noexcept -> int {
    const auto remainder = value % static_cast<std::int64_t>(divisor);
    return static_cast<int>(
        remainder < 0 ? remainder + divisor : remainder);
}

struct BackroomsFixturePattern {
    int nominal_anchor_x = 0;
    int nominal_anchor_z = 0;
    int nominal_length = kBackroomsLightFixtureNominalLength;
};

[[nodiscard]] auto backrooms_fixture_pattern_at(
    const BackroomsModuleDescriptor& descriptor,
    int layout_seed,
    int world_x,
    int world_z,
    int local_x,
    int local_z,
    bool wall) noexcept -> std::optional<BackroomsFixturePattern> {
    if (wall) {
        return std::nullopt;
    }

    auto row_spacing = 9;
    auto strip_period = 11;
    if (descriptor.archetype == BackroomsArchetype::GrandHall) {
        row_spacing = 13;
        strip_period = 15;
    } else if (
        descriptor.archetype == BackroomsArchetype::CompressionMaze) {
        row_spacing = 7;
        strip_period = 9;
    }

    const auto phase =
        static_cast<int>(
            hash_position(
                layout_seed,
                descriptor.module_x,
                descriptor.module_z,
                0x1B8E6F45ULL) % 17U);
    const auto strip_offset =
        descriptor.primary_axis_x
            ? positive_modulo64(
                  static_cast<std::int64_t>(local_x) + phase * 2,
                  strip_period)
            : positive_modulo64(
                  static_cast<std::int64_t>(local_z) + phase * 2,
                  strip_period);
    const auto on_fixture_row =
        descriptor.primary_axis_x
            ? positive_modulo64(
                  static_cast<std::int64_t>(local_z) + phase,
                  row_spacing) == 2
            : positive_modulo64(
                  static_cast<std::int64_t>(local_x) + phase,
                  row_spacing) == 2;
    if (!on_fixture_row || strip_offset > 3) {
        return std::nullopt;
    }

    const auto nominal_anchor_x = checked_int(
        static_cast<std::int64_t>(world_x) -
        (descriptor.primary_axis_x ? strip_offset : 0));
    const auto nominal_anchor_z = checked_int(
        static_cast<std::int64_t>(world_z) -
        (descriptor.primary_axis_x ? 0 : strip_offset));
    if (!nominal_anchor_x.has_value() ||
        !nominal_anchor_z.has_value()) {
        return std::nullopt;
    }

    return BackroomsFixturePattern {
        .nominal_anchor_x = *nominal_anchor_x,
        .nominal_anchor_z = *nominal_anchor_z,
    };
}

[[nodiscard]] auto flooded_module_for_layout(
    int layout_seed,
    std::int64_t module_x,
    std::int64_t module_z) noexcept -> bool {

    const auto district_x =
        floor_division64(module_x, kFloodedDistrictModules);
    const auto district_z =
        floor_division64(module_z, kFloodedDistrictModules);

    // Je protege tout le macro-district d'apparition : le joueur dispose ainsi
    // toujours d'une vraie zone de lecture avant sa premiere nappe inondee.
    if (district_x == 0 && district_z == 0) {
        return false;
    }

    const auto local_module_x =
        positive_modulo64(module_x, kFloodedDistrictModules);
    const auto local_module_z =
        positive_modulo64(module_z, kFloodedDistrictModules);
    if (local_module_x < kFloodedDistrictCoreFirstModule ||
        local_module_x > kFloodedDistrictCoreLastModule ||
        local_module_z < kFloodedDistrictCoreFirstModule ||
        local_module_z > kFloodedDistrictCoreLastModule) {
        return false;
    }

    const auto district_hash =
        hash_position(
            layout_seed,
            district_x,
            district_z,
            0xA4F3D91B6C287E05ULL);
    if (district_hash % 100U < 20U) {
        return true;
    }

    // Je retire un seul coin du carre central dans quatre districts sur cinq.
    // Les trois modules restants forment donc toujours un triomino connecte.
    const auto local_index =
        (local_module_z - kFloodedDistrictCoreFirstModule) * 2 +
        (local_module_x - kFloodedDistrictCoreFirstModule);
    const auto omitted_index =
        static_cast<int>((district_hash >> 8U) % 4U);
    return local_index != omitted_index;
}

[[nodiscard]] constexpr auto poolrooms_basin_chance(
    BackroomsArchetype archetype) noexcept -> std::uint32_t {

    switch (archetype) {
    case BackroomsArchetype::GrandHall:
        return 88U;
    case BackroomsArchetype::PillarGallery:
        return 78U;
    case BackroomsArchetype::LongCorridor:
        return 62U;
    case BackroomsArchetype::NestedRooms:
        return 70U;
    case BackroomsArchetype::Anomaly:
        return 82U;
    case BackroomsArchetype::CompressionMaze:
        return 55U;
    case BackroomsArchetype::Blackout:
        return 68U;
    case BackroomsArchetype::ClassicOffice:
    case BackroomsArchetype::CubicleFarm:
    default:
        return 70U;
    }
}

[[nodiscard]] auto poolrooms_basin_surface(
    int layout_seed,
    BackroomsArchetype archetype,
    int world_x,
    int world_z) noexcept -> BackroomsPoolSurface {

    const auto basin_cell_x =
        floor_division(world_x, kPoolroomsBasinCellSize);
    const auto basin_cell_z =
        floor_division(world_z, kPoolroomsBasinCellSize);
    const auto basin_hash =
        hash_position(
            layout_seed,
            basin_cell_x,
            basin_cell_z,
            0xE7B91C43ULL);
    if (basin_hash % 100U >=
        poolrooms_basin_chance(archetype)) {
        return BackroomsPoolSurface::Dry;
    }

    const auto basin_x =
        positive_modulo(
            world_x,
            kPoolroomsBasinCellSize);
    const auto basin_z =
        positive_modulo(
            world_z,
            kPoolroomsBasinCellSize);
    const auto basin_shape =
        static_cast<int>((basin_hash >> 8U) % 4U);

    auto inside_water = false;
    auto inside_shore = false;
    switch (basin_shape) {
    case 0:
        inside_water =
            basin_x >= 7 && basin_x <= 24 &&
            basin_z >= 7 && basin_z <= 24;
        inside_shore =
            basin_x >= 4 && basin_x <= 27 &&
            basin_z >= 4 && basin_z <= 27;
        break;
    case 1:
        inside_water =
            basin_x >= 6 && basin_x <= 25 &&
            basin_z >= 10 && basin_z <= 21;
        inside_shore =
            basin_x >= 3 && basin_x <= 28 &&
            basin_z >= 7 && basin_z <= 24;
        break;
    case 2:
        inside_water =
            basin_x >= 10 && basin_x <= 21 &&
            basin_z >= 6 && basin_z <= 25;
        inside_shore =
            basin_x >= 7 && basin_x <= 24 &&
            basin_z >= 3 && basin_z <= 28;
        break;
    case 3:
    default: {
        const auto centered_x = basin_x * 2 - 31;
        const auto centered_z = basin_z * 2 - 31;
        const auto squared_distance =
            centered_x * centered_x +
            centered_z * centered_z;
        inside_water = squared_distance <= 17 * 17;
        inside_shore = squared_distance <= 23 * 23;
        break;
    }
    }

    if (inside_water) {
        return BackroomsPoolSurface::Water;
    }
    if (inside_shore) {
        return BackroomsPoolSurface::Shore;
    }
    return BackroomsPoolSurface::Dry;
}

[[nodiscard]] auto poolrooms_basin_water_level(
    int layout_seed,
    int world_x,
    int world_z) noexcept -> std::uint8_t {

    const auto basin_cell_x =
        floor_division(world_x, kPoolroomsBasinCellSize);
    const auto basin_cell_z =
        floor_division(world_z, kPoolroomsBasinCellSize);
    const auto water_level_hash =
        hash_position(
            layout_seed,
            basin_cell_x,
            basin_cell_z,
            0xC469A31DULL);

    // Je choisis une seule hauteur pour toute la cuve : trois bassins sur
    // quatre restent sous la margelle et le dernier affleure le sol sec.
    return water_level_hash % 4U == 0U
               ? kMaxWaterLevel
               : kPoolroomsShallowWaterLevel;
}

[[nodiscard]] auto derive_layout_seed(
    int seed,
    int logical_level) noexcept -> int {

    // Je garde exactement la disposition historique du niveau zéro, puis je
    // dérive une identité indépendante pour chaque autre profondeur.
    if (logical_level == 0) {
        return seed;
    }
    const auto mixed =
        hash_position(
            seed,
            static_cast<std::int64_t>(logical_level),
            0,
            0xDB4F0B9175AE2165ULL);
    return static_cast<int>(mixed & 0x7FFFFFFFU);
}

[[nodiscard]] auto level_boundary_hash(
    int seed,
    std::int64_t boundary_level,
    int district_x,
    int district_z,
    std::uint64_t salt) noexcept -> std::uint32_t {

    const auto level_salt =
        mix64(
            static_cast<std::uint64_t>(
                static_cast<std::int64_t>(boundary_level)) ^
            salt);
    return hash_position(seed, district_x, district_z, level_salt);
}

[[nodiscard]] constexpr auto within(int value, int first, int second) noexcept -> bool {
    const auto minimum = std::min(first, second);
    const auto maximum = std::max(first, second);
    return value >= minimum && value <= maximum;
}

[[nodiscard]] constexpr auto near(int value, int center, int radius) noexcept -> bool {
    return value >= center - radius && value <= center + radius;
}

[[nodiscard]] auto shared_portal_center(
    int seed,
    std::int64_t edge_x,
    std::int64_t edge_z,
    std::uint64_t salt) noexcept -> int {

    constexpr auto portal_span =
        static_cast<std::uint32_t>(
            kPortalMaximumCenter - kPortalMinimumCenter + 1);
    return kPortalMinimumCenter +
           static_cast<int>(
               hash_position(seed, edge_x, edge_z, salt) % portal_span);
}

[[nodiscard]] auto choose_tension(
    int seed,
    int module_x,
    int module_z) noexcept -> BackroomsTension {

    // Un même profil persiste sur plusieurs modules. Le joueur perçoit ainsi
    // des phases de respiration, de compression puis de rupture, plutôt qu'un
    // bruit aléatoire sans rythme.
    const auto district_x = floor_division(module_x, 4);
    const auto district_z = floor_division(module_z, 4);
    const auto roll =
        hash_position(seed, district_x, district_z, 0xA37F6D23ULL) % 100U;

    if (roll < 27U) {
        return BackroomsTension::Familiarity;
    }
    if (roll < 46U) {
        return BackroomsTension::Compression;
    }
    if (roll < 63U) {
        return BackroomsTension::Expansion;
    }
    if (roll < 78U) {
        return BackroomsTension::Repetition;
    }
    if (roll < 91U) {
        return BackroomsTension::Anomaly;
    }
    return BackroomsTension::Blackout;
}

[[nodiscard]] auto choose_palette(
    int seed,
    int module_x,
    int module_z) noexcept -> BackroomsPalette {

    const auto roll =
        hash_position(seed, module_x, module_z, 0x51C3E91BULL) % 100U;
    if (roll < 59U) {
        return BackroomsPalette::NicotineYellow;
    }
    if (roll < 70U) {
        return BackroomsPalette::SickGreen;
    }
    if (roll < 80U) {
        return BackroomsPalette::WashedBlue;
    }
    if (roll < 88U) {
        return BackroomsPalette::FadedRose;
    }
    if (roll < 95U) {
        return BackroomsPalette::Oxide;
    }
    return BackroomsPalette::RawConcrete;
}

[[nodiscard]] auto choose_archetype(
    int seed,
    int module_x,
    int module_z,
    BackroomsTension tension) noexcept -> BackroomsArchetype {

    const auto roll =
        hash_position(seed, module_x, module_z, 0xC19B72D5ULL) % 100U;

    switch (tension) {
    case BackroomsTension::Familiarity:
        if (roll < 48U) {
            return BackroomsArchetype::ClassicOffice;
        }
        if (roll < 76U) {
            return BackroomsArchetype::CubicleFarm;
        }
        if (roll < 91U) {
            return BackroomsArchetype::LongCorridor;
        }
        return BackroomsArchetype::PillarGallery;

    case BackroomsTension::Compression:
        if (roll < 50U) {
            return BackroomsArchetype::CompressionMaze;
        }
        if (roll < 76U) {
            return BackroomsArchetype::LongCorridor;
        }
        if (roll < 91U) {
            return BackroomsArchetype::NestedRooms;
        }
        return BackroomsArchetype::Blackout;

    case BackroomsTension::Expansion:
        if (roll < 54U) {
            return BackroomsArchetype::GrandHall;
        }
        if (roll < 80U) {
            return BackroomsArchetype::PillarGallery;
        }
        if (roll < 92U) {
            return BackroomsArchetype::ClassicOffice;
        }
        return BackroomsArchetype::Anomaly;

    case BackroomsTension::Repetition:
        if (roll < 43U) {
            return BackroomsArchetype::PillarGallery;
        }
        if (roll < 75U) {
            return BackroomsArchetype::CubicleFarm;
        }
        if (roll < 91U) {
            return BackroomsArchetype::LongCorridor;
        }
        return BackroomsArchetype::NestedRooms;

    case BackroomsTension::Anomaly:
        if (roll < 50U) {
            return BackroomsArchetype::Anomaly;
        }
        if (roll < 73U) {
            return BackroomsArchetype::NestedRooms;
        }
        if (roll < 88U) {
            return BackroomsArchetype::GrandHall;
        }
        return BackroomsArchetype::CompressionMaze;

    case BackroomsTension::Blackout:
    default:
        if (roll < 55U) {
            return BackroomsArchetype::Blackout;
        }
        if (roll < 76U) {
            return BackroomsArchetype::CompressionMaze;
        }
        if (roll < 91U) {
            return BackroomsArchetype::LongCorridor;
        }
        return BackroomsArchetype::Anomaly;
    }
}

[[nodiscard]] auto choose_poolrooms_archetype(
    int seed,
    int module_x,
    int module_z) noexcept -> BackroomsArchetype {

    const auto roll =
        hash_position(seed, module_x, module_z, 0xAA53D6E9ULL) % 100U;
    if (roll < 25U) {
        return BackroomsArchetype::GrandHall;
    }
    if (roll < 45U) {
        return BackroomsArchetype::PillarGallery;
    }
    if (roll < 61U) {
        return BackroomsArchetype::LongCorridor;
    }
    if (roll < 75U) {
        return BackroomsArchetype::NestedRooms;
    }
    if (roll < 87U) {
        return BackroomsArchetype::Anomaly;
    }
    if (roll < 95U) {
        return BackroomsArchetype::CompressionMaze;
    }
    return BackroomsArchetype::Blackout;
}

[[nodiscard]] auto base_ceiling_height(
    int seed,
    int module_x,
    int module_z,
    BackroomsArchetype archetype) noexcept -> int {

    const auto variation = static_cast<int>(
        hash_position(seed, module_x, module_z, 0x7E85A14FULL) % 6U);
    switch (archetype) {
    case BackroomsArchetype::CompressionMaze:
        return 5 + variation % 2;
    case BackroomsArchetype::LongCorridor:
        return 6 + variation % 4;
    case BackroomsArchetype::GrandHall:
        return 16 + variation * 2;
    case BackroomsArchetype::PillarGallery:
        return 10 + variation;
    case BackroomsArchetype::NestedRooms:
        return 7 + variation;
    case BackroomsArchetype::Anomaly:
        return 8 + variation * 2;
    case BackroomsArchetype::Blackout:
        return 6 + variation % 3;
    case BackroomsArchetype::CubicleFarm:
        return 7 + variation % 3;
    case BackroomsArchetype::ClassicOffice:
    default:
        return 7 + variation % 3;
    }
}

} // namespace

BackroomsGenerator::BackroomsGenerator() noexcept
    : BackroomsGenerator(1337, 0) {}

BackroomsGenerator::BackroomsGenerator(
    int seed,
    int logical_level,
    int connector_district_modules,
    BackroomsPoolGeometryProfile pool_geometry_profile) noexcept
    : seed_(seed),
      logical_level_(logical_level),
      layout_seed_(derive_layout_seed(seed, logical_level)),
      connector_district_modules_(
          std::clamp(
              connector_district_modules,
              1,
              kBackroomsConnectorDistrictModules)),
      pool_geometry_profile_(pool_geometry_profile) {}

auto BackroomsGenerator::seed() const noexcept -> int {
    return seed_;
}

auto BackroomsGenerator::logical_level() const noexcept -> int {
    return logical_level_;
}

auto BackroomsGenerator::connector_district_modules() const noexcept -> int {
    return connector_district_modules_;
}

auto BackroomsGenerator::pool_geometry_profile() const noexcept
    -> BackroomsPoolGeometryProfile {

    return pool_geometry_profile_;
}

auto BackroomsGenerator::theme() const noexcept -> BackroomsTheme {
    return is_poolrooms()
               ? BackroomsTheme::Poolrooms
               : BackroomsTheme::Offices;
}

auto BackroomsGenerator::is_poolrooms() const noexcept -> bool {
    return logical_level_ <= -2;
}

auto BackroomsGenerator::is_flooded_module(
    int module_x,
    int module_z) const noexcept -> bool {

    return is_poolrooms() &&
           pool_geometry_profile_ ==
               BackroomsPoolGeometryProfile::FloodedDistrictsV4 &&
           flooded_module_for_layout(
               layout_seed_,
               module_x,
               module_z);
}

auto BackroomsGenerator::is_flooded_at(
    int world_x,
    int world_z) const noexcept -> bool {

    return is_flooded_module(
        module_coordinate(world_x),
        module_coordinate(world_z));
}

auto BackroomsGenerator::module_coordinate(int world_coordinate) noexcept -> int {
    return floor_division(world_coordinate, kBackroomsModuleSize);
}

auto BackroomsGenerator::local_coordinate(int world_coordinate) noexcept -> int {
    return positive_modulo(world_coordinate, kBackroomsModuleSize);
}

auto BackroomsGenerator::module_descriptor(
    int module_x,
    int module_z) const noexcept -> BackroomsModuleDescriptor {

    BackroomsModuleDescriptor descriptor {};
    descriptor.module_x = module_x;
    descriptor.module_z = module_z;
    descriptor.theme = theme();

    descriptor.tension =
        choose_tension(layout_seed_, module_x, module_z);
    descriptor.palette =
        choose_palette(layout_seed_, module_x, module_z);
    descriptor.archetype =
        is_poolrooms()
            ? choose_poolrooms_archetype(
                  layout_seed_,
                  module_x,
                  module_z)
            : choose_archetype(
                  layout_seed_,
                  module_x,
                  module_z,
                  descriptor.tension);

    if (is_poolrooms()) {
        const auto palette_roll =
            hash_position(
                layout_seed_,
                module_x,
                module_z,
                0x13B5E8C7ULL) %
            100U;
        descriptor.palette =
            palette_roll < 59U
                ? BackroomsPalette::WashedBlue
                : palette_roll < 82U
                      ? BackroomsPalette::SickGreen
                      : palette_roll < 94U
                            ? BackroomsPalette::RawConcrete
                            : BackroomsPalette::Oxide;
    }

    const auto hub_hash =
        hash_position(
            layout_seed_,
            module_x,
            module_z,
            0xD48F3C61ULL);
    descriptor.hub_x = 25 + static_cast<int>(hub_hash % 15U);
    descriptor.hub_z = 25 + static_cast<int>((hub_hash >> 8U) % 15U);
    descriptor.primary_axis_x = ((hub_hash >> 20U) & 1U) == 0U;

    // Chaque ouverture est calculée depuis l'arête partagée. Deux modules
    // voisins obtiennent donc exactement le même raccord sans état global.
    descriptor.north_portal_x =
        shared_portal_center(
            layout_seed_,
            module_x,
            module_z,
            0x1187A4E3ULL);
    descriptor.south_portal_x =
        shared_portal_center(
            layout_seed_,
            module_x,
            static_cast<std::int64_t>(module_z) + 1,
            0x1187A4E3ULL);
    descriptor.west_portal_z =
        shared_portal_center(
            layout_seed_,
            module_x,
            module_z,
            0x4D2C90B7ULL);
    descriptor.east_portal_z =
        shared_portal_center(
            layout_seed_,
            static_cast<std::int64_t>(module_x) + 1,
            module_z,
            0x4D2C90B7ULL);
    descriptor.base_ceiling_height =
        base_ceiling_height(
            layout_seed_,
            module_x,
            module_z,
            descriptor.archetype);
    if (is_poolrooms()) {
        const auto height_roll =
            static_cast<int>(
                hash_position(
                    layout_seed_,
                    module_x,
                    module_z,
                    0x943EF12DULL) %
                9U);
        descriptor.base_ceiling_height =
            std::clamp(
                descriptor.base_ceiling_height + height_roll / 2,
                kPoolroomsMinimumCeilingHeight,
                27);
    }

    // Le premier module donne au joueur quelques secondes pour comprendre les
    // volumes et les contrôles avant que la distribution de tension ne prenne
    // le relais.
    if (!is_poolrooms() && module_x == 0 && module_z == 0) {
        descriptor.archetype = BackroomsArchetype::ClassicOffice;
        descriptor.palette = BackroomsPalette::NicotineYellow;
        descriptor.tension = BackroomsTension::Familiarity;
        descriptor.hub_x = kBackroomsModuleSize / 2;
        descriptor.hub_z = kBackroomsModuleSize / 2;
        descriptor.base_ceiling_height = 8;
    }

    return descriptor;
}

auto BackroomsGenerator::descriptor_at(
    int world_x,
    int world_z) const noexcept -> BackroomsModuleDescriptor {

    return module_descriptor(
        module_coordinate(world_x),
        module_coordinate(world_z));
}

auto BackroomsGenerator::connector_in_district(
    BackroomsConnectorDirection direction,
    int district_x,
    int district_z) const noexcept
    -> std::optional<BackroomsLevelConnector> {

    switch (direction) {
    case BackroomsConnectorDirection::Up:
        if (logical_level_ == std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        break;
    case BackroomsConnectorDirection::Down:
        if (logical_level_ == std::numeric_limits<int>::lowest()) {
            return std::nullopt;
        }
        break;
    default:
        // Je refuse une direction inconnue avant tout calcul de niveau : elle
        // ne doit jamais etre interpretee implicitement comme une descente.
        return std::nullopt;
    }

    const auto boundary_level =
        direction == BackroomsConnectorDirection::Up
            ? static_cast<std::int64_t>(logical_level_)
            : static_cast<std::int64_t>(logical_level_) - 1;
    const auto placement_hash =
        level_boundary_hash(
            seed_,
            boundary_level,
            district_x,
            district_z,
            0x6A134E9B29D75C41ULL);
    const auto orientation_hash =
        level_boundary_hash(
            seed_,
            boundary_level,
            district_x,
            district_z,
            0xC39481F0276BAD5EULL);

    const auto district_module_count =
        static_cast<std::uint32_t>(
            connector_district_modules_);
    const auto module_offset_x =
        static_cast<int>(
            placement_hash % district_module_count);
    const auto module_offset_z =
        static_cast<int>(
            (placement_hash >> 2U) % district_module_count);
    const auto module_x =
        static_cast<std::int64_t>(district_x) *
            connector_district_modules_ +
        module_offset_x;
    const auto module_z =
        static_cast<std::int64_t>(district_z) *
            connector_district_modules_ +
        module_offset_z;
    const auto local_x =
        15 + static_cast<int>((placement_hash >> 8U) % 34U);
    const auto local_z =
        15 + static_cast<int>((placement_hash >> 16U) % 34U);
    const auto trigger_x = checked_int(
        module_x * kBackroomsModuleSize + local_x);
    const auto trigger_z = checked_int(
        module_z * kBackroomsModuleSize + local_z);
    if (!trigger_x.has_value() || !trigger_z.has_value()) {
        return std::nullopt;
    }
    const BlockCoord trigger {
        *trigger_x,
        kBackroomsFloorY + 1,
        *trigger_z,
    };

    const auto orientation =
        static_cast<int>(orientation_hash % 4U);
    constexpr int landing_distance = 3;
    const auto landing_offset_x =
        orientation == 1
            ? landing_distance
            : orientation == 3 ? -landing_distance : 0;
    const auto landing_offset_z =
        orientation == 0
            ? landing_distance
            : orientation == 2 ? -landing_distance : 0;

    const auto destination_level =
        direction == BackroomsConnectorDirection::Up
            ? logical_level_ + 1
            : logical_level_ - 1;
    const auto landing_x = checked_int(
        static_cast<std::int64_t>(trigger.x) + landing_offset_x);
    const auto landing_z = checked_int(
        static_cast<std::int64_t>(trigger.z) + landing_offset_z);
    if (!landing_x.has_value() || !landing_z.has_value()) {
        return std::nullopt;
    }

    const auto slide =
        direction == BackroomsConnectorDirection::Down &&
        is_poolrooms() &&
        ((placement_hash >> 24U) % 5U == 0U);
    return BackroomsLevelConnector {
        .direction = direction,
        .style =
            slide
                ? BackroomsConnectorStyle::Slide
                : BackroomsConnectorStyle::Stairs,
        .destination_level = destination_level,
        .trigger_block = trigger,
        .destination_landing_block = {
            *landing_x,
            kBackroomsFloorY + 1,
            *landing_z,
        },
        .destination_yaw_degrees =
            static_cast<float>(orientation * 90),
    };
}

auto BackroomsGenerator::connector_near(
    int world_x,
    int world_y,
    int world_z,
    int horizontal_radius) const noexcept
    -> std::optional<BackroomsLevelConnector> {

    if (horizontal_radius < 0 ||
        horizontal_radius > kBackroomsMaximumConnectorSearchRadius) {
        return std::nullopt;
    }
    const auto radius = horizontal_radius;
    const auto district_world_size =
        connector_district_modules_ *
        kBackroomsModuleSize;
    const auto center_district_x =
        floor_division(world_x, district_world_size);
    const auto center_district_z =
        floor_division(world_z, district_world_size);
    const auto district_radius =
        radius / district_world_size + 1;

    std::optional<BackroomsLevelConnector> nearest;
    auto nearest_distance_squared =
        std::numeric_limits<std::int64_t>::max();
    constexpr std::array<BackroomsConnectorDirection, 2> directions {{
        BackroomsConnectorDirection::Up,
        BackroomsConnectorDirection::Down,
    }};
    for (int district_offset_z = -district_radius;
         district_offset_z <= district_radius;
         ++district_offset_z) {
        for (int district_offset_x = -district_radius;
             district_offset_x <= district_radius;
            ++district_offset_x) {
            for (const auto direction : directions) {
                const auto district_x = checked_int(
                    static_cast<std::int64_t>(center_district_x) +
                    district_offset_x);
                const auto district_z = checked_int(
                    static_cast<std::int64_t>(center_district_z) +
                    district_offset_z);
                if (!district_x.has_value() ||
                    !district_z.has_value()) {
                    continue;
                }
                const auto connector =
                    connector_in_district(
                        direction,
                        *district_x,
                        *district_z);
                if (!connector.has_value()) {
                    continue;
                }
                const auto delta_x =
                    static_cast<std::int64_t>(world_x) -
                    connector->trigger_block.x;
                const auto delta_z =
                    static_cast<std::int64_t>(world_z) -
                    connector->trigger_block.z;
                const auto delta_y =
                    static_cast<std::int64_t>(world_y) -
                    connector->trigger_block.y;
                if (std::abs(delta_x) > radius ||
                    std::abs(delta_z) > radius ||
                    std::abs(delta_y) > 1) {
                    continue;
                }

                const auto distance_squared =
                    delta_x * delta_x + delta_z * delta_z;
                if (distance_squared < nearest_distance_squared) {
                    nearest = *connector;
                    nearest_distance_squared = distance_squared;
                }
            }
        }
    }
    return nearest;
}

auto BackroomsGenerator::wall_block_for(
    BackroomsPalette palette) const noexcept -> BlockId {

    switch (palette) {
    case BackroomsPalette::SickGreen:
        return to_block_id(BlockType::BackroomsWallGreen);
    case BackroomsPalette::WashedBlue:
        return to_block_id(BlockType::BackroomsWallBlue);
    case BackroomsPalette::FadedRose:
        return to_block_id(BlockType::BackroomsWallRose);
    case BackroomsPalette::Oxide:
        return to_block_id(BlockType::BackroomsWallOxide);
    case BackroomsPalette::RawConcrete:
        return to_block_id(BlockType::BackroomsConcrete);
    case BackroomsPalette::NicotineYellow:
    default:
        return to_block_id(BlockType::BackroomsWallYellow);
    }
}

auto BackroomsGenerator::floor_block_for(
    const BackroomsModuleDescriptor& descriptor,
    int local_x,
    int local_z) const noexcept -> BlockId {

    if (descriptor.palette == BackroomsPalette::RawConcrete ||
        descriptor.archetype == BackroomsArchetype::GrandHall) {
        return to_block_id(BlockType::BackroomsConcrete);
    }

    if (descriptor.archetype == BackroomsArchetype::Anomaly) {
        const auto patch =
            hash_position(
                layout_seed_,
                static_cast<std::int64_t>(descriptor.module_x) * 4 +
                    local_x / 16,
                static_cast<std::int64_t>(descriptor.module_z) * 4 +
                    local_z / 16,
                0x9425E17BULL);
        if (patch % 7U == 0U) {
            return to_block_id(BlockType::BackroomsConcrete);
        }
    }
    return to_block_id(BlockType::BackroomsCarpet);
}

auto BackroomsGenerator::is_guaranteed_route_in_rectangle(
    const BackroomsModuleDescriptor& descriptor,
    int minimum_local_x,
    int minimum_local_z,
    int maximum_local_x,
    int maximum_local_z) const noexcept -> bool {

    const auto intersects =
        [minimum_local_x,
         minimum_local_z,
         maximum_local_x,
         maximum_local_z](
            int first_x,
            int first_z,
            int second_x,
            int second_z) noexcept {
            const auto rectangle_minimum_x =
                std::min(first_x, second_x);
            const auto rectangle_maximum_x =
                std::max(first_x, second_x);
            const auto rectangle_minimum_z =
                std::min(first_z, second_z);
            const auto rectangle_maximum_z =
                std::max(first_z, second_z);
            return
                minimum_local_x <= rectangle_maximum_x &&
                maximum_local_x >= rectangle_minimum_x &&
                minimum_local_z <= rectangle_maximum_z &&
                maximum_local_z >= rectangle_minimum_z;
        };

    const auto district_x =
        floor_division(
            descriptor.module_x,
            connector_district_modules_);
    const auto district_z =
        floor_division(
            descriptor.module_z,
            connector_district_modules_);
    constexpr std::array<BackroomsConnectorDirection, 2> directions {{
        BackroomsConnectorDirection::Up,
        BackroomsConnectorDirection::Down,
    }};
    for (const auto direction : directions) {
        const auto connector =
            connector_in_district(
                direction,
                district_x,
                district_z);
        if (!connector.has_value() ||
            module_coordinate(connector->trigger_block.x) !=
                descriptor.module_x ||
            module_coordinate(connector->trigger_block.z) !=
                descriptor.module_z) {
            continue;
        }

        const auto connector_x =
            local_coordinate(connector->trigger_block.x);
        const auto connector_z =
            local_coordinate(connector->trigger_block.z);
        const auto room =
            intersects(
                connector_x - kConnectorRoomHalfWidth,
                connector_z - kConnectorRoomHalfWidth,
                connector_x + kConnectorRoomHalfWidth,
                connector_z + kConnectorRoomHalfWidth);
        const auto vertical =
            intersects(
                connector_x - kRouteHalfWidth,
                connector_z,
                connector_x + kRouteHalfWidth,
                descriptor.hub_z);
        const auto horizontal =
            intersects(
                connector_x,
                descriptor.hub_z - kRouteHalfWidth,
                descriptor.hub_x,
                descriptor.hub_z + kRouteHalfWidth);
        if (room || vertical || horizontal) {
            return true;
        }
    }

    if (intersects(
            descriptor.hub_x - kHubHalfWidth,
            descriptor.hub_z - kHubHalfWidth,
            descriptor.hub_x + kHubHalfWidth,
            descriptor.hub_z + kHubHalfWidth)) {
        return true;
    }

    const auto north_vertical =
        intersects(
            descriptor.north_portal_x - kRouteHalfWidth,
            0,
            descriptor.north_portal_x + kRouteHalfWidth,
            descriptor.hub_z);
    const auto north_horizontal =
        intersects(
            descriptor.north_portal_x,
            descriptor.hub_z - kRouteHalfWidth,
            descriptor.hub_x,
            descriptor.hub_z + kRouteHalfWidth);

    const auto south_vertical =
        intersects(
            descriptor.south_portal_x - kRouteHalfWidth,
            descriptor.hub_z,
            descriptor.south_portal_x + kRouteHalfWidth,
            kBackroomsModuleSize - 1);
    const auto south_horizontal =
        intersects(
            descriptor.south_portal_x,
            descriptor.hub_z - kRouteHalfWidth,
            descriptor.hub_x,
            descriptor.hub_z + kRouteHalfWidth);

    const auto west_horizontal =
        intersects(
            0,
            descriptor.west_portal_z - kRouteHalfWidth,
            descriptor.hub_x,
            descriptor.west_portal_z + kRouteHalfWidth);
    const auto west_vertical =
        intersects(
            descriptor.hub_x - kRouteHalfWidth,
            descriptor.west_portal_z,
            descriptor.hub_x + kRouteHalfWidth,
            descriptor.hub_z);

    const auto east_horizontal =
        intersects(
            descriptor.hub_x,
            descriptor.east_portal_z - kRouteHalfWidth,
            kBackroomsModuleSize - 1,
            descriptor.east_portal_z + kRouteHalfWidth);
    const auto east_vertical =
        intersects(
            descriptor.hub_x - kRouteHalfWidth,
            descriptor.east_portal_z,
            descriptor.hub_x + kRouteHalfWidth,
            descriptor.hub_z);

    return north_vertical || north_horizontal ||
           south_vertical || south_horizontal ||
           west_horizontal || west_vertical ||
           east_horizontal || east_vertical;
}

auto BackroomsGenerator::is_guaranteed_route(
    const BackroomsModuleDescriptor& descriptor,
    int local_x,
    int local_z) const noexcept -> bool {

    return is_guaranteed_route_in_rectangle(
        descriptor,
        local_x,
        local_z,
        local_x,
        local_z);
}

auto BackroomsGenerator::ceiling_height_at(
    const BackroomsModuleDescriptor& descriptor,
    int local_x,
    int local_z) const noexcept -> int {

    auto height = descriptor.base_ceiling_height;

    if (descriptor.archetype == BackroomsArchetype::Anomaly) {
        const auto zone_x = local_x / 16;
        const auto zone_z = local_z / 16;
        const auto zone_hash =
            hash_position(
                layout_seed_,
                static_cast<std::int64_t>(descriptor.module_x) * 4 +
                    zone_x,
                static_cast<std::int64_t>(descriptor.module_z) * 4 +
                    zone_z,
                0xE1347AA9ULL);
        height = 5 + static_cast<int>(zone_hash % 14U);
    } else if (descriptor.archetype == BackroomsArchetype::NestedRooms) {
        const auto edge_distance = std::min(
            std::min(local_x, kBackroomsModuleSize - 1 - local_x),
            std::min(local_z, kBackroomsModuleSize - 1 - local_z));
        if (edge_distance > 22) {
            height += 3;
        } else if (edge_distance > 13) {
            height += 1;
        }
    } else if (descriptor.archetype == BackroomsArchetype::GrandHall) {
        const auto zone_hash =
            hash_position(
                layout_seed_,
                static_cast<std::int64_t>(descriptor.module_x) * 2 +
                    local_x / 32,
                static_cast<std::int64_t>(descriptor.module_z) * 2 +
                    local_z / 32,
                0x2C5D8F11ULL);
        height = std::clamp(
            descriptor.base_ceiling_height +
                static_cast<int>(zone_hash % 5U) - 2,
            14,
            kBackroomsMaxCeilingHeight);
    }

    // Quatre blocs d'air constituent la limite dure de jouabilité. Cette borne
    // reste vraie même dans les secteurs volontairement oppressants.
    return std::clamp(height, 5, kBackroomsMaxCeilingHeight);
}

auto BackroomsGenerator::wall_height_at(
    const BackroomsModuleDescriptor& descriptor,
    int local_x,
    int local_z) const noexcept -> int {

    const auto ceiling_y =
        kBackroomsFloorY +
        ceiling_height_at(descriptor, local_x, local_z);

    const auto on_north =
        local_z == 0 &&
        !near(
            local_x,
            descriptor.north_portal_x,
            kBackroomsPortalHalfWidth);
    const auto on_south =
        local_z == kBackroomsModuleSize - 1 &&
        !near(
            local_x,
            descriptor.south_portal_x,
            kBackroomsPortalHalfWidth);
    const auto on_west =
        local_x == 0 &&
        !near(
            local_z,
            descriptor.west_portal_z,
            kBackroomsPortalHalfWidth);
    const auto on_east =
        local_x == kBackroomsModuleSize - 1 &&
        !near(
            local_z,
            descriptor.east_portal_z,
            kBackroomsPortalHalfWidth);

    if (on_north || on_south || on_west || on_east) {
        return ceiling_y;
    }

    if (local_x == 0 || local_x == kBackroomsModuleSize - 1 ||
        local_z == 0 || local_z == kBackroomsModuleSize - 1) {
        return kBackroomsFloorY;
    }

    if (is_guaranteed_route(descriptor, local_x, local_z)) {
        return kBackroomsFloorY;
    }

    const auto pattern_hash =
        hash_position(
            layout_seed_,
            descriptor.module_x,
            descriptor.module_z,
            0x684DB2C7ULL);
    const auto shift_x = static_cast<int>(pattern_hash % 11U);
    const auto shift_z = static_cast<int>((pattern_hash >> 8U) % 13U);
    auto wall = false;
    auto low_partition = false;

    switch (descriptor.archetype) {
    case BackroomsArchetype::ClassicOffice: {
        const auto vertical =
            positive_modulo(local_x + shift_x, 16) == 0;
        const auto horizontal =
            positive_modulo(local_z + shift_z, 18) == 0;
        const auto vertical_door =
            positive_modulo(local_z + shift_z * 2, 19) < 5;
        const auto horizontal_door =
            positive_modulo(local_x + shift_x * 3, 21) < 5;
        wall =
            (vertical && !vertical_door) ||
            (horizontal && !horizontal_door);
        break;
    }

    case BackroomsArchetype::CubicleFarm: {
        const auto cell_x =
            positive_modulo(local_x + shift_x, 11);
        const auto cell_z =
            positive_modulo(local_z + shift_z, 10);
        const auto vertical =
            cell_x == 0 && !(cell_z >= 3 && cell_z <= 6);
        const auto horizontal =
            cell_z == 0 && !(cell_x >= 4 && cell_x <= 7);
        wall = vertical || horizontal;
        low_partition = wall;
        break;
    }

    case BackroomsArchetype::CompressionMaze: {
        const auto stripe_x =
            positive_modulo(local_x + shift_x, 8) == 0;
        const auto stripe_z =
            positive_modulo(local_z + shift_z, 9) == 0;
        const auto door_x =
            positive_modulo(
                local_z + (local_x / 8) * 5 + shift_z,
                23) < 5;
        const auto door_z =
            positive_modulo(
                local_x + (local_z / 9) * 7 + shift_x,
                25) < 5;
        wall =
            (stripe_x && !door_x) ||
            (stripe_z && !door_z &&
             positive_modulo(local_z + shift_z, 18) == 0);
        break;
    }

    case BackroomsArchetype::LongCorridor: {
        if (descriptor.primary_axis_x) {
            const auto parallel =
                positive_modulo(local_z + shift_z, 12) == 0;
            const auto opening =
                positive_modulo(local_x + shift_x, 25) < 6;
            wall = parallel && !opening;
        } else {
            const auto parallel =
                positive_modulo(local_x + shift_x, 12) == 0;
            const auto opening =
                positive_modulo(local_z + shift_z, 25) < 6;
            wall = parallel && !opening;
        }
        break;
    }

    case BackroomsArchetype::GrandHall: {
        const auto pillar_x =
            positive_modulo(local_x + shift_x, 15) <= 1;
        const auto pillar_z =
            positive_modulo(local_z + shift_z, 15) <= 1;
        wall = pillar_x && pillar_z;
        break;
    }

    case BackroomsArchetype::PillarGallery: {
        const auto pillar_x =
            positive_modulo(local_x + shift_x, 9) <= 1;
        const auto pillar_z =
            positive_modulo(local_z + shift_z, 9) <= 1;
        const auto omission =
            hash_position(
                layout_seed_,
                static_cast<std::int64_t>(descriptor.module_x) * 8 +
                    local_x / 9,
                static_cast<std::int64_t>(descriptor.module_z) * 8 +
                    local_z / 9,
                0xF7A82C39ULL) % 7U == 0U;
        wall = pillar_x && pillar_z && !omission;
        break;
    }

    case BackroomsArchetype::NestedRooms: {
        const auto edge_distance = std::min(
            std::min(local_x, kBackroomsModuleSize - 1 - local_x),
            std::min(local_z, kBackroomsModuleSize - 1 - local_z));
        const auto ring =
            edge_distance == 10 ||
            edge_distance == 19 ||
            edge_distance == 27;
        const auto opening =
            positive_modulo(
                local_x + local_z + shift_x + shift_z,
                17) < 5;
        wall = ring && !opening;
        break;
    }

    case BackroomsArchetype::Anomaly: {
        const auto diagonal =
            positive_modulo(
                local_x + local_z * 2 + shift_x,
                17) <= 1;
        const auto diagonal_gap =
            positive_modulo(
                local_x * 3 - local_z + shift_z,
                29) < 7;
        const auto displaced_box =
            ((local_x >= 12 && local_x <= 22) ||
             (local_x >= 41 && local_x <= 49)) &&
            (local_z == 14 + shift_z % 5 ||
             local_z == 45 - shift_x % 5);
        const auto isolated_pillar =
            positive_modulo(local_x + shift_x, 13) <= 1 &&
            positive_modulo(local_z + shift_z, 17) <= 1;
        wall =
            (diagonal && !diagonal_gap) ||
            displaced_box ||
            isolated_pillar;
        break;
    }

    case BackroomsArchetype::Blackout: {
        const auto vertical =
            positive_modulo(local_x + shift_x, 17) == 0;
        const auto horizontal =
            positive_modulo(local_z + shift_z, 20) == 0;
        const auto opening =
            positive_modulo(
                local_x + local_z + shift_x,
                24) < 7;
        wall = (vertical || horizontal) && !opening;
        break;
    }
    }

    if (!wall) {
        return kBackroomsFloorY;
    }
    return low_partition
               ? std::min(kBackroomsFloorY + 2, ceiling_y)
               : ceiling_y;
}

auto BackroomsGenerator::light_state_at(
    const BackroomsModuleDescriptor& descriptor,
    int world_x,
    int world_z,
    int local_x,
    int local_z,
    bool wall) const noexcept -> BackroomsLightState {

    const auto fixture =
        backrooms_fixture_pattern_at(
            descriptor,
            layout_seed_,
            world_x,
            world_z,
            local_x,
            local_z,
            wall);
    if (!fixture.has_value()) {
        return BackroomsLightState::None;
    }

    // Les pannes sont corrélées par zones de 16 m. Cela produit des poches
    // d'obscurité lisibles, bien plus inquiétantes que des lampes aléatoires.
    // Je rattache les quatre cellules d'une rampe à une ancre commune. Une
    // rampe ne peut ainsi plus changer d'état au milieu de sa longueur.
    const auto fixture_anchor_x = fixture->nominal_anchor_x;
    const auto fixture_anchor_z = fixture->nominal_anchor_z;
    const auto outage_cell_x =
        floor_division(fixture_anchor_x, 16);
    const auto outage_cell_z =
        floor_division(fixture_anchor_z, 16);
    const auto outage_roll =
        hash_position(
            layout_seed_,
            outage_cell_x,
            outage_cell_z,
            0xB94C203DULL) % 100U;

    std::uint32_t outage_threshold = 14U;
    switch (descriptor.tension) {
    case BackroomsTension::Familiarity:
        outage_threshold = 10U;
        break;
    case BackroomsTension::Compression:
        outage_threshold = 24U;
        break;
    case BackroomsTension::Expansion:
        outage_threshold = 18U;
        break;
    case BackroomsTension::Repetition:
        outage_threshold = 29U;
        break;
    case BackroomsTension::Anomaly:
        outage_threshold = 43U;
        break;
    case BackroomsTension::Blackout:
        outage_threshold = 74U;
        break;
    }

    const auto fixture_hash =
        hash_position(
            layout_seed_,
            fixture_anchor_x,
            fixture_anchor_z,
            0x6ACF5381ULL);
    if (outage_roll < outage_threshold) {
        const auto emergency_chance =
            descriptor.tension == BackroomsTension::Blackout ? 12U : 5U;
        return fixture_hash % 100U < emergency_chance
                   ? BackroomsLightState::Emergency
                   : BackroomsLightState::Failed;
    }

    // Quelques tubes morts isolés cassent la régularité sans transformer la
    // scène en clignotement agressif.
    if (fixture_hash % 100U < 5U) {
        return BackroomsLightState::Failed;
    }
    return BackroomsLightState::Active;
}

auto BackroomsGenerator::sample_poolrooms_column(
    const BackroomsModuleDescriptor& descriptor,
    int world_x,
    int world_z,
    int local_x,
    int local_z) const noexcept -> BackroomsColumnSample {

    const auto guaranteed_route =
        is_guaranteed_route(descriptor, local_x, local_z);
    const auto ceiling_height =
        std::max(
            kPoolroomsMinimumCeilingHeight,
            ceiling_height_at(descriptor, local_x, local_z));
    const auto ceiling_y =
        kBackroomsFloorY + ceiling_height;
    auto wall_top_y =
        wall_height_at(descriptor, local_x, local_z);
    auto wall = wall_top_y > kBackroomsFloorY;
    if (wall) {
        wall_top_y = ceiling_y;
    }

    const auto flooded_module =
        is_flooded_module(
            descriptor.module_x,
            descriptor.module_z);
    auto pool_surface =
        flooded_module
            ? BackroomsPoolSurface::Water
            : poolrooms_basin_surface(
                  layout_seed_,
                  descriptor.archetype,
                  world_x,
                  world_z);
    const auto basin_local_origin_x =
        local_x -
        positive_modulo(
            world_x,
            kPoolroomsBasinCellSize);
    const auto basin_local_origin_z =
        local_z -
        positive_modulo(
            world_z,
            kPoolroomsBasinCellSize);
    const auto route_crosses_basin =
        is_guaranteed_route_in_rectangle(
            descriptor,
            basin_local_origin_x + 6,
            basin_local_origin_z + 6,
            basin_local_origin_x + 25,
            basin_local_origin_z + 25);
    if (!flooded_module &&
        pool_surface != BackroomsPoolSurface::Dry &&
        route_crosses_basin) {
        // Je supprime le bassin entier si une circulation structurelle devait
        // le trancher. Je préfère une vraie salle sèche à deux petites flaques
        // artificielles de part et d'autre d'une passerelle.
        pool_surface = BackroomsPoolSurface::Dry;
    }

    // Je donne à chaque bassin un vrai volume de salle. Je retire donc les
    // cloisons procédurales de son noyau et de sa margelle, sans toucher aux
    // murs de modules puisque les formes restent largement en retrait.
    if (!flooded_module &&
        pool_surface != BackroomsPoolSurface::Dry) {
        wall = false;
        wall_top_y = kBackroomsFloorY;
    }

    if (flooded_module) {
        // Je conserve les vrais murs et piliers du module. Seules les cellules
        // ouvertes recoivent l'eau continue, ce qui evite de transformer le
        // district en simple salle vide tout en supprimant les petits decors.
        if (wall) {
            pool_surface = BackroomsPoolSurface::Dry;
        } else {
            const auto module_x64 =
                static_cast<std::int64_t>(descriptor.module_x);
            const auto module_z64 =
                static_cast<std::int64_t>(descriptor.module_z);
            const auto transition_to_dry_module =
                (local_x < kFloodedTransitionWidth &&
                 !flooded_module_for_layout(
                     layout_seed_,
                     module_x64 - 1,
                     module_z64)) ||
                (local_x >=
                         kBackroomsModuleSize -
                             kFloodedTransitionWidth &&
                 !flooded_module_for_layout(
                     layout_seed_,
                     module_x64 + 1,
                     module_z64)) ||
                (local_z < kFloodedTransitionWidth &&
                 !flooded_module_for_layout(
                     layout_seed_,
                     module_x64,
                     module_z64 - 1)) ||
                (local_z >=
                         kBackroomsModuleSize -
                             kFloodedTransitionWidth &&
                 !flooded_module_for_layout(
                     layout_seed_,
                     module_x64,
                     module_z64 + 1));
            if (transition_to_dry_module) {
                pool_surface = BackroomsPoolSurface::Shore;
            }
        }
    }

    const auto route_near_water =
        is_guaranteed_route_in_rectangle(
            descriptor,
            local_x - kPoolroomsRouteShoreWidth,
            local_z - kPoolroomsRouteShoreWidth,
            local_x + kPoolroomsRouteShoreWidth,
            local_z + kPoolroomsRouteShoreWidth);
    if (!flooded_module &&
        pool_surface == BackroomsPoolSurface::Water &&
        route_near_water) {
        // Je transforme la traversée obligatoire en passerelle carrelée, avec
        // une cellule de rive sèche de chaque côté au lieu de couper l'eau net.
        pool_surface = BackroomsPoolSurface::Shore;
    }

    auto wall_block =
        to_block_id(BlockType::PoolroomsTile);
    const auto wall_variation =
        hash_position(
            layout_seed_,
            world_x,
            world_z,
            0x81C25F39ULL) %
        100U;
    if (descriptor.tension == BackroomsTension::Blackout) {
        wall_block =
            to_block_id(BlockType::PoolroomsDarkTile);
    } else if (wall_variation >= 94U) {
        wall_block =
            to_block_id(BlockType::PoolroomsMetal);
    }

    // Je réserve les objets à une grille très clairsemée, sur les plages sèches
    // uniquement. Aucun décor ne peut ainsi percer un trou isolé dans l'eau ou
    // réduire la largeur utile pour le joueur et Jack.
    const auto decoration_hash =
        hash_position(
            layout_seed_,
            floor_division(world_x, 6),
            floor_division(world_z, 6),
            0x51D806ABULL);
    const auto decoration_anchor =
        positive_modulo64(
            static_cast<std::int64_t>(world_x) +
                static_cast<int>(decoration_hash & 3U),
            12) == 3 &&
        positive_modulo64(
            static_cast<std::int64_t>(world_z) +
                static_cast<int>((decoration_hash >> 2U) & 3U),
            12) ==
            3;
    if (!wall &&
        !guaranteed_route &&
        pool_surface == BackroomsPoolSurface::Dry &&
        decoration_anchor &&
        decoration_hash % 100U < 22U) {
        wall = true;
        wall_top_y = kBackroomsFloorY + 1;
        wall_block =
            decoration_hash % 3U == 0U
                ? to_block_id(BlockType::PoolroomsPlastic)
                : to_block_id(BlockType::PoolroomsMetal);
    }

    const auto connector =
        connector_near(
            world_x,
            kBackroomsFloorY + 1,
            world_z,
            2);
    if (connector.has_value()) {
        if (pool_surface != BackroomsPoolSurface::Dry) {
            pool_surface = BackroomsPoolSurface::Shore;
        }
    }

    const auto connector_structure =
        connector_near(
            world_x,
            kBackroomsFloorY + 1,
            world_z,
            4);
    auto connector_arch = false;
    if (connector_structure.has_value()) {
        if (pool_surface != BackroomsPoolSurface::Dry) {
            pool_surface = BackroomsPoolSurface::Shore;
        }
        wall = false;
        wall_top_y = kBackroomsFloorY;
        const auto delta_x =
            static_cast<std::int64_t>(world_x) -
            connector_structure->trigger_block.x;
        const auto delta_z =
            static_cast<std::int64_t>(world_z) -
            connector_structure->trigger_block.z;
        const auto orientation =
            static_cast<int>(
                connector_structure->
                    destination_yaw_degrees /
                90.0F) %
            4;
        const auto forward =
            orientation == 0
                ? delta_z
                : orientation == 1
                      ? delta_x
                      : orientation == 2
                            ? -delta_z
                            : -delta_x;
        const auto lateral =
            orientation == 0 || orientation == 2
                ? delta_x
                : delta_z;
        const auto corner_post =
            std::abs(delta_x) == 4 &&
            std::abs(delta_z) == 4;
        const auto side_rail =
            std::abs(lateral) == 4 &&
            forward >= -3 &&
            forward <= 4;
        connector_arch =
            forward == -4 &&
            std::abs(lateral) <= 4;

        if ((corner_post || side_rail) &&
            !guaranteed_route) {
            wall = true;
            wall_top_y =
                kBackroomsFloorY +
                (corner_post ? 4 : 1);
            wall_block =
                connector_structure->style ==
                        BackroomsConnectorStyle::Slide
                    ? to_block_id(
                          BlockType::PoolroomsPlastic)
                    : to_block_id(
                          BlockType::PoolroomsMetal);
        }
    }

    const auto connector_elevated =
        connector_near(
            world_x,
            kBackroomsFloorY + 1,
            world_z,
            12);
    auto connector_flight = false;
    auto connector_balcony = false;
    auto connector_forward = std::int64_t {0};
    auto connector_lateral = std::int64_t {0};
    if (connector_elevated.has_value()) {
        const auto delta_x =
            static_cast<std::int64_t>(world_x) -
            connector_elevated->trigger_block.x;
        const auto delta_z =
            static_cast<std::int64_t>(world_z) -
            connector_elevated->trigger_block.z;
        const auto orientation =
            static_cast<int>(
                connector_elevated->
                    destination_yaw_degrees /
                90.0F) %
            4;
        connector_forward =
            orientation == 0
                ? delta_z
                : orientation == 1
                      ? delta_x
                      : orientation == 2
                            ? -delta_z
                            : -delta_x;
        connector_lateral =
            orientation == 0 || orientation == 2
                ? delta_x
                : delta_z;
        connector_flight =
            connector_forward >= -9 &&
            connector_forward <= -5 &&
            std::abs(connector_lateral) <= 1;
        connector_balcony =
            connector_forward >= -12 &&
            connector_forward <= -10 &&
            std::abs(connector_lateral) <= 5;

        if (connector_flight || connector_balcony) {
            // Je garde tout le chemin réel au même niveau et je place la volée
            // architecturale au-dessus de la tête : aucun cube de marche ne
            // peut donc coincer le contrôleur voxel.
            if (pool_surface != BackroomsPoolSurface::Dry) {
                pool_surface = BackroomsPoolSurface::Shore;
            }
            wall = false;
            wall_top_y = kBackroomsFloorY;
        }
    }

    const auto balcony_hash =
        hash_position(
            layout_seed_,
            descriptor.module_x,
            descriptor.module_z,
            0xB7286D41ULL);
    const auto balcony_side =
        static_cast<int>(balcony_hash % 4U);
    const auto balcony_along =
        balcony_side == 0 || balcony_side == 2
            ? local_x
            : local_z;
    const auto balcony_depth =
        balcony_side == 0
            ? local_z
            : balcony_side == 1
                  ? kBackroomsModuleSize - 1 - local_x
                  : balcony_side == 2
                        ? kBackroomsModuleSize - 1 - local_z
                        : local_x;
    const auto room_balcony =
        balcony_hash % 100U < 68U &&
        balcony_along >= 11 &&
        balcony_along <= 52 &&
        balcony_depth >= 7 &&
        balcony_depth <= 10;
    const auto balcony_support =
        room_balcony &&
        balcony_depth == 10 &&
        (balcony_along == 14 ||
         balcony_along == 49);
    if (balcony_support &&
        !guaranteed_route &&
        !connector_flight &&
        !connector_balcony) {
        if (pool_surface != BackroomsPoolSurface::Dry) {
            pool_surface = BackroomsPoolSurface::Shore;
        }
        wall = true;
        wall_top_y =
            std::min(
                kBackroomsFloorY + 5,
                ceiling_y - 3);
        wall_block =
            to_block_id(BlockType::PoolroomsMetal);
    }

    const auto light_state =
        light_state_at(
            descriptor,
            world_x,
            world_z,
            local_x,
            local_z,
            wall);
    auto ceiling_block =
        to_block_id(BlockType::PoolroomsTile);
    if (light_state == BackroomsLightState::Active ||
        light_state == BackroomsLightState::Emergency) {
        ceiling_block =
            to_block_id(BlockType::PoolroomsLight);
    } else if (light_state == BackroomsLightState::Failed) {
        ceiling_block =
            to_block_id(BlockType::PoolroomsFailedLight);
    }

    auto overhead_bottom_y = kWorldMaxY + 1;
    auto overhead_top_y = kWorldMinY - 1;
    auto overhead_block =
        to_block_id(BlockType::PoolroomsTile);
    auto elevated_feature =
        BackroomsElevatedFeature::None;
    const auto rib_phase =
        static_cast<int>(
            hash_position(
                layout_seed_,
                descriptor.module_x,
                descriptor.module_z,
                0x73CE419DULL) %
            17U);
    const auto arch_rib =
        descriptor.primary_axis_x
            ? positive_modulo(local_x + rib_phase, 18) == 0
            : positive_modulo(local_z + rib_phase, 18) == 0;
    if (connector_flight &&
        connector_elevated.has_value()) {
        const auto connector_deck_y =
            std::min(
                kBackroomsFloorY + 7,
                ceiling_y - 2);
        const auto stair_index = static_cast<int>(
            -5 - connector_forward);
        overhead_bottom_y =
            std::min(
                kBackroomsFloorY + 5 +
                    stair_index / 2,
                connector_deck_y);
        overhead_top_y = overhead_bottom_y;
        overhead_block =
            connector_elevated->style ==
                    BackroomsConnectorStyle::Slide
                ? to_block_id(
                      BlockType::PoolroomsPlastic)
                : to_block_id(
                      BlockType::PoolroomsMetal);
        elevated_feature =
            connector_elevated->style ==
                    BackroomsConnectorStyle::Slide
                ? BackroomsElevatedFeature::SlideChute
                : BackroomsElevatedFeature::StairFlight;
    } else if (connector_balcony &&
               connector_elevated.has_value()) {
        const auto deck_y =
            std::min(
                kBackroomsFloorY + 7,
                ceiling_y - 2);
        overhead_bottom_y = deck_y;
        overhead_top_y =
            std::abs(connector_lateral) == 5
                ? std::min(deck_y + 1, ceiling_y - 1)
                : deck_y;
        overhead_block =
            std::abs(connector_lateral) == 5
                ? connector_elevated->style ==
                          BackroomsConnectorStyle::Slide
                      ? to_block_id(
                            BlockType::PoolroomsPlastic)
                      : to_block_id(
                            BlockType::PoolroomsMetal)
                : to_block_id(BlockType::PoolroomsTile);
        elevated_feature =
            BackroomsElevatedFeature::Balcony;
    } else if (!wall &&
               connector_arch &&
               ceiling_height >= 8) {
        overhead_bottom_y =
            kBackroomsFloorY + 5;
        overhead_top_y =
            kBackroomsFloorY + 6;
        overhead_block =
            connector_structure->style ==
                    BackroomsConnectorStyle::Slide
                ? to_block_id(
                      BlockType::PoolroomsPlastic)
                : to_block_id(
                      BlockType::PoolroomsMetal);
        elevated_feature =
            BackroomsElevatedFeature::Arch;
    } else if (
        room_balcony &&
        (!wall || balcony_support) &&
        ceiling_height >= 8) {
        const auto deck_y =
            std::min(
                kBackroomsFloorY + 6,
                ceiling_y - 2);
        const auto railing =
            balcony_depth == 7 ||
            balcony_along == 11 ||
            balcony_along == 52;
        overhead_bottom_y = deck_y;
        overhead_top_y =
            railing
                ? std::min(deck_y + 1, ceiling_y - 1)
                : deck_y;
        overhead_block =
            railing
                ? to_block_id(BlockType::PoolroomsMetal)
                : to_block_id(BlockType::PoolroomsTile);
        elevated_feature =
            BackroomsElevatedFeature::Balcony;
    } else if (!wall && arch_rib && ceiling_height >= 8) {
        // Ces deux rangées forment un linteau voxel haut : je conserve au
        // minimum cinq mètres libres sous chaque arche.
        overhead_bottom_y = ceiling_y - 2;
        overhead_top_y = ceiling_y - 1;
        elevated_feature =
            BackroomsElevatedFeature::Arch;
    }

    auto deep_water = false;
    if (flooded_module &&
        pool_surface == BackroomsPoolSurface::Water) {
        const auto deep_hash =
            hash_position(
                layout_seed_,
                descriptor.module_x,
                descriptor.module_z,
                0xD72A40F19B65CE83ULL);
        const auto deep_module = deep_hash % 100U < 15U;
        const auto orientation =
            static_cast<int>((deep_hash >> 8U) % 4U);
        const auto along =
            orientation == 0 || orientation == 2
                ? local_x
                : local_z;
        const auto depth =
            orientation == 0
                ? local_z
                : orientation == 1
                      ? kBackroomsModuleSize - 1 - local_x
                      : orientation == 2
                            ? kBackroomsModuleSize - 1 - local_z
                            : local_x;
        const auto inside_lateral_pool =
            along >= 12 && along <= 51 &&
            depth >= 8 && depth <= 12;
        const auto route_clear =
            !is_guaranteed_route_in_rectangle(
                descriptor,
                local_x - kFloodedDeepPoolRouteMargin,
                local_z - kFloodedDeepPoolRouteMargin,
                local_x + kFloodedDeepPoolRouteMargin,
                local_z + kFloodedDeepPoolRouteMargin);
        const auto connector_clear =
            !connector_near(
                 world_x,
                 kBackroomsFloorY + 1,
                 world_z,
                 kFloodedDeepPoolConnectorMargin)
                 .has_value();
        deep_water =
            deep_module &&
            inside_lateral_pool &&
            route_clear &&
            connector_clear;
    }

    // Je fixe la géométrie seulement après avoir réservé les routes, les
    // connecteurs et les supports. Une cellule asséchée ne peut ainsi laisser
    // ni cuvette vide ni état d'eau résiduel.
    const auto wet =
        pool_surface == BackroomsPoolSurface::Water;
    const auto recessed =
        pool_geometry_profile_ !=
        BackroomsPoolGeometryProfile::LegacyFlat;
    const auto floor_y =
        wet && recessed
            ? kBackroomsFloorY - (deep_water ? 2 : 1)
            : kBackroomsFloorY;
    const auto water_bottom_y =
        wet
            ? recessed
                  ? floor_y + 1
                  : kBackroomsFloorY + 1
            : kWorldMinY - 1;
    const auto water_top_y =
        wet
            ? recessed
                  ? kBackroomsFloorY
                  : kBackroomsFloorY + 1
            : kWorldMinY - 1;

    auto floor_block =
        wet
            ? to_block_id(BlockType::PoolroomsWetTile)
            : pool_surface == BackroomsPoolSurface::Shore
                  ? to_block_id(BlockType::PoolroomsMetal)
                  : to_block_id(BlockType::PoolroomsTile);
    if (connector.has_value()) {
        // Je conserve l'identité visuelle du palier, même lorsque celui-ci a
        // remplacé une cellule qui appartenait initialement à un bassin.
        floor_block =
            connector->style == BackroomsConnectorStyle::Slide
                ? to_block_id(BlockType::PoolroomsPlastic)
                : to_block_id(BlockType::PoolroomsMetal);
    }

    const auto water_level =
        wet && flooded_module
            ? kMaxWaterLevel
            : wet && recessed
                  ? poolrooms_basin_water_level(
                        layout_seed_,
                        world_x,
                        world_z)
                  : kPoolroomsShallowWaterLevel;

    return {
        .floor_y = floor_y,
        .ceiling_y = ceiling_y,
        .wall_top_y = wall_top_y,
        .overhead_bottom_y = overhead_bottom_y,
        .overhead_top_y = overhead_top_y,
        .water_y = water_top_y,
        .water_bottom_y = water_bottom_y,
        .water_top_y = water_top_y,
        .foundation_block =
            to_block_id(BlockType::PoolroomsDarkTile),
        .roof_block =
            to_block_id(BlockType::PoolroomsTile),
        .floor_block = floor_block,
        .wall_block = wall_block,
        .ceiling_block = ceiling_block,
        .overhead_block = overhead_block,
        .water_state =
            wet
                ? make_water_state(
                      water_level,
                      true,
                      true)
                : WaterState {0},
        .wall = wall,
        .guaranteed_route = guaranteed_route,
        .light_state = light_state,
        .elevated_feature = elevated_feature,
        .pool_surface = pool_surface,
        .flooded_district = flooded_module,
        .deep_water = deep_water,
        .water_depth_cells =
            wet
                ? static_cast<std::uint8_t>(
                      water_top_y - water_bottom_y + 1)
                : static_cast<std::uint8_t>(0U),
    };
}

auto BackroomsGenerator::sample_column(
    int world_x,
    int world_z) const noexcept -> BackroomsColumnSample {

    const auto descriptor = descriptor_at(world_x, world_z);
    const auto local_x = local_coordinate(world_x);
    const auto local_z = local_coordinate(world_z);
    if (is_poolrooms()) {
        return sample_poolrooms_column(
            descriptor,
            world_x,
            world_z,
            local_x,
            local_z);
    }

    auto wall_top_y =
        wall_height_at(descriptor, local_x, local_z);
    auto wall = wall_top_y > kBackroomsFloorY;
    const auto ceiling_y =
        kBackroomsFloorY +
        ceiling_height_at(descriptor, local_x, local_z);
    const auto guaranteed_route =
        is_guaranteed_route(descriptor, local_x, local_z);
    auto wall_block =
        wall_block_for(descriptor.palette);
    auto floor_block =
        floor_block_for(descriptor, local_x, local_z);
    const auto connector_pad =
        connector_near(
            world_x,
            kBackroomsFloorY + 1,
            world_z,
            2);
    if (connector_pad.has_value()) {
        floor_block =
            to_block_id(BlockType::PoolroomsMetal);
    }

    const auto connector_structure =
        connector_near(
            world_x,
            kBackroomsFloorY + 1,
            world_z,
            4);
    auto connector_arch = false;
    if (connector_structure.has_value()) {
        wall = false;
        wall_top_y = kBackroomsFloorY;
        const auto delta_x =
            static_cast<std::int64_t>(world_x) -
            connector_structure->trigger_block.x;
        const auto delta_z =
            static_cast<std::int64_t>(world_z) -
            connector_structure->trigger_block.z;
        const auto orientation =
            static_cast<int>(
                connector_structure->
                    destination_yaw_degrees /
                90.0F) %
            4;
        const auto forward =
            orientation == 0
                ? delta_z
                : orientation == 1
                      ? delta_x
                      : orientation == 2
                            ? -delta_z
                            : -delta_x;
        const auto lateral =
            orientation == 0 || orientation == 2
                ? delta_x
                : delta_z;
        const auto corner_post =
            std::abs(delta_x) == 4 &&
            std::abs(delta_z) == 4;
        const auto side_rail =
            std::abs(lateral) == 4 &&
            forward >= -3 &&
            forward <= 4;
        connector_arch =
            forward == -4 &&
            std::abs(lateral) <= 4;
        if ((corner_post || side_rail) &&
            !guaranteed_route) {
            wall = true;
            wall_top_y =
                kBackroomsFloorY +
                (corner_post ? 4 : 1);
            wall_block =
                to_block_id(BlockType::PoolroomsMetal);
        }
    }

    const auto decoration_hash =
        hash_position(
            layout_seed_,
            floor_division(world_x, 5),
            floor_division(world_z, 5),
            0x29D56A83ULL);
    const auto decoration_anchor =
        positive_modulo64(
            static_cast<std::int64_t>(world_x) +
                static_cast<int>(decoration_hash & 7U),
            13) ==
            4 &&
        positive_modulo64(
            static_cast<std::int64_t>(world_z) +
                static_cast<int>((decoration_hash >> 3U) & 7U),
            13) ==
            4;
    if (!wall &&
        !guaranteed_route &&
        decoration_anchor &&
        decoration_hash % 100U < 38U) {
        wall = true;
        wall_top_y = kBackroomsFloorY + 1;
        const auto decoration_type =
            (decoration_hash >> 8U) % 10U;
        wall_block =
            decoration_type < 5U
                ? to_block_id(BlockType::BackroomsDesk)
                : decoration_type < 8U
                      ? to_block_id(BlockType::BackroomsChair)
                      : to_block_id(BlockType::BackroomsPlant);
    }

    const auto light_state =
        light_state_at(
            descriptor,
            world_x,
            world_z,
            local_x,
            local_z,
            wall);

    auto ceiling_block =
        to_block_id(BlockType::BackroomsCeilingTile);
    switch (light_state) {
    case BackroomsLightState::Active:
        ceiling_block =
            to_block_id(BlockType::BackroomsFluorescentLight);
        break;
    case BackroomsLightState::Failed:
        ceiling_block =
            to_block_id(BlockType::BackroomsFailedLight);
        break;
    case BackroomsLightState::Emergency:
        ceiling_block =
            to_block_id(BlockType::BackroomsEmergencyLight);
        break;
    case BackroomsLightState::None:
    default:
        break;
    }

    auto overhead_bottom_y = kWorldMaxY + 1;
    auto overhead_top_y = kWorldMinY - 1;
    auto overhead_block =
        to_block_id(BlockType::Air);
    if (!wall &&
        connector_arch &&
        ceiling_y >= kBackroomsFloorY + 8) {
        overhead_bottom_y =
            kBackroomsFloorY + 5;
        overhead_top_y =
            kBackroomsFloorY + 6;
        overhead_block =
            to_block_id(BlockType::PoolroomsMetal);
    }

    return {
        .floor_y = kBackroomsFloorY,
        .ceiling_y = ceiling_y,
        .wall_top_y = wall_top_y,
        .overhead_bottom_y = overhead_bottom_y,
        .overhead_top_y = overhead_top_y,
        .foundation_block =
            to_block_id(BlockType::BackroomsConcrete),
        .roof_block =
            to_block_id(BlockType::BackroomsConcrete),
        .floor_block = floor_block,
        .wall_block = wall_block,
        .ceiling_block = ceiling_block,
        .overhead_block = overhead_block,
        .wall = wall,
        .guaranteed_route = guaranteed_route,
        .light_state = light_state,
    };
}

auto BackroomsGenerator::light_fixture_at(
    int world_x,
    int world_z) const noexcept
    -> std::optional<BackroomsLightFixtureLayout> {
    const auto descriptor = descriptor_at(world_x, world_z);
    const auto sample = sample_column(world_x, world_z);
    if (sample.light_state == BackroomsLightState::None) {
        return std::nullopt;
    }

    const auto fixture =
        backrooms_fixture_pattern_at(
            descriptor,
            layout_seed_,
            world_x,
            world_z,
            local_coordinate(world_x),
            local_coordinate(world_z),
            sample.wall);
    if (!fixture.has_value()) {
        return std::nullopt;
    }

    return BackroomsLightFixtureLayout {
        .logical_level = logical_level_,
        .module_x = descriptor.module_x,
        .module_z = descriptor.module_z,
        .nominal_anchor_x = fixture->nominal_anchor_x,
        .nominal_anchor_z = fixture->nominal_anchor_z,
        .ceiling_y = sample.ceiling_y,
        .nominal_length = fixture->nominal_length,
        .connector_district_modules = connector_district_modules_,
        .primary_axis_x = descriptor.primary_axis_x,
        .theme = descriptor.theme,
        .archetype = descriptor.archetype,
        .state = sample.light_state,
        .pool_geometry_profile = pool_geometry_profile_,
        .block = sample.ceiling_block,
    };
}

auto BackroomsGenerator::sample_block(
    int world_x,
    int y,
    int world_z) const noexcept -> BlockId {

    if (!is_world_y_valid(y)) {
        return to_block_id(BlockType::Air);
    }

    const auto column = sample_column(world_x, world_z);
    if (y < column.floor_y) {
        return column.foundation_block;
    }
    if (y == column.floor_y) {
        return column.floor_block;
    }
    if (column.wall &&
        y > column.floor_y &&
        y <= column.wall_top_y) {
        return column.wall_block;
    }
    if (y >= column.overhead_bottom_y &&
        y <= column.overhead_top_y) {
        return column.overhead_block;
    }
    if (y == column.ceiling_y) {
        return column.ceiling_block;
    }
    if (y > column.ceiling_y && y <= kBackroomsRoofY) {
        // Je remplis toute la masse située au-dessus du faux plafond. Deux
        // salles de hauteurs différentes obtiennent ainsi une vraie retombée
        // solide entre elles, sans aucune vue sur le vide extérieur. J'utilise
        // un matériau de toiture pour ne jamais prolonger un néon émissif.
        // Je m'arrête à la hauteur maximale des salles : cette dalle commune
        // ferme le monde tout en évitant de mailler des sections inaccessibles.
        return column.roof_block;
    }
    return to_block_id(BlockType::Air);
}

auto BackroomsGenerator::sample_water_state(
    int world_x,
    int y,
    int world_z) const noexcept -> WaterState {

    if (!is_world_y_valid(y)) {
        return 0;
    }
    const auto column = sample_column(world_x, world_z);
    return column.water_state != WaterState {0} &&
                   y >= column.water_bottom_y &&
                   y <= column.water_top_y
               ? column.water_state
               : WaterState {0};
}

auto BackroomsGenerator::is_walkable(
    int world_x,
    int world_z) const noexcept -> bool {

    const auto column = sample_column(world_x, world_z);
    return !column.wall &&
           column.ceiling_y - column.floor_y >= 5;
}

auto BackroomsGenerator::spawn_block() const noexcept -> BlockCoord {
    const auto descriptor = module_descriptor(0, 0);
    return {
        descriptor.hub_x,
        kBackroomsFloorY + 1,
        descriptor.hub_z,
    };
}

} // namespace valcraft
