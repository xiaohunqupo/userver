#pragma once

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <userver/cache/update_type.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/formats/json_fwd.hpp>
#include <userver/yaml_config/fwd.hpp>

USERVER_NAMESPACE_BEGIN

namespace dump {
struct Config;
}  // namespace dump

namespace cache {

class ConfigError : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

enum class FirstUpdateMode {
    kRequired,
    kBestEffort,
    kSkip,
};

FirstUpdateMode Parse(const yaml_config::YamlConfig& config, formats::parse::To<FirstUpdateMode>);

std::string_view ToString(FirstUpdateMode);

enum class FirstUpdateType {
    kFull,
    kIncremental,
    kIncrementalThenAsyncFull,
};

FirstUpdateType Parse(const yaml_config::YamlConfig& config, formats::parse::To<FirstUpdateType>);

std::string_view ToString(FirstUpdateType);

struct ConfigPatch final {
    std::chrono::milliseconds update_interval{};
    std::chrono::milliseconds update_jitter{};
    std::chrono::milliseconds full_update_interval{};
    std::chrono::milliseconds full_update_jitter{};
    std::optional<std::chrono::milliseconds> exception_interval{};
    bool updates_enabled{true};
    std::uint64_t alert_on_failing_to_update_times{0};
};

ConfigPatch Parse(const formats::json::Value& value, formats::parse::To<ConfigPatch>);

struct Config final {
    Config() = default;
    explicit Config(const yaml_config::YamlConfig& config, const std::optional<dump::Config>& dump_config);

    Config MergeWith(const ConfigPatch& patch) const;

    AllowedUpdateTypes allowed_update_types{};
    bool allow_first_update_failure{};
    std::optional<bool> force_periodic_update;
    bool config_updates_enabled{};
    bool has_pre_assign_check{};
    std::optional<std::string> task_processor_name;
    std::chrono::milliseconds cleanup_interval{};
    bool is_strong_period{};
    std::optional<std::uint64_t> failed_updates_before_expiration;
    bool is_safe_data_lifetime{};

    FirstUpdateMode first_update_mode{};
    FirstUpdateType first_update_type{};

    std::chrono::milliseconds update_interval{};
    std::chrono::milliseconds update_jitter{};
    std::chrono::milliseconds full_update_interval{};
    std::chrono::milliseconds full_update_jitter{};
    std::optional<std::chrono::milliseconds> exception_interval;
    bool updates_enabled{};
    std::uint64_t alert_on_failing_to_update_times{};
};

extern const dynamic_config::Key<std::unordered_map<std::string, ConfigPatch>> kCacheConfigSet;

}  // namespace cache

USERVER_NAMESPACE_END
