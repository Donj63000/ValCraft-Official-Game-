#include "gameplay/ShipCrew.h"

#include "gameplay/SeaAdventure.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kCrewWalkSpeed = 1.22F;
constexpr float kCrewHurtSeconds = 0.45F;
constexpr float kCrewRecoverAnimationSeconds = 1.0F;
constexpr float kFishingStormStop = 0.65F;
constexpr std::size_t kStationCount = static_cast<std::size_t>(ShipCrewStation::Count);

constexpr std::array<ShipCrewRole, kShipCrewMemberCount> kCanonicalRoles {{
    ShipCrewRole::Captain,
    ShipCrewRole::Fisher,
    ShipCrewRole::Rigger,
    ShipCrewRole::WaterTender,
    ShipCrewRole::Deckhand,
    ShipCrewRole::Quartermaster,
}};

auto finite_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) ? value : fallback;
}

auto finite_vec3_or(const glm::vec3& value, const glm::vec3& fallback) noexcept -> glm::vec3 {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) ? value : fallback;
}

auto hash_u32(std::uint32_t value) noexcept -> std::uint32_t {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

auto normalized_angle(float angle) noexcept -> float {
    return std::remainder(finite_or(angle, 0.0F), 2.0F * kPi);
}

auto known_activity(ShipCrewActivity activity) noexcept -> bool {
    switch (activity) {
    case ShipCrewActivity::Idle:
    case ShipCrewActivity::Steer:
    case ShipCrewActivity::Inspect:
    case ShipCrewActivity::Fish:
    case ShipCrewActivity::TendWater:
    case ShipCrewActivity::Carry:
    case ShipCrewActivity::HaulRope:
    case ShipCrewActivity::Scrub:
    case ShipCrewActivity::TurnCapstan:
    case ShipCrewActivity::SortCargo:
    case ShipCrewActivity::Socialize:
    case ShipCrewActivity::Rest:
        return true;
    }
    return false;
}

auto known_cargo(ShipCrewCargo cargo) noexcept -> bool {
    return cargo == ShipCrewCargo::None || cargo == ShipCrewCargo::Fish || cargo == ShipCrewCargo::Water;
}

auto known_station(ShipCrewStation station) noexcept -> bool {
    return static_cast<std::size_t>(station) < kStationCount;
}

auto fallback_station(ShipCrewRole role) noexcept -> ShipCrewStation {
    switch (role) {
    case ShipCrewRole::Captain:
        return ShipCrewStation::Helm;
    case ShipCrewRole::Fisher:
        return ShipCrewStation::PortFishing;
    case ShipCrewRole::Rigger:
        return ShipCrewStation::MainMast;
    case ShipCrewRole::WaterTender:
        return ShipCrewStation::WaterStill;
    case ShipCrewRole::Quartermaster:
        return ShipCrewStation::CargoSort;
    case ShipCrewRole::Deckhand:
    default:
        return ShipCrewStation::AftDeck;
    }
}

auto node_for(const ShipBlueprint& blueprint, ShipCrewStation station) noexcept
    -> const ShipCrewNavigationNode* {
    for (const auto& node : blueprint.crew_navigation_nodes) {
        if (node.station == station) {
            return &node;
        }
    }
    return nullptr;
}

auto node_position(const ShipBlueprint& blueprint, ShipCrewStation station) noexcept -> glm::vec3 {
    if (const auto* node = node_for(blueprint, station); node != nullptr) {
        return node->local_position;
    }
    return {};
}

auto station_is_exterior(const ShipBlueprint& blueprint, ShipCrewStation station) noexcept -> bool {
    if (const auto* node = node_for(blueprint, station); node != nullptr) {
        return node->exterior;
    }
    return true;
}

auto stations_are_adjacent(const ShipBlueprint& blueprint,
                           ShipCrewStation first,
                           ShipCrewStation second) noexcept -> bool {
    if (first == second) {
        return true;
    }
    return std::ranges::any_of(blueprint.crew_navigation_edges, [&](const ShipCrewNavigationEdge& edge) {
        return (edge.first == first && edge.second == second) ||
               (edge.first == second && edge.second == first);
    });
}

auto station_yaw(ShipCrewStation station) noexcept -> float {
    switch (station) {
    case ShipCrewStation::Helm:
    case ShipCrewStation::AftDeck:
    case ShipCrewStation::MidDeckPort:
    case ShipCrewStation::MidDeckStarboard:
    case ShipCrewStation::ForeDeck:
    case ShipCrewStation::CargoSort:
        return 0.5F * kPi;
    case ShipCrewStation::AftWatch:
    case ShipCrewStation::ForeStairsTop:
    case ShipCrewStation::ForeStairsMid:
    case ShipCrewStation::ForeStairsBottom:
        return -0.5F * kPi;
    case ShipCrewStation::PortFishing:
    case ShipCrewStation::Galley:
    case ShipCrewStation::CargoFish:
        return kPi;
    case ShipCrewStation::StarboardFishing:
    case ShipCrewStation::MainMast:
    case ShipCrewStation::ForeMast:
    case ShipCrewStation::MizzenMast:
    case ShipCrewStation::WaterStill:
    case ShipCrewStation::Capstan:
    case ShipCrewStation::CargoWater:
        return 0.0F;
    case ShipCrewStation::AftStairsTop:
    case ShipCrewStation::AftStairsMid:
    case ShipCrewStation::AftStairsBottom:
    case ShipCrewStation::ChartTable:
    case ShipCrewStation::CaptainCabin:
    case ShipCrewStation::CrewBunks:
    case ShipCrewStation::MessTable:
    case ShipCrewStation::ForeHatchPortA:
    case ShipCrewStation::ForeHatchPortB:
    case ShipCrewStation::HelmBypassPort:
    case ShipCrewStation::QuarterdeckStepTop:
    case ShipCrewStation::QuarterdeckStepBottom:
    case ShipCrewStation::ForecastleStepBottom:
    case ShipCrewStation::ForecastleStepTop:
    case ShipCrewStation::ForeStairsExitCenter:
    case ShipCrewStation::ForeStairsExitPort:
    case ShipCrewStation::AftCabinDoor:
    case ShipCrewStation::AftLowerPortA:
    case ShipCrewStation::AftLowerPortB:
    case ShipCrewStation::ForeLowerPortA:
    case ShipCrewStation::ForeLowerPortB:
    case ShipCrewStation::WaterStillApproach:
    case ShipCrewStation::Count:
    default:
        return 0.5F * kPi;
    }
}

auto shared_station_visual_offset(const ShipCrewMemberSaveState& member) noexcept -> glm::vec3 {
    switch (member.current_station) {
    case ShipCrewStation::MessTable:
        if (member.role == ShipCrewRole::Fisher) return {-0.75F, 0.0F, 0.0F};
        if (member.role == ShipCrewRole::Rigger) return {0.75F, 0.0F, 0.0F};
        if (member.role == ShipCrewRole::Quartermaster) return {0.0F, 0.0F, 0.75F};
        break;
    case ShipCrewStation::Capstan:
        if (member.role == ShipCrewRole::Rigger) return {0.0F, 0.0F, -0.55F};
        if (member.role == ShipCrewRole::Deckhand) return {0.0F, 0.0F, 0.55F};
        break;
    case ShipCrewStation::CargoFish:
        if (member.role == ShipCrewRole::Fisher) return {0.0F, 0.0F, -0.50F};
        if (member.role == ShipCrewRole::Quartermaster) return {0.0F, 0.0F, 0.50F};
        break;
    case ShipCrewStation::CargoWater:
        if (member.role == ShipCrewRole::WaterTender) return {0.0F, 0.0F, -0.50F};
        if (member.role == ShipCrewRole::Quartermaster) return {0.0F, 0.0F, 0.50F};
        break;
    default:
        break;
    }
    return {};
}

auto next_hop(const ShipBlueprint& blueprint,
              ShipCrewStation start,
              ShipCrewStation destination) noexcept -> std::optional<ShipCrewStation> {
    if (!known_station(start) || !known_station(destination) ||
        node_for(blueprint, start) == nullptr || node_for(blueprint, destination) == nullptr) {
        return std::nullopt;
    }
    if (start == destination) {
        return start;
    }

    std::array<int, kStationCount> previous {};
    previous.fill(-1);
    std::array<ShipCrewStation, kStationCount> queue {};
    std::size_t read_index = 0U;
    std::size_t write_index = 0U;
    const auto start_index = static_cast<std::size_t>(start);
    const auto destination_index = static_cast<std::size_t>(destination);
    previous[start_index] = static_cast<int>(start_index);
    queue[write_index++] = start;

    while (read_index < write_index && previous[destination_index] < 0) {
        const auto current = queue[read_index++];
        for (const auto& edge : blueprint.crew_navigation_edges) {
            auto neighbor = ShipCrewStation::Count;
            if (edge.first == current) {
                neighbor = edge.second;
            } else if (edge.second == current) {
                neighbor = edge.first;
            }
            if (!known_station(neighbor) || node_for(blueprint, neighbor) == nullptr) {
                continue;
            }
            const auto neighbor_index = static_cast<std::size_t>(neighbor);
            if (previous[neighbor_index] >= 0 || write_index >= queue.size()) {
                continue;
            }
            previous[neighbor_index] = static_cast<int>(static_cast<std::size_t>(current));
            queue[write_index++] = neighbor;
        }
    }

    if (previous[destination_index] < 0) {
        return std::nullopt;
    }

    auto cursor = destination_index;
    while (previous[cursor] != static_cast<int>(start_index)) {
        const auto parent = previous[cursor];
        if (parent < 0 || static_cast<std::size_t>(parent) == cursor) {
            return std::nullopt;
        }
        cursor = static_cast<std::size_t>(parent);
    }
    return static_cast<ShipCrewStation>(cursor);
}

auto distance_to_segment(const glm::vec3& point, const glm::vec3& start, const glm::vec3& end) noexcept -> float {
    const auto edge = end - start;
    const auto length_squared = glm::dot(edge, edge);
    if (length_squared <= 1.0e-6F) {
        return glm::length(point - start);
    }
    const auto amount = std::clamp(glm::dot(point - start, edge) / length_squared, 0.0F, 1.0F);
    return glm::length(point - (start + edge * amount));
}

void route_to(ShipCrewMemberSaveState& member,
              const ShipBlueprint& blueprint,
              ShipCrewStation destination,
              ShipCrewActivity activity) noexcept {
    if (!known_station(destination) || node_for(blueprint, destination) == nullptr) {
        return;
    }
    const auto destination_changed = member.destination_station != destination;
    const auto activity_changed = member.activity != activity;
    member.destination_station = destination;
    member.activity = activity;
    if (destination_changed || activity_changed) {
        member.activity_timer = 0.0F;
    }
    if (member.current_station == member.next_station && member.current_station != destination) {
        if (const auto hop = next_hop(blueprint, member.current_station, destination); hop.has_value()) {
            member.next_station = *hop;
        }
    }
}

struct RoutineTask {
    ShipCrewStation station = ShipCrewStation::AftDeck;
    ShipCrewActivity activity = ShipCrewActivity::Idle;
    float duration = 8.0F;
};

auto routine_task(ShipCrewRole role, std::uint8_t step) noexcept -> RoutineTask {
    switch (role) {
    case ShipCrewRole::Captain:
        switch (step % 4U) {
        case 0U: return {ShipCrewStation::Helm, ShipCrewActivity::Steer, 18.0F};
        case 1U: return {ShipCrewStation::ChartTable, ShipCrewActivity::Inspect, 10.0F};
        case 2U: return {ShipCrewStation::AftWatch, ShipCrewActivity::Inspect, 12.0F};
        default: return {ShipCrewStation::CaptainCabin, ShipCrewActivity::Rest, 14.0F};
        }
    case ShipCrewRole::Rigger:
        switch (step % 4U) {
        case 0U: return {ShipCrewStation::MainMast, ShipCrewActivity::HaulRope, 12.0F};
        case 1U: return {ShipCrewStation::ForeMast, ShipCrewActivity::HaulRope, 11.0F};
        case 2U: return {ShipCrewStation::Capstan, ShipCrewActivity::TurnCapstan, 10.0F};
        default: return {ShipCrewStation::MessTable, ShipCrewActivity::Socialize, 8.0F};
        }
    case ShipCrewRole::Deckhand:
        switch (step % 4U) {
        case 0U: return {ShipCrewStation::AftDeck, ShipCrewActivity::Scrub, 12.0F};
        case 1U: return {ShipCrewStation::MidDeckStarboard, ShipCrewActivity::Scrub, 10.0F};
        case 2U: return {ShipCrewStation::Capstan, ShipCrewActivity::TurnCapstan, 10.0F};
        default: return {ShipCrewStation::CrewBunks, ShipCrewActivity::Rest, 9.0F};
        }
    case ShipCrewRole::Quartermaster:
        switch (step % 4U) {
        case 0U: return {ShipCrewStation::CargoSort, ShipCrewActivity::SortCargo, 14.0F};
        case 1U: return {ShipCrewStation::CargoFish, ShipCrewActivity::SortCargo, 8.0F};
        case 2U: return {ShipCrewStation::CargoWater, ShipCrewActivity::SortCargo, 8.0F};
        default: return {ShipCrewStation::MessTable, ShipCrewActivity::Socialize, 9.0F};
        }
    case ShipCrewRole::Fisher:
        return {ShipCrewStation::PortFishing, ShipCrewActivity::Fish, kAutomaticFishWorkSeconds};
    case ShipCrewRole::WaterTender:
        return {ShipCrewStation::WaterStill, ShipCrewActivity::TendWater, kAutomaticWaterWorkSeconds};
    }
    return {};
}

auto visual_role(ShipCrewRole role) noexcept -> CrewVisualRole {
    switch (role) {
    case ShipCrewRole::Captain: return CrewVisualRole::Captain;
    case ShipCrewRole::Fisher: return CrewVisualRole::Fisher;
    case ShipCrewRole::Rigger: return CrewVisualRole::Rigger;
    case ShipCrewRole::WaterTender: return CrewVisualRole::WaterTender;
    case ShipCrewRole::Quartermaster: return CrewVisualRole::Quartermaster;
    case ShipCrewRole::Deckhand:
    default: return CrewVisualRole::Deckhand;
    }
}

auto visual_activity(const ShipCrewMemberSaveState& member,
                     float activity_phase,
                     float recover_timer,
                     bool moving) noexcept -> CrewVisualActivity {
    if (member.recovery_timer > 0.0F || member.health <= 0.0F) {
        return CrewVisualActivity::KnockedOut;
    }
    if (recover_timer > 0.0F) {
        return CrewVisualActivity::Recover;
    }
    if (moving) {
        return CrewVisualActivity::Walk;
    }
    switch (member.activity) {
    case ShipCrewActivity::Steer: return CrewVisualActivity::Steer;
    case ShipCrewActivity::Inspect: return CrewVisualActivity::Inspect;
    case ShipCrewActivity::Fish:
        if (activity_phase < 3.0F / kAutomaticFishWorkSeconds) {
            return CrewVisualActivity::FishCast;
        }
        return activity_phase > 1.0F - 3.0F / kAutomaticFishWorkSeconds
                   ? CrewVisualActivity::FishReel
                   : CrewVisualActivity::FishWait;
    case ShipCrewActivity::TendWater: return CrewVisualActivity::TendWater;
    case ShipCrewActivity::Carry: return CrewVisualActivity::Carry;
    case ShipCrewActivity::HaulRope: return CrewVisualActivity::HaulRope;
    case ShipCrewActivity::Scrub: return CrewVisualActivity::Scrub;
    case ShipCrewActivity::TurnCapstan: return CrewVisualActivity::TurnCapstan;
    case ShipCrewActivity::SortCargo: return CrewVisualActivity::SortCargo;
    case ShipCrewActivity::Socialize: return CrewVisualActivity::Socialize;
    case ShipCrewActivity::Rest: return CrewVisualActivity::Rest;
    case ShipCrewActivity::Idle:
    default: return CrewVisualActivity::Idle;
    }
}

auto ray_aabb_distance(const glm::vec3& origin,
                       const glm::vec3& direction,
                       const glm::vec3& min_corner,
                       const glm::vec3& max_corner,
                       float max_distance) noexcept -> std::optional<float> {
    auto near_distance = 0.0F;
    auto far_distance = max_distance;
    for (int axis = 0; axis < 3; ++axis) {
        const auto origin_axis = origin[axis];
        const auto direction_axis = direction[axis];
        if (std::abs(direction_axis) <= 1.0e-7F) {
            if (origin_axis < min_corner[axis] || origin_axis > max_corner[axis]) {
                return std::nullopt;
            }
            continue;
        }
        auto first = (min_corner[axis] - origin_axis) / direction_axis;
        auto second = (max_corner[axis] - origin_axis) / direction_axis;
        if (first > second) {
            std::swap(first, second);
        }
        near_distance = std::max(near_distance, first);
        far_distance = std::min(far_distance, second);
        if (near_distance > far_distance) {
            return std::nullopt;
        }
    }
    return near_distance <= max_distance ? std::optional<float> {near_distance} : std::nullopt;
}

auto cargo_light(const glm::vec3& local_position, std::span<const glm::vec3> lanterns) noexcept -> float {
    auto light = 0.10F;
    for (const auto& lantern : lanterns) {
        const auto distance = glm::length(local_position - lantern);
        light = std::max(light, std::clamp(1.0F - distance / 8.0F, 0.0F, 1.0F));
    }
    return light;
}

} // namespace

