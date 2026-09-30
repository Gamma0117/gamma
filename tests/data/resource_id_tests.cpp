#include "data/resource_id.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string_view>

using aurora::data::ResourceId;

TEST_CASE("Resource ids accept namespace:path with slash-separated segments", "[data][id]")
{
    for (const std::string_view text :
         {"aurora:stone", "aurora:loot/ogre", "aurora:blueprints/smithy_gothic", "mod_2:a/b_c/d3"}) {
        CAPTURE(text);
        const std::optional<ResourceId> id = ResourceId::parse(text);
        REQUIRE(id);
        CHECK(id->str() == text);
    }

    const ResourceId loot = *ResourceId::parse("aurora:loot/ogre");
    CHECK(loot.nameSpace() == "aurora");
    CHECK(loot.path() == "loot/ogre");
    CHECK_FALSE(loot.isSingleSegment());
    CHECK(ResourceId::parse("aurora:stone")->isSingleSegment());
}

TEST_CASE("Resource ids reject anything outside the grammar", "[data][id]")
{
    for (const std::string_view text : {"", "stone", ":stone", "aurora:", "Aurora:stone", "aurora:Stone",
                                        "aurora:/stone", "aurora:stone/", "aurora:a//b", "aurora:../stone",
                                        "aurora:stone.png", "a:b:c", "aurora: stone", "aurora:st one", "au-ra:x"}) {
        CAPTURE(text);
        CHECK_FALSE(ResourceId::parse(text));
    }
}

TEST_CASE("A bare path takes the default namespace", "[data][id]")
{
    CHECK(ResourceId::parse("block/stone", "aurora")->str() == "aurora:block/stone");
    CHECK(ResourceId::parse("mod:block/fancy", "aurora")->str() == "mod:block/fancy");
    CHECK_FALSE(ResourceId::parse("block/Stone", "aurora"));
    CHECK_FALSE(ResourceId::parse("block/stone", "Bad"));
    CHECK_FALSE(ResourceId::parse("block/stone")); // The strict form still needs a namespace.
}

TEST_CASE("Resource ids compare by their text", "[data][id]")
{
    CHECK(*ResourceId::parse("aurora:a") < *ResourceId::parse("aurora:b"));
    CHECK(*ResourceId::parse("aurora:a") == *ResourceId::parse("a", "aurora"));
    CHECK(ResourceId().empty());
    CHECK(ResourceId().path().empty());
}
