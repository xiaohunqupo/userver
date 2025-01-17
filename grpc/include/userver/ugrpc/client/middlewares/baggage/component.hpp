#pragma once

/// @file userver/ugrpc/client/baggage/component.hpp
/// @brief @copybrief ugrpc::client::middlewares::baggage::Component

#include <userver/ugrpc/client/middlewares/base.hpp>

USERVER_NAMESPACE_BEGIN

/// Client baggage middleware
namespace ugrpc::client::middlewares::baggage {

// clang-format off

/// @ingroup userver_components
///
/// @brief Component for gRPC client baggage
///
/// The component does **not** have any options for service config.
///
/// ## Static configuration example:
///
/// @snippet grpc/functional_tests/basic_chaos/static_config.yaml Sample grpc client baggage middleware component config

// clang-format on

class Component final : public MiddlewareComponentBase {
public:
    /// @ingroup userver_component_names
    /// @brief The default name of ugrpc::client::middlewares::baggage::Component
    static constexpr std::string_view kName = "grpc-client-baggage";

    Component(const components::ComponentConfig& config, const components::ComponentContext& context);

    std::shared_ptr<const MiddlewareFactoryBase> GetMiddlewareFactory() override;

    static yaml_config::Schema GetStaticConfigSchema();
};

}  // namespace ugrpc::client::middlewares::baggage

USERVER_NAMESPACE_END
