#include "app/CommandConsole.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace valcraft {

namespace {

auto normalized_command(std::string_view input) -> std::string {
    std::string normalized {};
    normalized.reserve(input.size());

    auto previous_was_space = true;
    for (const auto character : input) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isspace(byte) != 0) {
            if (!previous_was_space) {
                normalized.push_back(' ');
                previous_was_space = true;
            }
            continue;
        }

        normalized.push_back(
            static_cast<char>(
                std::tolower(byte)));
        previous_was_space = false;
    }

    if (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }
    return normalized;
}

} // namespace

auto parse_command_console_input(std::string_view input)
    -> CommandConsoleParseResult {
    const auto command = normalized_command(input);
    if (command.empty()) {
        return {
            CommandConsoleCommand::None,
            CommandConsoleParseStatus::Empty,
        };
    }
    if (command == "/meteo tempete") {
        return {
            CommandConsoleCommand::StartTempest,
            CommandConsoleParseStatus::Ready,
        };
    }
    if (command == "/meteo" ||
        command.starts_with("/meteo ")) {
        return {
            CommandConsoleCommand::None,
            CommandConsoleParseStatus::InvalidUsage,
        };
    }
    return {
        CommandConsoleCommand::None,
        CommandConsoleParseStatus::UnknownCommand,
    };
}

auto build_command_console_layout(int width, int height) noexcept
    -> CommandConsoleLayout {
    const auto viewport_width =
        static_cast<float>(std::max(width, 1));
    const auto viewport_height =
        static_cast<float>(std::max(height, 1));
    const auto requested_margin =
        std::clamp(
            std::min(viewport_width, viewport_height) * 0.025F,
            10.0F,
            22.0F);
    const auto horizontal_margin =
        std::min(
            requested_margin,
            std::max(
                0.0F,
                (viewport_width - 1.0F) *
                    0.5F));
    const auto vertical_margin =
        std::min(
            requested_margin,
            std::max(
                0.0F,
                (viewport_height - 1.0F) *
                    0.5F));
    const auto panel_width =
        std::max(
            1.0F,
            std::min(
                920.0F,
                viewport_width -
                    horizontal_margin * 2.0F));
    const auto panel_height =
        std::max(
            1.0F,
            std::min(
                std::clamp(
                    viewport_height * 0.15F,
                    108.0F,
                    142.0F),
                viewport_height -
                    vertical_margin * 2.0F));
    const auto inner_padding =
        std::min(
            std::clamp(
                panel_width * 0.018F,
                8.0F,
                16.0F),
            std::max(
                0.0F,
                (panel_width - 1.0F) *
                    0.5F));
    const auto input_height =
        std::clamp(panel_height * 0.28F, 26.0F, 36.0F);

    CommandConsoleLayout layout {};
    layout.panel_x =
        (viewport_width - panel_width) * 0.5F;
    layout.panel_y = vertical_margin;
    layout.panel_width = panel_width;
    layout.panel_height = panel_height;
    layout.input_x = layout.panel_x + inner_padding;
    layout.input_y =
        std::min(
            layout.panel_y +
                std::clamp(
                    panel_height * 0.31F,
                    30.0F,
                    44.0F),
            layout.panel_y +
                std::max(
                    panel_height - 1.0F,
                    0.0F));
    layout.input_width =
        std::max(
            1.0F,
            panel_width - inner_padding * 2.0F);
    layout.input_height =
        std::min(
            input_height,
            std::max(
                1.0F,
                layout.panel_y +
                        layout.panel_height -
                        layout.input_y));
    layout.text_pixel_size =
        panel_width >= 520.0F ? 3.0F : 2.0F;
    return layout;
}

auto build_command_console_text_window(
    std::string_view input,
    std::size_t cursor_byte_offset,
    std::size_t maximum_visible_characters) noexcept
    -> CommandConsoleTextWindow {
    const auto cursor =
        std::min(cursor_byte_offset, input.size());
    if (maximum_visible_characters == 0U) {
        return {
            cursor,
            0U,
            0U,
        };
    }
    const auto capacity =
        maximum_visible_characters;
    if (input.size() <= capacity) {
        return {
            0U,
            input.size(),
            cursor,
        };
    }

    auto start =
        cursor > capacity
            ? cursor - capacity
            : 0U;
    if (start + capacity > input.size()) {
        start = input.size() - capacity;
    }
    return {
        start,
        capacity,
        cursor - start,
    };
}

auto CommandConsoleToggle::handle_key_down(
    bool is_repeat,
    bool console_visible,
    bool can_open) noexcept
    -> CommandConsoleToggleAction {
    if (is_repeat) {
        return CommandConsoleToggleAction::None;
    }

    pending_open_ =
        !console_visible &&
        can_open;
    return console_visible
               ? CommandConsoleToggleAction::Close
               : CommandConsoleToggleAction::None;
}

