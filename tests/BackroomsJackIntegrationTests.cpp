#include "app/DeathScreen.h"
#include "app/GameOptions.h"
#include "gameplay/BackroomsJack.h"
#include "gameplay/PlayerController.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace valcraft {

TEST_CASE("Jack smoke poses are parsed explicitly") {
    constexpr std::array cases {
        std::pair {
            std::string_view {"standing"},
            BackroomsJackSmokeMode::Standing,
        },
        std::pair {
            std::string_view {"hunched"},
            BackroomsJackSmokeMode::Hunched,
        },
        std::pair {
            std::string_view {"stare"},
            BackroomsJackSmokeMode::Stare,
        },
        std::pair {
            std::string_view {"chase"},
            BackroomsJackSmokeMode::Chase,
        },
        std::pair {
            std::string_view {"jumpscare"},
            BackroomsJackSmokeMode::Jumpscare,
        },
        std::pair {
            std::string_view {"corridor"},
            BackroomsJackSmokeMode::CorridorStare,
        },
        std::pair {
            std::string_view {"rear"},
            BackroomsJackSmokeMode::RearStare,
        },
    };
    for (const auto& [name, expected] : cases) {
        const auto argument =
            std::string {"--smoke-backrooms-jack="} +
            std::string {name};
        const std::array<std::string_view, 1> arguments {
            argument,
        };
        const auto parsed =
            parse_game_options(arguments);
        INFO(argument);
        REQUIRE(parsed.ok);
        CHECK(
            parsed.options.smoke_backrooms_jack ==
            expected);
    }

    const std::array<std::string_view, 1> invalid {
        "--smoke-backrooms-jack=teleport",
    };
    const auto rejected =
        parse_game_options(invalid);
    CHECK_FALSE(rejected.ok);
    CHECK_FALSE(rejected.error_message.empty());
}

TEST_CASE("Jack corridor smoke distance is bounded and order independent") {
    const auto parse_corridor_distance =
        [](std::string_view distance,
           bool distance_first = false) {
            const auto distance_argument =
                std::string {
                    "--smoke-backrooms-jack-distance="} +
                std::string {distance};
            const auto corridor_argument =
                std::string {
                    "--smoke-backrooms-jack=corridor"};
            const std::array<std::string_view, 2> arguments {
                distance_first
                    ? std::string_view {distance_argument}
                    : std::string_view {corridor_argument},
                distance_first
                    ? std::string_view {corridor_argument}
                    : std::string_view {distance_argument},
            };
            return parse_game_options(arguments);
        };

    const auto default_parse = parse_game_options(
        std::array<std::string_view, 1> {
            "--smoke-backrooms-jack=corridor",
        });
    REQUIRE(default_parse.ok);
    CHECK_FALSE(
        default_parse.options
            .smoke_backrooms_jack_distance_explicitly_set);
    CHECK(
        default_parse.options.smoke_backrooms_jack_distance ==
        doctest::Approx(
            kBackroomsJackSmokeCorridorDistanceDefault));

    constexpr std::array accepted_distances {
        std::pair {std::string_view {"32"}, 32.0F},
        std::pair {std::string_view {"40.5"}, 40.5F},
        std::pair {std::string_view {"52"}, 52.0F},
    };
    for (const auto& [distance, expected] : accepted_distances) {
        for (const auto distance_first : {false, true}) {
            const auto parsed =
                parse_corridor_distance(
                    distance,
                    distance_first);
            INFO(distance);
            INFO(distance_first);
            REQUIRE(parsed.ok);
            CHECK(
                parsed.options.smoke_backrooms_jack ==
                BackroomsJackSmokeMode::CorridorStare);
            CHECK(
                parsed.options
                    .smoke_backrooms_jack_distance_explicitly_set);
            CHECK(
                parsed.options.smoke_backrooms_jack_distance ==
                doctest::Approx(expected));
        }
    }

    for (const auto distance :
         {std::string_view {"31.999"},
          std::string_view {"52.001"},
          std::string_view {"nan"},
          std::string_view {"inf"},
          std::string_view {"not-a-number"},
          std::string_view {""}}) {
        INFO(distance);
        CHECK_FALSE(parse_corridor_distance(distance).ok);
    }

    const auto wrong_mode = parse_game_options(
        std::array<std::string_view, 2> {
            "--smoke-backrooms-jack=rear",
            "--smoke-backrooms-jack-distance=40",
        });
    CHECK_FALSE(wrong_mode.ok);
    const auto no_mode = parse_game_options(
        std::array<std::string_view, 1> {
            "--smoke-backrooms-jack-distance=40",
        });
    CHECK_FALSE(no_mode.ok);
}

