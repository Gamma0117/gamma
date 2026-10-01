#include "client/cursor_controller.h"
#include "platform/input_state.h"

#include <iostream>

int main()
{
    using namespace aurora;
    platform::InputState input;
    client::CursorController cursor;
    cursor.onClick(false);
    const bool allowedA = cursor.captured();
    input.startFrame();
    input.onKeyPressed(platform::Key::Space);
    input.onKeyPressed(platform::Key::Escape);
    input.onButtonPressed(platform::MouseButton::Left);
    if (input.wasKeyPressed(platform::Key::Escape)) {
        cursor.onEscape();
    }
    const bool afterEscape = cursor.captured();
    if (input.wasButtonPressed(platform::MouseButton::Left)) {
        cursor.onClick(false);
    }
    const bool allowedB = cursor.captured();
    std::cout << "allowed_A=" << allowedA << " after_Esc=" << afterEscape
              << " allowed_B=" << allowedB
              << " block_seen_by_endpoints=" << (allowedA && !allowedB)
              << " old_space=" << input.wasKeyPressed(platform::Key::Space) << '\n';
}
