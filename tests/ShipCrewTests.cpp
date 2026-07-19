#include "gameplay/SeaAdventure.h"
#include "gameplay/ShipCrew.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <sstream>
#include <string>

namespace valcraft {

namespace {

auto station_position(ShipCrewStation station) -> glm::vec3 {
    for (const auto& node : amelie_ship_blueprint().crew_navigation_nodes) {
        if (node.station == station) {
            return node.local_position;
        }
    }
    return {};
}

void place_at_station(ShipCrewMemberSaveState& member, ShipCrewStation station) {
    member.local_position = station_position(station);
    member.current_station = station;
    member.next_station = station;
    member.destination_station = station;
}

auto update_for(ShipCrewSystem& crew,
                const ShipEntity& ship,
                const EnvironmentState& environment,
                float seconds,
                std::uint32_t& fish,
                std::uint32_t& water) -> ShipCrewUpdateResult {
    ShipCrewUpdateResult combined {};
    auto remaining = seconds;
    while (remaining > 0.0F) {
        const auto step = std::min(remaining, 0.25F);
        const auto result = crew.update(ship, environment, step, fish, water);
        combined.fish_delivered = combined.fish_delivered || result.fish_delivered;
        combined.water_delivered = combined.water_delivered || result.water_delivered;
        remaining -= step;
    }
    return combined;
}

} // namespace

TEST_CASE("L'Amelie expose un graphe d'equipage stable et connexe") {
    const auto& blueprint = amelie_ship_blueprint();
    CHECK(std::string(blueprint.name) == "L'Amélie");
    REQUIRE(blueprint.crew_navigation_nodes.size() == static_cast<std::size_t>(ShipCrewStation::Count));
    CHECK(blueprint.navigation_revision != 0U);

    std::array<bool, static_cast<std::size_t>(ShipCrewStation::Count)> present {};
    for (const auto& node : blueprint.crew_navigation_nodes) {
        const auto index = static_cast<std::size_t>(node.station);
        REQUIRE(index < present.size());
        CHECK_FALSE(present[index]);
        present[index] = true;
        CHECK(std::isfinite(node.local_position.x));
        CHECK(std::isfinite(node.local_position.y));
        CHECK(std::isfinite(node.local_position.z));
    }
    for (const auto value : present) {
        CHECK(value);
    }

    std::array<bool, static_cast<std::size_t>(ShipCrewStation::Count)> visited {};
    std::queue<ShipCrewStation> pending;
    pending.push(ShipCrewStation::Helm);
    visited[static_cast<std::size_t>(ShipCrewStation::Helm)] = true;
    while (!pending.empty()) {
        const auto current = pending.front();
        pending.pop();
        for (const auto& edge : blueprint.crew_navigation_edges) {
            auto neighbor = ShipCrewStation::Count;
            if (edge.first == current) neighbor = edge.second;
            if (edge.second == current) neighbor = edge.first;
            const auto index = static_cast<std::size_t>(neighbor);
            if (index < visited.size() && !visited[index]) {
                visited[index] = true;
                pending.push(neighbor);
            }
        }
    }
    for (const auto value : visited) {
        CHECK(value);
    }
}

TEST_CASE("la refonte de L'Amelie ferme la poupe et libere la hauteur des marins") {
    const auto& blueprint = amelie_ship_blueprint();
    CHECK(blueprint.bounds.max.x - blueprint.bounds.min.x >= 17.0F);
    CHECK(blueprint.bounds.min.z <= -35.0F);
    CHECK(blueprint.bounds.max.z >= 45.0F);

    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    const auto origin = ship.world_origin();
    for (float z = -34.0F; z <= -31.0F; z += 0.5F) {
        const auto feet = origin + glm::vec3 {0.0F, 1.01F, z};
        const auto support = ship.support_height_in_range(feet, feet.y - 0.10F, feet.y + 0.10F);
        REQUIRE_MESSAGE(support.has_value(), "le plancher de la cabine doit atteindre la poupe");
    }
    CHECK(ship.intersects_aabb(
        origin + glm::vec3 {-0.25F, 4.72F, -35.12F},
        origin + glm::vec3 {0.25F, 5.28F, -34.88F}));

    for (const auto& node : blueprint.crew_navigation_nodes) {
        CAPTURE(static_cast<int>(node.station));
        const auto feet = origin + node.local_position;
        const auto support = ship.support_height_in_range(feet, feet.y - 0.16F, feet.y + 0.16F);
        CHECK_MESSAGE(support.has_value(), "chaque station d'equipage doit avoir un support");
        CHECK_FALSE(ship.intersects_aabb(
            feet + glm::vec3 {-0.24F, 0.04F, -0.24F},
            feet + glm::vec3 {0.24F, 1.84F, 0.24F}));
    }
}

TEST_CASE("chaque route hors escalier garde le marin sur un passage degage") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    const auto origin = ship.world_origin();
    const auto& blueprint = amelie_ship_blueprint();

