#include <userver/utils/strong_typedef.hpp>

#include <array>
#include <limits>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <gtest/gtest.h>
#include <boost/optional.hpp>
#include <boost/optional/optional_io.hpp>

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/serialize_container.hpp>
#include <userver/formats/json/string_builder.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/formats/parse/boost_optional.hpp>
#include <userver/formats/serialize/boost_optional.hpp>
#include <userver/formats/yaml/serialize.hpp>
#include <userver/formats/yaml/value.hpp>
#include <userver/utils/fmt_compat.hpp>

USERVER_NAMESPACE_BEGIN

namespace {

struct IntTag {};
struct OptionalIntTag {};
using IntTypedef = utils::StrongTypedef<IntTag, int>;

using OptionalIntTypedef = utils::StrongTypedef<OptionalIntTag, boost::optional<int>>;

struct IntStructTypedef : public utils::StrongTypedef<IntStructTypedef, int> {
    using StrongTypedef::StrongTypedef;
};

struct VectorStructTypedef : public utils::StrongTypedef<VectorStructTypedef, std::vector<int>> {
    using StrongTypedef::StrongTypedef;
};

struct NonStreamable {};

struct MyNonStreamable final
    : utils::StrongTypedef<MyNonStreamable, NonStreamable, utils::StrongTypedefOps::kCompareTransparent> {
    using StrongTypedef::StrongTypedef;
};

}  // namespace

USERVER_NAMESPACE_END

namespace fmt {

// fmt::format custoimization
template <>
struct formatter<USERVER_NAMESPACE::NonStreamable> : formatter<const char*> {
    template <typename FormatContext>
    auto format(const USERVER_NAMESPACE::NonStreamable& /*v*/, FormatContext& ctx) USERVER_FMT_CONST {
        return formatter<const char*>::format("!!!", ctx);
    }
};

}  // namespace fmt

USERVER_NAMESPACE_BEGIN

TEST(SerializeStrongTypedef, ParseInt) {
    auto json_object = formats::json::FromString(R"json({"data" : 10})json");
    auto json_data = json_object["data"];

    auto value = json_data.As<IntTypedef>();

    EXPECT_EQ(10, value.GetUnderlying());
}

TEST(SerializeStrongTypedef, ParseOptionalInt) {
    auto json_object = formats::json::FromString(R"json({"data" : 10})json");
    auto json_data = json_object["data"];

    auto value = json_data.As<OptionalIntTypedef>();

    ASSERT_TRUE(value.GetUnderlying());
    EXPECT_EQ(10, *(value.GetUnderlying()));
}

TEST(SerializeStrongTypedef, ParseIntStruct) {
    const auto json = formats::json::FromString(R"json(10)json");
    auto value = json.As<IntStructTypedef>();
    EXPECT_EQ(value.GetUnderlying(), 10);
}

TEST(SerializeStrongTypedef, ParseVectorStruct) {
    const auto json = formats::json::FromString(R"json([10])json");
    auto value = json.As<VectorStructTypedef>();
    EXPECT_EQ(value.GetUnderlying(), std::vector{10});
}

TEST(SerializeStrongTypedef, ParseOptionalIntNone) {
    auto json_object = formats::json::FromString(R"json({"data" : null})json");
    auto json_data = json_object["data"];

    auto value = json_data.As<OptionalIntTypedef>();

    EXPECT_FALSE(value.GetUnderlying());
}

TEST(SerializeStrongTypedef, SerializeCycleInt) {
    IntTypedef reference{10};
    // Serialize
    auto json_object = formats::json::ValueBuilder(reference).ExtractValue();

    // Parse
    auto test = json_object.As<IntTypedef>();

    EXPECT_EQ(reference, test);
}

TEST(SerializeStrongTypedef, SerializeCycleOptionalInt) {
    OptionalIntTypedef reference;
    reference = OptionalIntTypedef{boost::optional<int>{10}};

    // Serialize
    auto json_object = formats::json::ValueBuilder(reference).ExtractValue();

    // Parse
    auto test = json_object.As<OptionalIntTypedef>();

    EXPECT_EQ(reference, test);
}

TEST(SerializeStrongTypedef, SerializeCycleOptionalIntNone) {
    OptionalIntTypedef reference{boost::none};
    // Serialize
    auto json_object = formats::json::ValueBuilder(reference).ExtractValue();

    // Parse
    auto test = json_object.As<OptionalIntTypedef>();

    EXPECT_EQ(reference, test);
}

TEST(SerializeStrongTypedef, SerializeIntStruct) {
    const IntStructTypedef value{10};
    const auto json = formats::json::ValueBuilder{value}.ExtractValue();
    ASSERT_EQ(json, formats::json::FromString("10"));
}

TEST(SerializeStrongTypedef, SerializeVectorStruct) {
    const VectorStructTypedef value{std::vector{10}};
    const auto json = formats::json::ValueBuilder{value}.ExtractValue();
    ASSERT_EQ(json, formats::json::FromString("[10]"));
}

TEST(SerializeStrongTypedef, WriteToStreamIntStruct) {
    const IntStructTypedef value{10};
    formats::json::StringBuilder builder;
    WriteToStream(value, builder);
    EXPECT_EQ(formats::json::FromString(builder.GetString()), formats::json::FromString("10"));
}

TEST(SerializeStrongTypedef, WriteToStreamVectorStruct) {
    const VectorStructTypedef value{std::vector{10}};
    formats::json::StringBuilder builder;
    WriteToStream(value, builder);
    EXPECT_EQ(formats::json::FromString(builder.GetString()), formats::json::FromString("[10]"));
}

TEST(SerializeStrongTypedef, Fmt) {
    EXPECT_EQ("42", fmt::format("{}", IntTypedef{42}));
    EXPECT_EQ("f", fmt::format("{:x}", IntTypedef{15}));
    EXPECT_EQ("!!!", fmt::format("{}", MyNonStreamable{}));
}

TEST(SerializeStrongTypedef, FmtJoin) {
    EXPECT_EQ("!!!,!!!", fmt::format("{}", fmt::join(std::array<MyNonStreamable, 2>{}, ",")));
}

TEST(SerializeStrongTypedef, ToString) {
    using StringTypedefCompareStrong =
        utils::StrongTypedef<struct StringTagStrong, std::string, utils::StrongTypedefOps::kCompareStrong>;
    using StringTypedefCompareTransparent =
        utils::StrongTypedef<struct StringTagTransparent, std::string, utils::StrongTypedefOps::kCompareTransparent>;
    using StringTypedefCompareTransparentOnly = utils::
        StrongTypedef<struct StringTagTransparentOnly, std::string, utils::StrongTypedefOps::kCompareTransparentOnly>;

    using UInt64Typedef = utils::StrongTypedef<struct UInt64Tag, uint64_t>;

    EXPECT_EQ("qwerty", ToString(StringTypedefCompareStrong("qwerty")));
    EXPECT_EQ("qwerty", ToString(StringTypedefCompareTransparent("qwerty")));
    EXPECT_EQ("qwerty", ToString(StringTypedefCompareTransparentOnly("qwerty")));

    EXPECT_EQ("42", ToString(IntTypedef(42)));

    EXPECT_EQ(
        std::to_string(std::numeric_limits<uint64_t>::max()),
        ToString(UInt64Typedef(std::numeric_limits<uint64_t>::max()))
    );
}

USERVER_NAMESPACE_END
