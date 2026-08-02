#include "app/ConfirmDialog.h"
#include "app/DeathScreen.h"
#include "app/GameBranding.h"
#include "app/GameOptions.h"
#include "app/GameLoop.h"
#include "app/Hotbar.h"
#include "app/InputBindings.h"
#include "app/InventoryMenu.h"
#include "app/MainMenu.h"
#include "app/OptionsMenu.h"
#include "app/PauseMenu.h"
#include "app/PerformanceReport.h"
#include "app/ProcessMemory.h"
#include "app/SaveGame.h"
#include "app/SaveSlotMenu.h"
#include "app/SessionSaveState.h"
#include "app/StreamingFocus.h"
#include "gameplay/SeaAdventure.h"
#include "render/HotbarLayout.h"
#include "world/Environment.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

namespace valcraft {

namespace {

template <typename T, std::size_t Size>
void overwrite_unique_binary_sequence(const std::filesystem::path& path,
                                      const std::array<T, Size>& expected,
                                      const std::array<T, Size>& replacement) {
    static_assert(std::is_trivially_copyable_v<T>);
    constexpr auto byte_count = sizeof(T) * Size;
    auto expected_bytes = std::array<char, byte_count> {};
    auto replacement_bytes = std::array<char, byte_count> {};
    std::memcpy(expected_bytes.data(), expected.data(), byte_count);
    std::memcpy(replacement_bytes.data(), replacement.data(), byte_count);

    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    const auto raw_size = std::filesystem::file_size(path);
    REQUIRE(raw_size <= (std::numeric_limits<std::size_t>::max)());
    auto bytes = std::vector<char>(static_cast<std::size_t>(raw_size));
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(input.gcount() == static_cast<std::streamsize>(bytes.size()));

    const auto match = std::search(bytes.begin(), bytes.end(), expected_bytes.begin(), expected_bytes.end());
    REQUIRE(match != bytes.end());
    REQUIRE(std::search(match + 1, bytes.end(), expected_bytes.begin(), expected_bytes.end()) == bytes.end());

    std::fstream output(path, std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(output.good());
    output.seekp(static_cast<std::streamoff>(match - bytes.begin()));
    output.write(replacement_bytes.data(), static_cast<std::streamsize>(replacement_bytes.size()));
    REQUIRE(output.good());
}

template <typename T, std::size_t Size>
void truncate_inside_unique_binary_sequence(const std::filesystem::path& path,
                                             const std::array<T, Size>& expected) {
    static_assert(std::is_trivially_copyable_v<T>);
    constexpr auto byte_count = sizeof(T) * Size;
    auto expected_bytes = std::array<char, byte_count> {};
    std::memcpy(expected_bytes.data(), expected.data(), byte_count);

    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    const auto raw_size = std::filesystem::file_size(path);
    REQUIRE(raw_size <= (std::numeric_limits<std::size_t>::max)());
    auto bytes = std::vector<char>(static_cast<std::size_t>(raw_size));
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(input.gcount() == static_cast<std::streamsize>(bytes.size()));

    const auto match = std::search(bytes.begin(), bytes.end(), expected_bytes.begin(), expected_bytes.end());
    REQUIRE(match != bytes.end());
    REQUIRE(std::search(match + 1, bytes.end(), expected_bytes.begin(), expected_bytes.end()) == bytes.end());
    const auto sequence_offset = static_cast<std::uintmax_t>(match - bytes.begin());
    input.close();
    // Je coupe au milieu du champ cible pour prouver que le lecteur refuse un
    // roster v9 incomplet au lieu de prendre la hotbar pour la suite du membre.
    std::filesystem::resize_file(path, sequence_offset + byte_count / 2U);
}

constexpr auto kSerializedV13BuildByteCount =
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
constexpr auto kSerializedV14BuildByteCount =
    kSerializedV13BuildByteCount +
    (7U + kConstructionPlanCount) *
        sizeof(std::uint32_t);
constexpr auto kSerializedSeaV11ByteCount =
    79U +
    (sizeof(std::uint8_t) +
     sizeof(std::uint64_t) +
     kShipCrewMemberCount * 48U) +
    sizeof(std::uint8_t) +
    sizeof(float) +
    (sizeof(std::uint8_t) +
     sizeof(std::uint64_t) +
     kOldGuardMemberCount * 37U);
constexpr auto kSerializedStatusEntryByteCount =
    4U * sizeof(std::uint64_t) +
    sizeof(std::uint8_t) +
    sizeof(float) +
    sizeof(std::uint8_t);
constexpr auto kSerializedSummonedStatsByteCount =
    10U * sizeof(float) +
    2U * sizeof(std::uint8_t);
constexpr auto kSerializedSummonedSnapshotByteCount =
    sizeof(std::uint8_t) +
    3U * sizeof(std::uint64_t) +
    3U * sizeof(float) +
    sizeof(std::uint8_t) +
    kSerializedSummonedStatsByteCount +
    3U * sizeof(double) +
    5U * sizeof(float) +
    2U * sizeof(double) +
    3U * sizeof(std::uint8_t);
constexpr auto kSerializedDefaultRuntimeByteCount =
    sizeof(std::uint32_t) +
    kMaximumStatusEffects *
        kSerializedStatusEntryByteCount +
    sizeof(double) +
    sizeof(std::uint64_t) +
    sizeof(std::uint64_t) +
    3U * sizeof(float) +
    4U * sizeof(float) +
    sizeof(std::uint8_t) +
    sizeof(std::uint64_t) +
    sizeof(std::uint32_t) +
    kMaximumSavedPlayerSummons *
        (kSerializedSummonedSnapshotByteCount +
         sizeof(std::uint8_t) +
         sizeof(float) +
         sizeof(std::uint64_t)) +
    2U * sizeof(std::uint64_t);
constexpr auto kSerializedMaritimeExperienceByteCount =
    2U * sizeof(std::uint64_t) +
    2U * sizeof(std::uint8_t);
constexpr auto kSerializedPlayerStateByteCount =
    2U * 3U * sizeof(float) +
    19U * sizeof(float) +
    8U * sizeof(std::uint8_t);
constexpr auto kSerializedBuildOffset =
    8U +
    sizeof(std::uint32_t) +
    sizeof(std::uint64_t) +
    sizeof(int) +
    2U * sizeof(float) +
    2U * sizeof(std::uint32_t) +
    3U * sizeof(std::uint8_t) +
    3U * sizeof(float) +
    kSerializedPlayerStateByteCount +
    sizeof(std::uint32_t) +
    sizeof(std::uint64_t);
constexpr auto kSerializedLegendaryWeaponExtensionByteCount =
    4U +
    sizeof(std::uint8_t) +
    sizeof(std::uint64_t) +
    3U * sizeof(std::uint8_t) +
    2U * sizeof(std::uint32_t) +
    sizeof(std::uint8_t) +
    4U * sizeof(std::uint8_t);
constexpr auto kSerializedBackroomsFlashlightExtensionByteCount =
    4U +
    sizeof(std::uint8_t) +
    sizeof(float) +
    sizeof(std::uint8_t);
constexpr auto kSerializedBackroomsJackExtensionByteCount = 146U;
constexpr auto kSerializedBackroomsLevelExtensionByteCount =
    4U + sizeof(std::uint8_t) + sizeof(std::int32_t);
constexpr auto kSerializedBackroomsMarlowExtensionByteCount = 40U;

void remove_current_backrooms_marlow_extension(
    std::vector<char>& bytes) {
    constexpr auto magic =
        std::array<char, 4> {'M', 'R', 'L', 'W'};
    REQUIRE(
        bytes.size() >=
        kSerializedBackroomsMarlowExtensionByteCount);
    const auto extension_begin =
        bytes.end() -
        static_cast<std::ptrdiff_t>(
            kSerializedBackroomsMarlowExtensionByteCount);
    REQUIRE(
        std::equal(
            magic.begin(),
            magic.end(),
            extension_begin));
    bytes.erase(extension_begin, bytes.end());
}

void remove_current_backrooms_jack_extension(
    std::vector<char>& bytes) {
    // Je retire d'abord MRLW pour retrouver la terminaison BRLV historique.
    remove_current_backrooms_marlow_extension(bytes);
    constexpr auto level_magic =
        std::array<char, 4> {
            'B',
            'R',
            'L',
            'V',
        };
    REQUIRE(
        bytes.size() >=
        kSerializedBackroomsLevelExtensionByteCount);
    const auto level_extension_begin =
        bytes.end() -
        static_cast<std::ptrdiff_t>(
            kSerializedBackroomsLevelExtensionByteCount);
    REQUIRE(
        std::equal(
            level_magic.begin(),
            level_magic.end(),
            level_extension_begin));
    bytes.erase(
        level_extension_begin,
        bytes.end());

    constexpr auto magic =
        std::array<char, 4> {
            'B',
            'J',
            'C',
            'K',
        };
    REQUIRE(
        bytes.size() >=
        kSerializedBackroomsJackExtensionByteCount);
    const auto extension_begin =
        bytes.end() -
        static_cast<std::ptrdiff_t>(
            kSerializedBackroomsJackExtensionByteCount);
    REQUIRE(
        std::equal(
            magic.begin(),
            magic.end(),
            extension_begin));
    bytes.erase(
        extension_begin,
        bytes.end());
}

void remove_current_backrooms_flashlight_extension(
    std::vector<char>& bytes) {
    // Je retire d'abord BRLV puis Jack pour retrouver exactement la
    // terminaison BFLH du format v16.
    remove_current_backrooms_jack_extension(bytes);
    constexpr auto magic =
        std::array<char, 4> {
            'B',
            'F',
            'L',
            'H',
        };
    REQUIRE(
        bytes.size() >=
        kSerializedBackroomsFlashlightExtensionByteCount);
    const auto extension_begin =
        bytes.end() -
        static_cast<std::ptrdiff_t>(
            kSerializedBackroomsFlashlightExtensionByteCount);
    REQUIRE(
        std::equal(
            magic.begin(),
            magic.end(),
            extension_begin));
    bytes.erase(
        extension_begin,
        bytes.end());
}

void remove_current_legendary_weapon_extension(
    std::vector<char>& bytes) {
    // Je retire d'abord l'extension v16 qui suit désormais LWEA.
    remove_current_backrooms_flashlight_extension(
        bytes);
    constexpr auto magic =
        std::array<char, 4> {
            'L',
            'W',
            'E',
            'A',
        };
    REQUIRE(
        bytes.size() >=
        kSerializedLegendaryWeaponExtensionByteCount);
    const auto extension_begin =
        bytes.end() -
        static_cast<std::ptrdiff_t>(
            kSerializedLegendaryWeaponExtensionByteCount);
    REQUIRE(
        std::equal(
            magic.begin(),
            magic.end(),
            extension_begin));
    bytes.erase(
        extension_begin,
        bytes.end());
}

void downgrade_current_save_to_v15(
    const std::filesystem::path& path) {
    std::ifstream input(
        path,
        std::ios::binary);
    REQUIRE(input.good());
    const auto raw_size =
        std::filesystem::file_size(path);
    REQUIRE(
        raw_size <=
        (std::numeric_limits<
             std::size_t>::max)());
    auto bytes =
        std::vector<char>(
            static_cast<std::size_t>(
                raw_size));
    input.read(
        bytes.data(),
        static_cast<std::streamsize>(
            bytes.size()));
    REQUIRE(
        input.gcount() ==
        static_cast<std::streamsize>(
            bytes.size()));
    input.close();

    remove_current_backrooms_flashlight_extension(
        bytes);
    constexpr auto version =
        std::uint32_t {15U};
    std::memcpy(
        bytes.data() + 8U,
        &version,
        sizeof(version));
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

void downgrade_current_save_to_v14(
    const std::filesystem::path& path) {
    std::ifstream input(
        path,
        std::ios::binary);
    REQUIRE(input.good());
    const auto raw_size =
        std::filesystem::file_size(path);
    REQUIRE(
        raw_size <=
        (std::numeric_limits<
             std::size_t>::max)());
    auto bytes =
        std::vector<char>(
            static_cast<std::size_t>(
                raw_size));
    input.read(
        bytes.data(),
        static_cast<std::streamsize>(
            bytes.size()));
    REQUIRE(
        input.gcount() ==
        static_cast<std::streamsize>(
            bytes.size()));
    input.close();

    remove_current_legendary_weapon_extension(
        bytes);
    REQUIRE(
        bytes.size() >=
        8U + sizeof(std::uint32_t));
    constexpr auto version =
        std::uint32_t {14U};
    std::memcpy(
        bytes.data() + 8U,
        &version,
        sizeof(version));

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

void downgrade_current_save_to_v13(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    const auto raw_size =
        std::filesystem::file_size(path);
    REQUIRE(
        raw_size <=
        (std::numeric_limits<
             std::size_t>::max)());
    auto bytes =
        std::vector<char>(
            static_cast<std::size_t>(
                raw_size));
    input.read(
        bytes.data(),
        static_cast<std::streamsize>(
            bytes.size()));
    REQUIRE(
        input.gcount() ==
        static_cast<std::streamsize>(
            bytes.size()));
    input.close();
    remove_current_legendary_weapon_extension(
        bytes);

    auto cursor =
        static_cast<std::size_t>(
            kSerializedBuildOffset);
    auto v13_build = std::vector<char> {};
    v13_build.reserve(
        kSerializedV13BuildByteCount);
    const auto read_expected_count =
        [&](std::size_t expected) {
            REQUIRE(
                cursor +
                    sizeof(std::uint32_t) <=
                bytes.size());
            auto count =
                std::uint32_t {0U};
            std::memcpy(
                &count,
                bytes.data() + cursor,
                sizeof(count));
            REQUIRE(
                count ==
                static_cast<std::uint32_t>(
                    expected));
            cursor += sizeof(count);
        };
    const auto append_payload =
        [&](std::size_t byte_count) {
            REQUIRE(
                cursor + byte_count <=
                bytes.size());
            v13_build.insert(
                v13_build.end(),
                bytes.begin() +
                    static_cast<
                        std::ptrdiff_t>(
                        cursor),
                bytes.begin() +
                    static_cast<
                        std::ptrdiff_t>(
                        cursor +
                        byte_count));
            cursor += byte_count;
        };

    read_expected_count(
        kPlayerAttributeCount);
    append_payload(
        kPlayerAttributeCount *
        sizeof(std::uint8_t));
    read_expected_count(kAbilityCount);
    append_payload(
        kAbilityCount *
        sizeof(std::uint8_t));
    read_expected_count(kAbilityCount);
    append_payload(
        kAbilityCount *
        sizeof(std::uint8_t));
    read_expected_count(
        kEquippedAbilitySlotCount);
    append_payload(
        kEquippedAbilitySlotCount *
        sizeof(std::uint8_t));
    append_payload(3U * sizeof(float));
    read_expected_count(kAbilityCount);
    append_payload(
        kAbilityCount * sizeof(float));
    read_expected_count(kAbilityCount);
    append_payload(
        kAbilityCount *
        sizeof(std::uint8_t));
    read_expected_count(
        kConstructionPlanCount);
    for (std::size_t plan = 0U;
         plan < kConstructionPlanCount;
         ++plan) {
        append_payload(
            3U * sizeof(std::uint8_t));
        read_expected_count(
            kConstructionPlanMaximumCellCount);
        append_payload(
            kConstructionPlanMaximumCellCount *
            (3U * sizeof(std::int8_t) +
             sizeof(std::uint16_t)));
    }
    append_payload(
        sizeof(std::uint8_t) +
        sizeof(std::uint64_t) +
        sizeof(std::uint8_t) +
        sizeof(std::uint64_t));
    REQUIRE(
        v13_build.size() ==
        kSerializedV13BuildByteCount);
    REQUIRE(
        cursor ==
        kSerializedBuildOffset +
            kSerializedV14BuildByteCount);

    bytes.erase(
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                kSerializedBuildOffset),
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                cursor));
    bytes.insert(
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                kSerializedBuildOffset),
        v13_build.begin(),
        v13_build.end());

    // Je ne destine ce convertisseur qu'aux fixtures sans runtime actif :
    // leur extension v14 a donc une taille fixe et reproductible.
    constexpr auto runtime_extension_byte_count =
        kSerializedMaritimeExperienceByteCount +
        kSerializedDefaultRuntimeByteCount;
    const auto runtime_offset =
        kSerializedBuildOffset +
        kSerializedV13BuildByteCount +
        kSerializedSeaV11ByteCount;
    REQUIRE(
        bytes.size() >=
        runtime_offset +
            runtime_extension_byte_count);
    bytes.erase(
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                runtime_offset),
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                runtime_offset +
                runtime_extension_byte_count));

    constexpr auto version =
        std::uint32_t {13U};
    std::memcpy(
        bytes.data() + 8U,
        &version,
        sizeof(version));
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

void downgrade_current_save_to_v11(
    const std::filesystem::path& path,
    std::size_t item_drop_count = 0U) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    const auto raw_size = std::filesystem::file_size(path);
    REQUIRE(raw_size <= (std::numeric_limits<std::size_t>::max)());
    auto bytes = std::vector<char>(static_cast<std::size_t>(raw_size));
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(input.gcount() == static_cast<std::streamsize>(bytes.size()));
    input.close();
    remove_current_legendary_weapon_extension(
        bytes);

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
    REQUIRE(
        bytes.size() >=
        build_offset +
            kSerializedV14BuildByteCount);
    // Je retire le bloc append-only v14 avant de changer la version ; sinon
    // un lecteur v11 prendrait ces octets pour l'état maritime.
    bytes.erase(
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                build_offset),
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                build_offset +
                kSerializedV14BuildByteCount));

    constexpr auto runtime_extension_byte_count =
        kSerializedMaritimeExperienceByteCount +
        kSerializedDefaultRuntimeByteCount;
    const auto runtime_offset =
        build_offset +
        kSerializedSeaV11ByteCount;
    REQUIRE(
        bytes.size() >=
        runtime_offset +
            runtime_extension_byte_count);
    bytes.erase(
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                runtime_offset),
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                runtime_offset +
                runtime_extension_byte_count));

    constexpr auto magic = std::array<char, 4> {'I', 'T', 'E', 'M'};
    const auto extension_byte_count =
        magic.size() +
        kHotbarSlotCount +
        kInventoryStorageSlotCount +
        1U +
        kEquipmentSlotCount +
        item_drop_count +
        sizeof(std::uint64_t);
    REQUIRE(bytes.size() >= extension_byte_count);
    const auto extension_begin =
        bytes.end() -
        static_cast<std::ptrdiff_t>(extension_byte_count);
    REQUIRE(std::equal(
        magic.begin(),
        magic.end(),
        extension_begin));
    bytes.erase(extension_begin, bytes.end());

    REQUIRE(bytes.size() >= 8U + sizeof(std::uint32_t));
    constexpr std::uint32_t version = 11U;
    std::memcpy(bytes.data() + 8U, &version, sizeof(version));

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

void downgrade_current_sea_save_to_v9(const std::filesystem::path& path,
                                      SeaVoyagePhase phase,
                                      float phase_elapsed) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    const auto raw_size = std::filesystem::file_size(path);
    REQUIRE(raw_size <= (std::numeric_limits<std::size_t>::max)());
    auto bytes = std::vector<char>(static_cast<std::size_t>(raw_size));
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(input.gcount() == static_cast<std::streamsize>(bytes.size()));
    input.close();
    remove_current_legendary_weapon_extension(
        bytes);

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
    REQUIRE(
        bytes.size() >=
        build_offset +
            kSerializedV14BuildByteCount);
    bytes.erase(
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                build_offset),
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                build_offset +
                kSerializedV14BuildByteCount));

    auto extension_prefix = std::array<
        char,
        sizeof(std::uint8_t) +
            sizeof(float) +
            sizeof(std::uint8_t) +
            sizeof(std::uint64_t)> {};
    extension_prefix[0] = static_cast<char>(phase);
    std::memcpy(
        extension_prefix.data() + sizeof(std::uint8_t),
        &phase_elapsed,
        sizeof(float));
    extension_prefix[sizeof(std::uint8_t) + sizeof(float)] = 1;
    const auto patrol_revision = kOldGuardPatrolRevision;
    std::memcpy(
        extension_prefix.data() +
            sizeof(std::uint8_t) +
            sizeof(float) +
            sizeof(std::uint8_t),
        &patrol_revision,
        sizeof(patrol_revision));
    const auto match = std::search(
        bytes.begin(),
        bytes.end(),
        extension_prefix.begin(),
        extension_prefix.end());
    REQUIRE(match != bytes.end());
    REQUIRE(
        std::search(
            match + 1,
            bytes.end(),
            extension_prefix.begin(),
            extension_prefix.end()) ==
        bytes.end());

    constexpr auto old_guard_member_bytes =
        sizeof(float) * 8U +
        sizeof(std::uint8_t) * 5U;
    constexpr auto departure_payload_bytes =
        sizeof(std::uint8_t) + sizeof(float);
    constexpr auto old_guard_payload_bytes =
        sizeof(std::uint8_t) +
        sizeof(std::uint64_t) +
        kOldGuardMemberCount * old_guard_member_bytes;
    // Je retire les extensions v10, v11 et v14 placées après le roster
    // historique avant de rétablir l'entête v9.
    bytes.erase(
        match,
        match +
            static_cast<std::ptrdiff_t>(
                departure_payload_bytes +
                old_guard_payload_bytes +
                kSerializedMaritimeExperienceByteCount +
                kSerializedDefaultRuntimeByteCount));
    constexpr auto item_extension_byte_count =
        4U +
        kHotbarSlotCount +
        kInventoryStorageSlotCount +
        1U +
        kEquipmentSlotCount +
        sizeof(std::uint64_t);
    REQUIRE(bytes.size() >= item_extension_byte_count);
    const auto item_extension_begin =
        bytes.end() -
        static_cast<std::ptrdiff_t>(
            item_extension_byte_count);
    constexpr auto item_magic =
        std::array<char, 4> {'I', 'T', 'E', 'M'};
    REQUIRE(std::equal(
        item_magic.begin(),
        item_magic.end(),
        item_extension_begin));
    bytes.erase(item_extension_begin, bytes.end());
    REQUIRE(bytes.size() >= 8U + sizeof(std::uint32_t));
    constexpr std::uint32_t version = 9U;
    std::memcpy(bytes.data() + 8U, &version, sizeof(version));

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

void overwrite_save_generation_version(const std::filesystem::path& path,
                                       WorldGenerationVersion generation_version) {
    // Je cible l'offset stable du champ v8+ : magic, version, horodatage,
    // seed, heure, compte chunks, village, meteo, mode puis profil.
    constexpr auto generation_version_offset = std::streamoff {39};
    const auto raw_version = static_cast<std::uint32_t>(generation_version);
    std::fstream output(path, std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(output.good());
    output.seekp(generation_version_offset);
    output.write(reinterpret_cast<const char*>(&raw_version), sizeof(raw_version));
    REQUIRE(output.good());
}

struct HistoricalSaveFixtureV2ToV6 {
    std::uint32_t version = 2U;
    std::uint64_t saved_at = 0U;
    int seed = 0;
    float time_of_day = 0.0F;
    std::uint32_t modified_chunk_count = 1U;
    bool has_starting_village = true;
    float weather_time_seconds = 0.0F;
    glm::vec3 spawn_position {0.0F};
    PlayerState player_state {};
    PlayerProgressionState progression {};
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    CreatureInstance creature {};
    ItemDrop item_drop {};
    ChunkCoord chunk_coord {};
    std::vector<BlockId> dense_blocks {};
    std::vector<WaterState> dense_water_state {};
    std::size_t solid_index = 0U;
    std::size_t water_index = 0U;
};

[[nodiscard]] auto make_historical_save_fixture_v2_to_v6(
    std::uint32_t version) -> HistoricalSaveFixtureV2ToV6 {
    REQUIRE(version >= 2U);
    REQUIRE(version <= 6U);

    HistoricalSaveFixtureV2ToV6 fixture {};
    fixture.version = version;
    fixture.saved_at = 1'712'185'200ULL + version;
    fixture.seed = 24'600 + static_cast<int>(version);
    fixture.time_of_day = 5.25F + static_cast<float>(version);
    fixture.weather_time_seconds =
        91.5F + static_cast<float>(version);
    fixture.spawn_position = {
        18.5F + static_cast<float>(version),
        73.0F,
        -14.5F,
    };

    fixture.player_state.position = fixture.spawn_position;
    fixture.player_state.velocity = {0.25F, -0.5F, 0.75F};
    fixture.player_state.yaw_degrees = 127.5F;
    fixture.player_state.pitch_degrees = -18.25F;
    fixture.player_state.body_yaw_degrees = 91.0F;
    fixture.player_state.animation_time = 14.75F;
    fixture.player_state.step_phase = 0.625F;
    fixture.player_state.health = 17.5F;
    fixture.player_state.air_seconds = 8.25F;
    fixture.player_state.on_ground = true;
    fixture.player_state.fly_mode = true;

    fixture.progression = {6U, 42ULL};

    fixture.hotbar.slots.fill(empty_item_stack());
    fixture.hotbar.slots[5] =
        make_item_stack(to_block_id(BlockType::Torch), 7U);
    fixture.hotbar.selected_index = 5U;

    fixture.inventory.storage_slots.fill(inventory_empty_slot());
    fixture.inventory.equipment_slots.fill(inventory_empty_slot());
    fixture.inventory.storage_slots[11] =
        inventory_make_slot(
            to_block_id(BlockType::DiamondOre),
            3U);
    fixture.inventory.carried_slot =
        inventory_make_slot(
            to_block_id(BlockType::Wood),
            2U);
    fixture.inventory.carrying_item = true;
    fixture.inventory
        .equipment_slots[equipment_slot_index(EquipmentSlot::Chest)] =
        inventory_make_slot(
            to_block_id(BlockType::Pastron),
            1U);

    fixture.creature.anchor.chunk = {2, -3};
    fixture.creature.anchor.ground_block = {37, 64, -42};
    fixture.creature.anchor.spawn_position = {37.5F, 65.0F, -41.5F};
    fixture.creature.anchor.species = CreatureSpecies::Cow;
    fixture.creature.position = {38.25F, 65.0F, -40.75F};
    fixture.creature.yaw_radians = 1.125F;
    fixture.creature.behavior_timer = 2.25F;
    fixture.creature.animation_time = 3.5F;
    fixture.creature.wander_heading = -0.75F;
    fixture.creature.nervous_intensity = 0.375F;
    fixture.creature.behavior_seed = 0x1020'3040U;
    fixture.creature.appearance_seed = 0x5060'7080U;
    fixture.creature.behavior_state = CreatureBehaviorState::Wander;
    fixture.creature.phase = CreaturePhase::Day;
    fixture.creature.morph_factor = 0.125F;
    fixture.creature.motion_amount = 0.625F;
    fixture.creature.gaze_weight = 0.25F;
    fixture.creature.attack_cooldown = 1.75F;
    fixture.creature.attack_amount = 0.5F;
    fixture.creature.health = 4.5F;

    fixture.item_drop.position = {21.5F, 74.25F, -12.5F};
    fixture.item_drop.velocity = {0.5F, 1.25F, -0.75F};
    fixture.item_drop.stack =
        inventory_make_slot(
            to_block_id(BlockType::IronOre),
            3U);
    fixture.item_drop.age_seconds = 12.5F;
    fixture.item_drop.pickup_cooldown = 0.625F;
    fixture.item_drop.grounded = false;

    fixture.chunk_coord = {-2, 4};
    fixture.dense_blocks.assign(
        kChunkVolume,
        to_block_id(BlockType::Air));
    fixture.dense_water_state.assign(
        kChunkVolume,
        WaterState {0});
    fixture.solid_index = 17U;
    fixture.water_index = 31U;
    fixture.dense_blocks[fixture.solid_index] =
        to_block_id(BlockType::Stone);
    if (version < 3U) {
        // Je reproduis l'encodage v2 : l'eau etait encore un bloc que le
        // lecteur migre ensuite vers un etat liquide source.
        fixture.dense_blocks[fixture.water_index] =
            to_block_id(BlockType::Water);
    } else {
        fixture.dense_water_state[fixture.water_index] =
            make_water_state(5U, true);
    }
    return fixture;
}

void write_historical_save_fixture_v2_to_v6(
    const std::filesystem::path& path,
    const HistoricalSaveFixtureV2ToV6& fixture) {
    REQUIRE(fixture.version >= 2U);
    REQUIRE(fixture.version <= 6U);
    REQUIRE(fixture.dense_blocks.size() == kChunkVolume);
    REQUIRE(fixture.dense_water_state.size() == kChunkVolume);

    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(
        path,
        std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());

    const auto write_bytes =
        [&](const void* data, std::size_t size) {
            output.write(
                reinterpret_cast<const char*>(data),
                static_cast<std::streamsize>(size));
        };
    const auto write_value = [&](const auto& value) {
        write_bytes(&value, sizeof(value));
    };
    const auto write_bool = [&](bool value) {
        write_value(
            static_cast<std::uint8_t>(
                value ? 1U : 0U));
    };
    const auto write_enum = [&](auto value) {
        using Enum = decltype(value);
        using Underlying = std::underlying_type_t<Enum>;
        write_value(static_cast<Underlying>(value));
    };
    const auto write_vec3 = [&](const glm::vec3& value) {
        write_value(value.x);
        write_value(value.y);
        write_value(value.z);
    };
    const auto write_hotbar_slot = [&](const HotbarSlot& slot) {
        write_value(slot.block_id);
        write_value(slot.count);
    };
    const auto write_player_state = [&](const PlayerState& state) {
        write_vec3(state.position);
        write_vec3(state.velocity);
        write_value(state.yaw_degrees);
        write_value(state.pitch_degrees);
        write_value(state.body_yaw_degrees);
        write_value(state.animation_time);
        write_value(state.step_phase);
        write_value(state.health);
        write_value(state.air_seconds);
        write_value(state.hurt_timer);
        write_value(state.damage_cooldown);
        write_value(state.regen_delay);
        write_value(state.regen_tick_timer);
        write_value(state.drowning_tick_timer);
        write_value(state.fall_start_y);
        write_value(state.primary_action_progress);
        write_value(state.secondary_action_progress);
        write_value(state.landing_impact);
        write_value(state.airborne_time);
        write_value(state.look_sway_yaw);
        write_value(state.look_sway_pitch);
        write_bool(state.on_ground);
        write_bool(state.fly_mode);
        write_bool(state.head_underwater);
        write_bool(state.swimming);
        write_bool(state.primary_action_active);
        write_bool(state.secondary_action_active);
        write_bool(state.dead);
        write_enum(state.death_cause);
    };
    const auto write_creature =
        [&](const CreatureInstance& creature) {
            write_value(creature.anchor.chunk.x);
            write_value(creature.anchor.chunk.z);
            write_value(creature.anchor.ground_block.x);
            write_value(creature.anchor.ground_block.y);
            write_value(creature.anchor.ground_block.z);
            write_vec3(creature.anchor.spawn_position);
            write_enum(creature.anchor.species);
            write_vec3(creature.position);
            write_value(creature.yaw_radians);
            write_value(creature.behavior_timer);
            write_value(creature.animation_time);
            write_value(creature.wander_heading);
            write_value(creature.nervous_intensity);
            write_value(creature.behavior_seed);
            write_value(creature.appearance_seed);
            write_enum(creature.behavior_state);
            write_enum(creature.phase);
            write_value(creature.morph_factor);
            write_value(creature.motion_amount);
            write_value(creature.gaze_weight);
            write_value(creature.attack_cooldown);
            write_value(creature.attack_amount);
            if (fixture.version >= 5U) {
                write_value(creature.health);
            }
        };
    const auto write_item_drop = [&](const ItemDrop& drop) {
        write_vec3(drop.position);
        write_vec3(drop.velocity);
        write_hotbar_slot(drop.stack);
        write_value(drop.age_seconds);
        write_value(drop.pickup_cooldown);
        write_bool(drop.grounded);
    };

    constexpr auto magic =
        std::array<char, 8> {
            'V', 'A', 'L', 'S', 'L', 'O', 'T', '1',
        };
    // J'ecris chaque champ dans l'ordre historique exact au lieu de modifier
    // l'entete d'une sauvegarde moderne qui conserverait des octets futurs.
    write_bytes(magic.data(), magic.size());
    write_value(fixture.version);
    write_value(fixture.saved_at);
    write_value(fixture.seed);
    write_value(fixture.time_of_day);
    write_value(fixture.modified_chunk_count);
    write_bool(fixture.has_starting_village);
    if (fixture.version >= 4U) {
        write_value(fixture.weather_time_seconds);
    }
    write_vec3(fixture.spawn_position);
    write_player_state(fixture.player_state);
    if (fixture.version >= 6U) {
        write_value(fixture.progression.level);
        write_value(fixture.progression.experience);
    }

    for (const auto& slot : fixture.hotbar.slots) {
        write_hotbar_slot(slot);
    }
    write_value(
        static_cast<std::uint32_t>(
            fixture.hotbar.selected_index));
    for (const auto& slot : fixture.inventory.storage_slots) {
        write_hotbar_slot(slot);
    }
    write_hotbar_slot(fixture.inventory.carried_slot);
    write_bool(fixture.inventory.carrying_item);
    if (fixture.version >= 5U) {
        for (const auto& slot :
             fixture.inventory.equipment_slots) {
            write_hotbar_slot(slot);
        }
    }

    write_value(std::uint32_t {1U});
    write_creature(fixture.creature);
    write_value(std::uint32_t {1U});
    write_item_drop(fixture.item_drop);

    write_value(std::uint32_t {1U});
    write_value(fixture.chunk_coord.x);
    write_value(fixture.chunk_coord.z);
    write_bytes(
        fixture.dense_blocks.data(),
        fixture.dense_blocks.size() * sizeof(BlockId));
    if (fixture.version >= 3U) {
        write_bytes(
            fixture.dense_water_state.data(),
            fixture.dense_water_state.size() *
                sizeof(WaterState));
    }

    output.close();
    REQUIRE(output.good());
}

auto make_save_test_crew_state() noexcept -> ShipCrewSaveState {
    constexpr std::array roles {
        ShipCrewRole::Captain,
        ShipCrewRole::Fisher,
        ShipCrewRole::Rigger,
        ShipCrewRole::WaterTender,
        ShipCrewRole::Deckhand,
        ShipCrewRole::Quartermaster,
    };
    constexpr std::array activities {
        ShipCrewActivity::Inspect,
        ShipCrewActivity::Carry,
        ShipCrewActivity::HaulRope,
        ShipCrewActivity::TendWater,
        ShipCrewActivity::Scrub,
        ShipCrewActivity::Socialize,
    };
    constexpr std::array stations {
        ShipCrewStation::Helm,
        ShipCrewStation::PortFishing,
        ShipCrewStation::MainMast,
        ShipCrewStation::WaterStill,
        ShipCrewStation::MidDeckPort,
        ShipCrewStation::CargoSort,
    };

    ShipCrewSaveState crew {};
    crew.initialized = true;
    crew.navigation_revision = amelie_ship_blueprint().navigation_revision;
    for (std::size_t index = 0; index < crew.members.size(); ++index) {
        auto& member = crew.members[index];
        const auto scalar = static_cast<float>(index);
        member.local_position = {-5.25F + scalar * 1.75F, 1.0F + scalar * 0.125F, -18.5F + scalar * 7.0F};
        member.yaw_radians = -1.25F + scalar * 0.375F;
        member.animation_time = 40.5F + scalar * 71.0F;
        member.activity_timer = 12.25F + scalar * 6.5F;
        member.work_progress = index == 1U ? 37.5F : (index == 3U ? 91.25F : 0.0F);
        member.health = ship_crew_max_health(roles[index]) - scalar * 0.25F;
        member.recovery_timer = 0.0F;
        member.hurt_timer = 0.05F * scalar;
        member.id = static_cast<std::uint8_t>(index);
        member.routine_step = static_cast<std::uint8_t>(index % 4U);
        member.role = roles[index];
        member.activity = activities[index];
        member.cargo = index == 1U
                           ? ShipCrewCargo::Fish
                           : (index == 3U ? ShipCrewCargo::Water : ShipCrewCargo::None);
        member.current_station = stations[index];
        member.next_station = stations[(index + 1U) % stations.size()];
        member.destination_station = stations[(index + 2U) % stations.size()];
    }
    // Je couvre aussi un membre assomme afin de verifier ses minuteries et sa
    // vie nulle sans laisser le test dependre d'un etat impossible en jeu.
    crew.members[4].health = 0.0F;
    crew.members[4].recovery_timer = 12.5F;
    return crew;
}

} // namespace

