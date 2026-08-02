#pragma once

#include "app/Audit.h"
#include "render/VisualPipeline.h"
#include "world/World.h"

#include <span>
#include <vector>
#include <string>
#include <string_view>

namespace valcraft {

struct PerformanceOptions {
    int spawn_preload_radius = 1;
    std::size_t chunk_generation_budget = 2;
    std::size_t fluid_cell_budget = 256;
    std::size_t mesh_rebuild_budget = 4;
    std::size_t light_node_budget = 8192;
    double max_generation_ms = 1.0;
    double max_fluid_ms = 1.0;
    double max_lighting_ms = 1.5;
    double max_meshing_ms = 2.0;
    int stream_radius = 5;
    bool shadows_enabled = true;
    int shadow_map_size = 1024;
    bool post_process_enabled = true;
    bool adaptive_quality = true;
    bool report_frame_stats = false;
    std::string perf_json_path {};
    bool perf_trace_enabled = false;
    std::string perf_scenario {};
    std::size_t perf_warmup_frames = 0;

    [[nodiscard]] auto world_budget() const noexcept -> WorldWorkBudget {
        WorldWorkBudget budget {};
        budget.chunk_generation_budget = chunk_generation_budget;
        budget.fluid_cell_budget = fluid_cell_budget;
        budget.mesh_rebuild_budget = mesh_rebuild_budget;
        budget.light_node_budget = light_node_budget;
        budget.max_generation_ms = max_generation_ms;
        budget.max_fluid_ms = max_fluid_ms;
        budget.max_lighting_ms = max_lighting_ms;
        budget.max_meshing_ms = max_meshing_ms;
        return budget;
    }
};

enum class StartupUiOverlay {
    None = 0,
    Inventory = 1,
    Pause = 2,
    Progression = 3,
    ConstructionPlan = 4,
};

enum class SmokeSessionMode {
    Menu = 0,
    SeaNew = 1,
    SeaLegacy = 2,
    SeaOpen = 3,
    Backrooms = 4,
};

enum class SmokeShipView {
    None = 0,
    Deck = 1,
    Bow = 2,
    Stern = 3,
    Port = 4,
    Starboard = 5,
    Interior = 6,
    CaptainCabin = 7,
    CargoHold = 8,
    CrewDeck = 9,
    Infirmary = 10,
    Mess = 11,
    GunDeck = 12,
    Underwater = 13,
    Wake = 14,
};

enum class BackroomsJackSmokeMode {
    None = 0,
    Standing = 1,
    Hunched = 2,
    Stare = 3,
    Chase = 4,
    Jumpscare = 5,
    // Je distingue les deux mises en scene psychologiques afin de pouvoir
    // capturer leurs vraies distances sans modifier une partie normale.
    CorridorStare = 6,
    RearStare = 7,
};

inline constexpr auto kBackroomsJackSmokeCorridorDistanceMinimum = 32.0F;
inline constexpr auto kBackroomsJackSmokeCorridorDistanceDefault = 40.0F;
inline constexpr auto kBackroomsJackSmokeCorridorDistanceMaximum = 52.0F;

struct GameOptions {
    bool smoke_test = false;
    int smoke_frames = 60;
    SmokeSessionMode smoke_session = SmokeSessionMode::Menu;
    SmokeShipView smoke_ship_view = SmokeShipView::None;
    bool hidden_window = false;
    int window_width = 1600;
    int window_height = 900;
    bool freeze_time = false;
    float initial_time_of_day = 8.0F;
    // Je distingue l'heure par défaut d'un override réellement fourni en CLI.
    bool initial_time_explicitly_set = false;
    float initial_weather_time_seconds = 0.0F;
    StartupUiOverlay startup_ui_overlay = StartupUiOverlay::None;
    std::string frame_capture_path {};
    // Je démarre désormais sur le rendu moderne demandé ; le pipeline 16×16
    // reste disponible explicitement avec --visual-pipeline=legacy.
    VisualPipeline visual_pipeline = VisualPipeline::ModernStylized;
    PerformanceOptions performance {};
    AuditOptions audit {};
    std::vector<std::string> raw_arguments {};
    // Je place les nouveaux paramètres de smoke en fin d'agrégat pour ne pas
    // déplacer les initialiseurs historiques utilisés par les outils de test.
    bool smoke_backrooms_flashlight = false;
    // Je peux cadrer directement la dalle supérieure lors d'un contrôle visuel
    // automatisé, sans modifier la caméra d'une partie normale.
    bool smoke_backrooms_ceiling_view = false;
    // Je fixe la camera dans une poche Blackout sans source proche. Je peux
    // ainsi capturer exactement la meme scene avec ou sans la Maglite.
    bool smoke_backrooms_blackout = false;
    BackroomsJackSmokeMode smoke_backrooms_jack =
        BackroomsJackSmokeMode::None;
    // Je garde 40 m comme cadrage de reference, mais je peux rejouer les
    // bornes deterministes du couloir sans modifier le comportement normal.
    float smoke_backrooms_jack_distance =
        kBackroomsJackSmokeCorridorDistanceDefault;
    bool smoke_backrooms_jack_distance_explicitly_set = false;
    // Je peux capturer directement un étage profond sans devoir automatiser
    // plusieurs escaliers dans un smoke visuel.
    int smoke_backrooms_level = 0;
};

// Je réserve l'override horaire au smoke maritime déterministe afin qu'une
// nouvelle partie interactive conserve son départ historique à 8 h 15.
[[nodiscard]] constexpr auto resolve_new_session_time_of_day(
    const GameOptions& options) noexcept -> float {
    if (options.smoke_test &&
        options.smoke_session == SmokeSessionMode::SeaNew &&
        options.initial_time_explicitly_set) {
        return options.initial_time_of_day;
    }
    return 8.25F;
}

// Je centralise la distinction entre le smoke du menu et ceux qui doivent
// charger une vraie partie avant la premiere frame de capture.
[[nodiscard]] constexpr auto smoke_session_starts_gameplay(
    SmokeSessionMode mode) noexcept -> bool {
    switch (mode) {
    case SmokeSessionMode::SeaNew:
    case SmokeSessionMode::SeaLegacy:
    case SmokeSessionMode::SeaOpen:
    case SmokeSessionMode::Backrooms:
        return true;
    case SmokeSessionMode::Menu:
    default:
        return false;
    }
}

struct GameOptionParseResult {
    bool ok = false;
    GameOptions options {};
    std::string error_message {};
};

[[nodiscard]] auto parse_game_options(std::span<const std::string_view> arguments) -> GameOptionParseResult;
[[nodiscard]] auto parse_game_options(int argc, char** argv) -> GameOptionParseResult;

} // namespace valcraft
