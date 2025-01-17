#include <storages/postgres/tests/util_pgtest.hpp>
#include <userver/storages/postgres/io/composite_types.hpp>

#include <userver/storages/postgres/io/bitstring.hpp>

// intentionally declared in global namespace
struct PairForUnknownBufferCategoryExceptionReadabilityTest {
    int x;
    int y;
};

USERVER_NAMESPACE_BEGIN

namespace pg = storages::postgres;
namespace io = pg::io;
namespace tt = io::traits;

namespace {

constexpr const char* const kSchemaName = "__pgtest";
const std::string kCreateTestSchema = "create schema if not exists __pgtest";
const std::string kDropTestSchema = "drop schema if exists __pgtest cascade";

constexpr pg::DBTypeName kCompositeName{kSchemaName, "foobar"};
const std::string kCreateACompositeType = R"~(
create type __pgtest.foobar as (
  i integer,
  s text,
  d double precision,
  a integer[],
  v varchar[]
))~";

const std::string kCreateCompositeOfComposites = R"~(
create type __pgtest.foobars as (
  f __pgtest.foobar[]
))~";

const std::string kCreateDomain = R"~(
create domain __pgtest.positive as integer check(value > 0)
)~";

const std::string kCreateCompositeWithDomain = R"~(
create type __pgtest.with_domain as (
    v __pgtest.positive
))~";

const std::string kDropSomeFields = R"~(
alter type __pgtest.foobar
  drop attribute i,
  drop attribute d,
  drop attribute v
)~";

const std::string kCreateCompositeWithArray = R"~(
create type __pgtest.with_array as (
    s integer[]
))~";

}  // namespace

namespace pgtest {

struct FooBar {
    int i{};
    std::string s;
    double d{};
    std::vector<int> a;
    std::vector<std::string> v;

    bool operator==(const FooBar& rhs) const {
        return i == rhs.i && s == rhs.s && d == rhs.d && a == rhs.a && v == rhs.v;
    }
};

struct FooBarWithOptionalFields {
    std::optional<int> i;
    std::optional<std::string> s;
    std::optional<double> d;
    std::vector<int> a;
    std::vector<std::string> v;

    bool operator==(const FooBarWithOptionalFields& rhs) const {
        return i == rhs.i && s == rhs.s && d == rhs.d && a == rhs.a && v == rhs.v;
    }
};

using FooBarOpt = std::optional<FooBar>;

class FooClass {
    int i{};
    std::string s;
    double d{};
    std::vector<int> a;
    std::vector<std::string> v;

public:
    FooClass() = default;
    explicit FooClass(int x) : i(x), s(std::to_string(x)), d(x), a{i}, v{s} {}

    // Only non-const version of Introspect() is used by the uPostgres driver
    auto Introspect() { return std::tie(i, s, d, a, v); }

    auto GetI() const { return i; }
    auto GetS() const { return s; }
    auto GetD() const { return d; }
    auto GetA() const { return a; }
    auto GetV() const { return v; }
};

using FooTuple = std::tuple<int, std::string, double, std::vector<int>, std::vector<std::string>>;

struct BunchOfFoo {
    std::vector<FooBar> foobars;

    bool operator==(const BunchOfFoo& rhs) const { return foobars == rhs.foobars; }
};

}  // namespace pgtest

namespace pgtest {

// These declarations are separate from the others as they shouldn't get into
// the code snippet for documentation.
struct NoUseInWrite {
    int i;
    std::string s;
    double d;
    std::vector<int> a;
    std::vector<std::string> v;
};

struct NoUserMapping {
    int i{};
    std::string s;
    double d{};
    std::vector<int> a;
    std::vector<std::string> v;

    bool operator==(const NoUserMapping& rhs) const {
        return i == rhs.i && s == rhs.s && d == rhs.d && a == rhs.a && v == rhs.v;
    }
    bool operator==(const FooBar& rhs) const {
        return i == rhs.i && s == rhs.s && d == rhs.d && a == rhs.a && v == rhs.v;
    }
};

struct NoUserMappingBunch {
    std::vector<NoUserMapping> elements;

    bool operator==(const NoUserMappingBunch& rhs) const { return elements == rhs.elements; }
    bool operator==(const BunchOfFoo& rhs) const {
        if (elements.size() != rhs.foobars.size()) return false;
        for (size_t i = 0; i < elements.size(); ++i) {
            if (!(elements[i] == rhs.foobars[i])) return false;
        }
        return true;
    }
};

struct WithDomain {
    int v;
};

struct FooBarWithSomeFieldsDropped {
    std::string s;
    std::vector<int> a;

