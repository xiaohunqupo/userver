#include <userver/ugrpc/client/channels.hpp>

#include <grpcpp/create_channel.h>

#include <userver/engine/async.hpp>

#include <userver/ugrpc/impl/async_method_invocation.hpp>
#include <userver/ugrpc/impl/deadline_timepoint.hpp>
#include <userver/ugrpc/impl/to_string.hpp>

USERVER_NAMESPACE_BEGIN

namespace ugrpc::client {

namespace impl {

namespace {

[[nodiscard]] bool
DoTryWaitForConnected(grpc::Channel& channel, grpc::CompletionQueue& queue, engine::Deadline deadline) {
    while (true) {
        // A potentially-blocking call
        const auto state = channel.GetState(true);

        if (state == ::GRPC_CHANNEL_READY) return true;
        if (state == ::GRPC_CHANNEL_SHUTDOWN) return false;

        ugrpc::impl::AsyncMethodInvocation operation;
        channel.NotifyOnStateChange(state, deadline, &queue, operation.GetTag());
        if (operation.Wait() != ugrpc::impl::AsyncMethodInvocation::WaitStatus::kOk) return false;
    }
}

}  // namespace

[[nodiscard]] bool TryWaitForConnected(
    grpc::Channel& channel,
    grpc::CompletionQueue& queue,
    engine::Deadline deadline,
    engine::TaskProcessor& blocking_task_processor
) {
    return engine::AsyncNoSpan(
               blocking_task_processor, DoTryWaitForConnected, std::ref(channel), std::ref(queue), deadline
    )
        .Get();
}

}  // namespace impl

std::shared_ptr<grpc::Channel> MakeChannel(
    engine::TaskProcessor& blocking_task_processor,
    std::shared_ptr<grpc::ChannelCredentials> channel_credentials,
    const std::string& endpoint
) {
    // Spawn a blocking task creating a gRPC channel
    // This is third party code, no use of span inside it
    return engine::AsyncNoSpan(
               blocking_task_processor,
               grpc::CreateChannel,
               ugrpc::impl::ToGrpcString(endpoint),
               std::ref(channel_credentials)
    )
        .Get();
}

}  // namespace ugrpc::client

USERVER_NAMESPACE_END
