#include <storages/postgres/tests/util_pgtest.hpp>
#include <userver/storages/postgres/detail/db_data_type_name.hpp>
#include <userver/storages/postgres/io/enum_types.hpp>
#include <userver/utils/trivial_map.hpp>

USERVER_NAMESPACE_BEGIN

namespace pg = storages::postgres;
namespace io = pg::io;
namespace tt = io::traits;

namespace {

const std::string kCreateTestSchema = "create schema if not exists __pgtest";
const std::string kDropTestSchema = "drop schema if exists __pgtest cascade";

const std::string kCreateAnEnumType = R"~(
-- /// [User enum type postgres]
CREATE TYPE __pgtest.rainbow AS enum (
  'red', 'orange', 'yellow', 'green', 'cyan'
)
-- /// [User enum type postgres]
)~";

const std::string kSelectEnumValues = R"~(
select  'red'::__pgtest.rainbow as red,
        'orange'::__pgtest.rainbow as orange,
        'yellow'::__pgtest.rainbow as yellow,
        'green'::__pgtest.rainbow as green,
        'cyan'::__pgtest.rainbow as cyan
)~";

}  // anonymous namespace

/*! [User enum type cpp] */
enum class Rainbow { kRed, kOrange, kYellow, kGreen, kCyan };

// This specialization MUST go to the header together with the mapped type
template <>
struct storages::postgres::io::CppToUserPg<Rainbow> {
    static constexpr DBTypeName postgres_name = "__pgtest.rainbow";
    static constexpr USERVER_NAMESPACE::utils::TrivialBiMap enumerators = [](auto selector) {
        return selector()
            .Case("red", Rainbow::kRed)
            .Case("orange", Rainbow::kOrange)
            .Case("yellow", Rainbow::kYellow)
            .Case("green", Rainbow::kGreen)
            .Case("cyan", Rainbow::kCyan);
    };
};
/*! [User enum type cpp] */

// This data type is for testing a data type that is used only for reading
enum class RainbowRO { kRed, kOrange, kYellow, kGreen, kCyan };

enum class AnotherRainbowRO { kRed, kOrange, kYellow, kGreen, kCyan };

/*! [User enum type cpp2] */
enum class AnotherRainbow { kRed, kOrange, kYellow, kGreen, kCyan };
// This specialization MUST go to the header together with the mapped type
template <>
struct storages::postgres::io::CppToUserPg<AnotherRainbow> : storages::postgres::io::EnumMappingBase<AnotherRainbow> {
    static constexpr DBTypeName postgres_name = "__pgtest.rainbow";
    static constexpr Enumerator enumerators[]{
        {EnumType::kRed, "red"},
        {EnumType::kOrange, "orange"},
        {EnumType::kYellow, "yellow"},
        {EnumType::kGreen, "green"},
        {EnumType::kCyan, "cyan"},
    };
};
/*! [User enum type cpp2] */

// Reopen the namespace not to get to the code snippet
namespace storages::postgres::io {

template <>
struct CppToUserPg<RainbowRO> : EnumMappingBase<RainbowRO> {
    static constexpr DBTypeName postgres_name = "__pgtest.rainbow";
    static constexpr Enumerator enumerators[]{
        {EnumType::kRed, "red"},
        {EnumType::kOrange, "orange"},
        {EnumType::kYellow, "yellow"},
        {EnumType::kGreen, "green"},
        {EnumType::kCyan, "cyan"}};
};

template <>
struct CppToUserPg<AnotherRainbowRO> : EnumMappingBase<AnotherRainbowRO> {
    static constexpr DBTypeName postgres_name = "__pgtest.rainbow";
    static constexpr USERVER_NAMESPACE::utils::TrivialBiMap enumerators = [](auto selector) {
        return selector()
            .Case("red", AnotherRainbowRO::kRed)
            .Case("orange", AnotherRainbowRO::kOrange)
            .Case("yellow", AnotherRainbowRO::kYellow)
            .Case("green", AnotherRainbowRO::kGreen)
            .Case("cyan", AnotherRainbowRO::kCyan);
    };
};

namespace traits {

// To ensure it is never written to a buffer
template <>
struct HasFormatter<RainbowRO> : std::false_type {};

template <>
struct HasFormatter<AnotherRainbowRO> : std::false_type {};

}  // namespace traits

}  // namespace storages::postgres::io

