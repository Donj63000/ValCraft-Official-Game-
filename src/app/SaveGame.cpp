#include "app/SaveGame.h"

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
constexpr std::uint32_t kSaveVersion = 10;
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

static_assert(kChunkSizeX > 0 && kChunkSizeZ > 0);
static_assert(kShipCrewMemberCount == 6U, "Changer le roster v9 exige une nouvelle version de sauvegarde");
static_assert(sizeof(std::underlying_type_t<ShipCrewRole>) == sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<ShipCrewActivity>) == sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<ShipCrewCargo>) == sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<ShipCrewStation>) == sizeof(std::uint8_t));

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

class BinaryWriter;
class BinaryReader;

auto is_supported_save_version(std::uint32_t version) noexcept -> bool {
    return version >= kSaveVersionLegacy && version <= kSaveVersion;
}

auto has_sane_save_metadata_counts(const SaveSlotMetadata& metadata) noexcept -> bool {
    return metadata.modified_chunk_count <= kMaxSavedChunkSnapshotCount;
}

auto generation_profile_for_game_mode(GameMode mode) noexcept -> WorldGenerationProfile {
    return mode == GameMode::SeaAdventure
               ? WorldGenerationProfile::OceanAdventure
               : WorldGenerationProfile::Continental;
}

auto is_supported_world_generation_version(WorldGenerationProfile profile,
                                           WorldGenerationVersion version) noexcept -> bool {
    if (profile == WorldGenerationProfile::Continental) {
        return version == WorldGenerationVersion::LegacyV1;
    }
    return profile == WorldGenerationProfile::OceanAdventure &&
           (version == WorldGenerationVersion::LegacyV1 ||
            version == WorldGenerationVersion::SparseArchipelagoV2);
}

auto is_world_generation_version_compatible_with_save(std::uint32_t save_version,
                                                      WorldGenerationProfile profile,
                                                      WorldGenerationVersion generation_version) noexcept -> bool {
    if (!is_supported_world_generation_version(profile, generation_version)) {
        return false;
    }
    // Les formats v8/v9 possedaient deja le champ, mais seule la geographie
    // historique V1 existait alors. Une valeur V2 dans ces entetes est donc un
    // payload corrompu et ne doit jamais changer leur monde retroactivement.
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
    for (const auto& drop : snapshot.item_drops) {
        auto sanitized_drop = drop;
        if (!sanitize_item_drop_state(sanitized_drop)) {
            throw std::runtime_error("Save snapshot contains an invalid item drop");
        }
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
        plan.generation_profile != WorldGenerationProfile::OceanAdventure) {
        throw std::runtime_error("World save plan uses an unsupported generation profile");
    }
    if (plan.generation_profile != generation_profile_for_game_mode(snapshot.metadata.game_mode)) {
        throw std::runtime_error("World save plan profile does not match the selected game mode");
    }
    if (!is_supported_world_generation_version(plan.generation_profile, plan.generation_version)) {
        throw std::runtime_error("World save plan uses an unsupported generation version");
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

        if (chunk.sparse_cells.empty() || !chunk.dense_water_state.empty()) {
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
           profile == WorldGenerationProfile::OceanAdventure;
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

    // Je normalise aussi a la frontiere binaire pour qu'un payload ancien ou
    // corrompu ne propage jamais de NaN ni de minuterie pathologique. Pour une
    // save v7/v8, la valeur par defaut laisse le systeme recreer le roster.
    state = sanitize_sea_adventure_save_state(raw);
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

auto read_player_progression(BinaryReader& reader, PlayerProgressionState& progression) -> bool {
    PlayerProgressionState raw {};
    if (!reader.read_value(raw.level) ||
        !reader.read_value(raw.experience)) {
        return false;
    }
    progression = sanitize_player_progression_state(raw);
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
        if (!read_player_progression(reader, snapshot.progression)) {
            return std::nullopt;
        }
    } else {
        snapshot.progression = {};
    }
    if (version >= kSaveVersionGameMode) {
        if (!read_sea_adventure_state(reader, snapshot.sea_adventure, version)) {
            return std::nullopt;
        }
    } else {
        snapshot.metadata.game_mode = GameMode::ClassicAdventure;
        snapshot.sea_adventure = {};
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
                auto sparse_count = std::uint32_t {0};
                if (!reader.read_value(sparse_count) || sparse_count == 0U || sparse_count > kChunkVolume) {
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
                snapshot.world_save_plan.chunks.push_back(std::move(chunk_plan));
                if (!report_load_progress(input, total_bytes, SaveLoadPhase::ReadingWorld, progress_callback)) {
                    return std::nullopt;
                }
                continue;
            }
            if (encoding != SavedChunkEncoding::Dense) {
                return std::nullopt;
            }
        }

        chunk_plan.dense_blocks.resize(kChunkVolume);
        chunk_plan.dense_water_state.resize(kChunkVolume);
        if (!reader.read_bytes(chunk_plan.dense_blocks.data(), chunk_plan.dense_blocks.size() * sizeof(BlockId))) {
            return std::nullopt;
        }
        if (version >= kSaveVersionWaterState) {
            if (!reader.read_bytes(
                    chunk_plan.dense_water_state.data(),
                    chunk_plan.dense_water_state.size() * sizeof(WaterState))) {
                return std::nullopt;
            }
            snapshot.world_save_plan.chunks.push_back(std::move(chunk_plan));
            if (!report_load_progress(input, total_bytes, SaveLoadPhase::ReadingWorld, progress_callback)) {
                return std::nullopt;
            }
            continue;
        }

        std::fill(chunk_plan.dense_water_state.begin(), chunk_plan.dense_water_state.end(), WaterState {0});
        for (std::size_t block_index = 0; block_index < chunk_plan.dense_blocks.size(); ++block_index) {
            auto& block_id = chunk_plan.dense_blocks[block_index];
            if (block_id != to_block_id(BlockType::Water)) {
                continue;
            }
            chunk_plan.dense_water_state[block_index] = make_water_state(kMaxWaterLevel, true);
            block_id = to_block_id(BlockType::Air);
        }
        snapshot.world_save_plan.chunks.push_back(std::move(chunk_plan));
        if (!report_load_progress(input, total_bytes, SaveLoadPhase::ReadingWorld, progress_callback)) {
            return std::nullopt;
        }
    }

    normalize_inventory_state(snapshot.inventory, snapshot.hotbar);
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
    write_sea_adventure_state(writer, snapshot.sea_adventure);

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
        if (!writer.ok()) {
            throw std::runtime_error("Unable to write sparse world save plan chunk");
        }
    }

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
