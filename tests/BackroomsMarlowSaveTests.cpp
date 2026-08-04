#include "app/SaveGame.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace valcraft {

namespace {

constexpr std::size_t kSaveVersionOffset = 8U;
constexpr std::size_t kJackExtensionSize = 146U;
constexpr std::size_t kBackroomsLevelExtensionSize = 9U;
constexpr std::size_t kMarlowExtensionSize = 40U;
constexpr std::size_t kMarlowFormatOffset = 4U;
constexpr std::size_t kMarlowPressureOffset = 5U;
constexpr std::size_t kMarlowCueOffset = 9U;
constexpr std::size_t kMarlowManifestationOffset = 13U;
constexpr std::size_t kMarlowCooldownOffset = 17U;
constexpr std::size_t kMarlowLevelOffset = 21U;
constexpr std::size_t kMarlowModeOffset = 25U;
constexpr std::size_t kMarlowRandomOffset = 26U;
constexpr std::size_t kMarlowSequenceOffset = 30U;
constexpr std::size_t kMarlowHasModeOffset = 38U;
constexpr std::size_t kMarlowInitializedOffset = 39U;

class TemporarySaveDirectory {
public:
    explicit TemporarySaveDirectory(const char* label) {
        const auto suffix = std::to_string(
            static_cast<unsigned long long>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
        path_ = std::filesystem::temp_directory_path() /
                (std::string {label} + "-" + suffix);
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    ~TemporarySaveDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporarySaveDirectory(const TemporarySaveDirectory&) = delete;
    auto operator=(const TemporarySaveDirectory&)
        -> TemporarySaveDirectory& = delete;

    [[nodiscard]] auto path() const noexcept
        -> const std::filesystem::path& {
        return path_;
    }

private:
    std::filesystem::path path_ {};
};

[[nodiscard]] auto read_file(const std::filesystem::path& path)
    -> std::vector<std::uint8_t> {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Impossible d'ouvrir le slot Marlow");
    }
    const auto end = input.tellg();
    if (end < std::streampos {0}) {
        throw std::runtime_error("Taille du slot Marlow invalide");
    }
    auto bytes = std::vector<std::uint8_t>(
        static_cast<std::size_t>(static_cast<std::streamoff>(end)));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!input.good() && !input.eof()) {
        throw std::runtime_error("Lecture du slot Marlow incomplete");
    }
    return bytes;
}

void write_file(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Impossible d'ecrire le slot Marlow");
    }
    if (!bytes.empty()) {
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!output.good()) {
        throw std::runtime_error("Ecriture du slot Marlow incomplete");
    }
}

[[nodiscard]] auto tail_extension_offset(
    const std::vector<std::uint8_t>& bytes,
    std::size_t extension_size) -> std::size_t {
    if (bytes.size() < extension_size) {
        throw std::runtime_error("Extension terminale Marlow tronquee");
    }
    return bytes.size() - extension_size;
}