    bool operator==(const FooBarWithSomeFieldsDropped& rhs) const { return s == rhs.s && a == rhs.a; }
};

struct WithUnorderedSet {
    std::unordered_set<int> s;

    bool operator==(const WithUnorderedSet& rhs) const { return s == rhs.s; }
};

struct User {
    int id{};
    std::bitset<4> status{};
};

}  // namespace pgtest

/*! [User type mapping] */
namespace storages::postgres::io {

// This specialization MUST go to the header together with the mapped type
template <>
struct CppToUserPg<pgtest::FooBar> {
    static constexpr DBTypeName postgres_name = kCompositeName;
};

// This specialization MUST go to the header together with the mapped type
template <>
struct CppToUserPg<pgtest::FooClass> {
    static constexpr DBTypeName postgres_name = kCompositeName;
};

// This specialization MUST go to the header together with the mapped type
template <>
struct CppToUserPg<pgtest::FooTuple> {
    static constexpr DBTypeName postgres_name = kCompositeName;
};

template <>
struct CppToUserPg<pgtest::BunchOfFoo> {
    static constexpr DBTypeName postgres_name = "__pgtest.foobars";
};

}  // namespace storages::postgres::io
/*! [User type mapping] */

namespace storages::postgres::io {

// These mappings are separate from the others as they shouldn't get into the
// code snippet for documentation.
template <>
struct CppToUserPg<pgtest::NoUseInWrite> {
    static constexpr DBTypeName postgres_name = kCompositeName;
};

template <>
struct CppToUserPg<pgtest::FooBarWithOptionalFields> {
    static constexpr DBTypeName postgres_name = kCompositeName;
};

template <>
struct CppToUserPg<pgtest::WithDomain> {
    static constexpr DBTypeName postgres_name = "__pgtest.with_domain";
};

template <>
struct CppToUserPg<pgtest::FooBarWithSomeFieldsDropped> {
    static constexpr DBTypeName postgres_name = kCompositeName;
};

template <>
struct CppToUserPg<pgtest::WithUnorderedSet> {
    static constexpr DBTypeName postgres_name = "__pgtest.with_array";
};

template <>
struct CppToUserPg<pgtest::User> {
    static constexpr DBTypeName postgres_name = "__pgtest.user";
};

}  // namespace storages::postgres::io

namespace static_test {

static_assert(io::traits::TupleHasParsers<pgtest::FooTuple>::value);
static_assert(tt::detail::AssertHasCompositeFormatters<pgtest::FooTuple>());
static_assert(tt::detail::AssertHasCompositeFormatters<pgtest::FooBar>());
static_assert(tt::detail::AssertHasCompositeFormatters<pgtest::FooBarWithOptionalFields>());
static_assert(tt::detail::AssertHasCompositeFormatters<pgtest::FooClass>());
static_assert(tt::detail::AssertHasCompositeFormatters<pgtest::NoUseInWrite>());
static_assert(tt::detail::AssertHasCompositeFormatters<pgtest::NoUserMapping>());
static_assert(tt::detail::AssertHasCompositeFormatters<pgtest::WithUnorderedSet>());

static_assert(io::traits::TupleHasFormatters<pgtest::FooTuple>::value);
static_assert(tt::detail::AssertHasCompositeFormatters<pgtest::FooTuple>());
static_assert(tt::detail::AssertHasCompositeFormatters<pgtest::FooBar>());
static_assert(tt::detail::AssertHasCompositeFormatters<pgtest::FooClass>());

static_assert(tt::kHasParser<pgtest::BunchOfFoo>);
static_assert(tt::kHasFormatter<pgtest::BunchOfFoo>);

static_assert(tt::kHasParser<pgtest::NoUserMapping>);
static_assert(!tt::kHasFormatter<pgtest::NoUserMapping>);

static_assert(tt::kTypeBufferCategory<pgtest::FooTuple> == io::BufferCategory::kCompositeBuffer);
static_assert(tt::kTypeBufferCategory<pgtest::FooBar> == io::BufferCategory::kCompositeBuffer);
static_assert(tt::kTypeBufferCategory<pgtest::FooBarWithOptionalFields> == io::BufferCategory::kCompositeBuffer);
static_assert(tt::kTypeBufferCategory<pgtest::FooClass> == io::BufferCategory::kCompositeBuffer);
static_assert(tt::kTypeBufferCategory<pgtest::BunchOfFoo> == io::BufferCategory::kCompositeBuffer);
static_assert(tt::kTypeBufferCategory<pgtest::NoUseInWrite> == io::BufferCategory::kCompositeBuffer);
static_assert(tt::kTypeBufferCategory<pgtest::NoUserMapping> == io::BufferCategory::kCompositeBuffer);
static_assert(tt::kTypeBufferCategory<pgtest::WithUnorderedSet> == io::BufferCategory::kCompositeBuffer);

}  // namespace static_test

