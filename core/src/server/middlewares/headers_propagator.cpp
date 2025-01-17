#include <userver/server/middlewares/headers_propagator.hpp>

#include <server/request/internal_request_context.hpp>

#include <userver/components/component_config.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/request/task_inherited_request.hpp>
#include <userver/utils/algo.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::middlewares {

HeadersPropagator::HeadersPropagator(const handlers::HttpHandlerBase&, std::vector<std::string> headers)
    : headers_(std::move(headers)) {}

void HeadersPropagator::HandleRequest(http::HttpRequest& request, request::RequestContext& context) const {
    USERVER_NAMESPACE::server::request::HeadersToPropagate headers_to_propagate;
    for (const auto& header_name : headers_) {
        const auto* header_value = utils::FindOrNullptr(request.GetHeaders(), header_name);
        if (!header_value) {
            continue;
        }
        headers_to_propagate.emplace_back(header_name, *header_value);
    }
    USERVER_NAMESPACE::server::request::SetPropagatedHeaders(headers_to_propagate);
    Next(request, context);
}

HeadersPropagatorFactory::HeadersPropagatorFactory(
    const components::ComponentConfig& config,
    const components::ComponentContext& context
)
    : HttpMiddlewareFactoryBase(config, context), headers_(config["headers"].As<std::vector<std::string>>({})) {}

std::unique_ptr<HttpMiddlewareBase>
HeadersPropagatorFactory::Create(const handlers::HttpHandlerBase& handler, yaml_config::YamlConfig) const {
    return std::make_unique<HeadersPropagator>(handler, headers_);
}

yaml_config::Schema HeadersPropagatorFactory::GetStaticConfigSchema() {
    return yaml_config::MergeSchemas<ComponentBase>(R"(
type: object
description: Http service headers propagator middleware
additionalProperties: false
properties:
    headers:
        type: array
        description: array of headers to propagate
        items:
            type: string
            description: header
)");
}

}  // namespace server::middlewares

USERVER_NAMESPACE_END
