#include <userver/utest/utest.hpp>

#include <gmock/gmock.h>

#include <userver/dynamic_config/storage_mock.hpp>
#include <userver/dynamic_config/test_helpers.hpp>
#include <userver/engine/async.hpp>
#include <userver/engine/single_consumer_event.hpp>
#include <userver/engine/sleep.hpp>
#include <userver/utest/log_capture_fixture.hpp>

#include <../include/userver/ugrpc/client/impl/completion_queue_pool.hpp>
#include <ugrpc/client/impl/client_configs.hpp>
#include <userver/ugrpc/client/client_factory.hpp>
#include <userver/ugrpc/client/exceptions.hpp>
#include <userver/ugrpc/server/exceptions.hpp>
#include <userver/ugrpc/server/server.hpp>
#include <userver/ugrpc/tests/standalone_client.hpp>

#include <tests/unit_test_client.usrv.pb.hpp>
#include <tests/unit_test_service.usrv.pb.hpp>
#include <userver/ugrpc/tests/service_fixtures.hpp>

using namespace std::chrono_literals;

USERVER_NAMESPACE_BEGIN

namespace {

class UnitTestServiceCancelEcho final : public sample::ugrpc::UnitTestServiceBase {
public:
    ChatResult Chat(CallContext& /*context*/, ChatReaderWriter& stream) override {
        sample::ugrpc::StreamGreetingRequest request;
        sample::ugrpc::StreamGreetingResponse response{};

        EXPECT_TRUE(stream.Read(request));
        // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.UninitializedObject)
        UEXPECT_NO_THROW(stream.Write(response));

        EXPECT_FALSE(stream.Read(request));
        // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.UninitializedObject)
        UEXPECT_THROW(stream.Write(response), ugrpc::server::RpcInterruptedError);

        return grpc::Status::OK;
    }
};

}  // namespace

using GrpcCancel = ugrpc::tests::ServiceFixture<UnitTestServiceCancelEcho>;

UTEST_F(GrpcCancel, TryCancel) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();

    for (int i = 0; i < 2; ++i) {
        auto call = client.Chat();

        EXPECT_TRUE(call.Write({}));
        sample::ugrpc::StreamGreetingResponse response;
        EXPECT_TRUE(call.Read(response));

        // Drop 'call' without finishing. After this the server side should
        // immediately receive RpcInterruptedError. The connection should not be
        // closed.
    }
}

namespace {

class UnitTestServiceCancelEchoInf final : public sample::ugrpc::UnitTestServiceBase {
public:
    ChatResult Chat(CallContext& /*context*/, ChatReaderWriter& stream) override {
        for (;;) {
            sample::ugrpc::StreamGreetingRequest request;
            if (!stream.Read(request)) return grpc::Status::OK;
            sample::ugrpc::StreamGreetingResponse response{};
            // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.UninitializedObject)
            stream.Write(response);
            return grpc::Status::OK;
        }
    }
};

}  // namespace

using GrpcCancelDeadline = ugrpc::tests::ServiceFixture<UnitTestServiceCancelEchoInf>;

UTEST_F_MT(GrpcCancelDeadline, TryCancel, 2) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();

    auto context = std::make_unique<grpc::ClientContext>();
    context->set_deadline(engine::Deadline::FromDuration(50ms));
    try {
        auto call = client.Chat(std::move(context));
        for (;;) {
            if (!call.Write({})) return;
            sample::ugrpc::StreamGreetingResponse response;
            if (!call.Read(response)) return;
        }
    } catch (const ugrpc::client::DeadlineExceededError&) {
        // If the server detects the deadline first
    } catch (const ugrpc::client::RpcInterruptedError&) {
        // If the client detects the deadline first
    }
}

namespace {

class UnitTestServiceCancelEchoInfWrites final : public sample::ugrpc::UnitTestServiceBase {
public:
    ChatResult Chat(CallContext& /*context*/, ChatReaderWriter& stream) override {
        sample::ugrpc::StreamGreetingRequest request;
        EXPECT_TRUE(stream.Read(request));

        sample::ugrpc::StreamGreetingResponse response{};
        for (;;) {
            // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.UninitializedObject)
            stream.Write(response);
        }

        return grpc::Status::OK;
    }
};

}  // namespace

