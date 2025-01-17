#include <userver/server/request/request_config.hpp>

#include <server/http/parse_http_status.hpp>
#include <userver/logging/level_serialization.hpp>
#include <userver/utils/trivial_map.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::request {

HttpRequestConfig Parse(const yaml_config::YamlConfig& value, formats::parse::To<HttpRequestConfig>) {
    HttpRequestConfig conf{};

    conf.max_url_size = value["max_url_size"].As<size_t>(conf.max_url_size);

    conf.max_request_size = value["max_request_size"].As<size_t>(conf.max_request_size);

    conf.max_headers_size = value["max_headers_size"].As<size_t>(conf.max_headers_size);

    conf.request_body_size_log_limit =
        value["request_body_size_log_limit"].As<size_t>(conf.request_body_size_log_limit);

    conf.request_headers_size_log_limit =
        value["request_headers_size_log_limit"].As<size_t>(conf.request_headers_size_log_limit);

    conf.response_data_size_log_limit =
        value["response_data_size_log_limit"].As<size_t>(conf.response_data_size_log_limit);

    conf.parse_args_from_body = value["parse_args_from_body"].As<bool>(conf.parse_args_from_body);

    conf.set_tracing_headers = value["set_tracing_headers"].As<bool>(conf.set_tracing_headers);

    conf.deadline_propagation_enabled =
        value["deadline_propagation_enabled"].As<bool>(conf.deadline_propagation_enabled);

    conf.deadline_expired_status_code =
        value["deadline_expired_status_code"].As<http::HttpStatus>(conf.deadline_expired_status_code);

    return conf;
}

}  // namespace server::request

USERVER_NAMESPACE_END
