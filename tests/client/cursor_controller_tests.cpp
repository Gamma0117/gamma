#include "client/cursor_controller.h"

#include <catch2/catch_test_macros.hpp>

using aurora::client::CursorController;

TEST_CASE("The cursor is captured only by a click outside the UI", "[client][input]")
{
    CursorController cursor;
    CHECK_FALSE(cursor.captured());
    cursor.onCursorMoved(10.0, 10.0);
    cursor.onCursorMoved(50.0, 10.0);
    CHECK(cursor.takeLookDelta() == glm::dvec2(0.0)); // Not captured: the mouse does not turn the camera.

    cursor.onClick(true); // On the F3 panel.
    CHECK_FALSE(cursor.captured());
    cursor.onClick(false);
    CHECK(cursor.captured());
}

TEST_CASE("Capturing never turns the camera by the jump to the first position", "[client][input]")
{
    CursorController cursor;
    cursor.onCursorMoved(100.0, 100.0);
    cursor.onClick(false);
    cursor.onCursorMoved(640.0, 360.0); // The window recentres the hidden cursor.
    CHECK(cursor.takeLookDelta() == glm::dvec2(0.0));
    cursor.onCursorMoved(645.0, 358.0);
    cursor.onCursorMoved(650.0, 358.0);
    CHECK(cursor.takeLookDelta() == glm::dvec2(10.0, -2.0));
    CHECK(cursor.takeLookDelta() == glm::dvec2(0.0)); // Taken once.
}

TEST_CASE("Esc and focus loss release the cursor and recapturing starts fresh", "[client][input]")
{
    CursorController cursor;
    cursor.onClick(false);
    cursor.onCursorMoved(0.0, 0.0);
    cursor.onCursorMoved(5.0, 0.0);

    SECTION("Esc")
    {
        cursor.onEscape();
    }
    SECTION("Focus lost")
    {
        cursor.onFocusChanged(false);
    }
    CHECK_FALSE(cursor.captured());
    CHECK(cursor.takeLookDelta() == glm::dvec2(0.0)); // Collected movement is dropped too.
    cursor.onCursorMoved(900.0, 900.0);              // The free cursor wanders.
    CHECK(cursor.takeLookDelta() == glm::dvec2(0.0));

    cursor.onFocusChanged(true);
    CHECK_FALSE(cursor.captured()); // Focus alone does not capture.
    cursor.onClick(false);
    cursor.onCursorMoved(100.0, 100.0);
    CHECK(cursor.takeLookDelta() == glm::dvec2(0.0)); // No jump from 900, 900.
    cursor.onCursorMoved(101.0, 100.0);
    CHECK(cursor.takeLookDelta() == glm::dvec2(1.0, 0.0));
}

TEST_CASE("Movement keys reach the camera only while captured and the UI does not want them", "[client][input]")
{
    CursorController cursor;
    CHECK_FALSE(cursor.acceptsMovement(false));
    cursor.onClick(false);
    CHECK(cursor.acceptsMovement(false));
    CHECK_FALSE(cursor.acceptsMovement(true));
    cursor.onEscape();
    CHECK_FALSE(cursor.acceptsMovement(false));
}