    const auto is_stair_edge = [](const ShipCrewNavigationEdge& edge) {
        const auto matches = [&](ShipCrewStation first, ShipCrewStation second) {
            return (edge.first == first && edge.second == second) ||
                   (edge.first == second && edge.second == first);
        };
        return matches(ShipCrewStation::AftStairsTop, ShipCrewStation::AftStairsMid) ||
               matches(ShipCrewStation::AftStairsMid, ShipCrewStation::AftStairsBottom) ||
               matches(ShipCrewStation::ForeStairsTop, ShipCrewStation::ForeStairsMid) ||
               matches(ShipCrewStation::ForeStairsMid, ShipCrewStation::ForeStairsBottom) ||
               matches(ShipCrewStation::QuarterdeckStepTop, ShipCrewStation::QuarterdeckStepBottom) ||
               matches(ShipCrewStation::ForecastleStepBottom, ShipCrewStation::ForecastleStepTop);
    };

    std::ostringstream failures;
    for (const auto& edge : blueprint.crew_navigation_edges) {
        if (is_stair_edge(edge)) {
            continue;
        }
        const auto start = station_position(edge.first);
        const auto end = station_position(edge.second);
        const auto sample_count = std::max(1, static_cast<int>(std::ceil(glm::length(end - start) / 0.20F)));
        bool edge_reported = false;
        for (int sample = 0; sample <= sample_count; ++sample) {
            const auto amount = static_cast<float>(sample) / static_cast<float>(sample_count);
            const auto feet = origin + start + (end - start) * amount;
            const auto supported = ship.support_height_in_range(feet, feet.y - 0.16F, feet.y + 0.16F).has_value();
            const auto blocked = ship.intersects_aabb(
                feet + glm::vec3 {-0.34F, 0.04F, -0.34F},
                feet + glm::vec3 {0.34F, 1.88F, 0.34F});
            if ((!supported || blocked) && !edge_reported) {
                failures << static_cast<int>(edge.first) << "->" << static_cast<int>(edge.second)
                         << " sample=" << sample << " position=(" << feet.x << ',' << feet.y << ',' << feet.z
                         << ") support=" << supported << " blocked=" << blocked << '\n';
                edge_reported = true;
            }
        }
    }
    CHECK_MESSAGE(failures.str().empty(), failures.str());
}

TEST_CASE("le roster canonique contient un capitaine et cinq matelots distincts") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(1337, ship);

    const auto members = crew.members();
    const auto render = crew.render_instances();
    REQUIRE(members.size() == kShipCrewMemberCount);
    REQUIRE(render.size() == kShipCrewMemberCount);
    const std::array<ShipCrewRole, kShipCrewMemberCount> expected {{
        ShipCrewRole::Captain,
        ShipCrewRole::Fisher,
        ShipCrewRole::Rigger,
        ShipCrewRole::WaterTender,
        ShipCrewRole::Deckhand,
        ShipCrewRole::Quartermaster,
    }};
    for (std::size_t index = 0; index < members.size(); ++index) {
        CHECK(members[index].id == index);
        CHECK(members[index].role == expected[index]);
        CHECK(members[index].health == doctest::Approx(ship_crew_max_health(expected[index])));
        CHECK(render[index].appearance_seed != 0U);
        if (index > 0U) {
            CHECK(render[index].appearance_seed != render[index - 1U].appearance_seed);
        }
    }
}

