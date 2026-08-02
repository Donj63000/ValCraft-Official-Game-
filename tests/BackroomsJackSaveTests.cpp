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
constexpr std::size_t kFlashlightExtensionSize = 10U;
constexpr std::size_t kJackExtensionSize = 146U;
constexpr std::size_t kBackroomsLevelExtensionSize =
    4U + sizeof(std::uint8_t) + sizeof(std::int32_t);
constexpr std::size_t kMarlowExtensionSize = 40U;
constexpr std::size_t kJackFormatVersionOffset = 4U;
constexpr std::size_t kJackPhaseOffset = 5U;
constexpr std::size_t kJackPositionOffset = 6U;
constexpr std::size_t kJackHunchOffset = 50U;
constexpr std::size_t kJackSpawnDelayOffset = 74U;
constexpr std::size_t kJackCooldownOffset = 78U;
constexpr std::size_t kJackEvadedCountOffset = 118U;
constexpr std::size_t kJackRandomStateOffset = 127U;
constexpr std::size_t kJackActiveOffset = 139U;

class TemporarySaveDirectory {
public:
    explicit TemporarySaveDirectory(const char* label) {
        const auto suffix =
            std::to_string(
                static_cast<unsigned long long>(
                    std::chrono::steady_clock::now()
                        .time_since_epoch()
                        .count()));
        path_ =
            std::filesystem::temp_directory_path() /
            (std::string {label} + "-" + suffix);
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    ~TemporarySaveDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporarySaveDirectory(
        const TemporarySaveDirectory&) = delete;
    auto operator=(
        const TemporarySaveDirectory&)
        -> TemporarySaveDirectory& = delete;

    [[nodiscard]] auto path() const noexcept
        -> const std::filesystem::path& {
        return path_;
    }

private:
    std::filesystem::path path_ {};
};

[[nodiscard]] auto read_file(
    const std::filesystem::path& path)
    -> std::vector<std::uint8_t> {
    std::ifstream input(
        path,
        std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error(
            "Impossible d'ouvrir la sauvegarde de test");
    }
    const auto end = input.tellg();
    if (end < std::streampos {0}) {
        throw std::runtime_error(
            "Taille de sauvegarde de test invalide");
    }
    auto bytes = std::vector<std::uint8_t>(
        static_cast<std::size_t>(
            static_cast<std::streamoff>(end)));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!input.good() && !input.eof()) {
        throw std::runtime_error(
            "Lecture de sauvegarde de test incomplète");
    }
    return bytes;
}

void write_file(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(
        path,
        std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "Impossible d'écrire la sauvegarde de test");
    }
    if (!bytes.empty()) {
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!output.good()) {
        throw std::runtime_error(
            "Écriture de sauvegarde de test incomplète");
    }
}

template <typename T>
void overwrite_value(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    const T& value) {
    REQUIRE(offset + sizeof(T) <= bytes.size());
    std::memcpy(
        bytes.data() + offset,
        &value,
        sizeof(T));
}

[[nodiscard]] auto same_jack_state(
    const BackroomsJackState& lhs,
    const BackroomsJackState& rhs) noexcept -> bool {
    return lhs.phase == rhs.phase &&
           lhs.position == rhs.position &&
           lhs.last_seen_player_position ==
               rhs.last_seen_player_position &&
           lhs.previous_player_position ==
               rhs.previous_player_position &&
           lhs.body_yaw_degrees ==
               rhs.body_yaw_degrees &&
           lhs.head_yaw_degrees ==
               rhs.head_yaw_degrees &&
           lhs.hunch_ratio == rhs.hunch_ratio &&
           lhs.motion_amount == rhs.motion_amount &&
           lhs.phase_seconds == rhs.phase_seconds &&
           lhs.suspicion == rhs.suspicion &&
           lhs.lost_sight_seconds ==
               rhs.lost_sight_seconds &&
           lhs.unseen_travel_distance ==
               rhs.unseen_travel_distance &&
           lhs.spawn_check_seconds ==
               rhs.spawn_check_seconds &&
           lhs.cooldown_seconds ==
               rhs.cooldown_seconds &&
           lhs.footstep_distance ==
               rhs.footstep_distance &&
           lhs.evaded_chunks == rhs.evaded_chunks &&
           lhs.evaded_chunk_count ==
               rhs.evaded_chunk_count &&
           lhs.last_evade_chunk ==
               rhs.last_evade_chunk &&
           lhs.random_state == rhs.random_state &&
           lhs.next_event_sequence ==
               rhs.next_event_sequence &&
           lhs.logical_level ==
               rhs.logical_level &&
           lhs.active == rhs.active &&
           lhs.has_previous_player_position ==
               rhs.has_previous_player_position &&
           lhs.has_last_evade_chunk ==
               rhs.has_last_evade_chunk &&
           lhs.next_step_is_wooden ==
               rhs.next_step_is_wooden &&
           lhs.notice_event_emitted ==
               rhs.notice_event_emitted &&
           lhs.chase_event_emitted ==
               rhs.chase_event_emitted &&
           lhs.screamer_event_emitted ==
               rhs.screamer_event_emitted;
}