namespace static_test {

template <typename T>
struct Checker {
    static_assert(tt::IsMappedToPg<T>());
    static_assert(io::detail::EnumerationMap<T>::size == 5);

    static_assert(tt::kHasParser<T>);
    static_assert(tt::kHasFormatter<T>);
};
template struct Checker<Rainbow>;
template struct Checker<AnotherRainbow>;

}  // namespace static_test

namespace {

TEST(PostgreIO, Enum) {
    using EnumMap = io::detail::EnumerationMap<Rainbow>;
    EXPECT_EQ("red", EnumMap::GetLiteral(Rainbow::kRed));
    EXPECT_EQ(Rainbow::kRed, EnumMap::GetEnumerator("red"));
}

TEST(PostgreIO, EnumTrivialMap) {
    using EnumMap = io::detail::EnumerationMap<AnotherRainbow>;
    EXPECT_EQ("red", EnumMap::GetLiteral(AnotherRainbow::kRed));
    EXPECT_EQ(AnotherRainbow::kRed, EnumMap::GetEnumerator("red"));
}

UTEST_P(PostgreConnection, EnumRoundtrip) {
    using EnumMap = io::detail::EnumerationMap<Rainbow>;
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    pg::ResultSet res{nullptr};
    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";

    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    UEXPECT_NO_THROW(GetConn()->Execute(kCreateAnEnumType)) << "Successfully create an enumeration type";
    UEXPECT_NO_THROW(GetConn()->ReloadUserTypes()) << "Reload user types";
    const auto& user_types = GetConn()->GetUserTypes();
    EXPECT_NE(0, io::CppToPg<Rainbow>::GetOid(user_types));

    UEXPECT_NO_THROW(res = GetConn()->Execute(kSelectEnumValues));
    for (auto f : res.Front()) {
        UEXPECT_NO_THROW(f.As<Rainbow>());
    }

    for (const auto& [literal, enumerator] : EnumMap::enumerators) {
        res = GetConn()->Execute("select $1", enumerator);
        EXPECT_EQ(enumerator, res[0][0].As<Rainbow>());
        EXPECT_EQ(literal, res[0][0].As<std::string_view>());
        // Test the data type that is used for reading only
        UEXPECT_NO_THROW(res[0][0].As<RainbowRO>()) << "Read a datatype that is never written to a Pg buffer";
    }
    {
        auto& connection = GetConn();
        /// [User enum type cpp usage]
        auto result = connection->Execute("select $1", Rainbow::kRed);
        EXPECT_EQ(Rainbow::kRed, result[0][0].As<Rainbow>());
        /// [User enum type cpp usage]
    }
    UEXPECT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
}

UTEST_P(PostgreConnection, EnumTrivialMapRoundtrip) {
    using EnumMap = io::detail::EnumerationMap<AnotherRainbow>;
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    pg::ResultSet res{nullptr};
    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";

    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    UEXPECT_NO_THROW(GetConn()->Execute(kCreateAnEnumType)) << "Successfully create an enumeration type";
    UEXPECT_NO_THROW(GetConn()->ReloadUserTypes()) << "Reload user types";
    const auto& user_types = GetConn()->GetUserTypes();
    EXPECT_NE(0, io::CppToPg<AnotherRainbow>::GetOid(user_types));

    UEXPECT_NO_THROW(res = GetConn()->Execute(kSelectEnumValues));
    for (auto f : res.Front()) {
        UEXPECT_NO_THROW(f.As<AnotherRainbow>());
    }

    static constexpr AnotherRainbow enumerators[]{
        AnotherRainbow::kRed,
        AnotherRainbow::kOrange,
        AnotherRainbow::kYellow,
        AnotherRainbow::kGreen,
        AnotherRainbow::kCyan};
    for (const auto& en : enumerators) {
        UEXPECT_NO_THROW(res = GetConn()->Execute("select $1", en));
        EXPECT_EQ(en, res[0][0].As<AnotherRainbow>());
        EXPECT_EQ(EnumMap::GetLiteral(en), res[0][0].As<std::string_view>());
        // Test the data type that is used for reading only
        UEXPECT_NO_THROW(res[0][0].As<AnotherRainbowRO>()) << "Read a datatype that is never written to a Pg buffer";
    }

    UEXPECT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
}

}  // namespace

