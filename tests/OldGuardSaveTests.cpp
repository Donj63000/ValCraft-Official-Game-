#include "app/SaveGame.h"
#include "gameplay/OldGuard.h"
#include "gameplay/SeaAdventure.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace valcraft {

namespace {

constexpr auto kSerializedOldGuardMemberBytes =
    sizeof(float) * 8U +
    sizeof(std::uint8_t) * 5U;
constexpr auto kSerializedOldGuardPayloadBytes =
    sizeof(std::uint8_t) +
    sizeof(std::uint64_t) +
    kOldGuardMemberCount *
        kSerializedOldGuardMemberBytes;
constexpr auto kSerializedStatusEntryBytes =
    4U * sizeof(std::uint64_t) +
    sizeof(std::uint8_t) +
    sizeof(float) +
    sizeof(std::uint8_t);
constexpr auto kSerializedSummonedStatsBytes =
    10U * sizeof(float) +
    2U * sizeof(std::uint8_t);
constexpr auto kSerializedSummonedSnapshotBytes =
    sizeof(std::uint8_t) +
    3U * sizeof(std::uint64_t) +
    3U * sizeof(float) +
    sizeof(std::uint8_t) +
    kSerializedSummonedStatsBytes +
    3U * sizeof(double) +
    5U * sizeof(float) +
    2U * sizeof(double) +
    3U * sizeof(std::uint8_t);
constexpr auto kSerializedDefaultRuntimeBytes =
    sizeof(std::uint32_t) +
    kMaximumStatusEffects *
        kSerializedStatusEntryBytes +
    sizeof(double) +
    sizeof(std::uint64_t) +
    sizeof(std::uint64_t) +
    3U * sizeof(float) +
    4U * sizeof(float) +
    sizeof(std::uint8_t) +
    sizeof(std::uint64_t) +
    sizeof(std::uint32_t) +
    kMaximumSavedPlayerSummons *
        (kSerializedSummonedSnapshotBytes +
         sizeof(std::uint8_t) +
         sizeof(float) +
         sizeof(std::uint64_t)) +
    2U * sizeof(std::uint64_t);
constexpr auto kSerializedV14RuntimeExtensionBytes =
    2U * sizeof(std::uint64_t) +
    2U * sizeof(std::uint8_t) +
    kSerializedDefaultRuntimeBytes;

auto temporary_save_root(std::string_view label) -> std::filesystem::path {
    const auto suffix =
        std::to_string(
            static_cast<unsigned long long>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
    return std::filesystem::temp_directory_path() /
           (std::string(label) + "-" + suffix);
}

auto find_departure_extension(
    const std::vector<char>& bytes,
    SeaVoyagePhase phase,
    float elapsed) -> std::vector<char>::const_iterator {
    auto signature =
        std::array<char,
                   sizeof(std::uint8_t) +
                       sizeof(float)> {};
    signature[0] =
        static_cast<char>(
            static_cast<std::uint8_t>(phase));
    std::memcpy(
        signature.data() + sizeof(std::uint8_t),
        &elapsed,
        sizeof(elapsed));
    return std::search(
        bytes.begin(),
        bytes.end(),
        signature.begin(),
        signature.end());
}

auto read_all_bytes(const std::filesystem::path& path)
    -> std::vector<char> {
    const auto byte_count =
        std::filesystem::file_size(path);
    REQUIRE(
        byte_count <=
        (std::numeric_limits<std::size_t>::max)());
    auto bytes =
        std::vector<char>(
            static_cast<std::size_t>(
                byte_count));
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    input.read(
        bytes.data(),
        static_cast<std::streamsize>(
            bytes.size()));
    REQUIRE(
        input.gcount() ==
        static_cast<std::streamsize>(
            bytes.size()));
    return bytes;
}

void erase_item_extension_without_drops(
    std::vector<char>& bytes) {
    constexpr auto magic =
        std::array<char, 4> {'I', 'T', 'E', 'M'};
    constexpr auto extension_byte_count =
        magic.size() +
        kHotbarSlotCount +
        kInventoryStorageSlotCount +
        1U +
        kEquipmentSlotCount +
        sizeof(std::uint64_t);
    REQUIRE(bytes.size() >= extension_byte_count);
    const auto extension_begin =
        bytes.end() -
        static_cast<std::ptrdiff_t>(
            extension_byte_count);
    REQUIRE(std::equal(
        magic.begin(),
        magic.end(),
        extension_begin));
    bytes.erase(extension_begin, bytes.end());
}

void erase_progression_build_extension(
    std::vector<char>& bytes) {
    constexpr auto player_state_byte_count =
        2U * 3U * sizeof(float) +
        19U * sizeof(float) +
        8U * sizeof(std::uint8_t);
    constexpr auto build_offset =
        8U +
        sizeof(std::uint32_t) +
        sizeof(std::uint64_t) +
        sizeof(int) +
        2U * sizeof(float) +
        2U * sizeof(std::uint32_t) +
        3U * sizeof(std::uint8_t) +
        3U * sizeof(float) +
        player_state_byte_count +
        sizeof(std::uint32_t) +
        sizeof(std::uint64_t);
    constexpr auto serialized_v13_build_byte_count =
        kPlayerAttributeCount * sizeof(std::uint8_t) +
        2U * kAbilityCount * sizeof(std::uint8_t) +
        kEquippedAbilitySlotCount * sizeof(std::uint8_t) +
        3U * sizeof(float) +
        kAbilityCount * sizeof(float) +
        kAbilityCount * sizeof(std::uint8_t) +
        kConstructionPlanCount *
            (3U * sizeof(std::uint8_t) +
             kConstructionPlanMaximumCellCount *
                 (3U * sizeof(std::int8_t) +
                  sizeof(std::uint16_t))) +
        sizeof(std::uint8_t) +
        sizeof(std::uint64_t) +
        sizeof(std::uint8_t) +
        sizeof(std::uint64_t);
    constexpr auto serialized_build_byte_count =
        serialized_v13_build_byte_count +
        (7U + kConstructionPlanCount) *
            sizeof(std::uint32_t);
    REQUIRE(
        bytes.size() >=
        build_offset +
            serialized_build_byte_count);
    bytes.erase(
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                build_offset),
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                build_offset +
                serialized_build_byte_count));
}