[[nodiscard]] auto make_persistent_jack_state()
    -> BackroomsJackState {
    BackroomsJackState state {};
    state.phase = BackroomsJackPhase::Searching;
    state.position = {-18.5F, 41.001F, 63.5F};
    state.last_seen_player_position =
        {-14.25F, 41.001F, 58.75F};
    state.previous_player_position =
        {-12.5F, 41.001F, 55.5F};
    state.body_yaw_degrees = -137.5F;
    state.head_yaw_degrees = 22.25F;
    state.hunch_ratio = 0.72F;
    state.motion_amount = 0.86F;
    state.phase_seconds = 19.75F;
    state.suspicion = 2.5F;
    state.lost_sight_seconds = 6.25F;
    state.unseen_travel_distance = 47.5F;
    state.spawn_check_seconds = 0.0F;
    state.cooldown_seconds = 0.0F;
    state.footstep_distance = 1.08F;
    state.evaded_chunks = {{
        {-2, 3},
        {-1, 3},
        {0, 3},
        {0, 0},
    }};
    state.evaded_chunk_count = 3U;
    state.last_evade_chunk = {0, 3};
    state.random_state = 0x7B91A43DU;
    state.next_event_sequence = 987'654'321ULL;
    state.active = true;
    state.has_previous_player_position = true;
    state.has_last_evade_chunk = true;
    state.next_step_is_wooden = true;
    state.notice_event_emitted = true;
    state.chase_event_emitted = true;
    state.screamer_event_emitted = false;
    return state;
}

} // namespace

TEST_CASE("le format v19 conserve BJCK v1 et BRLV avant MRLW") {
    TemporarySaveDirectory directory {
        "valcraft-jack-save-roundtrip"};
    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 0x13572468;
    snapshot.metadata.game_mode = GameMode::Backrooms;
    snapshot.backrooms_flashlight = {
        .battery_charge = 0.63F,
        .enabled = true,
    };
    snapshot.backrooms_jack =
        make_persistent_jack_state();
    snapshot.backrooms_level = -2;
    snapshot.backrooms_jack.logical_level = -2;
    snapshot.backrooms_marlow.logical_level = -2;

    write_save_slot(
        directory.path(),
        0U,
        snapshot);
    const auto path =
        save_slot_file_path(directory.path(), 0U);
    const auto bytes = read_file(path);
    REQUIRE(bytes.size() >=
            kSaveVersionOffset +
                sizeof(std::uint32_t));
    REQUIRE(bytes.size() >= kJackExtensionSize);

    auto save_version = std::uint32_t {0U};
    std::memcpy(
        &save_version,
        bytes.data() + kSaveVersionOffset,
        sizeof(save_version));
    CHECK(save_version == 19U);

    const auto jack_offset =
        bytes.size() -
        kMarlowExtensionSize -
        kBackroomsLevelExtensionSize -
        kJackExtensionSize;
    REQUIRE(jack_offset >= kFlashlightExtensionSize);
    const auto flashlight_offset =
        jack_offset - kFlashlightExtensionSize;
    CHECK(bytes[flashlight_offset + 0U] ==
          static_cast<std::uint8_t>('B'));
    CHECK(bytes[flashlight_offset + 1U] ==
          static_cast<std::uint8_t>('F'));
    CHECK(bytes[flashlight_offset + 2U] ==
          static_cast<std::uint8_t>('L'));
    CHECK(bytes[flashlight_offset + 3U] ==
          static_cast<std::uint8_t>('H'));
    CHECK(bytes[flashlight_offset + 4U] == 1U);
    CHECK(bytes[jack_offset + 0U] ==
          static_cast<std::uint8_t>('B'));
    CHECK(bytes[jack_offset + 1U] ==
          static_cast<std::uint8_t>('J'));
    CHECK(bytes[jack_offset + 2U] ==
          static_cast<std::uint8_t>('C'));
    CHECK(bytes[jack_offset + 3U] ==
          static_cast<std::uint8_t>('K'));
    CHECK(bytes[jack_offset +
                kJackFormatVersionOffset] == 1U);
    const auto level_offset =
        bytes.size() -
        kMarlowExtensionSize -
        kBackroomsLevelExtensionSize;
    CHECK(bytes[level_offset + 0U] ==
          static_cast<std::uint8_t>('B'));
    CHECK(bytes[level_offset + 1U] ==
          static_cast<std::uint8_t>('R'));
    CHECK(bytes[level_offset + 2U] ==
          static_cast<std::uint8_t>('L'));
    CHECK(bytes[level_offset + 3U] ==
          static_cast<std::uint8_t>('V'));
    CHECK(bytes[level_offset + 4U] == 1U);

    const auto loaded =
        load_save_slot(directory.path(), 0U);
    REQUIRE(loaded.has_value());
    CHECK(same_jack_state(
        loaded->backrooms_jack,
        snapshot.backrooms_jack));
    CHECK(loaded->backrooms_level == -2);
    CHECK(
        loaded->world_save_plan.backrooms_level ==
        -2);
    CHECK(
        loaded->backrooms_flashlight.battery_charge ==
        doctest::Approx(0.63F));
    CHECK(loaded->backrooms_flashlight.enabled);
}

