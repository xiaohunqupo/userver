#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include <grpcpp/client_context.h>
#include <grpcpp/support/time.h>

#include <userver/engine/deadline.hpp>
#include <userver/engine/sleep.hpp>
#include <userver/server/request/task_inherited_data.hpp>
#include <userver/utest/assert_macros.hpp>
#include <userver/utest/utest.hpp>

#include <ugrpc/client/impl/client_configs.hpp>
#include <ugrpc/server/impl/server_configs.hpp>
#include <ugrpc/server/middlewares/deadline_propagation/middleware.hpp>
#include <userver/ugrpc/client/exceptions.hpp>

#include <tests/messages.pb.h>
#include <tests/deadline_helpers.hpp>
#include <tests/unit_test_client.usrv.pb.hpp>
#include <tests/unit_test_service.usrv.pb.hpp>
#include <userver/ugrpc/tests/service_fixtures.hpp>

USERVER_NAMESPACE_BEGIN

namespace {

using ClientType = sample::ugrpc::UnitTestServiceClient;

constexpr auto kDeadlinePropagated = "deadline-propagated";
constexpr auto kCancelledByDp = "cancelled-by-deadline-propagation";

class UnitTestDeadlineStatsService final : public sample::ugrpc::UnitTestServiceBase {
public:
    SayHelloResult SayHello(CallContext& /*context*/, sample::ugrpc::GreetingRequest&& request) override {
        sample::ugrpc::GreetingResponse response;
        response.set_name("Hello " + request.name());

        if (wait_deadline_) {
            tests::WaitUntilRpcDeadlineService();
        }

        return response;
    }

    void SetWaitDeadline(bool value) { wait_deadline_ = value; }

private:
    bool wait_deadline_{false};
};

class DeadlineStatsTests : public ugrpc::tests::ServiceFixture<UnitTestDeadlineStatsService> {
public:
    DeadlineStatsTests() {
        ExtendDynamicConfig({
            {ugrpc::client::impl::kEnforceClientTaskDeadline, true},
            {ugrpc::server::impl::kServerCancelTaskByDeadline, true},
        });
    }

    void BeSlow() { GetService().SetWaitDeadline(true); }

    void BeFast() { GetService().SetWaitDeadline(false); }

    bool PerformRequest(bool set_deadline) {
        sample::ugrpc::GreetingRequest request;
        sample::ugrpc::GreetingResponse response;
        request.set_name("abacaba");

        auto client = MakeClient<ClientType>();

        auto client_context = tests::MakeClientContext(set_deadline);
        try {
            response = client.SayHello(request, std::move(client_context));
            EXPECT_EQ(response.name(), "Hello abacaba");
            return true;
        } catch (const ugrpc::client::DeadlineExceededError& /*exception*/) {
            return false;
        }
    }

    void DisableClientDp() { ExtendDynamicConfig({{ugrpc::client::impl::kEnforceClientTaskDeadline, false}}); }

    utils::statistics::Rate GetServerStatistic(const std::string& path) {
        const auto statistics = GetStatistics(
            "grpc.server.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/SayHello"}}
        );

        return statistics.SingleMetric(path).AsRate();
    }

    utils::statistics::Rate GetClientStatistic(const std::string& path) {
        const auto statistics = GetStatistics(
            "grpc.client.by-destination", {{"grpc_destination", "sample.ugrpc.UnitTestService/SayHello"}}
        );

        return statistics.SingleMetric(path).AsRate();
    }
};

}  // namespace

UTEST_F(DeadlineStatsTests, ServerDeadlineUpdated) {
    constexpr std::size_t kRequestCount{3};

    // Requests with deadline
    for (std::size_t i = 0; i < kRequestCount; ++i) {
        EXPECT_TRUE(PerformRequest(true));
    }

    // Make sure that server metrics are written
    GetServer().StopServing();

    EXPECT_EQ(GetServerStatistic(kDeadlinePropagated), kRequestCount);
}

UTEST_F(DeadlineStatsTests, ServerDeadlineNotUpdatedWithoutDeadline) {
    constexpr std::size_t kRequestCount{3};

    // Requests without deadline, default deadline is used
    for (std::size_t i = 0; i < kRequestCount; ++i) {
        EXPECT_TRUE(PerformRequest(false));
    }

    // Make sure that server metrics are written
    GetServer().StopServing();

    EXPECT_EQ(GetServerStatistic(kDeadlinePropagated), 0);
}

