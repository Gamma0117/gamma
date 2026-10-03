#include "client/player_control.h"
#include "core/constants.h"
#include "core/tick_scheduler.h"
#include "server/server_player.h"

#include "../entity/entity_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using aurora::client::MovementKeys;
using aurora::client::PlayerControl;
using aurora::client::PlayerFrameInput;
using aurora::core::TickScheduler;
using aurora::entity::MovementIntent;
using aurora::entity::PlayerMotion;
using aurora::entity::PlayerState;
using aurora::server::PlayerMessage;
using aurora::server::ServerPlayer;
using aurora::test::sameBits;

namespace {

using Micros = std::chrono::microseconds;

// Frame lengths: fixed, a saved list (its last length repeats), or uniform in [min, max] microseconds from a seeded
// splitmix64 (the same numbers on every platform and standard library, unlike std:: distributions).
struct Timeline {
    std::string name;
    std::int64_t minMicros = 0;
    std::int64_t maxMicros = 0;
    std::uint64_t seed = 0;
    std::vector<std::int64_t> saved; // Used when not empty.
};

Timeline fixedFps(int fps)
{
    const std::int64_t micros = 1'000'000 / fps;
    return {std::format("{} fps", fps), micros, micros, 0, {}};
}

Timeline jitter(std::int64_t minMs, std::int64_t maxMs, std::uint64_t seed)
{
    return {std::format("{}..{} ms frames, seed {}", minMs, maxMs, seed), minMs * 1000, maxMs * 1000, seed, {}};
}

class FrameLengths {
public:
    explicit FrameLengths(const Timeline& timeline)
        : m_timeline(timeline)
        , m_state(timeline.seed)
    {
    }