using GrpcCancelWritesDone = ugrpc::tests::ServiceFixture<UnitTestServiceCancelEchoInfWrites>;

UTEST_F_MT(GrpcCancelWritesDone, TryCancel, 2) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();

    auto context = std::make_unique<grpc::ClientContext>();
    context->set_deadline(engine::Deadline::FromDuration(50ms));
    auto call = client.Chat(std::move(context));
    const auto is_written = call.Write({});
    if (!is_written) {
        // The call of Write() is failed, so we have to finish the test
        // This case is very rare when the deadline has already expired
        return;
    }
    EXPECT_TRUE(call.WritesDone());

    try {
        for (;;) {
            sample::ugrpc::StreamGreetingResponse response;
            if (!call.Read(response)) return;
        }
    } catch (const ugrpc::client::DeadlineExceededError&) {
    }
}

namespace {

class UnitTestServiceCancelEchoNoSecondWrite final : public sample::ugrpc::UnitTestServiceBase {
public:
    ChatResult Chat(CallContext& /*context*/, ChatReaderWriter& stream) override {
        sample::ugrpc::StreamGreetingRequest request;
        EXPECT_TRUE(stream.Read(request));
        sample::ugrpc::StreamGreetingResponse response{};

        // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.UninitializedObject)
        stream.Write(response);
        return grpc::Status::OK;
    }
};

}  // namespace

using GrpcCancelAfterRead = ugrpc::tests::ServiceFixture<UnitTestServiceCancelEchoNoSecondWrite>;

UTEST_F_MT(GrpcCancelAfterRead, TryCancel, 2) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();

    auto context = std::make_unique<grpc::ClientContext>();
    context->set_deadline(engine::Deadline::FromDuration(150ms));
    auto call = client.Chat(std::move(context));
    EXPECT_TRUE(call.Write({}));

    sample::ugrpc::StreamGreetingResponse response;
    EXPECT_TRUE(call.Read(response));
    EXPECT_FALSE(call.Read(response));
}

namespace {

class UnitTestServiceEcho final : public sample::ugrpc::UnitTestServiceBase {
public:
    ChatResult Chat(CallContext& /*context*/, ChatReaderWriter& stream) override {
        sample::ugrpc::StreamGreetingRequest request;
        sample::ugrpc::StreamGreetingResponse response;
        while (stream.Read(request)) {
            response.set_name(request.name());
            response.set_number(request.number());
            stream.Write(response);
        }
        return grpc::Status::OK;
    }
};

using GrpcServerEcho = ugrpc::tests::ServiceFixture<UnitTestServiceEcho>;

}  // namespace

UTEST_F_MT(GrpcServerEcho, DestroyServerDuringRequest, 2) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();

    auto call = client.Chat();
    EXPECT_TRUE(call.Write({}));

    sample::ugrpc::StreamGreetingResponse response;
    EXPECT_TRUE(call.Read(response));

    auto complete_rpc = engine::AsyncNoSpan([&] {
        // Make sure that 'server.StopServing()' call starts
        engine::SleepFor(50ms);

        // The server should wait for the ongoing RPC to complete
        EXPECT_TRUE(call.Write({}));
        UEXPECT_NO_THROW(EXPECT_TRUE(call.Read(response)));
        EXPECT_TRUE(call.WritesDone());
        UEXPECT_NO_THROW(EXPECT_FALSE(call.Read(response)));
    });

    GetServer().StopServing();
    complete_rpc.Get();
}

UTEST(GrpcServer, DeadlineAffectsWaitForReady) {
    ugrpc::tests::StandaloneClientFactory client_factory;

    auto client = client_factory.MakeClient<sample::ugrpc::UnitTestServiceClient>(
        ugrpc::tests::MakeIpv6Endpoint(ugrpc::tests::GetFreeIpv6Port())
    );

    auto context = std::make_unique<grpc::ClientContext>();
    context->set_deadline(engine::Deadline::FromDuration(100ms));
    context->set_wait_for_ready(true);

    auto long_deadline = engine::Deadline::FromDuration(100ms + 1s);
    UEXPECT_THROW(client.SayHello({}, std::move(context)), ugrpc::client::DeadlineExceededError);
    EXPECT_FALSE(long_deadline.IsReached());
}