TEST_CASE("session save state commits only the revision captured by its worker") {
    SessionSaveState state {};
    CHECK_FALSE(state.dirty());
    CHECK(state.transition_allowed());

    state.mark_dirty();
    state.begin_save();
    CHECK(state.dirty());

    SUBCASE("a matching completion secures the session") {
        state.complete_save();
        CHECK_FALSE(state.dirty());
        CHECK_FALSE(state.failed());
        CHECK(state.transition_allowed());
    }

    SUBCASE("a later mutation remains dirty after the older worker completes") {
        state.mark_dirty();
        state.complete_save();
        CHECK(state.dirty());
        CHECK_FALSE(state.failed());
        CHECK(state.transition_allowed());
    }

    SUBCASE("a failed write blocks transitions until a successful retry") {
        state.fail_save();
        CHECK(state.dirty());
        CHECK(state.failed());
        CHECK_FALSE(state.transition_allowed());

        state.begin_save();
        CHECK(state.transition_allowed());
        state.complete_save();
        CHECK_FALSE(state.dirty());
        CHECK_FALSE(state.failed());
    }
}

TEST_CASE("game option parser accepts smoke perf flags and values") {
    const std::vector<std::string_view> arguments {
        "--smoke-test",
        "--smoke-frames=120",
        "--window-width=1920",
        "--window-height=1080",
        "--perf-warmup-frames=30",
        "--hidden-window",
        "--freeze-time",
        "--perf-report",
        "--perf-json=artifacts/run.json",
        "--perf-trace",
        "--perf-scenario=baseline",
        "--disable-shadows",
        "--disable-post-process",
        "--fixed-render-quality",
        "--stream-radius=14",
    };

    const auto parsed = parse_game_options(arguments);

    REQUIRE(parsed.ok);
    CHECK(parsed.options.smoke_test);
    CHECK(parsed.options.hidden_window);
    CHECK(parsed.options.freeze_time);
    CHECK(parsed.options.smoke_frames == 120);
    CHECK(parsed.options.window_width == 1920);
    CHECK(parsed.options.window_height == 1080);
    CHECK(parsed.options.performance.perf_warmup_frames == 30);
    CHECK(parsed.options.performance.report_frame_stats);
    CHECK(parsed.options.performance.perf_json_path == "artifacts/run.json");
    CHECK(parsed.options.performance.perf_trace_enabled);
    CHECK(parsed.options.performance.perf_scenario == "baseline");
    CHECK_FALSE(parsed.options.performance.shadows_enabled);
    CHECK_FALSE(parsed.options.performance.post_process_enabled);
    CHECK_FALSE(parsed.options.performance.adaptive_quality);
    CHECK(parsed.options.performance.stream_radius == 14);
}

TEST_CASE("game option parser starts modern and keeps the explicit legacy fallback") {
    const auto default_parse =
        parse_game_options(std::vector<std::string_view> {});
    REQUIRE(default_parse.ok);
    CHECK(default_parse.options.visual_pipeline ==
          VisualPipeline::ModernStylized);

    const auto legacy_parse =
        parse_game_options(
            std::vector<std::string_view> {
                "--visual-pipeline=legacy",
            });
    REQUIRE(legacy_parse.ok);
    CHECK(legacy_parse.options.visual_pipeline ==
          VisualPipeline::LegacyVoxel);

    const auto modern_parse =
        parse_game_options(
            std::vector<std::string_view> {
                "--visual-pipeline=modern",
            });
    REQUIRE(modern_parse.ok);
    CHECK(modern_parse.options.visual_pipeline ==
          VisualPipeline::ModernStylized);

    for (const auto argument : {
             std::string_view {"--visual-pipeline="},
             std::string_view {"--visual-pipeline=stylized"},
             std::string_view {"--visual-pipeline=MODERN"},
         }) {
        const auto parsed =
            parse_game_options(
                std::vector<std::string_view> {
                    argument,
                });
        CAPTURE(argument);
        CHECK_FALSE(parsed.ok);
        CHECK(parsed.error_message ==
              "Invalid value for --visual-pipeline");
    }
}

TEST_CASE("game option parser accepts explicit smoke session modes") {
    const auto default_parse = parse_game_options(std::vector<std::string_view> {});
    REQUIRE(default_parse.ok);
    CHECK(default_parse.options.smoke_session == SmokeSessionMode::Menu);

    const auto menu_parse = parse_game_options(std::vector<std::string_view> {"--smoke-session=menu"});
    REQUIRE(menu_parse.ok);
    CHECK(menu_parse.options.smoke_session == SmokeSessionMode::Menu);

    const auto sea_new_parse = parse_game_options(std::vector<std::string_view> {"--smoke-session=sea-new"});
    REQUIRE(sea_new_parse.ok);
    CHECK(sea_new_parse.options.smoke_session == SmokeSessionMode::SeaNew);

    const auto sea_legacy_parse = parse_game_options(std::vector<std::string_view> {"--smoke-session=sea-legacy"});
    REQUIRE(sea_legacy_parse.ok);
    CHECK(sea_legacy_parse.options.smoke_session == SmokeSessionMode::SeaLegacy);

    const auto sea_open_parse =
        parse_game_options(
            std::vector<std::string_view> {
                "--smoke-session=sea-open",
            });
    REQUIRE(sea_open_parse.ok);
    CHECK(
        sea_open_parse.options.smoke_session ==
        SmokeSessionMode::SeaOpen);

    const auto backrooms_parse =
        parse_game_options(
            std::vector<std::string_view> {
                "--smoke-session=backrooms",
            });
    REQUIRE(backrooms_parse.ok);
    CHECK(
        backrooms_parse.options.smoke_session ==
        SmokeSessionMode::Backrooms);

    const auto empty_parse = parse_game_options(std::vector<std::string_view> {"--smoke-session="});
    CHECK_FALSE(empty_parse.ok);
    CHECK(empty_parse.error_message == "Invalid value for --smoke-session");

    const auto unknown_parse = parse_game_options(std::vector<std::string_view> {"--smoke-session=classic"});
    CHECK_FALSE(unknown_parse.ok);
    CHECK(unknown_parse.error_message == "Invalid value for --smoke-session");
}

TEST_CASE("Backrooms smoke starts gameplay and remains compatible with frame capture") {
    const auto parsed =
        parse_game_options(
            std::vector<std::string_view> {
                "--smoke-test",
                "--smoke-session=backrooms",
                "--smoke-frames=180",
                "--hidden-window",
                "--visual-pipeline=modern",
                "--fixed-render-quality",
                "--smoke-backrooms-blackout",
                "--smoke-backrooms-flashlight",
                "--smoke-backrooms-ceiling-view",
                "--capture-frame=artifacts/backrooms/lighting.bmp",
            });

    REQUIRE(parsed.ok);
    CHECK(parsed.options.smoke_test);
    CHECK(parsed.options.hidden_window);
    CHECK(
        parsed.options.smoke_session ==
        SmokeSessionMode::Backrooms);
    CHECK(
        smoke_session_starts_gameplay(
            parsed.options.smoke_session));
    CHECK(parsed.options.smoke_frames == 180);
    CHECK(
        parsed.options.visual_pipeline ==
        VisualPipeline::ModernStylized);
    CHECK(
        parsed.options.smoke_backrooms_flashlight);
    CHECK(
        parsed.options.smoke_backrooms_ceiling_view);
    CHECK(
        parsed.options.smoke_backrooms_blackout);
    CHECK_FALSE(
        parsed.options.performance.adaptive_quality);
    CHECK(
        parsed.options.frame_capture_path ==
        "artifacts/backrooms/lighting.bmp");

    CHECK_FALSE(
        smoke_session_starts_gameplay(
            SmokeSessionMode::Menu));
    CHECK(
        smoke_session_starts_gameplay(
            SmokeSessionMode::SeaNew));
    CHECK(
        smoke_session_starts_gameplay(
            SmokeSessionMode::SeaLegacy));
    CHECK(
        smoke_session_starts_gameplay(
            SmokeSessionMode::SeaOpen));
}

TEST_CASE("game option parser accepts deterministic maritime smoke views") {
    const auto default_parse = parse_game_options(std::vector<std::string_view> {});
    REQUIRE(default_parse.ok);
    CHECK(default_parse.options.smoke_ship_view == SmokeShipView::None);

    const std::array<std::pair<std::string_view, SmokeShipView>, 14> valid_views {{
        {"deck", SmokeShipView::Deck},
        {"bow", SmokeShipView::Bow},
        {"stern", SmokeShipView::Stern},
        {"port", SmokeShipView::Port},
        {"starboard", SmokeShipView::Starboard},
        {"interior", SmokeShipView::Interior},
        {"cabin", SmokeShipView::CaptainCabin},
        {"cargo", SmokeShipView::CargoHold},
        {"crew", SmokeShipView::CrewDeck},
        {"infirmary", SmokeShipView::Infirmary},
        {"mess", SmokeShipView::Mess},
        {"battery", SmokeShipView::GunDeck},
        {"underwater", SmokeShipView::Underwater},
        {"wake", SmokeShipView::Wake},
    }};
    for (const auto& [label, expected] : valid_views) {
        const auto argument = std::string("--smoke-ship-view=") + std::string(label);
        const auto parsed = parse_game_options(std::vector<std::string_view> {argument});
        REQUIRE(parsed.ok);
        CHECK(parsed.options.smoke_ship_view == expected);
    }

    const auto empty_parse = parse_game_options(std::vector<std::string_view> {"--smoke-ship-view="});
    CHECK_FALSE(empty_parse.ok);
    CHECK(empty_parse.error_message == "Invalid value for --smoke-ship-view");

    const auto unknown_parse = parse_game_options(std::vector<std::string_view> {"--smoke-ship-view=top"});
    CHECK_FALSE(unknown_parse.ok);
    CHECK(unknown_parse.error_message == "Invalid value for --smoke-ship-view");
}

TEST_CASE("game option parser accepts startup ui preview overlays") {
    const std::vector<std::string_view> inventory_arguments {"--ui-preview=inventory"};
    const auto parsed_inventory = parse_game_options(inventory_arguments);
    REQUIRE(parsed_inventory.ok);
    CHECK(parsed_inventory.options.startup_ui_overlay == StartupUiOverlay::Inventory);

    const std::vector<std::string_view> pause_arguments {"--ui-preview=pause"};
    const auto parsed_pause = parse_game_options(pause_arguments);
    REQUIRE(parsed_pause.ok);
    CHECK(parsed_pause.options.startup_ui_overlay == StartupUiOverlay::Pause);

    const std::vector<std::string_view> progression_arguments {
        "--ui-preview=progression"};
    const auto parsed_progression =
        parse_game_options(
            progression_arguments);
    REQUIRE(parsed_progression.ok);
    CHECK(
        parsed_progression.options
                .startup_ui_overlay ==
            StartupUiOverlay::
                Progression);

    const std::vector<std::string_view> construction_arguments {
        "--ui-preview=construction-plan"};
    const auto parsed_construction =
        parse_game_options(
            construction_arguments);
    REQUIRE(parsed_construction.ok);
    CHECK(
        parsed_construction.options
                .startup_ui_overlay ==
            StartupUiOverlay::
                ConstructionPlan);

    const std::vector<std::string_view> invalid_arguments {"--ui-preview=main"};
    CHECK_FALSE(parse_game_options(invalid_arguments).ok);
}

TEST_CASE("game option parser accepts deterministic non negative weather time") {
    const auto default_parse =
        parse_game_options(std::vector<std::string_view> {});
    REQUIRE(default_parse.ok);
    CHECK(default_parse.options.initial_weather_time_seconds ==
          doctest::Approx(0.0F));

    const auto tempest_parse =
        parse_game_options(
            std::vector<std::string_view> {
                "--initial-weather-time=2685.0979",
            });
    REQUIRE(tempest_parse.ok);
    CHECK(tempest_parse.options.initial_weather_time_seconds ==
          doctest::Approx(2685.0979F));

    const auto zero_parse =
        parse_game_options(
            std::vector<std::string_view> {
                "--initial-weather-time=0",
            });
    REQUIRE(zero_parse.ok);
    CHECK(zero_parse.options.initial_weather_time_seconds ==
          doctest::Approx(0.0F));

    const auto maximum_parse =
        parse_game_options(
            std::vector<std::string_view> {
                "--initial-weather-time=1000000000",
            });
    REQUIRE(maximum_parse.ok);
    CHECK(maximum_parse.options.initial_weather_time_seconds ==
          doctest::Approx(kMaximumWeatherTimeSeconds));
}

TEST_CASE("sea new smoke applies only an explicitly requested initial time") {
    const auto default_parse =
        parse_game_options(std::vector<std::string_view> {});
    REQUIRE(default_parse.ok);
    CHECK_FALSE(default_parse.options.initial_time_explicitly_set);
    CHECK(resolve_new_session_time_of_day(default_parse.options) ==
          doctest::Approx(8.25F));

    const auto sea_default_parse =
        parse_game_options(
            std::vector<std::string_view> {
                "--smoke-test",
                "--smoke-session=sea-new",
            });
    REQUIRE(sea_default_parse.ok);
    CHECK_FALSE(
        sea_default_parse.options.initial_time_explicitly_set);
    CHECK(resolve_new_session_time_of_day(
              sea_default_parse.options) ==
          doctest::Approx(8.25F));

    const auto night_parse =
        parse_game_options(
            std::vector<std::string_view> {
                "--smoke-test",
                "--smoke-session=sea-new",
                "--initial-time=0",
            });
    REQUIRE(night_parse.ok);
    CHECK(night_parse.options.initial_time_explicitly_set);
    CHECK(night_parse.options.initial_time_of_day ==
          doctest::Approx(0.0F));
    CHECK(resolve_new_session_time_of_day(night_parse.options) ==
          doctest::Approx(0.0F));

    const auto dense_port_parse =
        parse_game_options(
            std::vector<std::string_view> {
                "--smoke-test",
                "--smoke-session=sea-new",
                "--initial-time=16.5",
            });
    REQUIRE(dense_port_parse.ok);
    CHECK(resolve_new_session_time_of_day(
              dense_port_parse.options) ==
          doctest::Approx(16.5F));

    const auto interactive_parse =
        parse_game_options(
            std::vector<std::string_view> {
                "--initial-time=0",
            });
    REQUIRE(interactive_parse.ok);
    CHECK(interactive_parse.options.initial_time_explicitly_set);
    CHECK(resolve_new_session_time_of_day(
              interactive_parse.options) ==
          doctest::Approx(8.25F));
}

TEST_CASE("game option parser rejects invalid deterministic weather time") {
    constexpr std::array<std::string_view, 6> invalid_arguments {{
        "--initial-weather-time=",
        "--initial-weather-time=-0.001",
        "--initial-weather-time=nan",
        "--initial-weather-time=inf",
        "--initial-weather-time=1e30",
        "--initial-weather-time=1e100",
    }};

    for (const auto argument : invalid_arguments) {
        const auto parsed =
            parse_game_options(
                std::vector<std::string_view> {argument});
        CAPTURE(argument);
        CHECK_FALSE(parsed.ok);
        CHECK(parsed.error_message ==
              "Invalid value for --initial-weather-time");
    }
}

TEST_CASE("game option parser accepts explicit visual frame capture target") {
    const std::vector<std::string_view> arguments {
        "--smoke-test",
        "--capture-frame=artifacts/visual-check.bmp",
    };

    const auto parsed = parse_game_options(arguments);

    REQUIRE(parsed.ok);
    CHECK(parsed.options.smoke_test);
    CHECK(parsed.options.frame_capture_path == "artifacts/visual-check.bmp");
    CHECK_FALSE(parse_game_options(std::vector<std::string_view> {"--capture-frame="}).ok);
}

TEST_CASE("game option parser rejects non finite time and unsafe streaming radius") {
    const std::vector<std::string_view> nan_time {"--initial-time=nan"};
    const auto parsed_nan_time = parse_game_options(nan_time);
    CHECK_FALSE(parsed_nan_time.ok);

    const std::vector<std::string_view> infinite_time {"--initial-time=inf"};
    const auto parsed_infinite_time = parse_game_options(infinite_time);
    CHECK_FALSE(parsed_infinite_time.ok);

    const std::vector<std::string_view> oversized_stream_radius {"--stream-radius=2147483647"};
    const auto parsed_stream_radius = parse_game_options(oversized_stream_radius);
    CHECK_FALSE(parsed_stream_radius.ok);

    CHECK_FALSE(parse_game_options(std::vector<std::string_view> {"--window-width=0"}).ok);
    CHECK_FALSE(parse_game_options(std::vector<std::string_view> {"--window-height=99999"}).ok);
    CHECK_FALSE(parse_game_options(std::vector<std::string_view> {"--perf-warmup-frames=-1"}).ok);
}

TEST_CASE("game option parser accepts perf capture flags outside smoke mode") {
    const std::vector<std::string_view> arguments {
        "--perf-report",
        "--perf-json=artifacts/run.json",
        "--perf-trace",
        "--perf-scenario=interactive_session",
    };

    const auto parsed = parse_game_options(arguments);

    REQUIRE(parsed.ok);
    CHECK_FALSE(parsed.options.smoke_test);
    CHECK(parsed.options.performance.report_frame_stats);
    CHECK(parsed.options.performance.perf_json_path == "artifacts/run.json");
    CHECK(parsed.options.performance.perf_trace_enabled);
    CHECK(parsed.options.performance.perf_scenario == "interactive_session");
}

TEST_CASE("gameplay action keys use physical scancodes") {
    SDL_Keysym drop_key {};
    drop_key.sym = SDLK_q;
    drop_key.scancode = SDL_SCANCODE_Q;
    CHECK(is_drop_action_key(drop_key));

    SDL_Keysym super_vision_key {};
    super_vision_key.sym = SDLK_v;
    super_vision_key.scancode = SDL_SCANCODE_V;
    CHECK(is_super_vision_action_key(super_vision_key));

    SDL_Keysym flight_key {};
    flight_key.sym = SDLK_f;
    flight_key.scancode = SDL_SCANCODE_F;
    CHECK(is_flight_action_key(flight_key));
    CHECK(is_backrooms_flashlight_action_key(flight_key));

    SDL_Keysym remapped_flashlight_key {};
    remapped_flashlight_key.sym = SDLK_f;
    remapped_flashlight_key.scancode = SDL_SCANCODE_G;
    CHECK_FALSE(
        is_backrooms_flashlight_action_key(
            remapped_flashlight_key));

    SDL_Keysym reload_key {};
    reload_key.sym = SDLK_r;
    reload_key.scancode = SDL_SCANCODE_R;
    CHECK(is_reload_action_key(reload_key));

    SDL_Keysym azerty_console_key {};
    azerty_console_key.sym = static_cast<SDL_Keycode>(0x00B2);
    azerty_console_key.scancode = SDL_SCANCODE_GRAVE;
    CHECK(is_command_console_key(azerty_console_key));

    SDL_Keysym remapped_v_key {};
    remapped_v_key.sym = SDLK_v;
    remapped_v_key.scancode = SDL_SCANCODE_B;
    CHECK_FALSE(is_super_vision_action_key(remapped_v_key));

    SDL_Keysym remapped_f_key {};
    remapped_f_key.sym = SDLK_f;
    remapped_f_key.scancode = SDL_SCANCODE_G;
    CHECK_FALSE(is_flight_action_key(remapped_f_key));

    SDL_Keysym remapped_r_key {};
    remapped_r_key.sym = SDLK_r;
    remapped_r_key.scancode = SDL_SCANCODE_T;
    CHECK_FALSE(is_reload_action_key(remapped_r_key));

    SDL_Keysym remapped_console_key {};
    remapped_console_key.sym = static_cast<SDL_Keycode>(0x00B2);
    remapped_console_key.scancode = SDL_SCANCODE_1;
    CHECK_FALSE(is_command_console_key(remapped_console_key));
}

TEST_CASE("progression input bindings use the physical P and F1 through F5 keys") {
    SDL_Keysym progression_key {};
    progression_key.sym = SDLK_o;
    progression_key.scancode = SDL_SCANCODE_P;
    CHECK(is_progression_menu_key(progression_key));

    SDL_Keysym remapped_progression_key {};
    remapped_progression_key.sym = SDLK_p;
    remapped_progression_key.scancode = SDL_SCANCODE_O;
    CHECK_FALSE(is_progression_menu_key(remapped_progression_key));

    constexpr std::array ability_scancodes {
        SDL_SCANCODE_F1,
        SDL_SCANCODE_F2,
        SDL_SCANCODE_F3,
        SDL_SCANCODE_F4,
        SDL_SCANCODE_F5,
    };
    for (std::size_t slot = 0U; slot < ability_scancodes.size(); ++slot) {
        SDL_Keysym ability_key {};
        ability_key.sym = SDLK_UNKNOWN;
        ability_key.scancode = ability_scancodes[slot];

        const auto mapped_slot = ability_slot_from_key(ability_key);
        REQUIRE(mapped_slot.has_value());
        CHECK(*mapped_slot == slot);
    }
}

TEST_CASE("progression ability bindings reject keys outside F1 through F5") {
    constexpr std::array invalid_scancodes {
        SDL_SCANCODE_UNKNOWN,
        SDL_SCANCODE_P,
        SDL_SCANCODE_1,
        SDL_SCANCODE_F6,
        SDL_SCANCODE_ESCAPE,
    };
    for (const auto scancode : invalid_scancodes) {
        SDL_Keysym invalid_key {};
        invalid_key.scancode = scancode;
        CHECK_FALSE(ability_slot_from_key(invalid_key).has_value());
    }
}

TEST_CASE("game option parser enables audit only when audit or perf capture flags are present") {
    const std::vector<std::string_view> default_arguments {
        "--hidden-window",
        "--freeze-time",
    };

    const auto default_parse = parse_game_options(default_arguments);

    REQUIRE(default_parse.ok);
    CHECK_FALSE(default_parse.options.audit.enabled);
    CHECK(default_parse.options.raw_arguments.size() == default_arguments.size());

    const std::vector<std::string_view> audit_arguments {
        "--audit",
        "--audit-mode=forensic",
        "--audit-dir=performancesaudit/custom",
        "--audit-label=manual block break",
        "--audit-trace-frames",
    };

    const auto audit_parse = parse_game_options(audit_arguments);

    REQUIRE(audit_parse.ok);
    CHECK(audit_parse.options.audit.enabled);
    CHECK(audit_parse.options.audit.mode == AuditMode::Forensic);
    CHECK(audit_parse.options.audit.root_directory == std::filesystem::path("performancesaudit/custom"));
    CHECK(audit_parse.options.audit.label == "manual block break");
    CHECK(audit_parse.options.audit.trace_frames);
    CHECK(audit_parse.options.raw_arguments.size() == audit_arguments.size());
}

TEST_CASE("audit helpers sanitize labels and build run paths under performancesaudit runs") {
    AuditOptions options {};
    options.enabled = true;
    options.mode = AuditMode::Forensic;
    options.root_directory = std::filesystem::path("performancesaudit");
    options.label = "manual block/break.v1";

    CHECK(sanitize_audit_label("manual block/break.v1") == "manual-block-break-v1");
    CHECK(sanitize_audit_label("   ") == "interactive");

    const auto started_at = std::chrono::system_clock::from_time_t(1712185200);
    const auto paths = make_audit_run_paths(options, "session-id", started_at);
    const auto other_paths = make_audit_run_paths(options, "other-session-id", started_at);
    const auto run_name = paths.run_directory.filename().string();

    CHECK(paths.root_directory == std::filesystem::absolute(options.root_directory));
    CHECK(paths.run_directory.parent_path() == paths.root_directory / "runs");
    CHECK(run_name.find("-session-id-forensic-manual-block-break-v1") != std::string::npos);
    CHECK(run_name.ends_with("-forensic-manual-block-break-v1"));
    CHECK(paths.run_directory != other_paths.run_directory);
    CHECK(paths.manifest_path == paths.run_directory / "manifest.json");
    CHECK(paths.summary_json_path == paths.run_directory / "summary.json");
    CHECK(paths.summary_text_path == paths.run_directory / "summary.txt");
    CHECK(paths.events_path == paths.run_directory / "events.jsonl");
    CHECK(paths.seconds_path == paths.run_directory / "seconds.jsonl");
    CHECK(paths.frames_path == paths.run_directory / "frames.jsonl");
    CHECK(paths.spikes_path == paths.run_directory / "spikes.json");
}

TEST_CASE("audit writer evicts every target strictly by priority and reports actual output counts") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto audit_root = std::filesystem::temp_directory_path() / ("valcraft-audit-writer-" + unique_suffix);
    std::filesystem::remove_all(audit_root);

    AuditOptions options {};
    options.enabled = true;
    options.mode = AuditMode::Forensic;
    options.root_directory = audit_root;
    options.label = "writer-priority";
    options.writer_queue_capacity = 3;
    options.writer_batch_size = 64;
    options.writer_flush_interval_ms = 60'000;

    const auto started_at = std::chrono::system_clock::from_time_t(1712185200);
    const auto paths = make_audit_run_paths(options, "writer-priority-session", started_at);
    std::vector<std::string> errors;
    AuditWriter writer;

    REQUIRE(writer.start(options, paths, &errors));
    REQUIRE(writer.enqueue_event("low-event", AuditPriority::Low, AuditEventCategory::Player));
    REQUIRE(writer.enqueue_frame("normal-frame", AuditPriority::Normal));
    REQUIRE(writer.enqueue_second("high-second", AuditPriority::High));
    REQUIRE(writer.enqueue_second("critical-second", AuditPriority::Critical));
    CHECK_FALSE(writer.enqueue_frame("rejected-low-frame", AuditPriority::Low));
    REQUIRE(writer.enqueue_event("critical-event", AuditPriority::Critical, AuditEventCategory::Session));
    CHECK_FALSE(writer.enqueue_second("rejected-low-second", AuditPriority::Low));

    writer.stop(&errors);
    CHECK(errors.empty());
    CHECK_FALSE(writer.active());

    const auto counters = writer.counters();
    CHECK(counters.written_event_counts[audit_event_category_index(AuditEventCategory::Session)] == 1);
    CHECK(counters.written_event_counts[audit_event_category_index(AuditEventCategory::Player)] == 0);
    CHECK(counters.dropped_event_counts[audit_event_category_index(AuditEventCategory::Player)] == 1);
    CHECK(counters.written_second_samples == 2);
    CHECK(counters.dropped_second_samples == 1);
    CHECK(counters.written_frames == 0);
    CHECK(counters.dropped_frames == 2);

    const auto read_file = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    };
    const auto events_jsonl = read_file(paths.events_path);
    const auto seconds_jsonl = read_file(paths.seconds_path);
    const auto frames_jsonl = read_file(paths.frames_path);
    CHECK(events_jsonl.find("critical-event") != std::string::npos);
    CHECK(events_jsonl.find("low-event") == std::string::npos);
    CHECK(seconds_jsonl.find("high-second") != std::string::npos);
    CHECK(seconds_jsonl.find("critical-second") != std::string::npos);
    CHECK(seconds_jsonl.find("rejected-low-second") == std::string::npos);
    CHECK(frames_jsonl.empty());

    std::filesystem::remove_all(audit_root);
}

TEST_CASE("audit writer destructor drains accepted lines safely") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto audit_root = std::filesystem::temp_directory_path() / ("valcraft-audit-writer-raii-" + unique_suffix);
    std::filesystem::remove_all(audit_root);

    AuditOptions options {};
    options.enabled = true;
    options.root_directory = audit_root;
    options.writer_batch_size = 64;
    options.writer_flush_interval_ms = 60'000;

    const auto started_at = std::chrono::system_clock::from_time_t(1712185200);
    const auto paths = make_audit_run_paths(options, "writer-raii-session", started_at);
    std::vector<std::string> errors;
    {
        AuditWriter writer;
        REQUIRE(writer.start(options, paths, &errors));
        REQUIRE(writer.enqueue_event("raii-event", AuditPriority::Critical, AuditEventCategory::Session));
    }

    CHECK(errors.empty());
    std::ifstream input(paths.events_path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    CHECK(buffer.str().find("raii-event") != std::string::npos);
    input.close();

    std::filesystem::remove_all(audit_root);
}

TEST_CASE("audit recorder stays opt in and writes no files when disabled") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto audit_root = std::filesystem::temp_directory_path() / ("valcraft-audit-disabled-" + unique_suffix);
    std::filesystem::remove_all(audit_root);

    AuditOptions options {};
    options.enabled = false;
    options.root_directory = audit_root;

    AuditStartContext start_context {};
    start_context.working_directory = audit_root;

    {
        AuditRecorder recorder(options, start_context);
        CHECK_FALSE(recorder.enabled());
        recorder.finalize(AuditFinalizeContext {});
    }

    CHECK_FALSE(std::filesystem::exists(audit_root));
}

TEST_CASE("window icon path resolution prefers working directory then executable directory then build root") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root = std::filesystem::temp_directory_path() / ("valcraft-window-icon-tests-" + unique_suffix);
    const auto working_directory = root / "workspace";
    const auto build_directory = root / "build";
    const auto executable_directory = build_directory / "bin";
    const auto icon_name = std::filesystem::path(kGameWindowIconRelativePath).filename();
    const auto parent_icon_path = build_directory / "Images" / icon_name;
    const auto executable_icon_path = executable_directory / "Images" / icon_name;
    const auto working_icon_path = working_directory / "Images" / icon_name;

    std::filesystem::remove_all(root);
    std::filesystem::create_directories(parent_icon_path.parent_path());
    std::filesystem::create_directories(executable_icon_path.parent_path());
    std::filesystem::create_directories(working_icon_path.parent_path());

    {
        std::ofstream icon_stream(parent_icon_path, std::ios::binary);
        icon_stream.put('\0');
    }
    CHECK(resolve_window_icon_path(working_directory, executable_directory) == parent_icon_path);

    {
        std::ofstream icon_stream(executable_icon_path, std::ios::binary);
        icon_stream.put('\0');
    }
    CHECK(resolve_window_icon_path(working_directory, executable_directory) == executable_icon_path);

    {
        std::ofstream icon_stream(working_icon_path, std::ios::binary);
        icon_stream.put('\0');
    }
    CHECK(resolve_window_icon_path(working_directory, executable_directory) == working_icon_path);

    std::filesystem::remove_all(root);
}

