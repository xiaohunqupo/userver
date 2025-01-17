#pragma once

/// @file userver/storages/redis/component.hpp
/// @brief @copybrief components::Redis

#include <memory>
#include <string>
#include <unordered_map>

#include <userver/components/component_base.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/rcu/rcu.hpp>
#include <userver/storages/redis/base.hpp>
#include <userver/storages/redis/fwd.hpp>
#include <userver/storages/redis/wait_connected_mode.hpp>
#include <userver/storages/secdist/secdist.hpp>
#include <userver/testsuite/redis_control.hpp>
#include <userver/utils/statistics/entry.hpp>

USERVER_NAMESPACE_BEGIN

/// Components, clients and helpers for different databases and storages
namespace storages {}

/// Redis client and helpers
namespace storages::redis {

class SubscribeClientImpl;

namespace impl {
class Sentinel;
class ThreadPools;
}  // namespace impl
}  // namespace storages::redis

namespace components {

// clang-format off

/// @ingroup userver_components
///
/// @brief Redis client component
///
/// Provides access to a redis cluster.
///
/// ## Dynamic options:
/// * @ref REDIS_COMMANDS_BUFFERING_SETTINGS
/// * @ref REDIS_DEFAULT_COMMAND_CONTROL
/// * @ref REDIS_METRICS_SETTINGS
/// * @ref REDIS_PUBSUB_METRICS_SETTINGS
/// * @ref REDIS_RETRY_BUDGET_SETTINGS
/// * @ref REDIS_REPLICA_MONITORING_SETTINGS
/// * @ref REDIS_SUBSCRIBER_DEFAULT_COMMAND_CONTROL
/// * @ref REDIS_SUBSCRIPTIONS_REBALANCE_MIN_INTERVAL_SECONDS
/// * @ref REDIS_WAIT_CONNECTED
///
/// ## Static options:
/// Name | Description | Default value
/// ---- | ----------- | -------------
/// thread_pools.redis_thread_pool_size | thread count to serve Redis requests | -
/// thread_pools.sentinel_thread_pool_size | thread count to serve sentinel requests. | -
/// groups | array of redis clusters to work with excluding subscribers | -
/// groups.[].config_name | key name in secdist with options for this cluster | -
/// groups.[].db | name to refer to the cluster in components::Redis::GetClient() | -
/// groups.[].sharding_strategy | one of RedisCluster, KeyShardCrc32, KeyShardTaximeterCrc32 or KeyShardGpsStorageDriver | "KeyShardTaximeterCrc32"
/// groups.[].allow_reads_from_master | allows read requests from master instance | false
/// subscribe_groups | array of redis clusters to work with in subscribe mode | -
/// subscribe_groups.[].config_name | key name in secdist with options for this cluster | -
/// subscribe_groups.[].db | name to refer to the cluster in components::Redis::GetSubscribeClient() | -
/// subscribe_groups.[].sharding_strategy | either RedisCluster or KeyShardTaximeterCrc32 | "KeyShardTaximeterCrc32"
///
/// ## Static configuration example:
///
/// ```
///    # yaml
///    redis:
///        groups:
///          - config_name: cats
///            db: hello_service_cats_catalogue
///            sharding_strategy: RedisCluster
///          - config_name: dogs
///            db: hello_service_dogs_catalogue
///        subscribe_groups:
///          - config_name: food
///            db: hello_service_pet_food_orders
///        thread_pools:
///            redis_thread_pool_size: 8
///            sentinel_thread_pool_size: 1
/// ```
///
/// ## Secdist format
///
/// If a `config_name` option is provided, for example
/// `groups.some.config_name: some_name_of_your_database`, then the Secdist
/// entry for that alias should look like following:
/// @code{.json}
/// {
///   "redis_settings": {
///     "some_name_of_your_database": {
///       "password": "the_password_of_your_database",
///       "sentinels": [
///         {"host": "the_host1_of_your_database", "port": 11564}
///       ],
///       "shards": [
///         {"name": "test_master0"}
///       ]
///     }
///   }
/// }
/// @endcode
///
/// ## Cluster Redis setup
///
/// Redis cluster is the new recommended way of setting up Redis servers
/// with improved stability.
///
/// To start, set `sharding_strategy: RedisCluster` in the static config
/// as shown above.
///
/// Secdist configuration is simplified:
///
/// 1. `"shards"` field is ignored, you can specify an empty array there;
/// 2. `"sentinels"` field should contain some of the cluster nodes. They are
///    only used for topology discovery; it is not necessary to list all nodes.

// clang-format on
class Redis : public ComponentBase {
public:
    Redis(const ComponentConfig& config, const ComponentContext& component_context);

    ~Redis() override;

    /// @ingroup userver_component_names
    /// @brief The default name of components::Redis
    static constexpr std::string_view kName = "redis";

    std::shared_ptr<storages::redis::Client>
    GetClient(const std::string& name, storages::redis::RedisWaitConnected wait_connected = {}) const;
    [[deprecated("use GetClient()")]] std::shared_ptr<storages::redis::impl::Sentinel> Client(const std::string& name
    ) const;
    std::shared_ptr<storages::redis::SubscribeClient>
    GetSubscribeClient(const std::string& name, storages::redis::RedisWaitConnected wait_connected = {}) const;

    static yaml_config::Schema GetStaticConfigSchema();

private:
    void OnConfigUpdate(const dynamic_config::Snapshot& cfg);
    void OnSecdistUpdate(const storages::secdist::SecdistConfig& cfg);

    void Connect(
        const ComponentConfig& config,
        const ComponentContext& component_context,
        const testsuite::RedisControl& testsuite_redis_control
    );

    void WriteStatistics(utils::statistics::Writer& writer);
    void WriteStatisticsPubsub(utils::statistics::Writer& writer);

    std::shared_ptr<storages::redis::impl::ThreadPools> thread_pools_;
    std::unordered_map<std::string, std::shared_ptr<storages::redis::impl::Sentinel>> sentinels_;
    std::unordered_map<std::string, std::shared_ptr<storages::redis::Client>> clients_;
    std::unordered_map<std::string, std::shared_ptr<storages::redis::SubscribeClientImpl>> subscribe_clients_;

    dynamic_config::Source config_;
    concurrent::AsyncEventSubscriberScope config_subscription_;
    concurrent::AsyncEventSubscriberScope secdist_subscription_;

    utils::statistics::Entry statistics_holder_;
    utils::statistics::Entry subscribe_statistics_holder_;

    storages::redis::MetricsSettings::StaticSettings static_metrics_settings_;
    rcu::Variable<storages::redis::MetricsSettings> metrics_settings_;
    rcu::Variable<storages::redis::PubsubMetricsSettings> pubsub_metrics_settings_;
};

template <>
inline constexpr bool kHasValidate<Redis> = true;

}  // namespace components

USERVER_NAMESPACE_END
