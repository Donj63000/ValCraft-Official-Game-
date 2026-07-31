#pragma once

#include "app/Hotbar.h"
#include "app/GameMode.h"
#include "app/InventoryMenu.h"
#include "creatures/CreatureTypes.h"
#include "gameplay/ItemDropSystem.h"
#include "gameplay/BackroomsJack.h"
#include "gameplay/BackroomsFlashlight.h"
#include "gameplay/PlayerController.h"
#include "gameplay/PlayerProgression.h"
#include "gameplay/SeaAdventure.h"
#include "gameplay/progression/ExperienceAwardService.h"
#include "gameplay/progression/PlayerAbilityEffects.h"
#include "gameplay/progression/PlayerBuildState.h"
#include "gameplay/progression/SummonedUnitSystem.h"
#include "gameplay/weapons/LegendaryWeaponProgression.h"
#include "world/World.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace valcraft {

constexpr std::size_t kSaveSlotCount = 8;
constexpr std::size_t kMaximumSavedPlayerSummons = 8U;

struct SaveSlotMetadata {
    bool exists = false;
    std::uint64_t saved_at_unix_seconds = 0;
    int seed = 1337;
    float time_of_day = 8.0F;
    float weather_time_seconds = 0.0F;
    std::uint32_t modified_chunk_count = 0;
    bool has_starting_village = false;
    GameMode game_mode = GameMode::ClassicAdventure;

    auto operator==(const SaveSlotMetadata&) const -> bool = default;
};

struct WindAccelerationRuntimeSaveState {
    float remaining_seconds = 0.0F;
    float movement_bonus = 0.0F;
    float recovery_bonus = 0.0F;
    float dodge_remaining_seconds = 0.0F;
    bool blade_armed = false;
    AbilityCastSequence cast_sequence = 0U;

    auto operator==(const WindAccelerationRuntimeSaveState&) const
        -> bool = default;
};

struct SummonedFootmanRuntimeSaveState {
    SummonedUnitSystemSnapshot runtime {};
    std::optional<glm::vec3> ship_local_position {};
    float far_seconds = 0.0F;
    AbilityCastSequence cast_sequence = 0U;

    auto operator==(const SummonedFootmanRuntimeSaveState&) const
        -> bool = default;
};

struct PlayerAbilityRuntimeSaveState {
    PlayerAbilityEffectsSnapshot player_effects {};
    WindAccelerationRuntimeSaveState wind {};
    std::array<
        SummonedFootmanRuntimeSaveState,
        kMaximumSavedPlayerSummons>
        summoned_footmen {};
    // Je conserve les prochaines séquences explicitement pour garantir qu'un
    // chargement ne réutilise jamais un identifiant ou un cast déjà restauré.
    SummonedUnitId next_summoned_unit_id = 1U;
    AbilityCastSequence next_cast_sequence = 1U;

    auto operator==(const PlayerAbilityRuntimeSaveState&) const
        -> bool = default;
};

[[nodiscard]] auto sanitize_player_ability_runtime_save_state(
    const PlayerAbilityRuntimeSaveState& state) noexcept
    -> PlayerAbilityRuntimeSaveState;

struct SaveGameSnapshot {
    SaveSlotMetadata metadata {};
    glm::vec3 spawn_position {0.5F, 70.0F, 0.5F};
    PlayerState player_state {};
    PlayerProgressionState progression {};
    PlayerBuildState player_build {};
    SeaAdventureSaveState sea_adventure {};
    MaritimeExperienceAwardState maritime_experience {};
    PlayerAbilityRuntimeSaveState player_ability_runtime {};
    LegendaryWeaponProgressionState legendary_weapon {};
    BackroomsFlashlightState backrooms_flashlight {};
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    std::uint64_t musket_shot_sequence = 0U;
    std::vector<CreatureInstance> creatures {};
    std::vector<ItemDrop> item_drops {};
    // Je conserve les donnees de monde sous leur forme compacte au chargement,
    // afin de ne jamais materialiser tous les chunks sur le thread d'interface.
    WorldSavePlan world_save_plan {};
    // Je garde ce champ pour l'ecriture des anciens appelants et des fixtures.
    std::vector<WorldChunkSnapshot> chunk_snapshots {};
    // Je l'ajoute en fin de snapshot pour ne déplacer aucun initialiseur
    // positionnel historique. Le runtime de navigation reste volontairement
    // hors sauvegarde et sera reconstruit autour du joueur au chargement.
    BackroomsJackState backrooms_jack {
        initialize_backrooms_jack(0U),
    };
    // Je place le niveau à la toute fin pour conserver tous les initialisateurs
    // positionnels historiques du snapshot et l'écris dans l'extension BRLV.
    std::int32_t backrooms_level = 0;
};

enum class SaveLoadPhase : std::uint8_t {
    OpeningFile,
    ReadingMetadata,
    ReadingPlayer,
    ReadingEntities,
    ReadingWorld,
    Finalizing,
};

struct SaveLoadProgress {
    SaveLoadPhase phase = SaveLoadPhase::OpeningFile;
    std::uint64_t completed_bytes = 0;
    std::uint64_t total_bytes = 0;
    float normalized = 0.0F;
};

enum class SaveLoadControl : std::uint8_t {
    Continue,
    Cancel,
};

using SaveLoadProgressCallback = std::function<SaveLoadControl(const SaveLoadProgress&)>;

[[nodiscard]] auto save_slot_file_path(const std::filesystem::path& root_directory, std::size_t slot_index)
    -> std::filesystem::path;
[[nodiscard]] auto scan_save_slots(const std::filesystem::path& root_directory)
    -> std::array<SaveSlotMetadata, kSaveSlotCount>;
[[nodiscard]] auto load_save_slot(const std::filesystem::path& root_directory, std::size_t slot_index)
    -> std::optional<SaveGameSnapshot>;
[[nodiscard]] auto load_save_slot(const std::filesystem::path& root_directory,
                                  std::size_t slot_index,
                                  const SaveLoadProgressCallback& progress_callback)
    -> std::optional<SaveGameSnapshot>;
[[nodiscard]] auto remove_save_slot(const std::filesystem::path& root_directory, std::size_t slot_index) -> bool;
void write_save_slot(const std::filesystem::path& root_directory, std::size_t slot_index, const SaveGameSnapshot& snapshot);
void write_save_slot(const std::filesystem::path& root_directory,
                     std::size_t slot_index,
                     const SaveGameSnapshot& snapshot,
                     const WorldSavePlan& world_save_plan);

} // namespace valcraft
