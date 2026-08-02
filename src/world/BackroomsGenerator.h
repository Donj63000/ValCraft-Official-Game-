#pragma once

#include "world/Block.h"

#include <cstdint>
#include <optional>

namespace valcraft {

inline constexpr int kBackroomsModuleSize = 64;
inline constexpr int kBackroomsFloorY = 40;
inline constexpr int kBackroomsMaxCeilingHeight = 26;
inline constexpr int kBackroomsRoofY =
    kBackroomsFloorY + kBackroomsMaxCeilingHeight;
inline constexpr int kBackroomsPortalHalfWidth = 2;
inline constexpr int kBackroomsConnectorDistrictModules = 4;

enum class BackroomsTheme : std::uint8_t {
    Offices = 0,
    Poolrooms = 1,
};

enum class BackroomsConnectorDirection : std::uint8_t {
    Up = 0,
    Down = 1,
};

enum class BackroomsConnectorStyle : std::uint8_t {
    Stairs = 0,
    Slide = 1,
};

enum class BackroomsArchetype : std::uint8_t {
    ClassicOffice = 0,
    CubicleFarm = 1,
    CompressionMaze = 2,
    LongCorridor = 3,
    GrandHall = 4,
    PillarGallery = 5,
    NestedRooms = 6,
    Anomaly = 7,
    Blackout = 8,
};

enum class BackroomsPalette : std::uint8_t {
    NicotineYellow = 0,
    SickGreen = 1,
    WashedBlue = 2,
    FadedRose = 3,
    Oxide = 4,
    RawConcrete = 5,
};

enum class BackroomsTension : std::uint8_t {
    Familiarity = 0,
    Compression = 1,
    Expansion = 2,
    Repetition = 3,
    Anomaly = 4,
    Blackout = 5,
};

enum class BackroomsLightState : std::uint8_t {
    None = 0,
    Active = 1,
    Failed = 2,
    Emergency = 3,
};

enum class BackroomsElevatedFeature : std::uint8_t {
    None = 0,
    Arch = 1,
    Balcony = 2,
    StairFlight = 3,
    SlideChute = 4,
};

enum class BackroomsPoolSurface : std::uint8_t {
    Dry = 0,
    Shore = 1,
    Water = 2,
};

enum class BackroomsPoolGeometryProfile : std::uint8_t {
    LegacyFlat = 0,
    RecessedOneBlock = 1,
    FloodedDistrictsV4 = 2,
};

struct BackroomsModuleDescriptor {
    int module_x = 0;
    int module_z = 0;
    BackroomsTheme theme = BackroomsTheme::Offices;
    BackroomsArchetype archetype = BackroomsArchetype::ClassicOffice;
    BackroomsPalette palette = BackroomsPalette::NicotineYellow;
    BackroomsTension tension = BackroomsTension::Familiarity;
    int hub_x = kBackroomsModuleSize / 2;
    int hub_z = kBackroomsModuleSize / 2;
    int north_portal_x = kBackroomsModuleSize / 2;
    int south_portal_x = kBackroomsModuleSize / 2;
    int west_portal_z = kBackroomsModuleSize / 2;
    int east_portal_z = kBackroomsModuleSize / 2;
    int base_ceiling_height = 7;
    bool primary_axis_x = true;

    auto operator==(const BackroomsModuleDescriptor&) const -> bool = default;
};

struct BackroomsColumnSample {
    int floor_y = kBackroomsFloorY;
    int ceiling_y = kBackroomsFloorY + 7;
    int wall_top_y = kBackroomsFloorY;
    int overhead_bottom_y = kWorldMaxY + 1;
    int overhead_top_y = kWorldMinY - 1;
    // Je garde water_y comme alias de la surface pour les consommateurs V1-V3,
    // puis je decris le volume V4 avec un intervalle vertical explicite.
    int water_y = kWorldMinY - 1;
    int water_bottom_y = kWorldMinY - 1;
    int water_top_y = kWorldMinY - 1;
    BlockId foundation_block = to_block_id(BlockType::Stone);
    BlockId roof_block = to_block_id(BlockType::Stone);
    BlockId floor_block = to_block_id(BlockType::Dirt);
    BlockId wall_block = to_block_id(BlockType::Sand);
    BlockId ceiling_block = to_block_id(BlockType::Stone);
    BlockId overhead_block = to_block_id(BlockType::Air);
    WaterState water_state = 0;
    bool wall = false;
    bool guaranteed_route = false;
    BackroomsLightState light_state = BackroomsLightState::None;
    BackroomsElevatedFeature elevated_feature =
        BackroomsElevatedFeature::None;
    BackroomsPoolSurface pool_surface =
        BackroomsPoolSurface::Dry;
    bool flooded_district = false;
    bool deep_water = false;
    std::uint8_t water_depth_cells = 0U;

