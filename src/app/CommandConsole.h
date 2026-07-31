#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace valcraft {

inline constexpr std::size_t kCommandConsoleMaxInputBytes = 160U;
inline constexpr std::size_t kCommandConsoleMaxHistoryEntries = 20U;

enum class CommandConsoleCommand : std::uint8_t {
    None = 0,
    StartTempest = 1,
    GiveMusket = 2,
    EnterIssou = 3,
    ResetIssou = 4,
    ExitIssou = 5,
    SkipIssouCountdown = 6,
    DisableIssouGore = 7,
    EnableIssouGore = 8,
    SetIssouAwakening0 = 9,
    SetIssouAwakening1 = 10,
    SetIssouAwakening2 = 11,
    SetIssouAwakening3 = 12,
};

enum class CommandConsoleFamily : std::uint8_t {
    None = 0,
    Weather = 1,
    Give = 2,
    Issou = 3,
};

enum class CommandConsoleParseStatus : std::uint8_t {
    Ready = 0,
    Empty = 1,
    InvalidUsage = 2,
    UnknownCommand = 3,
};

enum class CommandConsoleToggleAction : std::uint8_t {
    None = 0,
    Open = 1,
    Close = 2,
};

struct CommandConsoleParseResult {
    CommandConsoleCommand command = CommandConsoleCommand::None;
    CommandConsoleParseStatus status = CommandConsoleParseStatus::Empty;
    CommandConsoleFamily family = CommandConsoleFamily::None;

    auto operator==(const CommandConsoleParseResult&) const -> bool = default;
};

struct CommandConsoleView {
    std::string_view input {};
    std::string_view feedback {};
    std::size_t cursor_byte_offset = 0U;
    bool feedback_is_error = false;
    bool visible = false;
};

struct CommandConsoleLayout {
    float panel_x = 0.0F;
    float panel_y = 0.0F;
    float panel_width = 0.0F;
    float panel_height = 0.0F;
    float input_x = 0.0F;
    float input_y = 0.0F;
    float input_width = 0.0F;
    float input_height = 0.0F;
    float text_pixel_size = 2.0F;
};

struct CommandConsoleTextWindow {
    std::size_t start = 0U;
    std::size_t length = 0U;
    std::size_t cursor_offset = 0U;
};

[[nodiscard]] auto parse_command_console_input(std::string_view input)
    -> CommandConsoleParseResult;
[[nodiscard]] constexpr auto command_console_usage(CommandConsoleFamily family) noexcept
    -> std::string_view {
    switch (family) {
    case CommandConsoleFamily::Weather:
        return "UTILISATION : /METEO TEMPETE";
    case CommandConsoleFamily::Give:
        return "UTILISATION : /GIVE FUSIL";
    case CommandConsoleFamily::Issou:
        return "UTILISATION : /ISSOU [RESET|EXIT|SKIP|GORE 0|1|AWAKE 0|1|2|3]";
    case CommandConsoleFamily::None:
    default:
        return {};
    }
}
[[nodiscard]] auto build_command_console_layout(int width, int height) noexcept
    -> CommandConsoleLayout;
[[nodiscard]] auto build_command_console_text_window(
    std::string_view input,
    std::size_t cursor_byte_offset,
    std::size_t maximum_visible_characters) noexcept
    -> CommandConsoleTextWindow;

class CommandConsoleToggle {
public:
    [[nodiscard]] auto handle_key_down(
        bool is_repeat,
        bool console_visible,
        bool can_open) noexcept
        -> CommandConsoleToggleAction;
    [[nodiscard]] auto handle_key_up(
        bool can_open) noexcept
        -> CommandConsoleToggleAction;
    void cancel() noexcept;

private:
    bool pending_open_ = false;
};

class CommandConsole {
public:
    void open() noexcept;
    void close() noexcept;
    [[nodiscard]] auto visible() const noexcept -> bool;

    void insert_text(std::string_view text);
    void backspace() noexcept;
    void delete_forward() noexcept;
    void move_cursor_left() noexcept;
    void move_cursor_right() noexcept;
    void move_cursor_home() noexcept;
    void move_cursor_end() noexcept;
    void clear_input() noexcept;
    void show_previous_history();
    void show_next_history();

    [[nodiscard]] auto submit() -> CommandConsoleParseResult;
    void set_feedback(std::string feedback, bool is_error);

    [[nodiscard]] auto view() const noexcept -> CommandConsoleView;

private:
    void leave_history_navigation() noexcept;
    void load_history_entry(std::size_t index);

    std::string input_ {};
    std::string feedback_ {};
    std::string history_draft_ {};
    std::vector<std::string> history_ {};
    std::optional<std::size_t> history_position_ {};
    std::size_t cursor_byte_offset_ = 0U;
    bool feedback_is_error_ = false;
    bool visible_ = false;
};

} // namespace valcraft
