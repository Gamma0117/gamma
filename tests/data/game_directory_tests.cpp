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
