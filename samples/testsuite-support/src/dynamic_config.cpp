#include "dynamic_config.hpp"

#include <userver/components/component.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/dynamic_config/value.hpp>
#include <userver/testsuite/testpoint.hpp>

namespace tests::handlers {

namespace {

const dynamic_config::Key<int> kMeaningOfLife{"MEANING_OF_LIFE", 42};

}  // namespace

DynamicConfig::DynamicConfig(const components::ComponentConfig& config, const components::ComponentContext& context)
    : server::handlers::HttpHandlerBase(config, context),
      config_source_(context.FindComponent<components::DynamicConfig>().GetSource()) {}

std::string DynamicConfig::HandleRequest(
    [[maybe_unused]] server::http::HttpRequest& request,
    [[maybe_unused]] server::request::RequestContext& context
) const {
    request.GetHttpResponse().SetContentType(http::content_type::kTextPlain);
    const int config_value = config_source_.GetCopy(kMeaningOfLife);
    return std::to_string(config_value);
}

}  // namespace tests::handlers