TEST_CASE("audit recorder writes manifest summary and compatibility report when enabled") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto audit_root = std::filesystem::temp_directory_path() / ("valcraft-audit-enabled-" + unique_suffix);
    std::filesystem::remove_all(audit_root);

    AuditOptions options {};
    options.enabled = true;
    options.mode = AuditMode::Measure;
    options.root_directory = audit_root;
    options.label = "manual block break";
    options.trace_frames = true;
    options.compatibility_json_path = (audit_root / "compatibility.json").string();

    AuditStartContext start_context {};
    start_context.platform = "windows";
    start_context.build_type = "Debug";
    start_context.working_directory = audit_root;
    start_context.arguments = {"--audit", "--audit-mode=measure", "--audit-label=manual block break"};

    PerformanceRunReport report {};
    report.metadata.platform = "windows";
    report.metadata.build_type = "Debug";
    report.metadata.capture_mode = "measure";
    report.metadata.scenario = "manual block break";
    report.summary.frame_count = 1;
    report.summary.frame_total_ms.average = 16.0;
    report.summary.frame_total_ms.maximum = 16.0;
    report.summary.frame_total_ms.p50 = 16.0;
    report.summary.frame_total_ms.p95 = 16.0;
    report.summary.frame_total_ms.p99 = 16.0;

    std::filesystem::path manifest_path;
    std::filesystem::path summary_json_path;
    std::filesystem::path summary_text_path;
    std::filesystem::path events_path;
    std::filesystem::path seconds_path;
    std::filesystem::path frames_path;
    std::filesystem::path spikes_path;

    {
        AuditRecorder recorder(options, start_context);
        REQUIRE(recorder.enabled());

        AuditEvent event {};
        event.frame_index = 0;
        event.second_index = 0;
        event.category = AuditEventCategory::Player;
        event.kind = "block_break";
        event.severity = AuditSeverity::Info;
        event.payload_json = audit_json_object({
            {"world_x", audit_json_number(12)},
            {"world_y", audit_json_number(64)},
            {"world_z", audit_json_number(5)},
        });
        recorder.record_event(event, AuditPriority::High);

        AuditFrameSample frame {};
        frame.frame_index = 0;
        frame.second_index = 0;
        frame.fps = 62.5;
        frame.ui_screen = "gameplay";
        frame.mouse_captured = true;
        frame.input_raw_events = 2;
        frame.input_action_events = 1;
        frame.active_creatures = 3;
        frame.active_item_drops = 1;
        frame.performance.frame_index = 0;
        frame.performance.frame_total_ms = 16.0;
        frame.performance.world_ms = 5.0;
        recorder.record_frame(frame, AuditPriority::Low);

        AuditSecondSample second {};
        second.second_index = 0;
        second.frame_count = 1;
        second.fps_avg = 62.5;
        second.fps_min = 62.5;
        second.fps_max = 62.5;
        second.frame_ms_avg = 16.0;
        second.frame_ms_p95 = 16.0;
        second.frame_ms_max = 16.0;
        second.gpu_frame_ms_avg = 4.25;
        second.gpu_timing_samples = 1;
        second.player_events = 1;
        second.block_breaks = 1;
        recorder.record_second(second, AuditPriority::High);

        AuditFinalizeContext finalize_context {};
        finalize_context.status = AuditRunStatus::Completed;
        finalize_context.performance_report = report;
        recorder.finalize(finalize_context);

        manifest_path = recorder.paths().manifest_path;
        summary_json_path = recorder.paths().summary_json_path;
        summary_text_path = recorder.paths().summary_text_path;
        events_path = recorder.paths().events_path;
        seconds_path = recorder.paths().seconds_path;
        frames_path = recorder.paths().frames_path;
        spikes_path = recorder.paths().spikes_path;
    }

    const auto read_file = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    };

    CHECK(std::filesystem::exists(manifest_path));
    CHECK(std::filesystem::exists(summary_json_path));
    CHECK(std::filesystem::exists(summary_text_path));
    CHECK(std::filesystem::exists(events_path));
    CHECK(std::filesystem::exists(seconds_path));
    CHECK(std::filesystem::exists(frames_path));
    CHECK(std::filesystem::exists(spikes_path));
    CHECK(std::filesystem::exists(audit_root / "compatibility.json"));

    const auto manifest_json = read_file(manifest_path);
    const auto summary_json = read_file(summary_json_path);
    const auto summary_text = read_file(summary_text_path);
    const auto events_jsonl = read_file(events_path);
    const auto seconds_jsonl = read_file(seconds_path);

    CHECK(manifest_json.find("\"status\": \"completed\"") != std::string::npos);
    CHECK(manifest_json.find("\"recorded_frames\": 1") != std::string::npos);
    CHECK(manifest_json.find("\"second_samples\": 1") != std::string::npos);
    CHECK(summary_json.find("\"performance_report\":") != std::string::npos);
    CHECK(summary_json.find("\"events_written\": 1") != std::string::npos);
    CHECK(summary_text.find("ValCraft audit summary") != std::string::npos);
    CHECK(events_jsonl.find("\"kind\": \"block_break\"") != std::string::npos);
    CHECK(seconds_jsonl.find("\"frame_count\": 1") != std::string::npos);
    CHECK(seconds_jsonl.find("\"gpu_timing_samples\": 1") != std::string::npos);

    std::filesystem::remove_all(audit_root);
}

TEST_CASE("default performance options favor lighter frame pacing defaults") {
    const PerformanceOptions options {};

    CHECK(options.chunk_generation_budget == 2);
    CHECK(options.fluid_cell_budget == 256);
    CHECK(options.mesh_rebuild_budget == 4);
    CHECK(options.light_node_budget == 8192);
    CHECK(options.max_generation_ms == doctest::Approx(1.0));
    CHECK(options.max_fluid_ms == doctest::Approx(1.0));
    CHECK(options.max_lighting_ms == doctest::Approx(1.5));
    CHECK(options.max_meshing_ms == doctest::Approx(2.0));
    CHECK(options.stream_radius == 5);
    CHECK(options.shadows_enabled);
    CHECK(options.shadow_map_size == 1024);
    CHECK(options.post_process_enabled);
    CHECK(options.adaptive_quality);
}

TEST_CASE("dominant stage detection chooses the largest measured subsystem") {
    FramePerformanceSample sample {};
    sample.streaming_ms = 1.0;
    sample.generation_ms = 2.5;
    sample.lighting_ms = 0.5;
    sample.meshing_ms = 4.0;
    sample.upload_ms = 3.0;

    CHECK(detect_dominant_stage(sample) == PerformanceStage::Meshing);

    sample.render_overhead_ms = 6.0;
    CHECK(detect_dominant_stage(sample) == PerformanceStage::RenderOverhead);
}

TEST_CASE("performance report builds lag buckets spike windows and percentiles") {
    PerformanceReportMetadata metadata {};
    metadata.platform = "windows";
    metadata.build_type = "RelWithDebInfo";
    metadata.smoke_frames = 6;
    metadata.stream_radius = 10;
    metadata.shadows_enabled = true;
    metadata.freeze_time = true;
    metadata.scenario = "baseline";

    std::vector<FramePerformanceSample> samples {
        {0, 10.0, 1.0, 1.0, 1.0, 2.0, 0.5, 0.0, 0.5, 1, 1, 10, 1, 0, 0, 0, 0, 0, 0, 0, 1, 40, 0, 40, PerformanceStage::Unattributed},
        {1, 20.0, 1.0, 1.0, 1.0, 9.0, 0.5, 0.0, 0.5, 2, 2, 20, 2, 1, 0, 0, 0, 2, 0, 0, 1, 42, 0, 42, PerformanceStage::Unattributed},
        {2, 25.0, 1.0, 1.0, 6.0, 5.0, 0.5, 0.0, 0.5, 3, 2, 30, 2, 2, 1, 0, 0, 2, 0, 0, 1, 44, 0, 44, PerformanceStage::Unattributed},
        {3, 12.0, 1.0, 1.0, 1.0, 2.0, 0.5, 0.0, 0.5, 1, 1, 10, 1, 0, 0, 0, 0, 0, 0, 0, 1, 40, 0, 40, PerformanceStage::Unattributed},
        {4, 40.0, 1.0, 12.0, 1.0, 4.0, 0.5, 0.0, 0.5, 4, 3, 40, 3, 3, 1, 1, 1, 4, 1, 0, 1, 50, 0, 50, PerformanceStage::Unattributed},
        {5, 55.0, 1.0, 4.0, 1.0, 3.0, 12.0, 0.0, 0.5, 4, 4, 50, 4, 2, 1, 1, 1, 4, 1, 0, 1, 60, 0, 60, PerformanceStage::Unattributed},
    };

    const auto report = build_performance_report(metadata, samples, true, 3);

    CHECK(report.summary.frame_count == 6);
    CHECK(report.summary.frame_total_ms.p50 == doctest::Approx(22.5));
    CHECK(report.summary.frame_total_ms.p95 == doctest::Approx(51.25));
    CHECK(report.summary.lag_buckets.over_16_7_ms == 4);
    CHECK(report.summary.lag_buckets.over_33_3_ms == 2);
    CHECK(report.summary.lag_buckets.over_50_0_ms == 1);
    REQUIRE(report.spike_windows.size() == 2);
    CHECK(report.spike_windows[0].start_frame == 1);
    CHECK(report.spike_windows[0].end_frame == 2);
    CHECK(report.spike_windows[0].peak_frame == 2);
    CHECK(report.spike_windows[0].dominant_stage == PerformanceStage::Lighting);
    CHECK(report.spike_windows[1].start_frame == 4);
    CHECK(report.spike_windows[1].peak_frame == 5);
    CHECK(report.hotspots.worst_frame_stage == PerformanceStage::Upload);
    CHECK(report.worst_frames.size() == 3);
    CHECK(report.worst_frames.front().frame_index == 5);
    CHECK(report.frames.size() == 6);
}

TEST_CASE("performance report JSON includes schema metadata hotspots and optional frames") {
    PerformanceReportMetadata metadata {};
    metadata.platform = "windows";
    metadata.build_type = "RelWithDebInfo";
    metadata.capture_mode = "interactive";
    metadata.smoke_frames = 2;
    metadata.stream_radius = 10;
    metadata.shadows_enabled = false;
    metadata.post_process_enabled = false;
    metadata.freeze_time = true;
    metadata.scenario = "no_shadows";

    std::vector<FramePerformanceSample> samples {
        {0, 8.0, 0.5, 1.0, 1.0, 2.0, 0.1, 0.0, 0.5, 1, 1, 8, 1, 0, 0, 0, 0, 0, 0, 0, 1, 32, 0, 32, PerformanceStage::Unattributed},
        {1, 18.0, 0.5, 3.0, 1.0, 2.0, 0.1, 0.0, 0.5, 2, 2, 18, 2, 1, 0, 0, 0, 1, 0, 0, 1, 36, 0, 36, PerformanceStage::Unattributed},
    };

    const std::vector<PerformanceEvent> events {
        {1, PerformanceEventKind::BlockBreak, "STONE", 12, 64, 5, 0, 0, 0, 3, 1},
    };

    const auto report = build_performance_report(metadata, samples, true, 10, events);
    const auto json = format_performance_json(report);

    CHECK(json.find("\"schema_version\": 3") != std::string::npos);
    CHECK(json.find("\"visual_pipeline\": \"legacy\"") != std::string::npos);
    CHECK(json.find("\"material_pack_version\": 0") != std::string::npos);
    CHECK(json.find("\"render_categories\": {") != std::string::npos);
    CHECK(json.find("\"capture_mode\": \"interactive\"") != std::string::npos);
    CHECK(json.find("\"scenario\": \"no_shadows\"") != std::string::npos);
    CHECK(json.find("\"post_process_enabled\": false") != std::string::npos);
    CHECK(json.find("\"process_working_set_bytes\"") != std::string::npos);
    CHECK(json.find("\"gpu_frame_ms\"") != std::string::npos);
    CHECK(json.find("\"hotspots\"") != std::string::npos);
    CHECK(json.find("\"event_summary\": {") != std::string::npos);
    CHECK(json.find("\"block_breaks\": 1") != std::string::npos);
    CHECK(json.find("\"events\": [") != std::string::npos);
    CHECK(json.find("\"kind\": \"block_break\"") != std::string::npos);
    CHECK(json.find("\"worst_frames\"") != std::string::npos);
    CHECK(json.find("\"spike_windows\"") != std::string::npos);
    CHECK(json.find("\"frames\": [") != std::string::npos);
}

TEST_CASE("performance report v3 attributes complete CPU stages and memory peaks") {
    FramePerformanceSample sample {};
    sample.frame_index = 7;
    sample.frame_total_ms = 18.0;
    sample.event_processing_ms = 0.4;
    sample.simulation_ms = 2.0;
    sample.world_ms = 7.0;
    sample.render_cpu_ms = 7.0;
    sample.render_overhead_ms = 0.75;
    sample.present_ms = 1.0;
    sample.telemetry_ms = 0.25;
    sample.residual_ms = 0.5;
    sample.gpu_frame_ms = 5.5;
    sample.gpu_timing_valid = true;
    sample.resolved_quality = 1U;
    sample.adaptive_frame_ema_ms = 15.25;
    sample.adaptive_frame_p95_ms = 17.5;
    sample.process_working_set_bytes = 512U * 1024U * 1024U;
    sample.process_private_bytes = 768U * 1024U * 1024U;
    sample.pending_fluid = 3;
    sample.processed_fluid_cells = 64;

    const auto report = build_performance_report({}, {sample}, true, 1);

    CHECK(report.schema_version == 3);
    CHECK(report.hotspots.worst_frame_stage == PerformanceStage::World);
    CHECK(report.summary.event_processing_ms.maximum == doctest::Approx(0.4));
    CHECK(report.summary.render_cpu_ms.maximum == doctest::Approx(7.0));
    CHECK(report.summary.render_overhead_ms.maximum == doctest::Approx(0.75));
    CHECK(report.summary.telemetry_ms.maximum == doctest::Approx(0.25));
    CHECK(report.summary.gpu_frame_ms.maximum == doctest::Approx(5.5));
    CHECK(report.summary.gpu_timing_samples == 1);
    CHECK(report.summary.process_working_set_bytes.maximum == 512U * 1024U * 1024U);
    CHECK(report.summary.pending_fluid.maximum == 3);
    CHECK(report.summary.max_processed_fluid_cells == 64);

    const auto json = format_performance_json(report);
    CHECK(json.find("\"resolved_quality\": 1") != std::string::npos);
    CHECK(json.find("\"adaptive_frame_ema_ms\": 15.250000") != std::string::npos);
    CHECK(json.find("\"adaptive_frame_p95_ms\": 17.500000") != std::string::npos);
}

TEST_CASE("process memory sampler reports a coherent resident snapshot") {
    const auto memory = query_process_memory();

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    CHECK(memory.valid);
    CHECK(memory.working_set_bytes > 0U);
    CHECK(memory.private_bytes > 0U);
#else
    CHECK_FALSE(memory.valid);
    CHECK(memory.working_set_bytes == 0U);
    CHECK(memory.private_bytes == 0U);
#endif
}

TEST_CASE("smoke mode uses a deterministic fixed simulation step") {
    const auto fixed_step = std::chrono::duration<double>(1.0 / 60.0);
    const auto measured = std::chrono::duration<double>(0.001);

    CHECK(resolve_simulation_frame_time(true, measured, fixed_step) == fixed_step);
}

TEST_CASE("normal mode clamps very large frame times without forcing the fixed step") {
    const auto fixed_step = std::chrono::duration<double>(1.0 / 60.0);
    const auto measured = std::chrono::duration<double>(0.5);
    const auto resolved = resolve_simulation_frame_time(false, measured, fixed_step);

    CHECK(resolved.count() == doctest::Approx(0.25));
    CHECK(resolved != fixed_step);
}

TEST_CASE("sea streaming follows a disembarked player instead of the moving ship") {
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(8126);
    const auto ship_position =
        sea_adventure.ship_position();
    const glm::vec3 ship_velocity {
        0.0F,
        0.0F,
        2.0F,
    };

    PlayerController deck_player(
        sea_adventure.deck_spawn_position());
    const auto deck_hud =
        sea_adventure.hud_state(
            deck_player);
    REQUIRE(deck_hud.on_ship);
    const auto deck_focus =
        resolve_sea_adventure_streaming_focus(
            deck_player.position(),
            deck_hud.on_ship,
            ship_position,
            ship_velocity);
    CHECK(deck_focus.z >
          deck_player.position().z);

    PlayerController disembarked_player(
        ship_position +
        glm::vec3 {
            28.0F,
            4.10F,
            0.0F,
        });
    const auto disembarked_hud =
        sea_adventure.hud_state(
            disembarked_player);
    REQUIRE_FALSE(disembarked_hud.on_ship);

    const auto unguarded_focus =
        sea_horizon_predictive_streaming_focus(
            disembarked_player.position(),
            ship_position,
            ship_velocity);
    REQUIRE(
        unguarded_focus !=
        disembarked_player.position());

    const auto guarded_focus =
        resolve_sea_adventure_streaming_focus(
            disembarked_player.position(),
            disembarked_hud.on_ship,
            ship_position,
            ship_velocity);
    CHECK(
        guarded_focus ==
        disembarked_player.position());
}

TEST_CASE("movement input helper reads the physical strafe cluster consistently") {
    std::array<Uint8, SDL_NUM_SCANCODES> keys {};

    keys[SDL_SCANCODE_D] = 1;
    auto input = read_player_movement_input(keys.data());
    CHECK(input.move_right == doctest::Approx(1.0F));
    CHECK(input.move_forward == doctest::Approx(0.0F));

    keys.fill(0);
    keys[SDL_SCANCODE_A] = 1;
    input = read_player_movement_input(keys.data());
    CHECK(input.move_right == doctest::Approx(-1.0F));

    keys.fill(0);
    keys[SDL_SCANCODE_W] = 1;
    keys[SDL_SCANCODE_SPACE] = 1;
    input = read_player_movement_input(keys.data());
    CHECK(input.move_forward == doctest::Approx(1.0F));
    CHECK(input.move_up == doctest::Approx(1.0F));
    CHECK(input.jump);

    keys.fill(0);
    keys[SDL_SCANCODE_W] = 1;
    keys[SDL_SCANCODE_LSHIFT] = 1;
    input = read_player_movement_input(keys.data());
    CHECK(input.move_forward == doctest::Approx(1.0F));
    CHECK(input.sprint);

    keys.fill(0);
    keys[SDL_SCANCODE_LCTRL] = 1;
    input = read_player_movement_input(keys.data());
    CHECK(input.move_up == doctest::Approx(-1.0F));
    CHECK_FALSE(input.sprint);
}

TEST_CASE("drop action key follows the physical q position to avoid azerty movement conflicts") {
    SDL_Keysym qwerty_drop {};
    qwerty_drop.sym = SDLK_q;
    qwerty_drop.scancode = SDL_SCANCODE_Q;
    CHECK(is_drop_action_key(qwerty_drop));

    SDL_Keysym azerty_drop {};
    azerty_drop.sym = SDLK_a;
    azerty_drop.scancode = SDL_SCANCODE_Q;
    CHECK(is_drop_action_key(azerty_drop));

    SDL_Keysym azerty_move_left {};
    azerty_move_left.sym = SDLK_q;
    azerty_move_left.scancode = SDL_SCANCODE_A;
    CHECK_FALSE(is_drop_action_key(azerty_move_left));
}

TEST_CASE("default hotbar exposes starter stacks and an empty hand slot") {
    const auto hotbar = make_default_hotbar_state();

    CHECK(hotbar.slots.size() == kHotbarSlotCount);
    CHECK(hotbar.selected_index == 0);
    CHECK(hotbar.slots[0].block_id == to_block_id(BlockType::Grass));
    CHECK(hotbar.slots[0].count == 32);
    CHECK(hotbar.slots[3].block_id == to_block_id(BlockType::Cobblestone));
    CHECK(hotbar.slots[3].count == 32);
    CHECK(hotbar.slots[5].block_id == to_block_id(BlockType::Planks));
    CHECK(hotbar.slots[5].count == 32);
    CHECK(hotbar.slots[6].block_id == to_block_id(BlockType::Torch));
    CHECK(hotbar.slots[6].count == 16);
    CHECK(hotbar.slots[7].block_id == to_block_id(BlockType::Water));
    CHECK(hotbar.slots[7].count == 8);
    CHECK(hotbar.slots[8].block_id == to_block_id(BlockType::Air));
    CHECK(hotbar.slots[8].count == 0);
}

TEST_CASE("hotbar helpers sanitize corrupted item ids and selected index") {
    HotbarState hotbar {};
    hotbar.slots[0] = make_item_stack(to_block_id(BlockType::Stone), 12);
    hotbar.slots[1] = {static_cast<BlockId>(255U), 64};
    hotbar.selected_index = 999U;

    CHECK(hotbar.selected_slot().block_id == to_block_id(BlockType::Stone));
    CHECK(selected_hotbar_block(hotbar) == to_block_id(BlockType::Stone));

    normalize_item_stack(hotbar.slots[1]);
    CHECK_FALSE(hotbar_slot_has_item(hotbar.slots[1]));
    CHECK(hotbar.slots[1].block_id == to_block_id(BlockType::Air));
    CHECK(hotbar.slots[1].count == 0);
}

TEST_CASE("hotbar selection supports number keys and mouse wheel wrap") {
    HotbarState hotbar = make_default_hotbar_state();

    CHECK(hotbar_index_from_number_key(1) == 0);
    CHECK(hotbar_index_from_number_key(9) == 8);
    CHECK_FALSE(hotbar_index_from_number_key(0).has_value());

    select_hotbar_index(hotbar, 6);
    CHECK(selected_hotbar_block(hotbar) == to_block_id(BlockType::Torch));

    select_hotbar_index(hotbar, 8);
    cycle_hotbar_selection(hotbar, 1);
    CHECK(hotbar.selected_index == 0);

    cycle_hotbar_selection(hotbar, -1);
    CHECK(hotbar.selected_index == 8);
    CHECK(selected_hotbar_block(hotbar) == to_block_id(BlockType::Air));
}

TEST_CASE("selected torch slot exposes local handheld light state") {
    auto hotbar = make_default_hotbar_state();

    CHECK_FALSE(selected_hotbar_emits_local_light(hotbar));

    select_hotbar_index(hotbar, 6);
    CHECK(selected_hotbar_emits_local_light(hotbar));

    select_hotbar_index(hotbar, 7);
    CHECK_FALSE(selected_hotbar_emits_local_light(hotbar));

    hotbar.slots[6] = empty_item_stack();
    select_hotbar_index(hotbar, 6);
    CHECK_FALSE(selected_hotbar_emits_local_light(hotbar));
}

TEST_CASE("tool items are single stack non placeable hotbar items") {
    HotbarState hotbar {};
    hotbar.slots[0] = make_item_stack(to_block_id(BlockType::Pickaxe), 8);
    hotbar.slots[1] = make_item_stack(to_block_id(BlockType::Axe), 1);
    hotbar.slots[2] = make_item_stack(to_block_id(BlockType::Shovel), 1);

    CHECK(hotbar.slots[0].block_id == to_block_id(BlockType::Pickaxe));
    CHECK(hotbar.slots[0].count == 1);
    CHECK(max_item_stack_count(to_block_id(BlockType::Pickaxe)) == 1);
    CHECK(is_inventory_only_item(to_block_id(BlockType::Pickaxe)));
    CHECK(is_tool_item(to_block_id(BlockType::Axe)));
    CHECK_FALSE(is_placeable_item(to_block_id(BlockType::Shovel)));
    CHECK_FALSE(weapon_stats(to_block_id(BlockType::Pickaxe)).has_value());
    CHECK_FALSE(item_equipment_slot(to_block_id(BlockType::Shovel)).has_value());
    CHECK(inventory_item_label(to_block_id(BlockType::Pickaxe)) == "PIOCHE");
    CHECK(inventory_item_label(to_block_id(BlockType::Axe)) == "HACHE");
    CHECK(inventory_item_label(to_block_id(BlockType::Shovel)) == "PELLE");

    select_hotbar_index(hotbar, 0);
    CHECK(selected_hotbar_block(hotbar) == to_block_id(BlockType::Air));
}

TEST_CASE("musket is a non placeable single weapon with sanitized per-item chamber state") {
    CHECK(
        to_block_id(BlockType::Musket) ==
        static_cast<BlockId>(40U));

    auto musket =
        make_item_stack(
            to_block_id(BlockType::Musket),
            8U);
    CHECK(musket.count == 1U);
    CHECK(is_inventory_only_item(musket.block_id));
    CHECK(is_weapon_item(musket.block_id));
    CHECK_FALSE(is_placeable_item(musket.block_id));
    CHECK_FALSE(item_equipment_slot(musket.block_id).has_value());
    CHECK_FALSE(weapon_stats(musket.block_id).has_value());
    CHECK(inventory_item_label(musket.block_id) == "FUSIL");
    CHECK(is_musket_loaded(musket));

    HotbarState hotbar {};
    InventoryMenuState inventory {};
    hotbar.slots[0] = musket;
    inventory.equipment_slots[
        equipment_slot_index(
            EquipmentSlot::Weapon)] =
        make_item_stack(
            to_block_id(BlockType::Sword),
            1U);
    CHECK_FALSE(
        inventory_active_weapon_stats(
            inventory,
            hotbar)
            .has_value());

    set_musket_loaded(musket, false);
    CHECK_FALSE(is_musket_loaded(musket));
    normalize_item_stack(musket);
    CHECK_FALSE(is_musket_loaded(musket));

    musket.instance_state = 0xFFU;
    normalize_item_stack(musket);
    CHECK(musket.instance_state == kMusketLoadedInstanceState);
    CHECK(is_musket_loaded(musket));

    auto stone =
        HotbarSlot {
            to_block_id(BlockType::Stone),
            1U,
            0xFFU,
        };
    normalize_item_stack(stone);
    CHECK(stone.instance_state == 0U);
}

TEST_CASE("l'echine du leviathan est une arme unique non placable hors statistiques legacy") {
    CHECK(
        to_block_id(
            BlockType::LeviathanSpine) ==
        static_cast<BlockId>(41U));
    CHECK(
        is_known_block_id(
            static_cast<BlockId>(41U)));
    CHECK_FALSE(
        is_known_block_id(
            static_cast<BlockId>(
                to_block_id(
                    BlockType::BackroomsRampNegativeZ) +
                1U)));

    const auto weapon =
        make_item_stack(
            to_block_id(
                BlockType::LeviathanSpine),
            64U);
    CHECK(weapon.count == 1U);
    CHECK(weapon.instance_state == 0U);
    CHECK(is_inventory_only_item(weapon.block_id));
    CHECK(is_weapon_item(weapon.block_id));
    CHECK(is_legendary_weapon_item(weapon));
    CHECK(is_unique_inventory_item(weapon.block_id));
    CHECK(is_undroppable_item(weapon.block_id));
    CHECK_FALSE(item_stack_can_be_dropped(weapon));
    CHECK_FALSE(is_placeable_item(weapon.block_id));
    CHECK(
        item_equipment_slot(weapon.block_id) ==
        EquipmentSlot::Weapon);
    CHECK_FALSE(
        weapon_stats(weapon.block_id)
            .has_value());
    CHECK(
        inventory_item_label(weapon.block_id) ==
        "L'ECHINE DU LEVIATHAN");

    InventoryMenuState inventory {};
    HotbarState hotbar {};
    inventory.equipment_slots[
        equipment_slot_index(
            EquipmentSlot::Weapon)] = weapon;
    hotbar.slots[0] =
        make_item_stack(
            to_block_id(BlockType::Sword),
            1U);
    CHECK(
        inventory_has_equipped_legendary_weapon(
            inventory));
    CHECK_FALSE(
        inventory_active_weapon_stats(
            inventory,
            hotbar)
            .has_value());
}

TEST_CASE("l'inventaire deduplique et recupere l'arme legendaire sans ecraser de slot") {
    const auto weapon =
        inventory_make_slot(
            to_block_id(
                BlockType::LeviathanSpine),
            1U);
    InventoryMenuState inventory {};
    HotbarState hotbar {};
    inventory.equipment_slots[
        equipment_slot_index(
            EquipmentSlot::Weapon)] = weapon;
    hotbar.slots[0] = weapon;
    hotbar.selected_index = 0U;
    inventory.storage_slots[4] = weapon;
    inventory.carried_slot = weapon;
    inventory.carrying_item = true;

    const auto reconciled =
        inventory_reconcile_legendary_weapon(
            inventory,
            hotbar,
            true);
    CHECK(reconciled.removed_duplicates == 3U);
    CHECK(reconciled.weapon_present);
    CHECK_FALSE(reconciled.weapon_granted);
    CHECK_FALSE(reconciled.recovery_required);
    CHECK(
        inventory_legendary_weapon_count(
            inventory,
            hotbar) == 1U);
    CHECK(
        is_legendary_weapon_item(
            inventory.equipment_slots[
                equipment_slot_index(
                    EquipmentSlot::Weapon)]));

    InventoryMenuState recovered_inventory {};
    HotbarState recovered_hotbar {};
    const auto recovered =
        inventory_reconcile_legendary_weapon(
            recovered_inventory,
            recovered_hotbar,
            true);
    CHECK(recovered.weapon_granted);
    CHECK(recovered.weapon_present);
    CHECK_FALSE(recovered.recovery_required);
    CHECK(
        inventory_legendary_weapon_count(
            recovered_inventory,
            recovered_hotbar) == 1U);

    InventoryMenuState full_inventory {};
    HotbarState full_hotbar {};
    full_inventory.storage_slots.fill(
        inventory_make_slot(
            to_block_id(BlockType::Stone),
            64U));
    full_hotbar.slots.fill(
        inventory_make_slot(
            to_block_id(BlockType::Dirt),
            64U));
    const auto full_before = full_inventory;
    const auto full_hotbar_before = full_hotbar;
    const auto pending =
        inventory_reconcile_legendary_weapon(
            full_inventory,
            full_hotbar,
            true);
    CHECK(pending.recovery_required);
    CHECK_FALSE(pending.weapon_present);
    CHECK(full_inventory == full_before);
    CHECK(full_hotbar == full_hotbar_before);

    const auto removed =
        inventory_reconcile_legendary_weapon(
            recovered_inventory,
            recovered_hotbar,
            false);
    CHECK(removed.removed_duplicates == 1U);
    CHECK_FALSE(
        inventory_has_legendary_weapon(
            recovered_inventory,
            recovered_hotbar));
}

TEST_CASE("la resistance du bouclier peut etre neutralisee par une arme a deux mains") {
    InventoryMenuState inventory {};
    inventory.equipment_slots[
        equipment_slot_index(
            EquipmentSlot::Chest)] =
        inventory_make_slot(
            to_block_id(BlockType::Pastron),
            1U);
    inventory.equipment_slots[
        equipment_slot_index(
            EquipmentSlot::Shield)] =
        inventory_make_slot(
            to_block_id(BlockType::RoundShield),
            1U);
    CHECK(
        inventory_equipment_resistance_percent(
            inventory) ==
        doctest::Approx(30.0F));
    CHECK(
        inventory_equipment_resistance_percent(
            inventory,
            true) ==
        doctest::Approx(18.0F));
}

TEST_CASE("hotbar layout stays centered and anchored to the bottom across resolutions") {
    auto hotbar = make_default_hotbar_state();
    select_hotbar_index(hotbar, 6);

    const std::array<std::pair<int, int>, 3> viewports {{{1600, 900}, {960, 540}, {480, 320}}};
    for (const auto& [width, height] : viewports) {
        const auto layout = build_hotbar_layout(width, height, hotbar);

        CHECK(layout.slots.size() == kHotbarSlotCount);
        CHECK(layout.bar_left + layout.bar_width * 0.5F == doctest::Approx(static_cast<float>(width) * 0.5F));
        CHECK(layout.bar_bottom >= layout.safe_margin);
        CHECK(layout.bar_bottom - layout.safe_margin <= std::max(2.0F, layout.slot_size * 0.07F));
        CHECK(layout.bar_bottom < static_cast<float>(height) * 0.5F);
    }

    const auto wide_layout = build_hotbar_layout(1600, 900, hotbar);
    CHECK(wide_layout.slots[6].is_selected);
    CHECK(wide_layout.slots[6].has_icon);
    CHECK(wide_layout.slots[6].icon_tile == HotbarAtlasTile {0, 3});
    CHECK(wide_layout.slots[7].has_icon);
    CHECK(wide_layout.slots[7].icon_tile == HotbarAtlasTile {7, 2});
    CHECK_FALSE(wide_layout.slots[8].has_icon);
}

