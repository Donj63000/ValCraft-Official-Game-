#include "gameplay/ShipCrew.h"

#include "creatures/CrewAnimation.h"
#include "gameplay/SeaAdventure.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr float kCrewWalkSpeed = 1.22F;
constexpr float kCrewCarrySpeed = 1.02F;
constexpr float kCrewAcceleration = 4.80F;
constexpr float kCrewDeceleration = 7.20F;
constexpr float kCrewTurnSpeed = 6.40F;
constexpr float kCrewStationTurnSpeed = 4.20F;
constexpr float kCrewMoveAlignment = 0.94F;
constexpr float kCrewNodeClearance = 0.76F;
constexpr float kCrewPlayerForwardClearance = 1.05F;
constexpr float kCrewPlayerSideClearance = 0.72F;
constexpr float kCrewPlayerVerticalClearance = 1.95F;
constexpr float kCrewVisualOffsetSpeed = 4.50F;
constexpr float kCrewHurtSeconds = 0.45F;
constexpr float kCrewRecoverAnimationSeconds = 1.0F;
constexpr float kFishingStormStop = 0.65F;
constexpr float kHeavyCrewStorm = 0.55F;
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
    return std::remainder(finite_or(angle, 0.0F), kTwoPi);
}

auto move_towards(float current, float target, float maximum_delta) noexcept -> float {
    const auto delta = target - current;
    if (std::abs(delta) <= maximum_delta) {
        return target;
    }
    return current + std::copysign(maximum_delta, delta);
}

auto rotate_towards(float current, float target, float maximum_delta) noexcept -> float {
    const auto delta = normalized_angle(target - current);
    return normalized_angle(current + std::clamp(delta, -maximum_delta, maximum_delta));
}

auto yaw_from_local_direction(const glm::vec3& direction) noexcept -> float {
    // Le modele d'equipage regarde vers +X. Avec les matrices GLM/OpenGL,
    // une rotation positive autour de Y envoie +X vers -Z : le signe de Z doit
    // donc etre inverse. C'est la correction centrale du "moonwalk".
    return normalized_angle(std::atan2(-direction.z, direction.x));
}

auto forward_from_yaw(float yaw_radians) noexcept -> glm::vec3 {
    return {std::cos(yaw_radians), 0.0F, -std::sin(yaw_radians)};
}

