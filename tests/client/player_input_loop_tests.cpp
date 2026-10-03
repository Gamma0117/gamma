#include "player_loop_support.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

using namespace std::chrono_literals;
using aurora::client::MovementKeys;
using aurora::client::PlayerControl;
using aurora::test::PlayerLoop;
using aurora::entity::PlayerInput;
using aurora::platform::InputState;
using aurora::platform::Key;
using aurora::platform::MouseButton;

TEST_CASE("Space then Esc then a click in one poll never jumps and neutralises", "[client][input][loop]")
{
    PlayerLoop loop;
    loop.capture();
    REQUIRE(loop.cursor().captured());
    REQUIRE_FALSE(loop.sink.sent.empty());
    const std::uint32_t lastSent = loop.sink.sent.back().sequence;

    loop.frame([&] {
        loop.input.onKeyPressed(Key::Space); // A tap: not held at the tick.
        loop.input.onKeyPressed(Key::Escape);
        loop.input.onButtonPressed(MouseButton::Left);
    });
    CHECK(loop.cursor().captured()); // Captured again in the end...
    CHECK_FALSE(loop.sink.sent.back().intent.jump); // ...but the tap is gone...
    CHECK(loop.sink.neutralized == std::vector<std::uint32_t>{lastSent}); // ...and the server was told.
    loop.frame([] {});
    CHECK_FALSE(loop.sink.sent.back().intent.jump);

    // Control: a tap in an undisturbed frame jumps.
    loop.frame([&] { loop.input.onKeyPressed(Key::Space); });
    CHECK(loop.sink.sent.back().intent.jump);
}

TEST_CASE("A tap kept from an earlier frame is dropped by Esc and a click", "[client][input][loop]")
{
    PlayerLoop loop;
    loop.capture();
    loop.frame([&] { loop.input.onKeyPressed(Key::Space); }, 0); // No tick this frame: the tap waits.
    loop.frame([&] {
        loop.input.onKeyPressed(Key::Escape);
        loop.input.onButtonPressed(MouseButton::Left);
    });
    CHECK(loop.cursor().captured());
    CHECK_FALSE(loop.sink.sent.back().intent.jump);
}

TEST_CASE("Losing focus with W held and minimising neutralises in that frame", "[client][input][loop]")
{
    PlayerLoop loop;
    loop.capture();
    loop.held.forward = true;
    loop.frame([] {});
    REQUIRE(loop.sink.sent.back().intent.forward == 1);
    const std::size_t sentBefore = loop.sink.sent.size();
    const std::uint32_t lastSent = loop.sink.sent.back().sequence;

    loop.frame([&] { loop.input.onFocus(false); }, 1, true); // No tick runs in a minimised frame.
    CHECK(loop.sink.neutralized == std::vector<std::uint32_t>{lastSent});
    CHECK(loop.sink.sent.size() == sentBefore);
    loop.frame([] {}, 1, true);
    CHECK(loop.sink.neutralized.size() == 1); // Nothing new to send.

    // Restored and focused, but not captured: neutral inputs, W still held.
    loop.frame([&] { loop.input.onFocus(true); });
    CHECK(loop.sink.sent.back().intent.forward == 0);
    CHECK_FALSE(loop.cursor().captured());
}

TEST_CASE("Switching free flight on neutralises in the same frame", "[client][input][loop]")
{
    PlayerLoop loop;
    loop.capture();
    loop.held.forward = true;
    loop.frame([] {});
    const std::uint32_t lastSent = loop.sink.sent.back().sequence;
    loop.frame([] {}, 1, false, true);
    // The frame's tick still walked (it ran before the panel), and the panel's switch neutralised it at once.
    CHECK(loop.sink.neutralized == std::vector<std::uint32_t>{lastSent + 1});
    loop.frame([] {});
    CHECK(loop.sink.sent.back().intent.forward == 0);
    CHECK(loop.sink.neutralized.size() == 1);
}

TEST_CASE("The UI keeps clicks and keys from the player", "[client][input][loop]")
{
    PlayerLoop loop;
    loop.uiWantsMouse = true; // A click on the F3 panel.
    loop.frame([&] { loop.input.onButtonPressed(MouseButton::Left); });
    CHECK_FALSE(loop.cursor().captured());

    loop.uiWantsMouse = false;
    loop.capture();
    loop.held.forward = true;
    loop.frame([] {});
    REQUIRE(loop.sink.sent.back().intent.forward == 1);
    const std::uint32_t lastSent = loop.sink.sent.back().sequence;
    loop.uiWantsKeyboard = true; // Typing into the UI stops the player at once.
    loop.frame([] {});
    CHECK(loop.sink.neutralized == std::vector<std::uint32_t>{lastSent});
    CHECK(loop.sink.sent.back().intent.forward == 0);
    CHECK_FALSE(loop.control.accepting());
}

TEST_CASE("With input disabled (screenshot mode) clicks and keys never reach the player", "[client][input][loop]")
{
    PlayerLoop loop;
    loop.enabled = false;
    loop.held.forward = true;
    for (int i = 0; i < 3; ++i) {
        loop.frame([&] {
            loop.input.onButtonPressed(MouseButton::Left);
            loop.input.onKeyPressed(Key::Space);
        });
    }
    CHECK_FALSE(loop.cursor().captured());
    for (const PlayerInput& input : loop.sink.sent) {
        CHECK(input.intent == aurora::entity::MovementIntent{.yaw = 90.0f});
    }
}

TEST_CASE("The eyes are drawn between the last two client ticks", "[client][input][loop]")
{
    PlayerLoop loop;
    loop.capture();
    loop.held.forward = true;
    loop.frame([] {});
    loop.frame([] {});
    const aurora::client::LocalPlayer& player = loop.control.localPlayer();
    REQUIRE(player.previous().position.x < player.current().position.x);
    // The tick due at `now` just ran: the drawing starts at the previous prediction and reaches the current one when
    // the next tick is due.
    CHECK(loop.control.eyePosition(loop.now) == player.previous().position + glm::dvec3(0.0, 1.62, 0.0));
    CHECK(loop.control.eyePosition(loop.now + aurora::core::kTickInterval) ==
          player.current().position + glm::dvec3(0.0, 1.62, 0.0));
    CHECK(loop.control.tickProgress(loop.now + aurora::core::kTickInterval / 2) == 0.5);
}

TEST_CASE("A key pressed before a focus loss is void after it comes back", "[platform][input]")
{
    InputState input;
    input.startFrame();
    input.onKeyPressed(Key::Space);
    input.onFocus(false);
    input.onFocus(true);
    input.onButtonPressed(MouseButton::Left);
    CHECK(input.takeFocusLost());
    CHECK_FALSE(input.wasKeyPressed(Key::Space));
    CHECK(input.wasButtonPressed(MouseButton::Left));

    input.onKeyPressed(Key::Space); // A new press with focus counts.
    CHECK(input.wasKeyPressed(Key::Space));

    input.startFrame();
    input.onFocus(false);
    input.onKeyPressed(Key::W); // Unfocused: not recorded.
    CHECK_FALSE(input.wasKeyPressed(Key::W));
}