TEST_CASE("gameplay hud layout keeps the gameplay cluster out of the top half across resolutions") {
    auto hotbar = make_default_hotbar_state();
    select_hotbar_index(hotbar, 6);

    const std::array<std::pair<int, int>, 3> viewports {{{1600, 900}, {960, 540}, {480, 320}}};
    for (const auto& [width, height] : viewports) {
        const auto layout = build_gameplay_hud_layout(width, height, hotbar, 20.0F, 20.0F, 10.0F, 10.0F, false);

        CHECK(layout.hotbar.bar_left + layout.hotbar.bar_width * 0.5F == doctest::Approx(static_cast<float>(width) * 0.5F));
        CHECK(layout.hotbar.bar_bottom >= layout.safe_margin);
        CHECK(layout.hotbar_panel_bottom < static_cast<float>(height) * 0.5F);
        CHECK(layout.cluster_bottom < static_cast<float>(height) * 0.5F);
        CHECK(layout.cluster_top < static_cast<float>(height) * 0.5F);
        CHECK(layout.vitals_bottom > layout.hotbar_top);
        CHECK(layout.label.center_x == doctest::Approx(static_cast<float>(width) * 0.5F));
        CHECK(gameplay_hud_label_top(layout.label) == doctest::Approx(layout.cluster_top));
    }
}

TEST_CASE("gameplay hud layout places hearts left bubbles right and only flags air row when needed") {
    auto hotbar = make_default_hotbar_state();
    select_hotbar_index(hotbar, 0);

    const auto dry_layout = build_gameplay_hud_layout(1600, 900, hotbar, 17.0F, 20.0F, 10.0F, 10.0F, false);
    CHECK_FALSE(dry_layout.air_visible);
    CHECK(dry_layout.hearts.front().bottom > dry_layout.hotbar_top);
    CHECK(dry_layout.hearts.back().x < dry_layout.label.center_x);
    CHECK(dry_layout.slots[0].bottom > dry_layout.slots[1].bottom);
    CHECK(dry_layout.slots[0].count_bottom > dry_layout.slots[0].bottom);
    CHECK_FALSE(dry_layout.slots[8].has_icon);
    CHECK(dry_layout.label.center_x == doctest::Approx(dry_layout.hotbar.bar_left + dry_layout.hotbar.bar_width * 0.5F));
    CHECK(dry_layout.label.bottom > gameplay_hud_vital_top(dry_layout.hearts.front()));

    const auto underwater_layout = build_gameplay_hud_layout(1600, 900, hotbar, 17.0F, 20.0F, 6.5F, 10.0F, true);
    CHECK(underwater_layout.air_visible);
    CHECK(underwater_layout.bubbles.front().bottom == doctest::Approx(underwater_layout.vitals_bottom));
    CHECK(underwater_layout.bubbles.front().x > underwater_layout.label.center_x);
    CHECK(underwater_layout.label.bottom > gameplay_hud_vital_top(underwater_layout.bubbles.front()));
}

TEST_CASE("vital glyph mapping resolves full half and empty states for hearts and bubbles") {
    const auto full_hearts = build_vital_glyph_fills<kHudVitalGlyphCount>(20.0F, 20.0F);
    CHECK(std::all_of(full_hearts.begin(), full_hearts.end(), [](HudGlyphFill fill) { return fill == HudGlyphFill::Full; }));

    const auto wounded_hearts = build_vital_glyph_fills<kHudVitalGlyphCount>(19.0F, 20.0F);
    CHECK(std::count(wounded_hearts.begin(), wounded_hearts.end(), HudGlyphFill::Full) == 9);
    CHECK(std::count(wounded_hearts.begin(), wounded_hearts.end(), HudGlyphFill::Half) == 1);
    CHECK(std::count(wounded_hearts.begin(), wounded_hearts.end(), HudGlyphFill::Empty) == 0);

    const auto mid_air = build_vital_glyph_fills<kHudVitalGlyphCount>(6.5F, 10.0F);
    CHECK(std::count(mid_air.begin(), mid_air.end(), HudGlyphFill::Full) == 6);
    CHECK(std::count(mid_air.begin(), mid_air.end(), HudGlyphFill::Half) == 1);
    CHECK(std::count(mid_air.begin(), mid_air.end(), HudGlyphFill::Empty) == 3);

    const auto empty_row = build_vital_glyph_fills<kHudVitalGlyphCount>(0.0F, 10.0F);
    CHECK(std::all_of(empty_row.begin(), empty_row.end(), [](HudGlyphFill fill) { return fill == HudGlyphFill::Empty; }));
}

TEST_CASE("gameplay hud level badge stays top right and exposes stable progress geometry") {
    auto hotbar = make_default_hotbar_state();

    const auto desktop = build_gameplay_hud_layout(1600, 900, hotbar, 20.0F, 20.0F, 10.0F, 10.0F, false, 0.42F);
    CHECK(desktop.level.x >= desktop.safe_margin);
    CHECK(desktop.level.y >= desktop.safe_margin);
    CHECK(desktop.level.x + desktop.level.width <= 1600.0F - desktop.safe_margin + 0.01F);
    CHECK(desktop.level.progress_fill_width == doctest::Approx(desktop.level.progress_width * 0.42F));

    const auto mobile = build_gameplay_hud_layout(480, 320, hotbar, 20.0F, 20.0F, 10.0F, 10.0F, false, 1.0F);
    CHECK(mobile.level.x + mobile.level.width <= 480.0F - mobile.safe_margin + 0.01F);
    CHECK(mobile.level.y + mobile.level.height < mobile.cluster_top);
    CHECK(mobile.level.progress_fill_width == doctest::Approx(mobile.level.progress_width));
}

TEST_CASE("gameplay hud slot mapping keeps empty slots and stack counters stable") {
    HotbarState hotbar {};
    hotbar.slots[0] = make_item_stack(to_block_id(BlockType::Stone), 1);
    hotbar.slots[1] = make_item_stack(to_block_id(BlockType::Stone), 12);
    hotbar.slots[2] = empty_item_stack();
    select_hotbar_index(hotbar, 1);

    const auto layout = build_gameplay_hud_layout(960, 540, hotbar, 20.0F, 20.0F, 10.0F, 10.0F, false);

    CHECK(layout.slots[0].has_icon);
    CHECK_FALSE(layout.slots[0].show_stack_count);
    CHECK(layout.slots[1].has_icon);
    CHECK(layout.slots[1].show_stack_count);
    CHECK(layout.slots[1].count_bottom > layout.slots[1].bottom);
    CHECK_FALSE(layout.slots[2].has_icon);
    CHECK_FALSE(layout.slots[2].show_stack_count);
    CHECK_FALSE(gameplay_hud_stack_count_visible(hotbar.slots[0]));
    CHECK(gameplay_hud_stack_count_visible(hotbar.slots[1]));
    CHECK_FALSE(gameplay_hud_stack_count_visible(hotbar.slots[2]));
}

TEST_CASE("maritime hud layout keeps survival bars inside a stable top-left panel") {
    const auto desktop = build_maritime_hud_layout(1600, 900, true, 0.74F, 0.52F, 0.88F, true, 0.35F);

    CHECK(desktop.visible);
    CHECK_FALSE(desktop.compact);
    CHECK(desktop.panel_x >= 0.0F);
    CHECK(desktop.panel_y >= 0.0F);
    CHECK(desktop.panel_x + desktop.panel_width <= 1600.0F);
    CHECK(desktop.panel_y + desktop.panel_height <= 900.0F);
    CHECK(desktop.hunger_bar.fill_width == doctest::Approx(desktop.hunger_bar.width * 0.74F));
    CHECK(desktop.thirst_bar.fill_width == doctest::Approx(desktop.thirst_bar.width * 0.52F));
    CHECK(desktop.stamina_bar.fill_width == doctest::Approx(desktop.stamina_bar.width * 0.88F));
    CHECK(desktop.fishing_bar.fill_width == doctest::Approx(desktop.fishing_bar.width * 0.35F));

    const auto compact = build_maritime_hud_layout(420, 300, true, -1.0F, 2.0F, 0.25F, false, 1.5F);
    CHECK(compact.compact);
    CHECK(compact.panel_x + compact.panel_width <= 420.0F);
    CHECK(compact.panel_y + compact.panel_height <= 300.0F);
    CHECK(compact.hunger_bar.fill_width == doctest::Approx(0.0F));
    CHECK(compact.thirst_bar.fill_width == doctest::Approx(compact.thirst_bar.width));
    CHECK(compact.stamina_bar.fill_width == doctest::Approx(compact.stamina_bar.width * 0.25F));
}

TEST_CASE("main menu layout stays centered and resolves hovered buttons") {
    MainMenuState state {};
    state.visible = true;
    state.selected_action = MainMenuAction::Play;

    const auto base_layout = build_main_menu_layout(1600, 900, state);
    state.cursor_x = base_layout.buttons[1].x + base_layout.buttons[1].width * 0.5F;
    state.cursor_y = base_layout.buttons[1].y + base_layout.buttons[1].height * 0.5F;

    const auto layout = build_main_menu_layout(1600, 900, state);

    CHECK(layout.hero_center_x == doctest::Approx(800.0F));
    CHECK(layout.tagline_center_x == doctest::Approx(800.0F));
    CHECK(layout.buttons[0].label == "JOUER");
    CHECK(layout.buttons[1].label == "AVENTURE EN MER");
    CHECK(layout.buttons[2].label == "BACKROOMS");
    CHECK(layout.buttons[3].label == "CHARGER");
    CHECK(layout.buttons[4].label == "OPTIONS");
    CHECK(layout.buttons[1].hovered);
    CHECK(layout.buttons[1].selected);

    const auto action = main_menu_action_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(action.has_value());
    CHECK(*action == MainMenuAction::SeaAdventure);
}

TEST_CASE("main menu keyboard navigation wraps across all actions") {
    CHECK(next_main_menu_action(MainMenuAction::Play, 1) == MainMenuAction::SeaAdventure);
    CHECK(next_main_menu_action(MainMenuAction::SeaAdventure, 1) == MainMenuAction::Backrooms);
    CHECK(next_main_menu_action(MainMenuAction::Backrooms, 1) == MainMenuAction::Load);
    CHECK(next_main_menu_action(MainMenuAction::Load, 1) == MainMenuAction::Options);
    CHECK(next_main_menu_action(MainMenuAction::Options, 1) == MainMenuAction::Play);
    CHECK(next_main_menu_action(MainMenuAction::Play, -1) == MainMenuAction::Options);
}

TEST_CASE("main menu layout keeps the action stack centered on compact viewports") {
    MainMenuState state {};
    state.visible = true;

    const auto layout = build_main_menu_layout(360, 240, state);

    CHECK(layout.button_stack_x + layout.button_stack_width * 0.5F == doctest::Approx(180.0F));
    CHECK(layout.button_stack_y >= 0.0F);
    CHECK(layout.button_stack_y + layout.button_stack_height < 240.0F);
}

TEST_CASE("pause menu layout stays centered and resolves hovered enabled buttons") {
    PauseMenuState state {};
    state.visible = true;
    state.selected_action = PauseMenuAction::Resume;

    const auto base_layout = build_pause_menu_layout(1600, 900, state);
    state.cursor_x = base_layout.buttons[2].x + base_layout.buttons[2].width * 0.5F;
    state.cursor_y = base_layout.buttons[2].y + base_layout.buttons[2].height * 0.5F;

    const auto layout = build_pause_menu_layout(1600, 900, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(800.0F));
    CHECK(layout.buttons[0].label == "REPRENDRE");
    CHECK(layout.buttons[1].label == "SAUVEGARDER");
    CHECK(layout.buttons[2].hovered);
    CHECK(layout.buttons[2].selected);
    CHECK(layout.buttons[4].label == "MENU PRINCIPAL");

    const auto action = pause_menu_action_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(action.has_value());
    CHECK(*action == PauseMenuAction::Load);
}

TEST_CASE("pause menu keyboard navigation wraps across the full action list") {
    CHECK(next_pause_menu_action(PauseMenuAction::Resume, 1) == PauseMenuAction::Save);
    CHECK(next_pause_menu_action(PauseMenuAction::Save, 1) == PauseMenuAction::Load);
    CHECK(next_pause_menu_action(PauseMenuAction::Load, 1) == PauseMenuAction::Options);
    CHECK(next_pause_menu_action(PauseMenuAction::Options, 1) == PauseMenuAction::ReturnToMainMenu);
    CHECK(next_pause_menu_action(PauseMenuAction::ReturnToMainMenu, 1) == PauseMenuAction::Resume);
    CHECK(next_pause_menu_action(PauseMenuAction::Resume, -1) == PauseMenuAction::ReturnToMainMenu);
}

TEST_CASE("pause menu layout stays centered on compact viewports") {
    PauseMenuState state {};
    state.visible = true;

    const auto layout = build_pause_menu_layout(280, 220, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(140.0F));
    CHECK(layout.panel_y >= 0.0F);
    CHECK(layout.panel_y + layout.panel_height <= 220.0F);
}

TEST_CASE("pause menu modern layout keeps sections and buttons contained across viewports") {
    const std::array<std::pair<int, int>, 5> viewports {{
        {1600, 900},
        {1280, 720},
        {960, 540},
        {520, 320},
        {280, 220},
    }};

    PauseMenuState state {};
    state.visible = true;

    for (const auto& [width, height] : viewports) {
        CAPTURE(width);
        CAPTURE(height);

        const auto layout = build_pause_menu_layout(width, height, state);

        CHECK(layout.panel_x >= 0.0F);
        CHECK(layout.panel_x + layout.panel_width <= static_cast<float>(width));
        CHECK(layout.panel_y >= 0.0F);
        CHECK(layout.panel_y + layout.panel_height <= static_cast<float>(height));
        CHECK(layout.header_panel_x >= layout.panel_x);
        CHECK(layout.header_panel_x + layout.header_panel_width <= layout.panel_x + layout.panel_width);
        CHECK(layout.footer_panel_x >= layout.panel_x);
        CHECK(layout.footer_panel_x + layout.footer_panel_width <= layout.panel_x + layout.panel_width);
        CHECK(layout.accent_rail_y >= layout.panel_y);
        CHECK(layout.accent_rail_y + layout.accent_rail_height <= layout.panel_y + layout.panel_height);

        float previous_button_bottom = layout.button_stack_y;
        for (const auto& button : layout.buttons) {
            CHECK(button.x >= layout.panel_x);
            CHECK(button.x + button.width <= layout.panel_x + layout.panel_width);
            CHECK(button.y >= layout.panel_y);
            CHECK(button.y + button.height <= layout.panel_y + layout.panel_height);
            CHECK(button.y >= previous_button_bottom);
            previous_button_bottom = button.y + button.height;
        }
        CHECK(layout.buttons.front().y == doctest::Approx(layout.button_stack_y));
        CHECK(layout.buttons.back().y + layout.buttons.back().height ==
              doctest::Approx(layout.button_stack_y + layout.button_stack_height));
    }
}

TEST_CASE("pause menu hit testing ignores decorative and empty areas") {
    PauseMenuState state {};
    state.visible = true;

    const auto layout = build_pause_menu_layout(1280, 720, state);

    CHECK_FALSE(pause_menu_action_at(layout, layout.title_center_x, layout.title_y).has_value());
    CHECK_FALSE(pause_menu_action_at(layout, layout.footer_center_x, layout.footer_y).has_value());
    CHECK_FALSE(pause_menu_action_at(layout, layout.panel_x + 4.0F, layout.panel_y + 4.0F).has_value());
    CHECK_FALSE(pause_menu_action_at(layout, -4.0F, -4.0F).has_value());
}

TEST_CASE("save slot menu disables empty load slots and keeps the active slot highlighted") {
    SaveSlotMenuState state {};
    state.visible = true;
    state.mode = SaveSlotMenuMode::LoadGame;
    state.parent = SaveSlotMenuParent::MainMenu;
    state.selected_index = 1;
    state.slots[1].exists = true;
    state.slots[1].seed = 4242;
    state.slots[1].time_of_day = 14.5F;
    state.slots[3].exists = true;
    state.active_slot = 3;

    const auto base_layout = build_save_slot_menu_layout(1600, 900, state);
    state.cursor_x = base_layout.cards[1].x + base_layout.cards[1].width * 0.5F;
    state.cursor_y = base_layout.cards[1].y + base_layout.cards[1].height * 0.5F;

    const auto layout = build_save_slot_menu_layout(1600, 900, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(800.0F));
    CHECK_FALSE(layout.cards[0].enabled);
    CHECK(layout.cards[1].enabled);
    CHECK(layout.cards[1].hovered);
    CHECK(layout.cards[1].selected);
    CHECK(layout.cards[1].occupied);
    CHECK(layout.cards[3].active_slot);
    CHECK(save_slot_menu_mode_title(state.mode) == "CHARGER UNE PARTIE");
    CHECK(save_slot_menu_mode_subtitle(state.mode) == "SEULS LES SLOTS EXISTANTS SONT ACTIFS");
    CHECK(first_save_slot_menu_index(state) == 1);
    CHECK(next_save_slot_menu_index(state, 1) == 3);
    CHECK(next_save_slot_menu_index(state, -1) == kSaveSlotCount);

    const auto slot = save_slot_card_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(slot.has_value());
    CHECK(*slot == 1);
}

TEST_CASE("save slot menu primary action opens occupied play slots and protects dirty loads") {
    SaveSlotMenuState state {};
    state.mode = SaveSlotMenuMode::NewGame;
    state.slots[2].exists = true;

    CHECK(save_slot_menu_mode_subtitle(state.mode) == "VIDE = NOUVELLE PARTIE, OCCUPE = OUVRIR");
    CHECK(save_slot_menu_title(state) == "CHOISIR UN SLOT");
    CHECK(save_slot_menu_subtitle(state) == "VIDE = NOUVELLE PARTIE, OCCUPE = OUVRIR");
    CHECK(resolve_save_slot_primary_action(state, 0, false) == SaveSlotPrimaryAction::StartNewGame);
    CHECK(resolve_save_slot_primary_action(state, 2, false) == SaveSlotPrimaryAction::LoadGame);

    state.new_game_mode = GameMode::SeaAdventure;
    CHECK(save_slot_menu_title(state) == "AVENTURE EN MER");
    CHECK(save_slot_menu_subtitle(state) == "CHOISIS UN SLOT POUR PRENDRE LA MER");
    state.new_game_mode = GameMode::ClassicAdventure;

    state.mode = SaveSlotMenuMode::SaveGame;
    CHECK(resolve_save_slot_primary_action(state, 2, false) == SaveSlotPrimaryAction::ConfirmOverwrite);
    CHECK(resolve_save_slot_primary_action(state, 0, false) == SaveSlotPrimaryAction::SaveGame);

    state.mode = SaveSlotMenuMode::LoadGame;
    state.parent = SaveSlotMenuParent::PauseMenu;
    CHECK(resolve_save_slot_primary_action(state, 2, true) == SaveSlotPrimaryAction::ConfirmLoad);
    CHECK(resolve_save_slot_primary_action(state, 2, false) == SaveSlotPrimaryAction::LoadGame);
}

TEST_CASE("save slot menu delete button has its own hit zone") {
    SaveSlotMenuState state {};
    state.visible = true;
    state.mode = SaveSlotMenuMode::NewGame;
    state.selected_index = 1;
    state.slots[1].exists = true;

    const auto base_layout = build_save_slot_menu_layout(1600, 900, state);
    const auto& delete_button = base_layout.cards[1];
    state.cursor_x = delete_button.delete_x + delete_button.delete_size * 0.5F;
    state.cursor_y = delete_button.delete_y + delete_button.delete_size * 0.5F;

    const auto layout = build_save_slot_menu_layout(1600, 900, state);

    CHECK(layout.cards[0].delete_visible == false);
    CHECK(layout.cards[1].delete_visible);
    CHECK(layout.cards[1].delete_hovered);
    CHECK(layout.cards[1].selected);
    CHECK_FALSE(layout.cards[1].hovered);

    const auto delete_slot = save_slot_delete_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(delete_slot.has_value());
    CHECK(*delete_slot == 1);
    CHECK_FALSE(save_slot_card_at(layout, state.cursor_x, state.cursor_y).has_value());
}

TEST_CASE("save slot menu exposes a back button in the navigation loop") {
    SaveSlotMenuState state {};
    state.visible = true;
    state.mode = SaveSlotMenuMode::NewGame;
    state.selected_index = kSaveSlotCount;

    const auto base_layout = build_save_slot_menu_layout(960, 540, state);
    state.cursor_x = base_layout.back_button.x + base_layout.back_button.width * 0.5F;
    state.cursor_y = base_layout.back_button.y + base_layout.back_button.height * 0.5F;

    const auto layout = build_save_slot_menu_layout(960, 540, state);

    CHECK(layout.back_button.hovered);
    CHECK(layout.back_button.selected);
    CHECK(save_slot_back_hovered(layout, state.cursor_x, state.cursor_y));
    CHECK(next_save_slot_menu_index(state, 1) == 0);
    CHECK(next_save_slot_menu_index(state, -1) == 7);
}

TEST_CASE("options menu layout reflects toggle labels and hovered action") {
    OptionsMenuState state {};
    state.visible = true;
    state.parent = OptionsMenuParent::MainMenu;
    state.shadows_enabled = false;
    state.post_process_enabled = true;
    state.selected_action = OptionsMenuAction::ToggleShadows;

    const auto base_layout = build_options_menu_layout(1600, 900, state);
    state.cursor_x = base_layout.buttons[1].x + base_layout.buttons[1].width * 0.5F;
    state.cursor_y = base_layout.buttons[1].y + base_layout.buttons[1].height * 0.5F;

    const auto layout = build_options_menu_layout(1600, 900, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(800.0F));
    CHECK(layout.buttons[0].label == "OMBRES  OFF");
    CHECK(layout.buttons[1].label == "POST PROCESS  ON");
    CHECK(layout.buttons[2].label == "RETOUR");
    CHECK(options_menu_subtitle(state.parent) == "REGLAGES GENERAUX");
    CHECK(layout.buttons[1].hovered);
    CHECK(layout.buttons[1].selected);

    const auto action = options_menu_action_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(action.has_value());
    CHECK(*action == OptionsMenuAction::TogglePostProcess);
}

TEST_CASE("options menu keyboard navigation wraps across toggles and back") {
    CHECK(next_options_menu_action(OptionsMenuAction::ToggleShadows, 1) == OptionsMenuAction::TogglePostProcess);
    CHECK(next_options_menu_action(OptionsMenuAction::TogglePostProcess, 1) == OptionsMenuAction::Back);
    CHECK(next_options_menu_action(OptionsMenuAction::Back, 1) == OptionsMenuAction::ToggleShadows);
    CHECK(next_options_menu_action(OptionsMenuAction::ToggleShadows, -1) == OptionsMenuAction::Back);
}

TEST_CASE("confirm dialog stays centered and resolves the hovered cancel choice") {
    ConfirmDialogState state {};
    state.visible = true;
    state.intent = ConfirmDialogIntent::LoadSlot;
    state.selected_choice = ConfirmDialogChoice::Confirm;

    const auto base_layout = build_confirm_dialog_layout(1600, 900, state);
    state.cursor_x = base_layout.buttons[1].x + base_layout.buttons[1].width * 0.5F;
    state.cursor_y = base_layout.buttons[1].y + base_layout.buttons[1].height * 0.5F;

    const auto layout = build_confirm_dialog_layout(1600, 900, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(800.0F));
    CHECK(confirm_dialog_title(state.intent) == "CHARGER CETTE PARTIE");
    CHECK(confirm_dialog_subtitle(state.intent) == "LA PROGRESSION NON SAUVEGARDEE SERA PERDUE");
    CHECK(layout.buttons[1].hovered);
    CHECK(layout.buttons[1].selected);
    CHECK(next_confirm_dialog_choice(ConfirmDialogChoice::Confirm, 1) == ConfirmDialogChoice::Cancel);

    const auto choice = confirm_dialog_choice_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(choice.has_value());
    CHECK(*choice == ConfirmDialogChoice::Cancel);
}

TEST_CASE("confirm dialog delete intent uses yes no wording") {
    ConfirmDialogState state {};
    state.visible = true;
    state.intent = ConfirmDialogIntent::DeleteSlot;
    state.selected_choice = ConfirmDialogChoice::Confirm;

    const auto layout = build_confirm_dialog_layout(1600, 900, state);

    CHECK(confirm_dialog_title(state.intent) == "VOULEZ VOUS SUPPRIMER CETTE PARTIE ?");
    CHECK(confirm_dialog_subtitle(state.intent) == "CETTE ACTION EST DEFINITIVE");
    CHECK(layout.buttons[0].label == "OUI");
    CHECK(layout.buttons[1].label == "NON");
}

TEST_CASE("death screen layout stays centered and resolves hovered respawn action") {
    DeathScreenState state {};
    state.visible = true;
    state.cause = PlayerDeathCause::Drowning;
    state.selected_action = DeathScreenAction::Respawn;

    const auto base_layout = build_death_screen_layout(1600, 900, state);
    state.cursor_x = base_layout.buttons[0].x + base_layout.buttons[0].width * 0.5F;
    state.cursor_y = base_layout.buttons[0].y + base_layout.buttons[0].height * 0.5F;

    const auto layout = build_death_screen_layout(1600, 900, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(800.0F));
    CHECK(layout.buttons[0].hovered);
    CHECK(layout.buttons[0].selected);
    CHECK(death_screen_cause_label(state.cause) == "CAUSE NOYADE");

    const auto action = death_screen_action_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(action.has_value());
    CHECK(*action == DeathScreenAction::Respawn);
}

TEST_CASE("death screen keyboard navigation wraps between actions") {
    CHECK(next_death_screen_action(DeathScreenAction::Respawn, 1) == DeathScreenAction::Quit);
    CHECK(next_death_screen_action(DeathScreenAction::Quit, 1) == DeathScreenAction::Respawn);
    CHECK(next_death_screen_action(DeathScreenAction::Respawn, -1) == DeathScreenAction::Quit);
}

TEST_CASE("default inventory menu exposes a populated storage and an empty carried slot") {
    const auto inventory = make_default_inventory_menu_state();

    CHECK(inventory.storage_slots.size() == kInventoryStorageSlotCount);
    CHECK(inventory_slot_has_item(inventory.storage_slots[0]));
    CHECK(inventory.storage_slots[0].block_id == to_block_id(BlockType::Wood));
    CHECK(inventory.storage_slots[0].count == 16);
    CHECK(inventory_slot_has_item(inventory.storage_slots[13]));
    CHECK(inventory.storage_slots[13].block_id == to_block_id(BlockType::Torch));
    CHECK(inventory.storage_slots[13].count == 16);
    CHECK(inventory.storage_slots[18].block_id == to_block_id(BlockType::Pastron));
    CHECK(inventory.storage_slots[19].block_id == to_block_id(BlockType::RoundShield));
    CHECK(inventory.storage_slots[20].block_id == to_block_id(BlockType::Sword));
    CHECK(inventory.storage_slots[21].block_id == to_block_id(BlockType::Spear));
    CHECK(inventory.storage_slots[22].block_id == to_block_id(BlockType::Shoes));
    CHECK(inventory.storage_slots[23].block_id == to_block_id(BlockType::Pants));
    CHECK_FALSE(inventory_slot_has_item(inventory.storage_slots.back()));
    CHECK(std::all_of(inventory.equipment_slots.begin(), inventory.equipment_slots.end(), [](const HotbarSlot& slot) {
        return !inventory_slot_has_item(slot);
    }));
    CHECK_FALSE(inventory.carrying_item);
    CHECK_FALSE(inventory_slot_has_item(inventory.carried_slot));
}

TEST_CASE("inventory layout stays centered and resolves hovered storage and hotbar slots") {
    auto hotbar = make_default_hotbar_state();
    auto inventory = make_default_inventory_menu_state();
    inventory.visible = true;

    auto layout = build_inventory_menu_layout(1600, 900, inventory, hotbar);
    inventory.cursor_x = layout.slots[4].x + layout.slots[4].size * 0.5F;
    inventory.cursor_y = layout.slots[4].y + layout.slots[4].size * 0.5F;

    layout = build_inventory_menu_layout(1600, 900, inventory, hotbar);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(800.0F));
    CHECK(layout.slots[4].hovered);
    CHECK(layout.slots[kInventoryStorageSlotCount + 7].is_hotbar);
    CHECK(layout.slots[kInventoryStorageSlotCount + hotbar.selected_index].is_selected_hotbar);
    const auto equipment_layout_index = kInventoryStorageSlotCount + kHotbarSlotCount + equipment_slot_index(EquipmentSlot::Weapon);
    CHECK(layout.slots[equipment_layout_index].is_equipment);
    CHECK(layout.slots[equipment_layout_index].ref.group == InventorySlotGroup::Equipment);

    const auto hovered = inventory_slot_at(layout, inventory.cursor_x, inventory.cursor_y);
    REQUIRE(hovered.has_value());
    CHECK(hovered->group == InventorySlotGroup::Storage);
    CHECK(hovered->index == 4);
}

TEST_CASE("inventory layout stays horizontally centered and visible on compact viewports") {
    const auto hotbar = make_default_hotbar_state();
    const auto inventory = make_default_inventory_menu_state();

    const auto layout = build_inventory_menu_layout(520, 320, inventory, hotbar);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(260.0F));
    CHECK(layout.panel_y >= 0.0F);
    CHECK(layout.panel_y + layout.panel_height <= 320.0F);
    CHECK(layout.panel_y + layout.panel_height > 160.0F);
}

TEST_CASE("inventory and options menus stay fully visible on compact viewports") {
    const auto hotbar = make_default_hotbar_state();
    const auto inventory = make_default_inventory_menu_state();
    OptionsMenuState options {};
    options.visible = true;

    const std::array<std::pair<int, int>, 4> viewports {{
        {320, 240},
        {360, 260},
        {480, 320},
        {520, 320},
    }};

    const auto inside = [](float x, float y, float width, float height, float viewport_width, float viewport_height) {
        return x >= -0.01F &&
               y >= -0.01F &&
               x + width <= viewport_width + 0.01F &&
               y + height <= viewport_height + 0.01F;
    };

    for (const auto& [width, height] : viewports) {
        CAPTURE(width);
        CAPTURE(height);
        const auto viewport_width = static_cast<float>(width);
        const auto viewport_height = static_cast<float>(height);
        const auto inventory_layout = build_inventory_menu_layout(width, height, inventory, hotbar);

        CHECK(inside(inventory_layout.panel_x, inventory_layout.panel_y, inventory_layout.panel_width, inventory_layout.panel_height, viewport_width, viewport_height));
        CHECK(inside(inventory_layout.header_panel_x, inventory_layout.header_panel_y, inventory_layout.header_panel_width, inventory_layout.header_panel_height, viewport_width, viewport_height));
        CHECK(inside(inventory_layout.preview_panel_x, inventory_layout.preview_panel_y, inventory_layout.preview_panel_width, inventory_layout.preview_panel_height, viewport_width, viewport_height));
        CHECK(inside(inventory_layout.storage_panel_x, inventory_layout.storage_panel_y, inventory_layout.storage_panel_width, inventory_layout.storage_panel_height, viewport_width, viewport_height));
        CHECK(inside(inventory_layout.hotbar_panel_x, inventory_layout.hotbar_panel_y, inventory_layout.hotbar_panel_width, inventory_layout.hotbar_panel_height, viewport_width, viewport_height));
        CHECK(inside(inventory_layout.detail_panel_x, inventory_layout.detail_panel_y, inventory_layout.detail_panel_width, inventory_layout.detail_panel_height, viewport_width, viewport_height));
        CHECK(inside(inventory_layout.footer_panel_x, inventory_layout.footer_panel_y, inventory_layout.footer_panel_width, inventory_layout.footer_panel_height, viewport_width, viewport_height));
        CHECK(inventory_layout.slot_size > 0.0F);

        for (const auto& slot : inventory_layout.slots) {
            CHECK(inside(slot.x, slot.y, slot.size, slot.size, viewport_width, viewport_height));
        }
        for (const auto& keycap : inventory_layout.hotbar_keycaps) {
            CHECK(inside(keycap.x, keycap.y, keycap.width, keycap.height, viewport_width, viewport_height));
        }
        for (const auto& hint : inventory_layout.footer_hints) {
            CHECK(inside(hint.x, hint.y, hint.width, hint.height, viewport_width, viewport_height));
        }

        const auto options_layout = build_options_menu_layout(width, height, options);
        CHECK(inside(options_layout.panel_x, options_layout.panel_y, options_layout.panel_width, options_layout.panel_height, viewport_width, viewport_height));
        CHECK(options_layout.title_y >= options_layout.panel_y);
        CHECK(options_layout.subtitle_y >= options_layout.title_y);
        CHECK(options_layout.subtitle_y <= options_layout.panel_y + options_layout.panel_height);
        for (const auto& button : options_layout.buttons) {
            CHECK(inside(button.x, button.y, button.width, button.height, viewport_width, viewport_height));
            CHECK(button.x >= options_layout.panel_x);
            CHECK(button.x + button.width <= options_layout.panel_x + options_layout.panel_width);
            CHECK(button.y >= options_layout.panel_y);
            CHECK(button.y + button.height <= options_layout.panel_y + options_layout.panel_height);
        }
    }
}

