#include "app/DeathScreen.h"
#include "app/GameOptions.h"
#include "gameplay/PlayerController.h"

#include <doctest/doctest.h>

#include <array>
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
