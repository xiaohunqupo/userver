#include "enum.hpp"

#include <userver/chaotic/type_bundle_cpp.hpp>

#include "enum_parsers.ipp"

namespace ns {

bool operator==(const ns::Enum& lhs, const ns::Enum& rhs) { return lhs.foo == rhs.foo && true; }

USERVER_NAMESPACE::logging::LogHelper&
operator<<(USERVER_NAMESPACE::logging::LogHelper& lh, const ns::Enum::Foo& value) {
    return lh << ToString(value);
}

USERVER_NAMESPACE::logging::LogHelper& operator<<(USERVER_NAMESPACE::logging::LogHelper& lh, const ns::Enum& value) {
    return lh << ToString(USERVER_NAMESPACE::formats::json::ValueBuilder(value).ExtractValue());
}

Enum::Foo Parse(USERVER_NAMESPACE::formats::json::Value json, USERVER_NAMESPACE::formats::parse::To<ns::Enum::Foo> to) {
    return Parse<USERVER_NAMESPACE::formats::json::Value>(json, to);
}

Enum Parse(USERVER_NAMESPACE::formats::json::Value json, USERVER_NAMESPACE::formats::parse::To<ns::Enum> to) {
    return Parse<USERVER_NAMESPACE::formats::json::Value>(json, to);
}

Enum::Foo Parse(USERVER_NAMESPACE::formats::yaml::Value json, USERVER_NAMESPACE::formats::parse::To<ns::Enum::Foo> to) {
    return Parse<USERVER_NAMESPACE::formats::yaml::Value>(json, to);
}

Enum Parse(USERVER_NAMESPACE::formats::yaml::Value json, USERVER_NAMESPACE::formats::parse::To<ns::Enum> to) {
    return Parse<USERVER_NAMESPACE::formats::yaml::Value>(json, to);
}

Enum::Foo Parse(USERVER_NAMESPACE::yaml_config::Value json, USERVER_NAMESPACE::formats::parse::To<ns::Enum::Foo> to) {
    return Parse<USERVER_NAMESPACE::yaml_config::Value>(json, to);
}

Enum Parse(USERVER_NAMESPACE::yaml_config::Value json, USERVER_NAMESPACE::formats::parse::To<ns::Enum> to) {
    return Parse<USERVER_NAMESPACE::yaml_config::Value>(json, to);
}

ns::Enum::Foo FromString(std::string_view value, USERVER_NAMESPACE::formats::parse::To<ns::Enum::Foo>) {
    const auto result = kns__Enum__Foo_Mapping.TryFindBySecond(value);
    if (result.has_value()) {
        return *result;
    }
    throw std::runtime_error(fmt::format("Invalid enum value ({}) for type ns::Enum::Foo", value));
}

ns::Enum::Foo Parse(std::string_view value, USERVER_NAMESPACE::formats::parse::To<ns::Enum::Foo> to) {
    return FromString(value, to);
}

USERVER_NAMESPACE::formats::json::Value
Serialize(const ns::Enum::Foo& value, USERVER_NAMESPACE::formats::serialize::To<USERVER_NAMESPACE::formats::json::Value>) {
    return USERVER_NAMESPACE::formats::json::ValueBuilder(ToString(value)).ExtractValue();
}

USERVER_NAMESPACE::formats::json::Value
Serialize([[maybe_unused]] const ns::Enum& value, USERVER_NAMESPACE::formats::serialize::To<USERVER_NAMESPACE::formats::json::Value>) {
    USERVER_NAMESPACE::formats::json::ValueBuilder vb = USERVER_NAMESPACE::formats::common::Type::kObject;

    if (value.foo) {
        vb["foo"] = USERVER_NAMESPACE::chaotic::Primitive<ns::Enum::Foo>{*value.foo};
    }

    return vb.ExtractValue();
}

std::string ToString(ns::Enum::Foo value) {
    const auto result = kns__Enum__Foo_Mapping.TryFindByFirst(value);
    if (result.has_value()) {
        return std::string{*result};
    }
    throw std::runtime_error("Bad enum value");
}

}  // namespace ns
