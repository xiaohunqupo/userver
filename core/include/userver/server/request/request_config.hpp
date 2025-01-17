#pragma once

#include <cstdint>

#include <userver/http/http_version.hpp>
#include <userver/server/http/http_status.hpp>
#include <userver/yaml_config/yaml_config.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::request {

struct HttpRequestConfig {
    std::size_t max_url_size = 8192;
    std::size_t max_request_size = 1024 * 1024;
    std::size_t max_headers_size = 65536;
    std::size_t request_body_size_log_limit = 512;
    std::size_t request_headers_size_log_limit = 512;
    std::size_t response_data_size_log_limit = 512;
    bool parse_args_from_body = false;
    bool testing_mode = false;
    bool decompress_request = false;
    bool set_tracing_headers = true;
    bool deadline_propagation_enabled = true;
    http::HttpStatus deadline_expired_status_code = http::HttpStatus{498};
};

HttpRequestConfig Parse(const yaml_config::YamlConfig& value, formats::parse::To<HttpRequestConfig>);

}  // namespace server::request

USERVER_NAMESPACE_END