TEST_CASE("inventory modern layout keeps slots and footer hints inside owning panels") {
    const auto hotbar = make_default_hotbar_state();
    const auto inventory = make_default_inventory_menu_state();

    const auto layout = build_inventory_menu_layout(1280, 720, inventory, hotbar);

    const auto inside = [](float x, float y, float width, float height, float outer_x, float outer_y, float outer_width, float outer_height) {
        return x >= outer_x &&
               y >= outer_y &&
               x + width <= outer_x + outer_width &&
               y + height <= outer_y + outer_height;
    };

    for (std::size_t index = 0; index < kInventoryStorageSlotCount; ++index) {
        const auto& slot = layout.slots[index];
        CHECK(slot.ref.group == InventorySlotGroup::Storage);
        CHECK(inside(slot.x, slot.y, slot.size, slot.size, layout.storage_panel_x, layout.storage_panel_y, layout.storage_panel_width, layout.storage_panel_height));
    }

    for (std::size_t index = 0; index < kHotbarSlotCount; ++index) {
        const auto& slot = layout.slots[kInventoryStorageSlotCount + index];
        const auto& keycap = layout.hotbar_keycaps[index];
        CHECK(slot.ref.group == InventorySlotGroup::Hotbar);
        CHECK(slot.is_hotbar);
        CHECK(inside(slot.x, slot.y, slot.size, slot.size, layout.hotbar_panel_x, layout.hotbar_panel_y, layout.hotbar_panel_width, layout.hotbar_panel_height));
        CHECK(inside(keycap.x, keycap.y, keycap.width, keycap.height, layout.hotbar_panel_x, layout.hotbar_panel_y, layout.hotbar_panel_width, layout.hotbar_panel_height));
        CHECK(keycap.x + keycap.width * 0.5F == doctest::Approx(slot.x + slot.size * 0.5F));
        CHECK(keycap.number == index + 1U);
    }

    for (std::size_t index = 0; index < kEquipmentSlotCount; ++index) {
        const auto& slot = layout.slots[kInventoryStorageSlotCount + kHotbarSlotCount + index];
        CHECK(slot.ref.group == InventorySlotGroup::Equipment);
        CHECK(slot.is_equipment);
        CHECK(inside(slot.x, slot.y, slot.size, slot.size, layout.preview_panel_x, layout.preview_panel_y, layout.preview_panel_width, layout.preview_panel_height));
    }

    float previous_hint_right = layout.footer_panel_x;
    for (const auto& hint : layout.footer_hints) {
        CHECK_FALSE(hint.label.empty());
        CHECK(inside(hint.x, hint.y, hint.width, hint.height, layout.footer_panel_x, layout.footer_panel_y, layout.footer_panel_width, layout.footer_panel_height));
        CHECK(hint.x >= previous_hint_right);
        previous_hint_right = hint.x + hint.width;
    }
    CHECK(layout.footer_hints.front().emphasized);
}

TEST_CASE("inventory layout preserves visual slot order and hit tests equipment slots") {
    auto hotbar = make_default_hotbar_state();
    auto inventory = make_default_inventory_menu_state();

    auto layout = build_inventory_menu_layout(1280, 720, inventory, hotbar);
    const auto equipment_layout_index = kInventoryStorageSlotCount + kHotbarSlotCount + equipment_slot_index(EquipmentSlot::Chest);
    inventory.cursor_x = layout.slots[equipment_layout_index].x + layout.slots[equipment_layout_index].size * 0.5F;
    inventory.cursor_y = layout.slots[equipment_layout_index].y + layout.slots[equipment_layout_index].size * 0.5F;

    layout = build_inventory_menu_layout(1280, 720, inventory, hotbar);

    std::size_t storage_count = 0;
    std::size_t hotbar_count = 0;
    std::size_t equipment_count = 0;
    for (std::size_t index = 0; index < layout.slots.size(); ++index) {
        const auto& slot = layout.slots[index];
        if (index < kInventoryStorageSlotCount) {
            CHECK(slot.ref.group == InventorySlotGroup::Storage);
            ++storage_count;
        } else if (index < kInventoryStorageSlotCount + kHotbarSlotCount) {
            CHECK(slot.ref.group == InventorySlotGroup::Hotbar);
            ++hotbar_count;
        } else {
            CHECK(slot.ref.group == InventorySlotGroup::Equipment);
            ++equipment_count;
        }
    }
    CHECK(storage_count == kInventoryStorageSlotCount);
    CHECK(hotbar_count == kHotbarSlotCount);
    CHECK(equipment_count == kEquipmentSlotCount);

    const auto hovered = inventory_slot_at(layout, inventory.cursor_x, inventory.cursor_y);
    REQUIRE(hovered.has_value());
    CHECK(hovered->group == InventorySlotGroup::Equipment);
    CHECK(hovered->index == equipment_slot_index(EquipmentSlot::Chest));
    CHECK_FALSE(inventory_slot_at(layout, layout.panel_x + 2.0F, layout.panel_y + 2.0F).has_value());
}

TEST_CASE("inventory primary click can pick and swap full stacks between storage and hotbar") {
    auto hotbar = make_default_hotbar_state();
    auto inventory = make_default_inventory_menu_state();

    inventory_pick_or_swap(inventory, hotbar, {InventorySlotGroup::Storage, 0});
    CHECK(inventory.carrying_item);
    CHECK(inventory.carried_slot.block_id == to_block_id(BlockType::Wood));
    CHECK(inventory.carried_slot.count == 16);
    CHECK_FALSE(inventory_slot_has_item(inventory.storage_slots[0]));

    inventory_pick_or_swap(inventory, hotbar, {InventorySlotGroup::Hotbar, 1});
    CHECK(inventory.carrying_item);
    CHECK(inventory.carried_slot.block_id == to_block_id(BlockType::Dirt));
    CHECK(inventory.carried_slot.count == 32);
    CHECK(hotbar.slots[1].block_id == to_block_id(BlockType::Wood));
    CHECK(hotbar.slots[1].count == 16);
}

TEST_CASE("inventory primary click merges matching stacks up to 64") {
    HotbarState hotbar {};
    hotbar.slots[0] = inventory_make_slot(to_block_id(BlockType::Stone), 60);
    InventoryMenuState inventory {};
    inventory.storage_slots[0] = inventory_make_slot(to_block_id(BlockType::Stone), 8);

    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Storage, 0});
    REQUIRE(inventory.carrying_item);
    CHECK(inventory.carried_slot.count == 8);

    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Hotbar, 0});
    CHECK(hotbar.slots[0].count == 64);
    CHECK(inventory.carrying_item);
    CHECK(inventory.carried_slot.block_id == to_block_id(BlockType::Stone));
    CHECK(inventory.carried_slot.count == 4);
}

TEST_CASE("inventory secondary click splits stacks and places single items") {
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    inventory.storage_slots[0] = inventory_make_slot(to_block_id(BlockType::Wood), 9);

    inventory_secondary_click(inventory, hotbar, {InventorySlotGroup::Storage, 0});
    REQUIRE(inventory.carrying_item);
    CHECK(inventory.carried_slot.block_id == to_block_id(BlockType::Wood));
    CHECK(inventory.carried_slot.count == 5);
    CHECK(inventory.storage_slots[0].count == 4);

    inventory_secondary_click(inventory, hotbar, {InventorySlotGroup::Hotbar, 2});
    CHECK(hotbar.slots[2].block_id == to_block_id(BlockType::Wood));
    CHECK(hotbar.slots[2].count == 1);
    CHECK(inventory.carried_slot.count == 4);

    inventory_secondary_click(inventory, hotbar, {InventorySlotGroup::Hotbar, 2});
    CHECK(hotbar.slots[2].count == 2);
    CHECK(inventory.carried_slot.count == 3);
}

TEST_CASE("inventory equipment slots accept matching gear and expose combat stats") {
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    inventory.storage_slots[0] = inventory_make_slot(to_block_id(BlockType::Pastron), 1);
    inventory.storage_slots[1] = inventory_make_slot(to_block_id(BlockType::RoundShield), 1);
    inventory.storage_slots[2] = inventory_make_slot(to_block_id(BlockType::Sword), 1);
    inventory.storage_slots[3] = inventory_make_slot(to_block_id(BlockType::Stone), 8);

    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Storage, 0});
    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Equipment, equipment_slot_index(EquipmentSlot::Chest)});
    CHECK_FALSE(inventory.carrying_item);
    CHECK(inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Chest)].block_id == to_block_id(BlockType::Pastron));

    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Storage, 1});
    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Equipment, equipment_slot_index(EquipmentSlot::Shield)});
    CHECK(inventory_equipment_resistance_percent(inventory) == doctest::Approx(30.0F));

    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Storage, 2});
    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Equipment, equipment_slot_index(EquipmentSlot::Weapon)});
    const auto equipped_weapon = inventory_active_weapon_stats(inventory, hotbar);
    REQUIRE(equipped_weapon.has_value());
    CHECK(equipped_weapon->damage == doctest::Approx(6.0F));
    CHECK(equipped_weapon->range == doctest::Approx(3.1F));

    hotbar.slots[0] = inventory_make_slot(to_block_id(BlockType::Pickaxe), 1);
    select_hotbar_index(hotbar, 0);
    CHECK_FALSE(inventory_active_weapon_stats(inventory, hotbar).has_value());

    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Storage, 3});
    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Equipment, equipment_slot_index(EquipmentSlot::Feet)});
    CHECK(inventory.carrying_item);
    CHECK(inventory.carried_slot.block_id == to_block_id(BlockType::Stone));
    CHECK_FALSE(inventory_slot_has_item(inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Feet)]));
}

TEST_CASE("inventory pickup helper fills matching stacks before using empty slots") {
    HotbarState hotbar {};
    hotbar.slots[0] = inventory_make_slot(to_block_id(BlockType::Stone), 63);
    InventoryMenuState inventory {};

    const auto leftover = inventory_try_store_stack(inventory, hotbar, inventory_make_slot(to_block_id(BlockType::Stone), 4));

    CHECK_FALSE(inventory_slot_has_item(leftover));
    CHECK(hotbar.slots[0].count == 64);
    CHECK(hotbar.slots[1].block_id == to_block_id(BlockType::Stone));
    CHECK(hotbar.slots[1].count == 3);
}

TEST_CASE("inventory transfers preserve the chamber state of each musket") {
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    inventory.storage_slots[0] =
        inventory_make_slot(
            to_block_id(BlockType::Musket),
            1U);
    set_musket_loaded(
        inventory.storage_slots[0],
        false);

    inventory_primary_click(
        inventory,
        hotbar,
        {
            InventorySlotGroup::Storage,
            0U,
        });
    REQUIRE(inventory.carrying_item);
    CHECK_FALSE(
        is_musket_loaded(
            inventory.carried_slot));

    inventory_primary_click(
        inventory,
        hotbar,
        {
            InventorySlotGroup::Hotbar,
            3U,
        });
    CHECK_FALSE(inventory.carrying_item);
    CHECK(
        is_musket_item(
            hotbar.slots[3]));
    CHECK_FALSE(
        is_musket_loaded(
            hotbar.slots[3]));

    const auto removed =
        inventory_take_from_slot(
            hotbar.slots[3],
            1U);
    CHECK(is_musket_item(removed));
    CHECK_FALSE(is_musket_loaded(removed));
    CHECK_FALSE(
        inventory_slot_has_item(
            hotbar.slots[3]));

    inventory.storage_slots[1] =
        inventory_make_slot(
            to_block_id(BlockType::Musket),
            1U);
    inventory.storage_slots[2] =
        inventory_make_slot(
            to_block_id(BlockType::Musket),
            1U);
    set_musket_loaded(
        inventory.storage_slots[2],
        false);
    inventory_primary_click(
        inventory,
        hotbar,
        {
            InventorySlotGroup::Storage,
            1U,
        });
    REQUIRE(inventory.carrying_item);
    REQUIRE(
        is_musket_loaded(
            inventory.carried_slot));
    inventory_primary_click(
        inventory,
        hotbar,
        {
            InventorySlotGroup::Storage,
            2U,
        });
    CHECK_FALSE(
        is_musket_loaded(
            inventory.carried_slot));
    CHECK(
        is_musket_loaded(
            inventory.storage_slots[2]));
}

TEST_CASE("musket grant selects the first free hotbar slot and falls back transactionally") {
    InventoryMenuState inventory {};
    HotbarState hotbar {};
    hotbar.slots.fill(
        inventory_make_slot(
            to_block_id(BlockType::Stone),
            64U));
    hotbar.slots[3] =
        inventory_empty_slot();
    hotbar.slots[6] =
        inventory_empty_slot();
    select_hotbar_index(hotbar, 1U);

    const auto hotbar_grant =
        inventory_try_grant_loaded_musket(
            inventory,
            hotbar);
    REQUIRE(hotbar_grant.has_value());
    CHECK(
        *hotbar_grant ==
        InventorySlotRef {
            InventorySlotGroup::Hotbar,
            3U,
        });
    CHECK(hotbar.selected_index == 3U);
    CHECK(is_musket_loaded(hotbar.slots[3]));
    CHECK_FALSE(
        inventory_slot_has_item(
            hotbar.slots[6]));

    hotbar.slots[6] =
        inventory_make_slot(
            to_block_id(BlockType::Dirt),
            64U);
    const auto storage_grant =
        inventory_try_grant_loaded_musket(
            inventory,
            hotbar);
    REQUIRE(storage_grant.has_value());
    CHECK(
        *storage_grant ==
        InventorySlotRef {
            InventorySlotGroup::Storage,
            0U,
        });
    CHECK(hotbar.selected_index == 3U);
    CHECK(
        is_musket_loaded(
            inventory.storage_slots[0]));

    inventory.storage_slots.fill(
        inventory_make_slot(
            to_block_id(BlockType::Stone),
            64U));
    const auto inventory_before =
        inventory;
    const auto hotbar_before =
        hotbar;
    CHECK_FALSE(
        inventory_try_grant_loaded_musket(
            inventory,
            hotbar)
            .has_value());
    CHECK(inventory == inventory_before);
    CHECK(hotbar == hotbar_before);
}

TEST_CASE("inventory crafts tools from wood pine wood and planks") {
    auto hotbar = make_default_hotbar_state();
    auto inventory = make_default_inventory_menu_state();

    REQUIRE(inventory_available_tool_crafting_material(inventory, hotbar) == 96);
    REQUIRE(inventory_slot_has_item(hotbar.slots[8]) == false);

    REQUIRE(inventory_craft_tool(inventory, hotbar, to_block_id(BlockType::Pickaxe)));
    CHECK(hotbar.slots[8].block_id == to_block_id(BlockType::Pickaxe));
    CHECK(hotbar.slots[8].count == 1);
    CHECK(inventory.storage_slots[0].block_id == to_block_id(BlockType::Wood));
    CHECK(inventory.storage_slots[0].count == 13);
    CHECK(inventory_available_tool_crafting_material(inventory, hotbar) == 93);

    const std::array<BlockType, 3> accepted_materials {{
        BlockType::Wood,
        BlockType::PineWood,
        BlockType::Planks,
    }};
    for (const auto material : accepted_materials) {
        CAPTURE(static_cast<int>(material));
        InventoryMenuState material_inventory {};
        HotbarState material_hotbar {};
        material_inventory.storage_slots[0] = inventory_make_slot(to_block_id(material), 3);

        REQUIRE(inventory_craft_tool(material_inventory, material_hotbar, to_block_id(BlockType::Pickaxe)));
        CHECK_FALSE(inventory_slot_has_item(material_inventory.storage_slots[0]));
        CHECK(material_hotbar.slots[0].block_id == to_block_id(BlockType::Pickaxe));
        CHECK(material_hotbar.slots[0].count == 1);
    }

    InventoryMenuState mixed_inventory {};
    HotbarState mixed_hotbar {};
    mixed_inventory.storage_slots[0] = inventory_make_slot(to_block_id(BlockType::Wood), 1);
    mixed_hotbar.slots[2] = inventory_make_slot(to_block_id(BlockType::PineWood), 1);
    mixed_inventory.carried_slot = inventory_make_slot(to_block_id(BlockType::Planks), 1);
    mixed_inventory.carrying_item = true;

    REQUIRE(inventory_craft_tool(mixed_inventory, mixed_hotbar, to_block_id(BlockType::Axe)));
    CHECK_FALSE(inventory_slot_has_item(mixed_inventory.storage_slots[0]));
    CHECK_FALSE(inventory_slot_has_item(mixed_hotbar.slots[2]));
    CHECK_FALSE(mixed_inventory.carrying_item);
    CHECK(mixed_hotbar.slots[0].block_id == to_block_id(BlockType::Axe));

    InventoryMenuState exact_full_inventory {};
    HotbarState exact_full_hotbar {};
    exact_full_inventory.storage_slots.fill(inventory_make_slot(to_block_id(BlockType::Stone), 64));
    exact_full_hotbar.slots.fill(inventory_make_slot(to_block_id(BlockType::Dirt), 64));
    exact_full_inventory.storage_slots[0] = inventory_make_slot(to_block_id(BlockType::Wood), 3);

    REQUIRE(inventory_craft_tool(exact_full_inventory, exact_full_hotbar, to_block_id(BlockType::Shovel)));
    CHECK(exact_full_inventory.storage_slots[0].block_id == to_block_id(BlockType::Shovel));
    CHECK(exact_full_inventory.storage_slots[0].count == 1);
}

TEST_CASE("inventory tool craft leaves inventory unchanged when resources or space are missing") {
    InventoryMenuState inventory {};
    HotbarState hotbar {};
    inventory.storage_slots[0] = inventory_make_slot(to_block_id(BlockType::Wood), 2);

    const auto not_enough_inventory = inventory;
    const auto not_enough_hotbar = hotbar;
    CHECK_FALSE(inventory_craft_tool(inventory, hotbar, to_block_id(BlockType::Shovel)));
    CHECK(inventory == not_enough_inventory);
    CHECK(hotbar == not_enough_hotbar);

    InventoryMenuState full_inventory {};
    HotbarState full_hotbar {};
    full_inventory.storage_slots.fill(inventory_make_slot(to_block_id(BlockType::Stone), 64));
    full_hotbar.slots.fill(inventory_make_slot(to_block_id(BlockType::Dirt), 64));
    full_inventory.storage_slots[0] = inventory_make_slot(to_block_id(BlockType::Wood), 64);

    const auto full_inventory_before = full_inventory;
    const auto full_hotbar_before = full_hotbar;
    CHECK_FALSE(inventory_craft_tool(full_inventory, full_hotbar, to_block_id(BlockType::Shovel)));
    CHECK(full_inventory == full_inventory_before);
    CHECK(full_hotbar == full_hotbar_before);
}

TEST_CASE("inventory number key swap can move a hovered stack into an empty hotbar slot") {
    auto hotbar = make_default_hotbar_state();
    auto inventory = make_default_inventory_menu_state();

    REQUIRE_FALSE(inventory_slot_has_item(hotbar.slots[8]));
    REQUIRE(hotbar.slots[8].block_id == to_block_id(BlockType::Air));

    inventory_swap_with_hotbar(inventory, hotbar, {InventorySlotGroup::Storage, 13}, 8);

    CHECK(hotbar.slots[8].block_id == to_block_id(BlockType::Torch));
    CHECK(hotbar.slots[8].count == 16);
    CHECK_FALSE(inventory_slot_has_item(inventory.storage_slots[13]));
}