UTEST_F(DeadlineStatsTests, ClientDeadlineUpdated) {
    size_t expected_value{0};

    // TaskInheritedData has set up. Deadline will be propagated
    tests::InitTaskInheritedDeadline();

    // Enabled be default
    // Requests with deadline
    // TaskInheritedData less than context deadline and replace it
    EXPECT_TRUE(PerformRequest(true));
    EXPECT_TRUE(PerformRequest(true));
    EXPECT_TRUE(PerformRequest(true));

    expected_value += 3;
    EXPECT_EQ(GetClientStatistic(kDeadlinePropagated), expected_value);

    // Requests without deadline
    // TaskInheritedData will be set as deadline
    EXPECT_TRUE(PerformRequest(false));
    EXPECT_TRUE(PerformRequest(false));
    EXPECT_TRUE(PerformRequest(false));

    expected_value += 3;
    EXPECT_EQ(GetClientStatistic(kDeadlinePropagated), expected_value);
}

UTEST_F(DeadlineStatsTests, ClientDeadlineNotUpdated) {
    constexpr std::size_t kExpected{0};

    // TaskInheritedData has set up but context deadline is less
    tests::InitTaskInheritedDeadline(engine::Deadline::FromDuration(tests::kLongTimeout * 2));

    // Requests with deadline. Deadline will not be replaced
    EXPECT_TRUE(PerformRequest(true));
    EXPECT_TRUE(PerformRequest(true));
    EXPECT_TRUE(PerformRequest(true));

    EXPECT_EQ(GetClientStatistic(kDeadlinePropagated), kExpected);

    // Disable deadline propagation for the following tests
    const server::request::DeadlinePropagationBlocker dp_blocker;

    // Requests with deadline
    EXPECT_TRUE(PerformRequest(true));
    EXPECT_TRUE(PerformRequest(true));
    EXPECT_TRUE(PerformRequest(true));

    EXPECT_EQ(GetClientStatistic(kDeadlinePropagated), kExpected);

    // Requests without deadline
    EXPECT_TRUE(PerformRequest(false));
    EXPECT_TRUE(PerformRequest(false));
    EXPECT_TRUE(PerformRequest(false));

    EXPECT_EQ(GetClientStatistic(kDeadlinePropagated), kExpected);
}

UTEST_F(DeadlineStatsTests, ClientDeadlineCancelled) {
    constexpr std::size_t kExpected{1};

    // Server will wait for deadline before answer
    BeSlow();

    // TaskInheritedData has set up, but DP disabled
    tests::InitTaskInheritedDeadline();

    // Requests with deadline
    EXPECT_FALSE(PerformRequest(true));

    EXPECT_EQ(GetClientStatistic(kCancelledByDp), kExpected);
}

UTEST_F(DeadlineStatsTests, ClientDeadlineCancelledNotByDp) {
    constexpr std::size_t kExpected{0};

    // Server will wait for deadline before answer
    BeSlow();

    // No TaskInheritedData. Deadline not updated. Request cancelled but not
    // due to deadline propagation

    // Requests with deadline
    EXPECT_FALSE(PerformRequest(true));

    EXPECT_EQ(GetClientStatistic(kCancelledByDp), kExpected);
}

UTEST_F(DeadlineStatsTests, DisabledClientDeadlineUpdated) {
    constexpr std::size_t kExpected{0};

    DisableClientDp();

    // TaskInheritedData has set up, but DP disabled
    tests::InitTaskInheritedDeadline();

    // Requests with deadline
    // TaskInheritedData ignored
    EXPECT_TRUE(PerformRequest(true));
    EXPECT_TRUE(PerformRequest(true));
    EXPECT_TRUE(PerformRequest(true));

    // Requests without deadline
    // TaskInheritedData ignored
    EXPECT_TRUE(PerformRequest(false));
    EXPECT_TRUE(PerformRequest(false));
    EXPECT_TRUE(PerformRequest(false));

    EXPECT_EQ(GetClientStatistic(kDeadlinePropagated), kExpected);
}

UTEST_F(DeadlineStatsTests, DisabledClientDeadlineCancelled) {
    constexpr std::size_t kExpected{0};

    BeSlow();

    DisableClientDp();

    // TaskInheritedData has set up, but DP disabled.
    tests::InitTaskInheritedDeadline();

    // Failed by deadline. But not due to deadline propagation
    EXPECT_FALSE(PerformRequest(true));

    EXPECT_EQ(GetClientStatistic(kCancelledByDp), kExpected);
}

USERVER_NAMESPACE_END