auto horizontal_length(const glm::vec3& value) noexcept -> float {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

auto horizontal_direction_or(const glm::vec3& value, const glm::vec3& fallback) noexcept -> glm::vec3 {
    const auto length = horizontal_length(value);
    return length > 1.0e-5F
               ? glm::vec3 {value.x / length, 0.0F, value.z / length}
               : fallback;
}

auto approach_vec3(const glm::vec3& current, const glm::vec3& target, float maximum_delta) noexcept -> glm::vec3 {
    const auto delta = target - current;
    const auto distance = glm::length(delta);
    if (distance <= maximum_delta || distance <= 1.0e-6F) {
        return target;
    }
    return current + delta * (maximum_delta / distance);
}

auto member_is_moving(const ShipCrewMemberSaveState& member) noexcept -> bool {
    return member.current_station != member.destination_station ||
           member.next_station != member.current_station;
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
        // Ces postes regardent vers la proue (+Z).
        return -0.5F * kPi;
    case ShipCrewStation::AftWatch:
    case ShipCrewStation::ForeStairsTop:
    case ShipCrewStation::ForeStairsMid:
    case ShipCrewStation::ForeStairsBottom:
        // Ces postes regardent vers la poupe (-Z).
        return 0.5F * kPi;
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
        return -0.5F * kPi;
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
    case ShipCrewStation::MainMast:
        // En tempete, le pecheur vient aider le gabier. Les deux silhouettes
        // restent lisibles au lieu de fusionner au centre du mat.
        if (member.role == ShipCrewRole::Fisher) return {0.0F, 0.0F, -0.55F};
        if (member.role == ShipCrewRole::Rigger) return {0.0F, 0.0F, 0.55F};
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

auto station_allows_multiple(ShipCrewStation station) noexcept -> bool {
    switch (station) {
    case ShipCrewStation::MessTable:
    case ShipCrewStation::Capstan:
    case ShipCrewStation::MainMast:
    case ShipCrewStation::CargoFish:
    case ShipCrewStation::CargoWater:
        return true;
    default:
        return false;
    }
}

auto movement_priority(const ShipCrewMemberSaveState& member, std::size_t index) noexcept -> int {
    // Une livraison ne doit pas rester bloquee derriere une ronde cosmetique.
    // A urgence egale, l'identifiant canonique rend l'arbitrage deterministe.
    const auto urgency = member.cargo != ShipCrewCargo::None
                             ? 0
                             : (member.role == ShipCrewRole::Captain &&
                                        member.destination_station == ShipCrewStation::Helm
                                    ? 1
                                    : 2);
    return urgency * 16 + static_cast<int>(index);
}

auto same_undirected_edge(ShipCrewStation first_a,
                          ShipCrewStation second_a,
                          ShipCrewStation first_b,
                          ShipCrewStation second_b) noexcept -> bool {
    return (first_a == first_b && second_a == second_b) ||
           (first_a == second_b && second_a == first_b);
}

auto edge_is_available(const ShipBlueprint& blueprint,
                       std::span<const ShipCrewMemberSaveState> members,
                       std::size_t member_index) noexcept -> bool {
    const auto& member = members[member_index];
    if (member.current_station == member.next_station) {
        return true;
    }

    const auto edge_start = node_position(blueprint, member.current_station);
    const auto already_inside_edge = glm::length(member.local_position - edge_start) > 0.08F;
    if (already_inside_edge) {
        return true;
    }

    for (std::size_t other_index = 0; other_index < members.size(); ++other_index) {
        if (other_index == member_index) {
            continue;
        }
        const auto& other = members[other_index];
        if (other.current_station == other.next_station ||
            !same_undirected_edge(
                member.current_station,
                member.next_station,
                other.current_station,
                other.next_station)) {
            continue;
        }

        const auto other_start = node_position(blueprint, other.current_station);
        const auto other_inside_edge = glm::length(other.local_position - other_start) > 0.08F;
        if (other_inside_edge ||
            movement_priority(other, other_index) < movement_priority(member, member_index)) {
            return false;
        }
    }
    return true;
}

auto node_is_available(const ShipBlueprint& blueprint,
                       std::span<const ShipCrewMemberSaveState> members,
                       std::size_t member_index,
                       ShipCrewStation target_station) noexcept -> bool {
    if (station_allows_multiple(target_station)) {
        return true;
    }

    const auto target = node_position(blueprint, target_station);
    const auto& member = members[member_index];
    for (std::size_t other_index = 0; other_index < members.size(); ++other_index) {
        if (other_index == member_index) {
            continue;
        }
        const auto& other = members[other_index];
        if (glm::length(other.local_position - target) >= kCrewNodeClearance) {
            continue;
        }

        const auto both_approach_same_node =
            member.next_station == target_station &&
            member.current_station != target_station &&
            other.next_station == target_station &&
            other.current_station != target_station;
        if (both_approach_same_node &&
            movement_priority(member, member_index) < movement_priority(other, other_index)) {
            continue;
        }
        return false;
    }
    return true;
}


auto node_blockers_are_departing(const ShipBlueprint& blueprint,
                                 std::span<const ShipCrewMemberSaveState> members,
                                 std::size_t member_index,
                                 ShipCrewStation target_station) noexcept -> bool {
    const auto target = node_position(blueprint, target_station);
    auto found_blocker = false;
    for (std::size_t other_index = 0; other_index < members.size(); ++other_index) {
        if (other_index == member_index) {
            continue;
        }
        const auto& other = members[other_index];
        if (glm::length(other.local_position - target) >= kCrewNodeClearance) {
            continue;
        }

        found_blocker = true;
        const auto leaves_target =
            other.current_station == target_station &&
            other.next_station != other.current_station;
        if (!leaves_target) {
            return false;
        }
    }
    return found_blocker;
}

auto player_blocks_path(const ShipCrewMemberSaveState& member,
                        const glm::vec3& travel_direction,
                        const std::optional<glm::vec3>& player_local_position) noexcept -> bool {
    if (!player_local_position.has_value()) {
        return false;
    }

    const auto to_player = *player_local_position - member.local_position;
    if (std::abs(to_player.y) > kCrewPlayerVerticalClearance) {
        return false;
    }

    const glm::vec3 horizontal_to_player {to_player.x, 0.0F, to_player.z};
    const auto forward_distance = glm::dot(horizontal_to_player, travel_direction);
    const auto lateral = horizontal_to_player - travel_direction * forward_distance;
    return forward_distance >= -0.12F &&
           forward_distance <= kCrewPlayerForwardClearance &&
           horizontal_length(lateral) <= kCrewPlayerSideClearance;
}

auto next_hop(const ShipBlueprint& blueprint,
              ShipCrewStation start,
              ShipCrewStation destination,
              std::optional<ShipCrewStation> forbidden_station = std::nullopt) noexcept
    -> std::optional<ShipCrewStation> {
    if (!known_station(start) || !known_station(destination) ||
        node_for(blueprint, start) == nullptr || node_for(blueprint, destination) == nullptr ||
        (forbidden_station.has_value() && *forbidden_station == destination)) {
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
            if (!known_station(neighbor) ||
                node_for(blueprint, neighbor) == nullptr ||
                (forbidden_station.has_value() &&
                 neighbor == *forbidden_station)) {
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

auto situational_task(ShipCrewRole role, float storm_intensity) noexcept -> std::optional<RoutineTask> {
    if (storm_intensity < kHeavyCrewStorm) {
        return std::nullopt;
    }

    // En forte mer, les postes deviennent coherents avec la situation au lieu
    // de poursuivre une ronde de routine pendant que le navire est en danger.
    switch (role) {
    case ShipCrewRole::Captain:
        return RoutineTask {ShipCrewStation::Helm, ShipCrewActivity::Steer, 8.0F};
    case ShipCrewRole::Rigger:
        return RoutineTask {ShipCrewStation::ForeMast, ShipCrewActivity::HaulRope, 8.0F};
    case ShipCrewRole::Deckhand:
        return RoutineTask {ShipCrewStation::Capstan, ShipCrewActivity::TurnCapstan, 8.0F};
    case ShipCrewRole::Quartermaster:
        return RoutineTask {ShipCrewStation::CargoSort, ShipCrewActivity::SortCargo, 8.0F};
    case ShipCrewRole::Fisher:
    case ShipCrewRole::WaterTender:
        return std::nullopt;
    }
    return std::nullopt;
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
        // Une caisse ne doit pas disparaitre pendant son transport. La pose
        // Carry anime aussi les jambes lorsque motion_amount est non nul.
        return member.cargo != ShipCrewCargo::None || member.activity == ShipCrewActivity::Carry
                   ? CrewVisualActivity::Carry
                   : CrewVisualActivity::Walk;
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

struct CrewRayHit {
    std::size_t index = 0U;
    float distance = 0.0F;
};

auto find_crew_ray_hit(const ShipEntity& ship,
                       std::span<const ShipCrewMemberSaveState> members,
                       std::span<const CrewRenderInstance> render_instances,
                       const glm::vec3& origin,
                       const glm::vec3& ray_direction,
                       float max_distance,
                       bool include_knocked_out) noexcept -> std::optional<CrewRayHit> {
    const auto ship_obstacle =
        ship.raycast_collidable_distance(
            origin,
            ray_direction,
            max_distance);

    // Les volumes de selection sont definis dans le repere du navire. Une
    // transformation rigide conserve le parametre de distance du rayon.
    const auto local_origin =
        ship.world_to_local_point(origin);
    const auto local_direction =
        ship.world_to_local_direction(
            ray_direction);

    auto best_index = members.size();
    auto best_distance = std::numeric_limits<float>::max();

    for (std::size_t index = 0; index < members.size(); ++index) {
        const auto& member = members[index];
        const auto knocked_out =
            member.health <= 0.0F ||
            member.recovery_timer > 0.0F;
        if (knocked_out && !include_knocked_out) {
            continue;
        }

        if (index >= render_instances.size()) {
            break;
        }

        const auto feet =
            ship.world_to_local_point(
                render_instances[index].position);
        const auto half_width =
            knocked_out ? 0.82F : 0.34F;
        const auto height =
            knocked_out
                ? 0.62F
                : (member.role == ShipCrewRole::Captain
                       ? 1.88F
                       : 1.82F);
        const auto hit = ray_aabb_distance(
            local_origin,
            local_direction,
            feet +
                glm::vec3 {
                    -half_width,
                    0.02F,
                    -half_width,
                },
            feet +
                glm::vec3 {
                    half_width,
                    height,
                    half_width,
                },
            max_distance);

        // Le panneau et les coups ne traversent jamais un plancher, une cloison
        // ou un meuble du navire pour selectionner un marin cache.
        if (hit.has_value() &&
            ship_obstacle.has_value() &&
            *ship_obstacle + 0.02F < *hit) {
            continue;
        }
        if (hit.has_value() &&
            *hit < best_distance) {
            best_index = index;
            best_distance = *hit;
        }
    }

    if (best_index >= members.size()) {
        return std::nullopt;
    }
    return CrewRayHit {
        best_index,
        best_distance,
    };
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

auto ship_crew_role_label(ShipCrewRole role) noexcept -> std::string_view {
    switch (role) {
    case ShipCrewRole::Captain: return "CAPITAINE";
    case ShipCrewRole::Fisher: return "PECHEUR";
    case ShipCrewRole::Rigger: return "GABIER";
    case ShipCrewRole::WaterTender: return "MAITRE D'EAU";
    case ShipCrewRole::Deckhand: return "MATELOT DE PONT";
    case ShipCrewRole::Quartermaster: return "QUARTIER-MAITRE";
    }
    return "MARIN";
}

auto ship_crew_activity_label(ShipCrewActivity activity) noexcept -> std::string_view {
    switch (activity) {
    case ShipCrewActivity::Idle: return "ATTEND SON PROCHAIN ORDRE";
    case ShipCrewActivity::Steer: return "TIENT LA BARRE";
    case ShipCrewActivity::Inspect: return "CONTROLE LA ROUTE";
    case ShipCrewActivity::Fish: return "PECHE POUR LES VIVRES";
    case ShipCrewActivity::TendWater: return "PREPARE L'EAU POTABLE";
    case ShipCrewActivity::Carry: return "TRANSPORTE UNE CARGAISON";
    case ShipCrewActivity::HaulRope: return "SECURISE LE GREEMENT";
    case ShipCrewActivity::Scrub: return "ENTRETIENT LE PONT";
    case ShipCrewActivity::TurnCapstan: return "MANOEUVRE LE CABESTAN";
    case ShipCrewActivity::SortCargo: return "ORGANISE LES RESERVES";
    case ShipCrewActivity::Socialize: return "ECHANGE AVEC L'EQUIPAGE";
    case ShipCrewActivity::Rest: return "PREND SON QUART DE REPOS";
    }
    return "ACTIVITE INCONNUE";
}

auto ship_crew_cargo_label(ShipCrewCargo cargo) noexcept -> std::string_view {
    switch (cargo) {
    case ShipCrewCargo::Fish: return "POISSON";
    case ShipCrewCargo::Water: return "EAU";
    case ShipCrewCargo::None:
    default:
        return {};
    }
}

auto ship_crew_station_label(ShipCrewStation station) noexcept -> std::string_view {
    switch (station) {
    case ShipCrewStation::Helm: return "BARRE";
    case ShipCrewStation::ChartTable: return "TABLE A CARTES";
    case ShipCrewStation::CaptainCabin: return "CABINE DU CAPITAINE";
    case ShipCrewStation::AftWatch: return "VIGIE ARRIERE";
    case ShipCrewStation::PortFishing: return "PECHE BABORD";
    case ShipCrewStation::StarboardFishing: return "PECHE TRIBORD";
    case ShipCrewStation::MainMast: return "GRAND MAT";
    case ShipCrewStation::ForeMast: return "MAT DE MISAINE";
    case ShipCrewStation::MizzenMast: return "MAT D'ARTIMON";
    case ShipCrewStation::WaterStill: return "ALAMBIC A EAU";
    case ShipCrewStation::Galley: return "CUISINE";
    case ShipCrewStation::Capstan: return "CABESTAN";
    case ShipCrewStation::AftDeck: return "PONT ARRIERE";
    case ShipCrewStation::MidDeckPort: return "PONT BABORD";
    case ShipCrewStation::MidDeckStarboard: return "PONT TRIBORD";
    case ShipCrewStation::ForeDeck: return "GAILLARD AVANT";
    case ShipCrewStation::CargoFish: return "CALE AUX POISSONS";
    case ShipCrewStation::CargoWater: return "RESERVE D'EAU";
    case ShipCrewStation::CargoSort: return "CALE PRINCIPALE";
    case ShipCrewStation::CrewBunks: return "COUCHETTES";
    case ShipCrewStation::MessTable: return "TABLE D'EQUIPAGE";
    case ShipCrewStation::AftStairsTop:
    case ShipCrewStation::AftStairsMid:
    case ShipCrewStation::AftStairsBottom:
    case ShipCrewStation::ForeStairsTop:
    case ShipCrewStation::ForeStairsMid:
    case ShipCrewStation::ForeStairsBottom:
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
        return "PASSAGE DU NAVIRE";
    case ShipCrewStation::Count:
    default:
        return "POSTE INCONNU";
    }
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
        runtime_[index].visual_offset =
            member_is_moving(member) ? glm::vec3 {0.0F} : shared_station_visual_offset(member);
    }
    rebuild_render_instances(ship, {});
}

void ShipCrewSystem::restore_runtime_routes(const ShipEntity& ship) noexcept {
    const auto& blueprint = amelie_ship_blueprint();
    runtime_.fill({});
    for (std::size_t index = 0; index < state_.members.size(); ++index) {
        auto& member = state_.members[index];
        auto& runtime = runtime_[index];
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

        runtime.visual_offset =
            member_is_moving(member) ? glm::vec3 {0.0F} : shared_station_visual_offset(member);
    }
    (void)ship;
}

auto ShipCrewSystem::update(const ShipEntity& ship,
                            const EnvironmentState& environment,
                            float dt,
                            std::uint32_t& fish,
                            std::uint32_t& water_flasks,
                            std::optional<glm::vec3> player_world_position) noexcept
    -> ShipCrewUpdateResult {

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

    const auto storm_intensity =
        std::clamp(finite_or(environment.storm_intensity, 0.0F), 0.0F, 1.0F);
    const auto precipitation_intensity =
        std::clamp(finite_or(environment.precipitation_intensity, 0.0F), 0.0F, 1.0F);
    const auto player_position_is_finite =
        player_world_position.has_value() &&
        std::isfinite(player_world_position->x) &&
        std::isfinite(player_world_position->y) &&
        std::isfinite(player_world_position->z);
    const auto player_local_position =
        player_position_is_finite
            ? std::optional<glm::vec3> {
                  ship.world_to_local_point(
                      *player_world_position),
              }
            : std::nullopt;

    for (std::size_t index = 0; index < state_.members.size(); ++index) {
        auto& member = state_.members[index];
        auto& runtime = runtime_[index];
        member.animation_time = std::fmod(member.animation_time + dt, 86'400.0F);
        member.hurt_timer = std::max(0.0F, member.hurt_timer - dt);
        runtime.recover_timer = std::max(0.0F, runtime.recover_timer - dt);
        runtime.blocked = false;

        if (member.recovery_timer > 0.0F || member.health <= 0.0F) {
            member.health = 0.0F;
            member.recovery_timer = std::max(0.0F, member.recovery_timer - dt);
            runtime.current_speed = 0.0F;
            runtime.motion_amount = 0.0F;
            runtime.blocked_timer = 0.0F;
            runtime.activity_phase =
                1.0F - member.recovery_timer / kShipCrewKnockoutSeconds;

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

                // La cargaison survit a l'assommement. Le trajet est reconstruit
                // depuis un poste valide afin d'eviter un marin reveille mais fige.
                if (member.role == ShipCrewRole::Fisher &&
                    member.cargo == ShipCrewCargo::Fish) {
                    member.routine_step = 1U;
                    route_to(
                        member,
                        blueprint,
                        ShipCrewStation::CargoFish,
                        ShipCrewActivity::Carry);
                } else if (
                    member.role == ShipCrewRole::WaterTender &&
                    member.cargo == ShipCrewCargo::Water) {
                    member.routine_step = 1U;
                    route_to(
                        member,
                        blueprint,
                        ShipCrewStation::CargoWater,
                        ShipCrewActivity::Carry);
                }
            }

            const auto visual_target =
                member_is_moving(member)
                    ? glm::vec3 {0.0F}
                    : shared_station_visual_offset(member);
            runtime.visual_offset = approach_vec3(
                runtime.visual_offset,
                visual_target,
                kCrewVisualOffsetSpeed * dt);
            continue;
        }

        if (runtime.recover_timer > 0.0F) {
            // Le relevage bloque la routine et la locomotion pendant une seconde.
            runtime.current_speed = 0.0F;
            runtime.motion_amount = 0.0F;
            runtime.blocked_timer = 0.0F;
            runtime.activity_phase =
                1.0F - runtime.recover_timer / kCrewRecoverAnimationSeconds;
            runtime.visual_offset = approach_vec3(
                runtime.visual_offset,
                shared_station_visual_offset(member),
                kCrewVisualOffsetSpeed * dt);
            continue;
        }

        // Les producteurs cessent d'accumuler du travail lorsque le stock cible
        // est plein. Une place liberee exige donc bien un nouveau cycle complet.
        if (member.role == ShipCrewRole::Fisher &&
            member.cargo == ShipCrewCargo::None) {
            if (fish >= kAutomaticFishTarget) {
                member.work_progress = 0.0F;
                route_to(
                    member,
                    blueprint,
                    ShipCrewStation::MessTable,
                    ShipCrewActivity::Socialize);
            } else if (storm_intensity >= kFishingStormStop) {
                route_to(
                    member,
                    blueprint,
                    ShipCrewStation::MainMast,
                    ShipCrewActivity::HaulRope);
            } else if (
                (member.destination_station == ShipCrewStation::MainMast &&
                 member.activity == ShipCrewActivity::HaulRope) ||
                (member.routine_step == 0U &&
                 member.destination_station == ShipCrewStation::MessTable)) {
                // Une tempete peut interrompre le repos comme la peche. A son
                // passage, le pecheur reprend toujours un cycle productif au
                // lieu de rester indefiniment affecte au grand mat.
                member.routine_step = 0U;
                route_to(
                    member,
                    blueprint,
                    ShipCrewStation::PortFishing,
                    ShipCrewActivity::Fish);
            }
        } else if (
            member.role == ShipCrewRole::WaterTender &&
            member.cargo == ShipCrewCargo::None &&
            water_flasks >= kAutomaticWaterTarget) {
            member.work_progress = 0.0F;
            route_to(
                member,
                blueprint,
                ShipCrewStation::Galley,
                ShipCrewActivity::Socialize);
        } else if (
            member.role == ShipCrewRole::WaterTender &&
            member.cargo == ShipCrewCargo::None &&
            water_flasks < kAutomaticWaterTarget &&
            member.routine_step == 0U &&
            member.destination_station == ShipCrewStation::Galley) {
            route_to(
                member,
                blueprint,
                ShipCrewStation::WaterStill,
                ShipCrewActivity::TendWater);
        }

        const auto forced_task =
            member.cargo == ShipCrewCargo::None
                ? situational_task(member.role, storm_intensity)
                : std::nullopt;
        if (forced_task.has_value()) {
            route_to(
                member,
                blueprint,
                forced_task->station,
                forced_task->activity);
        }

        auto moving = member_is_moving(member);
        auto travelled_distance = 0.0F;
        auto reference_speed =
            member.cargo == ShipCrewCargo::None
                ? kCrewWalkSpeed
                : kCrewCarrySpeed;

        if (moving) {
            if (member.next_station == member.current_station) {
                const auto hop =
                    next_hop(
                        blueprint,
                        member.current_station,
                        member.destination_station);
                if (hop.has_value() && *hop != member.current_station) {
                    member.next_station = *hop;
                } else {
                    // Le graphe canonique est connexe, mais cette reparation
                    // empeche une sauvegarde corrompue de boucler indefiniment.
                    member.destination_station = member.current_station;
                    member.next_station = member.current_station;
                    member.activity = ShipCrewActivity::Idle;
                    moving = false;
                }
            }

            if (moving) {
                const auto edge_start =
                    node_position(blueprint, member.current_station);
                const auto starts_edge =
                    glm::length(member.local_position - edge_start) <= 0.08F;

                // Un waypoint occupe n'impose pas un face-a-face dans un couloir :
                // avant de s'engager, le marin cherche une route equivalente qui
                // contourne ce poste. Le graphe ne contient que 42 noeuds, ce BFS
                // ponctuel reste negligeable et rend les circulations robustes.
                if (starts_edge &&
                    member.next_station != member.destination_station &&
                    !node_is_available(
                        blueprint,
                        state_.members,
                        index,
                        member.next_station)) {
                    const auto alternate_hop =
                        next_hop(
                            blueprint,
                            member.current_station,
                            member.destination_station,
                            member.next_station);
                    if (alternate_hop.has_value() &&
                        *alternate_hop != member.current_station) {
                        member.next_station = *alternate_hop;
                    }
                }

                const auto target =
                    node_position(blueprint, member.next_station);
                const auto delta = target - member.local_position;
                const auto distance = glm::length(delta);
                const auto travel_direction =
                    horizontal_direction_or(
                        delta,
                        forward_from_yaw(member.yaw_radians));
                const auto desired_yaw =
                    yaw_from_local_direction(travel_direction);

                member.yaw_radians = rotate_towards(
                    member.yaw_radians,
                    desired_yaw,
                    kCrewTurnSpeed * dt);

                const auto alignment = std::clamp(
                    glm::dot(
                        forward_from_yaw(member.yaw_radians),
                        travel_direction),
                    -1.0F,
                    1.0F);
                const auto edge_available =
                    edge_is_available(
                        blueprint,
                        state_.members,
                        index);
                const auto node_available =
                    node_is_available(
                        blueprint,
                        state_.members,
                        index,
                        member.next_station);
                const auto inside_edge =
                    glm::length(
                        member.local_position -
                        node_position(
                            blueprint,
                            member.current_station)) >
                    0.08F;
                const auto final_node =
                    member.next_station ==
                    member.destination_station;
                const auto node_is_being_vacated =
                    !node_available &&
                    node_blockers_are_departing(
                        blueprint,
                        state_.members,
                        index,
                        member.next_station);
                const auto can_enter_reserved_node =
                    inside_edge &&
                    (!final_node ||
                     node_is_being_vacated);
                const auto near_reserved_node =
                    !node_available &&
                    !can_enter_reserved_node &&
                    distance <=
                        kCrewNodeClearance +
                            reference_speed * dt;
                const auto blocked_by_player =
                    player_blocks_path(
                        member,
                        travel_direction,
                        player_local_position);

                runtime.blocked =
                    !edge_available ||
                    near_reserved_node ||
                    blocked_by_player;
                runtime.blocked_timer =
                    runtime.blocked
                        ? std::min(10.0F, runtime.blocked_timer + dt)
                        : std::max(0.0F, runtime.blocked_timer - dt * 4.0F);

                const auto alignment_speed =
                    std::clamp(
                        (alignment - kCrewMoveAlignment) /
                            (1.0F - kCrewMoveAlignment),
                        0.0F,
                        1.0F);
                const auto desired_speed =
                    !runtime.blocked
                        ? reference_speed * alignment_speed
                        : 0.0F;
                const auto acceleration =
                    desired_speed > runtime.current_speed
                        ? kCrewAcceleration
                        : kCrewDeceleration;
                runtime.current_speed = move_towards(
                    runtime.current_speed,
                    desired_speed,
                    acceleration * dt);

                // Tant que le torse n'est pas presque aligne, le marin tourne
                // sur place. Aucune vitesse residuelle ne peut le faire glisser
                // lateralement ou a reculons.
                const auto may_translate =
                    !runtime.blocked &&
                    alignment >= kCrewMoveAlignment &&
                    distance > 1.0e-5F;
                const auto step_distance =
                    may_translate
                        ? std::min(
                              distance,
                              runtime.current_speed * dt)
                        : 0.0F;

                if (step_distance > 0.0F) {
                    member.local_position +=
                        delta / distance * step_distance;
                    runtime.locomotion_distance =
                        std::fmod(
                            runtime.locomotion_distance +
                                step_distance,
                            kCrewLocomotionCycleDistance * 1024.0F);
                    travelled_distance = step_distance;
                }

                const auto remaining_distance =
                    glm::length(target - member.local_position);
                if (remaining_distance <= 1.0e-4F &&
                    (node_available ||
                     can_enter_reserved_node)) {
                    member.local_position = target;
                    member.current_station = member.next_station;
                    runtime.blocked = false;
                    runtime.blocked_timer = 0.0F;

                    if (member.current_station ==
                        member.destination_station) {
                        member.next_station =
                            member.current_station;
                        member.activity_timer = 0.0F;
                        runtime.current_speed = 0.0F;
                    } else {
                        // La vitesse est conservee sur un waypoint intermediaire.
                        // Si le couloir tourne, l'alignement du prochain tick
                        // provoquera naturellement un freinage puis un demi-tour.
                        member.next_station =
                            next_hop(
                                blueprint,
                                member.current_station,
                                member.destination_station)
                                .value_or(member.current_station);
                    }
                }

                const auto realised_motion =
                    dt > 0.0F
                        ? std::clamp(
                              travelled_distance /
                                  std::max(reference_speed * dt, 0.001F),
                              0.0F,
                              1.0F)
                        : 0.0F;
                runtime.motion_amount = glm::mix(
                    runtime.motion_amount,
                    realised_motion,
                    std::clamp(dt * 12.0F, 0.0F, 1.0F));
            }
        }

        moving = member_is_moving(member);
        if (!moving) {
            runtime.current_speed = move_towards(
                runtime.current_speed,
                0.0F,
                kCrewDeceleration * dt);
            runtime.motion_amount = glm::mix(
                runtime.motion_amount,
                0.0F,
                std::clamp(dt * 12.0F, 0.0F, 1.0F));
            runtime.blocked = false;
            runtime.blocked_timer =
                std::max(0.0F, runtime.blocked_timer - dt * 4.0F);

            auto desired_yaw =
                station_yaw(member.current_station);

            // Lors d'un moment social ou d'attente, un marin proche reconnait
            // la presence du joueur. Les postes de travail conservent en revanche
            // leur orientation fonctionnelle vers la barre, le mat ou la caisse.
            if (player_local_position.has_value() &&
                (member.activity == ShipCrewActivity::Socialize ||
                 member.activity == ShipCrewActivity::Idle)) {
                const auto to_player =
                    *player_local_position -
                    (member.local_position + runtime.visual_offset);
                const auto player_distance =
                    horizontal_length(to_player);
                if (player_distance > 0.15F &&
                    player_distance < 3.40F &&
                    std::abs(to_player.y) < 2.20F) {
                    desired_yaw =
                        yaw_from_local_direction(to_player);
                }
            }

            member.yaw_radians = rotate_towards(
                member.yaw_radians,
                desired_yaw,
                kCrewStationTurnSpeed * dt);
        }

        const auto visual_target =
            moving
                ? glm::vec3 {0.0F}
                : shared_station_visual_offset(member);
        runtime.visual_offset = approach_vec3(
            runtime.visual_offset,
            visual_target,
            kCrewVisualOffsetSpeed * dt);

        if (moving) {
            continue;
        }

        if (member.role == ShipCrewRole::Fisher) {
            if (member.cargo == ShipCrewCargo::Fish) {
                if (member.current_station ==
                        ShipCrewStation::CargoFish &&
                    fish < kAutomaticFishTarget) {
                    ++fish;
                    result.fish_delivered = true;
                    member.cargo = ShipCrewCargo::None;
                    member.routine_step = 2U;
                    route_to(
                        member,
                        blueprint,
                        ShipCrewStation::MessTable,
                        ShipCrewActivity::Rest);
                } else {
                    runtime.activity_phase =
                        std::fmod(
                            member.animation_time * 0.18F,
                            1.0F);
                }
            } else if (
                fish < kAutomaticFishTarget &&
                storm_intensity < kFishingStormStop &&
                member.current_station ==
                    ShipCrewStation::PortFishing) {
                member.activity = ShipCrewActivity::Fish;
                const auto speed = std::clamp(
                    1.0F - storm_intensity * 0.50F,
                    0.50F,
                    1.0F);
                member.work_progress = std::min(
                    kAutomaticFishWorkSeconds,
                    member.work_progress + dt * speed);
                runtime.activity_phase =
                    member.work_progress /
                    kAutomaticFishWorkSeconds;

                if (member.work_progress >=
                    kAutomaticFishWorkSeconds) {
                    member.work_progress = 0.0F;
                    member.cargo = ShipCrewCargo::Fish;
                    member.routine_step = 1U;
                    route_to(
                        member,
                        blueprint,
                        ShipCrewStation::CargoFish,
                        ShipCrewActivity::Carry);
                }
            } else if (
                member.routine_step == 2U &&
                member.current_station ==
                    ShipCrewStation::MessTable) {
                member.activity_timer += dt;
                runtime.activity_phase =
                    std::clamp(
                        member.activity_timer / 12.0F,
                        0.0F,
                        1.0F);
                if (member.activity_timer >= 12.0F &&
                    fish < kAutomaticFishTarget) {
                    member.routine_step = 0U;
                    route_to(
                        member,
                        blueprint,
                        ShipCrewStation::PortFishing,
                        ShipCrewActivity::Fish);
                }
            } else {
                runtime.activity_phase =
                    std::fmod(
                        member.animation_time * 0.18F,
                        1.0F);
            }
            continue;
        }

        if (member.role == ShipCrewRole::WaterTender) {
            if (member.cargo == ShipCrewCargo::Water) {
                if (member.current_station ==
                        ShipCrewStation::CargoWater &&
                    water_flasks < kAutomaticWaterTarget) {
                    ++water_flasks;
                    result.water_delivered = true;
                    member.cargo = ShipCrewCargo::None;
                    member.routine_step = 2U;
                    route_to(
                        member,
                        blueprint,
                        ShipCrewStation::Galley,
                        ShipCrewActivity::Rest);
                } else {
                    runtime.activity_phase =
                        std::fmod(
                            member.animation_time * 0.18F,
                            1.0F);
                }
            } else if (
                water_flasks < kAutomaticWaterTarget &&
                member.current_station ==
                    ShipCrewStation::WaterStill) {
                member.activity =
                    ShipCrewActivity::TendWater;
                const auto rain_bonus =
                    1.0F +
                    0.5F * precipitation_intensity;
                member.work_progress = std::min(
                    kAutomaticWaterWorkSeconds,
                    member.work_progress +
                        dt * rain_bonus);
                runtime.activity_phase =
                    std::fmod(
                        member.work_progress,
                        8.0F) /
                    8.0F;

                if (member.work_progress >=
                    kAutomaticWaterWorkSeconds) {
                    member.work_progress = 0.0F;
                    member.cargo = ShipCrewCargo::Water;
                    member.routine_step = 1U;
                    route_to(
                        member,
                        blueprint,
                        ShipCrewStation::CargoWater,
                        ShipCrewActivity::Carry);
                }
            } else if (
                member.routine_step == 2U &&
                member.current_station ==
                    ShipCrewStation::Galley) {
                member.activity_timer += dt;
                runtime.activity_phase =
                    std::clamp(
                        member.activity_timer / 10.0F,
                        0.0F,
                        1.0F);
                if (member.activity_timer >= 10.0F &&
                    water_flasks <
                        kAutomaticWaterTarget) {
                    member.routine_step = 0U;
                    route_to(
                        member,
                        blueprint,
                        ShipCrewStation::WaterStill,
                        ShipCrewActivity::TendWater);
                }
            } else {
                runtime.activity_phase =
                    std::fmod(
                        member.animation_time * 0.18F,
                        1.0F);
            }
            continue;
        }

        if (forced_task.has_value()) {
            // Une tache de tempete reste active tant que la pression meteo
            // subsiste ; elle ne consomme pas l'etape normale de la ronde.
            member.activity_timer =
                std::fmod(
                    member.activity_timer + dt,
                    std::max(forced_task->duration, 0.001F));
            runtime.activity_phase =
                member.activity_timer /
                std::max(forced_task->duration, 0.001F);
            continue;
        }

        const auto task =
            routine_task(
                member.role,
                member.routine_step);
        if (member.destination_station != task.station ||
            member.activity != task.activity) {
            route_to(
                member,
                blueprint,
                task.station,
                task.activity);
            continue;
        }

        member.activity_timer += dt;
        runtime.activity_phase =
            task.duration > 0.0F
                ? std::clamp(
                      member.activity_timer /
                          task.duration,
                      0.0F,
                      1.0F)
                : 0.0F;
        if (member.activity_timer >= task.duration) {
            member.routine_step =
                static_cast<std::uint8_t>(
                    (member.routine_step + 1U) % 4U);
            const auto next_task =
                routine_task(
                    member.role,
                    member.routine_step);
            route_to(
                member,
                blueprint,
                next_task.station,
                next_task.activity);
        }
    }

    rebuild_render_instances(ship, environment);
    return result;
}

auto ShipCrewSystem::try_damage_from_player(const ShipEntity& ship,
                                             const glm::vec3& origin,
                                             const glm::vec3& direction,
                                             float max_distance,
                                             float damage) noexcept
    -> ShipCrewDamageResult {

    const auto hit =
        raycast_first_living(
            ship,
            origin,
            direction,
            max_distance);
    if (!hit.hit) {
        return {};
    }

    return apply_damage(
        hit.member_id,
        damage,
        hit.distance);
}

auto ShipCrewSystem::raycast_first_living(
    const ShipEntity& ship,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float max_distance) const noexcept -> ShipCrewRayHit {

    if (!std::isfinite(origin.x) ||
        !std::isfinite(origin.y) ||
        !std::isfinite(origin.z) ||
        !std::isfinite(direction.x) ||
        !std::isfinite(direction.y) ||
        !std::isfinite(direction.z) ||
        !std::isfinite(max_distance) ||
        max_distance <= 0.0F ||
        glm::dot(direction, direction) <= 1.0e-6F) {
        return {};
    }

    const auto ray_direction =
        glm::normalize(direction);
    const auto hit = find_crew_ray_hit(
        ship,
        state_.members,
        render_instances_,
        origin,
        ray_direction,
        max_distance,
        false);
    if (!hit.has_value()) {
        return {};
    }

    return {
        true,
        state_.members[hit->index].id,
        origin +
            ray_direction *
                hit->distance,
        hit->distance,
    };
}

auto ShipCrewSystem::apply_damage(
    std::uint8_t member_id,
    float damage,
    float hit_distance) noexcept -> ShipCrewDamageResult {

    if (!std::isfinite(damage) ||
        damage <= 0.0F) {
        return {};
    }

    const auto iterator =
        std::find_if(
            state_.members.begin(),
            state_.members.end(),
            [member_id](const ShipCrewMemberSaveState& member) {
                return member.id == member_id;
            });
    if (iterator == state_.members.end()) {
        return {};
    }

    const auto index =
        static_cast<std::size_t>(
            std::distance(
                state_.members.begin(),
                iterator));
    auto& member = *iterator;
    if (member.health <= 0.0F ||
        member.recovery_timer > 0.0F) {
        return {};
    }

    auto& runtime = runtime_[index];
    const auto hit_position =
        render_instances_[index].position;
    const auto applied_damage =
        std::min(member.health, damage);
    member.health =
        std::max(0.0F, member.health - applied_damage);
    member.hurt_timer = kCrewHurtSeconds;
    const auto knocked_out = member.health <= 0.0F;

    if (knocked_out) {
        member.health = 0.0F;
        member.recovery_timer =
            kShipCrewKnockoutSeconds;
        runtime.current_speed = 0.0F;
        runtime.motion_amount = 0.0F;
        runtime.blocked = false;
        runtime.blocked_timer = 0.0F;
        runtime.recover_timer = 0.0F;
        render_instances_[index].activity =
            CrewVisualActivity::KnockedOut;
        render_instances_[index].knockout_amount =
            1.0F;
    } else {
        render_instances_[index].hurt_amount =
            1.0F;
    }

    return {
        true,
        knocked_out,
        member.id,
        hit_position,
        applied_damage,
        member.health,
        std::isfinite(hit_distance)
            ? std::max(hit_distance, 0.0F)
            : 0.0F,
    };
}

auto ShipCrewSystem::focus_from_ray(const ShipEntity& ship,
                                    const glm::vec3& origin,
                                    const glm::vec3& direction,
                                    float max_distance) const noexcept
    -> ShipCrewFocusState {

    if (!std::isfinite(origin.x) ||
        !std::isfinite(origin.y) ||
        !std::isfinite(origin.z) ||
        !std::isfinite(direction.x) ||
        !std::isfinite(direction.y) ||
        !std::isfinite(direction.z) ||
        !std::isfinite(max_distance) ||
        max_distance <= 0.0F ||
        glm::dot(direction, direction) <= 1.0e-6F) {
        return {};
    }

    const auto hit = find_crew_ray_hit(
        ship,
        state_.members,
        render_instances_,
        origin,
        glm::normalize(direction),
        max_distance,
        true);
    if (!hit.has_value()) {
        return {};
    }

    const auto& member =
        state_.members[hit->index];
    const auto& runtime =
        runtime_[hit->index];
    ShipCrewFocusState focus {};
    focus.visible = true;
    focus.moving = member_is_moving(member);
    focus.blocked =
        focus.moving &&
        runtime.blocked_timer >= 0.12F;
    focus.knocked_out =
        member.health <= 0.0F ||
        member.recovery_timer > 0.0F;
    focus.member_id = member.id;
    focus.role = member.role;
    focus.activity = member.activity;
    focus.cargo = member.cargo;
    focus.destination_station =
        member.destination_station;
    focus.health_ratio = std::clamp(
        member.health /
            std::max(
                ship_crew_max_health(member.role),
                0.001F),
        0.0F,
        1.0F);
    focus.distance = hit->distance;

    if (!focus.moving &&
        !focus.knocked_out &&
        member.cargo == ShipCrewCargo::None) {
        if (member.role == ShipCrewRole::Fisher &&
            member.activity == ShipCrewActivity::Fish) {
            focus.has_progress = true;
            focus.progress_ratio = std::clamp(
                member.work_progress /
                    kAutomaticFishWorkSeconds,
                0.0F,
                1.0F);
        } else if (
            member.role == ShipCrewRole::WaterTender &&
            member.activity ==
                ShipCrewActivity::TendWater) {
            focus.has_progress = true;
            focus.progress_ratio = std::clamp(
                member.work_progress /
                    kAutomaticWaterWorkSeconds,
                0.0F,
                1.0F);
        }
    }

    return focus;
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
    for (std::size_t index = 0; index < state_.members.size(); ++index) {
        const auto& member = state_.members[index];
        const auto& runtime = runtime_[index];
        const auto moving = member_is_moving(member);
        // Je conserve visuellement la locomotion pendant la deceleration : le
        // sampler peut ainsi ramener les deux semelles au pont avant d'afficher
        // l'animation de tache au poste suivant.
        const auto visually_moving =
            moving || runtime.motion_amount > 0.02F;
        const auto visual_offset = runtime.visual_offset;
        const auto exterior = station_is_exterior(blueprint, member.current_station) && member.local_position.y >= 3.70F;
        auto& render = render_instances_[index];
        render.position =
            ship.local_to_world_point(
                member.local_position +
                visual_offset);
        render.yaw_radians = member.yaw_radians;
        render.platform_orientation =
            ship.orientation();
        render.animation_time = member.animation_time;
        render.appearance_seed = hash_u32(appearance_seed_ ^ (0x9E3779B9U * static_cast<std::uint32_t>(index + 1U)));
        render.role = visual_role(member.role);
        render.activity = visual_activity(
            member,
            runtime.activity_phase,
            runtime.recover_timer,
            visually_moving);
        render.motion_amount = runtime.motion_amount;
        // Je derive la phase visuelle de la distance effectivement parcourue.
        // Le modulo positif garantit une valeur finie dans [0, 1), meme si un
        // etat runtime invalide est rencontre avant sa reconstruction.
        const auto locomotion_distance =
            finite_or(runtime.locomotion_distance, 0.0F);
        const auto wrapped_locomotion_distance =
            std::fmod(
                std::fmod(
                    locomotion_distance,
                    kCrewLocomotionCycleDistance) +
                    kCrewLocomotionCycleDistance,
                kCrewLocomotionCycleDistance);
        render.locomotion_phase =
            wrapped_locomotion_distance /
            kCrewLocomotionCycleDistance;
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