TEST_CASE("save version 19 preserves loaded and empty muskets in every item container") {
    const auto unique_suffix =
        std::to_string(
            static_cast<unsigned long long>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
    const auto save_root =
        std::filesystem::temp_directory_path() /
        ("valcraft-save-v15-musket-" +
         unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.musket_shot_sequence =
        0x1122334455667788ULL;
    snapshot.hotbar.slots[2] =
        make_item_stack(
            to_block_id(BlockType::Musket),
            1U);
    snapshot.inventory.storage_slots[4] =
        make_item_stack(
            to_block_id(BlockType::Musket),
            1U);
    set_musket_loaded(
        snapshot.inventory.storage_slots[4],
        false);
    snapshot.inventory.carried_slot =
        make_item_stack(
            to_block_id(BlockType::Musket),
            1U);
    snapshot.inventory.carrying_item = true;
    snapshot.inventory.equipment_slots[
        equipment_slot_index(
            EquipmentSlot::Chest)] =
        HotbarSlot {
            to_block_id(BlockType::Pastron),
            1U,
            0xFFU,
        };

    ItemDrop drop {};
    drop.position = {2.0F, 65.0F, 3.0F};
    drop.stack =
        make_item_stack(
            to_block_id(BlockType::Musket),
            1U);
    set_musket_loaded(
        drop.stack,
        false);
    snapshot.item_drops.push_back(drop);

    write_save_slot(
        save_root,
        0U,
        snapshot);
    {
        std::ifstream input(
            save_slot_file_path(
                save_root,
                0U),
            std::ios::binary);
        REQUIRE(input.good());
        input.seekg(8);
        auto version =
            std::uint32_t {0U};
        input.read(
            reinterpret_cast<char*>(&version),
            sizeof(version));
        REQUIRE(input.good());
        CHECK(version == 19U);
    }

    const auto loaded =
        load_save_slot(
            save_root,
            0U);
    REQUIRE(loaded.has_value());
    CHECK(
        loaded->musket_shot_sequence ==
        snapshot.musket_shot_sequence);
    CHECK(
        is_musket_loaded(
            loaded->hotbar.slots[2]));
    CHECK_FALSE(
        is_musket_loaded(
            loaded->inventory.storage_slots[4]));
    CHECK(
        is_musket_loaded(
            loaded->inventory.carried_slot));
    CHECK(
        loaded->inventory.equipment_slots[
            equipment_slot_index(
                EquipmentSlot::Chest)]
            .instance_state == 0U);
    REQUIRE(loaded->item_drops.size() == 1U);
    CHECK_FALSE(
        is_musket_loaded(
            loaded->item_drops[0].stack));

    const auto truncated_path =
        save_slot_file_path(
            save_root,
            0U);
    const auto byte_count =
        std::filesystem::file_size(
            truncated_path);
    REQUIRE(byte_count > 0U);
    constexpr auto extension_byte_count =
        4U +
        kHotbarSlotCount +
        kInventoryStorageSlotCount +
        1U +
        kEquipmentSlotCount +
        1U +
        sizeof(std::uint64_t);
    REQUIRE(
        byte_count >=
        extension_byte_count +
            kSerializedLegendaryWeaponExtensionByteCount +
            kSerializedBackroomsFlashlightExtensionByteCount +
            kSerializedBackroomsJackExtensionByteCount +
            kSerializedBackroomsLevelExtensionByteCount +
            kSerializedBackroomsMarlowExtensionByteCount);

    const auto version_11_path =
        save_slot_file_path(
            save_root,
            1U);
    std::filesystem::copy_file(
        truncated_path,
        version_11_path);
    downgrade_current_save_to_v11(
        version_11_path,
        1U);
    const auto legacy_loaded =
        load_save_slot(
            save_root,
            1U);
    REQUIRE(legacy_loaded.has_value());
    CHECK(legacy_loaded->musket_shot_sequence == 0U);
    CHECK(
        is_musket_item(
            legacy_loaded->hotbar.slots[2]));
    CHECK_FALSE(
        is_musket_loaded(
            legacy_loaded->hotbar.slots[2]));

    {
        // Je cible le premier etat apres la signature ITEM et je prouve que
        // les bits futurs ou corrompus sont masques a la frontiere binaire.
        std::fstream output(
            truncated_path,
            std::ios::binary |
                std::ios::in |
                std::ios::out);
        REQUIRE(output.good());
        const auto hotbar_state_offset =
            static_cast<std::streamoff>(
                byte_count -
                kSerializedBackroomsMarlowExtensionByteCount -
                kSerializedBackroomsLevelExtensionByteCount -
                kSerializedBackroomsJackExtensionByteCount -
                kSerializedBackroomsFlashlightExtensionByteCount -
                kSerializedLegendaryWeaponExtensionByteCount -
                extension_byte_count +
                4U +
                2U);
        output.seekp(
            hotbar_state_offset);
        constexpr auto corrupted_state =
            std::uint8_t {0xFEU};
        output.write(
            reinterpret_cast<const char*>(
                &corrupted_state),
            sizeof(corrupted_state));
        REQUIRE(output.good());
    }
    const auto sanitized =
        load_save_slot(
            save_root,
            0U);
    REQUIRE(sanitized.has_value());
    CHECK(
        sanitized->hotbar.slots[2]
            .instance_state == 0U);
    CHECK_FALSE(
        is_musket_loaded(
            sanitized->hotbar.slots[2]));

    std::filesystem::resize_file(
        truncated_path,
        byte_count - 1U);
    CHECK_FALSE(
        load_save_slot(
            save_root,
            0U)
            .has_value());

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save version 19 round trips the permanent legendary weapon state and rejects a damaged extension") {
    const auto unique_suffix =
        std::to_string(
            static_cast<unsigned long long>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
    const auto save_root =
        std::filesystem::temp_directory_path() /
        ("valcraft-save-v15-legendary-" +
         unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.legendary_weapon = {
        0x1234'5678'9ABC'DEF0ULL,
        LegendaryWeaponQuestStage::FirstCombatComplete,
        LegendaryWeaponAwakening::Awakened,
        kLegendaryWeaponRequiredMapFragments,
        144U,
        kLegendaryWeaponUpgradeMomentum |
            kLegendaryWeaponUpgradeStability,
        LegendaryWeaponCosmetic::Sovereign,
        true,
        true,
        true,
        true,
    };
    REQUIRE(
        is_valid_legendary_weapon_progression_state(
            snapshot.legendary_weapon));
    snapshot.hotbar.slots[3] =
        inventory_make_slot(
            to_block_id(
                BlockType::LeviathanSpine),
            1U);
    write_save_slot(
        save_root,
        0U,
        snapshot);

    const auto loaded =
        load_save_slot(
            save_root,
            0U);
    REQUIRE(loaded.has_value());
    CHECK(
        loaded->legendary_weapon ==
        snapshot.legendary_weapon);
    CHECK(
        inventory_legendary_weapon_count(
            loaded->inventory,
            loaded->hotbar) == 1U);
    CHECK(
        is_legendary_weapon_item(
            loaded->hotbar.slots[3]));

    const auto source =
        save_slot_file_path(
            save_root,
            0U);
    const auto source_size =
        std::filesystem::file_size(source);
    REQUIRE(
        source_size >=
        kSerializedLegendaryWeaponExtensionByteCount +
            kSerializedBackroomsFlashlightExtensionByteCount +
            kSerializedBackroomsJackExtensionByteCount +
            kSerializedBackroomsLevelExtensionByteCount +
            kSerializedBackroomsMarlowExtensionByteCount);
    const auto extension_offset =
        static_cast<std::streamoff>(
            source_size -
            kSerializedBackroomsMarlowExtensionByteCount -
            kSerializedBackroomsLevelExtensionByteCount -
            kSerializedBackroomsJackExtensionByteCount -
            kSerializedBackroomsFlashlightExtensionByteCount -
            kSerializedLegendaryWeaponExtensionByteCount);
    const auto copy_and_overwrite =
        [&](std::size_t slot,
            std::streamoff relative_offset,
            std::uint8_t value) {
            const auto path =
                save_slot_file_path(
                    save_root,
                    slot);
            std::filesystem::copy_file(
                source,
                path);
            std::fstream output(
                path,
                std::ios::binary |
                    std::ios::in |
                    std::ios::out);
            REQUIRE(output.good());
            output.seekp(
                extension_offset +
                relative_offset);
            output.write(
                reinterpret_cast<const char*>(
                    &value),
                sizeof(value));
            REQUIRE(output.good());
        };

    copy_and_overwrite(
        1U,
        0,
        static_cast<std::uint8_t>('X'));
    CHECK_FALSE(
        load_save_slot(
            save_root,
            1U)
            .has_value());

    // Je cible l'octet d'eveil apres magic, schema, identifiant et etape.
    copy_and_overwrite(
        2U,
        4 +
            static_cast<std::streamoff>(
                sizeof(std::uint8_t) +
                sizeof(std::uint64_t) +
                sizeof(std::uint8_t)),
        255U);
    CHECK_FALSE(
        load_save_slot(
            save_root,
            2U)
            .has_value());

    // Je refuse aussi un booleen non canonique au lieu de le convertir.
    copy_and_overwrite(
        3U,
        4 +
            static_cast<std::streamoff>(
                sizeof(std::uint8_t) +
                sizeof(std::uint64_t) +
                3U * sizeof(std::uint8_t) +
                2U * sizeof(std::uint32_t) +
                sizeof(std::uint8_t)),
        2U);
    CHECK_FALSE(
        load_save_slot(
            save_root,
            3U)
            .has_value());

    const auto truncated =
        save_slot_file_path(
            save_root,
            4U);
    std::filesystem::copy_file(
        source,
        truncated);
    std::filesystem::resize_file(
        truncated,
        source_size - 1U);
    CHECK_FALSE(
        load_save_slot(
            save_root,
            4U)
            .has_value());

    auto invalid = snapshot;
    invalid.legendary_weapon.awakening =
        LegendaryWeaponAwakening::Dormant;
    CHECK_THROWS_AS(
        write_save_slot(
            save_root,
            5U,
            invalid),
        std::runtime_error);

    const auto legacy_path =
        save_slot_file_path(
            save_root,
            6U);
    std::filesystem::copy_file(
        source,
        legacy_path);
    downgrade_current_save_to_v14(
        legacy_path);
    const auto legacy =
        load_save_slot(
            save_root,
            6U);
    REQUIRE(legacy.has_value());
    CHECK(
        legacy->legendary_weapon ==
        LegendaryWeaponProgressionState {});
    CHECK_FALSE(
        inventory_has_legendary_weapon(
            legacy->inventory,
            legacy->hotbar));

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save version 19 preserves the Backrooms flashlight battery and rejects a damaged extension") {
    const auto unique_suffix =
        std::to_string(
            static_cast<unsigned long long>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
    const auto save_root =
        std::filesystem::temp_directory_path() /
        ("valcraft-save-v16-flashlight-" +
         unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.game_mode =
        GameMode::Backrooms;
    snapshot.backrooms_flashlight = {
        .battery_charge = 0.37F,
        .enabled = true,
    };
    write_save_slot(
        save_root,
        0U,
        snapshot);

    const auto current =
        load_save_slot(
            save_root,
            0U);
    REQUIRE(current.has_value());
    CHECK(
        current->backrooms_flashlight
            .battery_charge ==
        doctest::Approx(0.37F));
    CHECK(
        current->backrooms_flashlight
            .enabled);

    const auto source =
        save_slot_file_path(
            save_root,
            0U);
    const auto source_size =
        std::filesystem::file_size(source);
    REQUIRE(
        source_size >=
        kSerializedBackroomsFlashlightExtensionByteCount +
            kSerializedBackroomsJackExtensionByteCount +
            kSerializedBackroomsLevelExtensionByteCount +
            kSerializedBackroomsMarlowExtensionByteCount);

    const auto legacy_path =
        save_slot_file_path(
            save_root,
            1U);
    std::filesystem::copy_file(
        source,
        legacy_path);
    downgrade_current_save_to_v15(
        legacy_path);
    const auto legacy =
        load_save_slot(
            save_root,
            1U);
    REQUIRE(legacy.has_value());
    CHECK(
        legacy->backrooms_flashlight ==
        BackroomsFlashlightState {});

    const auto corrupted_magic =
        save_slot_file_path(
            save_root,
            2U);
    std::filesystem::copy_file(
        source,
        corrupted_magic);
    {
        std::fstream output(
            corrupted_magic,
            std::ios::binary |
                std::ios::in |
                std::ios::out);
        REQUIRE(output.good());
        output.seekp(
            static_cast<std::streamoff>(
                source_size -
                kSerializedBackroomsMarlowExtensionByteCount -
                kSerializedBackroomsLevelExtensionByteCount -
                kSerializedBackroomsJackExtensionByteCount -
                kSerializedBackroomsFlashlightExtensionByteCount));
        constexpr auto invalid_magic =
            std::uint8_t {'X'};
        output.write(
            reinterpret_cast<const char*>(
                &invalid_magic),
            sizeof(invalid_magic));
        REQUIRE(output.good());
    }
    CHECK_FALSE(
        load_save_slot(
            save_root,
            2U)
            .has_value());

    const auto corrupted_bool =
        save_slot_file_path(
            save_root,
            3U);
    std::filesystem::copy_file(
        source,
        corrupted_bool);
    {
        std::fstream output(
            corrupted_bool,
            std::ios::binary |
                std::ios::in |
                std::ios::out);
        REQUIRE(output.good());
        output.seekp(
            static_cast<std::streamoff>(
                source_size -
                kSerializedBackroomsMarlowExtensionByteCount -
                kSerializedBackroomsLevelExtensionByteCount -
                kSerializedBackroomsJackExtensionByteCount -
                1U));
        constexpr auto invalid_bool =
            std::uint8_t {2U};
        output.write(
            reinterpret_cast<const char*>(
                &invalid_bool),
            sizeof(invalid_bool));
        REQUIRE(output.good());
    }
    CHECK_FALSE(
        load_save_slot(
            save_root,
            3U)
            .has_value());

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save version 19 restaure le niveau Poolrooms et refuse un plan d'un autre etage") {
    const auto unique_suffix =
        std::to_string(
            static_cast<unsigned long long>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
    const auto save_root =
        std::filesystem::temp_directory_path() /
        ("valcraft-save-v18-poolrooms-" +
         unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 74'021;
    snapshot.metadata.game_mode =
        GameMode::Backrooms;
    snapshot.backrooms_level = -23;
    snapshot.backrooms_jack.logical_level = -23;
    snapshot.backrooms_marlow.logical_level = -23;

    WorldSavePlan plan {};
    plan.seed = snapshot.metadata.seed;
    plan.generation_profile =
        WorldGenerationProfile::Backrooms;
    plan.generation_version =
        WorldGenerationVersion::BackroomsV1;
    plan.backrooms_level = -23;
    write_save_slot(
        save_root,
        0U,
        snapshot,
        plan);

    const auto loaded =
        load_save_slot(save_root, 0U);
    REQUIRE(loaded.has_value());
    CHECK(loaded->backrooms_level == -23);
    CHECK(
        loaded->world_save_plan.backrooms_level ==
        -23);
    CHECK(
        loaded->backrooms_jack.logical_level ==
        -23);

    plan.backrooms_level = -22;
    CHECK_THROWS_AS(
        write_save_slot(
            save_root,
            1U,
            snapshot,
            plan),
        std::runtime_error);
    std::filesystem::remove_all(save_root);
}

TEST_CASE("les sauvegardes Backrooms V3 font un round trip et rejettent une version future") {
    const auto unique_suffix =
        std::to_string(
            static_cast<unsigned long long>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
    const auto save_root =
        std::filesystem::temp_directory_path() /
        ("valcraft-save-backrooms-v3-" +
         unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 91'337;
    snapshot.metadata.game_mode =
        GameMode::Backrooms;
    snapshot.backrooms_level = -23;
    snapshot.spawn_position = {4.5F, 42.001F, -6.5F};
    snapshot.player_state.position = {11.5F, 42.001F, 3.5F};
    snapshot.backrooms_jack.logical_level = -23;
    snapshot.backrooms_marlow.logical_level = -23;
    snapshot.backrooms_jack.position = {-8.5F, 42.001F, 7.5F};

    WorldSavePlan plan {};
    plan.seed = snapshot.metadata.seed;
    plan.generation_profile =
        WorldGenerationProfile::Backrooms;
    plan.generation_version =
        WorldGenerationVersion::BackroomsV3;
    plan.backrooms_level = snapshot.backrooms_level;
    write_save_slot(
        save_root,
        0U,
        snapshot,
        plan);

    const auto loaded =
        load_save_slot(save_root, 0U);
    REQUIRE(loaded.has_value());
    CHECK(
        loaded->world_save_plan.generation_version ==
        WorldGenerationVersion::BackroomsV3);
    CHECK(
        loaded->world_save_plan.backrooms_level ==
        snapshot.backrooms_level);
    CHECK(
        loaded->player_state.position.y ==
        doctest::Approx(snapshot.player_state.position.y));
    CHECK(
        loaded->backrooms_jack.position.y ==
        doctest::Approx(snapshot.backrooms_jack.position.y));

    plan.generation_version =
        WorldGenerationVersion::BackroomsV2;
    write_save_slot(
        save_root,
        2U,
        snapshot,
        plan);
    const auto loaded_v2 =
        load_save_slot(save_root, 2U);
    REQUIRE(loaded_v2.has_value());
    CHECK(
        loaded_v2->world_save_plan.generation_version ==
        WorldGenerationVersion::BackroomsV2);
    plan.generation_version =
        WorldGenerationVersion::BackroomsV3;

    // Je forge uniquement l'identifiant de génération pour vérifier que le
    // lecteur refuse une géographie inconnue sans interpréter son payload.
    write_save_slot(
        save_root,
        1U,
        snapshot,
        plan);
    overwrite_save_generation_version(
        save_slot_file_path(save_root, 1U),
        static_cast<WorldGenerationVersion>(8U));
    CHECK_FALSE(
        load_save_slot(save_root, 1U)
            .has_value());

    std::filesystem::remove_all(save_root);
}

TEST_CASE("la migration Backrooms V1 V2 vide les chunks et projette toutes les positions persistantes en V3") {
    constexpr auto seed = 74'021;
    constexpr auto anchor = 0;
    const BackroomsSpatialStack legacy_stack(
        seed,
        anchor,
        BackroomsSpatialProfile::LegacyV2);
    const auto pool_spawn = legacy_stack.spawn_block(-2);
    const auto office_spawn = legacy_stack.spawn_block(-1);
    const auto position_for =
        [](const BlockCoord& block) noexcept {
            return glm::vec3 {
                static_cast<float>(block.x) + 0.5F,
                static_cast<float>(block.y) + 0.001F,
                static_cast<float>(block.z) + 0.5F,
            };
        };
    const auto legacy_pool_position = position_for(pool_spawn);
    const auto legacy_office_position = position_for(office_spawn);

    SaveGameSnapshot v2 {};
    v2.metadata.seed = seed;
    v2.metadata.game_mode = GameMode::Backrooms;
    v2.metadata.modified_chunk_count = 7U;
    v2.backrooms_level = anchor;
    v2.spawn_position = legacy_office_position;
    v2.player_state.position = legacy_pool_position;
    v2.player_state.fall_start_y = legacy_pool_position.y + 12.0F;
    v2.backrooms_jack.position = legacy_pool_position;
    v2.backrooms_jack.last_seen_player_position =
        legacy_office_position;
    v2.backrooms_jack.previous_player_position =
        legacy_pool_position;
    v2.world_save_plan.seed = seed;
    v2.world_save_plan.generation_profile =
        WorldGenerationProfile::Backrooms;
    v2.world_save_plan.generation_version =
        WorldGenerationVersion::BackroomsV2;
    v2.world_save_plan.backrooms_level = anchor;
    WorldSavePlanChunk modified_chunk {};
    modified_chunk.coord = {3, -4};
    modified_chunk.sparse_cells.push_back({
        0U,
        to_block_id(BlockType::PoolroomsTile),
        WaterState {0},
    });
    v2.world_save_plan.chunks.push_back(
        std::move(modified_chunk));

    migrate_backrooms_snapshot_to_v3(
        v2,
        WorldGenerationVersion::BackroomsV2,
        anchor);

    CHECK(
        v2.world_save_plan.generation_version ==
        WorldGenerationVersion::BackroomsV3);
    CHECK(v2.world_save_plan.chunks.empty());
    CHECK(v2.metadata.modified_chunk_count == 0U);
    CHECK(
        v2.spawn_position.y ==
        doctest::Approx(legacy_office_position.y));
    CHECK(
        v2.player_state.position.y ==
        doctest::Approx(legacy_pool_position.y + 1.0F));
    CHECK(
        v2.player_state.fall_start_y ==
        doctest::Approx(v2.player_state.position.y));
    CHECK(
        v2.backrooms_jack.position.y ==
        doctest::Approx(legacy_pool_position.y + 1.0F));
    CHECK(
        v2.backrooms_jack.last_seen_player_position.y ==
        doctest::Approx(legacy_office_position.y));
    CHECK(
        v2.backrooms_jack.previous_player_position.y ==
        doctest::Approx(legacy_pool_position.y + 1.0F));
    CHECK(v2.player_state.position.x == legacy_pool_position.x);
    CHECK(v2.player_state.position.z == legacy_pool_position.z);
    const BackroomsSpatialStack migrated_stack(
        seed,
        anchor,
        BackroomsSpatialProfile::RecessedPoolroomsV3);
    CHECK(
        migrated_stack.has_body_clearance(
            v2.player_state.position.x,
            v2.player_state.position.y,
            v2.player_state.position.z,
            3,
            0.30F));
    CHECK(
        migrated_stack.has_body_clearance(
            v2.backrooms_jack.position.x,
            v2.backrooms_jack.position.y,
            v2.backrooms_jack.position.z,
            4,
            0.42F));

    constexpr auto v1_level = -23;
    const BackroomsGenerator v1_generator(seed, v1_level);
    const auto v1_spawn = v1_generator.spawn_block();
    SaveGameSnapshot v1 {};
    v1.metadata.seed = seed;
    v1.metadata.game_mode = GameMode::Backrooms;
    v1.backrooms_level = v1_level;
    v1.player_state.position = position_for(v1_spawn);
    v1.spawn_position = v1.player_state.position;
    v1.backrooms_jack.position = v1.player_state.position;
    v1.backrooms_jack.last_seen_player_position =
        v1.player_state.position;
    v1.backrooms_jack.previous_player_position =
        v1.player_state.position;
    v1.world_save_plan.generation_profile =
        WorldGenerationProfile::Backrooms;
    v1.world_save_plan.generation_version =
        WorldGenerationVersion::BackroomsV1;
    v1.world_save_plan.backrooms_level = v1_level;
    const auto historical_y = v1.player_state.position.y;

    migrate_backrooms_snapshot_to_v3(
        v1,
        WorldGenerationVersion::BackroomsV1,
        v1_level);
    CHECK(
        v1.world_save_plan.generation_version ==
        WorldGenerationVersion::BackroomsV3);
    CHECK(
        v1.player_state.position.y ==
        doctest::Approx(historical_y + 1.0F));
    CHECK(
        v1.spawn_position.y ==
        doctest::Approx(historical_y + 1.0F));
    CHECK(
        v1.backrooms_jack.position.y ==
        doctest::Approx(historical_y + 1.0F));
}

TEST_CASE("save game scanning and loading preserve slot metadata and payloads") {
    const auto unique_suffix =
        std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.saved_at_unix_seconds = 1712185200;
    snapshot.metadata.seed = 987654;
    snapshot.metadata.time_of_day = 18.25F;
    snapshot.metadata.weather_time_seconds = 735.5F;
    snapshot.metadata.has_starting_village = true;
    snapshot.metadata.game_mode = GameMode::SeaAdventure;
    snapshot.spawn_position = {4.5F, 82.0F, -6.5F};
    snapshot.player_state.position = {11.0F, 65.0F, 3.0F};
    snapshot.player_state.velocity = {0.5F, -1.0F, 2.5F};
    snapshot.player_state.fly_mode = true;
    snapshot.player_state.health = 13.5F;
    snapshot.player_state.death_cause = PlayerDeathCause::Zombie;
    snapshot.progression = {12U, 345ULL};
    snapshot.player_build.attributes.values[
        player_attribute_index(
            PlayerAttribute::Strength)] = 2U;
    const auto vanguard_index =
        ability_index(
            AbilityId::KnightVanguardStrike);
    snapshot.player_build.ability_ranks[
        vanguard_index] = 3U;
    snapshot.player_build.equipped_abilities[0U] =
        AbilityId::KnightVanguardStrike;
    snapshot.player_build.val_energy = 73.5F;
    snapshot.player_build
        .global_cooldown_remaining = 0.125F;
    snapshot.player_build
        .energy_regeneration_delay_remaining =
        0.75F;
    snapshot.player_build.cooldowns_remaining[
        vanguard_index] = 1.2F;
    snapshot.player_build.charges[
        vanguard_index] = 0U;
    snapshot.player_build
        .successful_cast_sequence =
        77ULL;
    snapshot.player_build
        .last_dominant_path =
        AbilityPath::Commander;
    snapshot.player_build.revision =
        0x1020'3040'5060'7080ULL;
    auto& saved_plan =
        snapshot.player_build
            .construction_plans[1U];
    saved_plan.cell_count = 2U;
    saved_plan.cells[0U] = {
        0,
        0,
        0,
        to_block_id(
            BlockType::Planks),
    };
    saved_plan.cells[1U] = {
        1,
        0,
        0,
        to_block_id(
            BlockType::Planks),
    };
    snapshot.player_build
        .selected_construction_plan = 1U;
    snapshot.sea_adventure.active = true;
    snapshot.sea_adventure.ship_position = {0.5F, 49.0F, 118.25F};
    snapshot.sea_adventure.route_distance = 512.75F;
    snapshot.sea_adventure.hunger = 72.0F;
    snapshot.sea_adventure.thirst = 61.0F;
    snapshot.sea_adventure.stamina = 45.0F;
    snapshot.sea_adventure.fishing_progress = 3.25F;
    snapshot.sea_adventure.fishing_target_seconds = 8.5F;
    snapshot.sea_adventure.survival_damage_timer = 0.5F;
    snapshot.sea_adventure.stranded_warning_timer = 2.0F;
    snapshot.sea_adventure.food_rations = 4U;
    snapshot.sea_adventure.water_flasks = 3U;
    snapshot.sea_adventure.fish = 2U;
    snapshot.sea_adventure.wood = 7U;
    snapshot.sea_adventure.stone = 5U;
    snapshot.sea_adventure.fiber = 9U;
    snapshot.sea_adventure.stamped_ship_x = 0;
    snapshot.sea_adventure.stamped_ship_z = 118;
    snapshot.sea_adventure.has_stamped_ship = true;
    snapshot.sea_adventure.fishing_active = true;
    snapshot.sea_adventure.voyage_phase = SeaVoyagePhase::Departing;
    snapshot.sea_adventure.voyage_phase_elapsed = 4.25F;
    snapshot.sea_adventure.crew = make_save_test_crew_state();
    snapshot.hotbar.slots[0] = make_item_stack(to_block_id(BlockType::Stone), 12);
    snapshot.hotbar.slots[4] = make_item_stack(to_block_id(BlockType::Torch), 16);
    snapshot.hotbar.slots[8] = make_item_stack(to_block_id(BlockType::Pickaxe), 1);
    snapshot.hotbar.selected_index = 4;
    snapshot.inventory.storage_slots[0] = make_item_stack(to_block_id(BlockType::Wood), 8);
    snapshot.inventory.storage_slots[7] = make_item_stack(to_block_id(BlockType::Water), 2);
    snapshot.inventory.storage_slots[8] = make_item_stack(to_block_id(BlockType::DiamondOre), 5);
    snapshot.inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Chest)] =
        make_item_stack(to_block_id(BlockType::Pastron), 1);
    snapshot.inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Weapon)] =
        make_item_stack(to_block_id(BlockType::Spear), 1);
    snapshot.inventory.carried_slot = make_item_stack(to_block_id(BlockType::Dirt), 5);
    snapshot.inventory.carrying_item = true;

    CreatureInstance creature {};
    creature.anchor.chunk = {2, -1};
    creature.anchor.ground_block = {33, 64, -14};
    creature.anchor.spawn_position = {33.5F, 65.0F, -13.5F};
    creature.anchor.species = CreatureSpecies::Cow;
    creature.position = {34.0F, 65.0F, -12.0F};
    creature.yaw_radians = 1.25F;
    creature.behavior_seed = 42;
    creature.appearance_seed = 7;
    creature.behavior_state = CreatureBehaviorState::Wander;
    creature.health = 4.5F;
    snapshot.creatures.push_back(creature);

    ItemDrop drop {};
    drop.position = {9.0F, 66.0F, 2.0F};
    drop.velocity = {0.0F, 1.0F, 0.0F};
    drop.stack = make_item_stack(to_block_id(BlockType::IronOre), 3);
    drop.age_seconds = 1.5F;
    drop.pickup_cooldown = 0.75F;
    drop.grounded = true;
    snapshot.item_drops.push_back(drop);

    ItemDrop tool_drop {};
    tool_drop.position = {10.0F, 66.0F, 2.0F};
    tool_drop.velocity = {0.0F, 0.5F, 0.0F};
    tool_drop.stack = make_item_stack(to_block_id(BlockType::Shovel), 1);
    tool_drop.age_seconds = 0.5F;
    tool_drop.pickup_cooldown = 0.25F;
    tool_drop.grounded = false;
    snapshot.item_drops.push_back(tool_drop);

    WorldChunkSnapshot chunk_snapshot {};
    chunk_snapshot.coord = {1, 2};
    chunk_snapshot.blocks.fill(to_block_id(BlockType::Air));
    chunk_snapshot.blocks[0] = to_block_id(BlockType::Stone);
    chunk_snapshot.blocks[1] = to_block_id(BlockType::Torch);
    chunk_snapshot.blocks[2] = to_block_id(BlockType::GoldOre);
    snapshot.chunk_snapshots.push_back(chunk_snapshot);

    write_save_slot(save_root, 2, snapshot);

    const auto scanned = scan_save_slots(save_root);
    CHECK_FALSE(scanned[0].exists);
    CHECK(scanned[2].exists);
    CHECK(scanned[2].saved_at_unix_seconds == snapshot.metadata.saved_at_unix_seconds);
    CHECK(scanned[2].seed == snapshot.metadata.seed);
    CHECK(scanned[2].time_of_day == doctest::Approx(snapshot.metadata.time_of_day));
    CHECK(scanned[2].weather_time_seconds == doctest::Approx(snapshot.metadata.weather_time_seconds));
    CHECK(scanned[2].modified_chunk_count == 1);
    CHECK(scanned[2].has_starting_village);
    CHECK(scanned[2].game_mode == GameMode::SeaAdventure);

    const auto loaded = load_save_slot(save_root, 2);
    REQUIRE(loaded.has_value());
    CHECK(loaded->metadata.exists);
    CHECK(loaded->metadata.seed == snapshot.metadata.seed);
    CHECK(loaded->metadata.weather_time_seconds == doctest::Approx(snapshot.metadata.weather_time_seconds));
    CHECK(loaded->metadata.has_starting_village);
    CHECK(loaded->metadata.game_mode == GameMode::SeaAdventure);
    CHECK(loaded->sea_adventure.active);
    CHECK(loaded->sea_adventure.ship_position.x == doctest::Approx(snapshot.sea_adventure.ship_position.x));
    CHECK(loaded->sea_adventure.ship_position.y == doctest::Approx(snapshot.sea_adventure.ship_position.y));
    CHECK(loaded->sea_adventure.ship_position.z == doctest::Approx(snapshot.sea_adventure.ship_position.z));
    CHECK(loaded->sea_adventure.route_distance == doctest::Approx(snapshot.sea_adventure.route_distance));
    CHECK(loaded->sea_adventure.hunger == doctest::Approx(snapshot.sea_adventure.hunger));
    CHECK(loaded->sea_adventure.thirst == doctest::Approx(snapshot.sea_adventure.thirst));
    CHECK(loaded->sea_adventure.stamina == doctest::Approx(snapshot.sea_adventure.stamina));
    CHECK(loaded->sea_adventure.fishing_progress == doctest::Approx(snapshot.sea_adventure.fishing_progress));
    CHECK(loaded->sea_adventure.fishing_target_seconds == doctest::Approx(snapshot.sea_adventure.fishing_target_seconds));
    CHECK(loaded->sea_adventure.food_rations == snapshot.sea_adventure.food_rations);
    CHECK(loaded->sea_adventure.water_flasks == snapshot.sea_adventure.water_flasks);
    CHECK(loaded->sea_adventure.fish == snapshot.sea_adventure.fish);
    CHECK(loaded->sea_adventure.wood == snapshot.sea_adventure.wood);
    CHECK(loaded->sea_adventure.stone == snapshot.sea_adventure.stone);
    CHECK(loaded->sea_adventure.fiber == snapshot.sea_adventure.fiber);
    CHECK(loaded->sea_adventure.stamped_ship_x == snapshot.sea_adventure.stamped_ship_x);
    CHECK(loaded->sea_adventure.stamped_ship_z == snapshot.sea_adventure.stamped_ship_z);
    CHECK(loaded->sea_adventure.has_stamped_ship);
    CHECK(loaded->sea_adventure.fishing_active);
    CHECK(loaded->sea_adventure.voyage_phase == SeaVoyagePhase::Departing);
    CHECK(loaded->sea_adventure.voyage_phase_elapsed == doctest::Approx(4.25F));
    CHECK(loaded->sea_adventure.crew == sanitize_ship_crew_save_state(snapshot.sea_adventure.crew));
    CHECK(loaded->spawn_position.x == doctest::Approx(snapshot.spawn_position.x));
    CHECK(loaded->spawn_position.y == doctest::Approx(snapshot.spawn_position.y));
    CHECK(loaded->spawn_position.z == doctest::Approx(snapshot.spawn_position.z));
    CHECK(loaded->player_state.position.x == doctest::Approx(snapshot.player_state.position.x));
    CHECK(loaded->player_state.position.y == doctest::Approx(snapshot.player_state.position.y));
    CHECK(loaded->player_state.position.z == doctest::Approx(snapshot.player_state.position.z));
    CHECK(loaded->player_state.velocity.x == doctest::Approx(snapshot.player_state.velocity.x));
    CHECK(loaded->player_state.velocity.y == doctest::Approx(snapshot.player_state.velocity.y));
    CHECK(loaded->player_state.velocity.z == doctest::Approx(snapshot.player_state.velocity.z));
    CHECK(loaded->player_state.fly_mode == snapshot.player_state.fly_mode);
    CHECK(loaded->player_state.health == doctest::Approx(snapshot.player_state.health));
    CHECK(loaded->player_state.death_cause == snapshot.player_state.death_cause);
    CHECK(loaded->progression.level == 12U);
    CHECK(loaded->progression.experience == 345ULL);
    CHECK(
        loaded->player_build.attributes.values[
            player_attribute_index(
                PlayerAttribute::Strength)] ==
        2U);
    CHECK(
        loaded->player_build.ability_ranks[
            vanguard_index] == 3U);
    CHECK(
        loaded->player_build.equipped_abilities[0U] ==
        AbilityId::KnightVanguardStrike);
    CHECK(
        loaded->player_build.val_energy ==
        doctest::Approx(73.5F));
    CHECK(
        loaded->player_build
            .global_cooldown_remaining ==
        doctest::Approx(0.125F));
    CHECK(
        loaded->player_build
            .energy_regeneration_delay_remaining ==
        doctest::Approx(0.75F));
    CHECK(
        loaded->player_build.cooldowns_remaining[
            vanguard_index] ==
        doctest::Approx(1.2F));
    CHECK(
        loaded->player_build.charges[
            vanguard_index] == 0U);
    CHECK(
        loaded->player_build
            .successful_cast_sequence ==
        77ULL);
    CHECK(
        loaded->player_build
            .last_dominant_path ==
        AbilityPath::Commander);
    CHECK(
        loaded->player_build.revision ==
        0x1020'3040'5060'7080ULL);
    CHECK(
        loaded->player_build
            .selected_construction_plan ==
        1U);
    REQUIRE(
        loaded->player_build
            .construction_plans[1U]
            .cell_count == 2U);
    CHECK(
        loaded->player_build
            .construction_plans[1U]
            .cells[1U]
            .material_id ==
        to_block_id(
            BlockType::Planks));
    CHECK(loaded->hotbar.selected_index == 4);
    CHECK(loaded->hotbar.slots[4].block_id == to_block_id(BlockType::Torch));
    CHECK(loaded->hotbar.slots[8].block_id == to_block_id(BlockType::Pickaxe));
    CHECK(loaded->inventory.storage_slots[7].block_id == to_block_id(BlockType::Water));
    CHECK(loaded->inventory.storage_slots[8].block_id == to_block_id(BlockType::DiamondOre));
    CHECK(loaded->inventory.storage_slots[8].count == 5);
    CHECK(loaded->inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Chest)].block_id ==
          to_block_id(BlockType::Pastron));
    CHECK(loaded->inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Weapon)].block_id ==
          to_block_id(BlockType::Spear));
    CHECK(inventory_equipment_resistance_percent(loaded->inventory) == doctest::Approx(18.0F));
    CHECK(loaded->inventory.carrying_item);
    REQUIRE(loaded->creatures.size() == 1);
    CHECK(loaded->creatures[0].anchor.species == CreatureSpecies::Cow);
    CHECK(loaded->creatures[0].behavior_state == CreatureBehaviorState::Wander);
    CHECK(loaded->creatures[0].health == doctest::Approx(4.5F));
    REQUIRE(loaded->item_drops.size() == 2);
    CHECK(loaded->item_drops[0].grounded);
    CHECK(loaded->item_drops[0].stack.block_id == to_block_id(BlockType::IronOre));
    CHECK(loaded->item_drops[0].stack.count == 3);
    CHECK_FALSE(loaded->item_drops[1].grounded);
    CHECK(loaded->item_drops[1].stack.block_id == to_block_id(BlockType::Shovel));
    CHECK(loaded->item_drops[1].stack.count == 1);
    CHECK(loaded->chunk_snapshots.empty());
    REQUIRE(loaded->world_save_plan.chunks.size() == 1);
    CHECK(loaded->world_save_plan.generation_version == WorldGenerationVersion::LivingOceanV3);
    CHECK(loaded->world_save_plan.chunks[0].coord == chunk_snapshot.coord);
    CHECK(loaded->world_save_plan.chunks[0].dense_blocks[1] == to_block_id(BlockType::Torch));
    CHECK(loaded->world_save_plan.chunks[0].dense_blocks[2] == to_block_id(BlockType::GoldOre));

    std::filesystem::remove_all(save_root);
}

TEST_CASE("sea adventure save payload is normalized at the binary boundary") {
    const auto unique_suffix =
        std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-sea-save-sanitize-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 4419;
    snapshot.metadata.game_mode = GameMode::SeaAdventure;
    snapshot.sea_adventure.active = true;
    snapshot.sea_adventure.ship_position = {
        23.5F,
        -900.0F,
        -2'000'000.0F,
    };
    snapshot.sea_adventure.route_distance = -std::numeric_limits<float>::infinity();
    snapshot.sea_adventure.hunger = std::numeric_limits<float>::quiet_NaN();
    snapshot.sea_adventure.thirst = -10.0F;
    snapshot.sea_adventure.stamina = 150.0F;
    snapshot.sea_adventure.survival_damage_timer = std::numeric_limits<float>::max();
    snapshot.sea_adventure.stranded_warning_timer = std::numeric_limits<float>::max();
    snapshot.sea_adventure.fishing_active = true;
    snapshot.sea_adventure.fishing_progress = 4.0F;
    snapshot.sea_adventure.fishing_target_seconds = 0.0F;
    snapshot.sea_adventure.stamped_ship_x = std::numeric_limits<std::int32_t>::max();
    snapshot.sea_adventure.stamped_ship_z = std::numeric_limits<std::int32_t>::min();
    snapshot.sea_adventure.has_stamped_ship = true;
    snapshot.sea_adventure.voyage_phase = static_cast<SeaVoyagePhase>(255U);
    snapshot.sea_adventure.voyage_phase_elapsed = std::numeric_limits<float>::quiet_NaN();

    write_save_slot(save_root, 0, snapshot);

    const auto loaded = load_save_slot(save_root, 0);
    REQUIRE(loaded.has_value());
    const auto& sea = loaded->sea_adventure;
    CHECK(sea.active);
    CHECK(sea.ship_position.x == doctest::Approx(0.5F));
    CHECK(sea.ship_position.y == doctest::Approx(49.0F));
    CHECK(sea.ship_position.z == doctest::Approx(0.5F));
    CHECK(sea.route_distance == doctest::Approx(0.0F));
    CHECK(sea.hunger == doctest::Approx(100.0F));
    CHECK(sea.thirst == doctest::Approx(0.0F));
    CHECK(sea.stamina == doctest::Approx(100.0F));
    CHECK(sea.survival_damage_timer >= 0.0F);
    CHECK(sea.survival_damage_timer < 1.75F);
    CHECK(sea.stranded_warning_timer >= 0.0F);
    CHECK(sea.stranded_warning_timer < 8.0F);
    CHECK_FALSE(sea.fishing_active);
    CHECK(sea.fishing_progress == doctest::Approx(0.0F));
    CHECK(sea.fishing_target_seconds == doctest::Approx(0.0F));
    CHECK_FALSE(sea.has_stamped_ship);
    CHECK(sea.stamped_ship_x == 0);
    CHECK(sea.stamped_ship_z == 0);
    CHECK(sea.voyage_phase == SeaVoyagePhase::Underway);
    CHECK(sea.voyage_phase_elapsed == doctest::Approx(0.0F));

    std::filesystem::remove_all(save_root);
}

TEST_CASE("version 11 sea crew and guard payload preserves the version 9 crew contract") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-v11-crew-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 74'103;
    snapshot.metadata.game_mode = GameMode::SeaAdventure;
    snapshot.sea_adventure.active = true;
    snapshot.sea_adventure.voyage_phase = SeaVoyagePhase::Departing;
    snapshot.sea_adventure.voyage_phase_elapsed = 3.5F;
    snapshot.sea_adventure.crew = make_save_test_crew_state();
    const auto expected_crew = sanitize_ship_crew_save_state(snapshot.sea_adventure.crew);

    write_save_slot(save_root, 0, snapshot);
    downgrade_current_save_to_v11(
        save_slot_file_path(save_root, 0));
    const auto round_trip = load_save_slot(save_root, 0);
    REQUIRE(round_trip.has_value());
    CHECK(round_trip->sea_adventure.crew == expected_crew);
    CHECK(round_trip->sea_adventure.voyage_phase == SeaVoyagePhase::Departing);
    CHECK(round_trip->sea_adventure.voyage_phase_elapsed == doctest::Approx(3.5F));

    const auto& last_member = expected_crew.members.back();
    overwrite_unique_binary_sequence(
        save_slot_file_path(save_root, 0),
        std::array<float, 3> {
            last_member.local_position.x,
            last_member.local_position.y,
            last_member.local_position.z,
        },
        std::array<float, 3> {
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            (std::numeric_limits<float>::max)(),
        });

    const auto serialized_tail = std::array<std::uint8_t, 8> {
        last_member.id,
        last_member.routine_step,
        static_cast<std::uint8_t>(last_member.role),
        static_cast<std::uint8_t>(last_member.activity),
        static_cast<std::uint8_t>(last_member.cargo),
        static_cast<std::uint8_t>(last_member.current_station),
        static_cast<std::uint8_t>(last_member.next_station),
        static_cast<std::uint8_t>(last_member.destination_station),
    };
    overwrite_unique_binary_sequence(
        save_slot_file_path(save_root, 0),
        serialized_tail,
        std::array<std::uint8_t, 8> {254U, 253U, 252U, 251U, 250U, 249U, 248U, 247U});

    const auto corrupted = load_save_slot(save_root, 0);
    REQUIRE(corrupted.has_value());
    const auto& crew = corrupted->sea_adventure.crew;
    CHECK(crew.initialized);
    CHECK(crew.navigation_revision == expected_crew.navigation_revision);
    for (std::size_t index = 0; index + 1U < crew.members.size(); ++index) {
        CHECK(crew.members[index] == expected_crew.members[index]);
    }

    const auto& recovered_member = crew.members.back();
    CHECK(recovered_member.local_position == glm::vec3 {0.0F});
    CHECK(recovered_member.id == 5U);
    CHECK(recovered_member.routine_step == 1U);
    CHECK(recovered_member.role == ShipCrewRole::Quartermaster);
    CHECK(recovered_member.activity == ShipCrewActivity::Idle);
    CHECK(recovered_member.cargo == ShipCrewCargo::None);
    CHECK(recovered_member.current_station == ShipCrewStation::CargoSort);
    CHECK(recovered_member.next_station == ShipCrewStation::CargoSort);
    CHECK(recovered_member.destination_station == ShipCrewStation::CargoSort);

    write_save_slot(save_root, 1, snapshot);
    downgrade_current_save_to_v11(
        save_slot_file_path(save_root, 1));
    truncate_inside_unique_binary_sequence(
        save_slot_file_path(save_root, 1),
        std::array<float, 3> {
            last_member.local_position.x,
            last_member.local_position.y,
            last_member.local_position.z,
        });
    CHECK(scan_save_slots(save_root)[1].exists);
    CHECK_FALSE(load_save_slot(save_root, 1).has_value());

    WorldSavePlan version_9_plan {};
    version_9_plan.seed = snapshot.metadata.seed;
    version_9_plan.generation_profile = WorldGenerationProfile::OceanAdventure;
    version_9_plan.generation_version = WorldGenerationVersion::LegacyV1;
    write_save_slot(save_root, 2, snapshot, version_9_plan);
    downgrade_current_sea_save_to_v9(
        save_slot_file_path(save_root, 2),
        snapshot.sea_adventure.voyage_phase,
        snapshot.sea_adventure.voyage_phase_elapsed);
    const auto version_9 = load_save_slot(save_root, 2);
    REQUIRE(version_9.has_value());
    CHECK(version_9->sea_adventure.crew == expected_crew);
    CHECK(version_9->sea_adventure.voyage_phase == SeaVoyagePhase::Underway);
    CHECK(version_9->sea_adventure.voyage_phase_elapsed == doctest::Approx(0.0F));
    CHECK(version_9->world_save_plan.generation_version == WorldGenerationVersion::LegacyV1);

    // Je verifie aussi le chemin public sans plan separe : un snapshot charge
    // doit conserver sa revision V1 meme lorsqu'il ne contient aucun override.
    write_save_slot(save_root, 1, *version_9);
    const auto upgraded_version_9 = load_save_slot(save_root, 1);
    REQUIRE(upgraded_version_9.has_value());
    CHECK(upgraded_version_9->world_save_plan.generation_version == WorldGenerationVersion::LegacyV1);
    CHECK(upgraded_version_9->sea_adventure.voyage_phase == SeaVoyagePhase::Underway);

    overwrite_save_generation_version(
        save_slot_file_path(save_root, 2),
        WorldGenerationVersion::SparseArchipelagoV2);
    CHECK_FALSE(scan_save_slots(save_root)[2].exists);
    CHECK_FALSE(load_save_slot(save_root, 2).has_value());

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game loader preserves backward compatibility with version 1 files") {
    const auto unique_suffix =
        std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-v1-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);
    std::filesystem::create_directories(save_root);

    const auto file_path = save_slot_file_path(save_root, 0);
    std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());

    const auto write_bytes = [&](const void* data, std::size_t size) {
        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    };
    const auto write_value = [&](const auto& value) {
        output.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(value)));
    };
    const auto write_bool = [&](bool value) {
        const std::uint8_t raw = value ? 1U : 0U;
        write_value(raw);
    };
    const auto write_vec3 = [&](const glm::vec3& value) {
        write_value(value.x);
        write_value(value.y);
        write_value(value.z);
    };
    const auto write_hotbar_slot = [&](const HotbarSlot& slot) {
        write_value(slot.block_id);
        write_value(slot.count);
    };
    const auto write_player_state = [&](const PlayerState& state) {
        write_vec3(state.position);
        write_vec3(state.velocity);
        write_value(state.yaw_degrees);
        write_value(state.pitch_degrees);
        write_value(state.body_yaw_degrees);
        write_value(state.animation_time);
        write_value(state.step_phase);
        write_value(state.health);
        write_value(state.air_seconds);
        write_value(state.hurt_timer);
        write_value(state.damage_cooldown);
        write_value(state.regen_delay);
        write_value(state.regen_tick_timer);
        write_value(state.drowning_tick_timer);
        write_value(state.fall_start_y);
        write_value(state.primary_action_progress);
        write_value(state.secondary_action_progress);
        write_value(state.landing_impact);
        write_value(state.airborne_time);
        write_value(state.look_sway_yaw);
        write_value(state.look_sway_pitch);
        write_bool(state.on_ground);
        write_bool(state.fly_mode);
        write_bool(state.head_underwater);
        write_bool(state.swimming);
        write_bool(state.primary_action_active);
        write_bool(state.secondary_action_active);
        write_bool(state.dead);
        const auto death_cause = static_cast<std::underlying_type_t<PlayerDeathCause>>(state.death_cause);
        write_value(death_cause);
    };

    const std::array<char, 8> magic {{'V', 'A', 'L', 'S', 'L', 'O', 'T', '1'}};
    const std::uint32_t version = 1;
    const std::uint64_t saved_at = 1712185200;
    const int seed = 13579;
    const float time_of_day = 9.5F;
    const std::uint32_t modified_chunk_count = 0;
    const glm::vec3 spawn_position {8.5F, 72.0F, -4.5F};
    auto hotbar = make_default_hotbar_state();
    auto inventory = make_default_inventory_menu_state();
    PlayerState player_state {};
    player_state.position = spawn_position;
    player_state.velocity = {0.25F, 0.0F, -0.5F};
    player_state.fly_mode = true;
    player_state.health = 18.0F;

    write_bytes(magic.data(), magic.size());
    write_value(version);
    write_value(saved_at);
    write_value(seed);
    write_value(time_of_day);
    write_value(modified_chunk_count);
    write_vec3(spawn_position);
    write_player_state(player_state);
    for (const auto& slot : hotbar.slots) {
        write_hotbar_slot(slot);
    }
    write_value(static_cast<std::uint32_t>(hotbar.selected_index));
    for (const auto& slot : inventory.storage_slots) {
        write_hotbar_slot(slot);
    }
    write_hotbar_slot(inventory.carried_slot);
    write_bool(inventory.carrying_item);
    write_value(std::uint32_t {0});
    write_value(std::uint32_t {0});
    write_value(std::uint32_t {0});
    output.close();

    const auto scanned = scan_save_slots(save_root);
    REQUIRE(scanned[0].exists);
    CHECK(scanned[0].saved_at_unix_seconds == saved_at);
    CHECK(scanned[0].seed == seed);
    CHECK(scanned[0].time_of_day == doctest::Approx(time_of_day));
    CHECK(scanned[0].weather_time_seconds == doctest::Approx(0.0F));
    CHECK_FALSE(scanned[0].has_starting_village);
    CHECK(scanned[0].game_mode == GameMode::ClassicAdventure);

    const auto loaded = load_save_slot(save_root, 0);
    REQUIRE(loaded.has_value());
    CHECK(loaded->metadata.exists);
    CHECK(loaded->metadata.seed == seed);
    CHECK(loaded->metadata.time_of_day == doctest::Approx(time_of_day));
    CHECK(loaded->metadata.weather_time_seconds == doctest::Approx(0.0F));
    CHECK_FALSE(loaded->metadata.has_starting_village);
    CHECK(loaded->metadata.game_mode == GameMode::ClassicAdventure);
    CHECK_FALSE(loaded->sea_adventure.active);
    CHECK(loaded->spawn_position.x == doctest::Approx(spawn_position.x));
    CHECK(loaded->spawn_position.y == doctest::Approx(spawn_position.y));
    CHECK(loaded->spawn_position.z == doctest::Approx(spawn_position.z));
    CHECK(loaded->player_state.fly_mode);
    CHECK(loaded->player_state.health == doctest::Approx(player_state.health));
    CHECK(loaded->progression.level == 1U);
    CHECK(loaded->progression.experience == 0ULL);
    CHECK(
        loaded->player_build ==
        PlayerBuildState {});
    CHECK(std::all_of(loaded->inventory.equipment_slots.begin(), loaded->inventory.equipment_slots.end(), [](const HotbarSlot& slot) {
        return !inventory_slot_has_item(slot);
    }));
    CHECK(loaded->creatures.empty());
    CHECK(loaded->item_drops.empty());
    CHECK(loaded->chunk_snapshots.empty());
    CHECK(loaded->world_save_plan.chunks.empty());
    CHECK(loaded->world_save_plan.generation_version == WorldGenerationVersion::LegacyV1);

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game loader reads exact historical version 2 to 6 binary fixtures") {
    const auto unique_suffix =
        std::to_string(
            static_cast<unsigned long long>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
    const auto save_root =
        std::filesystem::temp_directory_path() /
        ("valcraft-save-v2-v6-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    for (auto version = std::uint32_t {2U};
         version <= 6U;
         ++version) {
        CAPTURE(version);
        const auto slot_index =
            static_cast<std::size_t>(version - 2U);
        const auto fixture =
            make_historical_save_fixture_v2_to_v6(
                version);
        write_historical_save_fixture_v2_to_v6(
            save_slot_file_path(
                save_root,
                slot_index),
            fixture);

        const auto scanned = scan_save_slots(save_root);
        REQUIRE(scanned[slot_index].exists);
        CHECK(
            scanned[slot_index].saved_at_unix_seconds ==
            fixture.saved_at);
        CHECK(scanned[slot_index].seed == fixture.seed);
        CHECK(
            scanned[slot_index].time_of_day ==
            doctest::Approx(fixture.time_of_day));
        CHECK(
            scanned[slot_index].modified_chunk_count ==
            fixture.modified_chunk_count);
        CHECK(scanned[slot_index].has_starting_village);
        CHECK(
            scanned[slot_index].weather_time_seconds ==
            doctest::Approx(
                version >= 4U
                    ? fixture.weather_time_seconds
                    : 0.0F));
        CHECK(
            scanned[slot_index].game_mode ==
            GameMode::ClassicAdventure);

        const auto loaded =
            load_save_slot(save_root, slot_index);
        REQUIRE(loaded.has_value());
        CHECK(loaded->metadata.exists);
        CHECK(loaded->metadata.seed == fixture.seed);
        CHECK(loaded->metadata.has_starting_village);
        CHECK(
            loaded->metadata.weather_time_seconds ==
            doctest::Approx(
                version >= 4U
                    ? fixture.weather_time_seconds
                    : 0.0F));
        CHECK(
            loaded->metadata.game_mode ==
            GameMode::ClassicAdventure);
        CHECK_FALSE(loaded->sea_adventure.active);
        CHECK(
            loaded->world_save_plan.generation_profile ==
            WorldGenerationProfile::Continental);
        CHECK(
            loaded->world_save_plan.generation_version ==
            WorldGenerationVersion::LegacyV1);

        CHECK(
            loaded->spawn_position.x ==
            doctest::Approx(fixture.spawn_position.x));
        CHECK(
            loaded->spawn_position.y ==
            doctest::Approx(fixture.spawn_position.y));
        CHECK(
            loaded->spawn_position.z ==
            doctest::Approx(fixture.spawn_position.z));
        CHECK(
            loaded->player_state.position.x ==
            doctest::Approx(
                fixture.player_state.position.x));
        CHECK(
            loaded->player_state.velocity.z ==
            doctest::Approx(
                fixture.player_state.velocity.z));
        CHECK(
            loaded->player_state.health ==
            doctest::Approx(
                fixture.player_state.health));
        CHECK(loaded->player_state.fly_mode);
        CHECK(loaded->player_state.on_ground);
        CHECK(
            loaded->progression ==
            (version >= 6U
                 ? migrate_legacy_player_progression_state(
                       fixture.progression)
                 : PlayerProgressionState {}));

        CHECK(
            loaded->hotbar.slots ==
            fixture.hotbar.slots);
        CHECK(
            loaded->hotbar.selected_index ==
            fixture.hotbar.selected_index);
        CHECK(
            loaded->inventory.storage_slots ==
            fixture.inventory.storage_slots);
        CHECK(
            loaded->inventory.carried_slot ==
            fixture.inventory.carried_slot);
        CHECK(loaded->inventory.carrying_item);
        const auto& loaded_chest =
            loaded->inventory.equipment_slots[
                equipment_slot_index(
                    EquipmentSlot::Chest)];
        if (version >= 5U) {
            CHECK(
                loaded_chest ==
                fixture.inventory.equipment_slots[
                    equipment_slot_index(
                        EquipmentSlot::Chest)]);
        } else {
            CHECK_FALSE(
                inventory_slot_has_item(
                    loaded_chest));
        }
        for (const auto& slot : loaded->hotbar.slots) {
            CHECK(slot.instance_state == 0U);
        }
        for (const auto& slot :
             loaded->inventory.storage_slots) {
            CHECK(slot.instance_state == 0U);
        }
        CHECK(
            loaded->inventory.carried_slot.instance_state ==
            0U);
        CHECK(loaded->musket_shot_sequence == 0U);

        REQUIRE(loaded->creatures.size() == 1U);
        const auto& loaded_creature =
            loaded->creatures.front();
        CHECK(
            loaded_creature.anchor.chunk ==
            fixture.creature.anchor.chunk);
        CHECK(
            loaded_creature.anchor.ground_block ==
            fixture.creature.anchor.ground_block);
        CHECK(
            loaded_creature.anchor.species ==
            fixture.creature.anchor.species);
        CHECK(
            loaded_creature.position.x ==
            doctest::Approx(
                fixture.creature.position.x));
        CHECK(
            loaded_creature.behavior_seed ==
            fixture.creature.behavior_seed);
        CHECK(
            loaded_creature.behavior_state ==
            fixture.creature.behavior_state);
        CHECK(
            loaded_creature.health ==
            doctest::Approx(
                version >= 5U
                    ? fixture.creature.health
                    : creature_max_health(
                          fixture.creature
                              .anchor.species)));

        REQUIRE(loaded->item_drops.size() == 1U);
        const auto& loaded_drop =
            loaded->item_drops.front();
        CHECK(
            loaded_drop.position.x ==
            doctest::Approx(
                fixture.item_drop.position.x));
        CHECK(
            loaded_drop.velocity.y ==
            doctest::Approx(
                fixture.item_drop.velocity.y));
        CHECK(
            loaded_drop.stack ==
            fixture.item_drop.stack);
        CHECK(
            loaded_drop.age_seconds ==
            doctest::Approx(
                fixture.item_drop.age_seconds));
        CHECK_FALSE(loaded_drop.grounded);

        CHECK(loaded->chunk_snapshots.empty());
        REQUIRE(
            loaded->world_save_plan.chunks.size() ==
            1U);
        const auto& loaded_chunk =
            loaded->world_save_plan.chunks.front();
        CHECK(
            loaded_chunk.coord ==
            fixture.chunk_coord);
        REQUIRE(
            loaded_chunk.dense_blocks.size() ==
            kChunkVolume);
        REQUIRE(
            loaded_chunk.dense_water_state.size() ==
            kChunkVolume);
        CHECK(
            loaded_chunk.dense_blocks[
                fixture.solid_index] ==
            to_block_id(BlockType::Stone));
        CHECK(
            loaded_chunk.dense_blocks[
                fixture.water_index] ==
            to_block_id(BlockType::Air));
        if (version < 3U) {
            const auto migrated_water =
                loaded_chunk.dense_water_state[
                    fixture.water_index];
            CHECK(
                water_level_from_state(
                    migrated_water) ==
                kMaxWaterLevel);
            CHECK(
                water_state_is_source(
                    migrated_water));
        } else {
            CHECK(
                loaded_chunk.dense_water_state[
                    fixture.water_index] ==
                fixture.dense_water_state[
                    fixture.water_index]);
        }
    }

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game loader preserves the historical version 8 sea payload") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-v8-sea-" + unique_suffix);
    std::filesystem::remove_all(save_root);
    std::filesystem::create_directories(save_root);

    const auto file_path = save_slot_file_path(save_root, 0);
    std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());

    const auto write_bytes = [&](const void* data, std::size_t size) {
        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    };
    const auto write_value = [&](const auto& value) {
        output.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(value)));
    };
    const auto write_bool = [&](bool value) {
        write_value(static_cast<std::uint8_t>(value ? 1U : 0U));
    };
    const auto write_vec3 = [&](const glm::vec3& value) {
        write_value(value.x);
        write_value(value.y);
        write_value(value.z);
    };
    const auto write_hotbar_slot = [&](const HotbarSlot& slot) {
        write_value(slot.block_id);
        write_value(slot.count);
    };
    const auto write_player_state = [&](const PlayerState& state) {
        write_vec3(state.position);
        write_vec3(state.velocity);
        write_value(state.yaw_degrees);
        write_value(state.pitch_degrees);
        write_value(state.body_yaw_degrees);
        write_value(state.animation_time);
        write_value(state.step_phase);
        write_value(state.health);
        write_value(state.air_seconds);
        write_value(state.hurt_timer);
        write_value(state.damage_cooldown);
        write_value(state.regen_delay);
        write_value(state.regen_tick_timer);
        write_value(state.drowning_tick_timer);
        write_value(state.fall_start_y);
        write_value(state.primary_action_progress);
        write_value(state.secondary_action_progress);
        write_value(state.landing_impact);
        write_value(state.airborne_time);
        write_value(state.look_sway_yaw);
        write_value(state.look_sway_pitch);
        write_bool(state.on_ground);
        write_bool(state.fly_mode);
        write_bool(state.head_underwater);
        write_bool(state.swimming);
        write_bool(state.primary_action_active);
        write_bool(state.secondary_action_active);
        write_bool(state.dead);
        write_value(static_cast<std::underlying_type_t<PlayerDeathCause>>(state.death_cause));
    };
    const auto write_sea_state_v8 = [&](const SeaAdventureSaveState& state) {
        // Je fige volontairement l'ordre exact du payload v7/v8 pour detecter
        // toute lecture accidentelle des nouveaux champs dans une ancienne save.
        write_bool(state.active);
        write_vec3(state.ship_position);
        write_value(state.route_distance);
        write_value(state.hunger);
        write_value(state.thirst);
        write_value(state.stamina);
        write_value(state.fishing_progress);
        write_value(state.fishing_target_seconds);
        write_value(state.survival_damage_timer);
        write_value(state.stranded_warning_timer);
        write_value(state.food_rations);
        write_value(state.water_flasks);
        write_value(state.fish);
        write_value(state.wood);
        write_value(state.stone);
        write_value(state.fiber);
        write_value(state.stamped_ship_x);
        write_value(state.stamped_ship_z);
        write_bool(state.has_stamped_ship);
        write_bool(state.fishing_active);
    };

    constexpr std::array<char, 8> magic {{'V', 'A', 'L', 'S', 'L', 'O', 'T', '1'}};
    constexpr std::uint32_t version = 8U;
    constexpr std::uint64_t saved_at = 1'720'000'008ULL;
    constexpr int seed = 62'022;
    constexpr float time_of_day = 14.5F;
    constexpr float weather_time_seconds = 215.0F;
    const glm::vec3 spawn_position {0.5F, static_cast<float>(kSeaLevel + 5), 12.5F};
    PlayerState player_state {};
    player_state.position = spawn_position;
    PlayerProgressionState progression {8U, 808ULL};
    SeaAdventureSaveState sea_state {};
    sea_state.active = true;
    sea_state.ship_position = {0.5F, static_cast<float>(kSeaLevel + 1), 128.5F};
    sea_state.route_distance = 128.0F;
    sea_state.hunger = 72.0F;
    sea_state.thirst = 63.0F;
    sea_state.stamina = 54.0F;
    sea_state.food_rations = 4U;
    sea_state.water_flasks = 3U;
    sea_state.fish = 2U;
    sea_state.wood = 7U;
    sea_state.stone = 5U;
    sea_state.fiber = 9U;
    const auto hotbar = make_default_hotbar_state();
    const auto inventory = make_default_inventory_menu_state();

    write_bytes(magic.data(), magic.size());
    write_value(version);
    write_value(saved_at);
    write_value(seed);
    write_value(time_of_day);
    write_value(std::uint32_t {0});
    write_bool(false);
    write_value(weather_time_seconds);
    write_value(static_cast<std::uint8_t>(GameMode::SeaAdventure));
    write_value(static_cast<std::uint8_t>(WorldGenerationProfile::OceanAdventure));
    write_value(std::uint32_t {1});
    write_vec3(spawn_position);
    write_player_state(player_state);
    write_value(progression.level);
    write_value(progression.experience);
    write_sea_state_v8(sea_state);
    for (const auto& slot : hotbar.slots) {
        write_hotbar_slot(slot);
    }
    write_value(static_cast<std::uint32_t>(hotbar.selected_index));
    for (const auto& slot : inventory.storage_slots) {
        write_hotbar_slot(slot);
    }
    write_hotbar_slot(inventory.carried_slot);
    write_bool(inventory.carrying_item);
    for (const auto& slot : inventory.equipment_slots) {
        write_hotbar_slot(slot);
    }
    write_value(std::uint32_t {0});
    write_value(std::uint32_t {0});
    write_value(std::uint32_t {0});
    output.close();
    REQUIRE(output.good());

    const auto scanned = scan_save_slots(save_root);
    REQUIRE(scanned[0].exists);
    CHECK(scanned[0].saved_at_unix_seconds == saved_at);
    CHECK(scanned[0].game_mode == GameMode::SeaAdventure);

    const auto loaded = load_save_slot(save_root, 0);
    REQUIRE(loaded.has_value());
    CHECK(loaded->metadata.seed == seed);
    CHECK(
        loaded->progression ==
        migrate_legacy_player_progression_state(
            progression));
    CHECK(loaded->sea_adventure.active);
    CHECK(loaded->sea_adventure.ship_position.z == doctest::Approx(sea_state.ship_position.z));
    CHECK(loaded->sea_adventure.route_distance == doctest::Approx(sea_state.route_distance));
    CHECK(loaded->sea_adventure.food_rations == sea_state.food_rations);
    CHECK(loaded->sea_adventure.water_flasks == sea_state.water_flasks);
    CHECK(loaded->sea_adventure.fish == sea_state.fish);
    CHECK(loaded->sea_adventure.wood == sea_state.wood);
    CHECK(loaded->sea_adventure.stone == sea_state.stone);
    CHECK(loaded->sea_adventure.fiber == sea_state.fiber);
    CHECK(loaded->sea_adventure.crew == ShipCrewSaveState {});
    CHECK(loaded->sea_adventure.voyage_phase == SeaVoyagePhase::Underway);
    CHECK(loaded->sea_adventure.voyage_phase_elapsed == doctest::Approx(0.0F));
    CHECK(loaded->creatures.empty());
    CHECK(loaded->item_drops.empty());
    CHECK(loaded->world_save_plan.chunks.empty());
    CHECK(loaded->world_save_plan.generation_version == WorldGenerationVersion::LegacyV1);

    std::filesystem::remove_all(save_root);
}

