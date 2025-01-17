#pragma once

#include <userver/server/middlewares/builtin.hpp>
#include <userver/server/middlewares/http_middleware_base.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::middlewares {

class HandlerMetrics final : public HttpMiddlewareBase {
public:
    static constexpr std::string_view kName = builtin::kHandlerMetrics;

    explicit HandlerMetrics(const handlers::HttpHandlerBase&);

private:
    void HandleRequest(http::HttpRequest& request, request::RequestContext& context) const override;

    const handlers::HttpHandlerBase& handler_;
};

using HandlerMetricsFactory = SimpleHttpMiddlewareFactory<HandlerMetrics>;

}  // namespace server::middlewares

USERVER_NAMESPACE_END