auto ship_crew_max_health(ShipCrewRole role) noexcept -> float {
    return role == ShipCrewRole::Captain ? 18.0F : 14.0F;
}

auto sanitize_ship_crew_save_state(const ShipCrewSaveState& state) noexcept -> ShipCrewSaveState {
    if (!state.initialized) {
        return {};
    }

    auto sanitized = state;
    sanitized.initialized = true;
    for (std::size_t index = 0; index < sanitized.members.size(); ++index) {
        auto& member = sanitized.members[index];
        member.id = static_cast<std::uint8_t>(index);
        member.role = kCanonicalRoles[index];
        member.local_position = finite_vec3_or(member.local_position, {});
        member.local_position.x = std::clamp(member.local_position.x, -20.0F, 20.0F);
        member.local_position.y = std::clamp(member.local_position.y, -1.0F, 10.0F);
        member.local_position.z = std::clamp(member.local_position.z, -50.0F, 50.0F);
        member.yaw_radians = normalized_angle(member.yaw_radians);
        member.animation_time = std::clamp(finite_or(member.animation_time, 0.0F), 0.0F, 86'400.0F);
        member.activity_timer = std::clamp(finite_or(member.activity_timer, 0.0F), 0.0F, 300.0F);
        const auto maximum_work = member.role == ShipCrewRole::Fisher
                                      ? kAutomaticFishWorkSeconds
                                      : (member.role == ShipCrewRole::WaterTender
                                             ? kAutomaticWaterWorkSeconds
                                             : 0.0F);
        member.work_progress = std::clamp(finite_or(member.work_progress, 0.0F), 0.0F, maximum_work);
        const auto maximum_health = ship_crew_max_health(member.role);
        member.health = std::clamp(finite_or(member.health, maximum_health), 0.0F, maximum_health);
        member.recovery_timer = std::clamp(finite_or(member.recovery_timer, 0.0F), 0.0F, kShipCrewKnockoutSeconds);
        member.hurt_timer = std::clamp(finite_or(member.hurt_timer, 0.0F), 0.0F, kCrewHurtSeconds);
        member.routine_step = static_cast<std::uint8_t>(member.routine_step % 4U);
        if (!known_activity(member.activity)) {
            member.activity = ShipCrewActivity::Idle;
        }
        if (!known_cargo(member.cargo) ||
            (member.cargo == ShipCrewCargo::Fish && member.role != ShipCrewRole::Fisher) ||
            (member.cargo == ShipCrewCargo::Water && member.role != ShipCrewRole::WaterTender)) {
            member.cargo = ShipCrewCargo::None;
        }
        const auto fallback = fallback_station(member.role);
        if (!known_station(member.current_station)) member.current_station = fallback;
        if (!known_station(member.next_station)) member.next_station = member.current_station;
        if (!known_station(member.destination_station)) member.destination_station = member.current_station;
        if (member.recovery_timer > 0.0F || member.health <= 0.0F) {
            member.health = 0.0F;
            if (member.recovery_timer <= 0.0F) {
                member.recovery_timer = kShipCrewKnockoutSeconds;
            }
        }
    }
    return sanitized;
}

void ShipCrewSystem::reset(int world_seed, const ShipEntity& ship) noexcept {
    initialize_canonical_roster(world_seed, ship);
}

void ShipCrewSystem::load_state(const ShipCrewSaveState& state, int world_seed, const ShipEntity& ship) noexcept {
    world_seed_ = world_seed;
    appearance_seed_ = hash_u32(static_cast<std::uint32_t>(world_seed) ^ 0xA6E11E5U);
    state_ = sanitize_ship_crew_save_state(state);
    const auto& blueprint = amelie_ship_blueprint();
    if (!state_.initialized) {
        initialize_canonical_roster(world_seed, ship);
        return;
    }
    if (state_.navigation_revision != blueprint.navigation_revision) {
        // Je repars de stations garanties par le nouveau plan, tout en
        // conservant les états humains et le travail déjà réellement fourni.
        // Seule une cargaison en transit dépend de l'ancien chemin et est
        // abandonnée pendant cette migration géométrique.
        const auto previous = state_;
        initialize_canonical_roster(world_seed, ship);
        for (std::size_t index = 0; index < state_.members.size(); ++index) {
            auto& migrated = state_.members[index];
            const auto& saved = previous.members[index];
            migrated.animation_time = saved.animation_time;
            migrated.work_progress = saved.work_progress;
            migrated.health = saved.health;
            migrated.recovery_timer = saved.recovery_timer;
            migrated.hurt_timer = saved.hurt_timer;
            migrated.cargo = ShipCrewCargo::None;
        }
    }
    restore_runtime_routes(ship);
    rebuild_render_instances(ship, {});
}

void ShipCrewSystem::initialize_canonical_roster(int world_seed, const ShipEntity& ship) noexcept {
    const auto& blueprint = amelie_ship_blueprint();
    state_ = {};
    state_.initialized = true;
    state_.navigation_revision = blueprint.navigation_revision;
    world_seed_ = world_seed;
    appearance_seed_ = hash_u32(static_cast<std::uint32_t>(world_seed) ^ 0xA6E11E5U);
    runtime_.fill({});

    for (std::size_t index = 0; index < state_.members.size(); ++index) {
        auto& member = state_.members[index];
        member.id = static_cast<std::uint8_t>(index);
        member.role = kCanonicalRoles[index];
        member.health = ship_crew_max_health(member.role);
        member.current_station = fallback_station(member.role);
        member.next_station = member.current_station;
        member.destination_station = member.current_station;
        member.local_position = node_position(blueprint, member.current_station);
        member.yaw_radians = station_yaw(member.current_station);
        const auto task = routine_task(member.role, 0U);
        member.activity = task.activity;
        if (task.station != member.current_station) {
            route_to(member, blueprint, task.station, task.activity);
        }
    }
    rebuild_render_instances(ship, {});
}

void ShipCrewSystem::restore_runtime_routes(const ShipEntity& ship) noexcept {
    const auto& blueprint = amelie_ship_blueprint();
    runtime_.fill({});
    for (auto& member : state_.members) {
        const auto fallback = fallback_station(member.role);
        if (node_for(blueprint, member.current_station) == nullptr ||
            node_for(blueprint, member.next_station) == nullptr ||
            node_for(blueprint, member.destination_station) == nullptr) {
            member.current_station = fallback;
            member.next_station = fallback;
            member.destination_station = fallback;
            member.local_position = node_position(blueprint, fallback);
        }

        const auto edge_start = node_position(blueprint, member.current_station);
        const auto edge_end = node_position(blueprint, member.next_station);
        if (member.current_station == member.next_station) {
            member.local_position = edge_start;
        } else if (!stations_are_adjacent(blueprint, member.current_station, member.next_station) ||
                   distance_to_segment(member.local_position, edge_start, edge_end) > 0.15F) {
            member.local_position = edge_start;
            member.next_station = member.current_station;
        }

        // Je répare aussi les sauvegardes v9 produites avant la correction du
        // réveil : une cargaison valide reprend toujours la direction de sa cale.
        if (member.role == ShipCrewRole::Fisher && member.cargo == ShipCrewCargo::Fish) {
            member.routine_step = 1U;
            route_to(member, blueprint, ShipCrewStation::CargoFish, ShipCrewActivity::Carry);
        } else if (member.role == ShipCrewRole::WaterTender && member.cargo == ShipCrewCargo::Water) {
            member.routine_step = 1U;
            route_to(member, blueprint, ShipCrewStation::CargoWater, ShipCrewActivity::Carry);
        }
        if (member.current_station != member.destination_station && member.current_station == member.next_station) {
            member.next_station = next_hop(blueprint, member.current_station, member.destination_station)
                                      .value_or(member.current_station);
        } else if (member.current_station == member.destination_station &&
                   member.current_station == member.next_station) {
            member.yaw_radians = station_yaw(member.current_station);
        }
    }
    (void)ship;
}

auto ShipCrewSystem::update(const ShipEntity& ship,
                            const EnvironmentState& environment,
                            float dt,
                            std::uint32_t& fish,
                            std::uint32_t& water_flasks) noexcept -> ShipCrewUpdateResult {
    ShipCrewUpdateResult result {};
    dt = std::clamp(finite_or(dt, 0.0F), 0.0F, 0.25F);
    const auto& blueprint = amelie_ship_blueprint();
    if (!state_.initialized || state_.navigation_revision != blueprint.navigation_revision) {
        initialize_canonical_roster(world_seed_, ship);
    }
    if (dt <= 0.0F) {
        rebuild_render_instances(ship, environment);
        return result;
    }
    const auto storm_intensity = std::clamp(finite_or(environment.storm_intensity, 0.0F), 0.0F, 1.0F);
    const auto precipitation_intensity =
        std::clamp(finite_or(environment.precipitation_intensity, 0.0F), 0.0F, 1.0F);

    for (std::size_t index = 0; index < state_.members.size(); ++index) {
        auto& member = state_.members[index];
        auto& runtime = runtime_[index];
        member.animation_time = std::fmod(member.animation_time + dt, 86'400.0F);
        member.hurt_timer = std::max(0.0F, member.hurt_timer - dt);
        runtime.recover_timer = std::max(0.0F, runtime.recover_timer - dt);

        if (member.recovery_timer > 0.0F || member.health <= 0.0F) {
            member.health = 0.0F;
            member.recovery_timer = std::max(0.0F, member.recovery_timer - dt);
            runtime.motion_amount = 0.0F;
            runtime.activity_phase = 1.0F - member.recovery_timer / kShipCrewKnockoutSeconds;
            if (member.recovery_timer <= 0.0F) {
                member.health = ship_crew_max_health(member.role);
                runtime.recover_timer = kCrewRecoverAnimationSeconds;
                runtime.activity_phase = 0.0F;
                member.current_station = fallback_station(member.role);
                member.next_station = member.current_station;
                member.destination_station = member.current_station;
                member.local_position = node_position(blueprint, member.current_station);
                member.yaw_radians = station_yaw(member.current_station);
                member.activity_timer = 0.0F;

                // Je conserve la cargaison pendant l'assommement, puis je
                // recrée explicitement son trajet vers la cale au réveil.
                // Sans cette transition, le marin restait figé à son poste
                // de repli avec une livraison impossible à terminer.
                if (member.role == ShipCrewRole::Fisher && member.cargo == ShipCrewCargo::Fish) {
                    member.routine_step = 1U;
                    route_to(member, blueprint, ShipCrewStation::CargoFish, ShipCrewActivity::Carry);
                } else if (member.role == ShipCrewRole::WaterTender &&
                           member.cargo == ShipCrewCargo::Water) {
                    member.routine_step = 1U;
                    route_to(member, blueprint, ShipCrewStation::CargoWater, ShipCrewActivity::Carry);
                }
            }
            continue;
        }
        if (runtime.recover_timer > 0.0F) {
            // Je laisse au marin une seconde entière pour se relever. Sa pose,
            // sa position et son travail progressent seulement après cette animation.
            runtime.motion_amount = 0.0F;
            runtime.activity_phase = 1.0F - runtime.recover_timer / kCrewRecoverAnimationSeconds;
            continue;
        }

        // Je coupe tout accumulateur lorsque la cible est pleine : liberer une
        // place impose ainsi un nouveau cycle complet, sans gain instantane.
        if (member.role == ShipCrewRole::Fisher && member.cargo == ShipCrewCargo::None) {
            if (fish >= kAutomaticFishTarget) {
                member.work_progress = 0.0F;
                route_to(member, blueprint, ShipCrewStation::MessTable, ShipCrewActivity::Socialize);
            } else if (storm_intensity >= kFishingStormStop) {
                route_to(member, blueprint, ShipCrewStation::MainMast, ShipCrewActivity::HaulRope);
            } else if (member.routine_step == 0U &&
                       (member.destination_station == ShipCrewStation::MessTable ||
                        member.destination_station == ShipCrewStation::MainMast)) {
                route_to(member, blueprint, ShipCrewStation::PortFishing, ShipCrewActivity::Fish);
            }
        } else if (member.role == ShipCrewRole::WaterTender && member.cargo == ShipCrewCargo::None &&
                   water_flasks >= kAutomaticWaterTarget) {
            member.work_progress = 0.0F;
            route_to(member, blueprint, ShipCrewStation::Galley, ShipCrewActivity::Socialize);
        } else if (member.role == ShipCrewRole::WaterTender && member.cargo == ShipCrewCargo::None &&
                   water_flasks < kAutomaticWaterTarget && member.routine_step == 0U &&
                   member.destination_station == ShipCrewStation::Galley) {
            route_to(member, blueprint, ShipCrewStation::WaterStill, ShipCrewActivity::TendWater);
        }

        auto moving = member.current_station != member.destination_station ||
                      member.next_station != member.current_station;
        if (moving) {
            if (member.next_station == member.current_station) {
                member.next_station = next_hop(blueprint, member.current_station, member.destination_station)
                                          .value_or(member.current_station);
            }
            const auto target = node_position(blueprint, member.next_station);
            const auto delta = target - member.local_position;
            const auto distance = glm::length(delta);
            if (distance <= kCrewWalkSpeed * dt + 1.0e-4F) {
                member.local_position = target;
                member.current_station = member.next_station;
                if (member.current_station == member.destination_station) {
                    member.next_station = member.current_station;
                    member.activity_timer = 0.0F;
                    member.yaw_radians = station_yaw(member.current_station);
                } else {
                    member.next_station = next_hop(blueprint, member.current_station, member.destination_station)
                                              .value_or(member.current_station);
                }
            } else if (distance > 1.0e-5F) {
                const auto direction = delta / distance;
                member.local_position += direction * (kCrewWalkSpeed * dt);
                if (direction.x * direction.x + direction.z * direction.z > 1.0e-5F) {
                    member.yaw_radians = std::atan2(direction.z, direction.x);
                }
            }
            runtime.motion_amount = glm::mix(runtime.motion_amount, 1.0F, std::clamp(dt * 9.0F, 0.0F, 1.0F));
        } else {
            runtime.motion_amount = glm::mix(runtime.motion_amount, 0.0F, std::clamp(dt * 9.0F, 0.0F, 1.0F));
        }

        moving = member.current_station != member.destination_station || member.next_station != member.current_station;
        if (moving) {
            runtime.activity_phase = std::fmod(member.animation_time * 0.85F, 1.0F);
            continue;
        }

        if (member.role == ShipCrewRole::Fisher) {
            if (member.cargo == ShipCrewCargo::Fish) {
                if (member.current_station == ShipCrewStation::CargoFish) {
                    if (fish < kAutomaticFishTarget) {
                        ++fish;
                        result.fish_delivered = true;
                        member.cargo = ShipCrewCargo::None;
                        member.routine_step = 2U;
                        route_to(member, blueprint, ShipCrewStation::MessTable, ShipCrewActivity::Rest);
                    }
                }
            } else if (fish < kAutomaticFishTarget && storm_intensity < kFishingStormStop &&
                       member.current_station == ShipCrewStation::PortFishing) {
                member.activity = ShipCrewActivity::Fish;
                const auto speed = std::clamp(1.0F - storm_intensity * 0.50F, 0.50F, 1.0F);
                member.work_progress = std::min(kAutomaticFishWorkSeconds, member.work_progress + dt * speed);
                runtime.activity_phase = member.work_progress / kAutomaticFishWorkSeconds;
                if (member.work_progress >= kAutomaticFishWorkSeconds) {
                    member.work_progress = 0.0F;
                    member.cargo = ShipCrewCargo::Fish;
                    member.routine_step = 1U;
                    route_to(member, blueprint, ShipCrewStation::CargoFish, ShipCrewActivity::Carry);
                }
            } else if (member.routine_step == 2U && member.current_station == ShipCrewStation::MessTable) {
                member.activity_timer += dt;
                runtime.activity_phase = std::clamp(member.activity_timer / 12.0F, 0.0F, 1.0F);
                if (member.activity_timer >= 12.0F && fish < kAutomaticFishTarget) {
                    member.routine_step = 0U;
                    route_to(member, blueprint, ShipCrewStation::PortFishing, ShipCrewActivity::Fish);
                }
            } else {
                runtime.activity_phase = std::fmod(member.animation_time * 0.18F, 1.0F);
            }
            continue;
        }

        if (member.role == ShipCrewRole::WaterTender) {
            if (member.cargo == ShipCrewCargo::Water) {
                if (member.current_station == ShipCrewStation::CargoWater) {
                    if (water_flasks < kAutomaticWaterTarget) {
                        ++water_flasks;
                        result.water_delivered = true;
                        member.cargo = ShipCrewCargo::None;
                        member.routine_step = 2U;
                        route_to(member, blueprint, ShipCrewStation::Galley, ShipCrewActivity::Rest);
                    }
                }
            } else if (water_flasks < kAutomaticWaterTarget &&
                       member.current_station == ShipCrewStation::WaterStill) {
                member.activity = ShipCrewActivity::TendWater;
                const auto rain_bonus = 1.0F + 0.5F * precipitation_intensity;
                member.work_progress = std::min(
                    kAutomaticWaterWorkSeconds,
                    member.work_progress + dt * rain_bonus);
                runtime.activity_phase = std::fmod(member.work_progress, 8.0F) / 8.0F;
                if (member.work_progress >= kAutomaticWaterWorkSeconds) {
                    member.work_progress = 0.0F;
                    member.cargo = ShipCrewCargo::Water;
                    member.routine_step = 1U;
                    route_to(member, blueprint, ShipCrewStation::CargoWater, ShipCrewActivity::Carry);
                }
            } else if (member.routine_step == 2U && member.current_station == ShipCrewStation::Galley) {
                member.activity_timer += dt;
                runtime.activity_phase = std::clamp(member.activity_timer / 10.0F, 0.0F, 1.0F);
                if (member.activity_timer >= 10.0F && water_flasks < kAutomaticWaterTarget) {
                    member.routine_step = 0U;
                    route_to(member, blueprint, ShipCrewStation::WaterStill, ShipCrewActivity::TendWater);
                }
            } else {
                runtime.activity_phase = std::fmod(member.animation_time * 0.18F, 1.0F);
            }
            continue;
        }

        const auto task = routine_task(member.role, member.routine_step);
        if (member.destination_station != task.station || member.activity != task.activity) {
            route_to(member, blueprint, task.station, task.activity);
            continue;
        }
        member.activity_timer += dt;
        runtime.activity_phase = task.duration > 0.0F
                                     ? std::clamp(member.activity_timer / task.duration, 0.0F, 1.0F)
                                     : 0.0F;
        if (member.activity_timer >= task.duration) {
            member.routine_step = static_cast<std::uint8_t>((member.routine_step + 1U) % 4U);
            const auto next_task = routine_task(member.role, member.routine_step);
            route_to(member, blueprint, next_task.station, next_task.activity);
        }
    }

    rebuild_render_instances(ship, environment);
    return result;
}

auto ShipCrewSystem::try_damage_from_player(const ShipEntity& ship,
                                             const glm::vec3& origin,
                                             const glm::vec3& direction,
                                             float max_distance,
                                             float damage) noexcept -> ShipCrewDamageResult {
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        !std::isfinite(max_distance) || !std::isfinite(damage) || max_distance <= 0.0F || damage <= 0.0F ||
        glm::dot(direction, direction) <= 1.0e-6F) {
        return {};
    }

    const auto ray_direction = glm::normalize(direction);
    const auto origin_world = ship.world_origin();
    const auto ship_obstacle = ship.raycast_collidable_distance(origin, ray_direction, max_distance);
    auto best_index = state_.members.size();
    auto best_distance = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < state_.members.size(); ++index) {
        const auto& member = state_.members[index];
        if (member.health <= 0.0F || member.recovery_timer > 0.0F) {
            continue;
        }
        const auto moving = member.current_station != member.destination_station ||
                            member.next_station != member.current_station;
        const auto feet = origin_world + member.local_position +
                          (moving ? glm::vec3 {0.0F} : shared_station_visual_offset(member));
        const auto height = member.role == ShipCrewRole::Captain ? 1.88F : 1.82F;
        const auto hit = ray_aabb_distance(
            origin,
            ray_direction,
            feet + glm::vec3 {-0.34F, 0.02F, -0.34F},
            feet + glm::vec3 {0.34F, height, 0.34F},
            max_distance);
        if (hit.has_value() && ship_obstacle.has_value() && *ship_obstacle + 0.02F < *hit) {
            continue;
        }
        if (hit.has_value() && *hit < best_distance) {
            best_index = index;
            best_distance = *hit;
        }
    }
    if (best_index >= state_.members.size()) {
        return {};
    }

    auto& member = state_.members[best_index];
    const auto moving = member.current_station != member.destination_station ||
                        member.next_station != member.current_station;
    const auto hit_position = origin_world + member.local_position +
                              (moving ? glm::vec3 {0.0F} : shared_station_visual_offset(member));
    const auto applied_damage = std::min(member.health, damage);
    member.health = std::max(0.0F, member.health - applied_damage);
    member.hurt_timer = kCrewHurtSeconds;
    const auto knocked_out = member.health <= 0.0F;
    if (knocked_out) {
        member.health = 0.0F;
        member.recovery_timer = kShipCrewKnockoutSeconds;
        runtime_[best_index].motion_amount = 0.0F;
        runtime_[best_index].recover_timer = 0.0F;
        render_instances_[best_index].activity = CrewVisualActivity::KnockedOut;
        render_instances_[best_index].knockout_amount = 1.0F;
    } else {
        render_instances_[best_index].hurt_amount = 1.0F;
    }

    return {
        true,
        knocked_out,
        member.id,
        hit_position,
        applied_damage,
        member.health,
        best_distance,
    };
}

