#pragma once

#include "app/SaveGame.h"
#include "app/SessionSaveState.h"
#include "world/World.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace valcraft {

struct ScenarioSessionRestore {
    std::unique_ptr<World> world {};
    SaveGameSnapshot snapshot {};
    SessionSaveState save_state {};
    std::optional<std::size_t> active_save_slot {};
    std::uint64_t capture_sequence = 0U;
};

class ScenarioSessionState {
public:
    [[nodiscard]] auto capture(
        std::unique_ptr<World> world,
        SaveGameSnapshot snapshot,
        const SessionSaveState& save_state,
        std::optional<std::size_t> active_save_slot) noexcept
        -> bool {
        if (active() ||
            world == nullptr) {
            return false;
        }

        world_ = std::move(world);
        snapshot_ =
            std::move(snapshot);
        save_state_ = save_state;
        active_save_slot_ =
            active_save_slot;
        ++capture_sequence_;
        if (capture_sequence_ == 0U) {
            capture_sequence_ = 1U;
        }
        return true;
    }

    [[nodiscard]] auto release() noexcept
        -> std::optional<ScenarioSessionRestore> {
        if (!active()) {
            return std::nullopt;
        }

        ScenarioSessionRestore restore {
            std::move(world_),
            std::move(snapshot_),
            save_state_,
            active_save_slot_,
            capture_sequence_,
        };
        clear();
        return restore;
    }

    void clear() noexcept {
        world_.reset();
        snapshot_ = {};
        save_state_.reset_clean();
        active_save_slot_.reset();
    }

    [[nodiscard]] auto active() const noexcept
        -> bool {
        return world_ != nullptr;
    }

    [[nodiscard]] auto saves_allowed() const noexcept
        -> bool {
        return !active();
    }

    [[nodiscard]] auto permanent_rewards_allowed() const noexcept
        -> bool {
        return !active();
    }

    [[nodiscard]] auto capture_sequence() const noexcept
        -> std::uint64_t {
        return capture_sequence_;
    }

private:
    std::unique_ptr<World> world_ {};
    SaveGameSnapshot snapshot_ {};
    SessionSaveState save_state_ {};
    std::optional<std::size_t>
        active_save_slot_ {};
    std::uint64_t capture_sequence_ = 0U;
};

} // namespace valcraft