template <typename T>
void overwrite_value(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    const T& value) {
    REQUIRE(offset + sizeof(T) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

[[nodiscard]] auto same_marlow_state(
    const BackroomsMarlowState& left,
    const BackroomsMarlowState& right) noexcept -> bool {
    return left.pressure == right.pressure &&
           left.cue_seconds == right.cue_seconds &&
           left.manifestation_seconds == right.manifestation_seconds &&
           left.cooldown_seconds == right.cooldown_seconds &&
           left.logical_level == right.logical_level &&
           left.last_mode == right.last_mode &&
           left.random_state == right.random_state &&
           left.next_event_sequence == right.next_event_sequence &&
           left.has_last_mode == right.has_last_mode &&
           left.initialized == right.initialized;
}

[[nodiscard]] auto make_backrooms_snapshot(
    int seed,
    std::int32_t logical_level) -> SaveGameSnapshot {
    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = seed;
    snapshot.metadata.game_mode = GameMode::Backrooms;
    snapshot.backrooms_level = logical_level;
    snapshot.backrooms_jack = initialize_backrooms_jack(
        static_cast<std::uint32_t>(seed),
        logical_level);
    snapshot.backrooms_marlow = initialize_backrooms_marlow(
        static_cast<std::uint32_t>(seed),
        logical_level);
    return snapshot;
}

} // namespace

TEST_CASE("MRLW v1 reste terminal apres BJCK et BRLV et accepte Backrooms V4") {
    TemporarySaveDirectory directory {"valcraft-marlow-save-roundtrip"};
    auto snapshot = make_backrooms_snapshot(0x4D524C57, -2);
    snapshot.backrooms_marlow.pressure = 0.73F;
    snapshot.backrooms_marlow.cue_seconds = 17.25F;
    snapshot.backrooms_marlow.manifestation_seconds = 33.5F;
    snapshot.backrooms_marlow.cooldown_seconds = 11.75F;
    snapshot.backrooms_marlow.last_mode =
        BackroomsMarlowEncounterMode::WaterAmbush;
    snapshot.backrooms_marlow.random_state = 0xA1947C31U;
    snapshot.backrooms_marlow.next_event_sequence = 987'123ULL;
    snapshot.backrooms_marlow.has_last_mode = true;

    WorldSavePlan plan {};
    plan.seed = snapshot.metadata.seed;
    plan.generation_profile = WorldGenerationProfile::Backrooms;
    plan.generation_version = WorldGenerationVersion::BackroomsV4;
    plan.backrooms_level = snapshot.backrooms_level;
    write_save_slot(directory.path(), 0U, snapshot, plan);

    const auto path = save_slot_file_path(directory.path(), 0U);
    const auto bytes = read_file(path);
    REQUIRE(bytes.size() >=
            kMarlowExtensionSize +
                kBackroomsLevelExtensionSize +
                kJackExtensionSize);
    auto save_version = std::uint32_t {0U};
    std::memcpy(
        &save_version,
        bytes.data() + kSaveVersionOffset,
        sizeof(save_version));
    CHECK(save_version == 19U);

    const auto marlow_offset = tail_extension_offset(
        bytes,
        kMarlowExtensionSize);
    const auto level_offset = tail_extension_offset(
        bytes,
        kMarlowExtensionSize + kBackroomsLevelExtensionSize);
    const auto jack_offset = tail_extension_offset(
        bytes,
        kMarlowExtensionSize +
            kBackroomsLevelExtensionSize +
            kJackExtensionSize);
    CHECK(std::memcmp(bytes.data() + jack_offset, "BJCK", 4U) == 0);
    CHECK(std::memcmp(bytes.data() + level_offset, "BRLV", 4U) == 0);
    CHECK(std::memcmp(bytes.data() + marlow_offset, "MRLW", 4U) == 0);
    CHECK(bytes[marlow_offset + kMarlowFormatOffset] == 1U);

    const auto loaded = load_save_slot(directory.path(), 0U);
    REQUIRE(loaded.has_value());
    CHECK(same_marlow_state(
        loaded->backrooms_marlow,
        snapshot.backrooms_marlow));
    CHECK(loaded->backrooms_marlow.logical_level == -2);
    CHECK(
        loaded->world_save_plan.generation_version ==
        WorldGenerationVersion::BackroomsV4);
}

TEST_CASE("les slots v18 et v17 creent un Marlow dormant derive du seed et du niveau") {
    TemporarySaveDirectory directory {"valcraft-marlow-save-legacy"};
    const auto snapshot = make_backrooms_snapshot(0x4D41524C, -23);
    write_save_slot(directory.path(), 0U, snapshot);
    const auto source_path = save_slot_file_path(directory.path(), 0U);
    const auto valid = read_file(source_path);
    REQUIRE(valid.size() >
            kMarlowExtensionSize + kBackroomsLevelExtensionSize);

    auto version_18 = valid;
    version_18.resize(tail_extension_offset(
        version_18,
        kMarlowExtensionSize));
    overwrite_value(
        version_18,
        kSaveVersionOffset,
        std::uint32_t {18U});
    write_file(source_path, version_18);
    const auto loaded_18 = load_save_slot(directory.path(), 0U);
    REQUIRE(loaded_18.has_value());
    const auto expected_18 = initialize_backrooms_marlow(
        static_cast<std::uint32_t>(snapshot.metadata.seed),
        snapshot.backrooms_level);
    CHECK(same_marlow_state(
        loaded_18->backrooms_marlow,
        expected_18));
    CHECK(loaded_18->backrooms_marlow.pressure == 0.0F);
    CHECK(loaded_18->backrooms_marlow.cooldown_seconds == 0.0F);

    auto version_17 = valid;
    version_17.resize(tail_extension_offset(
        version_17,
        kMarlowExtensionSize + kBackroomsLevelExtensionSize));
    overwrite_value(
        version_17,
        kSaveVersionOffset,
        std::uint32_t {17U});
    write_file(source_path, version_17);
    const auto loaded_17 = load_save_slot(directory.path(), 0U);
    REQUIRE(loaded_17.has_value());
    const auto expected_17 = initialize_backrooms_marlow(
        static_cast<std::uint32_t>(snapshot.metadata.seed),
        0);
    CHECK(same_marlow_state(
        loaded_17->backrooms_marlow,
        expected_17));
    CHECK(loaded_17->backrooms_marlow.logical_level == 0);
}

TEST_CASE("MRLW rejette magic version floats enum RNG sequence niveau et booleens corrompus") {
    TemporarySaveDirectory directory {"valcraft-marlow-save-corruption"};
    const auto snapshot = make_backrooms_snapshot(0x53415645, -2);
    write_save_slot(directory.path(), 0U, snapshot);
    const auto path = save_slot_file_path(directory.path(), 0U);
    const auto valid = read_file(path);
    REQUIRE(valid.size() >= kMarlowExtensionSize);
    const auto offset = tail_extension_offset(
        valid,
        kMarlowExtensionSize);

    const auto rejected = [&](std::vector<std::uint8_t> corrupt) {
        write_file(path, corrupt);
        CHECK_FALSE(load_save_slot(directory.path(), 0U).has_value());
    };

    auto corrupt = valid;
    corrupt[offset] = static_cast<std::uint8_t>('X');
    rejected(corrupt);

    corrupt = valid;
    corrupt[offset + kMarlowFormatOffset] = 2U;
    rejected(corrupt);

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    corrupt = valid;
    overwrite_value(corrupt, offset + kMarlowPressureOffset, nan);
    rejected(corrupt);

    corrupt = valid;
    overwrite_value(corrupt, offset + kMarlowCueOffset, 60.1F);
    rejected(corrupt);

    corrupt = valid;
    overwrite_value(
        corrupt,
        offset + kMarlowManifestationOffset,
        std::numeric_limits<float>::infinity());
    rejected(corrupt);

    corrupt = valid;
    overwrite_value(corrupt, offset + kMarlowCooldownOffset, 24.1F);
    rejected(corrupt);

    corrupt = valid;
    overwrite_value(corrupt, offset + kMarlowLevelOffset, std::int32_t {-3});
    rejected(corrupt);

    corrupt = valid;
    corrupt[offset + kMarlowModeOffset] = 3U;
    rejected(corrupt);

    corrupt = valid;
    overwrite_value(corrupt, offset + kMarlowRandomOffset, std::uint32_t {0U});
    rejected(corrupt);

    corrupt = valid;
    overwrite_value(corrupt, offset + kMarlowSequenceOffset, std::uint64_t {0U});
    rejected(corrupt);

    corrupt = valid;
    corrupt[offset + kMarlowHasModeOffset] = 2U;
    rejected(corrupt);

    corrupt = valid;
    corrupt[offset + kMarlowInitializedOffset] = 2U;
    rejected(corrupt);

    corrupt = valid;
    corrupt.pop_back();
    rejected(corrupt);
}

TEST_CASE("l'ecriture refuse un etat Marlow non canonique ou sur un autre niveau") {
    TemporarySaveDirectory directory {"valcraft-marlow-save-invalid-write"};
    auto snapshot = make_backrooms_snapshot(12'345, -2);
    snapshot.backrooms_marlow.pressure =
        std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(
        write_save_slot(directory.path(), 0U, snapshot),
        std::runtime_error);

    snapshot = make_backrooms_snapshot(12'345, -2);
    snapshot.backrooms_marlow.last_mode =
        static_cast<BackroomsMarlowEncounterMode>(255U);
    CHECK_THROWS_AS(
        write_save_slot(directory.path(), 0U, snapshot),
        std::runtime_error);

    snapshot = make_backrooms_snapshot(12'345, -2);
    snapshot.backrooms_marlow.random_state = 0U;
    CHECK_THROWS_AS(
        write_save_slot(directory.path(), 0U, snapshot),
        std::runtime_error);

    snapshot = make_backrooms_snapshot(12'345, -2);
    snapshot.backrooms_marlow.next_event_sequence = 0U;
    CHECK_THROWS_AS(
        write_save_slot(directory.path(), 0U, snapshot),
        std::runtime_error);

    snapshot = make_backrooms_snapshot(12'345, -2);
    snapshot.backrooms_marlow.logical_level = -1;
    CHECK_THROWS_AS(
        write_save_slot(directory.path(), 0U, snapshot),
        std::runtime_error);
}

} // namespace valcraft
