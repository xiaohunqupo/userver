#include <userver/utest/utest.hpp>

#include <utility>
#include <vector>

#include <userver/engine/async.hpp>
#include <userver/engine/future_status.hpp>
#include <userver/engine/single_consumer_event.hpp>
#include <userver/engine/sleep.hpp>
#include <userver/engine/task/task_with_result.hpp>
#include <userver/utils/algo.hpp>

#include <tests/service_multichannel.hpp>
#include <tests/unit_test_client.usrv.pb.hpp>
#include <tests/unit_test_service.usrv.pb.hpp>
#include <userver/ugrpc/tests/service_fixtures.hpp>

using namespace std::chrono_literals;

USERVER_NAMESPACE_BEGIN

namespace {

constexpr int kNumber = 42;
constexpr auto kLongTimeout = 500ms;

void CheckServerContext(grpc::ServerContext& context) {
    const auto& client_metadata = context.client_metadata();
    EXPECT_EQ(utils::FindOptional(client_metadata, "req_header"), "value");
    context.AddTrailingMetadata("resp_header", "value");
}

class UnitTestLongAnswerService final : public sample::ugrpc::UnitTestServiceBase {
public:
    SayHelloResult SayHello(CallContext& context, sample::ugrpc::GreetingRequest&& request) override {
        if (request.name() != "default_context") {
            CheckServerContext(context.GetServerContext());
        }
        sample::ugrpc::GreetingResponse response;
        response.set_name("Hello " + request.name());

        engine::SleepUntil(engine::Deadline::FromDuration(answer_duration_));

        return response;
    }

    void SetAnswerDuration(std::chrono::milliseconds duration) { answer_duration_ = duration; };

private:
    std::chrono::milliseconds answer_duration_{kLongTimeout};
};

class UnitTestService final : public sample::ugrpc::UnitTestServiceBase {
public:
    SayHelloResult SayHello(CallContext& context, sample::ugrpc::GreetingRequest&& request) override {
        if (request.name() != "default_context") {
            CheckServerContext(context.GetServerContext());
        }
        sample::ugrpc::GreetingResponse response;
        response.set_name("Hello " + request.name());
        return response;
    }

    ReadManyResult
    ReadMany(CallContext& context, sample::ugrpc::StreamGreetingRequest&& request, ReadManyWriter& writer) override {
        CheckServerContext(context.GetServerContext());
        sample::ugrpc::StreamGreetingResponse response;
        response.set_name("Hello again " + request.name());
        for (int i = 0; i < request.number(); ++i) {
            response.set_number(i);
            writer.Write(response);
        }
        return grpc::Status::OK;
    }

    WriteManyResult WriteMany(CallContext& context, WriteManyReader& reader) override {
        CheckServerContext(context.GetServerContext());
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

    ChatResult Chat(CallContext& context, ChatReaderWriter& stream) override {
        CheckServerContext(context.GetServerContext());
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

}  // namespace

using GrpcClientTest = ugrpc::tests::ServiceFixture<UnitTestService>;

std::unique_ptr<grpc::ClientContext> PrepareClientContext() {
    auto context = std::make_unique<grpc::ClientContext>();
    context->AddMetadata("req_header", "value");
    return context;
}

void CheckClientContext(const grpc::ClientContext& context) {
    const auto& metadata = context.GetServerTrailingMetadata();
    const auto iter = metadata.find("resp_header");
    ASSERT_NE(iter, metadata.end());
    EXPECT_EQ(iter->second, "value");
}

UTEST_F(GrpcClientTest, UnaryRPC) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    sample::ugrpc::GreetingRequest out;
    out.set_name("userver");
    sample::ugrpc::GreetingResponse in;
    UEXPECT_NO_THROW(in = client.SayHello(out, PrepareClientContext()));
    EXPECT_EQ("Hello " + out.name(), in.name());
}

UTEST_F(GrpcClientTest, AsyncUnaryRPC) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    sample::ugrpc::GreetingRequest out;
    out.set_name("userver");
    auto future_for_move = client.AsyncSayHello(out, PrepareClientContext());
    auto future = std::move(future_for_move);  // test move operation

    bool is_ready = false;
    UEXPECT_NO_THROW(is_ready = future.IsReady());

    sample::ugrpc::GreetingResponse in;
    UEXPECT_NO_THROW(in = future.Get());
    CheckClientContext(future.GetCall().GetContext());
    EXPECT_EQ("Hello " + out.name(), in.name());
}

UTEST_F(GrpcClientTest, AsyncUnaryRPCWithTimeout) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    sample::ugrpc::GreetingRequest out;
    out.set_name("userver");
    auto future_for_move = client.AsyncSayHello(out, PrepareClientContext());
    auto future = std::move(future_for_move);  // test move operation

    EXPECT_EQ(future.WaitUntil(engine::Deadline::FromDuration(60s)), engine::FutureStatus::kReady);

