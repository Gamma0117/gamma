#pragma once

#include "client/player_control.h"
#include "core/constants.h"
#include "core/tick_scheduler.h"
#include "platform/input_state.h"

#include "../entity/entity_test_support.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace aurora::test {

// What PlayerControl would send to the server, in order.
struct RecordingSink final : client::PlayerMessageSink {
    std::vector<entity::PlayerInput> sent;
    std::vector<std::uint32_t> neutralized;

    void sendInput(const entity::PlayerInput& input) override { sent.push_back(input); }
    void sendNeutralize(std::uint32_t through) override { neutralized.push_back(through); }
};

// The palette size the loop's PlayerControl is told (as the shipped interaction.json).
inline constexpr std::size_t kLoopPaletteSize = 6;

// The app's player frame on a real InputState: poll (the events), then the production PlayerControl does the rest
// in its own order (input(), the frame's client ticks in update(), and the F3 switch after them). Time moves 50 ms
// per client tick, so a frame runs exactly `ticks` ticks. The left button's held state is `leftHeld` (the window
// reads it from GLFW, not from events); number keys become slots as the app maps them.
struct PlayerLoop {
    TestBlocks blocks;
    entity::CollisionWorld world = blocks.world();
    platform::InputState input;
    RecordingSink sink;
    client::PlayerControl control{std::make_shared<const data::PlayerMovementTuning>(standardTuning()), world, sink,
                                  kLoopPaletteSize};
    client::MovementKeys held;
    bool leftHeld = false;
    float yaw = 90.0f;
    float pitch = 0.0f;
    core::TickScheduler::TimePoint now = core::TickScheduler::TimePoint{} + std::chrono::seconds(1000);
    bool uiWantsMouse = false;
    bool uiWantsKeyboard = false;
    bool enabled = true;

    PlayerLoop()
    {
        // The spawn starts the client clock; its first tick runs at once (not accepting yet: a neutral input).
        control.update(entity::PlayerState{.serverTick = 1, .motion = standingAt(0.5, 64.0, 0.5)}, now, held, yaw,
                       pitch);
    }

    const client::CursorController& cursor() const { return control.cursor(); }

    std::optional<std::uint8_t> pressedSlot() const
    {
        for (std::uint8_t slot = 0; slot < 9; ++slot) {
            if (input.wasKeyPressed(static_cast<platform::Key>(static_cast<int>(platform::Key::Digit1) + slot))) {
                return slot;
            }
        }
        return std::nullopt;
    }

    void frame(const std::function<void()>& duringPoll, int ticks = 1, bool minimised = false,
               bool switchFreeFlight = false)
    {
        input.startFrame();
        duringPoll();
        const client::PlayerFrameInput events{.focusLost = input.takeFocusLost(),
                                              .minimised = minimised,
                                              .focused = input.isFocused(),
                                              .escapePressed = input.wasKeyPressed(platform::Key::Escape),
                                              .clickPressed = input.wasButtonPressed(platform::MouseButton::Left),
                                              .attackDown = leftHeld,
                                              .usePressed = input.wasButtonPressed(platform::MouseButton::Right),
                                              .slotPressed = pressedSlot(),
                                              .uiWantsMouse = uiWantsMouse,
                                              .uiWantsKeyboard = uiWantsKeyboard,
                                              .jumpPressed = input.wasKeyPressed(platform::Key::Space),
                                              .enabled = enabled};
        if (!control.input(events)) {
            return;
        }
        now += core::kTickInterval * ticks;
        control.update(std::nullopt, now, held, yaw, pitch);
        if (switchFreeFlight) {
            control.setFreeFlight(!control.freeFlight());
        }
    }

    // Captured and taking input, with one input sent. The capturing click is released again.
    void capture()
    {
        frame([&] { input.onButtonPressed(platform::MouseButton::Left); });
        frame([] {});
    }
};

} // namespace aurora::test
