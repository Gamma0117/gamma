// Block action input through the real InputState and PlayerControl: which presses become attack, use and slot in
// the inputs sent to the server, and when the crack effect may show (attackActive).

#include "player_loop_support.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

using aurora::entity::PlayerInput;
using aurora::entity::PlayerState;
using aurora::platform::Key;
using aurora::platform::MouseButton;
using aurora::test::PlayerLoop;

namespace {

// Presses the left button (held from then on) in an undisturbed frame.
void pressLeft(PlayerLoop& loop, int ticks = 1)
{
    loop.leftHeld = true;
    loop.frame([&] { loop.input.onButtonPressed(MouseButton::Left); }, ticks);
}

const PlayerInput& last(const PlayerLoop& loop)
{
    return loop.sink.sent.back();
}

} // namespace

TEST_CASE("The click that captures the mouse never attacks", "[client][input][action]")
{
    PlayerLoop loop;
    loop.leftHeld = true;
    loop.frame([&] { loop.input.onButtonPressed(MouseButton::Left); }); // Captures.
    REQUIRE(loop.cursor().captured());
    CHECK_FALSE(last(loop).intent.attack);
    for (int frame = 0; frame < 3; ++frame) {
        loop.frame([] {}); // Still held: not armed.
        CHECK_FALSE(last(loop).intent.attack);
        CHECK_FALSE(loop.control.attackActive());
    }
    // Let go and press again: now it attacks while held.
    loop.leftHeld = false;
    loop.frame([] {});
    pressLeft(loop);
    CHECK(last(loop).intent.attack);
    CHECK(loop.control.attackActive());
    loop.frame([] {}, 3);
    for (std::size_t i = loop.sink.sent.size() - 3; i < loop.sink.sent.size(); ++i) {
        CHECK(loop.sink.sent[i].intent.attack);
    }
}

TEST_CASE("A release in a frame without a tick ends the attack and the cracks at once", "[client][input][action]")
{
    PlayerLoop loop;
    loop.capture();
    pressLeft(loop);
    REQUIRE(last(loop).intent.attack);
    const std::size_t sent = loop.sink.sent.size();

    loop.leftHeld = false;
    loop.frame([] {}, 0); // The release is seen; no tick runs.
    CHECK(loop.sink.sent.size() == sent);
    CHECK_FALSE(loop.control.attackActive()); // The cracks hide now, not at the next state.
    loop.frame([] {});
    CHECK_FALSE(last(loop).intent.attack);

    // Holding it again without a new press does not attack.
    loop.leftHeld = true;
    loop.frame([] {});
    CHECK_FALSE(last(loop).intent.attack);
    CHECK_FALSE(loop.control.attackActive());
}

TEST_CASE("A click shorter than a tick attacks once without lighting the cracks", "[client][input][action]")
{
    PlayerLoop loop;
    loop.capture();
    // Pressed and released within one poll, in a frame with no tick.
    loop.leftHeld = false;
    loop.frame([&] { loop.input.onButtonPressed(MouseButton::Left); }, 0);
    CHECK_FALSE(loop.control.attackActive());
    loop.frame([] {}, 2);
    REQUIRE(loop.sink.sent.size() >= 2);
    CHECK(loop.sink.sent[loop.sink.sent.size() - 2].intent.attack); // The next tick takes the tap...
    CHECK_FALSE(last(loop).intent.attack);                            // ...once.
}

TEST_CASE("Right clicks before a tick become one use with the tick's slot and look", "[client][input][action]")
{
    PlayerLoop loop;
    loop.capture();
    loop.frame([&] { loop.input.onKeyPressed(Key::Digit3); });
    CHECK(loop.control.slot() == 2);

    // Two right clicks in two frames without a tick, a slot change after them, then a tick with a new look.
    loop.frame([&] { loop.input.onButtonPressed(MouseButton::Right); }, 0);
    loop.frame([&] { loop.input.onButtonPressed(MouseButton::Right); }, 0);
    loop.frame([&] { loop.input.onKeyPressed(Key::Digit5); }, 0);
    loop.yaw = 45.0f;
    loop.frame([] {}, 2);
    const PlayerInput& first = loop.sink.sent[loop.sink.sent.size() - 2];
    CHECK(first.intent.use);
    CHECK(first.intent.slot == 4);       // The slot when the tick sampled, not when clicked.
    CHECK(first.intent.yaw == 45.0f);    // The look of the tick too.
    CHECK_FALSE(last(loop).intent.use); // One use, not two.

    // Number keys outside the palette are ignored; with several in one poll the lowest wins.
    loop.frame([&] { loop.input.onKeyPressed(Key::Digit9); });
    CHECK(loop.control.slot() == 4);
    loop.frame([&] {
        loop.input.onKeyPressed(Key::Digit6);
        loop.input.onKeyPressed(Key::Digit2);
    });
    CHECK(loop.control.slot() == 1);
}

