#include "client/local_player.h"
#include "server/server_player.h"

#include "../data/data_test_support.h"
#include "../entity/entity_test_support.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <vector>

using aurora::client::LocalPlayer;
using aurora::entity::MovementIntent;
using aurora::entity::PlayerInput;
using aurora::entity::PlayerMotion;
using aurora::entity::PlayerState;
using aurora::server::PlayerMessage;
using aurora::server::ServerPlayer;
using aurora::test::sameBits;
using aurora::test::standardTuning;
using aurora::test::TestBlocks;
using Catch::Approx;

namespace {

constexpr MovementIntent kWalkEast{.forward = 1, .yaw = 90.0f};

std::shared_ptr<const aurora::data::PlayerMovementTuning> tuning()
{
    return std::make_shared<const aurora::data::PlayerMovementTuning>(standardTuning());
}

// A server player and the local player, connected by hand so each test decides the order of events. The server
// spawns and runs its spawn tick at once, as IntegratedServer does.
struct Link {
    TestBlocks serverBlocks;
    TestBlocks clientBlocks;
    ServerPlayer server{tuning()};
    LocalPlayer client{tuning()};
    std::uint64_t serverTick = 0;
    std::map<std::uint32_t, PlayerMotion> predicted; // The client's prediction right after making each input.

    Link()
    {
        server.spawn({0.5, 64.0, 0.5});
        serverStep();
        deliver();
    }

    std::optional<PlayerInput> clientTick(const MovementIntent& intent)
    {
        std::optional<PlayerInput> input = client.tick(intent, clientBlocks.world());
        if (input) {
            predicted[input->sequence] = client.current();
            server.receive({.kind = PlayerMessage::Kind::Input, .input = *input});
        }
        return input;
    }
    void serverStep()
    {
        server.tick(serverBlocks.world());
        ++serverTick;
    }
    PlayerState state() const { return server.state(serverTick); }
    void deliver() { client.receive(state(), clientBlocks.world()); }
    void neutralize()
    {
        if (const std::optional<std::uint32_t> through = client.neutralize(clientBlocks.world())) {
            server.receive({.kind = PlayerMessage::Kind::Neutralize, .through = *through});
        }
    }
};

} // namespace

TEST_CASE("In lockstep the prediction matches the server bit for bit", "[client][player]")
{
    // sameBits, not ==: == takes +0 and -0 as equal.
    REQUIRE(PlayerMotion{.velocity = {0.0, 0.0, 0.0}} == PlayerMotion{.velocity = {-0.0, 0.0, 0.0}});
    REQUIRE_FALSE(sameBits(PlayerMotion{.velocity = {0.0, 0.0, 0.0}}, PlayerMotion{.velocity = {-0.0, 0.0, 0.0}}));

    Link link;
    CHECK(link.client.spawned());
    for (int i = 0; i < 120; ++i) {
        MovementIntent intent = kWalkEast;
        intent.jump = i % 30 == 10;
        intent.sprint = i > 60;
        intent.yaw = static_cast<float>(90 + i);
        link.clientTick(intent);
        link.serverStep();
        // Whenever the server just applied an input, its state is the client's prediction for that input.
        if (const std::optional<std::uint32_t> applied = link.server.lastTickInput()) {
            CHECK(sameBits(link.server.motion(), link.predicted.at(*applied)));
        }
        link.deliver();
    }
    CHECK(link.client.stats().corrections == 0);
    CHECK(link.client.stats().resyncs == 0);
    CHECK(link.server.stats().starvedTicks == 1); // Only the spawn tick, before any input.
    CHECK(link.client.current().position.x > 10.0);
}

TEST_CASE("All settled with an empty history is normal and no resync", "[client][player]")
{
    Link link;
    link.clientTick(kWalkEast);
    for (int i = 0; i < 3; ++i) { // Two filling ticks, then the input.
        link.serverStep();
        link.deliver();
    }
    const auto stats = link.client.stats();
    CHECK(stats.lastInput == 1);
    CHECK(stats.lastSent == 1);
    CHECK(stats.history == 0);
    CHECK(stats.resyncs == 0);
    CHECK_FALSE(stats.resyncing);
    CHECK(link.client.current() == link.server.motion());
    CHECK(link.clientTick(kWalkEast)); // Goes on at once.
}