auto CommandConsoleToggle::handle_key_up(
    bool can_open) noexcept
    -> CommandConsoleToggleAction {
    const auto should_open =
        std::exchange(
            pending_open_,
            false) &&
        can_open;
    return should_open
               ? CommandConsoleToggleAction::Open
               : CommandConsoleToggleAction::None;
}

void CommandConsoleToggle::cancel() noexcept {
    pending_open_ = false;
}

void CommandConsole::open() noexcept {
    visible_ = true;
    history_position_.reset();
    history_draft_.clear();
}

void CommandConsole::close() noexcept {
    visible_ = false;
    history_position_.reset();
    history_draft_.clear();
}

auto CommandConsole::visible() const noexcept -> bool {
    return visible_;
}

void CommandConsole::insert_text(std::string_view text) {
    std::string accepted {};
    accepted.reserve(
        std::min(
            text.size(),
            kCommandConsoleMaxInputBytes -
                std::min(
                    input_.size(),
                    kCommandConsoleMaxInputBytes)));
    for (const auto character : text) {
        if (input_.size() + accepted.size() >=
            kCommandConsoleMaxInputBytes) {
            break;
        }
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte > 0x7EU ||
            character == '`') {
            continue;
        }
        accepted.push_back(character);
    }
    if (accepted.empty()) {
        return;
    }

    leave_history_navigation();
    input_.insert(
        cursor_byte_offset_,
        accepted);
    cursor_byte_offset_ +=
        accepted.size();
}

void CommandConsole::backspace() noexcept {
    if (cursor_byte_offset_ == 0U || input_.empty()) {
        return;
    }
    leave_history_navigation();
    input_.erase(cursor_byte_offset_ - 1U, 1U);
    --cursor_byte_offset_;
}

void CommandConsole::delete_forward() noexcept {
    if (cursor_byte_offset_ >= input_.size()) {
        return;
    }
    leave_history_navigation();
    input_.erase(cursor_byte_offset_, 1U);
}

void CommandConsole::move_cursor_left() noexcept {
    if (cursor_byte_offset_ > 0U) {
        --cursor_byte_offset_;
    }
}

void CommandConsole::move_cursor_right() noexcept {
    if (cursor_byte_offset_ < input_.size()) {
        ++cursor_byte_offset_;
    }
}

void CommandConsole::move_cursor_home() noexcept {
    cursor_byte_offset_ = 0U;
}

void CommandConsole::move_cursor_end() noexcept {
    cursor_byte_offset_ = input_.size();
}

void CommandConsole::clear_input() noexcept {
    input_.clear();
    cursor_byte_offset_ = 0U;
    leave_history_navigation();
}

void CommandConsole::show_previous_history() {
    if (history_.empty()) {
        return;
    }

    if (!history_position_.has_value()) {
        history_draft_ = input_;
        history_position_ = history_.size();
    }
    if (*history_position_ > 0U) {
        --*history_position_;
    }
    load_history_entry(*history_position_);
}

void CommandConsole::show_next_history() {
    if (!history_position_.has_value()) {
        return;
    }

    if (*history_position_ + 1U < history_.size()) {
        ++*history_position_;
        load_history_entry(*history_position_);
        return;
    }

    input_ = history_draft_;
    cursor_byte_offset_ = input_.size();
    history_position_.reset();
    history_draft_.clear();
}

auto CommandConsole::submit() -> CommandConsoleParseResult {
    const auto result = parse_command_console_input(input_);
    if (result.status != CommandConsoleParseStatus::Empty) {
        if (history_.empty() || history_.back() != input_) {
            if (history_.size() >= kCommandConsoleMaxHistoryEntries) {
                history_.erase(history_.begin());
            }
            history_.push_back(input_);
        }
    }

    input_.clear();
    cursor_byte_offset_ = 0U;
    history_position_.reset();
    history_draft_.clear();
    return result;
}

void CommandConsole::set_feedback(std::string feedback, bool is_error) {
    feedback_ = std::move(feedback);
    feedback_is_error_ = is_error;
}

auto CommandConsole::view() const noexcept -> CommandConsoleView {
    return {
        input_,
        feedback_,
        std::min(cursor_byte_offset_, input_.size()),
        feedback_is_error_,
        visible_,
    };
}

void CommandConsole::leave_history_navigation() noexcept {
    history_position_.reset();
    history_draft_.clear();
}

void CommandConsole::load_history_entry(std::size_t index) {
    if (index >= history_.size()) {
        return;
    }
    input_ = history_[index];
    cursor_byte_offset_ = input_.size();
}

} // namespace valcraft