void write_save_version(
    std::vector<char>& bytes,
    std::uint32_t version) {
    REQUIRE(
        bytes.size() >=
        8U + sizeof(version));
    std::memcpy(
        bytes.data() + 8U,
        &version,
        sizeof(version));
}

void write_all_bytes(
    const std::filesystem::path& path,
    const std::vector<char>& bytes) {
    std::ofstream output(
        path,
        std::ios::binary |
            std::ios::trunc);
    REQUIRE(output.good());
    output.write(
        bytes.data(),
        static_cast<std::streamsize>(
            bytes.size()));
    REQUIRE(output.good());
}

void downgrade_to_version_11(
    const std::filesystem::path& path,
    SeaVoyagePhase phase,
    float elapsed) {
    auto bytes = read_all_bytes(path);
    erase_progression_build_extension(bytes);
    const auto departure =
        find_departure_extension(
            bytes,
            phase,
            elapsed);
    REQUIRE(departure != bytes.end());
    const auto runtime_begin =
        departure +
        static_cast<std::ptrdiff_t>(
            sizeof(std::uint8_t) +
            sizeof(float) +
            kSerializedOldGuardPayloadBytes);
    REQUIRE(
        static_cast<std::size_t>(
            bytes.end() -
            runtime_begin) >=
        kSerializedV14RuntimeExtensionBytes);
    bytes.erase(
        runtime_begin,
        runtime_begin +
            static_cast<std::ptrdiff_t>(
                kSerializedV14RuntimeExtensionBytes));
    erase_item_extension_without_drops(bytes);
    write_save_version(bytes, 11U);
    write_all_bytes(path, bytes);
}

void downgrade_to_version_10(
    const std::filesystem::path& path,
    SeaVoyagePhase phase,
    float elapsed) {
    auto bytes = read_all_bytes(path);
    erase_progression_build_extension(bytes);
    const auto departure =
        find_departure_extension(
            bytes,
            phase,
            elapsed);
    REQUIRE(departure != bytes.end());
    const auto guard_begin =
        departure +
        static_cast<std::ptrdiff_t>(
            sizeof(std::uint8_t) +
            sizeof(float));
    REQUIRE(
        static_cast<std::size_t>(
            bytes.end() - guard_begin) >=
        kSerializedOldGuardPayloadBytes +
            kSerializedV14RuntimeExtensionBytes);
    bytes.erase(
        guard_begin,
        guard_begin +
            static_cast<std::ptrdiff_t>(
                kSerializedOldGuardPayloadBytes +
                kSerializedV14RuntimeExtensionBytes));
    erase_item_extension_without_drops(bytes);
    write_save_version(bytes, 10U);
    write_all_bytes(path, bytes);
}

} // namespace

