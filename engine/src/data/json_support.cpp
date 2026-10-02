#include "data/json_support.h"

#include <format>
#include <fstream>
#include <iterator>
#include <set>

namespace aurora::data::json {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxQuotedLength = 40;

// Parser callback that reports a key repeated within one object, at any depth. nlohmann would silently keep
// only the last value.
class DuplicateKeyCheck {
public:
    explicit DuplicateKeyCheck(FileIssues& issues)
        : m_issues(&issues)
    {
    }

    bool operator()(int /*depth*/, Json::parse_event_t event, Json& parsed)
    {
        switch (event) {
        case Json::parse_event_t::object_start:
        case Json::parse_event_t::array_start:
            m_frames.emplace_back();
            m_frames.back().isArray = event == Json::parse_event_t::array_start;
            break;
        case Json::parse_event_t::key:
            if (!m_frames.empty() && parsed.is_string()) {
                Frame& frame = m_frames.back();
                frame.key = parsed.get<std::string>();
                if (!frame.keys.insert(frame.key).second) {
                    m_issues->error(currentPointer(), std::format("duplicate key '{}'", frame.key));
                }
            }
            break;
        case Json::parse_event_t::value:
            elementDone();
            break;
        case Json::parse_event_t::object_end:
        case Json::parse_event_t::array_end:
            if (!m_frames.empty()) {
                m_frames.pop_back();
            }
            elementDone();
            break;
        }
        return true;
    }

private:
    struct Frame {
        bool isArray = false;
        std::size_t index = 0;
        std::string key;
        std::set<std::string> keys;
    };

    void elementDone()
    {
        if (!m_frames.empty() && m_frames.back().isArray) {
            ++m_frames.back().index;
        }
    }

    std::string currentPointer() const
    {
        std::string pointer;
        for (const Frame& frame : m_frames) {
            pointer += '/';
            pointer += frame.isArray ? std::to_string(frame.index) : pointerToken(frame.key);
        }
        return pointer;
    }

    FileIssues* m_issues;
    std::vector<Frame> m_frames;
};

} // namespace


std::string pointerToken(std::string_view token)
{
    std::string escaped;
    for (const char c : token) {
        if (c == '~') {
            escaped += "~0";
        } else if (c == '/') {
            escaped += "~1";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

std::string childPointer(std::string_view parent, std::string_view key)
{
    return std::format("{}/{}", parent, pointerToken(key));
}

std::string describe(const Json& value)
{
    if (value.is_object() || value.is_array()) {
        return value.type_name();
    }
    std::string text = value.dump(-1, ' ', false, Json::error_handler_t::replace);
    if (text.size() > kMaxQuotedLength) {
        text = text.substr(0, kMaxQuotedLength) + "...";
    }
    return std::format("{} {}", value.type_name(), text);
}

const Json* findField(const Json& object, std::string_view key)
{
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &*it;
}

std::optional<std::string> readFile(const fs::path& file, FileIssues& issues)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        issues.error({}, "cannot open the file");
        return std::nullopt;
    }
    std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        issues.error({}, "cannot read the file");
        return std::nullopt;
    }
    return text;
}

std::optional<Json> parseJson(const std::string& text, FileIssues& issues)
{
    // Library exceptions stop at this boundary and become issues of this file.
    try {
        return Json::parse(text, DuplicateKeyCheck(issues));
    } catch (const Json::exception& e) {
        std::string_view message = e.what();
        if (const std::size_t tagEnd = message.find("] "); message.starts_with("[json.") && tagEnd != message.npos) {
            message.remove_prefix(tagEnd + 2);
        }
        issues.error({}, std::string(message));
        return std::nullopt;
    }
}


std::optional<bool> readBool(const Json& value, const std::string& pointer, FileIssues& issues)
{
    if (!value.is_boolean()) {
        issues.error(pointer, std::format("expected true or false, got {}", describe(value)));
        return std::nullopt;
    }
    return value.get<bool>();
}

void warnUnknownFields(const Json& object, std::span<const std::string_view> known, const std::string& pointer,
                       FileIssues& issues)
{
    for (const auto& [key, value] : object.items()) {
        if (std::ranges::find(known, key) == known.end()) {
            issues.warning(childPointer(pointer, key), std::format("unknown field '{}' (ignored)", key));
        }
    }
}

} // namespace aurora::data::json
