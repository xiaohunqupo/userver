#pragma once

#include <userver/storages/secdist/secdist.hpp>
#include <userver/yaml_config/yaml_config.hpp>

#include <userver/ugrpc/client/client_factory.hpp>

USERVER_NAMESPACE_BEGIN

namespace ugrpc::client::impl {

enum class AuthType {
    kInsecure,
    kSsl,
};

/// Settings relating to the ClientFactory
struct ClientFactoryConfig final {
    AuthType auth_type{AuthType::kInsecure};

    /// Optional grpc-core channel args
    /// @see https://grpc.github.io/grpc/core/group__grpc__arg__keys.html
    grpc::ChannelArguments channel_args{};

    /// Number of underlying channels that will be created for every client
    /// in this factory.
    std::size_t channel_count{1};
};

ClientFactoryConfig Parse(const yaml_config::YamlConfig& value, formats::parse::To<ClientFactoryConfig>);

ClientFactorySettings
MakeFactorySettings(impl::ClientFactoryConfig&& config, const storages::secdist::SecdistConfig* secdist);

}  // namespace ugrpc::client::impl

USERVER_NAMESPACE_END