TEST_CASE("la sauvegarde v11 restaure exactement le rechargement des six gardes") {
    const auto save_root =
        temporary_save_root(
            "valcraft-old-guard-v11");
    std::filesystem::remove_all(save_root);

    OldGuardSystem source {};
    source.reset(42'771);
    auto guard_state =
        source.save_state();
    auto& reloading =
        guard_state.members[2];
    reloading.action =
        OldGuardAction::Reload;
    reloading.action_time = 1.75F;
    reloading.musket_loaded = false;
    reloading.reload_remaining = 3.25F;
    reloading.bayonet_cooldown = 0.45F;

    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 42'771;
    snapshot.metadata.game_mode =
        GameMode::SeaAdventure;
    snapshot.sea_adventure.active = true;
    snapshot.sea_adventure.voyage_phase =
        SeaVoyagePhase::Departing;
    snapshot.sea_adventure.voyage_phase_elapsed =
        2.375F;
    snapshot.sea_adventure.old_guard =
        guard_state;

    write_save_slot(
        save_root,
        0U,
        snapshot);
    downgrade_to_version_11(
        save_slot_file_path(
            save_root,
            0U),
        snapshot.sea_adventure.voyage_phase,
        snapshot.sea_adventure
            .voyage_phase_elapsed);
    const auto loaded =
        load_save_slot(
            save_root,
            0U);
    REQUIRE(loaded.has_value());
    const auto expected =
        sanitize_old_guard_save_state(
            guard_state);
    CHECK(
        loaded->sea_adventure.old_guard ==
        expected);

    SeaAdventureSystem restored {};
    restored.load_state(
        loaded->sea_adventure,
        snapshot.metadata.seed);
    REQUIRE(
        restored.old_guard_members().size() ==
        kOldGuardMemberCount);
    const auto& member =
        restored.old_guard_members()[2];
    CHECK_FALSE(member.musket_loaded);
    CHECK(
        member.action ==
        OldGuardAction::Reload);
    CHECK(
        member.reload_remaining ==
        doctest::Approx(3.25F));
    CHECK(
        member.bayonet_cooldown ==
        doctest::Approx(0.45F));
    CHECK(restored.old_guard_flashes().empty());
    CHECK(restored.old_guard_smoke().empty());

    std::filesystem::remove_all(save_root);
}

TEST_CASE("une sauvegarde v10 reconstruit automatiquement le roster canonique") {
    const auto save_root =
        temporary_save_root(
            "valcraft-old-guard-v10");
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 9'813;
    snapshot.metadata.game_mode =
        GameMode::SeaAdventure;
    snapshot.sea_adventure.active = true;
    snapshot.sea_adventure.voyage_phase =
        SeaVoyagePhase::Departing;
    snapshot.sea_adventure.voyage_phase_elapsed =
        2.375F;
    write_save_slot(
        save_root,
        0U,
        snapshot);

    const auto path =
        save_slot_file_path(
            save_root,
            0U);
    downgrade_to_version_10(
        path,
        snapshot.sea_adventure.voyage_phase,
        snapshot.sea_adventure.voyage_phase_elapsed);

    const auto loaded =
        load_save_slot(
            save_root,
            0U);
    REQUIRE(loaded.has_value());
    CHECK(
        loaded->sea_adventure.old_guard.initialized);
    for (std::size_t index = 0U;
         index < kOldGuardMemberCount;
         ++index) {
        const auto& member =
            loaded->sea_adventure.old_guard
                .members[index];
        CHECK(
            member.id ==
            static_cast<std::uint8_t>(
                index));
        CHECK(
            member.route_index ==
            static_cast<std::uint8_t>(
                index));
        CHECK(member.musket_loaded);
    }

    std::filesystem::remove_all(save_root);
}

TEST_CASE("un payload v11 tronque au milieu des gardes est refuse") {
    const auto save_root =
        temporary_save_root(
            "valcraft-old-guard-truncated");
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 1'309;
    snapshot.metadata.game_mode =
        GameMode::SeaAdventure;
    snapshot.sea_adventure.active = true;
    snapshot.sea_adventure.voyage_phase =
        SeaVoyagePhase::Departing;
    snapshot.sea_adventure.voyage_phase_elapsed =
        2.375F;
    write_save_slot(
        save_root,
        0U,
        snapshot);

    const auto path =
        save_slot_file_path(
            save_root,
            0U);
    downgrade_to_version_11(
        path,
        snapshot.sea_adventure.voyage_phase,
        snapshot.sea_adventure
            .voyage_phase_elapsed);
    auto bytes =
        read_all_bytes(path);
    const auto departure =
        find_departure_extension(
            bytes,
            snapshot.sea_adventure.voyage_phase,
            snapshot.sea_adventure.voyage_phase_elapsed);
    REQUIRE(departure != bytes.end());
    const auto payload_offset =
        static_cast<std::uintmax_t>(
            departure - bytes.begin()) +
        sizeof(std::uint8_t) +
        sizeof(float);
    std::filesystem::resize_file(
        path,
        payload_offset +
            kSerializedOldGuardPayloadBytes /
                2U);

    CHECK_FALSE(
        load_save_slot(
            save_root,
            0U)
            .has_value());

    std::filesystem::remove_all(save_root);
}

} // namespace valcraft
