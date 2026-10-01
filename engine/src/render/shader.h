#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace aurora::render {

// A linked vertex + fragment program read from GLSL files. Requires a current GL context.
class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    // On failure `error` names the file and holds the driver's log.
    bool loadFiles(const std::filesystem::path& vertexFile, const std::filesystem::path& fragmentFile,
                   std::string& error);
    void destroy();

    void use() const;
    // -1 if the program has no such active uniform.
    std::int32_t uniformLocation(std::string_view name) const;

private:
    std::uint32_t m_program = 0;
};

} // namespace aurora::render
