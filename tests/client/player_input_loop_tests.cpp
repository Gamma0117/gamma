#include "client/cursor_controller.h"
#include "client/local_player.h"
#include "client/movement_sampler.h"
#include "platform/input_state.h"

#include "../entity/entity_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

using aurora::client::CursorController;
using aurora::client::LocalPlayer;
using aurora::client::MovementKeys;
using aurora::client::MovementSampler;
using aurora::entity::PlayerInput;
using aurora::platform::InputState;
using aurora::platform::Key;
using aurora::platform::MouseButton;

namespace {

// The main loop's player input order on the real InputState, CursorController, MovementSampler and LocalPlayer:
// poll; a focus loss releases the mouse and blocks; a minimised window blocks and skips the rest; Esc and clicks;
// a release blocks; the allowance at the end of input handling (blocking if it was allowed until now); endFrame;
// the frame's ticks; then the F3 panel, whose free-flight switch blocks at once. What would go to the server is
// recorded.
struct PlayerLoop {
    aurora::test::TestBlocks blocks;
    InputState input;
    CursorController cursor;
    MovementSampler sampler;
    LocalPlayer player{std::make_shared<const aurora::data::PlayerMovementTuning>(aurora::test::standardTuning())};
    MovementKeys held;
    bool freeFlight = false;
    std::vector<PlayerInput> sent;
    std::vector<std::uint32_t> neutralized;

    PlayerLoop()
    {
        player.receive({.serverTick = 1, .motion = aurora::test::standingAt(0.5, 64.0, 0.5)}, blocks.world());
    }

    void block()
    {
        if (const std::optional<std::uint32_t> through =
                aurora::client::blockPlayerInput(sampler, player, blocks.world())) {
            neutralized.push_back(*through);
        }
    }

    void frame(const std::function<void()>& duringPoll, int ticks = 1, bool minimised = false,
               bool switchFreeFlight = false)
    {
        input.startFrame();
        duringPoll();
        if (input.takeFocusLost()) {
            cursor.onFocusChanged(false);
            block();
        }
        if (minimised) {
            block();
            return;
        }
        if (input.wasKeyPressed(Key::Escape)) {
            cursor.onEscape();
        }
        if (input.wasButtonPressed(MouseButton::Left)) {
            cursor.onClick(false);
        }
        if (cursor.takeReleased()) {
            block();
        }
        const bool accepting = input.isFocused() && cursor.acceptsMovement(false) && !freeFlight;
        if (!accepting && sampler.accepting()) {
            block();
        }
        sampler.endFrame(accepting, input.wasKeyPressed(Key::Space));
        for (int i = 0; i < ticks; ++i) {
            const aurora::entity::MovementIntent intent = sampler.sample(held, 90.0f, 0.0f);
            if (const std::optional<PlayerInput> made = player.tick(intent, blocks.world())) {
                sent.push_back(*made);
            }
        }
        if (switchFreeFlight) {
            freeFlight = !freeFlight;
            if (freeFlight) {
                block(); // At once, not next frame.
            }
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
    REQUIRE(loop.cursor.captured());
    REQUIRE_FALSE(loop.sent.empty());
    const std::uint32_t lastSent = loop.sent.back().sequence;

    loop.frame([&] {
        loop.input.onKeyPressed(Key::Space); // A tap: not held at the tick.
        loop.input.onKeyPressed(Key::Escape);
        loop.input.onButtonPressed(MouseButton::Left);
    });
    CHECK(loop.cursor.captured()); // Captured again in the end...
    CHECK_FALSE(loop.sent.back().intent.jump); // ...but the tap is gone...
    CHECK(loop.neutralized == std::vector<std::uint32_t>{lastSent}); // ...and the server was told.
    loop.frame([] {});
    CHECK_FALSE(loop.sent.back().intent.jump);

    // Control: a tap in an undisturbed frame jumps.
    loop.frame([&] { loop.input.onKeyPressed(Key::Space); });
    CHECK(loop.sent.back().intent.jump);
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
    CHECK(loop.cursor.captured());
    CHECK_FALSE(loop.sent.back().intent.jump);
}

TEST_CASE("Losing focus with W held and minimising neutralises in that frame", "[client][input][loop]")
{
    PlayerLoop loop;
    loop.capture();
    loop.held.forward = true;
    loop.frame([] {});
    REQUIRE(loop.sent.back().intent.forward == 1);
    const std::size_t sentBefore = loop.sent.size();
    const std::uint32_t lastSent = loop.sent.back().sequence;

    loop.frame([&] { loop.input.onFocus(false); }, 1, true); // No tick runs in a minimised frame.
    CHECK(loop.neutralized == std::vector<std::uint32_t>{lastSent});
    CHECK(loop.sent.size() == sentBefore);
    loop.frame([] {}, 1, true);
    CHECK(loop.neutralized.size() == 1); // Nothing new to send.

    // Restored and focused, but not captured: neutral inputs, W still held.
    loop.frame([&] { loop.input.onFocus(true); });
    CHECK(loop.sent.back().intent.forward == 0);
    CHECK_FALSE(loop.cursor.captured());
}

TEST_CASE("Switching free flight on neutralises in the same frame", "[client][input][loop]")
{
    PlayerLoop loop;
    loop.capture();
    loop.held.forward = true;
    loop.frame([] {});
    const std::uint32_t lastSent = loop.sent.back().sequence;
    loop.frame([] {}, 1, false, true);
    // The frame's tick still walked (it ran before the panel), and the panel's switch neutralised it at once.
    CHECK(loop.neutralized == std::vector<std::uint32_t>{lastSent + 1});
    loop.frame([] {});
    CHECK(loop.sent.back().intent.forward == 0);
    CHECK(loop.neutralized.size() == 1);
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