TEST_CASE("une sauvegarde Backrooms historique normalise niveau et delais de Jack") {
    TemporarySaveDirectory directory {
        "valcraft-jack-save-historical-pressure"};
    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 0x504C414E;
    snapshot.metadata.game_mode = GameMode::Backrooms;
    snapshot.backrooms_level = -37;
    snapshot.backrooms_jack = initialize_backrooms_jack(
        static_cast<std::uint32_t>(snapshot.metadata.seed),
        snapshot.backrooms_level);
    snapshot.backrooms_marlow = initialize_backrooms_marlow(
        static_cast<std::uint32_t>(snapshot.metadata.seed),
        snapshot.backrooms_level);
    snapshot.backrooms_jack.spawn_check_seconds =
        kBackroomsJackMaximumPersistedSpawnDelaySeconds;
    snapshot.backrooms_jack.cooldown_seconds =
        kBackroomsJackMaximumPersistedCooldownSeconds;

    write_save_slot(directory.path(), 0U, snapshot);
    const auto path = save_slot_file_path(directory.path(), 0U);
    auto bytes = read_file(path);
    REQUIRE(bytes.size() >=
            kMarlowExtensionSize +
                kBackroomsLevelExtensionSize +
                kJackExtensionSize);
    const auto jack_offset =
        bytes.size() -
        kMarlowExtensionSize -
        kBackroomsLevelExtensionSize -
        kJackExtensionSize;

    // Je reproduis les compteurs longs acceptes par l'ancien directeur. Le
    // chargeur doit les lire puis les borner, sans perdre le niveau BRLV.
    constexpr auto historical_spawn_delay = 180.0F;
    constexpr auto historical_cooldown = 360.0F;
    overwrite_value(
        bytes,
        jack_offset + kJackSpawnDelayOffset,
        historical_spawn_delay);
    overwrite_value(
        bytes,
        jack_offset + kJackCooldownOffset,
        historical_cooldown);
    write_file(path, bytes);

    const auto loaded = load_save_slot(directory.path(), 0U);
    REQUIRE(loaded.has_value());
    CHECK(loaded->backrooms_level == -37);
    CHECK(loaded->world_save_plan.backrooms_level == -37);
    CHECK(loaded->backrooms_jack.logical_level == -37);
    CHECK(loaded->backrooms_jack.spawn_check_seconds ==
          doctest::Approx(kBackroomsJackMaximumPersistedSpawnDelaySeconds));
    CHECK(loaded->backrooms_jack.cooldown_seconds ==
          doctest::Approx(kBackroomsJackMaximumPersistedCooldownSeconds));
}

