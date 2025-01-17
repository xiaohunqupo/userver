#pragma once

/// @file userver/ugrpc/client/client_factory_component.hpp
/// @brief @copybrief ugrpc::client::ClientFactoryComponent

#include <userver/components/component_base.hpp>

#include <userver/ugrpc/client/client_factory.hpp>

USERVER_NAMESPACE_BEGIN

namespace ugrpc::client {

// clang-format off

/// @ingroup userver_components
///
/// @brief Provides a ClientFactory in the component system
///
/// Multiple ClientFactoryComponent instances may be created if different
/// parameters are required for different clients.
///
/// Requires ugrpc::client::CommonComponent.
///
/// ## Authentication
/// Authentication is controlled by `auth-type` static config field.
/// Possible values:
/// - `insecure` (`InsecureChannelCredentials` - default)
/// - `ssl` (`SslCredentials`)
///
/// Default (system) authentication keys are used regardless of the chosen
/// auth-type.
///
/// ## Service config
/// As per https://github.com/grpc/grpc/blob/master/doc/service_config.md
/// service config should be distributed via the name resolution process.
/// We allow setting default service_config: pass desired JSON literal
/// to `default-service-config` parameter
///
/// ## Static options:
/// The default component name for static config is `"grpc-client-factory"`.
///
/// Name | Description | Default value
/// ---- | ----------- | -------------
/// task-processor | the task processor for blocking channel creation | -
/// channel-args | a map of channel arguments, see gRPC Core docs | {}
/// auth-type | authentication method, see above | -
/// default-service-config | default service config, see above | -
/// channel-count | Number of underlying grpc::Channel objects | 1
/// middlewares | middlewares names to use | -
///
/// @see https://grpc.github.io/grpc/core/group__grpc__arg__keys.html

// clang-format on

class ClientFactoryComponent final : public components::ComponentBase {
public:
    /// @ingroup userver_component_names
    /// @brief The default name of ugrpc::client::middlewares::log::Component
    static constexpr std::string_view kName = "grpc-client-factory";

    ClientFactoryComponent(const components::ComponentConfig& config, const components::ComponentContext& context);

    ClientFactory& GetFactory();

    static yaml_config::Schema GetStaticConfigSchema();

private:
    std::optional<ClientFactory> factory_;
};

}  // namespace ugrpc::client

template <>
inline constexpr bool components::kHasValidate<ugrpc::client::ClientFactoryComponent> = true;

USERVER_NAMESPACE_END