namespace {

class UnitTestServiceCancelHello final : public sample::ugrpc::UnitTestServiceBase {
public:
    UnitTestServiceCancelHello() = default;

    SayHelloResult SayHello(CallContext& /*context*/, sample::ugrpc::GreetingRequest&& /*request*/) override {
        sample::ugrpc::GreetingResponse response;

        // Wait until cancelled.
        const bool success = wait_event_.WaitForEvent();
        EXPECT_FALSE(success);
        EXPECT_TRUE(engine::current_task::ShouldCancel());

        finish_event_.Send();

        return response;
    }

    auto& GetWaitEvent() { return wait_event_; }
    auto& GetFinishEvent() { return finish_event_; }

private:
    engine::SingleConsumerEvent wait_event_;
    engine::SingleConsumerEvent finish_event_;
};

}  // namespace

using GrpcCancelByClient = ugrpc::tests::ServiceFixture<UnitTestServiceCancelHello>;

UTEST_F_MT(GrpcCancelByClient, CancelByClient, 3) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();

    auto context = std::make_unique<grpc::ClientContext>();
    context->set_deadline(engine::Deadline::FromDuration(100ms));
    context->set_wait_for_ready(true);
    UEXPECT_THROW(client.SayHello({}, std::move(context)), ugrpc::client::BaseError);

    ASSERT_TRUE(GetService().GetFinishEvent().WaitForEventFor(std::chrono::seconds{5}));
}

UTEST_F_MT(GrpcCancelByClient, CancelByClientNoReadyWait, 3) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();

    auto context = std::make_unique<grpc::ClientContext>();
    context->set_deadline(engine::Deadline::FromDuration(100ms));
    UEXPECT_THROW(client.SayHello({}, std::move(context)), ugrpc::client::BaseError);

    ASSERT_TRUE(GetService().GetFinishEvent().WaitForEventFor(std::chrono::seconds{5}));
}

namespace {

class UnitTestServiceCancelSleep final : public sample::ugrpc::UnitTestServiceBase {
public:
    SayHelloResult SayHello(CallContext& /*context*/, sample::ugrpc::GreetingRequest&& /*request*/) override {
        engine::SleepFor(std::chrono::seconds(1));
        sample::ugrpc::GreetingResponse response{};
        return response;
    }
};

}  // namespace

using GrpcCancelSleep = utest::LogCaptureFixture<ugrpc::tests::ServiceFixture<UnitTestServiceCancelSleep>>;

UTEST_F(GrpcCancelSleep, CancelByTimeoutLogging) {
    auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();

    UEXPECT_THROW(
        client.SayHello(
            {}, std::make_unique<::grpc::ClientContext>(), ugrpc::client::Qos{std::chrono::milliseconds(100)}
        ),
        ugrpc::client::DeadlineExceededError
    );

    engine::SleepFor(std::chrono::seconds(1));

    EXPECT_THAT(
        GetLogCapture().Filter("Handler task cancelled, error in "
                               "'sample.ugrpc.UnitTestService/SayHello': "
                               "'sample.ugrpc.UnitTestService/SayHello' failed: "
                               "connection error at Finish"),
        testing::SizeIs(1)
    ) << GetLogCapture().GetAll();
}

namespace {

class UnitTestServiceCancelError final : public sample::ugrpc::UnitTestServiceBase {
public:
    ChatResult Chat(CallContext& /*context*/, ChatReaderWriter& /*stream*/) override {
        engine::SleepFor(std::chrono::milliseconds(500));
        throw std::runtime_error("Some error");
    }
};

}  // namespace

using GrpcCancelError = utest::LogCaptureFixture<ugrpc::tests::ServiceFixture<UnitTestServiceCancelError>>;

UTEST_F(GrpcCancelError, CancelByError) {
    {
        auto client = MakeClient<sample::ugrpc::UnitTestServiceClient>();
        auto call = client.Chat();
    }

    engine::SleepFor(std::chrono::seconds(1));

    ASSERT_THAT(
        GetLogCapture().Filter("Handler task cancelled, error in "
                               "'sample.ugrpc.UnitTestService/Chat': "
                               "Some error (std::runtime_error)"),
        testing::SizeIs(1)
    ) << GetLogCapture().GetAll();
}

USERVER_NAMESPACE_END
