#pragma once

#include <cstdint>
#include <optional>

namespace valcraft {

class SessionSaveState {
public:
    void reset_clean() noexcept {
        revision_ = 0U;
        pending_revision_.reset();
        dirty_ = false;
        failed_ = false;
    }

    void mark_dirty() noexcept {
        // Je change la revision a chaque mutation afin qu'un worker plus ancien
        // ne puisse jamais valider des donnees capturees avant cette mutation.
        ++revision_;
        dirty_ = true;
    }

    void begin_save() noexcept {
        pending_revision_ = revision_;
        failed_ = false;
    }

    void complete_save() noexcept {
        if (pending_revision_.has_value() && *pending_revision_ == revision_) {
            dirty_ = false;
        }
        pending_revision_.reset();
        failed_ = false;
    }

    void fail_save() noexcept {
        pending_revision_.reset();
        dirty_ = true;
        failed_ = true;
    }

    [[nodiscard]] auto dirty() const noexcept -> bool {
        return dirty_;
    }

    [[nodiscard]] auto failed() const noexcept -> bool {
        return failed_;
    }

    [[nodiscard]] auto transition_allowed() const noexcept -> bool {
        return !failed_;
    }

private:
    std::uint64_t revision_ = 0U;
    std::optional<std::uint64_t> pending_revision_ {};
    bool dirty_ = false;
    bool failed_ = false;
};

} // namespace valcraft
