#include <userver/utest/utest.hpp>

#include <utility>
#include <vector>

#include <userver/engine/async.hpp>
#include <userver/engine/future_status.hpp>
#include <userver/engine/single_consumer_event.hpp>
#include <userver/engine/task/cancel.hpp>
#include <userver/engine/task/task_with_result.hpp>
#include <userver/utils/algo.hpp>

#include <tests/unit_test_client.usrv.pb.hpp>
#include <tests/unit_test_service.usrv.pb.hpp>
#include <userver/ugrpc/tests/service_fixtures.hpp>

using namespace std::chrono_literals;

USERVER_NAMESPACE_BEGIN

namespace {

class UnitTestService final : public sample::ugrpc::UnitTestServiceBase {
public:
    SayHelloResult SayHello(CallContext& /*context*/, sample::ugrpc::GreetingRequest&& request) override {
        sample::ugrpc::GreetingResponse response;
        response.set_name("Hello " + request.name());
        return response;
    }

    ReadManyResult ReadMany(
        CallContext& /*context*/,
        sample::ugrpc::StreamGreetingRequest&& request,
        ReadManyWriter& writer
    ) override {
        sample::ugrpc::StreamGreetingResponse response;
        response.set_name("Hello again " + request.name());
        for (int i = 0; i < request.number(); ++i) {
            response.set_number(i);
            writer.Write(response);
        }
        return grpc::Status::OK;
    }

    WriteManyResult WriteMany(CallContext& /*context*/, WriteManyReader& reader) override {
        sample::ugrpc::StreamGreetingRequest request;
        int count = 0;
        while (reader.Read(request)) {
            ++count;
        }
        sample::ugrpc::StreamGreetingResponse response;
        response.set_name("Hello");
        response.set_number(count);
        return response;
    }

    ChatResult Chat(CallContext& /*context*/, ChatReaderWriter& stream) override {
        sample::ugrpc::StreamGreetingRequest request;
        sample::ugrpc::StreamGreetingResponse response;
        int count = 0;
        while (stream.Read(request)) {
            ++count;
            response.set_number(count);
            response.set_name("Hello " + request.name());
            stream.Write(response);
        }
        return grpc::Status::OK;
    }
};

std::unique_ptr<grpc::ClientContext> PrepareClientContext() {
    auto context = std::make_unique<grpc::ClientContext>();
    return context;
}

void ExpectCancelledStats(const utils::statistics::Snapshot& stats) {
    // The is_cancelled_ flag may not be set at the point of sending statistics.
    // However, if it is set, then it is 1.
    EXPECT_LE(stats.SingleMetric("cancelled").AsRate(), 1);

    EXPECT_EQ(stats.SingleMetric("eps").AsRate(), 0);
    EXPECT_EQ(stats.SingleMetric("rps").AsRate(), 1);
    EXPECT_EQ(stats.SingleMetric("network-error").AsRate(), 0);
    EXPECT_EQ(stats.SingleMetric("abandoned-error").AsRate(), 0);
}

}  // namespace

using GrpcClientCancel = ugrpc::tests::ServiceFixture<UnitTestService>;

UTEST_F(GrpcClientCancel, UnaryCall) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    {
        engine::current_task::GetCancellationToken().RequestCancel();

        sample::ugrpc::GreetingRequest out;
        out.set_name("userver");
        UEXPECT_THROW(client.SayHello(out, PrepareClientContext()), ugrpc::client::RpcCancelledError);
    }

    const auto stats =
        GetStatistics("grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/SayHello"}});
    EXPECT_EQ(stats.SingleMetricOptional("cancelled"), std::nullopt);
    EXPECT_EQ(stats.SingleMetricOptional("rps"), std::nullopt);
}

UTEST_F(GrpcClientCancel, AsyncUnaryRPC) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    {
        engine::current_task::GetCancellationToken().RequestCancel();

        sample::ugrpc::GreetingRequest out;
        out.set_name("userver");
        UEXPECT_THROW((void)client.AsyncSayHello(out, PrepareClientContext()), ugrpc::client::RpcCancelledError);
    }

    const auto stats =
        GetStatistics("grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/SayHello"}});
    EXPECT_EQ(stats.SingleMetricOptional("cancelled"), std::nullopt);
    EXPECT_EQ(stats.SingleMetricOptional("rps"), std::nullopt);
}

UTEST_F(GrpcClientCancel, UnaryFinish) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    {
        sample::ugrpc::GreetingRequest out;
        out.set_name("userver");
        auto future = client.AsyncSayHello(out, PrepareClientContext());

        engine::current_task::GetCancellationToken().RequestCancel();

        sample::ugrpc::GreetingResponse in;
        UEXPECT_THROW(in = future.Get(), ugrpc::client::RpcCancelledError);
    }

    const auto stats =
        GetStatistics("grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/SayHello"}});
    ExpectCancelledStats(stats);
}

