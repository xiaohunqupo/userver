#pragma once

/// @file userver/ugrpc/client/channels.hpp
/// @brief Utilities for managing gRPC connections

#include <algorithm>

#include <grpcpp/channel.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/security/credentials.h>

#include <userver/engine/deadline.hpp>
#include <userver/engine/task/task_processor_fwd.hpp>

#include <userver/ugrpc/client/impl/client_data.hpp>

USERVER_NAMESPACE_BEGIN

namespace ugrpc::client {

namespace impl {

[[nodiscard]] bool TryWaitForConnected(
    grpc::Channel& channel,
    grpc::CompletionQueue& queue,
    engine::Deadline deadline,
    engine::TaskProcessor& blocking_task_processor
);

}  // namespace impl

/// @brief Create a new gRPC channel (connection pool) for the endpoint
///
/// Channel creation is expensive, reuse channels if possible.
///
/// Each channel is fully thread-safe: it can be used across multiple threads,
/// possibly from multiple clients and using multiple `CompletionQueue`
/// instances at the same time.
///
/// @param blocking_task_processor task processor for blocking channel creation
/// @param channel_credentials channel credentials
/// @param endpoint string host:port
/// @returns shared pointer to the gRPC channel
std::shared_ptr<grpc::Channel> MakeChannel(
    engine::TaskProcessor& blocking_task_processor,
    std::shared_ptr<grpc::ChannelCredentials> channel_credentials,
    const std::string& endpoint
);

/// @brief Wait until the channel state of `client` is `READY`. If the current
/// state is already `READY`, returns `true` immediately. In case of multiple
/// underlying channels, waits for all of them.
/// @returns `true` if the state changed before `deadline` expired
/// @note The wait operation does not support task cancellations
template <typename Client>
[[nodiscard]] bool
TryWaitForConnected(Client& client, engine::Deadline deadline, engine::TaskProcessor& blocking_task_processor) {
    const auto& channels = impl::GetClientData(client).GetChannels();
    auto& queue = impl::GetClientData(client).NextQueue();
    return std::all_of(channels.begin(), channels.end(), [&](auto& channel) {
        return ugrpc::client::impl::TryWaitForConnected(*channel, queue, deadline, blocking_task_processor);
    });
}

}  // namespace ugrpc::client

USERVER_NAMESPACE_END
