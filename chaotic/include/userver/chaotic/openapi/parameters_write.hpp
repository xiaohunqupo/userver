#pragma once

#include <fmt/args.h>

#include <userver/chaotic/openapi/parameters.hpp>
#include <userver/clients/http/request.hpp>
#include <userver/http/url.hpp>
#include <userver/utils/text_light.hpp>

USERVER_NAMESPACE_BEGIN

namespace chaotic::openapi {

/// @brief curl::easy abstraction
class ParameterSinkBase {
public:
    virtual void SetCookie(std::string_view, std::string&&) = 0;
    virtual void SetHeader(std::string_view, std::string&&) = 0;
    virtual void SetPath(Name&, std::string&&) = 0;
    virtual void SetQuery(std::string_view, std::string&&) = 0;
    virtual void SetMultiQuery(std::string_view, std::vector<std::string>&&) = 0;
};

class ParameterSinkHttpClient final : public ParameterSinkBase {
public:
    ParameterSinkHttpClient(clients::http::Request& request, std::string&& url_pattern);

    void SetCookie(std::string_view name, std::string&& value) override;

    void SetHeader(std::string_view name, std::string&& value) override;

    void SetPath(Name& name, std::string&& value) override;

    void SetQuery(std::string_view name, std::string&& value) override;

    void SetMultiQuery(std::string_view name, std::vector<std::string>&& value) override;

    void Flush();

private:
    std::string url_pattern_;
    clients::http::Request& request_;
    clients::http::Headers headers_;
    http::MultiArgs query_args_;
    std::unordered_map<std::string, std::string> cookies_;
    fmt::dynamic_format_arg_store<fmt::format_context> path_vars_;
};

void ValidatePathVariableValue(std::string_view name, std::string_view value);

template <In In, typename RawType>
void SetParameter(Name& name, RawType&& raw_value, ParameterSinkBase& dest) {
    if constexpr (In == In::kPath) {
        USERVER_NAMESPACE::chaotic::openapi::ValidatePathVariableValue(name, raw_value);
        dest.SetPath(name, std::forward<RawType>(raw_value));
    } else if constexpr (In == In::kCookie) {
        dest.SetCookie(name, std::forward<RawType>(raw_value));
    } else if constexpr (In == In::kHeader) {
        dest.SetHeader(name, std::forward<RawType>(raw_value));
    } else if constexpr (In == In::kQuery) {
        dest.SetQuery(name, std::forward<RawType>(raw_value));
    } else {
        static_assert(In == In::kQueryExplode, "Unknown 'In'");
        dest.SetMultiQuery(name, std::forward<RawType>(raw_value));
    }
}

template <typename T>
std::enable_if_t<std::is_integral_v<T>, std::string> ToRawParameter(T s) {
    return std::to_string(s);
}

template <typename T>
std::enable_if_t<std::is_enum_v<T>, std::string> ToRawParameter(T s) {
    return ToString(s);
}

std::string ToRawParameter(std::string&& s);

std::vector<std::string> ToRawParameter(std::vector<std::string>&& s);

template <typename Parameter>
auto SerializeParameter(const typename Parameter::UserType& value) {
    if constexpr (Parameter::Type == ParameterType::kTrivial) {
        // TODO: Convert()
        return USERVER_NAMESPACE::chaotic::openapi::ToRawParameter(typename Parameter::UserType{value});
    } else if constexpr (Parameter::Type == ParameterType::kArray) {
        // TODO: Convert()
        return USERVER_NAMESPACE::utils::text::Join(value, std::string(1, Parameter::kDelimiter));
    }
}

template <typename Parameter>
void WriteParameter(const typename Parameter::UserType& value, ParameterSinkBase& dest) {
    auto raw_value = USERVER_NAMESPACE::chaotic::openapi::SerializeParameter<Parameter>(value);
    USERVER_NAMESPACE::chaotic::openapi::SetParameter<Parameter::kIn>(Parameter::kName, std::move(raw_value), dest);
}

}  // namespace chaotic::openapi

USERVER_NAMESPACE_END
