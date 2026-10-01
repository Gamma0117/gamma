#include "client/cursor_controller.h"
#include "platform/input_state.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>

using aurora::client::CursorController;
using aurora::platform::InputState;
using aurora::platform::Key;
using aurora::platform::MouseButton;

TEST_CASE("Presses and cursor movement last one frame", "[platform][input]")
{
    InputState input;
    input.startFrame();
    input.onKeyPressed(Key::Escape);
    input.onButtonPressed(MouseButton::Left);
    input.onCursorMoved(10.0, 20.0);
    CHECK(input.wasKeyPressed(Key::Escape));
    CHECK_FALSE(input.wasKeyPressed(Key::F3));
    CHECK(input.wasButtonPressed(MouseButton::Left));
    CHECK(input.cursorMoved());

    input.startFrame();
    CHECK_FALSE(input.wasKeyPressed(Key::Escape));
    CHECK_FALSE(input.wasButtonPressed(MouseButton::Left));
    CHECK_FALSE(input.cursorMoved());
    CHECK(input.cursorX() == 10.0); // The position stays.
    CHECK(input.cursorY() == 20.0);
}

TEST_CASE("A focus loss waits until it is taken", "[platform][input]")
{
    InputState input;
    CHECK_FALSE(input.takeFocusLost());

    // The frame that receives it is skipped (minimised), then the window waits and polls again.
    input.startFrame();
    input.onFocus(false);
    input.startFrame(); // waitEvents()
    input.startFrame(); // pollEvents()
    CHECK_FALSE(input.isFocused());
    CHECK(input.takeFocusLost());
    CHECK_FALSE(input.takeFocusLost()); // Once.

    // Lost and regained before anyone looked: the loss still counts.
    input.onFocus(false);
    input.onFocus(true);
    CHECK(input.isFocused());
    CHECK(input.takeFocusLost());

    input.onFocus(true); // Gaining focus alone is nothing to report.
    CHECK_FALSE(input.takeFocusLost());
}

TEST_CASE("Losing focus while minimised releases the mouse after restoring", "[platform][input]")
{
    // The main loop's order: poll, take a focus loss, and only then skip the rest of the frame (waiting) if the
    // window is minimised. `duringPoll` and `duringWait` deliver the events GLFW would hand over in each call.
    InputState input;
    CursorController cursor;
    const auto frame = [&](const std::function<void()>& duringPoll, bool minimised,
                           const std::function<void()>& duringWait) {
        input.startFrame(); // pollEvents()
        duringPoll();
        if (input.takeFocusLost()) {
            cursor.onFocusChanged(false);
        }
        if (minimised) {
            input.startFrame(); // waitEvents()
            duringWait();
            return; // The rest of the frame is skipped.
        }
        if (input.wasButtonPressed(MouseButton::Left)) {
            cursor.onClick(false);
        }
    };
    const auto nothing = [] {};

    frame([&] { input.onButtonPressed(MouseButton::Left); }, false, nothing);
    REQUIRE(cursor.captured());

    SECTION("Focus lost in the poll that sees the window minimised")
    {
        frame([&] { input.onFocus(false); }, true, nothing);
    }
    SECTION("Focus lost while already waiting minimised")
    {
        frame(nothing, true, [&] { input.onFocus(false); });
    }
    frame([&] { input.onFocus(true); }, false, nothing); // Restored, focused again.
    CHECK_FALSE(cursor.captured());
}