TEST_CASE("les marins restent exactement dans le repere local du navire") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(731, ship);
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, {}, 0.0F, fish, water);
    const auto local_before = crew.members()[0].local_position;
    const auto world_before = crew.render_instances()[0].position;

    ship.set_position({0.5F, 49.0F, 500'000.5F});
    (void)crew.update(ship, {}, 0.0F, fish, water);
    CHECK(crew.members()[0].local_position.x == doctest::Approx(local_before.x));
    CHECK(crew.members()[0].local_position.y == doctest::Approx(local_before.y));
    CHECK(crew.members()[0].local_position.z == doctest::Approx(local_before.z));
    CHECK(crew.render_instances()[0].position.z - world_before.z == doctest::Approx(500'000.0F));
}

TEST_CASE("la marche oriente le marin dans la direction reelle de son trajet") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(732, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.next_station = ShipCrewStation::MidDeckPort;
    fisher.destination_station = ShipCrewStation::MidDeckPort;
    crew.load_state(state, 732, ship);

    const auto expected = glm::normalize(station_position(ShipCrewStation::MidDeckPort) -
                                         station_position(ShipCrewStation::PortFishing));
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, {}, 0.25F, fish, water);
    const auto yaw = crew.members()[1].yaw_radians;
    const auto alignment = std::cos(yaw) * expected.x + std::sin(yaw) * expected.z;
    CHECK(alignment > 0.999F);
}

TEST_CASE("le poisson est credite uniquement apres son transport dans la cale") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(91, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.activity = ShipCrewActivity::Fish;
    fisher.routine_step = 0U;
    fisher.work_progress = kAutomaticFishWorkSeconds - 0.10F;
    crew.load_state(state, 91, ship);

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, {}, 0.05F, fish, water);
    CHECK(fish == 0U);
    CHECK(crew.members()[1].cargo == ShipCrewCargo::None);
    (void)crew.update(ship, {}, 0.05F, fish, water);
    CHECK(fish == 0U);
    CHECK(crew.members()[1].cargo == ShipCrewCargo::Fish);

    const auto result = update_for(crew, ship, {}, 120.0F, fish, water);
    CHECK(result.fish_delivered);
    CHECK(fish == 1U);
    CHECK(crew.members()[1].cargo == ShipCrewCargo::None);
}

TEST_CASE("l'eau profite de la pluie mais respecte son objectif automatique") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(92, ship);
    auto state = crew.save_state();
    auto& tender = state.members[3];
    place_at_station(tender, ShipCrewStation::WaterStill);
    tender.activity = ShipCrewActivity::TendWater;
    tender.routine_step = 0U;
    tender.work_progress = kAutomaticWaterWorkSeconds - 0.30F;
    crew.load_state(state, 92, ship);

    EnvironmentState rain {};
    rain.precipitation_intensity = 1.0F;
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, rain, 0.10F, fish, water);
    CHECK(crew.members()[3].cargo == ShipCrewCargo::None);
    (void)crew.update(ship, rain, 0.11F, fish, water);
    CHECK(crew.members()[3].cargo == ShipCrewCargo::Water);
    CHECK(water == 0U);
    const auto delivered = update_for(crew, ship, rain, 120.0F, fish, water);
    CHECK(delivered.water_delivered);
    CHECK(water == 1U);

    state = crew.save_state();
    auto& full_tender = state.members[3];
    full_tender.work_progress = 200.0F;
    full_tender.cargo = ShipCrewCargo::None;
    full_tender.routine_step = 0U;
    place_at_station(full_tender, ShipCrewStation::WaterStill);
    crew.load_state(state, 92, ship);
    water = kAutomaticWaterTarget;
    (void)crew.update(ship, rain, 0.25F, fish, water);
    CHECK(crew.members()[3].work_progress == doctest::Approx(0.0F));
    CHECK(water == kAutomaticWaterTarget);
}

