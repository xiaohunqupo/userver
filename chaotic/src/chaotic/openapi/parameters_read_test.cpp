#include <userver/utest/utest.hpp>

#include <userver/chaotic/openapi/parameters_read.hpp>
#include <userver/server/http/http_request_builder.hpp>
#include <userver/utils/algo.hpp>

USERVER_NAMESPACE_BEGIN

namespace co = chaotic::openapi;

UTEST(OpenapiParametersRead, SourceHttpRequest) {
    auto request = server::http::HttpRequestBuilder()
                       .AddRequestArg("foo", "bar")
                       .AddRequestArg("foo", "baz")
                       .AddHeader("foo", "header")
                       .AddHeader("Cookie", "foo=cookie")
                       .SetPathArgs({{"foo", "path"}})
                       .Build();
    const auto& source = *request;

    static constexpr co::Name kName{"foo"};

    auto query_multi = co::ReadParameter<co::TrivialParameter<co::In::kQueryExplode, kName, std::string>>(source);
    auto query = co::ReadParameter<co::TrivialParameter<co::In::kQuery, kName, std::string>>(source);
    auto path = co::ReadParameter<co::TrivialParameter<co::In::kPath, kName, std::string>>(source);
    auto header = co::ReadParameter<co::TrivialParameter<co::In::kHeader, kName, std::string>>(source);
    auto cookie = co::ReadParameter<co::TrivialParameter<co::In::kCookie, kName, std::string>>(source);

    EXPECT_EQ(query_multi, (std::vector<std::string>{"bar", "baz"}));
    EXPECT_EQ(query, "bar");
    EXPECT_EQ(path, "path");
    EXPECT_EQ(header, "header");
    EXPECT_EQ(cookie, "cookie");
}

USERVER_NAMESPACE_END
