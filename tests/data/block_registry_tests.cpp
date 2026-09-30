#include "core/log.h"
#include "data/block_registry.h"

#include "data_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace aurora::data;

namespace {

BlockProperty property(std::string name, std::vector<std::string> values)
{
    return BlockProperty{std::move(name), std::move(values)};
}

// Values "v0" .. "v<count-1>".
BlockProperty numberedProperty(std::string name, std::size_t count)
{
    BlockProperty result{std::move(name), {}};
    for (std::size_t i = 0; i < count; ++i) {
        result.values.push_back(std::format("v{}", i));
    }
    return result;
}

// Properties must be passed sorted by name, as the loader hands them over.
BlockDefinition makeBlock(std::string_view id, std::vector<BlockProperty> properties = {},
                          std::vector<std::uint32_t> defaults = {})
{
    BlockDefinition block;
    block.id = *ResourceId::parse(id);
    block.defaultValues = defaults.empty() ? std::vector<std::uint32_t>(properties.size(), 0) : defaults;
    block.properties = std::move(properties);
    return block;
}

std::shared_ptr<const BlockRegistry> createOrFail(std::vector<BlockDefinition> blocks)
{
    std::vector<LoadIssue> issues;
    auto registry = BlockRegistry::create(std::move(blocks), issues);
    INFO(aurora::test::describeIssues(issues));
    REQUIRE(registry);
    return registry;
}

// The registry used by the state string tests.
std::shared_ptr<const BlockRegistry> sampleRegistry()
{
    return createOrFail({
        makeBlock("aurora:stone"),
        makeBlock("aurora:oak_log", {property("axis", {"x", "y", "z"})}, {1}),
        makeBlock("aurora:fence",
                  {property("east", {"false", "true"}), property("north", {"false", "true"}),
                   property("waterlogged", {"false", "true"})}),
        makeBlock("aurora:crop", {numberedProperty("age", 8)}),
    });
}

class QuietLog {
public:
    QuietLog()
        : m_previous(aurora::core::Log::minLevel())
    {
        aurora::core::Log::setMinLevel(aurora::core::LogLevel::Off);
    }
    ~QuietLog() { aurora::core::Log::setMinLevel(m_previous); }

    QuietLog(const QuietLog&) = delete;
    QuietLog& operator=(const QuietLog&) = delete;

private:
    aurora::core::LogLevel m_previous;
};

} // namespace

TEST_CASE("Built-in blocks come first and data blocks follow in id order", "[data][registry]")
{
    const auto registry = createOrFail({
        makeBlock("aurora:stone"),
        makeBlock("aurora:oak_log", {property("axis", {"x", "y", "z"})}, {1}),
        makeBlock("aurora:dirt"),
    });

    std::vector<std::string> ids;
    for (const BlockDefinition& block : registry->blocks()) {
        ids.push_back(block.id.str());
    }
    CHECK(ids == std::vector<std::string>{"aurora:air", "aurora:unknown", "aurora:dirt", "aurora:oak_log",
                                          "aurora:stone"});
    CHECK(registry->blockCount() == 5);
    CHECK(registry->stateCount() == 7);

    CHECK(registry->blockOf(kAirState).id.str() == "aurora:air");
    CHECK(registry->blockOf(kUnknownState).id.str() == "aurora:unknown");
    CHECK(registry->blockOf(kAirState).render == RenderLayer::Invisible);
    CHECK_FALSE(registry->blockOf(kAirState).solid);
    CHECK(registry->blockOf(kUnknownState).unbreakable);

    const BlockDefinition* log = registry->findBlock("aurora:oak_log");
    REQUIRE(log != nullptr);
    CHECK(log->firstState == 3);
    CHECK(log->stateCount == 3);
    CHECK(log->defaultState == 4);
    CHECK(registry->blockOf(5).id.str() == "aurora:oak_log");
    CHECK(registry->blockOf(6).id.str() == "aurora:stone");
    CHECK(registry->findBlock("aurora:missing") == nullptr);

    CHECK(BlockRegistry::isReservedId("aurora:air"));
    CHECK(BlockRegistry::isReservedId("aurora:unknown"));
    CHECK_FALSE(BlockRegistry::isReservedId("aurora:stone"));
}

TEST_CASE("Every block state round-trips through its string", "[data][registry]")
{
    const auto registry = sampleRegistry();
    for (std::uint32_t state = 0; state < registry->stateCount(); ++state) {
        const std::string text = registry->stateToString(static_cast<BlockStateId>(state));
        CAPTURE(state, text);
        const BlockRegistry::ParseResult parsed = registry->parseState(text);
        CHECK(parsed.error.empty());
        REQUIRE(parsed.state);
        CHECK(*parsed.state == state);
    }

    // Properties print in name order with every value spelled out.
    const BlockStateId fence = registry->findBlock("aurora:fence")->defaultState;
    CHECK(registry->stateToString(fence) == "aurora:fence[east=false,north=false,waterlogged=false]");
    CHECK(registry->stateToString(registry->findBlock("aurora:oak_log")->defaultState) == "aurora:oak_log[axis=y]");
    CHECK(registry->stateToString(registry->findBlock("aurora:stone")->defaultState) == "aurora:stone");
    CHECK(registry->stateToString(kAirState) == "aurora:air");
}