TEST_CASE("dt nul et tempete forte ne font pas progresser la production") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(51, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.work_progress = 42.0F;
    fisher.activity = ShipCrewActivity::Fish;
    crew.load_state(state, 51, ship);
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;

    (void)crew.update(ship, {}, 0.0F, fish, water);
    CHECK(crew.members()[1].work_progress == doctest::Approx(42.0F));
    EnvironmentState storm {};
    storm.storm_intensity = 0.80F;
    (void)update_for(crew, ship, storm, 5.0F, fish, water);
    CHECK(crew.members()[1].work_progress == doctest::Approx(42.0F));
    CHECK(fish == 0U);
}

TEST_CASE("un marin assomme conserve son travail puis recupere sans mourir") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(404, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.work_progress = 57.0F;
    crew.load_state(state, 404, ship);

    const auto target = crew.render_instances()[1].position + glm::vec3 {0.0F, 0.95F, 0.0F};
    const auto origin = target + glm::vec3 {0.0F, 0.0F, -3.0F};
    const auto hit = crew.try_damage_from_player(ship, origin, {0.0F, 0.0F, 1.0F}, 5.0F, 100.0F);
    REQUIRE(hit.hit);
    CHECK(hit.knocked_out);
    CHECK(hit.member_id == 1U);

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)update_for(crew, ship, {}, 29.75F, fish, water);
    CHECK(crew.members()[1].health == doctest::Approx(0.0F));
    CHECK(crew.members()[1].work_progress == doctest::Approx(57.0F));

    const auto saved_while_down = crew.save_state();
    ShipCrewSystem restored {};
    restored.load_state(saved_while_down, 404, ship);
    (void)update_for(restored, ship, {}, 0.25F, fish, water);
    CHECK(restored.members()[1].health == doctest::Approx(14.0F));
    CHECK(restored.members()[1].work_progress == doctest::Approx(57.0F));
    CHECK(restored.render_instances()[1].activity == CrewVisualActivity::Recover);
}

TEST_CASE("un marin assomme en transport reprend sa livraison apres rechargement") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(405, ship);

    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.cargo = ShipCrewCargo::Fish;
    fisher.activity = ShipCrewActivity::Carry;
    fisher.routine_step = 1U;
    fisher.destination_station = ShipCrewStation::CargoFish;
    crew.load_state(state, 405, ship);

    const auto target = crew.render_instances()[1].position + glm::vec3 {0.0F, 0.95F, 0.0F};
    const auto origin = target + glm::vec3 {0.0F, 0.0F, -3.0F};
    const auto hit = crew.try_damage_from_player(ship, origin, {0.0F, 0.0F, 1.0F}, 5.0F, 100.0F);
    REQUIRE(hit.hit);
    REQUIRE(hit.knocked_out);
    REQUIRE(crew.members()[1].cargo == ShipCrewCargo::Fish);

    ShipCrewSystem restored {};
    restored.load_state(crew.save_state(), 405, ship);
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)update_for(restored, ship, {}, kShipCrewKnockoutSeconds, fish, water);

    CHECK(restored.members()[1].health == doctest::Approx(ship_crew_max_health(ShipCrewRole::Fisher)));
    CHECK(restored.members()[1].cargo == ShipCrewCargo::Fish);
    CHECK(restored.members()[1].destination_station == ShipCrewStation::CargoFish);

    const auto delivered = update_for(restored, ship, {}, 120.0F, fish, water);
    CHECK(delivered.fish_delivered);
    CHECK(fish == 1U);
    CHECK(restored.members()[1].cargo == ShipCrewCargo::None);
}

