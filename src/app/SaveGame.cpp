#include "app/SaveGame.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>

namespace valcraft {

namespace {

constexpr std::array<char, 8> kSaveMagic {{'V', 'A', 'L', 'S', 'L', 'O', 'T', '1'}};
constexpr std::uint32_t kSaveVersion = 3;
constexpr std::uint32_t kSaveVersionStartingVillage = 2;
constexpr std::uint32_t kSaveVersionLegacy = 1;

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
}

auto read_creature(BinaryReader& reader, CreatureInstance& creature) -> bool {
    return reader.read_value(creature.anchor.chunk.x) &&
           reader.read_value(creature.anchor.chunk.z) &&
           reader.read_value(creature.anchor.ground_block.x) &&
           reader.read_value(creature.anchor.ground_block.y) &&
           reader.read_value(creature.anchor.ground_block.z) &&
           read_vec3(reader, creature.anchor.spawn_position) &&
           read_enum(reader, creature.anchor.species) &&
           read_vec3(reader, creature.position) &&
           reader.read_value(creature.yaw_radians) &&
           reader.read_value(creature.behavior_timer) &&
           reader.read_value(creature.animation_time) &&
           reader.read_value(creature.wander_heading) &&
           reader.read_value(creature.nervous_intensity) &&
           reader.read_value(creature.behavior_seed) &&
           reader.read_value(creature.appearance_seed) &&
           read_enum(reader, creature.behavior_state) &&
           read_enum(reader, creature.phase) &&
           reader.read_value(creature.morph_factor) &&
           reader.read_value(creature.motion_amount) &&
           reader.read_value(creature.gaze_weight) &&
           reader.read_value(creature.attack_cooldown) &&
           reader.read_value(creature.attack_amount);
}

void write_item_drop(BinaryWriter& writer, const ItemDrop& drop) {
    write_vec3(writer, drop.position);
    write_vec3(writer, drop.velocity);
    write_hotbar_slot(writer, drop.stack);
    writer.write_value(drop.age_seconds);
    writer.write_value(drop.pickup_cooldown);
    write_bool(writer, drop.grounded);
}

auto read_item_drop(BinaryReader& reader, ItemDrop& drop) -> bool {
    return read_vec3(reader, drop.position) &&
           read_vec3(reader, drop.velocity) &&
           read_hotbar_slot(reader, drop.stack) &&
           reader.read_value(drop.age_seconds) &&
           reader.read_value(drop.pickup_cooldown) &&
           read_bool(reader, drop.grounded);
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
        (version != kSaveVersion && version != kSaveVersionStartingVillage && version != kSaveVersionLegacy) ||
        !reader.read_value(metadata.saved_at_unix_seconds) ||
        !reader.read_value(metadata.seed) ||
        !reader.read_value(metadata.time_of_day) ||
        !reader.read_value(metadata.modified_chunk_count)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionStartingVillage && !read_bool(reader, metadata.has_starting_village)) {
        return std::nullopt;
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
    if (slot_index >= kSaveSlotCount) {
        return std::nullopt;
    }

    const auto file_path = save_slot_file_path(root_directory, slot_index);
    std::ifstream input(file_path, std::ios::binary);
    if (!input) {
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

    if (!reader.read_bytes(magic.data(), magic.size()) ||
        magic != kSaveMagic ||
        !reader.read_value(version) ||
        (version != kSaveVersion && version != kSaveVersionStartingVillage && version != kSaveVersionLegacy) ||
        !reader.read_value(snapshot.metadata.saved_at_unix_seconds) ||
        !reader.read_value(snapshot.metadata.seed) ||
        !reader.read_value(snapshot.metadata.time_of_day) ||
        !reader.read_value(snapshot.metadata.modified_chunk_count)) {
        return std::nullopt;
    }
    if (version >= kSaveVersionStartingVillage && !read_bool(reader, snapshot.metadata.has_starting_village)) {
        return std::nullopt;
    }
    if (!read_vec3(reader, snapshot.spawn_position) ||
        !read_player_state(reader, snapshot.player_state)) {
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
    snapshot.inventory.visible = false;
    snapshot.inventory.hovered_slot.reset();

    if (!reader.read_value(creature_count)) {
        return std::nullopt;
    }
    snapshot.creatures.resize(creature_count);
    for (auto& creature : snapshot.creatures) {
        if (!read_creature(reader, creature)) {
            return std::nullopt;
        }
    }

    if (!reader.read_value(item_drop_count)) {
        return std::nullopt;
    }
    snapshot.item_drops.resize(item_drop_count);
    for (auto& item_drop : snapshot.item_drops) {
        if (!read_item_drop(reader, item_drop)) {
            return std::nullopt;
        }
    }

    if (!reader.read_value(chunk_count)) {
        return std::nullopt;
    }
    snapshot.chunk_snapshots.resize(chunk_count);
    for (auto& chunk_snapshot : snapshot.chunk_snapshots) {
        if (!reader.read_value(chunk_snapshot.coord.x) ||
            !reader.read_value(chunk_snapshot.coord.z) ||
            !reader.read_bytes(chunk_snapshot.blocks.data(), chunk_snapshot.blocks.size() * sizeof(BlockId))) {
            return std::nullopt;
        }
        if (version >= kSaveVersion) {
            if (!reader.read_bytes(chunk_snapshot.water_state.data(), chunk_snapshot.water_state.size() * sizeof(WaterState))) {
                return std::nullopt;
            }
            continue;
        }

        chunk_snapshot.water_state.fill(0);
        for (std::size_t block_index = 0; block_index < chunk_snapshot.blocks.size(); ++block_index) {
            auto& block_id = chunk_snapshot.blocks[block_index];
            if (block_id != to_block_id(BlockType::Water)) {
                continue;
            }
            chunk_snapshot.water_state[block_index] = make_water_state(kMaxWaterLevel, true);
            block_id = to_block_id(BlockType::Air);
        }
    }

    normalize_inventory_state(snapshot.inventory, snapshot.hotbar);
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

void write_save_slot(const std::filesystem::path& root_directory, std::size_t slot_index, const SaveGameSnapshot& snapshot) {
    if (slot_index >= kSaveSlotCount) {
        return;
    }

    std::error_code error_code;
    std::filesystem::create_directories(root_directory, error_code);

    const auto file_path = save_slot_file_path(root_directory, slot_index);
    const auto temp_path = file_path.string() + ".tmp";
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
    const auto chunk_count = static_cast<std::uint32_t>(std::min<std::size_t>(
        snapshot.chunk_snapshots.size(),
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
    const auto creature_count = static_cast<std::uint32_t>(std::min<std::size_t>(
        snapshot.creatures.size(),
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
    const auto item_drop_count = static_cast<std::uint32_t>(std::min<std::size_t>(
        snapshot.item_drops.size(),
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));

    writer.write_bytes(kSaveMagic.data(), kSaveMagic.size());
    writer.write_value(kSaveVersion);
    writer.write_value(saved_at);
    writer.write_value(snapshot.metadata.seed);
    writer.write_value(snapshot.metadata.time_of_day);
    writer.write_value(chunk_count);
    write_bool(writer, snapshot.metadata.has_starting_village);
    write_vec3(writer, snapshot.spawn_position);
    write_player_state(writer, snapshot.player_state);

    for (const auto& slot : snapshot.hotbar.slots) {
        write_hotbar_slot(writer, slot);
    }
    writer.write_value(static_cast<std::uint32_t>(snapshot.hotbar.selected_index));

    for (const auto& slot : snapshot.inventory.storage_slots) {
        write_hotbar_slot(writer, slot);
    }
    write_hotbar_slot(writer, snapshot.inventory.carried_slot);
    write_bool(writer, snapshot.inventory.carrying_item);

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
        const auto& chunk_snapshot = snapshot.chunk_snapshots[index];
        writer.write_value(chunk_snapshot.coord.x);
        writer.write_value(chunk_snapshot.coord.z);
        writer.write_bytes(chunk_snapshot.blocks.data(), chunk_snapshot.blocks.size() * sizeof(BlockId));
        writer.write_bytes(chunk_snapshot.water_state.data(), chunk_snapshot.water_state.size() * sizeof(WaterState));
    }

    output.flush();
    if (!writer.ok() || !output.good()) {
        throw std::runtime_error("Unable to write save slot data");
    }
    output.close();

    std::filesystem::remove(file_path, error_code);
    std::filesystem::rename(temp_path, file_path, error_code);
    if (error_code) {
        std::filesystem::remove(temp_path, error_code);
        throw std::runtime_error("Unable to finalize save slot data");
    }
}

} // namespace valcraft
