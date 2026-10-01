#include "render/shader.h"

#include "core/utf8.h"
#include "data/block_textures.h"

#include <glad/glad.h>

#include <format>
#include <optional>
#include <string>
#include <vector>

namespace aurora::render {

namespace {

std::string infoLog(GLuint object, bool isProgram)
{
    GLint length = 0;
    if (isProgram) {
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
    } else {
        glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    }
    std::string log(static_cast<std::size_t>(length > 0 ? length : 0), '\0');
    if (length > 0) {
        if (isProgram) {
            glGetProgramInfoLog(object, length, nullptr, log.data());
        } else {
            glGetShaderInfoLog(object, length, nullptr, log.data());
        }
    }
    while (!log.empty() && (log.back() == '\0' || log.back() == '\n')) {
        log.pop_back();
    }
    return log;
}

// Compiles one stage; 0 with `error` set on failure.
GLuint compile(GLenum stage, const std::filesystem::path& file, std::string& error)
{
    std::string readError;
    const std::optional<std::vector<std::byte>> bytes = data::readFileBytes(file, readError);
    if (!bytes) {
        error = std::format("{}: {}", core::pathToUtf8(file), readError);
        return 0;
    }
    const auto* source = reinterpret_cast<const GLchar*>(bytes->data());
    const auto length = static_cast<GLint>(bytes->size());
    const GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, &length);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        error = std::format("{}: compile error: {}", core::pathToUtf8(file), infoLog(shader, false));
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

ShaderProgram::~ShaderProgram()
{
    destroy();
}

bool ShaderProgram::loadFiles(const std::filesystem::path& vertexFile, const std::filesystem::path& fragmentFile,
                              std::string& error)
{
    destroy();
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertexFile, error);
    if (vertex == 0) {
        return false;
    }
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentFile, error);
    if (fragment == 0) {
        glDeleteShader(vertex);
        return false;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        error = std::format("{} + {}: link error: {}", core::pathToUtf8(vertexFile), core::pathToUtf8(fragmentFile),
                            infoLog(program, true));
        glDeleteProgram(program);
        return false;
    }
    m_program = program;
    return true;
}

void ShaderProgram::destroy()
{
    if (m_program != 0) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

void ShaderProgram::use() const
{
    glUseProgram(m_program);
}

std::int32_t ShaderProgram::uniformLocation(std::string_view name) const
{
    return glGetUniformLocation(m_program, std::string(name).c_str());
}

} // namespace aurora::render
