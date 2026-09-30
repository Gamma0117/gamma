#include "data/game_directory.h"

#include "data_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <vector>

using aurora::data::resolveGameDirectory;
using aurora::test::TempGame;

TEST_CASE("An explicit game folder is used as is and never replaced", "[data][game_dir]")
{
    TempGame game("game_dir_explicit");
    game.write("good/data/aurora/blocks/stone.json", "{}");
    game.write("bare/readme.txt", "no data here");
    const std::vector<std::filesystem::path> candidates{game.root() / "good"};

    // An existing folder is taken even without data: its problems are reported by the loader, not skipped.
    CHECK(resolveGameDirectory(game.root() / "bare", candidates).path == game.root() / "bare");

    // A missing folder is an error, although a valid candidate exists.
    const auto missing = resolveGameDirectory(game.root() / "missing", candidates);
    CHECK(missing.path.empty());
    CHECK(missing.error.find("--game-dir") != std::string::npos);
}

TEST_CASE("A game folder that cannot be inspected is an error and not a reason to look elsewhere",
          "[data][game_dir]")
{
    TempGame game("game_dir_unreadable");
    game.write("good/data/aurora/blocks/stone.json", "{}");
    if (!aurora::test::makeSelfLoop(game.root() / "looped") ||
        !aurora::test::makeSelfLoop(game.root() / "odd" / "data" / "aurora")) {
        SKIP("symbolic links are not available here");
    }

    const auto explicitLoop = resolveGameDirectory(game.root() / "looped", {game.root() / "good"});
    CHECK(explicitLoop.path.empty());
    CHECK(explicitLoop.error.find("cannot access --game-dir") != std::string::npos);

    const auto candidateLoop = resolveGameDirectory(std::nullopt, {game.root() / "odd", game.root() / "good"});
    CHECK(candidateLoop.path.empty());
    CHECK(candidateLoop.error.find("cannot access") != std::string::npos);
}

TEST_CASE("A broken link in the game folder path is an error and not a reason to look elsewhere",
          "[data][game_dir]")
{
    TempGame game("game_dir_broken_link");
    game.write("good/data/aurora/blocks/stone.json", "{}");
    std::filesystem::create_directories(game.root() / "linked_marker" / "data");
    if (!aurora::test::makeBrokenLink(game.root() / "linked_root") ||
        !aurora::test::makeBrokenLink(game.root() / "linked_marker" / "data" / "aurora")) {
        SKIP("symbolic links are not available here");
    }
    const std::filesystem::path good = game.root() / "good";

    const auto explicitLink = resolveGameDirectory(game.root() / "linked_root", {good});
    CHECK(explicitLink.path.empty());
    CHECK(explicitLink.error.find("broken link") != std::string::npos);

    // Neither a broken candidate folder nor a broken data/aurora inside it falls through to the good candidate.
    for (const char* broken : {"linked_root", "linked_marker"}) {
        CAPTURE(broken);
        const auto result = resolveGameDirectory(std::nullopt, {game.root() / broken, good});
        CHECK(result.path.empty());
        CHECK(result.error.find("broken link") != std::string::npos);
    }
}

TEST_CASE("Without --game-dir the first candidate with data/aurora is used", "[data][game_dir]")
{
    TempGame game("game_dir_search");
    game.write("second/data/aurora/blocks/stone.json", "{}");
    game.write("third/data/aurora/blocks/stone.json", "{}");
    game.write("first/data/other/blocks/stone.json", "{}");

    const std::vector<std::filesystem::path> candidates{game.root() / "missing", game.root() / "first",
                                                        game.root() / "second", game.root() / "third"};
    CHECK(resolveGameDirectory(std::nullopt, candidates).path == game.root() / "second");

    const auto none = resolveGameDirectory(std::nullopt, {game.root() / "missing", game.root() / "first"});
    CHECK(none.path.empty());
    CHECK(none.error.find("missing") != std::string::npos);
    CHECK(none.error.find("first") != std::string::npos);
}
