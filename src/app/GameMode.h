#pragma once

#include <cstdint>
#include <string_view>

namespace valcraft {

enum class GameMode : std::uint8_t {
    ClassicAdventure = 0,
    SeaAdventure = 1,
    Backrooms = 2,
};

inline constexpr auto game_mode_label(GameMode mode) noexcept -> std::string_view {
    switch (mode) {
    case GameMode::ClassicAdventure:
        return "AVENTURE CLASSIQUE";
    case GameMode::SeaAdventure:
        return "AVENTURE EN MER";
    case GameMode::Backrooms:
        return "BACKROOMS";
    default:
        return "MODE INCONNU";
    }
}

inline constexpr auto is_known_game_mode(GameMode mode) noexcept -> bool {
    switch (mode) {
    case GameMode::ClassicAdventure:
    case GameMode::SeaAdventure:
    case GameMode::Backrooms:
        return true;
    default:
        return false;
    }
}

} // namespace valcraft