TEST_CASE("A bare id or omitted properties take the default values", "[data][registry]")
{
    const auto registry = sampleRegistry();
    CHECK(registry->parseState("aurora:oak_log").state == registry->findBlock("aurora:oak_log")->defaultState);
    CHECK(registry->parseState("aurora:stone").state == registry->findBlock("aurora:stone")->defaultState);

    const auto partial = registry->parseState("aurora:fence[north=true]");
    REQUIRE(partial.state);
    CHECK(registry->stateToString(*partial.state) == "aurora:fence[east=false,north=true,waterlogged=false]");

    // Order in the text does not matter.
    CHECK(registry->parseState("aurora:fence[waterlogged=true,east=true]").state ==
          registry->parseState("aurora:fence[east=true,waterlogged=true]").state);
}

TEST_CASE("Strict state parsing rejects malformed and unknown text", "[data][registry]")
{
    const auto registry = sampleRegistry();
    for (const std::string_view text :
         {"", "stone", "aurora:missing", "aurora:oak_log[]", "aurora:oak_log[axis=q]", "aurora:oak_log[color=red]",
          "aurora:oak_log[axis=x,axis=y]", "aurora:oak_log[axis=x", "aurora:oak_log[axis=x]]",
          "aurora:oak_log[axis=x] ", " aurora:oak_log", "aurora:oak_log[axis = x]", "aurora:oak_log[axis=x,]",
          "aurora:oak_log[=x]", "aurora:oak_log[axis]", "aurora:stone[axis=x]", "aurora:oak_log[axis=X]",
          "aurora:oak_log[[axis=x]]", "aurora:oak_log(axis=x)", "aurora:oak_log[axis=x][axis=y]"}) {
        CAPTURE(text);
        const BlockRegistry::ParseResult parsed = registry->parseState(text);
        CHECK_FALSE(parsed.state);
        CHECK_FALSE(parsed.error.empty());
    }

    CHECK(registry->parseState("aurora:oak_log[color=red]").error.find("no property 'color'") != std::string::npos);
    CHECK(registry->parseState("aurora:oak_log[axis=q]").error.find("'q' is not a value") != std::string::npos);
    CHECK(registry->parseState("aurora:missing").error.find("unknown block") != std::string::npos);
    CHECK(registry->parseState("aurora:oak_log[axis=x,axis=y]").error.find("twice") != std::string::npos);
}

TEST_CASE("Only the recovery path substitutes the unknown block", "[data][registry]")
{
    const auto registry = sampleRegistry();
    QuietLog quiet;
    CHECK(registry->resolveStateOrUnknown("aurora:gone") == kUnknownState);
    CHECK(registry->resolveStateOrUnknown("aurora:oak_log[axis=q]") == kUnknownState);
    CHECK(registry->resolveStateOrUnknown("aurora:oak_log[axis=z]") ==
          *registry->parseState("aurora:oak_log[axis=z]").state);
    CHECK_FALSE(registry->parseState("aurora:gone").state); // Strict parsing never does.
}

TEST_CASE("State counting checks the limit before every multiplication", "[data][registry]")
{
    CHECK(countStates({}, 10) == 1u);
    CHECK(countStates({property("a", {"x", "y"}), property("b", {"x", "y", "z"})}, 100) == 6u);
    CHECK(countStates({property("a", {"x", "y"}), property("b", {"x", "y", "z"})}, 6) == 6u);
    CHECK_FALSE(countStates({property("a", {"x", "y"}), property("b", {"x", "y", "z"})}, 5));
    CHECK_FALSE(countStates({property("a", {})}, 100));

    // 256^16 = 2^128 would wrap around a 64-bit product; it must be rejected, not wrapped.
    std::vector<BlockProperty> huge;
    for (int i = 0; i < 16; ++i) {
        huge.push_back(numberedProperty(std::format("p{:02}", i), 256));
    }
    CHECK_FALSE(countStates(huge, kMaxDataBlockStates));
    CHECK_FALSE(countStates(huge, UINT64_MAX));
}

TEST_CASE("Exactly 65536 states fit and one more does not", "[data][registry]")
{
    // 2 * 7 * 31 * 151 = 65534 data states + air + unknown = 65536.
    const auto fullBlock = [] {
        return makeBlock("aurora:full", {numberedProperty("a", 2), numberedProperty("b", 7),
                                         numberedProperty("c", 31), numberedProperty("d", 151)});
    };

    const auto registry = createOrFail({fullBlock()});
    CHECK(registry->stateCount() == 65536);
    const BlockStateId last = 65535;
    CHECK(registry->blockOf(last).id.str() == "aurora:full");
    CHECK(registry->stateToString(last) == "aurora:full[a=v1,b=v6,c=v30,d=v150]");
    CHECK(registry->parseState("aurora:full[a=v1,b=v6,c=v30,d=v150]").state == last);

    std::vector<LoadIssue> issues;
    CHECK_FALSE(BlockRegistry::create({fullBlock(), makeBlock("aurora:extra")}, issues));
    INFO(aurora::test::describeIssues(issues));
    CHECK(aurora::test::hasIssue(issues, IssueSeverity::Error, "", "", "Too many block states: 65537"));
    CHECK(aurora::test::hasIssue(issues, IssueSeverity::Error, "", "", "aurora:full (65534 states"));
}

TEST_CASE("Registry creation rejects reserved and repeated ids", "[data][registry]")
{
    std::vector<LoadIssue> issues;
    CHECK_FALSE(BlockRegistry::create({makeBlock("aurora:air")}, issues));
    CHECK_FALSE(BlockRegistry::create({makeBlock("aurora:stone"), makeBlock("aurora:stone")}, issues));
    CHECK(issues.size() == 2);
}