    EXPECT_TRUE(future.IsReady());

    CheckClientContext(future.GetCall().GetContext());

    sample::ugrpc::GreetingResponse in;
    UEXPECT_NO_THROW(in = future.Get());
    EXPECT_EQ("Hello " + out.name(), in.name());
}

UTEST_F(GrpcClientTest, UnaryRPCDefaultContext) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    sample::ugrpc::GreetingRequest out;
    out.set_name("default_context");

    sample::ugrpc::GreetingResponse in;
    UEXPECT_NO_THROW(in = client.SayHello(out));
    EXPECT_EQ("Hello " + out.name(), in.name());
}

UTEST_F(GrpcClientTest, InputStream) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    sample::ugrpc::StreamGreetingRequest out;
    out.set_name("userver");
    out.set_number(kNumber);
    auto is_for_move = client.ReadMany(out, PrepareClientContext());
    auto is = std::move(is_for_move);  // test move operation

    sample::ugrpc::StreamGreetingResponse in;
    for (auto i = 0; i < kNumber; ++i) {
        EXPECT_TRUE(is.Read(in));
        EXPECT_EQ(in.number(), i);
    }
    EXPECT_FALSE(is.Read(in));

    CheckClientContext(is.GetContext());
}

UTEST_F(GrpcClientTest, EmptyInputStream) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    sample::ugrpc::StreamGreetingRequest out;
    out.set_name("userver");
    out.set_number(0);
    auto is = client.ReadMany(out, PrepareClientContext());

    sample::ugrpc::StreamGreetingResponse in;
    EXPECT_FALSE(is.Read(in));
    CheckClientContext(is.GetContext());
}

UTEST_F(GrpcClientTest, OutputStream) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    auto os_for_move = client.WriteMany(PrepareClientContext());
    auto os = std::move(os_for_move);  // test move operation

    sample::ugrpc::StreamGreetingRequest out;
    out.set_name("userver");
    for (auto i = 0; i < kNumber; ++i) {
        out.set_number(i);
        EXPECT_TRUE(os.Write(out));
    }

    sample::ugrpc::StreamGreetingResponse in;
    UEXPECT_NO_THROW(in = os.Finish());
    EXPECT_EQ(in.number(), kNumber);
    CheckClientContext(os.GetContext());
}

UTEST_F(GrpcClientTest, OutputStreamWriteAndCheck) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    auto os_for_move = client.WriteMany(PrepareClientContext());
    auto os = std::move(os_for_move);  // test move operation

    sample::ugrpc::StreamGreetingRequest out;
    out.set_name("userver");
    for (auto i = 0; i < kNumber; ++i) {
        out.set_number(i);
        UEXPECT_NO_THROW(os.WriteAndCheck(out));
    }

    sample::ugrpc::StreamGreetingResponse in;
    UEXPECT_NO_THROW(in = os.Finish());
    EXPECT_EQ(in.number(), kNumber);
    CheckClientContext(os.GetContext());
}

UTEST_F(GrpcClientTest, EmptyOutputStream) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    auto os = client.WriteMany(PrepareClientContext());

    sample::ugrpc::StreamGreetingResponse in;
    UEXPECT_NO_THROW(in = os.Finish());
    EXPECT_EQ(in.number(), 0);
    CheckClientContext(os.GetContext());
}

UTEST_F(GrpcClientTest, BidirectionalStream) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    auto bs_for_move = client.Chat(PrepareClientContext());
    auto bs = std::move(bs_for_move);  // test move operation

    sample::ugrpc::StreamGreetingRequest out{};
    out.set_name("userver");
    sample::ugrpc::StreamGreetingResponse in;

    for (auto i = 0; i < 42; ++i) {
        out.set_number(i);
        EXPECT_TRUE(bs.Write(out));
        EXPECT_TRUE(bs.Read(in));
        EXPECT_EQ(in.number(), i + 1);
    }
    EXPECT_TRUE(bs.WritesDone());
    EXPECT_FALSE(bs.Read(in));
    CheckClientContext(bs.GetContext());
}

UTEST_F(GrpcClientTest, BidirectionalStreamWriteAndCheck) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    auto bs_for_move = client.Chat(PrepareClientContext());
    auto bs = std::move(bs_for_move);  // test move operation

    sample::ugrpc::StreamGreetingRequest out{};
    out.set_name("userver");
    sample::ugrpc::StreamGreetingResponse in;

    for (auto i = 0; i < 42; ++i) {
        out.set_number(i);
        // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.UninitializedObject)
        UEXPECT_NO_THROW(bs.WriteAndCheck(out));
        EXPECT_TRUE(bs.Read(in));
        EXPECT_EQ(in.number(), i + 1);
    }
    EXPECT_TRUE(bs.WritesDone());
    EXPECT_FALSE(bs.Read(in));
    CheckClientContext(bs.GetContext());
}

