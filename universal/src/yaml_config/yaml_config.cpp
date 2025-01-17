#include <userver/yaml_config/yaml_config.hpp>

#include <fmt/format.h>
#include <boost/filesystem/operations.hpp>

#include <userver/formats/common/conversion_stack.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/formats/yaml/serialize.hpp>
#include <userver/formats/yaml/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/utils/string_to_duration.hpp>
#include <userver/utils/text_light.hpp>

USERVER_NAMESPACE_BEGIN

namespace yaml_config {

namespace {

bool IsSubstitution(const formats::yaml::Value& value) {
    if (!value.IsString()) return false;
    const auto& str = value.As<std::string>();
    return !str.empty() && str.front() == '$';
}

std::string GetSubstitutionVarName(const formats::yaml::Value& value) { return value.As<std::string>().substr(1); }

std::string GetEnvName(std::string_view str) { return std::string{str} + "#env"; }

std::string GetFileName(std::string_view str) { return std::string{str} + "#file"; }

std::string GetFallbackName(std::string_view str) { return std::string{str} + "#fallback"; }

template <typename Field>
YamlConfig MakeMissingConfig(const YamlConfig& config, Field field) {
    const auto path = formats::common::MakeChildPath(config.GetPath(), field);
    return {formats::yaml::Value()[path], {}};
}

void AssertEnvMode(YamlConfig::Mode mode) {
    if (mode == YamlConfig::Mode::kSecure) {
        throw std::runtime_error(
            "YamlConfig was not constructed with Mode::kEnvAllowed or "
            "Mode::kEnvAndFileAllowed but an attempt "
            "to read an environment variable was made"
        );
    }
}

void AssertFileMode(YamlConfig::Mode mode) {
    if (mode != YamlConfig::Mode::kEnvAndFileAllowed) {
        throw std::runtime_error(
            "YamlConfig was not constructed with Mode::kEnvAndFileAllowed "
            "but an attempt to read a file was made"
        );
    }
}

std::optional<formats::yaml::Value> GetFromEnvImpl(const formats::yaml::Value& env_name, YamlConfig::Mode mode) {
    if (env_name.IsMissing()) {
        return {};
    }

    AssertEnvMode(mode);

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const auto* env_value = std::getenv(env_name.As<std::string>().c_str());
    if (env_value) {
        return formats::yaml::FromString(env_value);
    }

    return {};
}

std::optional<formats::yaml::Value> GetFromFileImpl(const formats::yaml::Value& file_name, YamlConfig::Mode mode) {
    if (file_name.IsMissing()) {
        return {};
    }

    AssertFileMode(mode);
    const auto str_filename = file_name.As<std::string>();
    if (!boost::filesystem::exists(str_filename)) {
        return {};
    }
    return formats::yaml::blocking::FromFile(str_filename);
}

std::optional<YamlConfig> GetSharpCommandValue(
    const formats::yaml::Value& yaml,
    YamlConfig::Mode mode,
    std::string_view key,
    bool met_substitution
) {
    const auto env_name = yaml[GetEnvName(key)];
    auto env_value = GetFromEnvImpl(env_name, mode);
    if (env_value) {
        env_value = env_value->CloneWithReplacedPath(yaml[key].GetPath());
        // Strip substitutions off to disallow nested substitutions
        return YamlConfig{std::move(*env_value), {}, YamlConfig::Mode::kSecure};
    }

    const auto file_name = yaml[GetFileName(key)];
    auto file_value = GetFromFileImpl(file_name, mode);
    if (file_value) {
        file_value = file_value->CloneWithReplacedPath(yaml[key].GetPath());
        // Strip substitutions off to disallow nested substitutions
        return YamlConfig{std::move(*file_value), {}, YamlConfig::Mode::kSecure};
    }

    if (met_substitution || !env_name.IsMissing() || !file_name.IsMissing()) {
        const auto fallback_name = GetFallbackName(key);
        if (yaml.HasMember(fallback_name)) {
            LOG_INFO() << "using fallback value for '" << key << '\'';
            // Strip substitutions off to disallow nested substitutions
            return YamlConfig{
                yaml[fallback_name].CloneWithReplacedPath(yaml[key].GetPath()), {}, YamlConfig::Mode::kSecure};
        }
    }

    return {};
}

std::optional<YamlConfig> GetYamlConfig(
    const formats::yaml::Value& yaml,
    const formats::yaml::Value& config_vars,
    YamlConfig::Mode mode,
    std::string_view key
) {
    auto value = yaml[key];

    const bool is_substitution = IsSubstitution(value);
    if (is_substitution) {
        const auto var_name = GetSubstitutionVarName(value);
        auto var_data = config_vars[var_name];
        if (!var_data.IsMissing()) {
            var_data = var_data.CloneWithReplacedPath(value.GetPath());
            // Strip substitutions off to disallow nested substitutions
            return YamlConfig{std::move(var_data), {}, YamlConfig::Mode::kSecure};
        }

        auto res = GetSharpCommandValue(
            config_vars,
            mode,
            var_name,
            /*met_substitution*/ false
        );
        if (res) {
            return YamlConfig{
                res->GetRawYamlWithoutConfigVars().CloneWithReplacedPath(value.GetPath()),
                {},
                YamlConfig::Mode::kSecure,
            };
        }
    }

    if (!value.IsMissing() && !is_substitution) {
        return YamlConfig{std::move(value), config_vars, mode};
    }

    return GetSharpCommandValue(
        yaml,
        mode,
        key,
        /*met_substitution*/ is_substitution
    );
}

}  // namespace

YamlConfig::YamlConfig(formats::yaml::Value yaml, formats::yaml::Value config_vars, Mode mode)
    : yaml_(std::move(yaml)), config_vars_(std::move(config_vars)), mode_(mode) {}

YamlConfig YamlConfig::operator[](std::string_view key) const {
    if (utils::text::EndsWith(key, "#env") || utils::text::EndsWith(key, "#file") ||
        utils::text::EndsWith(key, "#fallback")) {
        UASSERT_MSG(false, "Do not use names ending on #env, #file and #fallback");
        return MakeMissingConfig(*this, key);
    }

    auto yaml_config = GetYamlConfig(yaml_, config_vars_, mode_, key);
    if (yaml_config) {
        return std::move(*yaml_config);
    }

    return MakeMissingConfig(*this, key);
}

YamlConfig YamlConfig::operator[](size_t index) const {
    auto value = yaml_[index];

    if (IsSubstitution(value)) {
        const auto var_name = GetSubstitutionVarName(value);

        auto var_data = config_vars_[var_name];
        if (!var_data.IsMissing()) {
            var_data = var_data.CloneWithReplacedPath(value.GetPath());
            // Strip substitutions off to disallow nested substitutions
            return YamlConfig{std::move(var_data), {}, Mode::kSecure};
        }

        auto res = GetSharpCommandValue(
            config_vars_,
            mode_,
            var_name,
            /*met_substitution*/ false
        );
        if (res) {
            return YamlConfig{
                res->GetRawYamlWithoutConfigVars().CloneWithReplacedPath(value.GetPath()),
                {},
                YamlConfig::Mode::kSecure,
            };
        }

        // Avoid parsing $substitution as a string
        return MakeMissingConfig(*this, index)[value.As<std::string>()];
    }

    return {std::move(value), config_vars_};
}

std::size_t YamlConfig::GetSize() const { return yaml_.GetSize(); }

bool YamlConfig::IsMissing() const noexcept { return yaml_.IsMissing(); }

bool YamlConfig::IsNull() const noexcept { return yaml_.IsNull(); }

bool YamlConfig::IsBool() const noexcept { return yaml_.IsBool(); }

bool YamlConfig::IsInt() const noexcept { return yaml_.IsInt(); }

bool YamlConfig::IsInt64() const noexcept { return yaml_.IsInt64(); }

bool YamlConfig::IsUInt64() const noexcept { return yaml_.IsUInt64(); }

bool YamlConfig::IsDouble() const noexcept { return yaml_.IsDouble(); }

bool YamlConfig::IsString() const noexcept { return yaml_.IsString(); }

bool YamlConfig::IsArray() const noexcept { return yaml_.IsArray(); }

bool YamlConfig::IsObject() const noexcept { return yaml_.IsObject(); }

void YamlConfig::CheckNotMissing() const { return yaml_.CheckNotMissing(); }

void YamlConfig::CheckArray() const { return yaml_.CheckArray(); }

void YamlConfig::CheckArrayOrNull() const { yaml_.CheckArrayOrNull(); }

void YamlConfig::CheckObjectOrNull() const { yaml_.CheckObjectOrNull(); }

void YamlConfig::CheckObject() const { yaml_.CheckObject(); }

void YamlConfig::CheckString() const { yaml_.CheckString(); }

void YamlConfig::CheckObjectOrArrayOrNull() const { yaml_.CheckObjectOrArrayOrNull(); }

bool YamlConfig::HasMember(std::string_view key) const { return yaml_.HasMember(key); }

std::string YamlConfig::GetPath() const { return yaml_.GetPath(); }

YamlConfig::const_iterator YamlConfig::begin() const { return const_iterator{*this, yaml_.begin()}; }

YamlConfig::const_iterator YamlConfig::end() const { return const_iterator{*this, yaml_.end()}; }

formats::yaml::Value YamlConfig::GetRawYamlWithoutConfigVars() const { return yaml_; }

bool Parse(const YamlConfig& value, formats::parse::To<bool>) { return value.yaml_.As<bool>(); }

int64_t Parse(const YamlConfig& value, formats::parse::To<int64_t>) { return value.yaml_.As<int64_t>(); }

uint64_t Parse(const YamlConfig& value, formats::parse::To<uint64_t>) { return value.yaml_.As<uint64_t>(); }

double Parse(const YamlConfig& value, formats::parse::To<double>) { return value.yaml_.As<double>(); }

std::string Parse(const YamlConfig& value, formats::parse::To<std::string>) { return value.yaml_.As<std::string>(); }

std::chrono::seconds Parse(const YamlConfig& value, formats::parse::To<std::chrono::seconds>) {
    const auto as_milliseconds = Parse(value, formats::parse::To<std::chrono::milliseconds>{});
    const auto as_seconds = std::chrono::duration_cast<std::chrono::seconds>(as_milliseconds);

    if (as_seconds != as_milliseconds) {
        throw ParseException(fmt::format(
            "While parsing '{}': '{}' cannot be represented "
            "as milliseconds without precision loss",
            value.GetPath(),
            value.As<std::string>()
        ));
    }

    return as_seconds;
}

std::chrono::milliseconds Parse(const YamlConfig& value, formats::parse::To<std::chrono::milliseconds>) {
    const auto as_string = value.As<std::string>();
    try {
        return utils::StringToDuration(as_string);
    } catch (const std::exception& ex) {
        throw ParseException(fmt::format("While parsing '{}': {}", value.GetPath(), ex.what()));
    }
}

formats::json::Value Parse(const YamlConfig& value, formats::parse::To<formats::json::Value>) {
    return formats::common::PerformMinimalFormatConversion<formats::json::Value>(value);
}

}  // namespace yaml_config

USERVER_NAMESPACE_END