TEST_CASE("A full history pauses input once and resumes without a gap", "[client][player]")
{
    Link link;
    for (int i = 0; i < 70; ++i) { // The server stops answering.
        link.clientTick(kWalkEast);
    }
    auto stats = link.client.stats();
    CHECK(stats.lastSent == LocalPlayer::kMaxHistory);
    CHECK(stats.history == LocalPlayer::kMaxHistory);
    CHECK(stats.paused);
    CHECK(stats.inputPauses == 1); // Entering the pause counts once, not every tick.

    // The server comes back: 40 inputs at once, 36 dropped, 37 used.
    link.serverStep();
    link.deliver();
    stats = link.client.stats();
    CHECK(stats.lastInput == 37);
    CHECK(stats.history == 3); // 38..40 kept: no gap.
    CHECK(stats.resyncs == 0);
    const std::optional<PlayerInput> next = link.clientTick(kWalkEast);
    REQUIRE(next);
    CHECK(next->sequence == 41);
    CHECK_FALSE(link.client.stats().paused);

    // Full again later: a new pause.
    for (int i = 0; i < 50; ++i) {
        link.clientTick(kWalkEast);
    }
    CHECK(link.client.stats().inputPauses == 2);
}

TEST_CASE("Inputs settled by the server in any way leave the history", "[client][player]")
{
    Link link;
    SECTION("Frozen on the server: settled, and the prediction is corrected to it")
    {
        link.serverBlocks.unload({0, 0});
        for (int i = 0; i < 10; ++i) {
            link.clientTick(kWalkEast);
            link.serverStep();
            link.deliver();
        }
        CHECK(link.client.stats().frozen);
        CHECK(link.client.stats().corrections > 0);
        CHECK(link.client.stats().history <= 2);
        // Without new input the client ends where the server is.
        for (int i = 0; i < 4; ++i) {
            link.serverStep();
            link.deliver();
        }
        CHECK(link.client.current().position == link.server.motion().position);
    }
    SECTION("Dropped on the server")
    {
        for (int i = 0; i < 10; ++i) {
            link.clientTick(kWalkEast);
        }
        link.serverStep(); // 1..6 dropped, 7 used.
        link.deliver();
        CHECK(link.client.stats().lastInput == 7);
        CHECK(link.client.stats().history == 3);
    }
    CHECK(link.client.stats().resyncs == 0);
}

TEST_CASE("An older or repeated state is ignored", "[client][player]")
{
    Link link;
    link.clientTick(kWalkEast);
    link.clientTick(kWalkEast);
    link.serverStep();
    const PlayerState newer = link.state();
    link.deliver();
    const PlayerMotion current = link.client.current();
    PlayerState older = newer;
    older.serverTick -= 1;
    older.motion.position.y += 50.0;
    link.client.receive(older, link.clientBlocks.world());
    link.client.receive(newer, link.clientBlocks.world());
    CHECK(link.client.current() == current);
}

TEST_CASE("A gap waits for the server to settle everything sent and then goes on", "[client][player]")
{
    const aurora::test::QuietLog quiet; // The gap is logged on purpose.
    Link link;
    for (int i = 0; i < 10; ++i) {
        link.clientTick(kWalkEast);
        link.serverStep();
        link.deliver();
    }
    link.clientTick(kWalkEast);
    link.clientTick(kWalkEast); // Sent 1..12; the server holds 10..12.
    const std::uint32_t lastSent = link.client.stats().lastSent;
    REQUIRE(lastSent == 12);

    // A state that breaks the contract: its settled run went back below the history.
    PlayerState broken = link.state();
    ++link.serverTick;
    broken.serverTick = link.serverTick;
    broken.lastInput = 8;
    link.client.receive(broken, link.clientBlocks.world());
    CHECK(link.client.stats().resyncing);
    CHECK(link.client.stats().resyncs == 1);
    CHECK(link.client.current() == broken.motion);

    // Waiting: no new inputs, the server's states are shown, gravity and the queued inputs go on there.
    for (int i = 0; i < 2; ++i) {
        CHECK_FALSE(link.clientTick(kWalkEast));
        link.serverStep();
        link.deliver();
        CHECK(link.client.current() == link.server.motion());
    }
    CHECK(link.client.stats().resyncing); // Settled 11 of 12.
    link.serverStep();
    link.deliver();
    CHECK_FALSE(link.client.stats().resyncing);
    CHECK(link.client.stats().resyncs == 1); // Not once per tick.
    const std::optional<PlayerInput> next = link.clientTick(kWalkEast);
    REQUIRE(next);
    CHECK(next->sequence == lastSent + 1);
    link.serverStep();
    link.deliver();
    CHECK(link.client.stats().history <= 1);
    CHECK_FALSE(link.client.stats().resyncing);
}