TEST_CASE("la migration v16 conserve BFLH et initialise un Jack dormant sain") {
    TemporarySaveDirectory directory {
        "valcraft-jack-save-v16"};
    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 0x24681357;
    snapshot.metadata.game_mode = GameMode::Backrooms;
    snapshot.backrooms_flashlight = {
        .battery_charge = 0.41F,
        .enabled = true,
    };
    snapshot.backrooms_jack =
        make_persistent_jack_state();

    write_save_slot(
        directory.path(),
        0U,
        snapshot);
    const auto path =
        save_slot_file_path(directory.path(), 0U);
    auto bytes = read_file(path);
    REQUIRE(
        bytes.size() >
        kMarlowExtensionSize +
        kBackroomsLevelExtensionSize +
            kJackExtensionSize);
    bytes.resize(
        bytes.size() -
        kMarlowExtensionSize -
        kBackroomsLevelExtensionSize -
        kJackExtensionSize);
    const auto legacy_version = std::uint32_t {16U};
    overwrite_value(
        bytes,
        kSaveVersionOffset,
        legacy_version);
    write_file(path, bytes);

    const auto loaded =
        load_save_slot(directory.path(), 0U);
    REQUIRE(loaded.has_value());
    const auto expected =
        initialize_backrooms_jack(
            static_cast<std::uint32_t>(
                snapshot.metadata.seed));
    CHECK(same_jack_state(
        loaded->backrooms_jack,
        expected));
    CHECK(
        loaded->backrooms_jack.phase ==
        BackroomsJackPhase::Dormant);
    CHECK_FALSE(loaded->backrooms_jack.active);
    CHECK(loaded->backrooms_jack.random_state != 0U);
    CHECK(
        loaded->backrooms_flashlight.battery_charge ==
        doctest::Approx(0.41F));
    CHECK(loaded->backrooms_flashlight.enabled);
}

TEST_CASE("la migration v17 conserve BJCK et initialise le niveau historique a zero") {
    TemporarySaveDirectory directory {
        "valcraft-backrooms-level-save-v17"};
    SaveGameSnapshot snapshot {};
    snapshot.metadata.seed = 0x4C564C31;
    snapshot.metadata.game_mode = GameMode::Backrooms;
    snapshot.backrooms_level = -17;
    snapshot.backrooms_jack =
        make_persistent_jack_state();
    snapshot.backrooms_jack.logical_level = -17;
    snapshot.backrooms_marlow.logical_level = -17;
    write_save_slot(
        directory.path(),
        0U,
        snapshot);

    const auto path =
        save_slot_file_path(directory.path(), 0U);
    auto bytes = read_file(path);
    REQUIRE(
        bytes.size() >
        kMarlowExtensionSize +
        kBackroomsLevelExtensionSize);
    bytes.resize(
        bytes.size() -
        kMarlowExtensionSize -
        kBackroomsLevelExtensionSize);
    overwrite_value(
        bytes,
        kSaveVersionOffset,
        std::uint32_t {17U});
    write_file(path, bytes);

    const auto loaded =
        load_save_slot(directory.path(), 0U);
    REQUIRE(loaded.has_value());
    auto expected = snapshot.backrooms_jack;
    expected.logical_level = 0;
    CHECK(same_jack_state(
        loaded->backrooms_jack,
        expected));
    CHECK(loaded->backrooms_level == 0);
    CHECK(
        loaded->world_save_plan.backrooms_level ==
        0);
}

