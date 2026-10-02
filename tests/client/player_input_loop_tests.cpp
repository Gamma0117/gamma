#include "client/player_control.h"
#include "platform/input_state.h"

#include "../entity/entity_test_support.h"

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
using aurora::client::PlayerFrameInput;
using aurora::entity::PlayerInput;
using aurora::platform::InputState;
using aurora::platform::Key;
using aurora::platform::MouseButton;

namespace {

// What PlayerControl would send to the server, in order.
struct RecordingSink final : aurora::client::PlayerMessageSink {
    std::vector<PlayerInput> sent;
    std::vector<std::uint32_t> neutralized;

    void sendInput(const PlayerInput& input) override { sent.push_back(input); }
    void sendNeutralize(std::uint32_t through) override { neutralized.push_back(through); }
};

// The app's player frame on a real InputState: poll (the events), then the production PlayerControl does the rest in
// its own order (input(), the frame's client ticks in update(), and the F3 switch after them). Time moves 50 ms per
// client tick, so a frame runs exactly `ticks` ticks.
struct PlayerLoop {
    aurora::test::TestBlocks blocks;
    aurora::entity::CollisionWorld world = blocks.world();
    InputState input;
    RecordingSink sink;
    PlayerControl control{std::make_shared<const aurora::data::PlayerMovementTuning>(aurora::test::standardTuning()),
                          world, sink};
    MovementKeys held;
    aurora::core::TickScheduler::TimePoint now = aurora::core::TickScheduler::TimePoint{} + 1000s;
    bool uiWantsMouse = false;
    bool uiWantsKeyboard = false;
    bool enabled = true;

    PlayerLoop()
    {
        // The spawn starts the client clock; its first tick runs at once (not accepting yet: a neutral input).
        control.update(aurora::entity::PlayerState{.serverTick = 1, .motion = aurora::test::standingAt(0.5, 64.0, 0.5)},
                       now, held, 90.0f, 0.0f);
    }

    const aurora::client::CursorController& cursor() const { return control.cursor(); }

    void frame(const std::function<void()>& duringPoll, int ticks = 1, bool minimised = false,
               bool switchFreeFlight = false)
    {
        input.startFrame();
        duringPoll();
        const PlayerFrameInput events{.focusLost = input.takeFocusLost(),
                                      .minimised = minimised,
                                      .focused = input.isFocused(),
                                      .escapePressed = input.wasKeyPressed(Key::Escape),
                                      .clickPressed = input.wasButtonPressed(MouseButton::Left),
                                      .uiWantsMouse = uiWantsMouse,
                                      .uiWantsKeyboard = uiWantsKeyboard,
                                      .jumpPressed = input.wasKeyPressed(Key::Space),
                                      .enabled = enabled};
        if (!control.input(events)) {
            return;
        }
        now += aurora::core::kTickInterval * ticks;
        control.update(std::nullopt, now, held, 90.0f, 0.0f);
        if (switchFreeFlight) {
            control.setFreeFlight(!control.freeFlight());
        }
    }

    // Captured and taking input, with one input sent.
    void capture()
    {
        frame([&] { input.onButtonPressed(MouseButton::Left); });
        frame([] {});
    }
};

} // namespace

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
