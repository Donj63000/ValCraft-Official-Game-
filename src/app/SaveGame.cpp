#include "app/SaveGame.h"
#include "world/BackroomsSpatialStack.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace valcraft {

namespace {

constexpr std::array<char, 8> kSaveMagic {{'V', 'A', 'L', 'S', 'L', 'O', 'T', '1'}};
constexpr std::array<char, 4> kItemInstanceStateMagic {{'I', 'T', 'E', 'M'}};
constexpr std::array<char, 4> kLegendaryWeaponStateMagic {{'L', 'W', 'E', 'A'}};
constexpr std::array<char, 4> kBackroomsFlashlightStateMagic {{'B', 'F', 'L', 'H'}};
constexpr std::array<char, 4> kBackroomsJackStateMagic {{'B', 'J', 'C', 'K'}};
constexpr std::array<char, 4> kBackroomsLevelStateMagic {{'B', 'R', 'L', 'V'}};
constexpr std::array<char, 4> kBackroomsMarlowStateMagic {{'M', 'R', 'L', 'W'}};
constexpr std::uint8_t kLegendaryWeaponStateFormatVersion = 1U;
constexpr std::uint8_t kBackroomsFlashlightStateFormatVersion = 1U;
constexpr std::uint8_t kBackroomsJackStateFormatVersion = 1U;
constexpr std::uint8_t kBackroomsLevelStateFormatVersion = 1U;
constexpr std::uint8_t kBackroomsMarlowStateFormatVersion = 1U;
constexpr std::uint32_t kSaveVersion = 19;
constexpr std::uint32_t kSaveVersionBackroomsMarlow = 19;
constexpr std::uint32_t kSaveVersionBackroomsLevel = 18;
constexpr std::uint32_t kSaveVersionBackroomsJack = 17;
constexpr std::uint32_t kSaveVersionBackroomsFlashlight = 16;
constexpr std::uint32_t kSaveVersionBackrooms = 15;
constexpr std::uint32_t kSaveVersionLegendaryWeapon = 15;
constexpr std::uint32_t kSaveVersionRuntimeState = 14;
constexpr std::uint32_t kSaveVersionCountedBuildArrays = 14;
constexpr std::uint32_t kSaveVersionProgressionBuilds = 13;
constexpr std::uint32_t kSaveVersionItemInstanceState = 12;
constexpr std::uint32_t kSaveVersionOldGuard = 11;
constexpr std::uint32_t kSaveVersionSeaDeparture = 10;
constexpr std::uint32_t kSaveVersionShipCrew = 9;
constexpr std::uint32_t kSaveVersionCompactWorldOverrides = 8;
constexpr std::uint32_t kSaveVersionGameMode = 7;
constexpr std::uint32_t kSaveVersionPlayerProgression = 6;
constexpr std::uint32_t kSaveVersionEquipmentAndCreatureHealth = 5;
constexpr std::uint32_t kSaveVersionWeatherCycle = 4;
constexpr std::uint32_t kSaveVersionWaterState = 3;
constexpr std::uint32_t kSaveVersionStartingVillage = 2;
constexpr std::uint32_t kSaveVersionLegacy = 1;
constexpr std::uint32_t kMaxSavedCreatureCount = 256;
constexpr std::uint32_t kMaxSavedItemDropCount = 128;
constexpr std::uint32_t kMaxSavedChunkSnapshotCount = 4096;
constexpr int kSavedChunkNeighborMargin = kMaxStreamRadius + 1;
constexpr float kMaxSavedWorldCoordinateMagnitude = 1'000'000.0F;
constexpr std::uint64_t kMaximumSavedNavigationMilestone =
    1'000'000ULL /
    ExperienceRewardPolicy::kNavigationExperienceDistanceMeters;
constexpr float kMaximumSavedAbilityRuntimeSeconds = 86'400.0F;
constexpr float kMaximumSavedBackroomsJackSuspicion = 3.75F;
constexpr float kMaximumSavedBackroomsJackPhaseSeconds = 86'400.0F;
constexpr float kMaximumSavedBackroomsJackLostSightSeconds = 86'400.0F;
constexpr float kMaximumSavedBackroomsJackUnseenDistance = 100'000.0F;
constexpr float kMaximumSavedBackroomsJackSpawnDelay = 3'600.0F;
constexpr float kMaximumHistoricalSavedBackroomsJackCooldown = 360.0F;
constexpr float kMaximumSavedBackroomsJackFootstepDistance = 2.0F;
constexpr float kMaximumSavedBackroomsMarlowCueSeconds = 60.0F;
constexpr float kMaximumSavedBackroomsMarlowManifestationSeconds = 60.0F;
constexpr float kMaximumSavedBackroomsMarlowCooldownSeconds = 24.0F;

static_assert(kChunkSizeX > 0 && kChunkSizeZ > 0);
static_assert(kShipCrewMemberCount == 6U, "Changer le roster v9 exige une nouvelle version de sauvegarde");
static_assert(kOldGuardMemberCount == 6U, "Changer le roster v11 exige une nouvelle version de sauvegarde");
static_assert(sizeof(std::underlying_type_t<ShipCrewRole>) == sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<ShipCrewActivity>) == sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<ShipCrewCargo>) == sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<ShipCrewStation>) == sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<OldGuardAction>) == sizeof(std::uint8_t));
static_assert(
    sizeof(std::underlying_type_t<LegendaryWeaponQuestStage>) ==
    sizeof(std::uint8_t));
static_assert(
    sizeof(std::underlying_type_t<LegendaryWeaponAwakening>) ==
    sizeof(std::uint8_t));
static_assert(
    sizeof(std::underlying_type_t<LegendaryWeaponCosmetic>) ==
    sizeof(std::uint8_t));
static_assert(
    sizeof(std::underlying_type_t<BackroomsJackPhase>) ==
    sizeof(std::uint8_t));
static_assert(
    sizeof(std::underlying_type_t<BackroomsMarlowEncounterMode>) ==
    sizeof(std::uint8_t));
static_assert(sizeof(int) == sizeof(std::int32_t));

constexpr int kMinSafeSavedChunkX =
    (std::numeric_limits<int>::lowest)() / kChunkSizeX + kSavedChunkNeighborMargin;
constexpr int kMaxSafeSavedChunkX =
    ((std::numeric_limits<int>::max)() - (kChunkSizeX - 1)) / kChunkSizeX - kSavedChunkNeighborMargin;
constexpr int kMinSafeSavedChunkZ =
    (std::numeric_limits<int>::lowest)() / kChunkSizeZ + kSavedChunkNeighborMargin;
constexpr int kMaxSafeSavedChunkZ =
    ((std::numeric_limits<int>::max)() - (kChunkSizeZ - 1)) / kChunkSizeZ - kSavedChunkNeighborMargin;

enum class SavedChunkEncoding : std::uint8_t {
    Dense = 0,
    Sparse = 1,
};

[[nodiscard]] auto world_player_placed_mask_empty(
    const WorldPlayerPlacedMask& mask) noexcept -> bool {
    return std::all_of(
        mask.begin(),
        mask.end(),
        [](std::uint8_t value) noexcept {
            return value == 0U;
        });
}

class BinaryWriter;
class BinaryReader;
[[nodiscard]] auto read_expected_array_count(
    BinaryReader& reader,
    std::size_t expected) -> bool;

auto is_supported_save_version(std::uint32_t version) noexcept -> bool {
    return version >= kSaveVersionLegacy && version <= kSaveVersion;
}

auto has_sane_save_metadata_counts(const SaveSlotMetadata& metadata) noexcept -> bool {
    return metadata.modified_chunk_count <= kMaxSavedChunkSnapshotCount;
}

auto generation_profile_for_game_mode(GameMode mode) noexcept -> WorldGenerationProfile {
    switch (mode) {
    case GameMode::SeaAdventure:
        return WorldGenerationProfile::OceanAdventure;
    case GameMode::Backrooms:
        return WorldGenerationProfile::Backrooms;
    case GameMode::ClassicAdventure:
    default:
        return WorldGenerationProfile::Continental;
    }
}

auto is_supported_world_generation_version(WorldGenerationProfile profile,
                                           WorldGenerationVersion version) noexcept -> bool {
    switch (profile) {
    case WorldGenerationProfile::Continental:
        return version == WorldGenerationVersion::LegacyV1;
    case WorldGenerationProfile::OceanAdventure:
        return version == WorldGenerationVersion::LegacyV1 ||
               version == WorldGenerationVersion::SparseArchipelagoV2 ||
               version == WorldGenerationVersion::LivingOceanV3;
    case WorldGenerationProfile::Backrooms:
        return is_backrooms_generation_version(version);
    default:
        return false;
    }
}

auto is_world_generation_version_compatible_with_save(std::uint32_t save_version,
                                                      WorldGenerationProfile profile,
                                                      WorldGenerationVersion generation_version) noexcept -> bool {
    if (!is_supported_world_generation_version(profile, generation_version)) {
        return false;
    }
    if (profile == WorldGenerationProfile::Backrooms) {
        // Le profil et ses identifiants de blocs n'existent qu'à partir de v15.
        // Une valeur BackRooms injectée dans une ancienne entête est rejetée.
        return save_version >= kSaveVersionBackrooms &&
               is_backrooms_generation_version(
                   generation_version);
    }

    // Les formats v8/v9 possédaient déjà le champ, mais seule la géographie
    // historique V1 existait alors. Une valeur V2/V3 dans ces entêtes est donc
    // un payload corrompu et ne doit jamais changer leur monde rétroactivement.
    return save_version >= kSaveVersionSeaDeparture ||
           generation_version == WorldGenerationVersion::LegacyV1;
}

auto is_safe_saved_chunk_coord(const ChunkCoord& coord) noexcept -> bool {
    // Je reserve le rayon de streaming maximal et une couture de mesh autour
    // du chunk afin que les multiplications et les voisins restent dans int.
    return coord.x >= kMinSafeSavedChunkX && coord.x <= kMaxSafeSavedChunkX &&
           coord.z >= kMinSafeSavedChunkZ && coord.z <= kMaxSafeSavedChunkZ;
}

auto is_sane_saved_world_position(const glm::vec3& position) noexcept -> bool {
    // Je borne aussi les valeurs finies: au-dela, la precision float n'est
    // plus suffisante pour une physique voxel fiable, bien avant la limite int.
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
           std::abs(position.x) <= kMaxSavedWorldCoordinateMagnitude &&
           std::abs(position.y) <= kMaxSavedWorldCoordinateMagnitude &&
           std::abs(position.z) <= kMaxSavedWorldCoordinateMagnitude;
}

[[nodiscard]] auto is_finite_in_range(
    float value,
    float minimum,
    float maximum) noexcept -> bool {
    return std::isfinite(value) &&
           value >= minimum &&
           value <= maximum;
}

[[nodiscard]] auto backrooms_jack_phase_has_body_for_save(
    BackroomsJackPhase phase) noexcept -> bool {
    return phase == BackroomsJackPhase::Wandering ||
           phase == BackroomsJackPhase::Watching ||
           phase == BackroomsJackPhase::Chasing ||
           phase == BackroomsJackPhase::Searching ||
           phase == BackroomsJackPhase::Jumpscare;
}

