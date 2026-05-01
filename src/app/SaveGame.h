#pragma once

#include "app/Hotbar.h"
#include "app/InventoryMenu.h"
#include "creatures/CreatureTypes.h"
#include "gameplay/ItemDropSystem.h"
#include "gameplay/PlayerController.h"
#include "world/World.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace valcraft {

constexpr std::size_t kSaveSlotCount = 8;

struct SaveSlotMetadata {
    bool exists = false;
    std::uint64_t saved_at_unix_seconds = 0;
    int seed = 1337;
    float time_of_day = 8.0F;
    float weather_time_seconds = 0.0F;
    std::uint32_t modified_chunk_count = 0;
    bool has_starting_village = false;

    auto operator==(const SaveSlotMetadata&) const -> bool = default;
};

struct SaveGameSnapshot {
    SaveSlotMetadata metadata {};
    glm::vec3 spawn_position {0.5F, 70.0F, 0.5F};
    PlayerState player_state {};
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    std::vector<CreatureInstance> creatures {};
    std::vector<ItemDrop> item_drops {};
    std::vector<WorldChunkSnapshot> chunk_snapshots {};
};

[[nodiscard]] auto save_slot_file_path(const std::filesystem::path& root_directory, std::size_t slot_index)
    -> std::filesystem::path;
[[nodiscard]] auto scan_save_slots(const std::filesystem::path& root_directory)
    -> std::array<SaveSlotMetadata, kSaveSlotCount>;
[[nodiscard]] auto load_save_slot(const std::filesystem::path& root_directory, std::size_t slot_index)
    -> std::optional<SaveGameSnapshot>;
[[nodiscard]] auto remove_save_slot(const std::filesystem::path& root_directory, std::size_t slot_index) -> bool;
void write_save_slot(const std::filesystem::path& root_directory, std::size_t slot_index, const SaveGameSnapshot& snapshot);

} // namespace valcraft
