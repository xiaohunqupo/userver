#include <storages/mongo/pool_impl.hpp>

#include <storages/mongo/cc_config.hpp>
#include <storages/mongo/dynamic_config.hpp>
#include <userver/dynamic_config/value.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::mongo::impl {

PoolImpl::PoolImpl(std::string&& id, const PoolConfig& static_config, dynamic_config::Source config_source)
    : id_(std::move(id)),
      stats_verbosity_(static_config.stats_verbosity),
      config_source_(config_source),
      cc_sensor_(*this),
      cc_limiter_(*this),
      cc_controller_(
          id_,
          cc_sensor_,
          cc_limiter_,
          statistics_.congestion_control,
          static_config.cc_config,
          config_source,
          [](const dynamic_config::Snapshot& config) { return config[kCcConfig]; }
      ) {}

void PoolImpl::Start() {
    config_subscriber_ = config_source_.UpdateAndListen(this, "mongo_pool", &PoolImpl::OnConfigUpdate);
    cc_controller_.Start();
}

void PoolImpl::Stop() noexcept {
    cc_controller_.Stop();
    config_subscriber_.Unsubscribe();
}

void PoolImpl::OnConfigUpdate(const dynamic_config::Snapshot& config) {
    bool cc_enabled =
        config[kCongestionControlDatabasesSettings].GetOptional(id_).value_or(config[kCongestionControlEnabled]);
    cc_controller_.SetEnabled(cc_enabled);

    const auto new_pool_settings = config[kPoolSettings].GetOptional(id_);
    if (new_pool_settings.has_value()) {
        new_pool_settings->Validate(id_);
        SetPoolSettings(new_pool_settings.value());
    }
}

const std::string& PoolImpl::Id() const { return id_; }

const stats::PoolStatistics& PoolImpl::GetStatistics() const { return statistics_; }

stats::PoolStatistics& PoolImpl::GetStatistics() { return statistics_; }

dynamic_config::Snapshot PoolImpl::GetConfig() const { return config_source_.GetSnapshot(); }

StatsVerbosity PoolImpl::GetStatsVerbosity() const { return stats_verbosity_; }

}  // namespace storages::mongo::impl

USERVER_NAMESPACE_END