[[nodiscard]] auto is_valid_backrooms_jack_save_state(
    const BackroomsJackState& state) noexcept -> bool {
    const auto phase_value =
        static_cast<std::uint8_t>(state.phase);
    if (phase_value >
            static_cast<std::uint8_t>(
                BackroomsJackPhase::Cooldown) ||
        !is_sane_saved_world_position(state.position) ||
        !is_sane_saved_world_position(
            state.last_seen_player_position) ||
        !is_sane_saved_world_position(
            state.previous_player_position) ||
        !std::isfinite(state.body_yaw_degrees) ||
        state.body_yaw_degrees < -180.0F ||
        state.body_yaw_degrees >= 180.0F ||
        !is_finite_in_range(
            state.head_yaw_degrees,
            -45.0F,
            45.0F) ||
        !is_finite_in_range(
            state.hunch_ratio,
            0.0F,
            1.0F) ||
        !is_finite_in_range(
            state.motion_amount,
            0.0F,
            1.0F) ||
        !is_finite_in_range(
            state.phase_seconds,
            0.0F,
            kMaximumSavedBackroomsJackPhaseSeconds) ||
        !is_finite_in_range(
            state.suspicion,
            0.0F,
            kMaximumSavedBackroomsJackSuspicion) ||
        !is_finite_in_range(
            state.lost_sight_seconds,
            0.0F,
            kMaximumSavedBackroomsJackLostSightSeconds) ||
        !is_finite_in_range(
            state.unseen_travel_distance,
            0.0F,
            kMaximumSavedBackroomsJackUnseenDistance) ||
        !is_finite_in_range(
            state.spawn_check_seconds,
            0.0F,
            kMaximumSavedBackroomsJackSpawnDelay) ||
        !is_finite_in_range(
            state.cooldown_seconds,
            0.0F,
            kBackroomsJackMaximumPersistedCooldownSeconds) ||
        !is_finite_in_range(
            state.footstep_distance,
            0.0F,
            kMaximumSavedBackroomsJackFootstepDistance) ||
        state.evaded_chunk_count >
            state.evaded_chunks.size() ||
        !is_valid_backrooms_logical_level(
            state.logical_level) ||
        state.random_state == 0U ||
        state.next_event_sequence == 0U ||
        state.active !=
            backrooms_jack_phase_has_body_for_save(
                state.phase) ||
        (!state.active && state.motion_amount != 0.0F) ||
        (state.evaded_chunk_count > 0U &&
         !state.has_last_evade_chunk)) {
        return false;
    }

    for (const auto& chunk : state.evaded_chunks) {
        if (!is_safe_saved_chunk_coord(chunk)) {
            return false;
        }
    }
    if (!is_safe_saved_chunk_coord(
            state.last_evade_chunk)) {
        return false;
    }
    for (std::size_t first = 0U;
         first < state.evaded_chunk_count;
         ++first) {
        for (std::size_t second = first + 1U;
             second < state.evaded_chunk_count;
             ++second) {
            if (state.evaded_chunks[first] ==
                state.evaded_chunks[second]) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto is_valid_backrooms_marlow_save_state(
    const BackroomsMarlowState& state) noexcept -> bool {
    const auto mode_value =
        static_cast<std::uint8_t>(state.last_mode);
    return is_finite_in_range(
               state.pressure,
               0.0F,
               kBackroomsMarlowMaximumPressure) &&
           is_finite_in_range(
               state.cue_seconds,
               0.0F,
               kMaximumSavedBackroomsMarlowCueSeconds) &&
           is_finite_in_range(
               state.manifestation_seconds,
               0.0F,
               kMaximumSavedBackroomsMarlowManifestationSeconds) &&
           is_finite_in_range(
               state.cooldown_seconds,
               0.0F,
               kMaximumSavedBackroomsMarlowCooldownSeconds) &&
           is_valid_backrooms_logical_level(
               state.logical_level) &&
           mode_value <= static_cast<std::uint8_t>(
                              BackroomsMarlowEncounterMode::WaterAmbush) &&
           (state.has_last_mode ||
            state.last_mode ==
                BackroomsMarlowEncounterMode::CornerPeek) &&
           state.random_state != 0U &&
           state.next_event_sequence != 0U &&
           state.initialized;
}

void write_generation_profile(BinaryWriter& writer, WorldGenerationProfile profile);
auto read_generation_profile(BinaryReader& reader, WorldGenerationProfile& profile) -> bool;

void validate_snapshot_for_write(const SaveGameSnapshot& snapshot) {
    if (snapshot.creatures.size() > kMaxSavedCreatureCount ||
        snapshot.item_drops.size() > kMaxSavedItemDropCount ||
        snapshot.chunk_snapshots.size() > kMaxSavedChunkSnapshotCount ||
        snapshot.world_save_plan.chunks.size() > kMaxSavedChunkSnapshotCount ||
        (!snapshot.chunk_snapshots.empty() && !snapshot.world_save_plan.chunks.empty())) {
        throw std::runtime_error("Save snapshot exceeds supported payload limits");
    }

    if (!is_sane_saved_world_position(snapshot.spawn_position) ||
        !is_sane_saved_world_position(snapshot.player_state.position)) {
        throw std::runtime_error("Save snapshot contains an invalid world position");
    }
    if (!is_valid_backrooms_logical_level(
            snapshot.backrooms_level) ||
        snapshot.backrooms_jack.logical_level !=
            snapshot.backrooms_level ||
        snapshot.backrooms_marlow.logical_level !=
            snapshot.backrooms_level ||
        (snapshot.metadata.game_mode != GameMode::Backrooms &&
         snapshot.backrooms_level != 0)) {
        throw std::runtime_error(
            "Save snapshot contains an inconsistent Backrooms level");
    }
    if (!is_valid_backrooms_jack_save_state(
            snapshot.backrooms_jack)) {
        throw std::runtime_error(
            "Save snapshot contains an invalid Backrooms Jack state");
    }
    if (!is_valid_backrooms_marlow_save_state(
            snapshot.backrooms_marlow)) {
        throw std::runtime_error(
            "Save snapshot contains an invalid Backrooms Marlow state");
    }
    for (const auto& drop : snapshot.item_drops) {
        auto sanitized_drop = drop;
        if (!sanitize_item_drop_state(sanitized_drop)) {
            throw std::runtime_error("Save snapshot contains an invalid item drop");
        }
    }
    if (!is_valid_legendary_weapon_progression_state(
            snapshot.legendary_weapon)) {
        throw std::runtime_error(
            "Save snapshot contains an invalid legendary weapon state");
    }
    const auto legendary_weapon_count =
        inventory_legendary_weapon_count(
            snapshot.inventory,
            snapshot.hotbar);
    if ((!snapshot.legendary_weapon.weapon_owned &&
         legendary_weapon_count != 0U) ||
        legendary_weapon_count > 1U) {
        throw std::runtime_error(
            "Save snapshot contains inconsistent legendary weapon ownership");
    }

    for (const auto& chunk : snapshot.chunk_snapshots) {
        if (!is_safe_saved_chunk_coord(chunk.coord)) {
            throw std::runtime_error("Save snapshot contains an unsafe chunk coordinate");
        }
    }
    for (const auto& creature : snapshot.creatures) {
        if (!is_safe_saved_chunk_coord(creature.anchor.chunk)) {
            throw std::runtime_error("Save snapshot contains an unsafe creature chunk coordinate");
        }
    }
}

void validate_world_save_plan(const SaveGameSnapshot& snapshot, const WorldSavePlan& plan) {
    if (!snapshot.chunk_snapshots.empty() ||
        plan.chunks.size() > kMaxSavedChunkSnapshotCount ||
        plan.seed != snapshot.metadata.seed) {
        throw std::runtime_error("World save plan is inconsistent with the save snapshot");
    }

    if (plan.generation_profile != WorldGenerationProfile::Continental &&
        plan.generation_profile != WorldGenerationProfile::OceanAdventure &&
        plan.generation_profile != WorldGenerationProfile::Backrooms) {
        throw std::runtime_error("World save plan uses an unsupported generation profile");
    }
    if (plan.generation_profile != generation_profile_for_game_mode(snapshot.metadata.game_mode)) {
        throw std::runtime_error("World save plan profile does not match the selected game mode");
    }
    if (!is_supported_world_generation_version(plan.generation_profile, plan.generation_version)) {
        throw std::runtime_error("World save plan uses an unsupported generation version");
    }
    if (!is_valid_backrooms_logical_level(
            static_cast<std::int32_t>(
                plan.backrooms_level)) ||
        (plan.generation_profile ==
                 WorldGenerationProfile::Backrooms
             ? plan.backrooms_level !=
                   snapshot.backrooms_level
             : plan.backrooms_level != 0 ||
                   snapshot.backrooms_level != 0)) {
        throw std::runtime_error(
            "World save plan uses an inconsistent Backrooms level");
    }

    for (const auto& chunk : plan.chunks) {
        if (!is_safe_saved_chunk_coord(chunk.coord)) {
            throw std::runtime_error("World save plan contains an unsafe chunk coordinate");
        }
        if (chunk.dense()) {
            if (!chunk.sparse_cells.empty() ||
                chunk.dense_blocks.size() != kChunkVolume ||
                chunk.dense_water_state.size() != kChunkVolume) {
                throw std::runtime_error("Dense world save chunk has an invalid payload");
            }
            continue;
        }

        if ((!chunk.dense_blocks.empty() ||
             !chunk.dense_water_state.empty()) ||
            (chunk.sparse_cells.empty() &&
             world_player_placed_mask_empty(
                 chunk.player_placed_mask))) {
            throw std::runtime_error("Sparse world save chunk has an invalid payload");
        }
        auto previous_index = std::optional<std::size_t> {};
        for (const auto& cell : chunk.sparse_cells) {
            const auto index = static_cast<std::size_t>(cell.index);
            if (index >= kChunkVolume || (previous_index.has_value() && index <= *previous_index)) {
                throw std::runtime_error("Sparse world save cells are not strictly ordered");
            }
            previous_index = index;
        }
    }
}

void remove_temp_save_file(const std::filesystem::path& temp_path) noexcept {
    std::error_code error_code;
    std::filesystem::remove(temp_path, error_code);
}

class TempSaveFileCleanup {
public:
    explicit TempSaveFileCleanup(std::filesystem::path path)
        : path_(std::move(path)) {
    }

    ~TempSaveFileCleanup() {
        remove_temp_save_file(path_);
    }

    TempSaveFileCleanup(const TempSaveFileCleanup&) = delete;
    auto operator=(const TempSaveFileCleanup&) -> TempSaveFileCleanup& = delete;

private:
    std::filesystem::path path_ {};
};

void replace_save_file_with_temp(const std::filesystem::path& temp_path, const std::filesystem::path& file_path) {
#if defined(_WIN32)
    // Je remplace le slot final uniquement apres une ecriture complete du .tmp,
    // pour ne pas perdre une sauvegarde valide si la preparation echoue.
    if (MoveFileExW(
            temp_path.wstring().c_str(),
            file_path.wstring().c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        const auto last_error = static_cast<unsigned long>(GetLastError());
        remove_temp_save_file(temp_path);
        throw std::runtime_error(std::string("Unable to finalize save slot data: MoveFileExW failed with error ") +
                                 std::to_string(last_error));
    }
#else
    std::error_code error_code;
    std::filesystem::rename(temp_path, file_path, error_code);
    if (error_code) {
        remove_temp_save_file(temp_path);
        throw std::runtime_error("Unable to finalize save slot data");
    }
#endif
}

class BinaryWriter {
public:
    explicit BinaryWriter(std::ofstream& stream)
        : stream_(stream) {
    }

    template <typename T>
    void write_value(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        stream_.write(reinterpret_cast<const char*>(&value), sizeof(T));
        ok_ = ok_ && stream_.good();
    }

    void write_bytes(const void* data, std::size_t size) {
        stream_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        ok_ = ok_ && stream_.good();
    }

    [[nodiscard]] auto ok() const noexcept -> bool {
        return ok_;
    }

private:
    std::ofstream& stream_;
    bool ok_ = true;
};

class BinaryReader {
public:
    explicit BinaryReader(std::ifstream& stream)
        : stream_(stream) {
    }

    template <typename T>
    auto read_value(T& value) -> bool {
        static_assert(std::is_trivially_copyable_v<T>);
        stream_.read(reinterpret_cast<char*>(&value), sizeof(T));
        return stream_.good();
    }

    auto read_bytes(void* data, std::size_t size) -> bool {
        stream_.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
        return stream_.good();
    }

private:
    std::ifstream& stream_;
};

auto report_load_progress(std::ifstream& input,
                          std::uint64_t total_bytes,
                          SaveLoadPhase phase,
                          const SaveLoadProgressCallback& callback,
                          bool complete = false) -> bool {
    if (!callback) {
        return true;
    }

    auto completed_bytes = std::uint64_t {0};
    if (complete) {
        completed_bytes = total_bytes;
    } else {
        const auto position = input.tellg();
        if (position >= std::streampos {0}) {
            completed_bytes = std::min<std::uint64_t>(
                static_cast<std::uint64_t>(static_cast<std::streamoff>(position)),
                total_bytes);
        }
    }
    const auto normalized = total_bytes == 0U
                                ? (complete ? 1.0F : 0.0F)
                                : std::clamp(
                                      static_cast<float>(completed_bytes) / static_cast<float>(total_bytes),
                                      0.0F,
                                      1.0F);
    return callback({phase, completed_bytes, total_bytes, normalized}) == SaveLoadControl::Continue;
}

template <typename Enum>
void write_enum(BinaryWriter& writer, Enum value) {
    using Underlying = std::underlying_type_t<Enum>;
    writer.write_value(static_cast<Underlying>(value));
}

template <typename Enum>
auto read_enum(BinaryReader& reader, Enum& value) -> bool {
    using Underlying = std::underlying_type_t<Enum>;
    Underlying raw {};
    if (!reader.read_value(raw)) {
        return false;
    }
    value = static_cast<Enum>(raw);
    return true;
}

void write_bool(BinaryWriter& writer, bool value) {
    writer.write_value(static_cast<std::uint8_t>(value ? 1U : 0U));
}

auto read_bool(BinaryReader& reader, bool& value) -> bool {
    std::uint8_t raw = 0;
    if (!reader.read_value(raw)) {
        return false;
    }
    value = raw != 0;
    return true;
}

auto read_strict_bool(BinaryReader& reader, bool& value) -> bool {
    auto raw = std::uint8_t {0U};
    if (!reader.read_value(raw) || raw > 1U) {
        return false;
    }
    value = raw == 1U;
    return true;
}

void write_game_mode(BinaryWriter& writer, GameMode mode) {
    writer.write_value(static_cast<std::uint8_t>(mode));
}

auto read_game_mode(BinaryReader& reader, GameMode& mode) -> bool {
    std::uint8_t raw = 0;
    if (!reader.read_value(raw)) {
        return false;
    }
    mode = static_cast<GameMode>(raw);
    return is_known_game_mode(mode);
}

void write_generation_profile(BinaryWriter& writer, WorldGenerationProfile profile) {
    writer.write_value(static_cast<std::uint8_t>(profile));
}

auto read_generation_profile(BinaryReader& reader, WorldGenerationProfile& profile) -> bool {
    std::uint8_t raw = 0;
    if (!reader.read_value(raw)) {
        return false;
    }
    profile = static_cast<WorldGenerationProfile>(raw);
    return profile == WorldGenerationProfile::Continental ||
           profile == WorldGenerationProfile::OceanAdventure ||
           profile == WorldGenerationProfile::Backrooms;
}

void write_vec3(BinaryWriter& writer, const glm::vec3& value) {
    writer.write_value(value.x);
    writer.write_value(value.y);
    writer.write_value(value.z);
}

auto read_vec3(BinaryReader& reader, glm::vec3& value) -> bool {
    return reader.read_value(value.x) &&
           reader.read_value(value.y) &&
           reader.read_value(value.z);
}

void write_ship_crew_member(BinaryWriter& writer, const ShipCrewMemberSaveState& member) {
    write_vec3(writer, member.local_position);
    writer.write_value(member.yaw_radians);
    writer.write_value(member.animation_time);
    writer.write_value(member.activity_timer);
    writer.write_value(member.work_progress);
    writer.write_value(member.health);
    writer.write_value(member.recovery_timer);
    writer.write_value(member.hurt_timer);
    writer.write_value(member.id);
    writer.write_value(member.routine_step);
    write_enum(writer, member.role);
    write_enum(writer, member.activity);
    write_enum(writer, member.cargo);
    write_enum(writer, member.current_station);
    write_enum(writer, member.next_station);
    write_enum(writer, member.destination_station);
}

auto read_ship_crew_member(BinaryReader& reader, ShipCrewMemberSaveState& member) -> bool {
    return read_vec3(reader, member.local_position) &&
           reader.read_value(member.yaw_radians) &&
           reader.read_value(member.animation_time) &&
           reader.read_value(member.activity_timer) &&
           reader.read_value(member.work_progress) &&
           reader.read_value(member.health) &&
           reader.read_value(member.recovery_timer) &&
           reader.read_value(member.hurt_timer) &&
           reader.read_value(member.id) &&
           reader.read_value(member.routine_step) &&
           read_enum(reader, member.role) &&
           read_enum(reader, member.activity) &&
           read_enum(reader, member.cargo) &&
           read_enum(reader, member.current_station) &&
           read_enum(reader, member.next_station) &&
           read_enum(reader, member.destination_station);
}

void write_ship_crew_state(BinaryWriter& writer, const ShipCrewSaveState& state) {
    write_bool(writer, state.initialized);
    writer.write_value(state.navigation_revision);
    for (const auto& member : state.members) {
        write_ship_crew_member(writer, member);
    }
}

auto read_ship_crew_state(BinaryReader& reader, ShipCrewSaveState& state) -> bool {
    if (!read_bool(reader, state.initialized) ||
        !reader.read_value(state.navigation_revision)) {
        return false;
    }
    for (auto& member : state.members) {
        if (!read_ship_crew_member(reader, member)) {
            return false;
        }
    }
    return true;
}

void write_old_guard_member(BinaryWriter& writer,
                            const OldGuardMemberSaveState& member) {
    write_vec3(writer, member.local_position);
    writer.write_value(member.yaw_radians);
    writer.write_value(member.animation_time);
    writer.write_value(member.action_time);
    writer.write_value(member.reload_remaining);
    writer.write_value(member.bayonet_cooldown);
    writer.write_value(member.id);
    writer.write_value(member.route_index);
    writer.write_value(member.route_step);
    write_enum(writer, member.action);
    write_bool(writer, member.musket_loaded);
}

auto read_old_guard_member(BinaryReader& reader,
                           OldGuardMemberSaveState& member) -> bool {
    return read_vec3(reader, member.local_position) &&
           reader.read_value(member.yaw_radians) &&
           reader.read_value(member.animation_time) &&
           reader.read_value(member.action_time) &&
           reader.read_value(member.reload_remaining) &&
           reader.read_value(member.bayonet_cooldown) &&
           reader.read_value(member.id) &&
           reader.read_value(member.route_index) &&
           reader.read_value(member.route_step) &&
           read_enum(reader, member.action) &&
           read_bool(reader, member.musket_loaded);
}

void write_old_guard_state(BinaryWriter& writer,
                           const OldGuardSaveState& state) {
    write_bool(writer, state.initialized);
    writer.write_value(state.patrol_revision);
    for (const auto& member : state.members) {
        write_old_guard_member(writer, member);
    }
}

auto read_old_guard_state(BinaryReader& reader,
                          OldGuardSaveState& state) -> bool {
    if (!read_bool(reader, state.initialized) ||
        !reader.read_value(state.patrol_revision)) {
        return false;
    }
    for (auto& member : state.members) {
        if (!read_old_guard_member(reader, member)) {
            return false;
        }
    }
    return true;
}

void write_sea_adventure_state(BinaryWriter& writer, const SeaAdventureSaveState& state) {
    const auto sanitized = sanitize_sea_adventure_save_state(state);
    write_bool(writer, sanitized.active);
    write_vec3(writer, sanitized.ship_position);
    writer.write_value(sanitized.route_distance);
    writer.write_value(sanitized.hunger);
    writer.write_value(sanitized.thirst);
    writer.write_value(sanitized.stamina);
    writer.write_value(sanitized.fishing_progress);
    writer.write_value(sanitized.fishing_target_seconds);
    writer.write_value(sanitized.survival_damage_timer);
    writer.write_value(sanitized.stranded_warning_timer);
    writer.write_value(sanitized.food_rations);
    writer.write_value(sanitized.water_flasks);
    writer.write_value(sanitized.fish);
    writer.write_value(sanitized.wood);
    writer.write_value(sanitized.stone);
    writer.write_value(sanitized.fiber);
    writer.write_value(sanitized.stamped_ship_x);
    writer.write_value(sanitized.stamped_ship_z);
    write_bool(writer, sanitized.has_stamped_ship);
    write_bool(writer, sanitized.fishing_active);
    write_ship_crew_state(writer, sanitized.crew);
    write_enum(writer, sanitized.voyage_phase);
    writer.write_value(sanitized.voyage_phase_elapsed);
    // Je place l'extension v11 apres le payload maritime v10 afin que les
    // offsets et lecteurs historiques restent reproductibles.
    write_old_guard_state(writer, sanitized.old_guard);
}

auto read_sea_adventure_state(BinaryReader& reader,
                              SeaAdventureSaveState& state,
                              std::uint32_t version) -> bool {
    SeaAdventureSaveState raw {};
    if (!read_bool(reader, raw.active) ||
        !read_vec3(reader, raw.ship_position) ||
        !reader.read_value(raw.route_distance) ||
        !reader.read_value(raw.hunger) ||
        !reader.read_value(raw.thirst) ||
        !reader.read_value(raw.stamina) ||
        !reader.read_value(raw.fishing_progress) ||
        !reader.read_value(raw.fishing_target_seconds) ||
        !reader.read_value(raw.survival_damage_timer) ||
        !reader.read_value(raw.stranded_warning_timer) ||
        !reader.read_value(raw.food_rations) ||
        !reader.read_value(raw.water_flasks) ||
        !reader.read_value(raw.fish) ||
        !reader.read_value(raw.wood) ||
        !reader.read_value(raw.stone) ||
        !reader.read_value(raw.fiber) ||
        !reader.read_value(raw.stamped_ship_x) ||
        !reader.read_value(raw.stamped_ship_z) ||
        !read_bool(reader, raw.has_stamped_ship) ||
        !read_bool(reader, raw.fishing_active)) {
        return false;
    }

    if (version >= kSaveVersionShipCrew && !read_ship_crew_state(reader, raw.crew)) {
        return false;
    }
    if (version >= kSaveVersionSeaDeparture) {
        if (!read_enum(reader, raw.voyage_phase) ||
            !reader.read_value(raw.voyage_phase_elapsed)) {
            return false;
        }
    } else {
        // Je charge les anciennes parties directement en mer : je ne leur
        // ajoute ni port retroactif, ni nouvelle attente au quai.
        raw.voyage_phase = SeaVoyagePhase::Underway;
        raw.voyage_phase_elapsed = 0.0F;
    }
    if (version >= kSaveVersionOldGuard &&
        !read_old_guard_state(reader, raw.old_guard)) {
        return false;
    }

    // Je normalise aussi a la frontiere binaire pour qu'un payload ancien ou
    // corrompu ne propage jamais de NaN ni de minuterie pathologique. Pour une
    // save v7/v8, la valeur par defaut laisse le systeme recreer le roster.
    state = sanitize_sea_adventure_save_state(raw);
    return true;
}

void write_maritime_experience_state(
    BinaryWriter& writer,
    MaritimeExperienceAwardState state) {
    state =
        sanitize_maritime_experience_award_state(
            state);
    writer.write_value(
        std::min(
            state.navigation_milestones_awarded,
            kMaximumSavedNavigationMilestone));
    writer.write_value(
        state.first_delivery_milestones_mask);
    writer.write_value(
        state.departure_awarded);
    writer.write_value(
        state.open_sea_awarded);
}

auto read_maritime_experience_state(
    BinaryReader& reader,
    MaritimeExperienceAwardState& state) -> bool {
    MaritimeExperienceAwardState raw {};
    if (!reader.read_value(
            raw.navigation_milestones_awarded) ||
        !reader.read_value(
            raw.first_delivery_milestones_mask) ||
        !reader.read_value(
            raw.departure_awarded) ||
        !reader.read_value(
            raw.open_sea_awarded)) {
        return false;
    }
    raw.navigation_milestones_awarded =
        std::min(
            raw.navigation_milestones_awarded,
            kMaximumSavedNavigationMilestone);
    state =
        sanitize_maritime_experience_award_state(
            raw);
    return true;
}

[[nodiscard]] auto derive_legacy_maritime_experience_state(
    const SeaAdventureSaveState& state) noexcept
    -> MaritimeExperienceAwardState {
    if (!state.active) {
        return {};
    }

    MaritimeExperienceAwardState derived {};
    const auto route_distance =
        static_cast<std::uint64_t>(
            std::max(
                state.route_distance,
                0.0F));
    derived.navigation_milestones_awarded =
        std::min(
            ExperienceRewardPolicy::navigation_milestone(
                route_distance),
            kMaximumSavedNavigationMilestone);
    derived.departure_awarded =
        static_cast<std::uint8_t>(
            state.voyage_phase !=
                SeaVoyagePhase::Moored ||
            route_distance > 0ULL);
    derived.open_sea_awarded =
        static_cast<std::uint8_t>(
            state.voyage_phase ==
                SeaVoyagePhase::Underway);

    auto fish_delivery_observed =
        state.fish > 0U;
    auto water_delivery_observed =
        state.water_flasks > 5U;
    for (const auto& member :
         state.crew.members) {
        fish_delivery_observed =
            fish_delivery_observed ||
            (member.role ==
                 ShipCrewRole::Fisher &&
             member.routine_step >= 2U);
        water_delivery_observed =
            water_delivery_observed ||
            (member.role ==
                 ShipCrewRole::WaterTender &&
             member.routine_step >= 2U);
    }
    if (fish_delivery_observed) {
        derived.first_delivery_milestones_mask |=
            std::uint64_t {1ULL};
    }
    if (water_delivery_observed) {
        derived.first_delivery_milestones_mask |=
            std::uint64_t {1ULL} << 1U;
    }
    return derived;
}

void write_status_effect_snapshot_entry(
    BinaryWriter& writer,
    const StatusEffectSnapshotEntry& entry) {
    writer.write_value(entry.target_id);
    writer.write_value(entry.stack_tag);
    write_enum(writer, entry.kind);
    writer.write_value(entry.value);
    writer.write_value(entry.remaining_ticks);
    writer.write_value(entry.sequence);
    write_bool(writer, entry.active);
}

auto read_status_effect_snapshot_entry(
    BinaryReader& reader,
    StatusEffectSnapshotEntry& entry) -> bool {
    return reader.read_value(entry.target_id) &&
           reader.read_value(entry.stack_tag) &&
           read_enum(reader, entry.kind) &&
           reader.read_value(entry.value) &&
           reader.read_value(entry.remaining_ticks) &&
           reader.read_value(entry.sequence) &&
           read_bool(reader, entry.active);
}

void write_status_effect_system_snapshot(
    BinaryWriter& writer,
    const StatusEffectSystemSnapshot& state) {
    writer.write_value(
        static_cast<std::uint32_t>(
            state.entries.size()));
    for (const auto& entry : state.entries) {
        write_status_effect_snapshot_entry(
            writer,
            entry);
    }
    writer.write_value(
        state.fractional_tick_accumulator);
    writer.write_value(
        state.next_sequence);
}

auto read_status_effect_system_snapshot(
    BinaryReader& reader,
    StatusEffectSystemSnapshot& state) -> bool {
    if (!read_expected_array_count(
            reader,
            state.entries.size())) {
        return false;
    }
    for (auto& entry : state.entries) {
        if (!read_status_effect_snapshot_entry(
                reader,
                entry)) {
            return false;
        }
    }
    return reader.read_value(
               state.fractional_tick_accumulator) &&
           reader.read_value(
               state.next_sequence);
}

void write_player_ability_effects_snapshot(
    BinaryWriter& writer,
    const PlayerAbilityEffectsSnapshot& state) {
    write_status_effect_system_snapshot(
        writer,
        state.status_effects);
    writer.write_value(
        state.iron_guard_cast_sequence);
    writer.write_value(
        state.iron_guard_wave_damage);
    writer.write_value(
        state.iron_guard_wave_radius);
    writer.write_value(
        state.iron_guard_energy_refund);
}

auto read_player_ability_effects_snapshot(
    BinaryReader& reader,
    PlayerAbilityEffectsSnapshot& state) -> bool {
    return read_status_effect_system_snapshot(
               reader,
               state.status_effects) &&
           reader.read_value(
               state.iron_guard_cast_sequence) &&
           reader.read_value(
               state.iron_guard_wave_damage) &&
           reader.read_value(
               state.iron_guard_wave_radius) &&
           reader.read_value(
               state.iron_guard_energy_refund);
}

void write_summoned_unit_stats(
    BinaryWriter& writer,
    const SummonedUnitStats& stats) {
    writer.write_value(stats.duration_seconds);
    writer.write_value(stats.maximum_health);
    writer.write_value(stats.attack_damage);
    writer.write_value(
        stats.attack_interval_seconds);
    write_bool(
        writer,
        stats.has_light_taunt);
    write_bool(
        writer,
        stats.has_projectile_block);
    writer.write_value(
        stats.taunt_interval_seconds);
    writer.write_value(stats.taunt_radius);
    writer.write_value(
        stats.projectile_block_interval_seconds);
    writer.write_value(
        stats.mastery_survival_health);
    writer.write_value(
        stats.mastery_damage_reduction);
    writer.write_value(
        stats.mastery_damage_reduction_seconds);
}

auto read_summoned_unit_stats(
    BinaryReader& reader,
    SummonedUnitStats& stats) -> bool {
    return reader.read_value(
               stats.duration_seconds) &&
           reader.read_value(
               stats.maximum_health) &&
           reader.read_value(
               stats.attack_damage) &&
           reader.read_value(
               stats.attack_interval_seconds) &&
           read_bool(
               reader,
               stats.has_light_taunt) &&
           read_bool(
               reader,
               stats.has_projectile_block) &&
           reader.read_value(
               stats.taunt_interval_seconds) &&
           reader.read_value(
               stats.taunt_radius) &&
           reader.read_value(
               stats.projectile_block_interval_seconds) &&
           reader.read_value(
               stats.mastery_survival_health) &&
           reader.read_value(
               stats.mastery_damage_reduction) &&
           reader.read_value(
               stats.mastery_damage_reduction_seconds);
}

void write_summoned_unit_snapshot(
    BinaryWriter& writer,
    const SummonedUnitSystemSnapshot& state) {
    write_bool(writer, state.active);
    writer.write_value(state.unit_id);
    writer.write_value(state.owner_id);
    writer.write_value(state.cast_sequence);
    write_vec3(writer, state.position);
    write_enum(writer, state.rank);
    write_summoned_unit_stats(
        writer,
        state.stats);
    writer.write_value(state.age_seconds);
    writer.write_value(
        state.next_attack_seconds);
    writer.write_value(
        state.next_taunt_seconds);
    writer.write_value(state.health);
    writer.write_value(
        state.projectile_block_cooldown);
    writer.write_value(
        state.mastery_damage_reduction_seconds);
    writer.write_value(state.yaw_radians);
    writer.write_value(state.animation_time);
    writer.write_value(
        state.last_attack_event_seconds);
    writer.write_value(
        state.last_taunt_event_seconds);
    write_bool(writer, state.mastered);
    write_bool(
        writer,
        state.death_refusal_used);
    write_bool(
        writer,
        state.pending_mastery_taunt);
}

auto read_summoned_unit_snapshot(
    BinaryReader& reader,
    SummonedUnitSystemSnapshot& state) -> bool {
    return read_bool(reader, state.active) &&
           reader.read_value(state.unit_id) &&
           reader.read_value(state.owner_id) &&
           reader.read_value(state.cast_sequence) &&
           read_vec3(reader, state.position) &&
           read_enum(reader, state.rank) &&
           read_summoned_unit_stats(
               reader,
               state.stats) &&
           reader.read_value(state.age_seconds) &&
           reader.read_value(
               state.next_attack_seconds) &&
           reader.read_value(
               state.next_taunt_seconds) &&
           reader.read_value(state.health) &&
           reader.read_value(
               state.projectile_block_cooldown) &&
           reader.read_value(
               state.mastery_damage_reduction_seconds) &&
           reader.read_value(state.yaw_radians) &&
           reader.read_value(state.animation_time) &&
           reader.read_value(
               state.last_attack_event_seconds) &&
           reader.read_value(
               state.last_taunt_event_seconds) &&
           read_bool(reader, state.mastered) &&
           read_bool(
               reader,
               state.death_refusal_used) &&
           read_bool(
               reader,
               state.pending_mastery_taunt);
}

void write_player_ability_runtime_state(
    BinaryWriter& writer,
    const PlayerAbilityRuntimeSaveState& requested) {
    const auto state =
        sanitize_player_ability_runtime_save_state(
            requested);
    write_player_ability_effects_snapshot(
        writer,
        state.player_effects);
    writer.write_value(
        state.wind.remaining_seconds);
    writer.write_value(
        state.wind.movement_bonus);
    writer.write_value(
        state.wind.recovery_bonus);
    writer.write_value(
        state.wind.dodge_remaining_seconds);
    write_bool(
        writer,
        state.wind.blade_armed);
    writer.write_value(
        state.wind.cast_sequence);

    writer.write_value(
        static_cast<std::uint32_t>(
            state.summoned_footmen.size()));
    for (const auto& footman :
         state.summoned_footmen) {
        write_summoned_unit_snapshot(
            writer,
            footman.runtime);
        write_bool(
            writer,
            footman.ship_local_position
                .has_value());
        if (footman.ship_local_position
                .has_value()) {
            write_vec3(
                writer,
                *footman.ship_local_position);
        }
        writer.write_value(
            footman.far_seconds);
        writer.write_value(
            footman.cast_sequence);
    }
    writer.write_value(
        state.next_summoned_unit_id);
    writer.write_value(
        state.next_cast_sequence);
}

auto read_player_ability_runtime_state(
    BinaryReader& reader,
    PlayerAbilityRuntimeSaveState& state) -> bool {
    PlayerAbilityRuntimeSaveState raw {};
    if (!read_player_ability_effects_snapshot(
            reader,
            raw.player_effects) ||
        !reader.read_value(
            raw.wind.remaining_seconds) ||
        !reader.read_value(
            raw.wind.movement_bonus) ||
        !reader.read_value(
            raw.wind.recovery_bonus) ||
        !reader.read_value(
            raw.wind.dodge_remaining_seconds) ||
        !read_bool(
            reader,
            raw.wind.blade_armed) ||
        !reader.read_value(
            raw.wind.cast_sequence) ||
        !read_expected_array_count(
            reader,
            raw.summoned_footmen.size())) {
        return false;
    }

    for (auto& footman :
         raw.summoned_footmen) {
        auto has_ship_local_position = false;
        if (!read_summoned_unit_snapshot(
                reader,
                footman.runtime) ||
            !read_bool(
                reader,
                has_ship_local_position)) {
            return false;
        }
        if (has_ship_local_position) {
            glm::vec3 local_position {0.0F};
            if (!read_vec3(
                    reader,
                    local_position)) {
                return false;
            }
            footman.ship_local_position =
                local_position;
        }
        if (!reader.read_value(
                footman.far_seconds) ||
            !reader.read_value(
                footman.cast_sequence)) {
            return false;
        }
    }
    if (!reader.read_value(
            raw.next_summoned_unit_id) ||
        !reader.read_value(
            raw.next_cast_sequence)) {
        return false;
    }
    state =
        sanitize_player_ability_runtime_save_state(
            raw);
    return true;
}

void write_hotbar_slot(BinaryWriter& writer, const HotbarSlot& slot) {
    writer.write_value(slot.block_id);
    writer.write_value(slot.count);
}

auto read_hotbar_slot(BinaryReader& reader, HotbarSlot& slot) -> bool {
    return reader.read_value(slot.block_id) &&
           reader.read_value(slot.count);
}

void write_item_instance_state(BinaryWriter& writer,
                               const HotbarSlot& slot) {
    writer.write_value(
        sanitized_item_instance_state(
            slot.block_id,
            slot.instance_state));
}

auto read_item_instance_state(BinaryReader& reader,
                              HotbarSlot& slot) -> bool {
    return reader.read_value(slot.instance_state);
}

void write_item_instance_state_extension(
    BinaryWriter& writer,
    const SaveGameSnapshot& snapshot) {
    writer.write_bytes(
        kItemInstanceStateMagic.data(),
        kItemInstanceStateMagic.size());
    for (const auto& slot : snapshot.hotbar.slots) {
        write_item_instance_state(writer, slot);
    }
    for (const auto& slot : snapshot.inventory.storage_slots) {
        write_item_instance_state(writer, slot);
    }
    write_item_instance_state(
        writer,
        snapshot.inventory.carried_slot);
    for (const auto& slot : snapshot.inventory.equipment_slots) {
        write_item_instance_state(writer, slot);
    }
    for (const auto& drop : snapshot.item_drops) {
        write_item_instance_state(writer, drop.stack);
    }
    writer.write_value(
        snapshot.musket_shot_sequence);
}

auto read_item_instance_state_extension(
    BinaryReader& reader,
    SaveGameSnapshot& snapshot) -> bool {
    auto magic =
        std::array<char, kItemInstanceStateMagic.size()> {};
    if (!reader.read_bytes(magic.data(), magic.size()) ||
        magic != kItemInstanceStateMagic) {
        return false;
    }
    for (auto& slot : snapshot.hotbar.slots) {
        if (!read_item_instance_state(reader, slot)) {
            return false;
        }
    }
    for (auto& slot : snapshot.inventory.storage_slots) {
        if (!read_item_instance_state(reader, slot)) {
            return false;
        }
    }
    if (!read_item_instance_state(
            reader,
            snapshot.inventory.carried_slot)) {
        return false;
    }
    for (auto& slot : snapshot.inventory.equipment_slots) {
        if (!read_item_instance_state(reader, slot)) {
            return false;
        }
    }
    for (auto& drop : snapshot.item_drops) {
        if (!read_item_instance_state(reader, drop.stack)) {
            return false;
        }
    }
    return reader.read_value(
        snapshot.musket_shot_sequence);
}

void write_legendary_weapon_state_extension(
    BinaryWriter& writer,
    const LegendaryWeaponProgressionState& state) {
    writer.write_bytes(
        kLegendaryWeaponStateMagic.data(),
        kLegendaryWeaponStateMagic.size());
    writer.write_value(
        kLegendaryWeaponStateFormatVersion);
    writer.write_value(state.unique_weapon_id);
    write_enum(writer, state.quest_stage);
    write_enum(writer, state.awakening);
    writer.write_value(
        state.map_fragments_collected);
    writer.write_value(state.corrupted_kills);
    writer.write_value(state.upgrade_flags);
    write_enum(writer, state.cosmetic);
    write_bool(writer, state.weapon_owned);
    write_bool(
        writer,
        state.astral_boss_defeated);
    write_bool(
        writer,
        state.major_boss_defeated);
    write_bool(
        writer,
        state.forge_ritual_complete);
}

auto read_legendary_weapon_state_extension(
    BinaryReader& reader,
    LegendaryWeaponProgressionState& state) -> bool {
    auto magic =
        std::array<
            char,
            kLegendaryWeaponStateMagic.size()> {};
    auto format_version = std::uint8_t {0U};
    LegendaryWeaponProgressionState raw {};
    if (!reader.read_bytes(
            magic.data(),
            magic.size()) ||
        magic != kLegendaryWeaponStateMagic ||
        !reader.read_value(format_version) ||
        format_version !=
            kLegendaryWeaponStateFormatVersion ||
        !reader.read_value(raw.unique_weapon_id) ||
        !read_enum(reader, raw.quest_stage) ||
        !read_enum(reader, raw.awakening) ||
        !reader.read_value(
            raw.map_fragments_collected) ||
        !reader.read_value(raw.corrupted_kills) ||
        !reader.read_value(raw.upgrade_flags) ||
        !read_enum(reader, raw.cosmetic) ||
        !read_strict_bool(
            reader,
            raw.weapon_owned) ||
        !read_strict_bool(
            reader,
            raw.astral_boss_defeated) ||
        !read_strict_bool(
            reader,
            raw.major_boss_defeated) ||
        !read_strict_bool(
            reader,
            raw.forge_ritual_complete) ||
        !is_valid_legendary_weapon_progression_state(
            raw)) {
        return false;
    }
    state = raw;
    return true;
}

void write_backrooms_flashlight_state_extension(
    BinaryWriter& writer,
    const BackroomsFlashlightState& state) {
    const auto sanitized =
        sanitize_backrooms_flashlight_state(state);
    writer.write_bytes(
        kBackroomsFlashlightStateMagic.data(),
        kBackroomsFlashlightStateMagic.size());
    writer.write_value(
        kBackroomsFlashlightStateFormatVersion);
    writer.write_value(
        sanitized.battery_charge);
    write_bool(
        writer,
        sanitized.enabled);
}

auto read_backrooms_flashlight_state_extension(
    BinaryReader& reader,
    BackroomsFlashlightState& state) -> bool {
    auto magic =
        std::array<
            char,
            kBackroomsFlashlightStateMagic.size()> {};
    auto format_version = std::uint8_t {0U};
    BackroomsFlashlightState raw {};
    if (!reader.read_bytes(
            magic.data(),
            magic.size()) ||
        magic != kBackroomsFlashlightStateMagic ||
        !reader.read_value(format_version) ||
        format_version !=
            kBackroomsFlashlightStateFormatVersion ||
        !reader.read_value(raw.battery_charge) ||
        !read_strict_bool(
            reader,
            raw.enabled) ||
        !std::isfinite(raw.battery_charge) ||
        raw.battery_charge < 0.0F ||
        raw.battery_charge > 1.0F ||
        (raw.enabled &&
         raw.battery_charge <= 0.0F)) {
        return false;
    }
    state =
        sanitize_backrooms_flashlight_state(raw);
    return true;
}

void write_backrooms_jack_state_extension(
    BinaryWriter& writer,
    const BackroomsJackState& state) {
    // Je fixe BJCK v1 à 146 octets : magic/version/phase, trois vec3, onze
    // floats, quatre ChunkCoord, compteur u8, dernier chunk, RNG u32,
    // séquence u64 puis sept booléens stricts. Aucun chemin A* ni runtime
    // dépendant du streaming n'entre dans ce payload.
    writer.write_bytes(
        kBackroomsJackStateMagic.data(),
        kBackroomsJackStateMagic.size());
    writer.write_value(
        kBackroomsJackStateFormatVersion);
    write_enum(writer, state.phase);
    write_vec3(writer, state.position);
    write_vec3(
        writer,
        state.last_seen_player_position);
    write_vec3(
        writer,
        state.previous_player_position);
    writer.write_value(state.body_yaw_degrees);
    writer.write_value(state.head_yaw_degrees);
    writer.write_value(state.hunch_ratio);
    writer.write_value(state.motion_amount);
    writer.write_value(state.phase_seconds);
    writer.write_value(state.suspicion);
    writer.write_value(state.lost_sight_seconds);
    writer.write_value(
        state.unseen_travel_distance);
    writer.write_value(state.spawn_check_seconds);
    writer.write_value(state.cooldown_seconds);
    writer.write_value(state.footstep_distance);
    for (const auto& chunk : state.evaded_chunks) {
        writer.write_value(
            static_cast<std::int32_t>(chunk.x));
        writer.write_value(
            static_cast<std::int32_t>(chunk.z));
    }
    writer.write_value(
        static_cast<std::uint8_t>(
            state.evaded_chunk_count));
    writer.write_value(
        static_cast<std::int32_t>(
            state.last_evade_chunk.x));
    writer.write_value(
        static_cast<std::int32_t>(
            state.last_evade_chunk.z));
    writer.write_value(state.random_state);
    writer.write_value(state.next_event_sequence);
    write_bool(writer, state.active);
    write_bool(
        writer,
        state.has_previous_player_position);
    write_bool(
        writer,
        state.has_last_evade_chunk);
    write_bool(
        writer,
        state.next_step_is_wooden);
    write_bool(
        writer,
        state.notice_event_emitted);
    write_bool(
        writer,
        state.chase_event_emitted);
    write_bool(
        writer,
        state.screamer_event_emitted);
}

auto read_backrooms_jack_state_extension(
    BinaryReader& reader,
    BackroomsJackState& state) -> bool {
    auto magic =
        std::array<
            char,
            kBackroomsJackStateMagic.size()> {};
    auto format_version = std::uint8_t {0U};
    auto raw_count = std::uint8_t {0U};
    BackroomsJackState raw {};
    if (!reader.read_bytes(
            magic.data(),
            magic.size()) ||
        magic != kBackroomsJackStateMagic ||
        !reader.read_value(format_version) ||
        format_version !=
            kBackroomsJackStateFormatVersion ||
        !read_enum(reader, raw.phase) ||
        !read_vec3(reader, raw.position) ||
        !read_vec3(
            reader,
            raw.last_seen_player_position) ||
        !read_vec3(
            reader,
            raw.previous_player_position) ||
        !reader.read_value(raw.body_yaw_degrees) ||
        !reader.read_value(raw.head_yaw_degrees) ||
        !reader.read_value(raw.hunch_ratio) ||
        !reader.read_value(raw.motion_amount) ||
        !reader.read_value(raw.phase_seconds) ||
        !reader.read_value(raw.suspicion) ||
        !reader.read_value(raw.lost_sight_seconds) ||
        !reader.read_value(
            raw.unseen_travel_distance) ||
        !reader.read_value(raw.spawn_check_seconds) ||
        !reader.read_value(raw.cooldown_seconds) ||
        !reader.read_value(raw.footstep_distance)) {
        return false;
    }
    for (auto& chunk : raw.evaded_chunks) {
        auto chunk_x = std::int32_t {0};
        auto chunk_z = std::int32_t {0};
        if (!reader.read_value(chunk_x) ||
            !reader.read_value(chunk_z)) {
            return false;
        }
        chunk = {
            static_cast<int>(chunk_x),
            static_cast<int>(chunk_z),
        };
    }

    auto last_chunk_x = std::int32_t {0};
    auto last_chunk_z = std::int32_t {0};
    if (!reader.read_value(raw_count) ||
        !reader.read_value(last_chunk_x) ||
        !reader.read_value(last_chunk_z) ||
        !reader.read_value(raw.random_state) ||
        !reader.read_value(
            raw.next_event_sequence) ||
        !read_strict_bool(reader, raw.active) ||
        !read_strict_bool(
            reader,
            raw.has_previous_player_position) ||
        !read_strict_bool(
            reader,
            raw.has_last_evade_chunk) ||
        !read_strict_bool(
            reader,
            raw.next_step_is_wooden) ||
        !read_strict_bool(
            reader,
            raw.notice_event_emitted) ||
        !read_strict_bool(
            reader,
            raw.chase_event_emitted) ||
        !read_strict_bool(
            reader,
            raw.screamer_event_emitted)) {
        return false;
    }
    raw.evaded_chunk_count =
        static_cast<std::size_t>(raw_count);
    raw.last_evade_chunk = {
        static_cast<int>(last_chunk_x),
        static_cast<int>(last_chunk_z),
    };

    // Je reconnais les deux compteurs longs du directeur historique avant de
    // les borner au nouveau rythme. Je garde toutes les autres validations
    // strictes afin qu'une sauvegarde corrompue ne soit jamais reparée en
    // silence par le sanitiseur general.
    if (!is_finite_in_range(
            raw.spawn_check_seconds,
            0.0F,
            kMaximumSavedBackroomsJackSpawnDelay) ||
        !is_finite_in_range(
            raw.cooldown_seconds,
            0.0F,
            kMaximumHistoricalSavedBackroomsJackCooldown)) {
        return false;
    }
    raw.spawn_check_seconds = std::min(
        raw.spawn_check_seconds,
        kBackroomsJackMaximumPersistedSpawnDelaySeconds);
    raw.cooldown_seconds = std::min(
        raw.cooldown_seconds,
        kBackroomsJackMaximumPersistedCooldownSeconds);
    if (!is_valid_backrooms_jack_save_state(raw)) {
        return false;
    }
    state = raw;
    return true;
}

void write_backrooms_level_state_extension(
    BinaryWriter& writer,
    std::int32_t logical_level) {
    // Je garde le niveau dans une extension terminale autonome : le corps
    // historique et le contrat BJCK v1 restent ainsi strictement inchangés.
    writer.write_bytes(
        kBackroomsLevelStateMagic.data(),
        kBackroomsLevelStateMagic.size());
    writer.write_value(
        kBackroomsLevelStateFormatVersion);
    writer.write_value(logical_level);
}

auto read_backrooms_level_state_extension(
    BinaryReader& reader,
    std::int32_t& logical_level) -> bool {
    auto magic =
        std::array<
            char,
            kBackroomsLevelStateMagic.size()> {};
    auto format_version = std::uint8_t {0U};
    auto raw_level = std::int32_t {0};
    if (!reader.read_bytes(
            magic.data(),
            magic.size()) ||
        magic != kBackroomsLevelStateMagic ||
        !reader.read_value(format_version) ||
        format_version !=
            kBackroomsLevelStateFormatVersion ||
        !reader.read_value(raw_level) ||
        !is_valid_backrooms_logical_level(
            raw_level)) {
        return false;
    }
    logical_level = raw_level;
    return true;
}

void write_backrooms_marlow_state_extension(
    BinaryWriter& writer,
    const BackroomsMarlowState& state) {
    // Je fixe MRLW v1 a 40 octets et je n'y place que le directeur durable.
    // Le corps, le chemin, les cellules et la noyade en cours restent runtime.
    const BackroomsMarlowRuntime dormant_runtime {};
    const auto persistent =
        prepare_backrooms_marlow_for_persistence(
            state,
            dormant_runtime);
    writer.write_bytes(
        kBackroomsMarlowStateMagic.data(),
        kBackroomsMarlowStateMagic.size());
    writer.write_value(
        kBackroomsMarlowStateFormatVersion);
    writer.write_value(persistent.pressure);
    writer.write_value(persistent.cue_seconds);
    writer.write_value(
        persistent.manifestation_seconds);
    writer.write_value(persistent.cooldown_seconds);
    writer.write_value(persistent.logical_level);
    write_enum(writer, persistent.last_mode);
    writer.write_value(persistent.random_state);
    writer.write_value(
        persistent.next_event_sequence);
    write_bool(writer, persistent.has_last_mode);
    write_bool(writer, persistent.initialized);
}

auto read_backrooms_marlow_state_extension(
    BinaryReader& reader,
    BackroomsMarlowState& state) -> bool {
    auto magic = std::array<
        char,
        kBackroomsMarlowStateMagic.size()> {};
    auto format_version = std::uint8_t {0U};
    BackroomsMarlowState raw {};
    if (!reader.read_bytes(
            magic.data(),
            magic.size()) ||
        magic != kBackroomsMarlowStateMagic ||
        !reader.read_value(format_version) ||
        format_version !=
            kBackroomsMarlowStateFormatVersion ||
        !reader.read_value(raw.pressure) ||
        !reader.read_value(raw.cue_seconds) ||
        !reader.read_value(
            raw.manifestation_seconds) ||
        !reader.read_value(raw.cooldown_seconds) ||
        !reader.read_value(raw.logical_level) ||
        !read_enum(reader, raw.last_mode) ||
        !reader.read_value(raw.random_state) ||
        !reader.read_value(
            raw.next_event_sequence) ||
        !read_strict_bool(
            reader,
            raw.has_last_mode) ||
        !read_strict_bool(
            reader,
            raw.initialized) ||
        !is_valid_backrooms_marlow_save_state(raw)) {
        return false;
    }
    state = raw;
    return true;
}

void write_player_state(BinaryWriter& writer, const PlayerState& state) {
    write_vec3(writer, state.position);
    write_vec3(writer, state.velocity);
    writer.write_value(state.yaw_degrees);
    writer.write_value(state.pitch_degrees);
    writer.write_value(state.body_yaw_degrees);
    writer.write_value(state.animation_time);
    writer.write_value(state.step_phase);
    writer.write_value(state.health);
    writer.write_value(state.air_seconds);
    writer.write_value(state.hurt_timer);
    writer.write_value(state.damage_cooldown);
    writer.write_value(state.regen_delay);
    writer.write_value(state.regen_tick_timer);
    writer.write_value(state.drowning_tick_timer);
    writer.write_value(state.fall_start_y);
    writer.write_value(state.primary_action_progress);
    writer.write_value(state.secondary_action_progress);
    writer.write_value(state.landing_impact);
    writer.write_value(state.airborne_time);
    writer.write_value(state.look_sway_yaw);
    writer.write_value(state.look_sway_pitch);
    write_bool(writer, state.on_ground);
    write_bool(writer, state.fly_mode);
    write_bool(writer, state.head_underwater);
    write_bool(writer, state.swimming);
    write_bool(writer, state.primary_action_active);
    write_bool(writer, state.secondary_action_active);
    write_bool(writer, state.dead);
    write_enum(writer, state.death_cause);
}

auto read_player_state(BinaryReader& reader, PlayerState& state) -> bool {
    return read_vec3(reader, state.position) &&
           read_vec3(reader, state.velocity) &&
           reader.read_value(state.yaw_degrees) &&
           reader.read_value(state.pitch_degrees) &&
           reader.read_value(state.body_yaw_degrees) &&
           reader.read_value(state.animation_time) &&
           reader.read_value(state.step_phase) &&
           reader.read_value(state.health) &&
           reader.read_value(state.air_seconds) &&
           reader.read_value(state.hurt_timer) &&
           reader.read_value(state.damage_cooldown) &&
           reader.read_value(state.regen_delay) &&
           reader.read_value(state.regen_tick_timer) &&
           reader.read_value(state.drowning_tick_timer) &&
           reader.read_value(state.fall_start_y) &&
           reader.read_value(state.primary_action_progress) &&
           reader.read_value(state.secondary_action_progress) &&
           reader.read_value(state.landing_impact) &&
           reader.read_value(state.airborne_time) &&
           reader.read_value(state.look_sway_yaw) &&
           reader.read_value(state.look_sway_pitch) &&
           read_bool(reader, state.on_ground) &&
           read_bool(reader, state.fly_mode) &&
           read_bool(reader, state.head_underwater) &&
           read_bool(reader, state.swimming) &&
           read_bool(reader, state.primary_action_active) &&
           read_bool(reader, state.secondary_action_active) &&
           read_bool(reader, state.dead) &&
           read_enum(reader, state.death_cause);
}

void write_player_progression(BinaryWriter& writer, const PlayerProgressionState& progression) {
    const auto normalized = sanitize_player_progression_state(progression);
    writer.write_value(normalized.level);
    writer.write_value(normalized.experience);
}

[[nodiscard]] auto migrate_v13_player_progression_state(
    PlayerProgressionState state) noexcept
    -> PlayerProgressionState {
    // Je conserve exactement le niveau et le ratio de la barre v13 avant
    // d'appliquer la courbe v14, sans transformer un excédent corrompu en gain.
    state.level = normalize_player_progression_level(
        state.level);
    if (state.level >= kPlayerProgressionMaxLevel) {
        state.experience = 0ULL;
        return state;
    }

    const auto old_threshold =
        player_experience_for_next_level_v13(
            state.level);
    const auto new_threshold =
        player_experience_for_next_level(
            state.level);
    if (old_threshold == 0ULL ||
        new_threshold == 0ULL) {
        state.experience = 0ULL;
        return state;
    }

    const auto bounded_experience =
        std::min(
            state.experience,
            old_threshold - 1ULL);
    const auto ratio =
        static_cast<long double>(
            bounded_experience) /
        static_cast<long double>(
            old_threshold);
    const auto migrated_experience =
        static_cast<std::uint64_t>(
            std::floor(
                ratio *
                    static_cast<long double>(
                        new_threshold) +
                0.5L));
    state.experience =
        std::min(
            migrated_experience,
            new_threshold - 1ULL);
    return state;
}

auto read_player_progression(
    BinaryReader& reader,
    PlayerProgressionState& progression,
    std::uint32_t save_version) -> bool {
    PlayerProgressionState raw {};
    if (!reader.read_value(raw.level) ||
        !reader.read_value(raw.experience)) {
        return false;
    }
    if (save_version <
        kSaveVersionProgressionBuilds) {
        progression =
            migrate_legacy_player_progression_state(
                raw);
    } else if (save_version <
               kSaveVersionRuntimeState) {
        progression =
            migrate_v13_player_progression_state(
                raw);
    } else {
        progression =
            sanitize_player_progression_state(
                raw);
    }
    return true;
}

void write_player_build_state(
    BinaryWriter& writer,
    const PlayerBuildState& build,
    std::uint32_t level) {
    auto normalized = build;
    sanitize_player_build_state(
        normalized,
        level);

    writer.write_value(
        static_cast<std::uint32_t>(
            normalized.attributes.values.size()));
    for (const auto value :
         normalized.attributes.values) {
        writer.write_value(value);
    }
    writer.write_value(
        static_cast<std::uint32_t>(
            normalized.ability_ranks.size()));
    for (const auto rank :
         normalized.ability_ranks) {
        writer.write_value(rank);
    }
    writer.write_value(
        static_cast<std::uint32_t>(
            normalized.ability_masteries.size()));
    for (const auto mastery :
         normalized.ability_masteries) {
        writer.write_value(mastery);
    }
    writer.write_value(
        static_cast<std::uint32_t>(
            normalized.equipped_abilities.size()));
    for (const auto ability :
         normalized.equipped_abilities) {
        write_enum(
            writer,
            ability);
    }

    writer.write_value(
        normalized.val_energy);
    writer.write_value(
        normalized.global_cooldown_remaining);
    writer.write_value(
        normalized
            .energy_regeneration_delay_remaining);
    writer.write_value(
        static_cast<std::uint32_t>(
            normalized.cooldowns_remaining.size()));
    for (const auto cooldown :
         normalized.cooldowns_remaining) {
        writer.write_value(cooldown);
    }
    writer.write_value(
        static_cast<std::uint32_t>(
            normalized.charges.size()));
    for (const auto charges :
         normalized.charges) {
        writer.write_value(charges);
    }

    writer.write_value(
        static_cast<std::uint32_t>(
            normalized.construction_plans.size()));
    for (const auto& plan :
         normalized.construction_plans) {
        writer.write_value(plan.cell_count);
        write_enum(
            writer,
            plan.shape);
        write_bool(
            writer,
            plan.mirrored);
        writer.write_value(
            static_cast<std::uint32_t>(
                plan.cells.size()));
        for (const auto& cell : plan.cells) {
            writer.write_value(cell.x);
            writer.write_value(cell.y);
            writer.write_value(cell.z);
            writer.write_value(
                cell.material_id);
        }
    }
    writer.write_value(
        normalized.selected_construction_plan);
    writer.write_value(
        normalized.successful_cast_sequence);
    write_enum(
        writer,
        normalized.last_dominant_path);
    writer.write_value(
        normalized.revision);
}

[[nodiscard]] auto read_expected_array_count(
    BinaryReader& reader,
    std::size_t expected) -> bool {
    auto count = std::uint32_t {0U};
    return reader.read_value(count) &&
           count ==
               static_cast<std::uint32_t>(
                   expected);
}

auto read_player_build_state(
    BinaryReader& reader,
    PlayerBuildState& build,
    std::uint32_t level,
    std::uint32_t save_version) -> bool {
    PlayerBuildState raw {};
    const auto counted_arrays =
        save_version >=
        kSaveVersionCountedBuildArrays;
    if (counted_arrays &&
        !read_expected_array_count(
            reader,
            raw.attributes.values.size())) {
        return false;
    }
    for (auto& value : raw.attributes.values) {
        if (!reader.read_value(value)) {
            return false;
        }
    }
    if (counted_arrays &&
        !read_expected_array_count(
            reader,
            raw.ability_ranks.size())) {
        return false;
    }
    for (auto& rank : raw.ability_ranks) {
        if (!reader.read_value(rank)) {
            return false;
        }
    }
    if (counted_arrays &&
        !read_expected_array_count(
            reader,
            raw.ability_masteries.size())) {
        return false;
    }
    for (auto& mastery :
         raw.ability_masteries) {
        if (!reader.read_value(mastery)) {
            return false;
        }
    }
    if (counted_arrays &&
        !read_expected_array_count(
            reader,
            raw.equipped_abilities.size())) {
        return false;
    }
    for (auto& ability :
         raw.equipped_abilities) {
        if (!read_enum(
                reader,
                ability)) {
            return false;
        }
    }

    if (!reader.read_value(raw.val_energy) ||
        !reader.read_value(
            raw.global_cooldown_remaining) ||
        !reader.read_value(
            raw.energy_regeneration_delay_remaining)) {
        return false;
    }
    if (counted_arrays &&
        !read_expected_array_count(
            reader,
            raw.cooldowns_remaining.size())) {
        return false;
    }
    for (auto& cooldown :
         raw.cooldowns_remaining) {
        if (!reader.read_value(cooldown)) {
            return false;
        }
    }
    if (counted_arrays &&
        !read_expected_array_count(
            reader,
            raw.charges.size())) {
        return false;
    }
    for (auto& charges : raw.charges) {
        if (!reader.read_value(charges)) {
            return false;
        }
    }

    if (counted_arrays &&
        !read_expected_array_count(
            reader,
            raw.construction_plans.size())) {
        return false;
    }
    for (auto& plan :
         raw.construction_plans) {
        if (!reader.read_value(plan.cell_count) ||
            !read_enum(
                reader,
                plan.shape) ||
            !read_bool(
                reader,
                plan.mirrored)) {
            return false;
        }
        if (counted_arrays &&
            !read_expected_array_count(
                reader,
                plan.cells.size())) {
            return false;
        }
        for (auto& cell : plan.cells) {
            if (!reader.read_value(cell.x) ||
                !reader.read_value(cell.y) ||
                !reader.read_value(cell.z) ||
                !reader.read_value(
                    cell.material_id)) {
                return false;
            }
        }
    }
    if (!reader.read_value(
            raw.selected_construction_plan) ||
        !reader.read_value(
            raw.successful_cast_sequence) ||
        !read_enum(
            reader,
            raw.last_dominant_path) ||
        !reader.read_value(
            raw.revision)) {
        return false;
    }

    sanitize_player_build_state(
        raw,
        level);
    build = raw;
    return true;
}

void write_creature(BinaryWriter& writer, const CreatureInstance& creature) {
    writer.write_value(creature.anchor.chunk.x);
    writer.write_value(creature.anchor.chunk.z);
    writer.write_value(creature.anchor.ground_block.x);
    writer.write_value(creature.anchor.ground_block.y);
    writer.write_value(creature.anchor.ground_block.z);
    write_vec3(writer, creature.anchor.spawn_position);
    write_enum(writer, creature.anchor.species);
    write_vec3(writer, creature.position);
    writer.write_value(creature.yaw_radians);
    writer.write_value(creature.behavior_timer);
    writer.write_value(creature.animation_time);
    writer.write_value(creature.wander_heading);
    writer.write_value(creature.nervous_intensity);
    writer.write_value(creature.behavior_seed);
    writer.write_value(creature.appearance_seed);
    write_enum(writer, creature.behavior_state);
    write_enum(writer, creature.phase);
    writer.write_value(creature.morph_factor);
    writer.write_value(creature.motion_amount);
    writer.write_value(creature.gaze_weight);
    writer.write_value(creature.attack_cooldown);
    writer.write_value(creature.attack_amount);
    writer.write_value(creature.health);
}

auto read_creature(BinaryReader& reader, CreatureInstance& creature, std::uint32_t version) -> bool {
    if (!reader.read_value(creature.anchor.chunk.x) ||
        !reader.read_value(creature.anchor.chunk.z) ||
        !is_safe_saved_chunk_coord(creature.anchor.chunk)) {
        return false;
    }
    if (!reader.read_value(creature.anchor.ground_block.x) ||
        !reader.read_value(creature.anchor.ground_block.y) ||
        !reader.read_value(creature.anchor.ground_block.z) ||
        !read_vec3(reader, creature.anchor.spawn_position) ||
        !read_enum(reader, creature.anchor.species) ||
        !read_vec3(reader, creature.position) ||
        !reader.read_value(creature.yaw_radians) ||
        !reader.read_value(creature.behavior_timer) ||
        !reader.read_value(creature.animation_time) ||
        !reader.read_value(creature.wander_heading) ||
        !reader.read_value(creature.nervous_intensity) ||
        !reader.read_value(creature.behavior_seed) ||
        !reader.read_value(creature.appearance_seed) ||
        !read_enum(reader, creature.behavior_state) ||
        !read_enum(reader, creature.phase) ||
        !reader.read_value(creature.morph_factor) ||
        !reader.read_value(creature.motion_amount) ||
        !reader.read_value(creature.gaze_weight) ||
        !reader.read_value(creature.attack_cooldown) ||
        !reader.read_value(creature.attack_amount)) {
        return false;
    }
    if (version >= kSaveVersionEquipmentAndCreatureHealth) {
        return reader.read_value(creature.health);
    }

    creature.health = creature_max_health(creature.anchor.species);
    return true;
}

void write_item_drop(BinaryWriter& writer, const ItemDrop& drop) {
    auto sanitized_drop = drop;
    if (!sanitize_item_drop_state(sanitized_drop)) {
        throw std::runtime_error("Unable to serialize an invalid item drop");
    }
    write_vec3(writer, sanitized_drop.position);
    write_vec3(writer, sanitized_drop.velocity);
    write_hotbar_slot(writer, sanitized_drop.stack);
    writer.write_value(sanitized_drop.age_seconds);
    writer.write_value(sanitized_drop.pickup_cooldown);
    write_bool(writer, sanitized_drop.grounded);
}

auto read_item_drop(BinaryReader& reader, ItemDrop& drop) -> bool {
    if (!read_vec3(reader, drop.position) ||
        !read_vec3(reader, drop.velocity) ||
        !read_hotbar_slot(reader, drop.stack) ||
        !reader.read_value(drop.age_seconds) ||
        !reader.read_value(drop.pickup_cooldown) ||
        !read_bool(reader, drop.grounded)) {
        return false;
    }
    return sanitize_item_drop_state(drop);
}

auto load_metadata_from_file(const std::filesystem::path& file_path) -> std::optional<SaveSlotMetadata> {
    std::ifstream input(file_path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    BinaryReader reader(input);
    std::array<char, kSaveMagic.size()> magic {};
    std::uint32_t version = 0;
    SaveSlotMetadata metadata {};
    if (!reader.read_bytes(magic.data(), magic.size()) ||
        magic != kSaveMagic ||
        !reader.read_value(version) ||
        !is_supported_save_version(version) ||
        !reader.read_value(metadata.saved_at_unix_seconds) ||
        !reader.read_value(metadata.seed) ||
        !reader.read_value(metadata.time_of_day) ||
        !reader.read_value(metadata.modified_chunk_count)) {
        return std::nullopt;
    }
    if (!has_sane_save_metadata_counts(metadata)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionStartingVillage && !read_bool(reader, metadata.has_starting_village)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionWeatherCycle && !reader.read_value(metadata.weather_time_seconds)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionGameMode && !read_game_mode(reader, metadata.game_mode)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionCompactWorldOverrides) {
        auto generation_profile = WorldGenerationProfile::Continental;
        auto world_generation_version = std::uint32_t {0};
        if (!read_generation_profile(reader, generation_profile) ||
            !reader.read_value(world_generation_version) ||
            !is_world_generation_version_compatible_with_save(
                version,
                generation_profile,
                static_cast<WorldGenerationVersion>(world_generation_version)) ||
            generation_profile != generation_profile_for_game_mode(metadata.game_mode)) {
            return std::nullopt;
        }
    }

    metadata.exists = true;
    return metadata;
}

} // namespace

void migrate_backrooms_snapshot_to_v3(
    SaveGameSnapshot& snapshot,
    WorldGenerationVersion source_version,
    int logical_level) noexcept {
    if (!BackroomsSpatialStack::is_anchor_level_representable(
            logical_level)) {
        return;
    }
    const auto seed = snapshot.metadata.seed;
    const BackroomsSpatialStack target_stack(
        seed,
        logical_level,
        BackroomsSpatialProfile::RecessedPoolroomsV3);
    const auto fallback_block = target_stack.anchor_spawn_block();
    const auto fallback = glm::vec3 {
        static_cast<float>(fallback_block.x) + 0.5F,
        static_cast<float>(fallback_block.y) + 0.001F,
        static_cast<float>(fallback_block.z) + 0.5F,
    };
    constexpr std::array<int, 5U> vertical_offsets {{
        0,
        1,
        -1,
        2,
        -2,
    }};

    const auto migrate_position =
        [&](glm::vec3 position,
            int required_height,
            float half_width) noexcept {
            if (std::isfinite(position.y)) {
                position.y += static_cast<float>(
                    backrooms_v3_position_delta_y(
                        seed,
                        logical_level,
                        source_version,
                        position.y));
            }
            position = {
                std::isfinite(position.x) ? position.x : fallback.x,
                std::isfinite(position.y) ? position.y : fallback.y,
                std::isfinite(position.z) ? position.z : fallback.z,
            };

            // Je teste la position historique puis les quatre corrections Y
            // autorisées, avec toute l'emprise physique du corps concerné.
            for (const auto offset : vertical_offsets) {
                const auto candidate =
                    position + glm::vec3 {
                                   0.0F,
                                   static_cast<float>(offset),
                                   0.0F,
                    };
                if (target_stack.has_body_clearance(
                        candidate.x,
                        candidate.y,
                        candidate.z,
                        required_height,
                        half_width)) {
                    return candidate;
                }
            }
            return fallback;
        };

    snapshot.spawn_position =
        migrate_position(snapshot.spawn_position, 3, 0.30F);
    snapshot.player_state.position =
        migrate_position(
            snapshot.player_state.position,
            3,
            0.30F);
    snapshot.player_state.fall_start_y =
        snapshot.player_state.position.y;
    snapshot.backrooms_jack.position =
        migrate_position(
            snapshot.backrooms_jack.position,
            4,
            0.42F);
    snapshot.backrooms_jack.last_seen_player_position =
        migrate_position(
            snapshot.backrooms_jack.last_seen_player_position,
            3,
            0.30F);
    snapshot.backrooms_jack.previous_player_position =
        migrate_position(
            snapshot.backrooms_jack.previous_player_position,
            3,
            0.30F);

    // Je régénère toute la géométrie procédurale : aucun override
    // V1/V2 ne doit être rejoué sur les nouveaux fonds et les margelles.
    snapshot.world_save_plan.generation_version =
        WorldGenerationVersion::BackroomsV3;
    snapshot.world_save_plan.chunks.clear();
    snapshot.metadata.modified_chunk_count = 0U;
    snapshot.backrooms_marlow.logical_level =
        static_cast<std::int32_t>(logical_level);
}

auto sanitize_player_ability_runtime_save_state(
    const PlayerAbilityRuntimeSaveState& requested) noexcept
    -> PlayerAbilityRuntimeSaveState {
    PlayerAbilityRuntimeSaveState sanitized {};

    PlayerAbilityEffects normalized_effects {};
    static_cast<void>(
        normalized_effects.load_state(
            requested.player_effects));
    sanitized.player_effects =
        normalized_effects.snapshot();
    if (sanitized.player_effects
            .iron_guard_cast_sequence ==
        std::numeric_limits<
            AbilityCastSequence>::max()) {
        sanitized.player_effects = {};
    }

    const auto finite_clamp =
        [](float value,
           float minimum,
           float maximum) noexcept {
            return std::isfinite(value)
                       ? std::clamp(
                             value,
                             minimum,
                             maximum)
                       : minimum;
        };
    const auto finite_position =
        [](const glm::vec3& value) noexcept {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z) &&
                   std::abs(value.x) <=
                       kMaxSavedWorldCoordinateMagnitude &&
                   std::abs(value.y) <=
                       kMaxSavedWorldCoordinateMagnitude &&
                   std::abs(value.z) <=
                       kMaxSavedWorldCoordinateMagnitude;
        };

    if (std::isfinite(
            requested.wind.remaining_seconds) &&
        requested.wind.remaining_seconds > 0.0F &&
        requested.wind.cast_sequence != 0U &&
        requested.wind.cast_sequence !=
            std::numeric_limits<
                AbilityCastSequence>::max()) {
        sanitized.wind.remaining_seconds =
            std::clamp(
                requested.wind.remaining_seconds,
                0.0F,
                kMaximumSavedAbilityRuntimeSeconds);
        sanitized.wind.movement_bonus =
            finite_clamp(
                requested.wind.movement_bonus,
                0.0F,
                kMaximumTemporaryMovementSpeedBonus);
        sanitized.wind.recovery_bonus =
            finite_clamp(
                requested.wind.recovery_bonus,
                0.0F,
                kMaximumTemporaryRecoverySpeedBonus);
        sanitized.wind.dodge_remaining_seconds =
            finite_clamp(
                requested.wind
                    .dodge_remaining_seconds,
                0.0F,
                sanitized.wind.remaining_seconds);
        sanitized.wind.blade_armed =
            requested.wind.blade_armed;
        sanitized.wind.cast_sequence =
            requested.wind.cast_sequence;
    }

    const auto rank_is_valid =
        [](SummonedUnitRank rank) noexcept {
            return rank ==
                       SummonedUnitRank::RankOne ||
                   rank ==
                       SummonedUnitRank::RankTwo ||
                   rank ==
                       SummonedUnitRank::RankThree;
        };
    const auto positive_bounded =
        [](float value,
           float maximum) noexcept {
            return std::isfinite(value) &&
                   value > 0.0F &&
                   value <= maximum;
        };
    const auto stats_are_valid =
        [&positive_bounded](
            const SummonedUnitStats& stats) noexcept {
            if (!positive_bounded(
                    stats.duration_seconds,
                    kSummonedUnitMaximumDurationSeconds) ||
                !positive_bounded(
                    stats.maximum_health,
                    kSummonedUnitMaximumResolvedStat) ||
                !std::isfinite(
                    stats.attack_damage) ||
                stats.attack_damage < 0.0F ||
                stats.attack_damage >
                    kSummonedUnitMaximumResolvedStat ||
                !positive_bounded(
                    stats.attack_interval_seconds,
                    kSummonedUnitMaximumDurationSeconds) ||
                stats.attack_interval_seconds <
                    kSummonedUnitMinimumIntervalSeconds ||
                !std::isfinite(
                    stats.mastery_damage_reduction) ||
                stats.mastery_damage_reduction < 0.0F ||
                stats.mastery_damage_reduction > 1.0F ||
                !positive_bounded(
                    stats.mastery_survival_health,
                    stats.maximum_health) ||
                !positive_bounded(
                    stats.mastery_damage_reduction_seconds,
                    kSummonedUnitMaximumDurationSeconds)) {
                return false;
            }

            const auto taunt_is_valid =
                std::isfinite(
                    stats.taunt_interval_seconds) &&
                stats.taunt_interval_seconds >= 0.0F &&
                stats.taunt_interval_seconds <=
                    kSummonedUnitMaximumDurationSeconds &&
                std::isfinite(
                    stats.taunt_radius) &&
                stats.taunt_radius >= 0.0F &&
                stats.taunt_radius <=
                    kSummonedUnitMaximumRadius &&
                (!stats.has_light_taunt ||
                 (stats.taunt_interval_seconds >=
                      kSummonedUnitMinimumIntervalSeconds &&
                  stats.taunt_radius > 0.0F));
            const auto projectile_is_valid =
                std::isfinite(
                    stats
                        .projectile_block_interval_seconds) &&
                stats
                        .projectile_block_interval_seconds >=
                    0.0F &&
                stats
                        .projectile_block_interval_seconds <=
                    kSummonedUnitMaximumDurationSeconds &&
                (!stats.has_projectile_block ||
                 stats
                         .projectile_block_interval_seconds >=
                     kSummonedUnitMinimumIntervalSeconds);
            return taunt_is_valid &&
                   projectile_is_valid;
        };
    const auto event_epsilon =
        [](double first,
           double second) noexcept {
            constexpr auto absolute = 1.0e-7;
            constexpr auto relative =
                static_cast<double>(
                    std::numeric_limits<
                        float>::epsilon()) *
                4.0;
            return std::max(
                absolute,
                std::max(
                    std::abs(first),
                    std::abs(second)) *
                    relative);
        };

    auto used_ids =
        std::array<
            SummonedUnitId,
            kMaximumSavedPlayerSummons> {};
    auto used_cast_sequences =
        std::array<
            AbilityCastSequence,
            kMaximumSavedPlayerSummons> {};
    auto used_count = std::size_t {0U};
    auto maximum_unit_id =
        SummonedUnitId {0U};
    auto maximum_cast_sequence =
        std::max(
            sanitized.player_effects
                .iron_guard_cast_sequence,
            sanitized.wind.cast_sequence);

    for (std::size_t index = 0U;
         index <
         requested.summoned_footmen.size();
         ++index) {
        const auto& source =
            requested.summoned_footmen[index];
        if (!source.runtime.active) {
            continue;
        }

        auto runtime = source.runtime;
        if (runtime.cast_sequence == 0U) {
            runtime.cast_sequence =
                source.cast_sequence;
        }
        const auto duplicate_id =
            std::find(
                used_ids.begin(),
                used_ids.begin() +
                    static_cast<
                        std::ptrdiff_t>(
                        used_count),
                runtime.unit_id) !=
            used_ids.begin() +
                static_cast<
                    std::ptrdiff_t>(
                    used_count);
        const auto duplicate_cast =
            std::find(
                used_cast_sequences.begin(),
                used_cast_sequences.begin() +
                    static_cast<
                        std::ptrdiff_t>(
                        used_count),
                runtime.cast_sequence) !=
            used_cast_sequences.begin() +
                static_cast<
                    std::ptrdiff_t>(
                    used_count);
        const auto critical_state_valid =
            runtime.unit_id != 0U &&
            runtime.unit_id !=
                std::numeric_limits<
                    SummonedUnitId>::max() &&
            runtime.cast_sequence != 0U &&
            runtime.cast_sequence !=
                std::numeric_limits<
                    AbilityCastSequence>::max() &&
            !duplicate_id &&
            !duplicate_cast &&
            finite_position(runtime.position) &&
            rank_is_valid(runtime.rank) &&
            stats_are_valid(runtime.stats) &&
            std::isfinite(runtime.age_seconds) &&
            runtime.age_seconds >= 0.0 &&
            std::isfinite(runtime.health) &&
            runtime.health > 0.0F;
        if (!critical_state_valid) {
            continue;
        }
        const auto duration =
            static_cast<double>(
                runtime.stats.duration_seconds);
        if (runtime.age_seconds >=
            duration -
                event_epsilon(
                    runtime.age_seconds,
                    duration)) {
            continue;
        }

        runtime.health =
            std::min(
                runtime.health,
                runtime.stats.maximum_health);
        const auto sanitize_next_event =
            [&event_epsilon](
                double value,
                double age,
                float interval) noexcept {
                const auto maximum =
                    age +
                    static_cast<double>(
                        interval);
                const auto epsilon =
                    event_epsilon(
                        value,
                        maximum);
                if (!std::isfinite(value) ||
                    value <= age + epsilon ||
                    value >
                        maximum + epsilon) {
                    return age +
                           static_cast<double>(
                               interval);
                }
                return value;
            };
        runtime.next_attack_seconds =
            sanitize_next_event(
                runtime.next_attack_seconds,
                runtime.age_seconds,
                runtime.stats
                    .attack_interval_seconds);
        if (runtime.stats.has_light_taunt) {
            runtime.next_taunt_seconds =
                sanitize_next_event(
                    runtime.next_taunt_seconds,
                    runtime.age_seconds,
                    runtime.stats
                        .taunt_interval_seconds);
        } else if (
            !std::isfinite(
                runtime.next_taunt_seconds) ||
            runtime.next_taunt_seconds < 0.0) {
            runtime.next_taunt_seconds =
                static_cast<double>(
                    runtime.stats
                        .taunt_interval_seconds);
        }

        runtime.projectile_block_cooldown =
            runtime.stats.has_projectile_block
                ? finite_clamp(
                      runtime
                          .projectile_block_cooldown,
                      0.0F,
                      runtime.stats
                          .projectile_block_interval_seconds)
                : 0.0F;
        runtime.mastery_damage_reduction_seconds =
            runtime.mastered
                ? finite_clamp(
                      runtime
                          .mastery_damage_reduction_seconds,
                      0.0F,
                      runtime.stats
                          .mastery_damage_reduction_seconds)
                : 0.0F;
        runtime.yaw_radians =
            std::isfinite(
                runtime.yaw_radians)
                ? runtime.yaw_radians
                : 0.0F;
        const auto maximum_animation_time =
            runtime.age_seconds +
            event_epsilon(
                runtime.animation_time,
                runtime.age_seconds);
        runtime.animation_time =
            std::isfinite(
                runtime.animation_time) &&
                    runtime.animation_time >= 0.0F &&
                    static_cast<double>(
                        runtime.animation_time) <=
                        maximum_animation_time
                ? runtime.animation_time
                : static_cast<float>(
                      runtime.age_seconds);
        const auto sanitize_last_event =
            [&event_epsilon](
                double value,
                double age) noexcept {
                if (value == -1.0) {
                    return value;
                }
                return !std::isfinite(value) ||
                               value < 0.0 ||
                               value >
                                   age +
                                       event_epsilon(
                                           value,
                                           age)
                           ? -1.0
                           : value;
            };
        runtime.last_attack_event_seconds =
            sanitize_last_event(
                runtime.last_attack_event_seconds,
                runtime.age_seconds);
        runtime.last_taunt_event_seconds =
            sanitize_last_event(
                runtime.last_taunt_event_seconds,
                runtime.age_seconds);
        runtime.death_refusal_used =
            runtime.mastered &&
            runtime.death_refusal_used;
        runtime.pending_mastery_taunt =
            runtime.mastered &&
            runtime.death_refusal_used &&
            runtime.pending_mastery_taunt;

        auto& destination =
            sanitized.summoned_footmen[index];
        destination.runtime = runtime;
        destination.cast_sequence =
            runtime.cast_sequence;
        destination.far_seconds =
            finite_clamp(
                source.far_seconds,
                0.0F,
                kMaximumSavedAbilityRuntimeSeconds);
        if (source.ship_local_position
                .has_value() &&
            finite_position(
                *source.ship_local_position)) {
            destination.ship_local_position =
                source.ship_local_position;
        }

        used_ids[used_count] =
            runtime.unit_id;
        used_cast_sequences[used_count] =
            runtime.cast_sequence;
        ++used_count;
        maximum_unit_id =
            std::max(
                maximum_unit_id,
                runtime.unit_id);
        maximum_cast_sequence =
            std::max(
                maximum_cast_sequence,
                runtime.cast_sequence);
    }

    const auto minimum_next_unit_id =
        maximum_unit_id + 1U;
    sanitized.next_summoned_unit_id =
        std::max(
            requested.next_summoned_unit_id,
            std::max(
                minimum_next_unit_id,
                SummonedUnitId {1U}));
    const auto minimum_next_cast_sequence =
        maximum_cast_sequence + 1U;
    sanitized.next_cast_sequence =
        std::max(
            requested.next_cast_sequence,
            std::max(
                minimum_next_cast_sequence,
                AbilityCastSequence {1U}));
    return sanitized;
}

auto save_slot_file_path(const std::filesystem::path& root_directory, std::size_t slot_index) -> std::filesystem::path {
    if (slot_index >= kSaveSlotCount) {
        return {};
    }

    const auto display_index = static_cast<unsigned long long>(slot_index + 1U);
    const auto file_name = display_index < 10ULL
                               ? std::string("slot_0") + std::to_string(display_index) + ".valsave"
                               : std::string("slot_") + std::to_string(display_index) + ".valsave";
    return root_directory / file_name;
}

auto scan_save_slots(const std::filesystem::path& root_directory) -> std::array<SaveSlotMetadata, kSaveSlotCount> {
    std::array<SaveSlotMetadata, kSaveSlotCount> slots {};
    for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
        const auto metadata = load_metadata_from_file(save_slot_file_path(root_directory, slot_index));
        if (metadata.has_value()) {
            slots[slot_index] = *metadata;
        }
    }
    return slots;
}

auto load_save_slot(const std::filesystem::path& root_directory, std::size_t slot_index) -> std::optional<SaveGameSnapshot> {
    return load_save_slot(root_directory, slot_index, {});
}

auto load_save_slot(const std::filesystem::path& root_directory,
                    std::size_t slot_index,
                    const SaveLoadProgressCallback& progress_callback) -> std::optional<SaveGameSnapshot> {
    if (slot_index >= kSaveSlotCount) {
        return std::nullopt;
    }

    const auto file_path = save_slot_file_path(root_directory, slot_index);
    std::error_code file_size_error;
    const auto raw_file_size = std::filesystem::file_size(file_path, file_size_error);
    const auto total_bytes = file_size_error
                                 ? std::uint64_t {0}
                                 : static_cast<std::uint64_t>(std::min<std::uintmax_t>(
                                       raw_file_size,
                                       (std::numeric_limits<std::uint64_t>::max)()));
    std::ifstream input(file_path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    if (!report_load_progress(input, total_bytes, SaveLoadPhase::OpeningFile, progress_callback)) {
        return std::nullopt;
    }

    BinaryReader reader(input);
    std::array<char, kSaveMagic.size()> magic {};
    std::uint32_t version = 0;
    SaveGameSnapshot snapshot {};
    std::uint32_t hotbar_selected_index = 0;
    std::uint32_t creature_count = 0;
    std::uint32_t item_drop_count = 0;
    std::uint32_t chunk_count = 0;
    auto generation_profile = WorldGenerationProfile::Continental;
    // Les formats v1-v7 precedent le champ de version et correspondent tous
    // au generateur historique, quelle que soit la version courante du jeu.
    auto world_generation_version = static_cast<std::uint32_t>(WorldGenerationVersion::LegacyV1);

    if (!reader.read_bytes(magic.data(), magic.size()) ||
        magic != kSaveMagic ||
        !reader.read_value(version) ||
        !is_supported_save_version(version) ||
        !reader.read_value(snapshot.metadata.saved_at_unix_seconds) ||
        !reader.read_value(snapshot.metadata.seed) ||
        !reader.read_value(snapshot.metadata.time_of_day) ||
        !reader.read_value(snapshot.metadata.modified_chunk_count)) {
        return std::nullopt;
    }
    if (!has_sane_save_metadata_counts(snapshot.metadata)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionStartingVillage && !read_bool(reader, snapshot.metadata.has_starting_village)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionWeatherCycle && !reader.read_value(snapshot.metadata.weather_time_seconds)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionGameMode && !read_game_mode(reader, snapshot.metadata.game_mode)) {
        return std::nullopt;
    }
    generation_profile = generation_profile_for_game_mode(snapshot.metadata.game_mode);
    if (version >= kSaveVersionCompactWorldOverrides &&
        (!read_generation_profile(reader, generation_profile) ||
         !reader.read_value(world_generation_version) ||
         !is_world_generation_version_compatible_with_save(
             version,
             generation_profile,
             static_cast<WorldGenerationVersion>(world_generation_version)) ||
         generation_profile != generation_profile_for_game_mode(snapshot.metadata.game_mode))) {
        return std::nullopt;
    }
    if (!report_load_progress(input, total_bytes, SaveLoadPhase::ReadingMetadata, progress_callback)) {
        return std::nullopt;
    }
    if (!read_vec3(reader, snapshot.spawn_position) ||
        !is_sane_saved_world_position(snapshot.spawn_position) ||
        !read_player_state(reader, snapshot.player_state) ||
        !is_sane_saved_world_position(snapshot.player_state.position)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionPlayerProgression) {
        if (!read_player_progression(reader, snapshot.progression, version)) {
            return std::nullopt;
        }
    } else {
        snapshot.progression = {};
    }
    if (version >=
        kSaveVersionProgressionBuilds) {
        if (!read_player_build_state(
                reader,
                snapshot.player_build,
                snapshot.progression.level,
                version)) {
            return std::nullopt;
        }
    } else {
        snapshot.player_build = {};
    }
    if (version >= kSaveVersionGameMode) {
        if (!read_sea_adventure_state(reader, snapshot.sea_adventure, version)) {
            return std::nullopt;
        }
    } else {
        snapshot.metadata.game_mode = GameMode::ClassicAdventure;
        snapshot.sea_adventure = {};
    }
    if (version >=
        kSaveVersionRuntimeState) {
        if (!read_maritime_experience_state(
                reader,
                snapshot.maritime_experience) ||
            !read_player_ability_runtime_state(
                reader,
                snapshot.player_ability_runtime)) {
            return std::nullopt;
        }
    } else {
        // Je marque uniquement les jalons déductibles de l'ancien état : je
        // n'accorde aucune XP au chargement et je laisse les runtimes absents.
        snapshot.maritime_experience =
            derive_legacy_maritime_experience_state(
                snapshot.sea_adventure);
        snapshot.player_ability_runtime = {};
    }
    if (!report_load_progress(input, total_bytes, SaveLoadPhase::ReadingPlayer, progress_callback)) {
        return std::nullopt;
    }

    snapshot.metadata.exists = true;

    for (auto& slot : snapshot.hotbar.slots) {
        if (!read_hotbar_slot(reader, slot)) {
            return std::nullopt;
        }
    }
    if (!reader.read_value(hotbar_selected_index)) {
        return std::nullopt;
    }
    snapshot.hotbar.selected_index = std::min<std::size_t>(hotbar_selected_index, kHotbarSlotCount - 1U);

    for (auto& slot : snapshot.inventory.storage_slots) {
        if (!read_hotbar_slot(reader, slot)) {
            return std::nullopt;
        }
    }
    if (!read_hotbar_slot(reader, snapshot.inventory.carried_slot) ||
        !read_bool(reader, snapshot.inventory.carrying_item)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionEquipmentAndCreatureHealth) {
        for (auto& slot : snapshot.inventory.equipment_slots) {
            if (!read_hotbar_slot(reader, slot)) {
                return std::nullopt;
            }
        }
    } else {
        snapshot.inventory.equipment_slots.fill(inventory_empty_slot());
    }
    snapshot.inventory.visible = false;
    snapshot.inventory.hovered_slot.reset();
    if (!report_load_progress(input, total_bytes, SaveLoadPhase::ReadingPlayer, progress_callback)) {
        return std::nullopt;
    }

    if (!reader.read_value(creature_count) || creature_count > kMaxSavedCreatureCount) {
        return std::nullopt;
    }
    snapshot.creatures.resize(creature_count);
    for (std::size_t index = 0; index < snapshot.creatures.size(); ++index) {
        auto& creature = snapshot.creatures[index];
        if (!read_creature(reader, creature, version)) {
            return std::nullopt;
        }
        if ((index & 31U) == 31U &&
            !report_load_progress(input, total_bytes, SaveLoadPhase::ReadingEntities, progress_callback)) {
            return std::nullopt;
        }
    }

    if (!reader.read_value(item_drop_count) || item_drop_count > kMaxSavedItemDropCount) {
        return std::nullopt;
    }
    snapshot.item_drops.resize(item_drop_count);
    for (std::size_t index = 0; index < snapshot.item_drops.size(); ++index) {
        auto& item_drop = snapshot.item_drops[index];
        if (!read_item_drop(reader, item_drop)) {
            return std::nullopt;
        }
        if ((index & 31U) == 31U &&
            !report_load_progress(input, total_bytes, SaveLoadPhase::ReadingEntities, progress_callback)) {
            return std::nullopt;
        }
    }
    if (!report_load_progress(input, total_bytes, SaveLoadPhase::ReadingEntities, progress_callback)) {
        return std::nullopt;
    }

    if (!reader.read_value(chunk_count) || chunk_count > kMaxSavedChunkSnapshotCount) {
        return std::nullopt;
    }
    snapshot.world_save_plan.seed = snapshot.metadata.seed;
    snapshot.world_save_plan.generation_profile = generation_profile;
    snapshot.world_save_plan.generation_version =
        static_cast<WorldGenerationVersion>(world_generation_version);
    snapshot.world_save_plan.chunks.reserve(chunk_count);
    for (std::uint32_t chunk_number = 0; chunk_number < chunk_count; ++chunk_number) {
        WorldSavePlanChunk chunk_plan {};
        auto sparse_encoding = false;
        if (!reader.read_value(chunk_plan.coord.x) ||
            !reader.read_value(chunk_plan.coord.z) ||
            !is_safe_saved_chunk_coord(chunk_plan.coord)) {
            return std::nullopt;
        }
        if (version >= kSaveVersionCompactWorldOverrides) {
            auto raw_encoding = std::uint8_t {0};
            if (!reader.read_value(raw_encoding)) {
                return std::nullopt;
            }
            const auto encoding = static_cast<SavedChunkEncoding>(raw_encoding);
            if (encoding == SavedChunkEncoding::Sparse) {
                sparse_encoding = true;
                auto sparse_count = std::uint32_t {0};
                if (!reader.read_value(sparse_count) ||
                    sparse_count > kChunkVolume ||
                    (sparse_count == 0U &&
                     version < kSaveVersionProgressionBuilds)) {
                    return std::nullopt;
                }
                chunk_plan.sparse_cells.resize(sparse_count);
                auto previous_index = std::optional<std::size_t> {};
                for (std::uint32_t cell_number = 0; cell_number < sparse_count; ++cell_number) {
                    auto& cell = chunk_plan.sparse_cells[cell_number];
                    if (!reader.read_value(cell.index) ||
                        !reader.read_value(cell.block) ||
                        !reader.read_value(cell.water_state)) {
                        return std::nullopt;
                    }
                    const auto index = static_cast<std::size_t>(cell.index);
                    if (index >= kChunkVolume ||
                        (previous_index.has_value() && index <= *previous_index)) {
                        return std::nullopt;
                    }
                    previous_index = index;
                    if ((cell_number & 255U) == 255U &&
                            !report_load_progress(input, total_bytes, SaveLoadPhase::ReadingWorld, progress_callback)) {
                        return std::nullopt;
                    }
                }
            }
            if (encoding != SavedChunkEncoding::Dense &&
                encoding != SavedChunkEncoding::Sparse) {
                return std::nullopt;
            }
        }

        if (!sparse_encoding) {
            chunk_plan.dense_blocks.resize(kChunkVolume);
            chunk_plan.dense_water_state.resize(kChunkVolume);
            if (!reader.read_bytes(
                    chunk_plan.dense_blocks.data(),
                    chunk_plan.dense_blocks.size() *
                        sizeof(BlockId))) {
                return std::nullopt;
            }
            if (version >= kSaveVersionWaterState) {
                if (!reader.read_bytes(
                        chunk_plan.dense_water_state.data(),
                        chunk_plan.dense_water_state.size() *
                            sizeof(WaterState))) {
                    return std::nullopt;
                }
            } else {
                std::fill(
                    chunk_plan.dense_water_state.begin(),
                    chunk_plan.dense_water_state.end(),
                    WaterState {0});
                for (std::size_t block_index = 0;
                     block_index <
                     chunk_plan.dense_blocks.size();
                     ++block_index) {
                    auto& block_id =
                        chunk_plan.dense_blocks[
                            block_index];
                    if (block_id !=
                        to_block_id(
                            BlockType::Water)) {
                        continue;
                    }
                    chunk_plan.dense_water_state[
                        block_index] =
                        make_water_state(
                            kMaxWaterLevel,
                            true);
                    block_id =
                        to_block_id(
                            BlockType::Air);
                }
            }
        }

        if (version >= kSaveVersionProgressionBuilds) {
            if (!reader.read_bytes(
                    chunk_plan.player_placed_mask.data(),
                    chunk_plan.player_placed_mask.size())) {
                return std::nullopt;
            }
            if (sparse_encoding &&
                chunk_plan.sparse_cells.empty() &&
                world_player_placed_mask_empty(
                    chunk_plan.player_placed_mask)) {
                return std::nullopt;
            }
        }

        snapshot.world_save_plan.chunks.push_back(std::move(chunk_plan));
        if (!report_load_progress(input, total_bytes, SaveLoadPhase::ReadingWorld, progress_callback)) {
            return std::nullopt;
        }
    }

    if (version >= kSaveVersionItemInstanceState &&
        !read_item_instance_state_extension(
            reader,
            snapshot)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionLegendaryWeapon &&
        !read_legendary_weapon_state_extension(
            reader,
            snapshot.legendary_weapon)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionBackroomsFlashlight &&
        !read_backrooms_flashlight_state_extension(
            reader,
            snapshot.backrooms_flashlight)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionBackroomsJack) {
        if (!read_backrooms_jack_state_extension(
                reader,
                snapshot.backrooms_jack)) {
            return std::nullopt;
        }
    } else {
        // Je migre v1-v16 vers un Jack dormant neuf, dérivé du seed du slot.
        // Aucun monstre actif ne peut ainsi surgir immédiatement au chargement
        // d'une sauvegarde qui ne connaissait pas encore son état.
        snapshot.backrooms_jack =
            initialize_backrooms_jack(
                static_cast<std::uint32_t>(
                    snapshot.metadata.seed));
    }
    if (version >= kSaveVersionBackroomsLevel) {
        if (!read_backrooms_level_state_extension(
                reader,
                snapshot.backrooms_level) ||
            (snapshot.metadata.game_mode !=
                     GameMode::Backrooms &&
             snapshot.backrooms_level != 0)) {
            return std::nullopt;
        }
    } else {
        // Les sauvegardes v1-v17 ne connaissaient qu'un étage Backrooms.
        snapshot.backrooms_level = 0;
    }
    if (version >= kSaveVersionBackroomsMarlow) {
        if (!read_backrooms_marlow_state_extension(
                reader,
                snapshot.backrooms_marlow) ||
            snapshot.backrooms_marlow.logical_level !=
                snapshot.backrooms_level) {
            return std::nullopt;
        }
    } else {
        // Je reprends v1-v18 avec un directeur dormant deterministe. Aucune
        // manifestation, trajectoire ou noyade historique n'est inventee.
        snapshot.backrooms_marlow =
            initialize_backrooms_marlow(
                static_cast<std::uint32_t>(
                    snapshot.metadata.seed),
                snapshot.backrooms_level);
    }
    snapshot.world_save_plan.backrooms_level =
        snapshot.backrooms_level;
    snapshot.backrooms_jack.logical_level =
        snapshot.backrooms_level;
    normalize_inventory_state(snapshot.inventory, snapshot.hotbar);
    for (auto& drop : snapshot.item_drops) {
        normalize_item_stack(drop.stack);
    }
    static_cast<void>(
        inventory_reconcile_legendary_weapon(
            snapshot.inventory,
            snapshot.hotbar,
            snapshot.legendary_weapon.weapon_owned));
    if (!report_load_progress(input, total_bytes, SaveLoadPhase::Finalizing, progress_callback, true)) {
        return std::nullopt;
    }
    return snapshot;
}

auto remove_save_slot(const std::filesystem::path& root_directory, std::size_t slot_index) -> bool {
    if (slot_index >= kSaveSlotCount) {
        return false;
    }

    const auto file_path = save_slot_file_path(root_directory, slot_index);
    const auto temp_path = std::filesystem::path(file_path.string() + ".tmp");
    std::error_code error_code;
    bool removed = false;

    if (std::filesystem::exists(file_path, error_code)) {
        error_code.clear();
        removed = std::filesystem::remove(file_path, error_code) || removed;
        if (error_code) {
            return false;
        }
    } else if (error_code) {
        return false;
    }

    error_code.clear();
    if (std::filesystem::exists(temp_path, error_code)) {
        error_code.clear();
        removed = std::filesystem::remove(temp_path, error_code) || removed;
        if (error_code) {
            return false;
        }
    } else if (error_code) {
        return false;
    }

    return removed;
}

static void write_save_slot_impl(const std::filesystem::path& root_directory,
                                 std::size_t slot_index,
                                 const SaveGameSnapshot& snapshot,
                                 const WorldSavePlan* world_save_plan) {
    if (slot_index >= kSaveSlotCount) {
        return;
    }

    validate_snapshot_for_write(snapshot);
    const auto* effective_world_save_plan = world_save_plan;
    const auto embedded_plan_is_coherent =
        snapshot.chunk_snapshots.empty() &&
        snapshot.world_save_plan.seed == snapshot.metadata.seed &&
        snapshot.world_save_plan.generation_profile ==
            generation_profile_for_game_mode(snapshot.metadata.game_mode) &&
        is_supported_world_generation_version(
            snapshot.world_save_plan.generation_profile,
            snapshot.world_save_plan.generation_version);
    if (effective_world_save_plan == nullptr && embedded_plan_is_coherent) {
        // Je conserve aussi un plan charge sans overrides : sa version de
        // generation porte la geographie, meme lorsque sa liste de chunks est vide.
        effective_world_save_plan = &snapshot.world_save_plan;
    }
    if (effective_world_save_plan != nullptr) {
        validate_world_save_plan(snapshot, *effective_world_save_plan);
    }

    if (!root_directory.empty()) {
        std::error_code error_code;
        std::filesystem::create_directories(root_directory, error_code);
        if (error_code) {
            throw std::runtime_error("Unable to create save directory");
        }
    }

    const auto file_path = save_slot_file_path(root_directory, slot_index);
    const auto temp_path = std::filesystem::path(file_path.string() + ".tmp");
    const TempSaveFileCleanup temp_file_cleanup(temp_path);
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Unable to open save slot for writing");
    }

    BinaryWriter writer(output);
    const auto saved_at = snapshot.metadata.saved_at_unix_seconds == 0
                              ? static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                                std::chrono::system_clock::now().time_since_epoch())
                                                                .count())
                              : snapshot.metadata.saved_at_unix_seconds;
    const auto chunk_count = static_cast<std::uint32_t>(
        effective_world_save_plan != nullptr
            ? effective_world_save_plan->chunks.size()
            : snapshot.chunk_snapshots.size());
    const auto creature_count = static_cast<std::uint32_t>(snapshot.creatures.size());
    const auto item_drop_count = static_cast<std::uint32_t>(snapshot.item_drops.size());
    const auto generation_profile = effective_world_save_plan != nullptr
                                        ? effective_world_save_plan->generation_profile
                                        : generation_profile_for_game_mode(snapshot.metadata.game_mode);
    const auto generation_version = resolve_world_generation_version(
        generation_profile,
        effective_world_save_plan != nullptr
            ? effective_world_save_plan->generation_version
            : WorldGenerationVersion::Latest);

    writer.write_bytes(kSaveMagic.data(), kSaveMagic.size());
    writer.write_value(kSaveVersion);
    writer.write_value(saved_at);
    writer.write_value(snapshot.metadata.seed);
    writer.write_value(snapshot.metadata.time_of_day);
    writer.write_value(chunk_count);
    write_bool(writer, snapshot.metadata.has_starting_village);
    writer.write_value(snapshot.metadata.weather_time_seconds);
    write_game_mode(writer, snapshot.metadata.game_mode);
    write_generation_profile(writer, generation_profile);
    writer.write_value(static_cast<std::uint32_t>(generation_version));
    write_vec3(writer, snapshot.spawn_position);
    write_player_state(writer, snapshot.player_state);
    write_player_progression(writer, snapshot.progression);
    write_player_build_state(
        writer,
        snapshot.player_build,
        sanitize_player_progression_state(
            snapshot.progression)
            .level);
    write_sea_adventure_state(writer, snapshot.sea_adventure);
    write_maritime_experience_state(
        writer,
        snapshot.maritime_experience);
    write_player_ability_runtime_state(
        writer,
        snapshot.player_ability_runtime);

    for (const auto& slot : snapshot.hotbar.slots) {
        write_hotbar_slot(writer, slot);
    }
    writer.write_value(static_cast<std::uint32_t>(snapshot.hotbar.selected_index));

    for (const auto& slot : snapshot.inventory.storage_slots) {
        write_hotbar_slot(writer, slot);
    }
    write_hotbar_slot(writer, snapshot.inventory.carried_slot);
    write_bool(writer, snapshot.inventory.carrying_item);
    for (const auto& slot : snapshot.inventory.equipment_slots) {
        write_hotbar_slot(writer, slot);
    }

    writer.write_value(creature_count);
    for (std::size_t index = 0; index < creature_count; ++index) {
        write_creature(writer, snapshot.creatures[index]);
    }

    writer.write_value(item_drop_count);
    for (std::size_t index = 0; index < item_drop_count; ++index) {
        write_item_drop(writer, snapshot.item_drops[index]);
    }

    writer.write_value(chunk_count);
    for (std::size_t index = 0; index < chunk_count; ++index) {
        if (effective_world_save_plan == nullptr) {
            const auto& chunk_snapshot = snapshot.chunk_snapshots[index];
            writer.write_value(chunk_snapshot.coord.x);
            writer.write_value(chunk_snapshot.coord.z);
            writer.write_value(static_cast<std::uint8_t>(SavedChunkEncoding::Dense));
            writer.write_bytes(chunk_snapshot.blocks.data(), chunk_snapshot.blocks.size() * sizeof(BlockId));
            writer.write_bytes(
                chunk_snapshot.water_state.data(),
                chunk_snapshot.water_state.size() * sizeof(WaterState));
            writer.write_bytes(
                chunk_snapshot.player_placed_mask.data(),
                chunk_snapshot.player_placed_mask.size());
            if (!writer.ok()) {
                throw std::runtime_error("Unable to write dense world save chunk");
            }
            continue;
        }

        const auto& chunk_plan = effective_world_save_plan->chunks[index];
        writer.write_value(chunk_plan.coord.x);
        writer.write_value(chunk_plan.coord.z);
        if (chunk_plan.dense()) {
            writer.write_value(static_cast<std::uint8_t>(SavedChunkEncoding::Dense));
            writer.write_bytes(chunk_plan.dense_blocks.data(), chunk_plan.dense_blocks.size() * sizeof(BlockId));
            writer.write_bytes(
                chunk_plan.dense_water_state.data(),
                chunk_plan.dense_water_state.size() * sizeof(WaterState));
            writer.write_bytes(
                chunk_plan.player_placed_mask.data(),
                chunk_plan.player_placed_mask.size());
            if (!writer.ok()) {
                throw std::runtime_error("Unable to write dense world save plan chunk");
            }
            continue;
        }

        writer.write_value(static_cast<std::uint8_t>(SavedChunkEncoding::Sparse));
        writer.write_value(static_cast<std::uint32_t>(chunk_plan.sparse_cells.size()));
        for (const auto& cell : chunk_plan.sparse_cells) {
            writer.write_value(cell.index);
            writer.write_value(cell.block);
            writer.write_value(cell.water_state);
        }
        writer.write_bytes(
            chunk_plan.player_placed_mask.data(),
            chunk_plan.player_placed_mask.size());
        if (!writer.ok()) {
            throw std::runtime_error("Unable to write sparse world save plan chunk");
        }
    }

    // Je termine par une extension additive afin que le corps historique v11
    // et toutes ses fixtures restent lisibles sans recalculer leurs offsets.
    write_item_instance_state_extension(
        writer,
        snapshot);
    // Je separe l'etat permanent de l'arme de tous ses runtimes de combat :
    // un chargement repart donc toujours au repos avec ses jauges recreees.
    write_legendary_weapon_state_extension(
        writer,
        snapshot.legendary_weapon);
    // Je termine par l'état propre aux Backrooms afin que la recharge ne
    // puisse jamais être contournée par une simple sauvegarde/reprise.
    write_backrooms_flashlight_state_extension(
        writer,
        snapshot.backrooms_flashlight);
    // Je garde BJCK inchangé et place BRLV après lui pour une migration
    // append-only depuis le format v17.
    write_backrooms_jack_state_extension(
        writer,
        snapshot.backrooms_jack);
    // Je termine par le niveau logique borné sans modifier BJCK v1.
    write_backrooms_level_state_extension(
        writer,
        snapshot.backrooms_level);
    // Je garde MRLW terminal et append-only : BJCK v1 puis BRLV v1 restent
    // bit a bit a leurs positions relatives pour toutes les migrations.
    write_backrooms_marlow_state_extension(
        writer,
        snapshot.backrooms_marlow);

    output.flush();
    if (!writer.ok() || !output.good()) {
        output.close();
        remove_temp_save_file(temp_path);
        throw std::runtime_error("Unable to write save slot data");
    }
    output.close();
    if (!output.good()) {
        remove_temp_save_file(temp_path);
        throw std::runtime_error("Unable to close save slot data");
    }

    replace_save_file_with_temp(temp_path, file_path);
}

void write_save_slot(const std::filesystem::path& root_directory,
                     std::size_t slot_index,
                     const SaveGameSnapshot& snapshot) {
    write_save_slot_impl(root_directory, slot_index, snapshot, nullptr);
}

void write_save_slot(const std::filesystem::path& root_directory,
                     std::size_t slot_index,
                     const SaveGameSnapshot& snapshot,
                     const WorldSavePlan& world_save_plan) {
    write_save_slot_impl(root_directory, slot_index, snapshot, &world_save_plan);
}

} // namespace valcraft