TEST_CASE("Jack readiness observes a seven by seven revision snapshot") {
    static_assert(kBackroomsJackReadinessChunkRadius == 3);
    static_assert(kBackroomsJackReadinessChunkSide == 7);
    static_assert(kBackroomsJackReadinessCellCount == 49U);

    const BackroomsJackChunkReadiness readiness {};
    CHECK(readiness.ready.size() == 49U);
    CHECK(readiness.mesh_revisions.size() == 49U);
    CHECK(std::all_of(
        readiness.ready.begin(),
        readiness.ready.end(),
        [](bool ready) {
            return !ready;
        }));
    CHECK(std::all_of(
        readiness.mesh_revisions.begin(),
        readiness.mesh_revisions.end(),
        [](std::uint64_t revision) {
            return revision == 0U;
        }));
}

TEST_CASE("Game binds Jack to committed fog revisions and the current level") {
    const auto game_path =
        std::filesystem::path {__FILE__}.parent_path().parent_path() /
        "src" / "app" / "Game.cpp";
    std::ifstream input(game_path, std::ios::binary);
    REQUIRE(input.is_open());
    const std::string source {
        std::istreambuf_iterator<char> {input},
        std::istreambuf_iterator<char> {},
    };

    CHECK(source.find("kBackroomsJackFogSafetyMargin = 2.0F") !=
          std::string::npos);
    CHECK(source.find("kBackroomsJackVisibleDistanceCap = 64.0F") !=
          std::string::npos);
    CHECK(source.find("snapshot.safe_visible_distance(") !=
          std::string::npos);
    CHECK(source.find("renderer_.backrooms_terminal_fog_snapshot()") !=
          std::string::npos);
    CHECK(source.find(".maximum_visible_distance =") !=
          std::string::npos);
    CHECK(source.find("readiness.ready[index] ? revision : 0U") !=
          std::string::npos);
    CHECK(source.find("world.find_chunk(chunk)") !=
          std::string::npos);
    CHECK(source.find("!world_chunk->is_dirty()") != std::string::npos);
    CHECK(source.find("!world_chunk->is_lighting_dirty()") !=
          std::string::npos);
    CHECK(source.find("renderer.world_mesh_uploaded(chunk, revision)") !=
          std::string::npos);
    const auto jack_session_support = source.find(
        "auto Game::session_backrooms_supports_jack() const noexcept");
    REQUIRE(jack_session_support != std::string::npos);
    CHECK(source.find(
              "return backrooms_active();",
              jack_session_support) <
          jack_session_support + 320U);
    const auto current_level_binding = source.find(
        "const auto jack_logical_level =");
    REQUIRE(current_level_binding != std::string::npos);
    const auto current_generator = source.find(
        "const BackroomsGenerator generator",
        current_level_binding);
    REQUIRE(current_generator != std::string::npos);
    CHECK(source.find(
              "jack_logical_level",
              current_generator) <
          current_generator + 240U);
    CHECK(source.find(
              "options_.smoke_backrooms_jack_distance") !=
          std::string::npos);
    CHECK(source.find("preview_distance = 24.0F") !=
          std::string::npos);
    CHECK(source.find("safe_yaw + 180.0F") !=
          std::string::npos);
    const auto distant_boot = source.find(
        "case BackroomsJackEventKind::DistantBootStep:",
        source.find("backrooms_jack_event_volume"));
    const auto distant_wooden_leg = source.find(
        "case BackroomsJackEventKind::DistantWoodenLegStep:",
        distant_boot);
    REQUIRE(distant_boot != std::string::npos);
    REQUIRE(distant_wooden_leg != std::string::npos);
    CHECK(source.find("return 0.41F;", distant_boot) <
          distant_boot + 120U);
    CHECK(source.find("return 0.48F;", distant_wooden_leg) <
          distant_wooden_leg + 120U);

    // Je verrouille la plage psychoacoustique retenue : les pas restent
    // identifiables sans donner l'impression que Jack est deja tout pres.
    constexpr auto distant_boot_ratio = 0.41F / 0.76F;
    constexpr auto distant_wooden_leg_ratio = 0.48F / 0.88F;
    CHECK(distant_boot_ratio >= 0.35F);
    CHECK(distant_boot_ratio <= 0.55F);
    CHECK(distant_wooden_leg_ratio >= 0.35F);
    CHECK(distant_wooden_leg_ratio <= 0.55F);
}

TEST_CASE("Jack owns a stable player death cause and death screen label") {
    PlayerController player {
        {0.5F, 41.001F, 0.5F},
    };
    player.force_death(
        PlayerDeathCause::JackThePirate);
    REQUIRE(player.is_dead());
    CHECK(
        player.state().death_cause ==
        PlayerDeathCause::JackThePirate);
    CHECK(
        player_death_cause_label(
            player.state().death_cause) ==
        "JACK LE PIRATE");
    CHECK(
        death_screen_cause_label(
            player.state().death_cause) ==
        "CAUSE JACK LE PIRATE");
}

} // namespace valcraft
