#include <userver/chaotic/openapi/parameters_write.hpp>

#include <fmt/format.h>

USERVER_NAMESPACE_BEGIN

namespace chaotic::openapi {

ParameterSinkHttpClient::ParameterSinkHttpClient(clients::http::Request& request, std::string&& url_pattern)
    : url_pattern_(std::move(url_pattern)), request_(request) {}

void ParameterSinkHttpClient::SetCookie(std::string_view name, std::string&& value) { cookies_.emplace(name, value); }

void ParameterSinkHttpClient::SetHeader(std::string_view name, std::string&& value) { headers_.emplace(name, value); }

void ParameterSinkHttpClient::SetPath(Name& name, std::string&& value) {
    // Note: passing tmp value to fmt::arg() is OK and not a UAF
    // since fmt::dynamic_format_arg_store copies the data into itself.

    // Note2: an ugly std::string{}.c_str() is needed because
    // fmt::arg() wants char* :-(
    path_vars_.push_back(fmt::arg(name, value));
}

void ParameterSinkHttpClient::SetQuery(std::string_view name, std::string&& value) {
    query_args_.insert({std::string{name}, std::move(value)});
}

void ParameterSinkHttpClient::SetMultiQuery(std::string_view name, std::vector<std::string>&& value) {
    for (auto&& item : value) {
        query_args_.insert({std::string{name}, std::move(item)});
    }
}

void ParameterSinkHttpClient::Flush() {
    request_.url(http::MakeUrl(fmt::vformat(url_pattern_, path_vars_), {}, query_args_));
    request_.headers(std::move(headers_));
    request_.cookies(std::move(cookies_));
}

std::string ToRawParameter(std::string&& s) { return s; }

std::vector<std::string> ToRawParameter(std::vector<std::string>&& s) { return s; }

void ValidatePathVariableValue(std::string_view name, std::string_view value) {
    if (value.find('/') != std::string::npos || value.find('?') != std::string::npos) {
        throw std::runtime_error(fmt::format("Forbidden symbol in path variable value: {}='{}'", name, value));
    }
}

}  // namespace chaotic::openapi

USERVER_NAMESPACE_END