TEST_CASE("version 7 dense sea saves restore and migrate their stamped ship incrementally") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-v7-sea-" + unique_suffix);
    std::filesystem::remove_all(save_root);
    std::filesystem::create_directories(save_root);

    constexpr int seed = 62021;
    constexpr ChunkCoord chunk_coord {0, 0};
    constexpr int hull_x = 0;
    constexpr int hull_y = kSeaLevel + 3;
    constexpr int hull_z = 0;
    constexpr int water_x = 10;
    constexpr int water_y = kSeaLevel;
    constexpr int water_z = 10;
    const auto chunk_index = [](int x, int y, int z) {
        return static_cast<std::size_t>((y * kChunkSizeZ + z) * kChunkSizeX + x);
    };
    const auto hull_index = chunk_index(hull_x, hull_y, hull_z);
    const auto water_index = chunk_index(water_x, water_y, water_z);

    // Je pars d'un vrai chunk oceanique v7, puis je reproduis la coque que les
    // anciennes sauvegardes gravaient directement dans les voxels du monde.
    WorldGenerator generator(
        seed,
        WorldGenerationProfile::OceanAdventure,
        WorldGenerationVersion::LegacyV1);
    Chunk dense_chunk(chunk_coord);
    generator.generate_chunk(dense_chunk);
    auto dense_blocks = std::vector<BlockId>(dense_chunk.blocks().begin(), dense_chunk.blocks().end());
    auto dense_water = std::vector<WaterState>(dense_chunk.water_state().begin(), dense_chunk.water_state().end());
    REQUIRE(dense_blocks[hull_index] == to_block_id(BlockType::Air));
    REQUIRE(water_level_from_state(dense_water[water_index]) == kMaxWaterLevel);
    dense_blocks[hull_index] = to_block_id(BlockType::Planks);
    dense_water[hull_index] = WaterState {0};

    const auto file_path = save_slot_file_path(save_root, 0);
    std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());

    const auto write_bytes = [&](const void* data, std::size_t size) {
        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    };
    const auto write_value = [&](const auto& value) {
        output.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(value)));
    };
    const auto write_bool = [&](bool value) {
        const std::uint8_t raw = value ? 1U : 0U;
        write_value(raw);
    };
    const auto write_vec3 = [&](const glm::vec3& value) {
        write_value(value.x);
        write_value(value.y);
        write_value(value.z);
    };
    const auto write_hotbar_slot = [&](const HotbarSlot& slot) {
        write_value(slot.block_id);
        write_value(slot.count);
    };
    const auto write_player_state = [&](const PlayerState& state) {
        write_vec3(state.position);
        write_vec3(state.velocity);
        write_value(state.yaw_degrees);
        write_value(state.pitch_degrees);
        write_value(state.body_yaw_degrees);
        write_value(state.animation_time);
        write_value(state.step_phase);
        write_value(state.health);
        write_value(state.air_seconds);
        write_value(state.hurt_timer);
        write_value(state.damage_cooldown);
        write_value(state.regen_delay);
        write_value(state.regen_tick_timer);
        write_value(state.drowning_tick_timer);
        write_value(state.fall_start_y);
        write_value(state.primary_action_progress);
        write_value(state.secondary_action_progress);
        write_value(state.landing_impact);
        write_value(state.airborne_time);
        write_value(state.look_sway_yaw);
        write_value(state.look_sway_pitch);
        write_bool(state.on_ground);
        write_bool(state.fly_mode);
        write_bool(state.head_underwater);
        write_bool(state.swimming);
        write_bool(state.primary_action_active);
        write_bool(state.secondary_action_active);
        write_bool(state.dead);
        write_value(static_cast<std::underlying_type_t<PlayerDeathCause>>(state.death_cause));
    };
    const auto write_sea_state = [&](const SeaAdventureSaveState& state) {
        write_bool(state.active);
        write_vec3(state.ship_position);
        write_value(state.route_distance);
        write_value(state.hunger);
        write_value(state.thirst);
        write_value(state.stamina);
        write_value(state.fishing_progress);
        write_value(state.fishing_target_seconds);
        write_value(state.survival_damage_timer);
        write_value(state.stranded_warning_timer);
        write_value(state.food_rations);
        write_value(state.water_flasks);
        write_value(state.fish);
        write_value(state.wood);
        write_value(state.stone);
        write_value(state.fiber);
        write_value(state.stamped_ship_x);
        write_value(state.stamped_ship_z);
        write_bool(state.has_stamped_ship);
        write_bool(state.fishing_active);
    };

    const std::array<char, 8> magic {{'V', 'A', 'L', 'S', 'L', 'O', 'T', '1'}};
    constexpr std::uint32_t version = 7U;
    constexpr std::uint64_t saved_at = 1'720'000'007ULL;
    constexpr float time_of_day = 6.75F;
    constexpr float weather_time_seconds = 125.0F;
    constexpr std::uint32_t chunk_count = 1U;
    const glm::vec3 spawn_position {0.5F, static_cast<float>(kSeaLevel + 5), 0.5F};
    PlayerState player_state {};
    player_state.position = spawn_position;
    PlayerProgressionState progression {};
    progression.level = 7U;
    progression.experience = 777ULL;
    SeaAdventureSaveState sea_state {};
    sea_state.active = true;
    sea_state.ship_position = {0.5F, static_cast<float>(kSeaLevel + 1), 0.5F};
    sea_state.route_distance = 37.5F;
    sea_state.stamped_ship_x = 0;
    sea_state.stamped_ship_z = 0;
    sea_state.has_stamped_ship = true;
    const auto hotbar = make_default_hotbar_state();
    const auto inventory = make_default_inventory_menu_state();

    // J'ecris volontairement chaque champ dans l'ordre binaire historique v7;
    // il n'existe alors ni profil explicite, ni encodage dense/sparse par chunk.
    write_bytes(magic.data(), magic.size());
    write_value(version);
    write_value(saved_at);
    write_value(seed);
    write_value(time_of_day);
    write_value(chunk_count);
    write_bool(false);
    write_value(weather_time_seconds);
    write_value(static_cast<std::uint8_t>(GameMode::SeaAdventure));
    write_vec3(spawn_position);
    write_player_state(player_state);
    write_value(progression.level);
    write_value(progression.experience);
    write_sea_state(sea_state);
    for (const auto& slot : hotbar.slots) {
        write_hotbar_slot(slot);
    }
    write_value(static_cast<std::uint32_t>(hotbar.selected_index));
    for (const auto& slot : inventory.storage_slots) {
        write_hotbar_slot(slot);
    }
    write_hotbar_slot(inventory.carried_slot);
    write_bool(inventory.carrying_item);
    for (const auto& slot : inventory.equipment_slots) {
        write_hotbar_slot(slot);
    }
    write_value(std::uint32_t {0});
    write_value(std::uint32_t {0});
    write_value(chunk_count);
    write_value(chunk_coord.x);
    write_value(chunk_coord.z);
    write_bytes(dense_blocks.data(), dense_blocks.size() * sizeof(BlockId));
    write_bytes(dense_water.data(), dense_water.size() * sizeof(WaterState));
    output.close();
    REQUIRE(output.good());

    const auto loaded = load_save_slot(save_root, 0);
    REQUIRE(loaded.has_value());
    CHECK(loaded->metadata.game_mode == GameMode::SeaAdventure);
    CHECK(loaded->world_save_plan.seed == seed);
    CHECK(loaded->world_save_plan.generation_profile == WorldGenerationProfile::OceanAdventure);
    CHECK(loaded->world_save_plan.generation_version == WorldGenerationVersion::LegacyV1);
    REQUIRE(loaded->world_save_plan.chunks.size() == 1U);
    const auto& loaded_chunk = loaded->world_save_plan.chunks.front();
    CHECK(loaded_chunk.coord == chunk_coord);
    CHECK(loaded_chunk.dense());
    CHECK(loaded_chunk.sparse_cells.empty());
    REQUIRE(loaded_chunk.dense_blocks.size() == kChunkVolume);
    REQUIRE(loaded_chunk.dense_water_state.size() == kChunkVolume);
    CHECK(loaded_chunk.dense_blocks[hull_index] == to_block_id(BlockType::Planks));
    CHECK(water_level_from_state(loaded_chunk.dense_water_state[water_index]) == kMaxWaterLevel);
    CHECK(loaded->sea_adventure.active);
    CHECK(loaded->sea_adventure.has_stamped_ship);
    CHECK(loaded->sea_adventure.stamped_ship_x == 0);
    CHECK(loaded->sea_adventure.stamped_ship_z == 0);
    CHECK(loaded->sea_adventure.crew == ShipCrewSaveState {});
    CHECK(loaded->sea_adventure.voyage_phase == SeaVoyagePhase::Underway);
    CHECK(loaded->sea_adventure.voyage_phase_elapsed == doctest::Approx(0.0F));

    World restored_world(
        seed,
        0,
        WorldGenerationProfile::OceanAdventure,
        WorldGenerationVersion::LegacyV1);
    restored_world.begin_restore_save_plan(loaded->world_save_plan);
    constexpr std::size_t restore_budget = 512U;
    auto restore_iterations = std::size_t {0};
    while (restored_world.has_pending_save_restore() && restore_iterations < 1024U) {
        const auto stats = restored_world.process_save_restore(
            restore_budget,
            std::numeric_limits<double>::infinity());
        CHECK(stats.processed_cells <= restore_budget);
        CHECK(restored_world.chunk_records().empty());
        ++restore_iterations;
    }
    CHECK(restore_iterations > 1U);
    REQUIRE_FALSE(restored_world.has_pending_save_restore());
    CHECK(restored_world.save_restore_progress() == doctest::Approx(1.0F));
    CHECK(restored_world.chunk_records().empty());
    CHECK(restored_world.peek_block_or_generated(hull_x, hull_y, hull_z) == to_block_id(BlockType::Planks));
    CHECK(restored_world.peek_water_level_or_generated(water_x, water_y, water_z) == kMaxWaterLevel);

    SeaAdventureSystem restored_sea;
    restored_sea.load_state(loaded->sea_adventure, seed);
    REQUIRE(restored_sea.crew_members().size() == kShipCrewMemberCount);
    CHECK(restored_sea.crew_members().front().role == ShipCrewRole::Captain);
    CHECK(restored_sea.save_state().crew.initialized);
    const auto migrated_voxel_count = legacy_ship_voxel_count();
    REQUIRE(migrated_voxel_count == 2814U);
    CHECK(legacy_ship_blueprint_checksum() == 0x278956FF051EAC1EULL);
    restored_sea.begin_legacy_ship_migration(restored_world);
    REQUIRE(restored_sea.has_pending_legacy_ship_migration());
    constexpr std::size_t migration_budget = 64U;
    auto migration_iterations = std::size_t {0};
    auto migrated_cells = std::size_t {0};
    auto previous_progress = 0.0F;
    while (restored_sea.has_pending_legacy_ship_migration() && migration_iterations < 1024U) {
        const auto stats = restored_sea.migrate_legacy_ship_step(
            restored_world,
            migration_budget,
            std::numeric_limits<double>::infinity());
        CHECK(stats.processed_cells > 0U);
        CHECK(stats.processed_cells <= migration_budget);
        CHECK(stats.progress >= previous_progress);
        CHECK(restored_world.chunk_records().empty());
        migrated_cells += stats.processed_cells;
        previous_progress = stats.progress;
        ++migration_iterations;
    }
    CHECK(migration_iterations == (migrated_voxel_count + migration_budget - 1U) / migration_budget);
    CHECK(migrated_cells == migrated_voxel_count);
    REQUIRE_FALSE(restored_sea.has_pending_legacy_ship_migration());
    CHECK_FALSE(restored_sea.save_state().has_stamped_ship);
    CHECK(restored_sea.legacy_ship_migration_progress() == doctest::Approx(1.0F));
    CHECK(restored_world.peek_block_or_generated(hull_x, hull_y, hull_z) ==
          generator.sample_block(hull_x, hull_y, hull_z));
    CHECK(restored_world.capture_save_plan().chunks.empty());
    CHECK(restored_world.chunk_records().empty());

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game loader rejects corrupt oversized payload counts before allocation") {
    const auto unique_suffix =
        std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-corrupt-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);
    std::filesystem::create_directories(save_root);

    const auto file_path = save_slot_file_path(save_root, 0);
    std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());

    const auto write_bytes = [&](const void* data, std::size_t size) {
        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    };
    const auto write_value = [&](const auto& value) {
        output.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(value)));
    };
    const auto write_bool = [&](bool value) {
        const std::uint8_t raw = value ? 1U : 0U;
        write_value(raw);
    };
    const auto write_vec3 = [&](const glm::vec3& value) {
        write_value(value.x);
        write_value(value.y);
        write_value(value.z);
    };
    const auto write_hotbar_slot = [&](const HotbarSlot& slot) {
        write_value(slot.block_id);
        write_value(slot.count);
    };
    const auto write_player_state = [&](const PlayerState& state) {
        write_vec3(state.position);
        write_vec3(state.velocity);
        write_value(state.yaw_degrees);
        write_value(state.pitch_degrees);
        write_value(state.body_yaw_degrees);
        write_value(state.animation_time);
        write_value(state.step_phase);
        write_value(state.health);
        write_value(state.air_seconds);
        write_value(state.hurt_timer);
        write_value(state.damage_cooldown);
        write_value(state.regen_delay);
        write_value(state.regen_tick_timer);
        write_value(state.drowning_tick_timer);
        write_value(state.fall_start_y);
        write_value(state.primary_action_progress);
        write_value(state.secondary_action_progress);
        write_value(state.landing_impact);
        write_value(state.airborne_time);
        write_value(state.look_sway_yaw);
        write_value(state.look_sway_pitch);
        write_bool(state.on_ground);
        write_bool(state.fly_mode);
        write_bool(state.head_underwater);
        write_bool(state.swimming);
        write_bool(state.primary_action_active);
        write_bool(state.secondary_action_active);
        write_bool(state.dead);
        const auto death_cause = static_cast<std::underlying_type_t<PlayerDeathCause>>(state.death_cause);
        write_value(death_cause);
    };

    const std::array<char, 8> magic {{'V', 'A', 'L', 'S', 'L', 'O', 'T', '1'}};
    const std::uint32_t version = 5;
    const std::uint64_t saved_at = 1712185200;
    const int seed = 24680;
    const float time_of_day = 12.0F;
    const std::uint32_t modified_chunk_count = 0;
    const float weather_time_seconds = 0.0F;
    const glm::vec3 spawn_position {0.5F, 72.0F, 0.5F};
    const auto hotbar = make_default_hotbar_state();
    const auto inventory = make_default_inventory_menu_state();
    PlayerState player_state {};
    player_state.position = spawn_position;

    write_bytes(magic.data(), magic.size());
    write_value(version);
    write_value(saved_at);
    write_value(seed);
    write_value(time_of_day);
    write_value(modified_chunk_count);
    write_bool(false);
    write_value(weather_time_seconds);
    write_vec3(spawn_position);
    write_player_state(player_state);
    for (const auto& slot : hotbar.slots) {
        write_hotbar_slot(slot);
    }
    write_value(static_cast<std::uint32_t>(hotbar.selected_index));
    for (const auto& slot : inventory.storage_slots) {
        write_hotbar_slot(slot);
    }
    write_hotbar_slot(inventory.carried_slot);
    write_bool(inventory.carrying_item);
    for (const auto& slot : inventory.equipment_slots) {
        write_hotbar_slot(slot);
    }
    write_value(std::numeric_limits<std::uint32_t>::max());
    output.close();

    CHECK_FALSE(load_save_slot(save_root, 0).has_value());

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game writer rejects snapshots that exceed the loader payload limits") {
    const auto unique_suffix =
        std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-oversized-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.item_drops.resize(129);

    CHECK_THROWS_AS(write_save_slot(save_root, 0, snapshot), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(save_slot_file_path(save_root, 0)));

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game writer rejects unsafe player and chunk world coordinates") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-bounds-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot invalid_spawn {};
    invalid_spawn.spawn_position.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(write_save_slot(save_root, 0, invalid_spawn), std::runtime_error);

    SaveGameSnapshot invalid_player {};
    invalid_player.player_state.position.z = std::numeric_limits<float>::max();
    CHECK_THROWS_AS(write_save_slot(save_root, 0, invalid_player), std::runtime_error);

    SaveGameSnapshot invalid_drop {};
    ItemDrop extreme_drop {};
    extreme_drop.position.x = (std::numeric_limits<float>::max)();
    extreme_drop.stack = make_item_stack(to_block_id(BlockType::Stone), 1);
    invalid_drop.item_drops.push_back(extreme_drop);
    CHECK_THROWS_AS(write_save_slot(save_root, 0, invalid_drop), std::runtime_error);

    SaveGameSnapshot plan_snapshot {};
    WorldSavePlan invalid_plan {};
    WorldSavePlanChunk invalid_plan_chunk {};
    invalid_plan_chunk.coord = {(std::numeric_limits<int>::max)(), 0};
    invalid_plan_chunk.sparse_cells.push_back({0U, to_block_id(BlockType::Stone), WaterState {0}});
    invalid_plan.chunks.push_back(std::move(invalid_plan_chunk));
    CHECK_THROWS_AS(write_save_slot(save_root, 0, plan_snapshot, invalid_plan), std::runtime_error);

    SaveGameSnapshot legacy_snapshot {};
    WorldChunkSnapshot legacy_chunk {};
    legacy_chunk.coord = {0, (std::numeric_limits<int>::lowest)()};
    legacy_snapshot.chunk_snapshots.push_back(std::move(legacy_chunk));
    CHECK_THROWS_AS(write_save_slot(save_root, 0, legacy_snapshot), std::runtime_error);

    CHECK_FALSE(std::filesystem::exists(save_slot_file_path(save_root, 0)));
    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game loader rejects corrupted player and chunk world coordinates") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-corrupt-bounds-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot position_snapshot {};
    position_snapshot.spawn_position = {12'345.125F, -23'456.25F, 34'567.5F};
    position_snapshot.player_state.position = {-45'678.125F, 56'789.25F, -67'890.5F};
    ItemDrop saved_drop {};
    saved_drop.position = {123'456.75F, -234'567.5F, 345'678.25F};
    saved_drop.stack = make_item_stack(to_block_id(BlockType::Stone), 1);
    position_snapshot.item_drops.push_back(saved_drop);
    write_save_slot(save_root, 0, position_snapshot);
    REQUIRE(load_save_slot(save_root, 0).has_value());

    overwrite_unique_binary_sequence(
        save_slot_file_path(save_root, 0),
        std::array<float, 3> {position_snapshot.spawn_position.x,
                              position_snapshot.spawn_position.y,
                              position_snapshot.spawn_position.z},
        std::array<float, 3> {std::numeric_limits<float>::infinity(), 0.0F, 0.0F});
    CHECK_FALSE(load_save_slot(save_root, 0).has_value());

    write_save_slot(save_root, 0, position_snapshot);
    overwrite_unique_binary_sequence(
        save_slot_file_path(save_root, 0),
        std::array<float, 3> {position_snapshot.player_state.position.x,
                              position_snapshot.player_state.position.y,
                              position_snapshot.player_state.position.z},
        std::array<float, 3> {1'000'001.0F, 0.0F, 0.0F});
    CHECK_FALSE(load_save_slot(save_root, 0).has_value());

    write_save_slot(save_root, 0, position_snapshot);
    overwrite_unique_binary_sequence(
        save_slot_file_path(save_root, 0),
        std::array<float, 3> {saved_drop.position.x, saved_drop.position.y, saved_drop.position.z},
        std::array<float, 3> {1'000'001.0F, 0.0F, 0.0F});
    CHECK_FALSE(load_save_slot(save_root, 0).has_value());

    constexpr ChunkCoord original_coord {1'234'567, -7'654'321};
    WorldSavePlan plan {};
    WorldSavePlanChunk chunk {};
    chunk.coord = original_coord;
    chunk.sparse_cells.push_back({0U, to_block_id(BlockType::Stone), WaterState {0}});
    plan.chunks.push_back(std::move(chunk));
    write_save_slot(save_root, 1, SaveGameSnapshot {}, plan);
    REQUIRE(load_save_slot(save_root, 1).has_value());

    overwrite_unique_binary_sequence(
        save_slot_file_path(save_root, 1),
        std::array<int, 2> {original_coord.x, original_coord.z},
        std::array<int, 2> {(std::numeric_limits<int>::max)(), (std::numeric_limits<int>::lowest)()});
    CHECK_FALSE(load_save_slot(save_root, 1).has_value());

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game normalizes extreme item drop animation state at the binary boundary") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-drop-state-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot extreme_snapshot {};
    ItemDrop extreme_drop {};
    extreme_drop.position = {11.25F, 62.5F, -7.75F};
    extreme_drop.velocity = {
        (std::numeric_limits<float>::max)(),
        std::numeric_limits<float>::quiet_NaN(),
        -(std::numeric_limits<float>::max)(),
    };
    extreme_drop.stack = make_item_stack(to_block_id(BlockType::Stone), 1);
    extreme_drop.age_seconds = (std::numeric_limits<float>::max)();
    extreme_drop.pickup_cooldown = (std::numeric_limits<float>::max)();
    extreme_snapshot.item_drops.push_back(extreme_drop);
    write_save_slot(save_root, 0, extreme_snapshot);

    const auto normalized = load_save_slot(save_root, 0);
    REQUIRE(normalized.has_value());
    REQUIRE(normalized->item_drops.size() == 1U);
    const auto& normalized_drop = normalized->item_drops.front();
    CHECK(std::isfinite(normalized_drop.age_seconds));
    CHECK(normalized_drop.age_seconds >= 0.0F);
    CHECK(normalized_drop.age_seconds < 63.0F);
    CHECK(normalized_drop.pickup_cooldown == doctest::Approx(1.0F));
    CHECK(normalized_drop.velocity.x == doctest::Approx(64.0F));
    CHECK(normalized_drop.velocity.y == doctest::Approx(0.0F));
    CHECK(normalized_drop.velocity.z == doctest::Approx(-64.0F));

    SaveGameSnapshot corrupted_snapshot {};
    ItemDrop corrupted_drop {};
    corrupted_drop.position = {-19.5F, 71.25F, 33.75F};
    corrupted_drop.stack = make_item_stack(to_block_id(BlockType::Wood), 2);
    corrupted_drop.age_seconds = 17.125F;
    corrupted_drop.pickup_cooldown = 0.8125F;
    corrupted_snapshot.item_drops.push_back(corrupted_drop);
    write_save_slot(save_root, 1, corrupted_snapshot);
    overwrite_unique_binary_sequence(
        save_slot_file_path(save_root, 1),
        std::array<float, 2> {corrupted_drop.age_seconds, corrupted_drop.pickup_cooldown},
        std::array<float, 2> {(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)()});

    const auto corrupted = load_save_slot(save_root, 1);
    REQUIRE(corrupted.has_value());
    REQUIRE(corrupted->item_drops.size() == 1U);
    CHECK(std::isfinite(corrupted->item_drops.front().age_seconds));
    CHECK(corrupted->item_drops.front().age_seconds < 63.0F);
    CHECK(corrupted->item_drops.front().pickup_cooldown == doctest::Approx(1.0F));

    std::filesystem::remove_all(save_root);
}

TEST_CASE("compact world save plan stays compact until its incremental world restoration") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-plan-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    constexpr int seed = 73191;
    constexpr int block_x = 2;
    constexpr int block_y = 100;
    constexpr int block_z = 3;
    World world(seed, 0);
    world.ensure_chunk_loaded({0, 0});
    const auto generated_block = world.get_block(block_x, block_y, block_z);
    const auto replacement = generated_block == to_block_id(BlockType::Stone)
                                 ? to_block_id(BlockType::Air)
                                 : to_block_id(BlockType::Stone);
    REQUIRE(
        world.set_player_block(
            block_x,
            block_y,
            block_z,
            replacement));

    auto save_plan = world.capture_save_plan();
    REQUIRE(save_plan.chunks.size() == 1U);
    CHECK(save_plan.seed == seed);
    CHECK_FALSE(save_plan.chunks.front().dense());
    REQUIRE(save_plan.chunks.front().sparse_cells.size() == 1U);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = seed;
    snapshot.metadata.modified_chunk_count = 1U;
    write_save_slot(save_root, 0, snapshot, save_plan);
    // Je garde un plafond compact tout en comptant les extensions persistantes
    // modernes, dont les 146 octets de l'état durable de Jack.
    CHECK(std::filesystem::file_size(save_slot_file_path(save_root, 0)) < 17U * 1024U);

    const auto loaded = load_save_slot(save_root, 0);
    REQUIRE(loaded.has_value());
    CHECK(loaded->chunk_snapshots.empty());
    REQUIRE(loaded->world_save_plan.chunks.size() == 1U);
    const auto block_index = static_cast<std::size_t>(
        (block_y * kChunkSizeZ + block_z) * kChunkSizeX + block_x);
    const ChunkCoord expected_coord {0, 0};
    CHECK(loaded->world_save_plan.chunks.front().coord == expected_coord);
    CHECK_FALSE(loaded->world_save_plan.chunks.front().dense());
    REQUIRE(loaded->world_save_plan.chunks.front().sparse_cells.size() == 1U);
    CHECK(loaded->world_save_plan.chunks.front().sparse_cells.front().index == block_index);
    CHECK(loaded->world_save_plan.chunks.front().sparse_cells.front().block == replacement);
    const auto player_mask_byte =
        block_index / 8U;
    const auto player_mask_bit =
        static_cast<std::uint8_t>(
            1U << (block_index % 8U));
    CHECK(
        (loaded->world_save_plan.chunks.front()
             .player_placed_mask[player_mask_byte] &
         player_mask_bit) != 0U);

    World restored(seed, 0);
    restored.begin_restore_save_plan(loaded->world_save_plan);
    while (restored.has_pending_save_restore()) {
        (void)restored.process_save_restore(kChunkVolume, std::numeric_limits<double>::infinity());
    }
    restored.ensure_chunk_loaded({0, 0});
    CHECK(restored.get_block(block_x, block_y, block_z) == replacement);
    CHECK(
        restored.was_player_placed(
            block_x,
            block_y,
            block_z));

    World ocean_world(seed + 1, 0, WorldGenerationProfile::OceanAdventure);
    ocean_world.set_block(block_x, block_y, block_z, to_block_id(BlockType::Stone));
    auto ocean_plan = ocean_world.capture_save_plan();
    REQUIRE(ocean_plan.chunks.size() == 1U);
    SaveGameSnapshot ocean_snapshot {};
    ocean_snapshot.metadata.seed = seed + 1;
    ocean_snapshot.metadata.game_mode = GameMode::SeaAdventure;
    ocean_snapshot.metadata.modified_chunk_count = 1U;
    write_save_slot(save_root, 1, ocean_snapshot, ocean_plan);
    const auto loaded_ocean = load_save_slot(save_root, 1);
    REQUIRE(loaded_ocean.has_value());
    CHECK(loaded_ocean->world_save_plan.generation_version == WorldGenerationVersion::LivingOceanV3);
    REQUIRE(loaded_ocean->world_save_plan.chunks.size() == 1U);
    REQUIRE(loaded_ocean->world_save_plan.chunks.front().sparse_cells.size() == 1U);
    CHECK(loaded_ocean->world_save_plan.chunks.front().sparse_cells.front().block == to_block_id(BlockType::Stone));

    WorldSavePlan dense_plan {};
    dense_plan.seed = seed + 2;
    WorldSavePlanChunk dense_chunk {};
    dense_chunk.coord = {0, 0};
    dense_chunk.dense_blocks.assign(kChunkVolume, to_block_id(BlockType::Air));
    dense_chunk.dense_water_state.assign(kChunkVolume, WaterState {0});
    dense_chunk.dense_blocks[block_index] = to_block_id(BlockType::DiamondOre);
    dense_plan.chunks.push_back(std::move(dense_chunk));
    SaveGameSnapshot dense_snapshot {};
    dense_snapshot.metadata.seed = seed + 2;
    dense_snapshot.metadata.modified_chunk_count = 1U;
    write_save_slot(save_root, 2, dense_snapshot, dense_plan);
    const auto loaded_dense = load_save_slot(save_root, 2);
    REQUIRE(loaded_dense.has_value());
    REQUIRE(loaded_dense->world_save_plan.chunks.size() == 1U);
    CHECK(loaded_dense->world_save_plan.chunks.front().dense());
    CHECK(loaded_dense->world_save_plan.chunks.front().dense_blocks[block_index] == to_block_id(BlockType::DiamondOre));

    World marker_only_world(
        seed + 3,
        0,
        WorldGenerationProfile::OceanAdventure);
    const auto marker_generated =
        marker_only_world.peek_block_or_generated(
            block_x,
            block_y,
            block_z);
    const auto marker_replacement =
        marker_generated ==
                to_block_id(
                    BlockType::Stone)
            ? to_block_id(
                  BlockType::Air)
            : to_block_id(
                  BlockType::Stone);
    REQUIRE(
        marker_only_world.set_player_block(
            block_x,
            block_y,
            block_z,
            marker_replacement));
    REQUIRE(
        marker_only_world.restore_generated_cell(
            block_x,
            block_y,
            block_z));
    const auto marker_only_plan =
        marker_only_world.capture_save_plan();
    REQUIRE(
        marker_only_plan.chunks.size() ==
        1U);
    CHECK(
        marker_only_plan.chunks.front()
            .sparse_cells.empty());

    SaveGameSnapshot marker_only_snapshot {};
    marker_only_snapshot.metadata.seed =
        seed + 3;
    marker_only_snapshot.metadata.game_mode =
        GameMode::SeaAdventure;
    marker_only_snapshot.metadata
        .modified_chunk_count = 1U;
    write_save_slot(
        save_root,
        3,
        marker_only_snapshot,
        marker_only_plan);
    const auto loaded_marker_only =
        load_save_slot(
            save_root,
            3);
    REQUIRE(
        loaded_marker_only.has_value());
    REQUIRE(
        loaded_marker_only->world_save_plan
            .chunks.size() == 1U);
    CHECK(
        loaded_marker_only->world_save_plan
            .chunks.front()
            .sparse_cells.empty());
    CHECK(
        (loaded_marker_only->world_save_plan
             .chunks.front()
             .player_placed_mask[player_mask_byte] &
         player_mask_bit) != 0U);

    World marker_only_restored(
        seed + 3,
        0,
        WorldGenerationProfile::OceanAdventure);
    marker_only_restored
        .begin_restore_save_plan(
            loaded_marker_only->world_save_plan);
    while (
        marker_only_restored
            .has_pending_save_restore()) {
        (void)marker_only_restored
            .process_save_restore(
                kChunkVolume,
                std::numeric_limits<
                    double>::infinity());
    }
    CHECK(
        marker_only_restored
            .was_player_placed(
                block_x,
                block_y,
                block_z));
    CHECK(
        marker_only_restored.get_block(
            block_x,
            block_y,
            block_z) ==
        marker_generated);

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game loading reports monotone byte progress and supports cancellation") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-progress-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 9137;
    WorldSavePlan plan {};
    plan.seed = snapshot.metadata.seed;
    WorldSavePlanChunk chunk {};
    chunk.coord = {0, 0};
    chunk.sparse_cells.push_back({17U, to_block_id(BlockType::Stone), WaterState {0}});
    plan.chunks.push_back(std::move(chunk));
    write_save_slot(save_root, 0, snapshot, plan);

    auto reports = std::vector<SaveLoadProgress> {};
    const auto loaded = load_save_slot(
        save_root,
        0,
        [&](const SaveLoadProgress& progress) {
            reports.push_back(progress);
            return SaveLoadControl::Continue;
        });
    REQUIRE(loaded.has_value());
    REQUIRE(reports.size() >= 5U);
    CHECK(reports.front().phase == SaveLoadPhase::OpeningFile);
    CHECK(reports.back().phase == SaveLoadPhase::Finalizing);
    CHECK(reports.back().normalized == doctest::Approx(1.0F));
    for (std::size_t index = 1; index < reports.size(); ++index) {
        CHECK(reports[index].completed_bytes >= reports[index - 1U].completed_bytes);
        CHECK(reports[index].normalized >= reports[index - 1U].normalized);
        CHECK(reports[index].normalized >= 0.0F);
        CHECK(reports[index].normalized <= 1.0F);
    }

    auto saw_world_phase = false;
    const auto cancelled = load_save_slot(
        save_root,
        0,
        [&](const SaveLoadProgress& progress) {
            if (progress.phase == SaveLoadPhase::ReadingWorld) {
                saw_world_phase = true;
                return SaveLoadControl::Cancel;
            }
            return SaveLoadControl::Continue;
        });
    CHECK(saw_world_phase);
    CHECK_FALSE(cancelled.has_value());

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game writer keeps an existing slot intact when a replacement is rejected before finalize") {
    const auto unique_suffix =
        std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-atomic-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot original {};
    original.metadata.saved_at_unix_seconds = 1712185200;
    original.metadata.seed = 1111;
    original.metadata.time_of_day = 7.25F;
    original.metadata.weather_time_seconds = 12.5F;
    original.spawn_position = {2.5F, 74.0F, -3.5F};
    original.player_state.position = original.spawn_position;
    original.hotbar = make_default_hotbar_state();
    original.inventory = make_default_inventory_menu_state();
    original.hotbar.selected_index = 2;

    write_save_slot(save_root, 3, original);
    const auto before = load_save_slot(save_root, 3);
    REQUIRE(before.has_value());
    REQUIRE(before->metadata.seed == original.metadata.seed);

    auto rejected_replacement = original;
    rejected_replacement.metadata.saved_at_unix_seconds = 1812185200;
    rejected_replacement.metadata.seed = 2222;
    rejected_replacement.metadata.time_of_day = 19.0F;
    rejected_replacement.item_drops.resize(129);

    CHECK_THROWS_AS(write_save_slot(save_root, 3, rejected_replacement), std::runtime_error);

    const auto after = load_save_slot(save_root, 3);
    REQUIRE(after.has_value());
    CHECK(after->metadata.saved_at_unix_seconds == before->metadata.saved_at_unix_seconds);
    CHECK(after->metadata.seed == before->metadata.seed);
    CHECK(after->metadata.time_of_day == doctest::Approx(before->metadata.time_of_day));
    CHECK(after->spawn_position.x == doctest::Approx(before->spawn_position.x));
    CHECK(after->spawn_position.y == doctest::Approx(before->spawn_position.y));
    CHECK(after->spawn_position.z == doctest::Approx(before->spawn_position.z));
    CHECK_FALSE(std::filesystem::exists(std::filesystem::path(save_slot_file_path(save_root, 3).string() + ".tmp")));

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game deletion removes slot metadata and payloads") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-delete-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.saved_at_unix_seconds = 1712185200;
    snapshot.metadata.exists = true;
    snapshot.metadata.seed = 9001;
    snapshot.metadata.time_of_day = 6.75F;
    snapshot.spawn_position = {4.5F, 72.0F, -2.0F};
    snapshot.player_state.position = snapshot.spawn_position;

    write_save_slot(save_root, 1, snapshot);

    const auto before_delete = scan_save_slots(save_root);
    REQUIRE(before_delete[1].exists);
    CHECK(load_save_slot(save_root, 1).has_value());

    CHECK(remove_save_slot(save_root, 1));

    const auto after_delete = scan_save_slots(save_root);
    CHECK_FALSE(after_delete[1].exists);
    CHECK_FALSE(load_save_slot(save_root, 1).has_value());

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save version 19 round trips maritime milestones and active ability runtimes exactly") {
    const auto unique_suffix =
        std::to_string(
            static_cast<unsigned long long>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
    const auto save_root =
        std::filesystem::temp_directory_path() /
        ("valcraft-save-v15-runtime-" +
         unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.game_mode =
        GameMode::SeaAdventure;
    snapshot.sea_adventure.active = true;
    snapshot.sea_adventure.voyage_phase =
        SeaVoyagePhase::Underway;
    snapshot.sea_adventure.route_distance =
        3'125.0F;
    snapshot.maritime_experience = {
        12ULL,
        3ULL,
        1U,
        1U,
    };

    const auto* guard_rank =
        ability_rank_definition(
            AbilityId::KnightIronGuard,
            3U);
    REQUIRE(guard_rank != nullptr);
    AbilityCastResolution guard {};
    guard.id =
        AbilityId::KnightIronGuard;
    guard.rank = 3U;
    guard.duration_seconds =
        guard_rank->duration_seconds;
    guard.values =
        guard_rank->values;
    guard.mastery_active = true;
    guard.cast_sequence = 701U;
    PlayerAbilityEffects effects {};
    REQUIRE(
        effects.activate_iron_guard(
                    guard,
                    true)
            .applied);
    static_cast<void>(
        effects.update(0.5F));
    snapshot.player_ability_runtime
        .player_effects =
        effects.snapshot();
    snapshot.player_ability_runtime.wind = {
        4.25F,
        0.30F,
        0.25F,
        0.20F,
        true,
        702U,
    };

    for (std::size_t index = 0U;
         index < 2U;
         ++index) {
        SummonedUnitSystem footman {};
        SummonedUnitSpawnRequest request {};
        request.owner_id = 1U;
        request.position = {
            3.0F +
                static_cast<float>(index),
            51.0F,
            -2.0F,
        };
        request.rank =
            index == 0U
                ? SummonedUnitRank::RankTwo
                : SummonedUnitRank::RankThree;
        request.mastered =
            index == 1U;
        request.unit_id =
            8'001U + index;
        request.cast_sequence =
            703U + index;
        REQUIRE(
            footman.summon(request)
                .spawned);
        static_cast<void>(
            footman.update(
                0.25F,
                {}));

        auto& saved =
            snapshot.player_ability_runtime
                .summoned_footmen[index];
        saved.runtime =
            footman.snapshot();
        saved.far_seconds =
            0.75F +
            static_cast<float>(index);
        saved.cast_sequence =
            request.cast_sequence;
        if (index == 1U) {
            saved.ship_local_position =
                glm::vec3 {
                    1.25F,
                    2.5F,
                    -3.75F,
                };
        }
    }
    snapshot.player_ability_runtime
        .next_summoned_unit_id =
        9'000U;
    snapshot.player_ability_runtime
        .next_cast_sequence =
        8'000U;
    const auto expected_runtime =
        sanitize_player_ability_runtime_save_state(
            snapshot.player_ability_runtime);

    write_save_slot(
        save_root,
        0U,
        snapshot);
    {
        std::ifstream input(
            save_slot_file_path(
                save_root,
                0U),
            std::ios::binary);
        REQUIRE(input.good());
        input.seekg(8);
        auto version =
            std::uint32_t {0U};
        input.read(
            reinterpret_cast<char*>(
                &version),
            sizeof(version));
        REQUIRE(input.good());
        CHECK(version == 19U);
    }

    const auto loaded =
        load_save_slot(
            save_root,
            0U);
    REQUIRE(loaded.has_value());
    CHECK(
        loaded->maritime_experience ==
        snapshot.maritime_experience);
    CHECK(
        loaded->player_ability_runtime ==
        expected_runtime);
    CHECK(
        loaded->player_ability_runtime
            .summoned_footmen[1U]
            .ship_local_position ==
        snapshot.player_ability_runtime
            .summoned_footmen[1U]
            .ship_local_position);

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save version 13 preserves the level and migrates the exact bar ratio in version 19") {
    const auto unique_suffix =
        std::to_string(
            static_cast<unsigned long long>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
    const auto save_root =
        std::filesystem::temp_directory_path() /
        ("valcraft-save-v13-migration-" +
         unique_suffix);
    std::filesystem::remove_all(save_root);

    constexpr auto level =
        std::uint32_t {42U};
    const auto old_threshold =
        player_experience_for_next_level_v13(
            level);
    const auto old_experience =
        old_threshold / 2ULL;
    REQUIRE(
        old_experience <
        player_experience_for_next_level(
            level));

    SaveGameSnapshot snapshot {};
    snapshot.metadata.game_mode =
        GameMode::SeaAdventure;
    snapshot.progression = {
        level,
        old_experience,
    };
    snapshot.sea_adventure.active = true;
    snapshot.sea_adventure.voyage_phase =
        SeaVoyagePhase::Underway;
    snapshot.sea_adventure.route_distance =
        625.0F;
    snapshot.sea_adventure.fish = 2U;
    snapshot.sea_adventure.water_flasks = 6U;
    write_save_slot(
        save_root,
        0U,
        snapshot);
    downgrade_current_save_to_v13(
        save_slot_file_path(
            save_root,
            0U));

    const auto loaded =
        load_save_slot(
            save_root,
            0U);
    REQUIRE(loaded.has_value());
    CHECK(
        loaded->progression.level ==
        level);
    const auto new_threshold =
        player_experience_for_next_level(
            level);
    const auto expected_experience =
        static_cast<std::uint64_t>(
            std::floor(
                (static_cast<long double>(
                     old_experience) /
                 static_cast<long double>(
                     old_threshold)) *
                    static_cast<long double>(
                        new_threshold) +
                0.5L));
    CHECK(
        loaded->progression.experience ==
        expected_experience);
    CHECK(
        loaded->maritime_experience
            .navigation_milestones_awarded ==
        2ULL);
    CHECK(
        loaded->maritime_experience
            .departure_awarded ==
        1U);
    CHECK(
        loaded->maritime_experience
            .open_sea_awarded ==
        1U);
    CHECK(
        loaded->maritime_experience
            .first_delivery_milestones_mask ==
        3ULL);
    CHECK(
        loaded->player_ability_runtime ==
        PlayerAbilityRuntimeSaveState {});

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save version 19 rejects invalid positional build array counts") {
    const auto unique_suffix =
        std::to_string(
            static_cast<unsigned long long>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
    const auto save_root =
        std::filesystem::temp_directory_path() /
        ("valcraft-save-v15-counts-" +
         unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    write_save_slot(
        save_root,
        0U,
        snapshot);
    const auto source =
        save_slot_file_path(
            save_root,
            0U);

    const auto overwrite_count =
        [](const std::filesystem::path& path,
           std::size_t offset,
           std::uint32_t value) {
            std::fstream output(
                path,
                std::ios::binary |
                    std::ios::in |
                    std::ios::out);
            REQUIRE(output.good());
            output.seekp(
                static_cast<std::streamoff>(
                    offset));
            output.write(
                reinterpret_cast<const char*>(
                    &value),
                sizeof(value));
            REQUIRE(output.good());
        };

    const auto ability_count_path =
        save_slot_file_path(
            save_root,
            1U);
    std::filesystem::copy_file(
        source,
        ability_count_path);
    constexpr auto ability_rank_count_offset =
        kSerializedBuildOffset +
        sizeof(std::uint32_t) +
        kPlayerAttributeCount *
            sizeof(std::uint8_t);
    overwrite_count(
        ability_count_path,
        ability_rank_count_offset,
        static_cast<std::uint32_t>(
            kAbilityCount + 1U));
    CHECK_FALSE(
        load_save_slot(
            save_root,
            1U)
            .has_value());

    const auto plan_count_path =
        save_slot_file_path(
            save_root,
            2U);
    std::filesystem::copy_file(
        source,
        plan_count_path);
    constexpr auto plan_count_offset =
        kSerializedBuildOffset +
        sizeof(std::uint32_t) +
        kPlayerAttributeCount *
            sizeof(std::uint8_t) +
        sizeof(std::uint32_t) +
        kAbilityCount *
            sizeof(std::uint8_t) +
        sizeof(std::uint32_t) +
        kAbilityCount *
            sizeof(std::uint8_t) +
        sizeof(std::uint32_t) +
        kEquippedAbilitySlotCount *
            sizeof(std::uint8_t) +
        3U * sizeof(float) +
        sizeof(std::uint32_t) +
        kAbilityCount * sizeof(float) +
        sizeof(std::uint32_t) +
        kAbilityCount *
            sizeof(std::uint8_t);
    overwrite_count(
        plan_count_path,
        plan_count_offset,
        static_cast<std::uint32_t>(
            kConstructionPlanCount + 1U));
    CHECK_FALSE(
        load_save_slot(
            save_root,
            2U)
            .has_value());

    const auto cell_count_path =
        save_slot_file_path(
            save_root,
            3U);
    std::filesystem::copy_file(
        source,
        cell_count_path);
    constexpr auto first_plan_storage_count_offset =
        plan_count_offset +
        sizeof(std::uint32_t) +
        3U * sizeof(std::uint8_t);
    overwrite_count(
        cell_count_path,
        first_plan_storage_count_offset,
        static_cast<std::uint32_t>(
            kConstructionPlanMaximumCellCount -
            1U));
    CHECK_FALSE(
        load_save_slot(
            save_root,
            3U)
            .has_value());

    std::filesystem::remove_all(save_root);
}

} // namespace valcraft