TEST_CASE("Neutralize turns unsettled inputs neutral on both sides alike", "[client][player]")
{
    Link link;
    link.clientTick(kWalkEast);
    link.clientTick(kWalkEast);
    const PlayerMotion walking = link.client.current();
    link.neutralize();
    CHECK(link.client.current() != walking); // Replayed as standing.
    CHECK_FALSE(link.client.neutralize(link.clientBlocks.world())); // Nothing new to neutralise.
    for (int i = 0; i < 4; ++i) {
        link.serverStep();
        link.deliver();
    }
    CHECK(link.server.stats().lastInput == 2);
    CHECK(link.client.current() == link.server.motion());
    CHECK(link.client.stats().corrections == 0);
    CHECK(link.server.motion().position.x == 0.5);

    link.clientTick(kWalkEast);
    CHECK(link.client.neutralize(link.clientBlocks.world()) == 3u);
}

TEST_CASE("Drawing blends only the last two predictions of one timeline", "[client][player]")
{
    Link link;
    // At the spawn both are the spawn.
    CHECK(link.client.previous() == link.client.current());
    CHECK(link.client.renderPosition(0.5) == link.client.current().position);

    link.clientTick(kWalkEast);
    link.clientTick(kWalkEast);
    const glm::dvec3 from = link.client.previous().position;
    const glm::dvec3 to = link.client.current().position;
    CHECK(from.x < to.x);
    CHECK(link.client.renderPosition(0.0) == from);
    CHECK(link.client.renderPosition(1.0) == to);
    CHECK(link.client.renderPosition(0.5).x == Approx((from.x + to.x) / 2));
    CHECK(link.client.eyeHeight(0.3) == Approx(1.62));

    // A correction: the server froze the player. Both ends come from the replay, nothing from before.
    link.serverBlocks.unload({0, 0});
    link.serverStep();
    link.serverStep();
    link.deliver();
    const auto stats = link.client.stats();
    CHECK(stats.corrections == 1);
    CHECK(link.client.renderPosition(0.0) == link.client.previous().position);
    CHECK(link.client.renderPosition(1.0) == link.client.current().position);
    CHECK(link.client.previous().position.x <= link.client.current().position.x);
    CHECK(link.client.current().position.x < to.x);

    // A tick that predicts nothing (here: the history is full) holds still.
    for (int i = 0; i < 45; ++i) {
        link.clientTick(kWalkEast);
    }
    CHECK(link.client.stats().paused);
    CHECK(link.client.previous() == link.client.current());

    // Sneaking lowers the eye between two ticks.
    Link sneaking;
    sneaking.clientTick({.sneak = true});
    CHECK(sneaking.client.eyeHeight(0.0) == Approx(1.62));
    CHECK(sneaking.client.eyeHeight(1.0) == Approx(1.27));
}

