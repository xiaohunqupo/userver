#pragma once

/// @file userver/storages/mongo/pool_config.hpp
/// @brief @copybrief storages::mongo::PoolConfig

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

#include <userver/components/component_fwd.hpp>
#include <userver/congestion_control/controllers/linear.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::mongo {

enum class StatsVerbosity {
    kTerse,  ///< Only pool stats and read/write overalls by collection
    kFull,   ///< Stats with separate metrics per operation type and label
    kNone,   ///< No stats at all
};

/// @brief Mongo connection pool options
///
/// Dynamic option @ref MONGO_CONNECTION_POOL_SETTINGS
struct PoolSettings final {
    /// Default initial connection count
    static constexpr size_t kDefaultInitialSize = 16;
    /// Default total connections limit
    static constexpr size_t kDefaultMaxSize = 128;
    /// Default idle connections limit
    static constexpr size_t kDefaultIdleLimit = 64;
    /// Default establishing connections limit
    static constexpr size_t kDefaultConnectingLimit = 8;

    /// Initial connection count
    size_t initial_size = kDefaultInitialSize;
    /// Total connections limit
    size_t max_size = kDefaultMaxSize;
    /// Idle connections limit
    size_t idle_limit = kDefaultIdleLimit;
    /// Establishing connections limit
    size_t connecting_limit = kDefaultConnectingLimit;

    /// @throws InvalidConfigException if pool settings are invalid
    void Validate(const std::string& pool_id) const;
};

PoolSettings Parse(const formats::json::Value& config, formats::parse::To<PoolSettings>);

PoolSettings Parse(const yaml_config::YamlConfig& config, formats::parse::To<PoolSettings>);

/// MongoDB connection pool configuration
struct PoolConfig final {
    enum class DriverImpl {
        kMongoCDriver,
    };

    /// Default connection timeout
    static constexpr auto kDefaultConnTimeout = std::chrono::seconds{2};
    /// Default socket timeout
    static constexpr auto kDefaultSoTimeout = std::chrono::seconds{10};
    /// Default connection queue timeout
    static constexpr auto kDefaultQueueTimeout = std::chrono::seconds{1};
    /// Default pool maintenance period
    static constexpr auto kDefaultMaintenancePeriod = std::chrono::seconds{15};
    /// Default application name
    static constexpr char kDefaultAppName[] = "userver";

    /// @throws InvalidConfigException if the config is invalid
    void Validate(const std::string& pool_id) const;

    /// Connection (I/O) timeout
    std::chrono::milliseconds conn_timeout = kDefaultConnTimeout;
    /// Socket (I/O) timeout
    std::chrono::milliseconds so_timeout = kDefaultSoTimeout;
    /// Connection queue wait time
    std::chrono::milliseconds queue_timeout = kDefaultQueueTimeout;
    /// settings for connections pool
    PoolSettings pool_settings{};
    /// Instance selection latency window override
    std::optional<std::chrono::milliseconds> local_threshold{};
    /// Pool maintenance period
    std::chrono::milliseconds maintenance_period = kDefaultMaintenancePeriod;

    /// Application name (sent to server)
    std::string app_name = kDefaultAppName;
    /// Default max replication lag for the pool
    std::optional<std::chrono::seconds> max_replication_lag;

    /// Driver implementation to use
    DriverImpl driver_impl = DriverImpl::kMongoCDriver;

    /// Whether to write detailed stats
    StatsVerbosity stats_verbosity = StatsVerbosity::kTerse;

    /// Congestion control config
    congestion_control::v2::LinearController::StaticConfig cc_config;
};

PoolConfig Parse(const yaml_config::YamlConfig& config, formats::parse::To<PoolConfig>);

}  // namespace storages::mongo

USERVER_NAMESPACE_END