UTEST_F(GrpcClientCancel, InputStreamRead) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    {
        sample::ugrpc::StreamGreetingRequest out;
        out.set_name("userver");
        out.set_number(1);

        auto is = client.ReadMany(out, PrepareClientContext());

        engine::current_task::GetCancellationToken().RequestCancel();

        sample::ugrpc::StreamGreetingResponse in;
        UEXPECT_THROW([[maybe_unused]] auto ok = is.Read(in), ugrpc::client::RpcCancelledError);
    }

    const auto stats =
        GetStatistics("grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/ReadMany"}});
    ExpectCancelledStats(stats);
}

UTEST_F(GrpcClientCancel, InputStreamCall) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    {
        sample::ugrpc::StreamGreetingRequest out;
        out.set_name("userver");
        out.set_number(1);

        engine::current_task::GetCancellationToken().RequestCancel();

        UEXPECT_THROW(auto is = client.ReadMany(out, PrepareClientContext()), ugrpc::client::RpcCancelledError);
    }

    const auto stats =
        GetStatistics("grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/ReadMany"}});
    EXPECT_EQ(stats.SingleMetricOptional("cancelled"), std::nullopt);
    EXPECT_EQ(stats.SingleMetricOptional("rps"), std::nullopt);
}

UTEST_F(GrpcClientCancel, OutputStreamCall) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    {
        engine::current_task::GetCancellationToken().RequestCancel();

        UEXPECT_THROW(auto os = client.WriteMany(PrepareClientContext()), ugrpc::client::RpcCancelledError);
    }

    const auto stats =
        GetStatistics("grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/WriteMany"}});
    EXPECT_EQ(stats.SingleMetricOptional("cancelled"), std::nullopt);
    EXPECT_EQ(stats.SingleMetricOptional("rps"), std::nullopt);
}

UTEST_F(GrpcClientCancel, OutputStreamWrite) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    {
        auto os = client.WriteMany(PrepareClientContext());

        engine::current_task::GetCancellationToken().RequestCancel();

        sample::ugrpc::StreamGreetingRequest out;
        out.set_name("userver");
        out.set_number(1);
        EXPECT_FALSE(os.Write(out));
    }

    const auto stats =
        GetStatistics("grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/WriteMany"}});
    ExpectCancelledStats(stats);
}

UTEST_F(GrpcClientCancel, OutputStreamFinish) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    {
        auto os = client.WriteMany(PrepareClientContext());

        sample::ugrpc::StreamGreetingRequest out;
        out.set_name("userver");
        out.set_number(1);
        EXPECT_TRUE(os.Write(out));

        engine::current_task::GetCancellationToken().RequestCancel();

        UEXPECT_THROW(os.Finish(), ugrpc::client::RpcCancelledError);
    }

    const auto stats =
        GetStatistics("grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/WriteMany"}});
    ExpectCancelledStats(stats);
}

UTEST_F(GrpcClientCancel, BidirectionalStreamCall) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    {
        engine::current_task::GetCancellationToken().RequestCancel();

        UEXPECT_THROW([[maybe_unused]] auto bs = client.Chat(PrepareClientContext()), ugrpc::client::RpcCancelledError);
    }

    const auto stats =
        GetStatistics("grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/Chat"}});
    EXPECT_EQ(stats.SingleMetricOptional("cancelled"), std::nullopt);
    EXPECT_EQ(stats.SingleMetricOptional("rps"), std::nullopt);
}

UTEST_F(GrpcClientCancel, BidirectionalStreamRead) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    {
        auto bs = client.Chat(PrepareClientContext());

        engine::current_task::GetCancellationToken().RequestCancel();

        sample::ugrpc::StreamGreetingResponse in;
        EXPECT_FALSE(bs.Read(in));
    }

    const auto stats =
        GetStatistics("grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/Chat"}});
    ExpectCancelledStats(stats);
}

UTEST_F(GrpcClientCancel, BidirectionalStreamWrite) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    {
        auto bs = client.Chat(PrepareClientContext());

        engine::current_task::GetCancellationToken().RequestCancel();

        sample::ugrpc::StreamGreetingRequest out{};
        EXPECT_FALSE(bs.Write(out));
    }

    const auto stats =
        GetStatistics("grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/Chat"}});
    ExpectCancelledStats(stats);
}

UTEST_F(GrpcClientCancel, BidirectionalStreamWritesDone) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    {
        auto bs = client.Chat(PrepareClientContext());

        engine::current_task::GetCancellationToken().RequestCancel();

        EXPECT_FALSE(bs.WritesDone());
    }

    const auto stats =
        GetStatistics("grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/Chat"}});
    ExpectCancelledStats(stats);
}

USERVER_NAMESPACE_END
