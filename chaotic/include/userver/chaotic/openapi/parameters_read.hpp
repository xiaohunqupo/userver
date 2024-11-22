#pragma once

#include <userver/chaotic/openapi/parameters.hpp>

#include <userver/server/http/http_request.hpp>

USERVER_NAMESPACE_BEGIN

namespace chaotic::openapi {

template <In In_>
auto GetParameter(std::string_view name, const server::http::HttpRequest& source) {
    if constexpr (In_ == In::kPath) {
        return source.GetPathArg(name);
    } else if constexpr (In_ == In::kCookie) {
        return source.GetCookie(std::string{name});
    } else if constexpr (In_ == In::kHeader) {
        return source.GetHeader(name);
    } else if constexpr (In_ == In::kQuery) {
        return source.GetArg(name);
    } else {
        static_assert(In_ == In::kQueryExplode, "Unknown 'In'");
        return source.GetArgVector(name);
    }
}

template <typename T>
auto ParseParameter(const T& raw_value) {
    return raw_value;
}

template <typename Parameter>
auto ReadParameter(const server::http::HttpRequest& source) {
    auto raw_value = USERVER_NAMESPACE::chaotic::openapi::GetParameter<Parameter::kIn>(Parameter::kName, source);
    return USERVER_NAMESPACE::chaotic::openapi::ParseParameter(raw_value);
}

}  // namespace chaotic::openapi

USERVER_NAMESPACE_END