TEST_CASE("le reveil est progressif et bloque le mouvement pendant toute son animation") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(406, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.health = 0.0F;
    fisher.recovery_timer = 0.25F;
    fisher.work_progress = 57.0F;
    crew.load_state(state, 406, ship);

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, {}, 0.25F, fish, water);
    const auto wake_position = crew.members()[1].local_position;
    CHECK(crew.render_instances()[1].activity == CrewVisualActivity::Recover);
    CHECK(crew.render_instances()[1].activity_phase == doctest::Approx(0.0F));

    (void)crew.update(ship, {}, 0.25F, fish, water);
    CHECK(crew.render_instances()[1].activity == CrewVisualActivity::Recover);
    CHECK(crew.render_instances()[1].activity_phase == doctest::Approx(0.25F));
    CHECK(glm::length(crew.members()[1].local_position - wake_position) < 1.0e-5F);
    CHECK(crew.members()[1].work_progress == doctest::Approx(57.0F));

    (void)update_for(crew, ship, {}, 0.50F, fish, water);
    CHECK(crew.render_instances()[1].activity == CrewVisualActivity::Recover);
    CHECK(crew.render_instances()[1].activity_phase == doctest::Approx(0.75F));
    CHECK(glm::length(crew.members()[1].local_position - wake_position) < 1.0e-5F);
}

TEST_CASE("le chargement repare les cargaisons et les aretes semantiquement invalides") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(407, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.cargo = ShipCrewCargo::Fish;
    fisher.activity = ShipCrewActivity::Idle;
    fisher.routine_step = 0U;

    auto& captain = state.members[0];
    place_at_station(captain, ShipCrewStation::Helm);
    captain.next_station = ShipCrewStation::CargoSort;
    captain.destination_station = ShipCrewStation::CargoSort;
    crew.load_state(state, 407, ship);

    CHECK(crew.members()[1].destination_station == ShipCrewStation::CargoFish);
    CHECK(crew.members()[1].activity == ShipCrewActivity::Carry);
    CHECK(crew.members()[1].routine_step == 1U);
    const auto& repaired_captain = crew.members()[0];
    CHECK(glm::length(repaired_captain.local_position - station_position(ShipCrewStation::Helm)) < 1.0e-5F);
    const auto adjacent = std::ranges::any_of(
        amelie_ship_blueprint().crew_navigation_edges,
        [&](const ShipCrewNavigationEdge& edge) {
            return (edge.first == repaired_captain.current_station && edge.second == repaired_captain.next_station) ||
                   (edge.second == repaired_captain.current_station && edge.first == repaired_captain.next_station);
        });
    CHECK(adjacent);
}

TEST_CASE("une revision de navigation migre les etats humains sans garder une cargaison obsolete") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(408, ship);
    auto state = crew.save_state();
    state.navigation_revision ^= 0x55U;
    state.members[0].health = 0.0F;
    state.members[0].recovery_timer = 12.0F;
    state.members[1].work_progress = 73.0F;
    state.members[1].cargo = ShipCrewCargo::Fish;
    crew.load_state(state, 408, ship);

    CHECK(crew.save_state().navigation_revision == amelie_ship_blueprint().navigation_revision);
    CHECK(crew.members()[0].health == doctest::Approx(0.0F));
    CHECK(crew.members()[0].recovery_timer == doctest::Approx(12.0F));
    CHECK(crew.members()[1].work_progress == doctest::Approx(73.0F));
    CHECK(crew.members()[1].cargo == ShipCrewCargo::None);
}

TEST_CASE("dt nul ne livre pas une cargaison et la meteo non finie reste neutre") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(409, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::CargoFish);
    fisher.cargo = ShipCrewCargo::Fish;
    auto& tender = state.members[3];
    place_at_station(tender, ShipCrewStation::WaterStill);
    tender.work_progress = 0.0F;
    crew.load_state(state, 409, ship);

    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, {}, 0.0F, fish, water);
    CHECK(fish == 0U);
    CHECK(crew.members()[1].cargo == ShipCrewCargo::Fish);

    EnvironmentState invalid_environment {};
    invalid_environment.precipitation_intensity = std::numeric_limits<float>::quiet_NaN();
    invalid_environment.storm_intensity = std::numeric_limits<float>::quiet_NaN();
    invalid_environment.daylight_factor = std::numeric_limits<float>::quiet_NaN();
    (void)crew.update(ship, invalid_environment, 0.25F, fish, water);
    CHECK(crew.members()[3].work_progress == doctest::Approx(0.25F));
    CHECK(std::isfinite(crew.render_instances()[3].daylight_factor));
}