    Micros next()
    {
        if (!m_timeline.saved.empty()) {
            return Micros(m_timeline.saved[std::min(m_index++, m_timeline.saved.size() - 1)]);
        }
        const auto span = static_cast<std::uint64_t>(m_timeline.maxMicros - m_timeline.minMicros + 1);
        return Micros(m_timeline.minMicros + static_cast<std::int64_t>(splitmix64() % span));
    }

private:
    std::uint64_t splitmix64()
    {
        std::uint64_t z = (m_state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    const Timeline& m_timeline;
    std::uint64_t m_state;
    std::size_t m_index = 0;
};

enum class Behaviour {
    Walk,     // W held.
    Sneak,    // W and Shift held.
    JumpTaps, // W held, Space tapped (pressed during one poll) every 700 ms.
    Blocks,   // W held; every second focus is lost for a frame, then a click captures again.
    Mine,     // W held; the left button held twice (0.4..2.4 s and 3.0..3.8 s) and tapped (pressed and released in
              // one poll) at 4.2 s.
    UseTaps,  // W held, the right button clicked every 300 ms.
};

const char* nameOf(Behaviour behaviour)
{
    switch (behaviour) {
    case Behaviour::Walk:
        return "walk";
    case Behaviour::Sneak:
        return "sneak";
    case Behaviour::JumpTaps:
        return "jump taps";
    case Behaviour::Blocks:
        return "blocks";
    case Behaviour::Mine:
        return "mine";
    case Behaviour::UseTaps:
        return "use taps";
    }
    return "?";
}

struct Scenario {
    const Timeline* timeline = nullptr;
    std::chrono::microseconds serverPhase{0};
    bool serverFirst = false; // At equal times, the server tick runs before the frame.
    Behaviour behaviour = Behaviour::Walk;
    std::chrono::milliseconds duration{5000};

    std::string describe() const
    {
        return std::format("{}, server phase {} us, server first {}, {}", timeline->name, serverPhase.count(),
                           serverFirst, nameOf(behaviour));
    }
};

struct Outcome {
    std::uint64_t serverTicks = 0;
    std::vector<std::uint32_t> applied;        // Sequences the server applied, in order.
    // Server ticks that were neutral (starving or filling) after the first input was applied and before the
    // settling at the end: designed, but only when inputs come late.
    std::vector<std::uint64_t> neutralTickNumbers;
    std::uint64_t dropped = 0;
    std::uint64_t corrections = 0; // Before the settling.
    std::uint64_t resyncs = 0;
    std::uint32_t lastSent = 0;
    std::size_t maxPendingAfterTick = 0;
    std::size_t maxHistory = 0;
    std::uint64_t taps = 0;
    std::uint64_t jumpsSent = 0;
    std::uint64_t jumpsApplied = 0;
    std::uint64_t neutralizeMessages = 0;
    std::uint64_t walkingAfterLastBlock = 0; // Applied inputs that walked after the last Neutralize arrived.
    std::uint64_t sneakFrames = 0;           // Frames drawn after the first applied input reached the client...
    std::uint64_t raisedEyeFrames = 0;       // ...of which the eyes were drawn above the sneaking eye height.
    std::uint64_t leftPresses = 0;           // Left presses after the capture (holds and taps).
    std::uint64_t attacksSent = 0;           // Inputs sent with attack...
    std::uint64_t attacksApplied = 0;        // ...and applied with it: the ticks the server could mine.
    std::uint64_t attacksAfterRelease = 0;   // Inputs sent with attack after the last release was seen.
    std::uint64_t useClicks = 0;
    std::uint64_t usesSent = 0;
    std::uint64_t usesApplied = 0;
    bool converged = false;
    std::string failure; // The first broken invariant, with when it happened.
};

// What PlayerControl sends, in order: the client -> server mailbox.
struct Mailbox final : aurora::client::PlayerMessageSink {
    std::vector<PlayerMessage> messages;
    std::vector<aurora::entity::PlayerInput> inputs;

    void sendInput(const aurora::entity::PlayerInput& input) override
    {
        messages.push_back({.kind = PlayerMessage::Kind::Input, .input = input});
        inputs.push_back(input);
    }
    void sendNeutralize(std::uint32_t through) override
    {
        messages.push_back({.kind = PlayerMessage::Kind::Neutralize, .through = through});
    }
};

// The game's two loops as discrete events on one fake timeline, through the production classes: frames (the
// scenario's input events, PlayerControl::input, then PlayerControl::update with the newest server state and the
// client ticks due) and server ticks on their own 20 Hz grid shifted by the phase. Messages reach the server at its
// next tick, in order; the server's newest state reaches the client at its next frame. After the scenario the client
// stops making inputs and the server settles everything sent.
Outcome simulate(const Scenario& scenario)
{
    const aurora::test::TestBlocks blocks;
    const aurora::entity::CollisionWorld world = blocks.world();
    const auto tuning = std::make_shared<const aurora::data::PlayerMovementTuning>(aurora::test::standardTuning());
    ServerPlayer server(tuning);
    Mailbox mailbox;
    PlayerControl control(tuning, world, mailbox);
    FrameLengths frames(*scenario.timeline);

    const TickScheduler::TimePoint start = TickScheduler::TimePoint{} + 1000s;
    const TickScheduler::TimePoint end = start + scenario.duration;
    TickScheduler serverClock(aurora::core::kTickInterval, aurora::core::kMaxCatchUpTicks);
    serverClock.reset(start + scenario.serverPhase);
    TickScheduler::TimePoint nextFrame = start;
    TickScheduler::TimePoint lastFrame = start;
    std::size_t delivered = 0; // Messages already given to the server.
    std::optional<PlayerState> toClient;

    server.spawn({0.5, 64.0, 0.5});
    PlayerMotion shadow = server.motion(); // The server's motion, stepped here once per tick with its intent.
    std::uint32_t lastSettled = 0;
    std::uint32_t neutralizedThrough = 0;
    bool settling = false; // After the scenario: neutral ticks and corrections are expected and not counted.
    Outcome outcome;
    const auto fail = [&](TickScheduler::TimePoint when, const std::string& what) {
        if (outcome.failure.empty()) {
            outcome.failure = std::format("at {} us: {}", std::chrono::duration_cast<Micros>(when - start).count(),
                                          what);
        }
    };

    const auto serverTick = [&](TickScheduler::TimePoint now) {
        for (; delivered < mailbox.messages.size(); ++delivered) {
            const PlayerMessage& message = mailbox.messages[delivered];
            server.receive(message);
            if (message.kind == PlayerMessage::Kind::Neutralize) {
                neutralizedThrough = std::max(neutralizedThrough, message.through);
            }
        }
        server.tick(world);
        ++outcome.serverTicks;
        aurora::entity::stepPlayer(shadow, server.lastTickIntent(), *tuning, world);
        if (!sameBits(shadow, server.motion())) {
            fail(now, "the server's motion is not exactly one step of the intent it reports");
        }
        if (const std::optional<std::uint32_t> input = server.lastTickInput()) {
            if (!outcome.applied.empty() && *input <= outcome.applied.back()) {
                fail(now, std::format("input {} applied after {}", *input, outcome.applied.back()));
            }
            outcome.applied.push_back(*input);
            const MovementIntent& intent = server.lastTickIntent();
            if (*input <= neutralizedThrough && !(intent == intent.neutral())) {
                fail(now, std::format("input {} moved although Neutralize({}) had arrived", *input,
                                      neutralizedThrough));
            }
            outcome.jumpsApplied += intent.jump ? 1 : 0;
            outcome.attacksApplied += intent.attack ? 1 : 0;
            outcome.usesApplied += intent.use ? 1 : 0;
            outcome.walkingAfterLastBlock = intent.forward > 0 ? outcome.walkingAfterLastBlock + 1 : 0;
        } else if (!outcome.applied.empty() && !settling) {
            outcome.neutralTickNumbers.push_back(outcome.serverTicks);
        }
        if (server.lastInput() < lastSettled) {
            fail(now, std::format("the settled run went down from {} to {}", lastSettled, server.lastInput()));
        }
        lastSettled = server.lastInput();
        outcome.maxPendingAfterTick = std::max(outcome.maxPendingAfterTick, server.stats().pendingInputs);
        if (server.stats().pendingInputs > ServerPlayer::kMaxPendingInputs) {
            fail(now, "more inputs waiting than the queue holds");
        }
        toClient = server.state(outcome.serverTicks);
    };

    const MovementKeys keys{.forward = true, .sneak = scenario.behaviour == Behaviour::Sneak};
    const double sneakEye = tuning->sneakEyeHeight;
    TickScheduler::TimePoint nextTap = start + 400ms;
    TickScheduler::TimePoint nextBlock = start + 1000ms;
    TickScheduler::TimePoint nextUse = start + 300ms;
    // Mine: the held spans and the tap, in order.
    const std::array<std::pair<Micros, Micros>, 3> holds{{{400ms, 2400ms}, {3000ms, 3800ms}, {4200ms, 4200ms}}};
    std::size_t hold = 0;
    bool leftHeld = false;
    std::size_t sentBeforeRelease = 0; // Inputs sent when the last release was seen.
    bool regainFocus = false;
    std::size_t frameIndex = 0;
    while (true) {
        const TickScheduler::TimePoint serverTime = serverClock.nextTickTime();
        const bool serverNext = serverTime < nextFrame || (serverTime == nextFrame && scenario.serverFirst);
        const TickScheduler::TimePoint now = serverNext ? serverTime : nextFrame;
        if (now > end) {
            break;
        }
        if (serverNext) {
            const std::uint32_t ticks = serverClock.advance(now).ticksToRun;
            for (std::uint32_t i = 0; i < ticks; ++i) {
                serverTick(now);
            }
            continue;
        }

        // The frame's events. The first frame clicks to capture the mouse; the window is focused all along except
        // in the blocking scenario.
        PlayerFrameInput events{.focused = true, .clickPressed = frameIndex == 0};
        if (scenario.behaviour == Behaviour::JumpTaps && now >= nextTap && now + 500ms < end) {
            events.jumpPressed = true;
            ++outcome.taps;
            nextTap += 700ms;
        }
        if (scenario.behaviour == Behaviour::Mine && hold < holds.size()) {
            const auto [pressAt, releaseAt] = holds[hold];
            if (!leftHeld && now >= start + pressAt) {
                events.clickPressed = true;
                ++outcome.leftPresses;
                leftHeld = pressAt != releaseAt; // A tap goes down and up within the poll.
                if (!leftHeld) {
                    ++hold;
                    sentBeforeRelease = mailbox.inputs.size() + 1; // The tap's tick may still attack once.
                }
            } else if (leftHeld && now >= start + releaseAt) {
                leftHeld = false;
                ++hold;
                sentBeforeRelease = mailbox.inputs.size();
            }
        }
        events.attackDown = leftHeld;
        if (scenario.behaviour == Behaviour::UseTaps && now >= nextUse && now + 500ms < end) {
            events.usePressed = true;
            ++outcome.useClicks;
            nextUse += 300ms;
        }
        if (scenario.behaviour == Behaviour::Blocks) {
            if (regainFocus) {
                events.clickPressed = true; // Focus came back: click into the window again.
                regainFocus = false;
            } else if (now >= nextBlock && now + 500ms < end) {
                events.focusLost = true;
                events.focused = false;
                regainFocus = true;
                nextBlock += 1000ms;
            }
        }
        control.input(events);
        control.update(std::exchange(toClient, std::nullopt), now, keys, 90.0f, 0.0f);

        const aurora::client::LocalPlayer& player = control.localPlayer();
        outcome.maxHistory = std::max(outcome.maxHistory, player.stats().history);
        if (player.stats().history > aurora::client::LocalPlayer::kMaxHistory) {
            fail(now, "the client history is longer than its limit");
        }
        if (scenario.behaviour == Behaviour::Sneak && player.stats().lastInput >= 1) {
            ++outcome.sneakFrames;
            const double eye = player.eyeHeight(control.tickProgress(now));
            outcome.raisedEyeFrames += eye > sneakEye + 1e-9 ? 1 : 0;
        }
        lastFrame = now;
        nextFrame += frames.next();
        ++frameIndex;
    }

    // No more inputs: the server settles everything sent (then stands with neutral ticks), and the client sees its
    // newest state.
    outcome.corrections = control.localPlayer().stats().corrections;
    settling = true;
    for (int i = 0; i < 10; ++i) {
        serverTick(end);
    }
    control.update(std::exchange(toClient, std::nullopt), lastFrame, keys, 90.0f, 0.0f);

    const aurora::client::LocalPlayerStats stats = control.localPlayer().stats();
    outcome.dropped = server.stats().droppedInputs;
    outcome.resyncs = stats.resyncs;
    outcome.lastSent = stats.lastSent;
    for (std::size_t i = 0; i < mailbox.inputs.size(); ++i) {
        const MovementIntent& intent = mailbox.inputs[i].intent;
        outcome.jumpsSent += intent.jump ? 1 : 0;
        outcome.attacksSent += intent.attack ? 1 : 0;
        outcome.usesSent += intent.use ? 1 : 0;
        if (scenario.behaviour == Behaviour::Mine && hold == holds.size() && i >= sentBeforeRelease) {
            outcome.attacksAfterRelease += intent.attack ? 1 : 0;
        }
    }
    for (const PlayerMessage& message : mailbox.messages) {
        outcome.neutralizeMessages += message.kind == PlayerMessage::Kind::Neutralize ? 1 : 0;
    }
    outcome.converged = stats.lastInput == stats.lastSent && stats.history == 0 &&
                        sameBits(control.localPlayer().current(), server.motion());
    return outcome;
}

// Every run: the server steps exactly once per tick with the intent it reports, applies each input at most once and
// in order, never lowers the settled run, never holds more than its queue; the client history stays within its
// limit; Neutralize wins over the inputs it covers; and once inputs stop, both sides agree bit for bit.
void checkInvariants(const Outcome& outcome)
{
    INFO(outcome.failure);
    CHECK(outcome.failure.empty());
    CHECK(outcome.converged);
    CHECK(outcome.maxPendingAfterTick <= ServerPlayer::kMaxPendingInputs);
    CHECK(outcome.maxHistory <= aurora::client::LocalPlayer::kMaxHistory);
    CHECK(outcome.resyncs == 0);
    // Frames up to 100 ms never bring more inputs at once than the queue holds: nothing is dropped, so after the
    // settling every input sent was applied, 1, 2, 3, ... in order.
    CHECK(outcome.dropped == 0);
    bool consecutive = outcome.applied.size() == outcome.lastSent;
    for (std::size_t i = 0; consecutive && i < outcome.applied.size(); ++i) {
        consecutive = outcome.applied[i] == i + 1;
    }
    CHECK(consecutive);
}

void checkBehaviour(const Scenario& scenario, const Outcome& outcome)
{
    switch (scenario.behaviour) {
    case Behaviour::Walk:
        break;
    case Behaviour::Sneak:
        CHECK(outcome.sneakFrames > 0);
        // The eyes rise only for a pose the server really applied: a neutral tick (inputs came late). Without one,
        // never; each one can show in the frames until the next client tick or state replaces it.
        if (outcome.neutralTickNumbers.empty()) {
            CHECK(outcome.raisedEyeFrames == 0);
        } else {
            CHECK(outcome.raisedEyeFrames <= 4 * outcome.neutralTickNumbers.size());
        }
        break;
    case Behaviour::JumpTaps:
        CHECK(outcome.taps >= 5);
        CHECK(outcome.jumpsSent == outcome.taps); // Every tap jumps once, never twice.
        CHECK(outcome.jumpsApplied == outcome.jumpsSent);
        break;
    case Behaviour::Blocks:
        CHECK(outcome.neutralizeMessages >= 3);
        CHECK(outcome.walkingAfterLastBlock > 0); // Captured again, the player walks again.
        break;
    case Behaviour::Mine:
        CHECK(outcome.leftPresses == 3);
        // 2.8 s held is about 56 ticks of attack, plus one for the tap; a frame of up to 70 ms sees each press and
        // release up to a tick or so late.
        CHECK(outcome.attacksSent >= 52);
        CHECK(outcome.attacksSent <= 60);
        CHECK(outcome.attacksApplied == outcome.attacksSent); // Mining time is the applied attack inputs.
        CHECK(outcome.attacksAfterRelease == 0);
        break;
    case Behaviour::UseTaps:
        CHECK(outcome.useClicks >= 10);
        CHECK(outcome.usesSent == outcome.useClicks); // Every click once (clicks are more than a tick apart)...
        CHECK(outcome.usesApplied == outcome.usesSent); // ...and applied once.
        break;
    }
}

const std::vector<Timeline>& controlledTimelines()
{
    static const std::vector<Timeline> timelines{fixedFps(144), fixedFps(60), fixedFps(30), fixedFps(20),
                                                 fixedFps(15),  fixedFps(10), jitter(5, 45, 20261002),
                                                 jitter(5, 45, 20261003)};
    return timelines;
}

// 30..70 ms frames. Seeds 9 and 57 were picked because their runs do have neutral ticks: with the phases and orders
// below (34 combinations, 10 s each) 10 of 34 for seed 9 and 6 of 34 for seed 57 (in the seed search, 7 ms phase
// steps, it was 4 of 16 each). The other two seeds never have one. Picked cases, not an estimate of how often.
const std::vector<Timeline>& lateFrameTimelines()
{
    static const std::vector<Timeline> timelines{jitter(30, 70, 20261002), jitter(30, 70, 20261003),
                                                 jitter(30, 70, 9), jitter(30, 70, 57)};
    return timelines;
}

// Server phases (0..48 ms in 3 ms steps) and both orders at equal times.
template <typename Body>
void forEachPhase(const Timeline& timeline, Behaviour behaviour, const Body& body)
{
    for (int phaseMs = 0; phaseMs < 50; phaseMs += 3) {
        for (const bool serverFirst : {false, true}) {
            body(Scenario{.timeline = &timeline,
                          .serverPhase = std::chrono::milliseconds(phaseMs),
                          .serverFirst = serverFirst,
                          .behaviour = behaviour});
        }
    }
}

// Controlled frames (fixed rates down to 10 fps, and 5..45 ms jitter, every frame shorter than a tick or a whole
// number of ticks): inputs never come late, so after the first input there is no neutral tick, no correction and the
// sneaking eyes never rise.
void checkControlled(Behaviour behaviour)
{
    for (const Timeline& timeline : controlledTimelines()) {
        forEachPhase(timeline, behaviour, [&](const Scenario& scenario) {
            INFO(scenario.describe());
            const Outcome outcome = simulate(scenario);
            checkInvariants(outcome);
            checkBehaviour(scenario, outcome);
            CHECK(outcome.neutralTickNumbers.empty());
            if (behaviour != Behaviour::Blocks) { // Neutralising what the server already applied corrects, by design.
                CHECK(outcome.corrections == 0);
            }
        });
    }
}

// Frames of 30..70 ms: a long frame can leave the server's queue empty for a tick, which is the designed neutral tick
// (and a correction), never a broken invariant.
void checkLateFrames(Behaviour behaviour)
{
    for (const Timeline& timeline : lateFrameTimelines()) {
        forEachPhase(timeline, behaviour, [&](Scenario scenario) {
            scenario.duration = 10s;
            INFO(scenario.describe());
            const Outcome outcome = simulate(scenario);
            checkInvariants(outcome);
            checkBehaviour(scenario, outcome);
        });
    }
}

} // namespace

TEST_CASE("Walking with controlled frame rates never starves the server or corrects", "[client][player][loop]")
{
    checkControlled(Behaviour::Walk);
}

TEST_CASE("Sneaking with controlled frame rates keeps the eyes low", "[client][player][loop][sneak]")
{
    checkControlled(Behaviour::Sneak);
}

TEST_CASE("Jump taps with controlled frame rates jump once each", "[client][player][loop]")
{
    checkControlled(Behaviour::JumpTaps);
}

TEST_CASE("Input blocking with controlled frame rates neutralises in time", "[client][player][loop]")
{
    checkControlled(Behaviour::Blocks);
}

TEST_CASE("Mining with controlled frame rates attacks exactly while held", "[client][player][loop][action]")
{
    checkControlled(Behaviour::Mine);
}

TEST_CASE("Use taps with controlled frame rates use once each", "[client][player][loop][action]")
{
    checkControlled(Behaviour::UseTaps);
}

TEST_CASE("Late frames keep every invariant", "[client][player][loop][sneak]")
{
    for (const Behaviour behaviour : {Behaviour::Walk, Behaviour::Sneak, Behaviour::JumpTaps, Behaviour::Blocks,
                                      Behaviour::Mine, Behaviour::UseTaps}) {
        checkLateFrames(behaviour);
    }
}

TEST_CASE("A saved timeline with one late frame starves the server for exactly one tick", "[client][player][loop]")
{
    // Server ticks at 0, 50, 100, ... ms; frames at 0, 49, 99.5, 145, 205, then every 50 ms (all within 30..70 ms).
    // The spawn state (tick 1 at 0 ms) reaches the frame at 49 ms, which starts the client clock: client ticks are
    // due at 49, 99, 149, ... ms and sent by the first frame at or after that.
    //   tick 2 (50 ms): input 1 waits (filling)     tick 3 (100 ms): 1 and 2 waiting, 1 applied, 2 kept in reserve
    //   frame 145 ms: nothing due yet               tick 4 (150 ms): 2 applied, nothing left
    //   tick 5 (200 ms): input 3 (due at 149 ms) is still not sent: the queue is empty, a neutral tick
    //   frame 205 ms (a 60 ms frame): inputs 3 and 4 sent together    tick 6 (250 ms): 3 applied, 4 in reserve
    // From then on every frame sends one input before the next server tick: no more neutral ticks. The client never
    // predicted the neutral tick, so the state after it corrects the prediction once.
    const Timeline saved{"saved: 49, 50.5, 45.5, 60, then 50 ms", 0, 0, 0, {49'000, 50'500, 45'500, 60'000, 50'000}};
    for (const Behaviour behaviour : {Behaviour::Walk, Behaviour::Sneak}) {
        const Scenario scenario{.timeline = &saved, .behaviour = behaviour, .duration = 3s};
        INFO(scenario.describe());
        const Outcome outcome = simulate(scenario);
        checkInvariants(outcome);
        checkBehaviour(scenario, outcome);
        CHECK(outcome.neutralTickNumbers == std::vector<std::uint64_t>{5});
        CHECK(outcome.corrections == 1);
        CHECK(outcome.applied.front() == 1);
        if (behaviour == Behaviour::Sneak) {
            // The neutral tick's standing pose came with the state, but the same frame predicted two sneaking ticks
            // over it before anything was drawn.
            CHECK(outcome.raisedEyeFrames == 0);
        }
    }
}

// Not run by default: the numbers behind the checks above, per timeline and behaviour.
TEST_CASE("Loop simulation report", "[.][loop-report]")
{
    std::vector<std::pair<Timeline, std::chrono::milliseconds>> timelines;
    for (const Timeline& timeline : controlledTimelines()) {
        timelines.emplace_back(timeline, 5s);
    }
    for (const Timeline& timeline : lateFrameTimelines()) {
        timelines.emplace_back(timeline, 10s);
    }
    std::printf("%-34s %-9s %5s %9s %8s %8s %8s %8s %7s %6s %7s\n", "timeline", "behaviour", "runs", "neutral>0",
                "maxNeut", "avgCorr", "maxPend", "maxHist", "dropped", "resync", "raised");
    for (const auto& [timeline, duration] : timelines) {
        for (const Behaviour behaviour : {Behaviour::Walk, Behaviour::Sneak, Behaviour::JumpTaps, Behaviour::Blocks,
                                          Behaviour::Mine, Behaviour::UseTaps}) {
            std::uint64_t runs = 0, withNeutral = 0, maxNeutral = 0, corrections = 0, dropped = 0, resyncs = 0,
                          raised = 0;
            std::size_t maxPending = 0, maxHistory = 0;
            forEachPhase(timeline, behaviour, [&](Scenario scenario) {
                scenario.duration = duration;
                const Outcome outcome = simulate(scenario);
                ++runs;
                withNeutral += outcome.neutralTickNumbers.empty() ? 0 : 1;
                maxNeutral = std::max<std::uint64_t>(maxNeutral, outcome.neutralTickNumbers.size());
                corrections += outcome.corrections;
                dropped += outcome.dropped;
                resyncs += outcome.resyncs;
                raised += outcome.raisedEyeFrames;
                maxPending = std::max(maxPending, outcome.maxPendingAfterTick);
                maxHistory = std::max(maxHistory, outcome.maxHistory);
            });
            std::printf("%-34s %-9s %5llu %9llu %8llu %8.2f %8zu %8zu %7llu %6llu %7llu\n", timeline.name.c_str(),
                        nameOf(behaviour), static_cast<unsigned long long>(runs),
                        static_cast<unsigned long long>(withNeutral), static_cast<unsigned long long>(maxNeutral),
                        static_cast<double>(corrections) / static_cast<double>(runs), maxPending, maxHistory,
                        static_cast<unsigned long long>(dropped), static_cast<unsigned long long>(resyncs),
                        static_cast<unsigned long long>(raised));
        }
    }
}