TEST_CASE("BJCK rejette enums floats compteurs RNG et booleens corrompus") {
    TemporarySaveDirectory directory {
        "valcraft-jack-save-corruption"};
    SaveGameSnapshot snapshot {};
    snapshot.metadata.game_mode = GameMode::Backrooms;
    snapshot.backrooms_jack =
        make_persistent_jack_state();
    write_save_slot(
        directory.path(),
        0U,
        snapshot);

    const auto source_path =
        save_slot_file_path(directory.path(), 0U);
    const auto valid_bytes = read_file(source_path);
    REQUIRE(valid_bytes.size() >=
            kMarlowExtensionSize +
                kBackroomsLevelExtensionSize +
                kJackExtensionSize);
    const auto jack_offset =
        valid_bytes.size() -
        kMarlowExtensionSize -
        kBackroomsLevelExtensionSize -
        kJackExtensionSize;

    auto invalid_format_version = valid_bytes;
    invalid_format_version[
        jack_offset +
        kJackFormatVersionOffset] = 2U;
    write_file(
        save_slot_file_path(directory.path(), 0U),
        invalid_format_version);
    CHECK_FALSE(
        load_save_slot(directory.path(), 0U)
            .has_value());

    auto invalid_phase = valid_bytes;
    invalid_phase[jack_offset + kJackPhaseOffset] =
        std::numeric_limits<std::uint8_t>::max();
    write_file(
        save_slot_file_path(directory.path(), 1U),
        invalid_phase);
    CHECK_FALSE(
        load_save_slot(directory.path(), 1U)
            .has_value());

    auto invalid_hunch = valid_bytes;
    const auto nan =
        std::numeric_limits<float>::quiet_NaN();
    overwrite_value(
        invalid_hunch,
        jack_offset + kJackHunchOffset,
        nan);
    write_file(
        save_slot_file_path(directory.path(), 2U),
        invalid_hunch);
    CHECK_FALSE(
        load_save_slot(directory.path(), 2U)
            .has_value());

    auto invalid_position = valid_bytes;
    const auto out_of_bounds_position =
        1'000'001.0F;
    overwrite_value(
        invalid_position,
        jack_offset + kJackPositionOffset,
        out_of_bounds_position);
    write_file(
        save_slot_file_path(directory.path(), 7U),
        invalid_position);
    CHECK_FALSE(
        load_save_slot(directory.path(), 7U)
            .has_value());

    auto invalid_count = valid_bytes;
    invalid_count[
        jack_offset +
        kJackEvadedCountOffset] = 5U;
    write_file(
        save_slot_file_path(directory.path(), 3U),
        invalid_count);
    CHECK_FALSE(
        load_save_slot(directory.path(), 3U)
            .has_value());

    auto invalid_rng = valid_bytes;
    const auto zero_rng = std::uint32_t {0U};
    overwrite_value(
        invalid_rng,
        jack_offset + kJackRandomStateOffset,
        zero_rng);
    write_file(
        save_slot_file_path(directory.path(), 4U),
        invalid_rng);
    CHECK_FALSE(
        load_save_slot(directory.path(), 4U)
            .has_value());

    auto invalid_bool = valid_bytes;
    invalid_bool[
        jack_offset +
        kJackActiveOffset] = 2U;
    write_file(
        save_slot_file_path(directory.path(), 5U),
        invalid_bool);
    CHECK_FALSE(
        load_save_slot(directory.path(), 5U)
            .has_value());

    auto truncated = valid_bytes;
    truncated.pop_back();
    write_file(
        save_slot_file_path(directory.path(), 6U),
        truncated);
    CHECK_FALSE(
        load_save_slot(directory.path(), 6U)
            .has_value());
}

TEST_CASE("BRLV rejette un niveau hors limites et les profils non Backrooms") {
    TemporarySaveDirectory directory {
        "valcraft-backrooms-level-save-corruption"};
    SaveGameSnapshot snapshot {};
    snapshot.metadata.game_mode = GameMode::Backrooms;
    snapshot.backrooms_jack =
        make_persistent_jack_state();
    write_save_slot(
        directory.path(),
        0U,
        snapshot);

    const auto path =
        save_slot_file_path(directory.path(), 0U);
    auto bytes = read_file(path);
    REQUIRE(
        bytes.size() >=
        kMarlowExtensionSize +
            kBackroomsLevelExtensionSize);
    const auto level_offset =
        bytes.size() -
        kMarlowExtensionSize -
        kBackroomsLevelExtensionSize;
    overwrite_value(
        bytes,
        level_offset + 5U,
        std::int32_t {
            kBackroomsMaximumLogicalLevel + 1});
    write_file(path, bytes);
    CHECK_FALSE(
        load_save_slot(directory.path(), 0U)
            .has_value());

    SaveGameSnapshot invalid_profile {};
    invalid_profile.metadata.game_mode =
        GameMode::ClassicAdventure;
    invalid_profile.backrooms_level = -2;
    invalid_profile.backrooms_jack.logical_level =
        -2;
    invalid_profile.backrooms_marlow.logical_level =
        -2;
    CHECK_THROWS_AS(
        write_save_slot(
            directory.path(),
            1U,
            invalid_profile),
        std::runtime_error);
}

TEST_CASE("l'ecriture refuse un etat Jack non canonique") {
    TemporarySaveDirectory directory {
        "valcraft-jack-save-invalid-write"};
    SaveGameSnapshot snapshot {};
    snapshot.backrooms_jack =
        make_persistent_jack_state();
    snapshot.backrooms_jack.random_state = 0U;
    CHECK_THROWS_AS(
        write_save_slot(
            directory.path(),
            0U,
            snapshot),
        std::runtime_error);

    snapshot.backrooms_jack =
        make_persistent_jack_state();
    snapshot.backrooms_jack.has_last_evade_chunk =
        false;
    CHECK_THROWS_AS(
        write_save_slot(
            directory.path(),
            1U,
            snapshot),
        std::runtime_error);
}

} // namespace valcraft