auto ShipCrewSystem::save_state() const noexcept -> const ShipCrewSaveState& {
    return state_;
}

auto ShipCrewSystem::members() const noexcept -> std::span<const ShipCrewMemberSaveState> {
    return state_.members;
}

auto ShipCrewSystem::render_instances() const noexcept -> std::span<const CrewRenderInstance> {
    return render_instances_;
}

void ShipCrewSystem::rebuild_render_instances(const ShipEntity& ship, const EnvironmentState& environment) noexcept {
    const auto& blueprint = amelie_ship_blueprint();
    const auto world_origin = ship.world_origin();
    for (std::size_t index = 0; index < state_.members.size(); ++index) {
        const auto& member = state_.members[index];
        const auto& runtime = runtime_[index];
        const auto moving = member.current_station != member.destination_station ||
                            member.next_station != member.current_station;
        const auto visual_offset = moving ? glm::vec3 {0.0F} : shared_station_visual_offset(member);
        const auto exterior = station_is_exterior(blueprint, member.current_station) && member.local_position.y >= 3.70F;
        auto& render = render_instances_[index];
        render.position = world_origin + member.local_position + visual_offset;
        render.yaw_radians = member.yaw_radians;
        render.animation_time = member.animation_time;
        render.appearance_seed = hash_u32(appearance_seed_ ^ (0x9E3779B9U * static_cast<std::uint32_t>(index + 1U)));
        render.role = visual_role(member.role);
        render.activity = visual_activity(member, runtime.activity_phase, runtime.recover_timer, moving);
        render.motion_amount = runtime.motion_amount;
        render.activity_phase = runtime.activity_phase;
        render.hurt_amount = std::clamp(member.hurt_timer / kCrewHurtSeconds, 0.0F, 1.0F);
        render.knockout_amount = member.recovery_timer > 0.0F || member.health <= 0.0F ? 1.0F : 0.0F;
        render.daylight_factor = std::clamp(finite_or(environment.daylight_factor, 1.0F), 0.0F, 1.0F);
        render.sky_light = exterior ? 1.0F : 0.16F;
        render.local_light = exterior ? 0.0F : cargo_light(member.local_position + visual_offset,
                                                           blueprint.interior_lanterns);
        render.precipitation_exposure = exterior ? 1.0F : 0.0F;
    }
}

} // namespace valcraft