TEST_CASE("A sneak the server applied keeps the eyes low once settled", "[client][player][sneak]")
{
    const MovementIntent sneak{.sneak = true};
    Link link;
    link.clientTick(sneak);
    CHECK(link.client.eyeHeight(1.0) == Approx(1.27));
    for (int i = 0; i < 3; ++i) { // Two filling ticks, then the input.
        link.serverStep();
        link.deliver();
    }
    REQUIRE(link.server.lastTickInput() == 1u);
    REQUIRE(link.server.lastTickIntent().sneak);
    REQUIRE(link.client.stats().lastInput == 1);
    REQUIRE(link.client.stats().history == 0);
    // A frame with no client tick after the state: both ends are the server's sneaking state.
    CHECK(link.client.eyeHeight(0.0) == Approx(1.27));
    CHECK(link.client.eyeHeight(1.0) == Approx(1.27));
    // A frame with a client tick, still sneaking: never blends towards standing.
    link.clientTick(sneak);
    for (const double alpha : {0.0, 0.5, 1.0}) {
        CHECK(link.client.eyeHeight(alpha) == Approx(1.27));
    }
}

TEST_CASE("The eye height follows the pose the server really applied", "[client][player][sneak]")
{
    const MovementIntent sneak{.sneak = true};
    const MovementIntent stand{};
    const auto eyesAt = [](const Link& link, double height) {
        return link.client.eyeHeight(0.0) == Approx(height) && link.client.eyeHeight(1.0) == Approx(height);
    };

    SECTION("Neutralize: the unsettled sneaks become standing here and there")
    {
        Link link;
        link.clientTick(sneak);
        link.clientTick(sneak);
        link.neutralize();
        CHECK(link.client.eyeHeight(1.0) == Approx(1.62));
        for (int i = 0; i < 4; ++i) {
            link.serverStep();
            link.deliver();
        }
        REQUIRE(link.client.stats().history == 0);
        CHECK_FALSE(link.server.motion().sneaking);
        CHECK(eyesAt(link, 1.62));
    }
    SECTION("A neutral tick on the server (no input) stands the player up")
    {
        Link link;
        link.clientTick(sneak);
        for (int i = 0; i < 3; ++i) {
            link.serverStep();
            link.deliver();
        }
        REQUIRE(eyesAt(link, 1.27));
        link.serverStep(); // Starving: neutral.
        link.deliver();
        REQUIRE_FALSE(link.server.lastTickInput());
        CHECK(eyesAt(link, 1.62));
    }
    SECTION("Inputs dropped on the server: the pose is the applied input's")
    {
        Link link;
        for (int i = 0; i < 10; ++i) {
            link.clientTick(i < 6 ? sneak : stand); // 1..6 sneak, 7..10 stand.
        }
        link.serverStep(); // 1..6 dropped, 7 applied.
        link.deliver();
        REQUIRE(link.client.stats().lastInput == 7);
        CHECK_FALSE(link.server.motion().sneaking);
        CHECK(link.client.previous().sneaking == false);
        CHECK(eyesAt(link, 1.62));
    }
    SECTION("A late input is settled but never applied: its sneak is not the pose")
    {
        // 1, 2 stand, 3 sneaks, 4 stands; the server gets 1 2 4 3, so 3 is late. After the second tick the settled
        // run is 3 (front 4 - 1) while the last applied input is 2: copying input 3's intent would sneak.
        TestBlocks blocks;
        LocalPlayer client(tuning());
        ServerPlayer server(tuning());
        server.spawn({0.5, 64.0, 0.5});
        server.tick(blocks.world());
        client.receive(server.state(1), blocks.world());
        std::map<std::uint32_t, PlayerInput> made;
        for (const MovementIntent& intent : {stand, stand, sneak, stand}) {
            const std::optional<PlayerInput> input = client.tick(intent, blocks.world());
            REQUIRE(input);
            made[input->sequence] = *input;
        }
        for (const std::uint32_t sequence : {1u, 2u, 4u, 3u}) {
            server.receive({.kind = PlayerMessage::Kind::Input, .input = made.at(sequence)});
        }
        server.tick(blocks.world());
        server.tick(blocks.world());
        client.receive(server.state(3), blocks.world());
        REQUIRE(client.stats().lastInput == 3);
        REQUIRE(client.stats().history == 1);
        CHECK(client.eyeHeight(0.0) == Approx(1.62));
        CHECK(client.eyeHeight(1.0) == Approx(1.62));
    }
    SECTION("Frozen on the server: the pose still follows the applied intent")
    {
        Link link;
        link.serverBlocks.unload({0, 0});
        link.clientTick(sneak);
        for (int i = 0; i < 3; ++i) {
            link.serverStep();
            link.deliver();
        }
        REQUIRE(link.client.stats().frozen);
        REQUIRE(link.client.stats().history == 0);
        CHECK(eyesAt(link, 1.27));
    }
    SECTION("A resync shows the server's pose")
    {
        const aurora::test::QuietLog quiet; // The gap is logged on purpose.
        Link link;
        for (int i = 0; i < 10; ++i) {
            link.clientTick(stand);
            link.serverStep();
            link.deliver();
        }
        link.clientTick(stand);
        link.clientTick(stand);
        PlayerState broken = link.state(); // The settled run goes back below the history: a gap.
        broken.serverTick = ++link.serverTick;
        broken.lastInput = 8;
        broken.motion.sneaking = true;
        link.client.receive(broken, link.clientBlocks.world());
        REQUIRE(link.client.stats().resyncing);
        CHECK(eyesAt(link, 1.27));
        CHECK_FALSE(link.clientTick(stand)); // Held while waiting: the pose stays.
        CHECK(eyesAt(link, 1.27));
    }
    SECTION("A paused history holds the pose")
    {
        Link link;
        for (int i = 0; i < 45; ++i) {
            link.clientTick(sneak);
        }
        REQUIRE(link.client.stats().paused);
        CHECK(eyesAt(link, 1.27));
    }
}

