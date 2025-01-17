#include <gmock/gmock.h>

#include <userver/ugrpc/tests/service_fixtures.hpp>
#include <userver/utest/using_namespace_userver.hpp>
#include <userver/utest/utest.hpp>

#include <samples/greeter_client.usrv.pb.hpp>

#include <greeter_client.hpp>
#include <greeter_service.hpp>

namespace {

/// [service fixture]
class GreeterServiceTest : public ugrpc::tests::ServiceFixtureBase {
protected:
    GreeterServiceTest() : service_(prefix_) {
        RegisterService(service_);
        StartServer();
    }

    ~GreeterServiceTest() override { StopServer(); }

private:
    const std::string prefix_{"Hello"};
    // We've made sure to separate the logic into samples::GreeterService that is
    // detached from the component system and only depends on things obtainable
    // in gtest tests.
    samples::GreeterService service_;
};
/// [service fixture]
}  // namespace

/// [service tests]
UTEST_F(GreeterServiceTest, SayHelloDirectCall) {
    const auto client = MakeClient<samples::api::GreeterServiceClient>();

    samples::api::GreetingRequest request;
    request.set_name("gtest");
    const auto response = client.SayHello(request);

    EXPECT_EQ(response.greeting(), "Hello, gtest!");
}

UTEST_F(GreeterServiceTest, SayHelloCustomClient) {
    // We've made sure to separate some logic into samples::GreeterClient that is
    // detached from the component system, it only needs the gRPC client, which we
    // can create in gtest tests.
    const samples::GreeterClient client{MakeClient<samples::api::GreeterServiceClient>()};
    const auto response = client.SayHello("gtest");
    EXPECT_EQ(response, "Hello, gtest!");
}
/// [service tests]

/// [service tests response stream]
UTEST_F(GreeterServiceTest, SayHelloResponseStreamCustomClient) {
    const samples::GreeterClient client{MakeClient<samples::api::GreeterServiceClient>()};
    const auto responses = client.SayHelloResponseStream("gtest");
    EXPECT_THAT(
        responses,
        testing::ElementsAre(
            "Hello, gtest!", "Hello, gtest!!", "Hello, gtest!!!", "Hello, gtest!!!!", "Hello, gtest!!!!!"
        )
    );
}
/// [service tests response stream]

/// [service tests request stream]
UTEST_F(GreeterServiceTest, SayHelloRequestStreamCustomClient) {
    const samples::GreeterClient client{MakeClient<samples::api::GreeterServiceClient>()};

    const std::vector<std::string_view> names = {"gtest", "!", "!", "!"};
    const auto response = client.SayHelloRequestStream(names);

    EXPECT_EQ(response, "Hello, gtest!!!");
}
/// [service tests request stream]

/// [service tests streams]
UTEST_F(GreeterServiceTest, SayHelloStreamsCustomClient) {
    const samples::GreeterClient client{MakeClient<samples::api::GreeterServiceClient>()};

    const std::vector<std::string_view> names = {"gtest", "!", "!", "!"};
    const auto responses = client.SayHelloStreams(names);
    EXPECT_THAT(responses, testing::ElementsAre("Hello, gtest", "Hello, gtest!", "Hello, gtest!!", "Hello, gtest!!!"));
}
/// [service tests streams]

/// [client tests]
namespace {

class GreeterMock final : public samples::api::GreeterServiceBase {
public:
    SayHelloResult SayHello(CallContext& /*context*/, ::samples::api::GreetingRequest&& /*request*/) override {
        samples::api::GreetingResponse response;
        response.set_greeting("Mocked response");
        return response;
    }

    SayHelloResponseStreamResult SayHelloResponseStream(
        CallContext& context,
        ::samples::api::GreetingRequest&& request,
        SayHelloResponseStreamWriter& writer
    ) override;
    SayHelloRequestStreamResult SayHelloRequestStream(CallContext& context, SayHelloRequestStreamReader& reader)
        override;
    SayHelloStreamsResult SayHelloStreams(CallContext& context, SayHelloStreamsReaderWriter& stream) override;
};

// Default-constructs GreeterMock.
using GreeterClientTest = ugrpc::tests::ServiceFixture<GreeterMock>;

}  // namespace

UTEST_F(GreeterClientTest, SayHelloMockedServiceCustomClient) {
    const samples::GreeterClient client{MakeClient<samples::api::GreeterServiceClient>()};
    const auto response = client.SayHello("gtest");
    EXPECT_EQ(response, "Mocked response");
}
/// [client tests]

/// [client tests response stream]
GreeterMock::SayHelloResponseStreamResult GreeterMock::SayHelloResponseStream(
    CallContext& /*context*/,
    ::samples::api::GreetingRequest&& /*request*/,
    SayHelloResponseStreamWriter& writer
) {
    samples::api::GreetingResponse response;
    std::string message = "Mocked response";

    int kCountSend = 5;
    for (auto i = 0; i < kCountSend; ++i) {
        message.push_back('!');
        response.set_greeting(grpc::string(message));
        writer.Write(response);
    }
    return grpc::Status::OK;
}

UTEST_F(GreeterClientTest, SayHelloResponseStreamMockedServiceCustomClient) {
    const samples::GreeterClient client{MakeClient<samples::api::GreeterServiceClient>()};
    const auto responses = client.SayHelloResponseStream("gtest");
    EXPECT_THAT(
        responses,
        testing::ElementsAre(
            "Mocked response!", "Mocked response!!", "Mocked response!!!", "Mocked response!!!!", "Mocked response!!!!!"
        )
    );
}
/// [client tests response stream]

/// [client tests request stream]
GreeterMock::SayHelloRequestStreamResult
GreeterMock::SayHelloRequestStream(CallContext& /*context*/, SayHelloRequestStreamReader& reader) {
    samples::api::GreetingResponse response;
    samples::api::GreetingRequest request;
    while (reader.Read(request)) {
    }
    response.set_greeting("Mocked response!!!");
    return response;
}

UTEST_F(GreeterClientTest, SayHelloRequestStreamMockedServiceCustomClient) {
    const samples::GreeterClient client{MakeClient<samples::api::GreeterServiceClient>()};
    const std::vector<std::string_view> names = {"gtest", "!", "!", "!"};
    auto response = client.SayHelloRequestStream(names);
    EXPECT_EQ(response, "Mocked response!!!");
}
/// [client tests request stream]

/// [client tests streams]
GreeterMock::SayHelloStreamsResult
GreeterMock::SayHelloStreams(CallContext& /*context*/, SayHelloStreamsReaderWriter& stream) {
    samples::api::GreetingResponse response;
    std::string message = "Mocked response";
    samples::api::GreetingRequest request;
    while (stream.Read(request)) {
        response.set_greeting(grpc::string(message));
        stream.Write(response);
        message.push_back('!');
    }
    return grpc::Status::OK;
}

UTEST_F(GreeterClientTest, SayHelloStreamsMockedServiceCustomClient) {
    const samples::GreeterClient client{MakeClient<samples::api::GreeterServiceClient>()};

    const std::vector<std::string_view> names = {"gtest", "!", "!", "!"};
    const auto responses = client.SayHelloStreams(names);

    EXPECT_THAT(
        responses,
        testing::ElementsAre("Mocked response", "Mocked response!", "Mocked response!!", "Mocked response!!!")
    );
}
/// [client tests streams]