    auto operator==(const BackroomsColumnSample&) const -> bool = default;
};

struct BackroomsLevelConnector {
    BackroomsConnectorDirection direction = BackroomsConnectorDirection::Up;
    BackroomsConnectorStyle style = BackroomsConnectorStyle::Stairs;
    int destination_level = 0;
    BlockCoord trigger_block {};
    BlockCoord destination_landing_block {};
    float destination_yaw_degrees = 0.0F;

    auto operator==(const BackroomsLevelConnector&) const -> bool = default;
};

class BackroomsGenerator {
public:
    BackroomsGenerator() noexcept;
    explicit BackroomsGenerator(
        int seed,
        int logical_level = 0,
        int connector_district_modules =
            kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile pool_geometry_profile =
            BackroomsPoolGeometryProfile::LegacyFlat) noexcept;

    [[nodiscard]] auto seed() const noexcept -> int;
    [[nodiscard]] auto logical_level() const noexcept -> int;
    [[nodiscard]] auto pool_geometry_profile() const noexcept
        -> BackroomsPoolGeometryProfile;
    [[nodiscard]] auto theme() const noexcept -> BackroomsTheme;
    [[nodiscard]] auto is_poolrooms() const noexcept -> bool;
    [[nodiscard]] auto is_flooded_module(
        int module_x,
        int module_z) const noexcept -> bool;
    [[nodiscard]] auto is_flooded_at(
        int world_x,
        int world_z) const noexcept -> bool;
    [[nodiscard]] auto module_descriptor(int module_x, int module_z) const noexcept
        -> BackroomsModuleDescriptor;
    [[nodiscard]] auto descriptor_at(int world_x, int world_z) const noexcept
        -> BackroomsModuleDescriptor;
    [[nodiscard]] auto sample_column(int world_x, int world_z) const noexcept
        -> BackroomsColumnSample;
    [[nodiscard]] auto sample_block(int world_x, int y, int world_z) const noexcept
        -> BlockId;
    [[nodiscard]] auto sample_water_state(
        int world_x,
        int y,
        int world_z) const noexcept -> WaterState;
    [[nodiscard]] auto is_walkable(int world_x, int world_z) const noexcept -> bool;
    [[nodiscard]] auto spawn_block() const noexcept -> BlockCoord;
    [[nodiscard]] auto connector_near(
        int world_x,
        int world_y,
        int world_z,
        int horizontal_radius = 1) const noexcept
        -> std::optional<BackroomsLevelConnector>;
    [[nodiscard]] auto connector_in_district(
        BackroomsConnectorDirection direction,
        int district_x,
        int district_z) const noexcept -> BackroomsLevelConnector;

    [[nodiscard]] static auto module_coordinate(int world_coordinate) noexcept -> int;
    [[nodiscard]] static auto local_coordinate(int world_coordinate) noexcept -> int;

private:
    [[nodiscard]] auto wall_block_for(BackroomsPalette palette) const noexcept -> BlockId;
    [[nodiscard]] auto floor_block_for(
        const BackroomsModuleDescriptor& descriptor,
        int local_x,
        int local_z) const noexcept -> BlockId;
    [[nodiscard]] auto ceiling_height_at(
        const BackroomsModuleDescriptor& descriptor,
        int local_x,
        int local_z) const noexcept -> int;
    [[nodiscard]] auto is_guaranteed_route(
        const BackroomsModuleDescriptor& descriptor,
        int local_x,
        int local_z) const noexcept -> bool;
    [[nodiscard]] auto is_guaranteed_route_in_rectangle(
        const BackroomsModuleDescriptor& descriptor,
        int minimum_local_x,
        int minimum_local_z,
        int maximum_local_x,
        int maximum_local_z) const noexcept -> bool;
    [[nodiscard]] auto wall_height_at(
        const BackroomsModuleDescriptor& descriptor,
        int local_x,
        int local_z) const noexcept -> int;
    [[nodiscard]] auto light_state_at(
        const BackroomsModuleDescriptor& descriptor,
        int world_x,
        int world_z,
        int local_x,
        int local_z,
        bool wall) const noexcept -> BackroomsLightState;
    [[nodiscard]] auto sample_poolrooms_column(
        const BackroomsModuleDescriptor& descriptor,
        int world_x,
        int world_z,
        int local_x,
        int local_z) const noexcept -> BackroomsColumnSample;
    int seed_ = 1337;
    int logical_level_ = 0;
    int layout_seed_ = 1337;
    int connector_district_modules_ =
        kBackroomsConnectorDistrictModules;
    BackroomsPoolGeometryProfile pool_geometry_profile_ =
        BackroomsPoolGeometryProfile::LegacyFlat;
};

} // namespace valcraft
