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

namespace {

// The main loop's input order on an InputState and a CursorController: poll, take a focus loss, and only then
// skip the rest of the frame (waiting) if the window is minimised, or else handle clicks. `duringPoll` and
// `duringWait` deliver the events GLFW would hand over in each call, in order.
struct LoopModel {
    InputState input;
    CursorController cursor;

    void frame(const std::function<void()>& duringPoll, bool minimised = false,
               const std::function<void()>& duringWait = [] {})
    {
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
    }

    void click() { input.onButtonPressed(MouseButton::Left); }
    void loseFocus() { input.onFocus(false); }
    void gainFocus() { input.onFocus(true); }
};

} // namespace

TEST_CASE("Losing focus while minimised releases the mouse after restoring", "[platform][input]")
{
    LoopModel loop;
    loop.frame([&] { loop.click(); });
    REQUIRE(loop.cursor.captured());

    SECTION("Focus lost in the poll that sees the window minimised")
    {
        loop.frame([&] { loop.loseFocus(); }, true);
    }
    SECTION("Focus lost while already waiting minimised")
    {
        loop.frame([] {}, true, [&] { loop.loseFocus(); });
    }
    loop.frame([&] { loop.gainFocus(); }); // Restored, focused again.
    CHECK_FALSE(loop.cursor.captured());
}

TEST_CASE("A click before a focus loss in the same poll never captures the mouse", "[platform][input]")
{
    LoopModel loop;
    SECTION("Click then focus lost")
    {
        loop.frame([&] {
            loop.click();
            loop.loseFocus();
        });
        CHECK_FALSE(loop.cursor.captured());
        CHECK_FALSE(loop.input.isFocused());
    }
    SECTION("Click then focus lost and regained: the regain does not undo the loss")
    {
        loop.frame([&] {
            loop.click();
            loop.loseFocus();
            loop.gainFocus();
        });
        CHECK_FALSE(loop.cursor.captured());
        CHECK(loop.input.isFocused());
    }
    SECTION("A click while unfocused does not count")
    {
        loop.frame([&] { loop.loseFocus(); });
        loop.frame([&] { loop.click(); });
        CHECK_FALSE(loop.cursor.captured());
    }
    SECTION("Focus lost and regained and then a new click: captured")
    {
        loop.frame([&] {
            loop.loseFocus();
            loop.gainFocus();
            loop.click();
        });
        CHECK(loop.cursor.captured());
    }
    SECTION("A click with focus: captured (control)")
    {
        loop.frame([&] { loop.click(); });
        CHECK(loop.cursor.captured());
    }

    // Whatever happened, a later click with focus captures.
    loop.frame([&] {
        loop.gainFocus();
        loop.click();
    });
    CHECK(loop.cursor.captured());
}
