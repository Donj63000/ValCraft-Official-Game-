#include "app/CommandConsole.h"

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <utility>

namespace valcraft {

TEST_CASE("command console recognizes the tempest command robustly") {
    CHECK(
        parse_command_console_input("/meteo tempete") ==
        CommandConsoleParseResult {
            CommandConsoleCommand::StartTempest,
            CommandConsoleParseStatus::Ready,
        });
    CHECK(
        parse_command_console_input("  /METEO   TEMPETE  ") ==
        CommandConsoleParseResult {
            CommandConsoleCommand::StartTempest,
            CommandConsoleParseStatus::Ready,
        });
}

TEST_CASE("command console distinguishes empty invalid and unknown commands") {
    CHECK(
        parse_command_console_input("   ").status ==
        CommandConsoleParseStatus::Empty);
    CHECK(
        parse_command_console_input("/meteo pluie").status ==
        CommandConsoleParseStatus::InvalidUsage);
    CHECK(
        parse_command_console_input("/meteo tempete maintenant").status ==
        CommandConsoleParseStatus::InvalidUsage);
    CHECK(
        parse_command_console_input("/inconnue").status ==
        CommandConsoleParseStatus::UnknownCommand);
}

TEST_CASE("command console edits its bounded ascii input around the cursor") {
    CommandConsole console {};
    console.open();
    console.insert_text("/meteo tempte");
    console.move_cursor_left();
    console.move_cursor_left();
    console.insert_text("e");

    CHECK(console.view().input == "/meteo tempete");
    CHECK(console.submit().command == CommandConsoleCommand::StartTempest);

    console.insert_text(std::string(kCommandConsoleMaxInputBytes + 32U, 'x'));
    CHECK(console.view().input.size() == kCommandConsoleMaxInputBytes);
    console.move_cursor_home();
    console.delete_forward();
    CHECK(console.view().input.size() == kCommandConsoleMaxInputBytes - 1U);
    console.move_cursor_end();
    console.backspace();
    CHECK(console.view().input.size() == kCommandConsoleMaxInputBytes - 2U);
}

TEST_CASE("command console safely inserts an aliased view of its own input") {
    CommandConsole console {};
    console.open();
    const std::string original =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";
    console.insert_text(original);
    console.move_cursor_home();
    console.insert_text(console.view().input);
    CHECK(console.view().input == original + original);
    CHECK(
        console.view().cursor_byte_offset ==
        original.size());
}

TEST_CASE("command console toggle opens and closes on a complete key cycle") {
    CommandConsoleToggle toggle {};

    CHECK(
        toggle.handle_key_down(
            false,
            false,
            true) ==
        CommandConsoleToggleAction::None);
    CHECK(
        toggle.handle_key_up(true) ==
        CommandConsoleToggleAction::Open);

    CHECK(
        toggle.handle_key_down(
            false,
            true,
            true) ==
        CommandConsoleToggleAction::Close);
    CHECK(
        toggle.handle_key_up(true) ==
        CommandConsoleToggleAction::None);
}

TEST_CASE("command console toggle handles repeats focus loss and modal changes") {
    CommandConsoleToggle toggle {};

    CHECK(
        toggle.handle_key_down(
            false,
            false,
            true) ==
        CommandConsoleToggleAction::None);
    CHECK(
        toggle.handle_key_down(
            true,
            false,
            true) ==
        CommandConsoleToggleAction::None);
    toggle.cancel();
    CHECK(
        toggle.handle_key_up(true) ==
        CommandConsoleToggleAction::None);

    CHECK(
        toggle.handle_key_down(
            false,
            false,
            true) ==
        CommandConsoleToggleAction::None);
    CHECK(
        toggle.handle_key_up(false) ==
        CommandConsoleToggleAction::None);
}

TEST_CASE("command console preserves a draft while browsing bounded history") {
    CommandConsole console {};
    console.open();
    console.insert_text("/meteo tempete");
    CHECK(console.submit().status == CommandConsoleParseStatus::Ready);
    console.insert_text("/inconnue");
    CHECK(console.submit().status == CommandConsoleParseStatus::UnknownCommand);

    console.insert_text("brouillon");
    console.show_previous_history();
    CHECK(console.view().input == "/inconnue");
    console.show_previous_history();
    CHECK(console.view().input == "/meteo tempete");
    console.show_next_history();
    CHECK(console.view().input == "/inconnue");
    console.show_next_history();
    CHECK(console.view().input == "brouillon");
}

TEST_CASE("filtered text keeps history navigation and its draft intact") {
    CommandConsole console {};
    console.open();
    console.insert_text("/meteo tempete");
    CHECK(console.submit().status ==
          CommandConsoleParseStatus::Ready);
    console.insert_text("brouillon");
    console.show_previous_history();
    REQUIRE(console.view().input == "/meteo tempete");

    console.insert_text("`\n");
    CHECK(console.view().input == "/meteo tempete");
    console.show_next_history();
    CHECK(console.view().input == "brouillon");
}

TEST_CASE("command console evicts only the oldest entries beyond its history limit") {
    CommandConsole console {};
    console.open();
    for (std::size_t index = 0U;
         index < kCommandConsoleMaxHistoryEntries + 5U;
         ++index) {
        console.insert_text(
            "/commande" +
            std::to_string(index));
        CHECK(console.submit().status ==
              CommandConsoleParseStatus::UnknownCommand);
    }

    for (std::size_t index = 0U;
         index < kCommandConsoleMaxHistoryEntries + 4U;
         ++index) {
        console.show_previous_history();
    }
    CHECK(console.view().input == "/commande5");
}

TEST_CASE("command console exposes feedback without owning renderer state") {
    CommandConsole console {};
    console.open();
    console.set_feedback("TEMPETE LANCEE", false);

    const auto view = console.view();
    CHECK(view.visible);
    CHECK(view.feedback == "TEMPETE LANCEE");
    CHECK_FALSE(view.feedback_is_error);

    console.close();
    CHECK_FALSE(console.view().visible);
}

TEST_CASE("command console layout remains inside small and large viewports") {
    for (const auto& [width, height] :
         {std::pair {1, 1},
          std::pair {16, 32},
          std::pair {32, 32},
          std::pair {640, 360},
          std::pair {1600, 900},
          std::pair {3840, 2160}}) {
        const auto layout =
            build_command_console_layout(
                width,
                height);
        CAPTURE(width);
        CAPTURE(height);
        CHECK(layout.panel_x >= 0.0F);
        CHECK(layout.panel_y >= 0.0F);
        CHECK(layout.panel_width > 0.0F);
        CHECK(layout.panel_height > 0.0F);
        CHECK(layout.panel_x + layout.panel_width <=
              static_cast<float>(width));
        CHECK(layout.panel_y + layout.panel_height <=
              static_cast<float>(height));
        CHECK(layout.input_x >= layout.panel_x);
        CHECK(layout.input_y >= layout.panel_y);
        CHECK(layout.input_x + layout.input_width <=
              layout.panel_x + layout.panel_width);
        CHECK(layout.input_y + layout.input_height <=
              layout.panel_y + layout.panel_height);
    }
}

TEST_CASE("command console keeps a bounded text window around its cursor") {
    const std::string input =
        "/meteo tempete abcdefghijklmnopqrstuvwxyz";

    const auto beginning =
        build_command_console_text_window(
            input,
            0U,
            12U);
    CHECK(beginning.start == 0U);
    CHECK(beginning.length == 12U);
    CHECK(beginning.cursor_offset == 0U);

    const auto middle =
        build_command_console_text_window(
            input,
            20U,
            12U);
    CHECK(middle.start == 8U);
    CHECK(middle.length == 12U);
    CHECK(middle.cursor_offset == 12U);

    const auto ending =
        build_command_console_text_window(
            input,
            input.size(),
            12U);
    CHECK(ending.start + ending.length == input.size());
    CHECK(ending.cursor_offset == ending.length);

    const auto cursor_beyond_input =
        build_command_console_text_window(
            input,
            input.size() + 50U,
            12U);
    CHECK(
        cursor_beyond_input.start +
            cursor_beyond_input.length ==
        input.size());
    CHECK(
        cursor_beyond_input.cursor_offset ==
        cursor_beyond_input.length);

    const auto no_capacity =
        build_command_console_text_window(
            input,
            7U,
            0U);
    CHECK(no_capacity.start == 7U);
    CHECK(no_capacity.length == 0U);
    CHECK(no_capacity.cursor_offset == 0U);
}

TEST_CASE("full recalled input keeps history navigation and its draft intact") {
    CommandConsole console {};
    console.open();
    console.insert_text(
        std::string(
            kCommandConsoleMaxInputBytes,
            'x'));
    REQUIRE(
        console.submit().status ==
        CommandConsoleParseStatus::UnknownCommand);

    console.insert_text("brouillon");
    console.show_previous_history();
    REQUIRE(
        console.view().input.size() ==
        kCommandConsoleMaxInputBytes);
    console.insert_text("y");
    CHECK(
        console.view().input.size() ==
        kCommandConsoleMaxInputBytes);
    console.show_next_history();
    CHECK(console.view().input == "brouillon");
}

} // namespace valcraft
