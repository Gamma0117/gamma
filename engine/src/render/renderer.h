#pragma once

#include <string>

namespace aurora::render {

struct ClearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

// Thin wrapper over global OpenGL state. Requires a current GL context.
class Renderer {
public:
    // Hooks up GL debug output (debug contexts only) and reads driver info.
    bool init();

    void clear(const ClearColor& color);

    const std::string& glVersion() const { return m_glVersion; }
    const std::string& glRenderer() const { return m_glRenderer; }

private:
    std::string m_glVersion;
    std::string m_glRenderer;
};

} // namespace aurora::render