TEST_CASE("Late and repeated inputs settle the client's history without a resync", "[client][player]")
{
    // The real server and client, inputs 1..4 predicted first, then delivered in another order.
    const auto deliverInOrder = [](std::initializer_list<std::uint32_t> order, std::uint32_t made) {
        struct Result {
            std::vector<std::uint32_t> settled;
            std::vector<std::size_t> history;
            aurora::client::LocalPlayerStats client;
            std::uint64_t stale = 0;
            bool converged = false;
        } result;
        TestBlocks blocks;
        LocalPlayer client(tuning());
        ServerPlayer server(tuning());
        server.spawn({0.5, 64.0, 0.5});
        server.tick(blocks.world());
        std::uint64_t tick = 1;
        client.receive(server.state(tick), blocks.world());
        std::map<std::uint32_t, PlayerInput> inputs;
        for (std::uint32_t i = 0; i < made; ++i) {
            const std::optional<PlayerInput> input = client.tick(kWalkEast, blocks.world());
            REQUIRE(input);
            inputs[input->sequence] = *input;
        }
        for (const std::uint32_t sequence : order) {
            server.receive({.kind = PlayerMessage::Kind::Input, .input = inputs.at(sequence)});
        }
        while (client.stats().lastInput < made) {
            server.tick(blocks.world());
            client.receive(server.state(++tick), blocks.world());
            result.settled.push_back(client.stats().lastInput);
            result.history.push_back(client.stats().history);
        }
        result.client = client.stats();
        result.stale = server.stats().staleInputs;
        result.converged = sameBits(client.current(), server.motion());
        return result;
    };

    SECTION("1 2 4 3: 3 is late, settled but never applied")
    {
        const auto result = deliverInOrder({1, 2, 4, 3}, 4);
        CHECK(result.settled == std::vector<std::uint32_t>{1, 3, 4});
        CHECK(result.history == std::vector<std::size_t>{3, 1, 0});
        CHECK(result.stale == 1);
        CHECK(result.client.resyncs == 0);
        CHECK(result.client.corrections == 1); // The client walked 3; the server never did.
        CHECK(result.converged);
    }
    SECTION("1 2 2 3: the copy is dropped, nothing changes")
    {
        const auto result = deliverInOrder({1, 2, 2, 3}, 3);
        CHECK(result.settled == std::vector<std::uint32_t>{1, 2, 3});
        CHECK(result.history == std::vector<std::size_t>{2, 1, 0});
        CHECK(result.stale == 1);
        CHECK(result.client.resyncs == 0);
        CHECK(result.client.corrections == 0);
        CHECK(result.converged);
    }
}
