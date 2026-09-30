#include "data/path_probe.h"

#include "data_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <system_error>

using aurora::data::PathKind;
using aurora::data::probePath;
using aurora::test::TempGame;

TEST_CASE("Paths are classified as folders or files or missing", "[data][path]")
{
    TempGame game("probe_basic");
    game.write("folder/file.txt", "text");

    CHECK(probePath(game.root() / "folder").kind == PathKind::Directory);
    CHECK(probePath(game.root() / "folder" / "file.txt").kind == PathKind::File);
    CHECK(probePath(game.root() / "absent").kind == PathKind::Missing);
    CHECK(probePath(game.root() / "absent" / "deeper" / "file.txt").kind == PathKind::Missing);
    CHECK(probePath(game.root() / "folder" / "absent.txt").kind == PathKind::Missing);
}

TEST_CASE("A broken link anywhere in a path is a failure and not a missing path", "[data][path]")
{
    TempGame game("probe_links");
    game.write("real/file.txt", "text");
    std::filesystem::create_directories(game.root() / "base");
    if (!aurora::test::makeBrokenLink(game.root() / "base" / "broken") ||
        !aurora::test::makeSelfLoop(game.root() / "base" / "looped")) {
        SKIP("symbolic links are not available here");
    }

    const auto last = probePath(game.root() / "base" / "broken");
    CHECK(last.kind == PathKind::Failed);
    CHECK(last.failure.find("broken link") != std::string::npos);

    // symlink_status() of the full path follows middle links and says "not found" here; the walk must not.
    const auto middle = probePath(game.root() / "base" / "broken" / "deeper" / "file.txt");
    CHECK(middle.kind == PathKind::Failed);
    CHECK(middle.failure.find("broken link") != std::string::npos);
    CHECK(middle.failure.find("broken (its target") != std::string::npos); // Names the link itself.

    CHECK(probePath(game.root() / "base" / "looped" / "file.txt").kind == PathKind::Failed);
}

TEST_CASE("Working links are followed", "[data][path]")
{
    TempGame game("probe_working_links");
    game.write("real/file.txt", "text");
    std::error_code error;
    std::filesystem::create_directory_symlink(game.root() / "real", game.root() / "linked", error);
    if (error) {
        SKIP("symbolic links are not available here");
    }
    CHECK(probePath(game.root() / "linked").kind == PathKind::Directory);
    CHECK(probePath(game.root() / "linked" / "file.txt").kind == PathKind::File);
    CHECK(probePath(game.root() / "linked" / "absent.txt").kind == PathKind::Missing);
}