UTEST_F(GrpcClientTest, EmptyBidirectionalStream) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    auto bs = client.Chat(PrepareClientContext());

    sample::ugrpc::StreamGreetingResponse in;
    EXPECT_TRUE(bs.WritesDone());
    EXPECT_FALSE(bs.Read(in));
    CheckClientContext(bs.GetContext());
}

using GrpcClientLongAnswerTest = ugrpc::tests::ServiceFixture<UnitTestLongAnswerService>;

UTEST_F(GrpcClientLongAnswerTest, AsyncUnaryLongAnswerRPC) {
    GetService().SetAnswerDuration(kLongTimeout);

    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    sample::ugrpc::GreetingRequest out;
    out.set_name("userver");

    auto future_for_move = client.AsyncSayHello(out, PrepareClientContext());
    auto future = std::move(future_for_move);  // test move operation

    EXPECT_EQ(future.WaitUntil(engine::Deadline::FromDuration(kLongTimeout / 100)), engine::FutureStatus::kTimeout);
    EXPECT_EQ(future.WaitUntil(engine::Deadline::FromDuration(kLongTimeout / 50)), engine::FutureStatus::kTimeout);
    EXPECT_EQ(future.WaitUntil(engine::Deadline::FromDuration(utest::kMaxTestWaitTime)), engine::FutureStatus::kReady);
    EXPECT_EQ(future.WaitUntil(engine::Deadline::FromDuration(utest::kMaxTestWaitTime)), engine::FutureStatus::kReady);
    sample::ugrpc::GreetingResponse in;
    UEXPECT_NO_THROW(in = future.Get());

    CheckClientContext(future.GetCall().GetContext());
    EXPECT_EQ("Hello " + out.name(), in.name());
}

using GrpcClientMultichannelTest = tests::ServiceFixtureMultichannel<UnitTestService>;

UTEST_P_MT(GrpcClientMultichannelTest, MultiThreadedClientTest, 4) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    engine::SingleConsumerEvent request_finished;
    std::vector<engine::TaskWithResult<void>> tasks;
    std::atomic<bool> keep_running{true};

    for (std::size_t i = 0; i < GetThreadCount(); ++i) {
        tasks.push_back(engine::AsyncNoSpan([&] {
            sample::ugrpc::GreetingRequest out;
            out.set_name("userver");

            while (keep_running) {
                auto future = client.AsyncSayHello(out, PrepareClientContext());
                auto in = future.Get();
                CheckClientContext(future.GetCall().GetContext());
                EXPECT_EQ("Hello " + out.name(), in.name());
                request_finished.Send();
                engine::Yield();
            }
        }));
    }

    EXPECT_TRUE(request_finished.WaitForEventFor(utest::kMaxTestWaitTime));

    // Make sure that multi-threaded requests work fine for some time
    engine::SleepFor(50ms);

    keep_running = false;
    for (auto& task : tasks) task.Get();
}

INSTANTIATE_UTEST_SUITE_P(Basic, GrpcClientMultichannelTest, testing::Values(std::size_t{1}, std::size_t{4}));

namespace {

class WriteAndFinishService final : public sample::ugrpc::UnitTestServiceBase {
public:
    ReadManyResult
    ReadMany(CallContext& /*context*/, sample::ugrpc::StreamGreetingRequest&& request, ReadManyWriter& /*writer*/)
        override {
        sample::ugrpc::StreamGreetingResponse response;
        response.set_number(kNumber);
        response.set_name("Hello " + request.name());
        return response;
    }

    ChatResult Chat(CallContext& /*context*/, ChatReaderWriter& /*stream*/) override {
        sample::ugrpc::StreamGreetingResponse response;
        response.set_number(kNumber);
        response.set_name("Hello");
        return response;
    }
};

}  // namespace

using GrpcWriteAndFinish = ugrpc::tests::ServiceFixture<WriteAndFinishService>;

UTEST_F(GrpcWriteAndFinish, InputStream) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    sample::ugrpc::StreamGreetingRequest out;
    out.set_name("userver");
    out.set_number(kNumber);
    auto is = client.ReadMany(out, PrepareClientContext());

    sample::ugrpc::StreamGreetingResponse in;
    EXPECT_TRUE(is.Read(in));
    EXPECT_EQ(in.number(), kNumber);
    EXPECT_EQ(in.name(), "Hello userver");
    EXPECT_FALSE(is.Read(in));
}

UTEST_F(GrpcWriteAndFinish, BidirectionalStream) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
    auto is = client.Chat(PrepareClientContext());

    sample::ugrpc::StreamGreetingResponse in;
    EXPECT_TRUE(is.Read(in));
    EXPECT_EQ(in.number(), kNumber);
    EXPECT_EQ(in.name(), "Hello");
    EXPECT_FALSE(is.Read(in));
}

USERVER_NAMESPACE_END
