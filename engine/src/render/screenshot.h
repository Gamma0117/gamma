#pragma once

#include <filesystem>
#include <string>

namespace aurora::render {

// Reads the current back buffer (width x height, the real framebuffer size) and writes it as a PNG, top row first.
// The file is written through std::filesystem, so non-ASCII paths work. False with `error` set on failure.
bool saveFramebufferPng(const std::filesystem::path& file, int width, int height, std::string& error);

} // namespace aurora::render