namespace {

UTEST_P(PostgreConnection, CompositeTypeRoundtrip) {
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    pg::ResultSet res{nullptr};
    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    UEXPECT_NO_THROW(GetConn()->Execute(kCreateACompositeType)) << "Successfully create a composite type";
    UEXPECT_NO_THROW(GetConn()->Execute(kCreateCompositeOfComposites)) << "Successfully create composite of composites";

    // The datatypes are expected to be automatically reloaded
    UEXPECT_NO_THROW(
        res = GetConn()->Execute("select ROW(42, 'foobar', 3.14, ARRAY[-1, 0, 1], "
                                 "ARRAY['a', 'b', 'c'])::__pgtest.foobar")
    );
    const std::vector<int> expected_int_vector{-1, 0, 1};
    const std::vector<std::string> expected_str_vector{"a", "b", "c"};

    ASSERT_FALSE(res.IsEmpty());

    pgtest::FooBar fb;
    UEXPECT_NO_THROW(res[0].To(fb));
    UEXPECT_THROW(res[0][0].As<std::string>(), pg::InvalidParserCategory);
    EXPECT_EQ(42, fb.i);
    EXPECT_EQ("foobar", fb.s);
    EXPECT_EQ(3.14, fb.d);
    EXPECT_EQ(expected_int_vector, fb.a);
    EXPECT_EQ(expected_str_vector, fb.v);

    pgtest::FooTuple ft;
    UEXPECT_NO_THROW(res[0].To(ft));
    EXPECT_EQ(42, std::get<0>(ft));
    EXPECT_EQ("foobar", std::get<1>(ft));
    EXPECT_EQ(3.14, std::get<2>(ft));
    EXPECT_EQ(expected_int_vector, std::get<3>(ft));
    EXPECT_EQ(expected_str_vector, std::get<4>(ft));

    pgtest::FooClass fc;
    UEXPECT_NO_THROW(res[0].To(fc));
    EXPECT_EQ(42, fc.GetI());
    EXPECT_EQ("foobar", fc.GetS());
    EXPECT_EQ(3.14, fc.GetD());
    EXPECT_EQ(expected_int_vector, fc.GetA());
    EXPECT_EQ(expected_str_vector, fc.GetV());

    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1 as foo", fb));
    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1 as foo", ft));
    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1 as foo", fc));

    using FooVector = std::vector<pgtest::FooBar>;
    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1 as array_of_foo", FooVector{fb, fb, fb}));

    ASSERT_FALSE(res.IsEmpty());
    UEXPECT_THROW(res[0][0].As<pgtest::FooBar>(), pg::InvalidParserCategory);
    UEXPECT_THROW(res[0][0].As<std::string>(), pg::InvalidParserCategory);
    EXPECT_EQ((FooVector{fb, fb, fb}), res[0].As<FooVector>());

    pgtest::BunchOfFoo bf{{fb, fb, fb}};
    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1 as bunch", bf));
    ASSERT_FALSE(res.IsEmpty());
    pgtest::BunchOfFoo bf1;
    UEXPECT_NO_THROW(res[0].To(bf1));
    EXPECT_EQ(bf, bf1);
    EXPECT_EQ(bf, res[0].As<pgtest::BunchOfFoo>());

    // Unwrapping composite structure to a row
    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1.*", bf));
    ASSERT_FALSE(res.IsEmpty());
    UEXPECT_NO_THROW(res[0].To(bf1, pg::kRowTag));
    EXPECT_EQ(bf, bf1);
    EXPECT_EQ(bf, res[0].As<pgtest::BunchOfFoo>(pg::kRowTag));

    EXPECT_ANY_THROW(res[0][0].To(bf1));

    // Using a mapped type only for reading
    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1 as foo", fb));
    UEXPECT_NO_THROW(res.AsContainer<std::vector<pgtest::NoUseInWrite>>())
        << "A type that is not used for writing query parameter buffers must be "
           "available for reading";

    UEXPECT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
}

UTEST_P(PostgreConnection, CompositeWithOptionalFieldsRoundtrip) {
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    pg::ResultSet res{nullptr};
    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    UEXPECT_NO_THROW(GetConn()->Execute(kCreateACompositeType)) << "Successfully create a composite type";
    // Auto reload doesn't work for outgoing types
    UASSERT_NO_THROW(GetConn()->ReloadUserTypes());

    pgtest::FooBarWithOptionalFields fb;
    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1", fb));
    EXPECT_EQ(fb, res.AsSingleRow<pgtest::FooBarWithOptionalFields>(pg::kFieldTag));

    UEXPECT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
}

UTEST_P(PostgreConnection, CompositeReadingSample) {
    CheckConnection(GetConn());

    /// [FieldTagSippet]
    auto result = GetConn()->Execute("select ROW($1, $2)", 42, 3.14);

    using TupleType = std::tuple<int, double>;
    auto tuple = result.AsSingleRow<TupleType>(storages::postgres::kFieldTag);
    EXPECT_EQ(std::get<0>(tuple), 42);
    /// [FieldTagSippet]
}

UTEST_P(PostgreConnection, OptionalCompositeTypeRoundtrip) {
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    pg::ResultSet res{nullptr};
    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    UEXPECT_NO_THROW(GetConn()->Execute(kCreateACompositeType)) << "Successfully create a composite type";

    UEXPECT_NO_THROW(
        res = GetConn()->Execute("select ROW(42, 'foobar', 3.14, ARRAY[-1, 0, 1], "
                                 "ARRAY['a', 'b', 'c'])::__pgtest.foobar")
    );
    {
        auto foo_opt = res.Front().As<pgtest::FooBarOpt>();
        EXPECT_TRUE(!!foo_opt) << "Non-empty optional result expected";
    }

    UEXPECT_NO_THROW(res = GetConn()->Execute("select null::__pgtest.foobar"));
    {
        auto foo_opt = res.Front().As<pgtest::FooBarOpt>();
        EXPECT_TRUE(!foo_opt) << "Empty optional result expected";
    }

    UEXPECT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
}

UTEST_P(PostgreConnection, CompositeTypeWithDomainRoundtrip) {
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    pg::ResultSet res{nullptr};
    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    UASSERT_NO_THROW(GetConn()->Execute(kCreateDomain)) << "Create domain";
    UASSERT_NO_THROW(GetConn()->Execute(kCreateCompositeWithDomain)) << "Create composite";
    // Auto reload doesn't work for outgoing types
    UASSERT_NO_THROW(GetConn()->ReloadUserTypes());

    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1::__pgtest.with_domain", pgtest::WithDomain{1}));
    UEXPECT_NO_THROW(res.AsSingleRow<pgtest::WithDomain>(pg::kFieldTag));
    auto v = res.AsSingleRow<pgtest::WithDomain>(pg::kFieldTag);
    EXPECT_EQ(1, v.v);
}

UTEST_P(PostgreConnection, CompositeTypeRoundtripAsRecord) {
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    pg::ResultSet res{nullptr};
    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    UEXPECT_NO_THROW(GetConn()->Execute(kCreateACompositeType)) << "Successfully create a composite type";
    UEXPECT_NO_THROW(GetConn()->Execute(kCreateCompositeOfComposites)) << "Successfully create composite of composites";

    UEXPECT_NO_THROW(
        res = GetConn()->Execute("SELECT fb.* FROM (SELECT ROW(42, 'foobar', 3.14, ARRAY[-1, 0, 1], "
                                 "ARRAY['a', 'b', 'c'])::__pgtest.foobar) fb")
    );
    const std::vector<int> expected_int_vector{-1, 0, 1};
    const std::vector<std::string> expected_str_vector{"a", "b", "c"};

    ASSERT_FALSE(res.IsEmpty());

    pgtest::FooBar fb;
    res[0].To(fb);
    UEXPECT_NO_THROW(res[0].To(fb));
    UEXPECT_THROW(res[0][0].As<std::string>(), pg::InvalidParserCategory);
    EXPECT_EQ(42, fb.i);
    EXPECT_EQ("foobar", fb.s);
    EXPECT_EQ(3.14, fb.d);
    EXPECT_EQ(expected_int_vector, fb.a);
    EXPECT_EQ(expected_str_vector, fb.v);

    pgtest::FooTuple ft;
    UEXPECT_NO_THROW(res[0].To(ft));
    EXPECT_EQ(42, std::get<0>(ft));
    EXPECT_EQ("foobar", std::get<1>(ft));
    EXPECT_EQ(3.14, std::get<2>(ft));
    EXPECT_EQ(expected_int_vector, std::get<3>(ft));
    EXPECT_EQ(expected_str_vector, std::get<4>(ft));

    pgtest::FooClass fc;
    UEXPECT_NO_THROW(res[0].To(fc));
    EXPECT_EQ(42, fc.GetI());
    EXPECT_EQ("foobar", fc.GetS());
    EXPECT_EQ(3.14, fc.GetD());
    EXPECT_EQ(expected_int_vector, fc.GetA());
    EXPECT_EQ(expected_str_vector, fc.GetV());

    pgtest::NoUserMapping nm;
    UEXPECT_NO_THROW(res[0].To(nm));
    UEXPECT_THROW(res[0][0].As<std::string>(), pg::InvalidParserCategory);
    EXPECT_EQ(42, nm.i);
    EXPECT_EQ("foobar", nm.s);
    EXPECT_EQ(3.14, nm.d);
    EXPECT_EQ(expected_int_vector, nm.a);
    EXPECT_EQ(expected_str_vector, nm.v);

    UEXPECT_NO_THROW(res = GetConn()->Execute("SELECT ROW($1.*) AS record", fb));
    UEXPECT_NO_THROW(res = GetConn()->Execute("SELECT ROW($1.*) AS record", ft));
    UEXPECT_NO_THROW(res = GetConn()->Execute("SELECT ROW($1.*) AS record", fc));

    using FooVector = std::vector<pgtest::FooBar>;
    UEXPECT_NO_THROW(res = GetConn()->Execute("SELECT $1::record[] AS array_of_records", FooVector{fb, fb, fb}));

    ASSERT_FALSE(res.IsEmpty());
    UEXPECT_THROW(res[0][0].As<pgtest::FooBar>(), pg::InvalidParserCategory);
    UEXPECT_THROW(res[0][0].As<std::string>(), pg::InvalidParserCategory);
    EXPECT_EQ((FooVector{fb, fb, fb}), res[0].As<FooVector>());

    pgtest::BunchOfFoo bf{{fb, fb, fb}};
    UEXPECT_NO_THROW(res = GetConn()->Execute("SELECT ROW($1.f::record[]) AS bunch", bf));
    ASSERT_FALSE(res.IsEmpty());

    pgtest::BunchOfFoo bf1;
    UEXPECT_NO_THROW(res[0].To(bf1));
    EXPECT_EQ(bf, bf1);
    EXPECT_EQ(bf, res[0].As<pgtest::BunchOfFoo>());

    pgtest::NoUserMappingBunch bnm;
    UEXPECT_NO_THROW(res[0].To(bnm));
    EXPECT_EQ(bnm, bf);
    EXPECT_EQ(bnm, res[0].As<pgtest::NoUserMappingBunch>());

    // Unwrapping composite structure to a row
    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1.f::record[]", bf));
    ASSERT_FALSE(res.IsEmpty());
    UEXPECT_NO_THROW(res[0].To(bf1, pg::kRowTag));
    EXPECT_EQ(bf, bf1);
    EXPECT_EQ(bf, res[0].As<pgtest::BunchOfFoo>(pg::kRowTag));

    EXPECT_ANY_THROW(res[0][0].To(bf1));

    // Using a mapped type only for reading
    UEXPECT_NO_THROW(res = GetConn()->Execute("SELECT ROW($1.*) AS record", fb));
    UEXPECT_NO_THROW(res.AsContainer<std::vector<pgtest::NoUseInWrite>>())
        << "A type that is not used for writing query parameter buffers must be "
           "available for reading";

    UEXPECT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
}

UTEST_P(PostgreConnection, OptionalCompositeTypeRoundtripAsRecord) {
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    pg::ResultSet res{nullptr};
    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    UEXPECT_NO_THROW(GetConn()->Execute(kCreateACompositeType)) << "Successfully create a composite type";

    UEXPECT_NO_THROW(
        res = GetConn()->Execute("SELECT fb.* FROM (SELECT ROW(42, 'foobar', 3.14, ARRAY[-1, 0, 1], "
                                 "ARRAY['a', 'b', 'c'])::__pgtest.foobar) fb")
    );
    {
        auto foo_opt = res.Front().As<pgtest::FooBarOpt>();
        EXPECT_TRUE(!!foo_opt) << "Non-empty optional result expected";
    }

    UEXPECT_NO_THROW(res = GetConn()->Execute("select null::record"));
    {
        auto foo_opt = res.Front().As<pgtest::FooBarOpt>();
        EXPECT_TRUE(!foo_opt) << "Empty optional result expected";
    }

    UEXPECT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
}

// Please never use this in your code, this is only to check type loaders
UTEST_P(PostgreConnection, VariableRecordTypes) {
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    pg::ResultSet res{nullptr};
    UEXPECT_NO_THROW(
        res = GetConn()->Execute("WITH test AS (SELECT unnest(ARRAY[1, 2]) a)"
                                 "SELECT CASE WHEN a = 1 THEN ROW(42)"
                                 "WHEN a = 2 THEN ROW('str'::text) "
                                 "END FROM test")
    );
    ASSERT_EQ(2, res.Size());

    EXPECT_EQ(42, std::get<0>(res[0].As<std::tuple<int>>()));
    EXPECT_EQ("str", std::get<0>(res[1].As<std::tuple<std::string>>()));
}

// This is not exactly allowed as well, we just don't want to crash on legacy
UTEST_P(PostgreConnection, CompositeDroppedFields) {
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    UEXPECT_NO_THROW(GetConn()->Execute(kCreateACompositeType)) << "Successfully create a composite type";
    UEXPECT_NO_THROW(GetConn()->Execute(kDropSomeFields)) << "Drop some composite type fields";

    pg::ResultSet res{nullptr};
    // The datatypes are expected to be automatically reloaded
    UEXPECT_NO_THROW(res = GetConn()->Execute("select ROW('foobar', ARRAY[-1, 0, 1])::__pgtest.foobar"));
    const std::vector<int> expected_int_vector{-1, 0, 1};

    ASSERT_FALSE(res.IsEmpty());

    pgtest::FooBarWithSomeFieldsDropped fb;
    UEXPECT_NO_THROW(res[0].To(fb));
    EXPECT_EQ("foobar", fb.s);
    EXPECT_EQ(expected_int_vector, fb.a);

    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1", fb));
    EXPECT_EQ(res.AsSingleRow<pgtest::FooBarWithSomeFieldsDropped>(), fb);
}

UTEST_P(PostgreConnection, CompositeUnorderedSet) {
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    UEXPECT_NO_THROW(GetConn()->Execute(kCreateCompositeWithArray)) << "Successfully create a composite type";

    pg::ResultSet res{nullptr};
    UEXPECT_NO_THROW(res = GetConn()->Execute("select ROW(ARRAY[-1, 0, 1])::__pgtest.with_array"));
    const std::unordered_set<int> expected_int_set{-1, 0, 1};

    ASSERT_FALSE(res.IsEmpty());

    pgtest::WithUnorderedSet usc;
    UEXPECT_NO_THROW(res[0].To(usc));
    EXPECT_EQ(expected_int_set, usc.s);

    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1", usc));
    EXPECT_EQ(res.AsSingleRow<pgtest::WithUnorderedSet>(), usc);

    // TODO: remove after https://github.com/boostorg/pfr/pull/109
    // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.UndefReturn)
    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1.*", usc));
    EXPECT_EQ(res.AsSingleRow<pgtest::WithUnorderedSet>(pg::kRowTag), usc);
}

UTEST_P(PostgreConnection, CompositeTypeParseExceptionReadability) {
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";

    pg::ResultSet res{nullptr};
    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    UEXPECT_NO_THROW(GetConn()->Execute(kCreateACompositeType)) << "Successfully create a composite type";

    UEXPECT_NO_THROW(
        res = GetConn()->Execute("select ROW(42, 'foobar', 3.14, ARRAY[-1, 0, 1], "
                                 "ARRAY['a', 'b', 'c'])::__pgtest.foobar")
    );
    ASSERT_FALSE(res.IsEmpty());

    UEXPECT_THROW_MSG(
        res[0][0].As<std::string>(),
        pg::InvalidParserCategory,
        fmt::format(
            "Buffer category 'composite buffer' doesn't match the "
            "category of the parser 'plain buffer' for type '{}'. "
            "Consider using different variable type (not '{}') to store "
            "complex result or split result tuple with 'UNNEST' in SQL "
            "query. (ResultSet error while reading field #0 name `row`)",
            compiler::GetTypeName<std::string>(),
            compiler::GetTypeName<std::string>()
        )
    );
    UEXPECT_NO_THROW(res[0].As<pgtest::FooBar>());

    UEXPECT_NO_THROW(res = GetConn()->Execute("select 'foobar'"));
    UEXPECT_THROW_MSG(
        res[0][0].As<pgtest::FooBar>(),
        pg::InvalidParserCategory,
        fmt::format(
            "Buffer category 'plain buffer' doesn't match the category "
            "of the parser 'composite buffer' for type '{}'. "
            "Consider using different variable type (not '{}') to store result, "
            "passing storages::postgres::kRowTag to function args for "
            "this field or explicitly cast to expected type in SQL "
            "query. (ResultSet error while reading field #0 name `?column?`)",
            compiler::GetTypeName<pgtest::FooBar>(),
            compiler::GetTypeName<pgtest::FooBar>()
        )
    );
    UEXPECT_NO_THROW(res[0][0].As<std::string>());

    UEXPECT_NO_THROW(res = GetConn()->Execute("select $1.i, $1.s", pgtest::FooBar{}));
    ASSERT_FALSE(res.IsEmpty());
    UEXPECT_NO_THROW(res[0][1].As<std::string>());

    // Table name used as a type name:
    {
        UEXPECT_NO_THROW(GetConn()->Execute(R"~(
      CREATE TABLE __pgtest.user (
        id int NOT NULL,
        status bit(4) DEFAULT '0001'::"bit" NOT NULL
      )
    )~"));

        const auto searchQuery = storages::Query(R"(
      SELECT id, status
      FROM __pgtest.user
      WHERE id = $1.id and status = $1.status;
    )");

        UEXPECT_THROW_MSG(
            GetConn()->Execute(searchQuery, pgtest::User{1, 2}),
            storages::postgres::UserTypeError,
            fmt::format(
                "Type '__pgtest.user' was not created in database and "
                "because of that the '{}' could not be serialized. Forgot a "
                "migration or rolled it after the service started?",
                compiler::GetTypeName<pgtest::User>()
            )
        );
    }

    // Type is now created:
    {
        UEXPECT_NO_THROW(GetConn()->Execute("DROP TABLE __pgtest.user"));
        UEXPECT_NO_THROW(GetConn()->Execute(R"~(
      CREATE TYPE __pgtest.user as (
        id int,
        status bit  -- `bit(4)` is ignored here and the actual type is varbit
      )
    )~"));

        UEXPECT_NO_THROW(res = GetConn()->Execute(R"~(
      CREATE TABLE __pgtest.user_table (
        id int NOT NULL,
        status bit(4) DEFAULT '0001'::"bit" NOT NULL
      )
    )~"));

        const auto searchQuery = storages::Query(R"(
      SELECT id, status
      FROM __pgtest.user_table
      WHERE id = $1.id and status = $1.status;
    )");

        // Auto reload doesn't work for outgoing types
        UASSERT_NO_THROW(GetConn()->ReloadUserTypes());

        UEXPECT_THROW_MSG(
            GetConn()->Execute(searchQuery, pgtest::User{1, 2}),
            storages::postgres::UserTypeError,
            "Type mismatch for '__pgtest.user' field 'status'. In database the "
            "type is 'bit' (oid: 1560), user supplied type is 'varbit' (oid: "
            "1562)"
        );

        UEXPECT_NO_THROW(res = GetConn()->Execute(R"~(
      CREATE TABLE __pgtest.user_table_type (
        x __pgtest.user NOT NULL
      )
    )~"));

        UEXPECT_THROW_MSG(
            GetConn()->Execute("INSERT INTO __pgtest.user_table_type(x) VALUES($1)", pgtest::User{1, 2}),
            storages::postgres::UserTypeError,
            "Type mismatch for '__pgtest.user' field 'status'. In database the "
            "type is 'bit' (oid: 1560), user supplied type is 'varbit' (oid: "
            "1562)"
        );

        UEXPECT_THROW_MSG(
            GetConn()->Execute("SELECT $1::__pgtest.user", 1),
            storages::postgres::AccessRuleViolation,
            "cannot cast type integer to __pgtest."
        );
    }
    {
        UEXPECT_NO_THROW(GetConn()->Execute("create type __pgtest.no_cpp_type as (f int)"));

        UEXPECT_THROW_MSG(
            GetConn()->Execute("create type __pgtest.no_cpp_type as (f int)"),
            storages::postgres::AccessRuleViolation,
            "already exists"
        );

        UEXPECT_THROW_MSG(
            GetConn()->Execute("SELECT $1::__pgtest.no_cpp_type", 1),
            storages::postgres::AccessRuleViolation,
            "cannot cast type integer to __pgtest."
        );
        UEXPECT_THROW_MSG(
            GetConn()->Execute("SELECT ROW($1)::__pgtest.no_cpp_type", 1),
            storages::postgres::NoBinaryParser,
            "PostgreSQL result set field 'row' of a composite type "
            "'__pgtest.no_cpp_type' (oid: "
        );
    }
}

// Following tests abort in debug
#ifdef NDEBUG

UTEST_P(PostgreConnection, InvalidInputBufferSizeExceptionReadability) {
    CheckConnection(GetConn());
    ASSERT_FALSE(GetConn()->IsReadOnly()) << "Expect a read-write connection";
    UASSERT_NO_THROW(GetConn()->Execute(kDropTestSchema)) << "Drop schema";
    UASSERT_NO_THROW(GetConn()->Execute(kCreateTestSchema)) << "Create schema";

    {
        UEXPECT_NO_THROW(GetConn()->Execute(R"~(
          CREATE TABLE __pgtest.numeric_problem (
                user_id serial NOT NULL PRIMARY KEY,
                timestamp bigint,
                balance_increment numeric,
                description varchar(50)
          )
        )~"));
        UEXPECT_NO_THROW(GetConn()->Execute(R"~(
          INSERT INTO __pgtest.numeric_problem(user_id, timestamp, balance_increment, description)
          VALUES ('1', 123, 2.0, 'nope'),
                 ('42', 1234, 4.0, 'nope 2')
        )~"));

        UEXPECT_NO_THROW(GetConn()->Execute(R"~(
            create or replace function the_function(
              user_id_ varchar(50)
            ) returns table(
                time_stamp bigint,
                balance_inc numeric,
                description_ varchar(50)
            ) as $$
            begin
             return query
                   select timestamp, balance_increment, description
                   from __pgtest.numeric_problem h
                   where h.user_id = user_id_::integer
                   order by h.timestamp;
            end;
            $$ language plpgsql;
        )~"));

        struct TransactionInfo final {
            long long timestamp;
            double balance_increment;
            std::string description;
        };

        auto result = GetConn()->Execute("SELECT time_stamp, balance_inc, description_ FROM the_function('42')");
        const auto set = result.AsSetOf<TransactionInfo>(storages::postgres::kRowTag);
        UEXPECT_THROW_MSG(
            set[0],
            storages::postgres::InvalidInputBufferSize,
            "Error while reading field #1 'balance_inc' "
            "which database type is 'numeric' (oid: 1700) as a C++ type 'double'. Refer to the 'Supported data types' "
            "in the documentation to make sure that the database type is actually representable as a C++ type "
            "'double'. Error details: Buffer size 10 is invalid for a floating point value type"
        );
    }
}

UTEST_P(PostgreConnection, UnknownBufferCategoryExceptionReadability) {
    auto result = GetConn()->Execute("SELECT ROW('fat & (rat | cat)'::tsquery, 1)");
    UEXPECT_THROW_MSG(
        result[0][0].As<PairForUnknownBufferCategoryExceptionReadabilityTest>(),
        storages::postgres::UnknownBufferCategory,
        "Database type is 'tsquery' (oid: 3615) and it is not representable as a C++ type 'int' within a C++ composite "
        "'PairForUnknownBufferCategoryExceptionReadabilityTest'. Refer to the 'Supported data types' in the "
        "documentation to find a proper C++ type."
    );
}

#endif

}  // namespace

USERVER_NAMESPACE_END