TEST_CASE("Presses before Esc and a click in the same poll do nothing", "[client][input][action]")
{
    PlayerLoop loop;
    loop.capture();
    loop.frame([&] { loop.input.onKeyPressed(Key::Digit2); });
    REQUIRE(loop.control.slot() == 1);
    const std::size_t neutralizedBefore = loop.sink.neutralized.size();

    loop.leftHeld = true;
    loop.frame([&] {
        loop.input.onButtonPressed(MouseButton::Left);
        loop.input.onButtonPressed(MouseButton::Right);
        loop.input.onKeyPressed(Key::Digit4);
        loop.input.onKeyPressed(Key::Escape);
        loop.input.onButtonPressed(MouseButton::Left); // Captures again in the same poll.
    });
    CHECK(loop.cursor().captured());
    CHECK(loop.sink.neutralized.size() == neutralizedBefore + 1);
    CHECK_FALSE(last(loop).intent.attack);
    CHECK_FALSE(last(loop).intent.use);
    CHECK(loop.control.slot() == 1);
    CHECK_FALSE(loop.control.attackActive());
    loop.frame([] {});
    CHECK_FALSE(last(loop).intent.attack); // Still held, never armed.
    CHECK_FALSE(last(loop).intent.use);
}

TEST_CASE("Every way input stops disarms the attack and drops a pending use", "[client][input][action]")
{
    const auto check = [](const char* what, const auto& stop) {
        INFO(what);
        PlayerLoop loop;
        loop.capture();
        pressLeft(loop);
        REQUIRE(last(loop).intent.attack);
        loop.frame([&] { loop.input.onButtonPressed(MouseButton::Right); }, 0); // A use waiting for the next tick.
        const std::size_t sent = loop.sink.sent.size();
        stop(loop);
        CHECK_FALSE(loop.control.attackActive());
        for (std::size_t i = sent; i < loop.sink.sent.size(); ++i) {
            CHECK_FALSE(loop.sink.sent[i].intent.attack);
            CHECK_FALSE(loop.sink.sent[i].intent.use);
        }
        CHECK_FALSE(loop.sink.neutralized.empty());
    };
    check("focus lost", [](PlayerLoop& loop) {
        loop.frame([&] { loop.input.onFocus(false); });
        loop.frame([&] { loop.input.onFocus(true); });
    });
    check("minimised", [](PlayerLoop& loop) {
        loop.frame([] {}, 1, true);
        loop.frame([] {});
    });
    check("UI keyboard", [](PlayerLoop& loop) {
        loop.uiWantsKeyboard = true;
        loop.frame([] {});
        loop.uiWantsKeyboard = false;
        loop.frame([] {});
    });
    check("free flight", [](PlayerLoop& loop) {
        loop.frame([] {}, 0, false, true);
        loop.frame([] {});
        loop.frame([] {}, 0, false, true);
        loop.frame([] {});
    });
    check("input disabled", [](PlayerLoop& loop) {
        loop.enabled = false;
        loop.frame([] {});
        loop.enabled = true;
        loop.frame([] {});
    });
    check("Esc", [](PlayerLoop& loop) {
        loop.frame([&] { loop.input.onKeyPressed(Key::Escape); });
        loop.frame([&] { loop.input.onButtonPressed(MouseButton::Left); }); // Captures; the button is still held.
        loop.frame([] {});
    });
}

TEST_CASE("Presses consumed while no input is made are gone", "[client][input][action]")
{
    PlayerLoop loop;
    loop.capture();
    // No server state arrives, so the history fills and the client stops making inputs.
    for (int frame = 0; frame < 50; ++frame) {
        loop.frame([] {});
    }
    const std::size_t sent = loop.sink.sent.size();
    REQUIRE(loop.control.localPlayer().stats().paused);
    loop.frame([&] {
        loop.input.onButtonPressed(MouseButton::Right);
        loop.input.onButtonPressed(MouseButton::Left); // A tap (not held).
    });
    CHECK(loop.sink.sent.size() == sent); // Sampled, but no input made.

    // The server settles everything; the next input carries neither the use nor the tap.
    loop.control.update(PlayerState{.serverTick = 2,
                                    .lastInput = loop.sink.sent.back().sequence,
                                    .motion = aurora::test::standingAt(0.5, 64.0, 0.5)},
                        loop.now, loop.held, loop.yaw, loop.pitch);
    loop.frame([] {});
    REQUIRE(loop.sink.sent.size() > sent);
    CHECK_FALSE(last(loop).intent.use);
    CHECK_FALSE(last(loop).intent.attack);
}