/// [autogenerated enum type postgres]
// Autogenerated enum with formatters and parsers
enum class GeneratedRainbow { kRed, kOrange, kYellow, kGreen, kCyan };

GeneratedRainbow Parse(std::string_view value, formats::parse::To<GeneratedRainbow>);
std::string ToString(GeneratedRainbow value);
/// [autogenerated enum type postgres]

/// [autogenerated enum registration postgres]
template <>
struct storages::postgres::io::CppToUserPg<GeneratedRainbow> : EnumMappingBase<GeneratedRainbow> {
    static constexpr DBTypeName postgres_name = "__pgtest.rainbow";
    static constexpr auto enumerators = storages::postgres::io::Codegen{};
};
/// [autogenerated enum registration postgres]

UTEST_P(PostgreConnection, GeneratedEnumSample) {
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";
    UEXPECT_NO_THROW(GetConn()->Execute(kCreateAnEnumType)) << "Successfully create an enumeration type";

    /// [autogenerated enum usage postgres]
    auto res = GetConn()->Execute("select 'red'::__pgtest.rainbow");
    EXPECT_EQ(res[0][0].As<GeneratedRainbow>(), GeneratedRainbow::kRed);

    res = GetConn()->Execute("select $1", GeneratedRainbow::kYellow);
    EXPECT_EQ(res[0][0].As<GeneratedRainbow>(), GeneratedRainbow::kYellow);
    /// [autogenerated enum usage postgres]

    UEXPECT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
}

static constexpr USERVER_NAMESPACE::utils::TrivialBiMap kGeneratedMap{[](auto selector) {
    return selector()
        .Case("red", GeneratedRainbow::kRed)
        .Case("orange", GeneratedRainbow::kOrange)
        .Case("yellow", GeneratedRainbow::kYellow)
        .Case("green", GeneratedRainbow::kGreen)
        .Case("cyan", GeneratedRainbow::kCyan);
}};

GeneratedRainbow Parse(std::string_view value, formats::parse::To<GeneratedRainbow>) {
    return *kGeneratedMap.TryFind(value);
}

std::string ToString(GeneratedRainbow value) { return std::string{*kGeneratedMap.TryFind(value)}; }

UTEST_P(PostgreConnection, GeneratedEnumRoundtrip) {
    using EnumMap = io::detail::EnumerationMap<GeneratedRainbow>;
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    pg::ResultSet res{nullptr};
    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";

    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    UEXPECT_NO_THROW(GetConn()->Execute(kCreateAnEnumType)) << "Successfully create an enumeration type";
    UEXPECT_NO_THROW(GetConn()->ReloadUserTypes()) << "Reload user types";
    const auto& user_types = GetConn()->GetUserTypes();
    EXPECT_NE(0, io::CppToPg<GeneratedRainbow>::GetOid(user_types));

    UEXPECT_NO_THROW(res = GetConn()->Execute(kSelectEnumValues));
    for (auto f : res.Front()) {
        UEXPECT_NO_THROW(f.As<GeneratedRainbow>());
    }

    for (const auto& p : kGeneratedMap) {
        UEXPECT_NO_THROW(res = GetConn()->Execute("select $1", p.second));
        EXPECT_EQ(p.second, res[0][0].As<GeneratedRainbow>());
        EXPECT_EQ(EnumMap::GetLiteral(p.second), p.first);
        EXPECT_EQ(EnumMap::GetLiteral(p.second), res[0][0].As<std::string_view>());
    }

    UEXPECT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
}

USERVER_NAMESPACE_END