TEST_CASE("la peche ne montre la prise que pendant les dernieres secondes du cycle") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(410, ship);
    auto state = crew.save_state();
    auto& fisher = state.members[1];
    place_at_station(fisher, ShipCrewStation::PortFishing);
    fisher.activity = ShipCrewActivity::Fish;
    fisher.work_progress = 60.0F;
    crew.load_state(state, 410, ship);
    std::uint32_t fish = 0U;
    std::uint32_t water = 0U;
    (void)crew.update(ship, {}, 0.25F, fish, water);
    CHECK(crew.render_instances()[1].activity == CrewVisualActivity::FishWait);

    state = crew.save_state();
    state.members[1].work_progress = kAutomaticFishWorkSeconds - 2.0F;
    crew.load_state(state, 410, ship);
    (void)crew.update(ship, {}, 0.25F, fish, water);
    CHECK(crew.render_instances()[1].activity == CrewVisualActivity::FishReel);
}

TEST_CASE("le navire bloque les coups portes a travers son pont") {
    ShipEntity ship {};
    ship.set_position({0.5F, 49.0F, 0.5F});
    ShipCrewSystem crew {};
    crew.reset(411, ship);
    auto state = crew.save_state();
    place_at_station(state.members[0], ShipCrewStation::CaptainCabin);
    crew.load_state(state, 411, ship);

    const auto origin = ship.world_origin() + glm::vec3 {0.0F, 6.0F, -22.0F};
    REQUIRE(ship.raycast_collidable_distance(origin, {0.0F, -1.0F, 0.0F}, 10.0F).has_value());
    const auto blocked = crew.try_damage_from_player(ship, origin, {0.0F, -1.0F, 0.0F}, 10.0F, 5.0F);
    CHECK_FALSE(blocked.hit);
}

TEST_CASE("les postes partages gardent des positions visuelles distinctes") {
    ShipEntity ship {};
    ShipCrewSystem crew {};
    crew.reset(412, ship);
    auto state = crew.save_state();
    place_at_station(state.members[2], ShipCrewStation::Capstan);
    place_at_station(state.members[4], ShipCrewStation::Capstan);
    crew.load_state(state, 412, ship);
    CHECK(glm::length(crew.render_instances()[2].position - crew.render_instances()[4].position) > 1.0F);
}

TEST_CASE("les lumieres d'equipage reutilisent exactement les lanternes de la cale") {
    const auto& blueprint = amelie_ship_blueprint();
    REQUIRE(blueprint.interior_lanterns.size() == 4U);
    for (const auto& lantern : blueprint.interior_lanterns) {
        const auto matching_part = std::ranges::any_of(blueprint.parts, [&](const ShipPart& part) {
            if (part.material != ShipMaterial::Lantern) {
                return false;
            }
            const auto center = (part.local_start + part.local_end) * 0.5F;
            return glm::length(center - lantern) < 0.01F;
        });
        CHECK(matching_part);
    }
}

TEST_CASE("la sanitization restaure les identites et rejette les valeurs corrompues") {
    ShipCrewSaveState state {};
    state.initialized = true;
    state.navigation_revision = 7U;
    for (auto& member : state.members) {
        member.id = 0U;
        member.role = static_cast<ShipCrewRole>(255U);
        member.activity = static_cast<ShipCrewActivity>(255U);
        member.cargo = static_cast<ShipCrewCargo>(255U);
        member.current_station = static_cast<ShipCrewStation>(255U);
        member.local_position = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        member.health = std::numeric_limits<float>::infinity();
    }
    const auto sanitized = sanitize_ship_crew_save_state(state);
    REQUIRE(sanitized.initialized);
    for (std::size_t index = 0; index < sanitized.members.size(); ++index) {
        const auto& member = sanitized.members[index];
        CHECK(member.id == index);
        CHECK(static_cast<std::size_t>(member.role) == index);
        CHECK(std::isfinite(member.local_position.x));
        CHECK(std::isfinite(member.health));
        CHECK(member.cargo == ShipCrewCargo::None);
    }
}

} // namespace valcraft
