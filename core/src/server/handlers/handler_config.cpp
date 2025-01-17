#include <userver/server/handlers/handler_config.hpp>

#include <fmt/format.h>

#include <server/server_config.hpp>

#include <server/http/parse_http_status.hpp>
#include <userver/formats/parse/common_containers.hpp>
#include <userver/logging/level_serialization.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::handlers {

UrlTrailingSlashOption Parse(const yaml_config::YamlConfig& yaml, formats::parse::To<UrlTrailingSlashOption>) {
    const auto& value = yaml.As<std::string>();
    if (value == "both") return UrlTrailingSlashOption::kBoth;
    if (value == "strict-match") return UrlTrailingSlashOption::kStrictMatch;
    throw std::runtime_error("can't parse UrlTrailingSlashOption from '" + value + '\'');
}

FallbackHandler Parse(const yaml_config::YamlConfig& yaml, formats::parse::To<FallbackHandler>) {
    const auto& value = yaml.As<std::string>();
    return FallbackHandlerFromString(value);
}

HandlerConfig ParseHandlerConfigsWithDefaults(
    const yaml_config::YamlConfig& value,
    const server::ServerConfig& server_config,
    bool is_monitor
) {
    HandlerConfig config;

    request::HttpRequestConfig handler_defaults{};
    if (!is_monitor) {
        handler_defaults = server_config.listener.handler_defaults;
    } else if (server_config.monitor_listener.has_value()) {
        handler_defaults = server_config.monitor_listener->handler_defaults;
    }

    {
        auto opt_path = value["path"].As<std::optional<std::string>>();
        const auto opt_fallback = value["as_fallback"].As<std::optional<FallbackHandler>>();

        if (!opt_path == !opt_fallback)
            throw std::runtime_error(fmt::format(
                "Expected 'path' or 'as_fallback' at {}, but {} provided",
                value.GetPath(),
                (!opt_path) ? "none" : "both"
            ));

        if (opt_path)
            config.path = std::move(*opt_path);
        else
            config.path = *opt_fallback;
    }

    config.task_processor = value["task_processor"].As<std::string>();
    config.method = value["method"].As<std::string>();
    config.request_config.max_request_size = value["max_request_size"].As<size_t>(handler_defaults.max_request_size);
    config.request_config.max_headers_size = value["max_headers_size"].As<size_t>(handler_defaults.max_headers_size);
    config.request_config.parse_args_from_body =
        value["parse_args_from_body"].As<bool>(handler_defaults.parse_args_from_body);
    config.auth = value["auth"].As<std::optional<auth::HandlerAuthConfig>>();
    config.url_trailing_slash =
        value["url_trailing_slash"].As<UrlTrailingSlashOption>(UrlTrailingSlashOption::kDefault);
    config.max_requests_in_flight = value["max_requests_in_flight"].As<std::optional<size_t>>();
    config.request_body_size_log_limit =
        value["request_body_size_log_limit"].As<size_t>(handler_defaults.request_body_size_log_limit);
    config.request_headers_size_log_limit =
        value["request_headers_size_log_limit"].As<size_t>(handler_defaults.request_headers_size_log_limit);
    config.response_data_size_log_limit =
        value["response_data_size_log_limit"].As<size_t>(handler_defaults.response_data_size_log_limit);
    config.max_requests_per_second = value["max_requests_per_second"].As<std::optional<size_t>>();
    config.decompress_request = value["decompress_request"].As<bool>(true);
    config.throttling_enabled = value["throttling_enabled"].As<bool>(true);
    config.set_response_server_hostname = value["set-response-server-hostname"].As<std::optional<bool>>();

    config.response_body_stream = value["response-body-stream"].As<bool>(false);

    if (config.max_requests_per_second && config.max_requests_per_second.value() <= 0) {
        throw std::runtime_error(
            "max_requests_per_second should be greater than 0, current value is " +
            std::to_string(config.max_requests_per_second.value())
        );
    }

    config.set_tracing_headers = value["set_tracing_headers"].As<bool>(handler_defaults.set_tracing_headers);

    config.deadline_propagation_enabled =
        value["deadline_propagation_enabled"].As<bool>(handler_defaults.deadline_propagation_enabled);

    config.deadline_expired_status_code =
        value["deadline_expired_status_code"].As<http::HttpStatus>(handler_defaults.deadline_expired_status_code);

    return config;
}

}  // namespace server::handlers

USERVER_NAMESPACE_END
