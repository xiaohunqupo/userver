#include <userver/storages/mongo/multi_mongo.hpp>

#include <userver/storages/mongo/exception.hpp>
#include <userver/storages/secdist/exceptions.hpp>
#include <userver/storages/secdist/secdist.hpp>
#include <userver/utils/statistics/writer.hpp>

#include <storages/mongo/dynamic_config.hpp>
#include <storages/mongo/mongo_secdist.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::mongo {

MultiMongo::PoolSet::PoolSet(MultiMongo& target) : target_(&target), pool_map_ptr_(std::make_shared<PoolMap>()) {}

MultiMongo::PoolSet::PoolSet(const PoolSet& other) { *this = other; }

MultiMongo::PoolSet::PoolSet(PoolSet&&) noexcept = default;

MultiMongo::PoolSet& MultiMongo::PoolSet::operator=(const PoolSet& rhs) {
    if (this == &rhs) return *this;

    target_ = rhs.target_;
    pool_map_ptr_ = std::make_shared<PoolMap>(*rhs.pool_map_ptr_);
    return *this;
}

MultiMongo::PoolSet& MultiMongo::PoolSet::operator=(PoolSet&&) noexcept = default;

void MultiMongo::PoolSet::AddExistingPools() {
    const auto pool_map = target_->pool_map_.Read();
    pool_map_ptr_->insert(pool_map->begin(), pool_map->end());
}

void MultiMongo::PoolSet::AddPool(std::string dbalias) {
    auto pool_ptr = target_->FindPool(dbalias);

    if (!pool_ptr) {
        auto pool_config = target_->pool_config_;
        const auto pool_settings = target_->config_source_.GetSnapshot(kPoolSettings);
        const auto new_pool_settings = pool_settings->GetOptional(target_->name_);
        if (new_pool_settings.has_value()) {
            new_pool_settings->Validate(target_->name_);
            pool_config.pool_settings = new_pool_settings.value();
        }

        pool_ptr = std::make_shared<storages::mongo::Pool>(
            target_->name_ + ':' + dbalias,
            secdist::GetSecdistConnectionString(target_->secdist_, dbalias),
            pool_config,
            target_->dns_resolver_,
            target_->config_source_
        );
    }

    pool_map_ptr_->emplace(std::move(dbalias), std::move(pool_ptr));
}

bool MultiMongo::PoolSet::RemovePool(const std::string& dbalias) {
    if (const auto it = pool_map_ptr_->find(dbalias); it != pool_map_ptr_->end()) {
        pool_map_ptr_->erase(it);
        return true;
    }
    return false;
}

void MultiMongo::PoolSet::Activate() { target_->pool_map_.Assign(*pool_map_ptr_); }

MultiMongo::MultiMongo(
    std::string name,
    storages::secdist::Secdist& secdist,
    storages::mongo::PoolConfig pool_config,
    clients::dns::Resolver* dns_resolver,
    dynamic_config::Source config_source
)
    : name_(std::move(name)),
      secdist_(secdist),
      config_source_(config_source),
      pool_config_(std::move(pool_config)),
      dns_resolver_(dns_resolver) {
    config_subscriber_ = config_source_.UpdateAndListen(this, "multi_mongo", &MultiMongo::OnConfigUpdate);
    secdist_subscriber_ = secdist.UpdateAndListen(this, "multi_mongo", &MultiMongo::OnSecdistUpdate);
}

storages::mongo::PoolPtr MultiMongo::GetPool(const std::string& dbalias) const {
    auto pool_ptr = FindPool(dbalias);
    if (!pool_ptr) {
        throw storages::mongo::PoolNotFoundException("pool ") << dbalias << " is not in the working set";
    }
    return pool_ptr;
}

void MultiMongo::AddPool(std::string dbalias) {
    auto set = NewPoolSet();
    set.AddExistingPools();
    set.AddPool(std::move(dbalias));
    set.Activate();
}

bool MultiMongo::RemovePool(const std::string& dbalias) {
    if (!FindPool(dbalias)) return false;

    auto set = NewPoolSet();
    set.AddExistingPools();
    if (set.RemovePool(dbalias)) {
        set.Activate();
        return true;
    }
    return false;
}

MultiMongo::PoolSet MultiMongo::NewPoolSet() { return PoolSet(*this); }

void DumpMetric(utils::statistics::Writer& writer, const MultiMongo& multi_mongo) {
    const auto pool_map = multi_mongo.pool_map_.Read();
    for (const auto& [dbalias, pool] : *pool_map) {
        UASSERT(pool);
        writer.ValueWithLabels(*pool, {"mongo_database", dbalias});
    }
}

void MultiMongo::OnConfigUpdate(const dynamic_config::Snapshot& config) {
    const auto new_pool_settings = config[kPoolSettings].GetOptional(name_);
    if (new_pool_settings.has_value()) {
        new_pool_settings->Validate(name_);

        const auto pool_map = pool_map_.Read();
        for (const auto& [_, pool] : *pool_map) {
            UASSERT(pool);
            pool->SetPoolSettings(new_pool_settings.value());
        }
    }
}

storages::mongo::PoolPtr MultiMongo::FindPool(const std::string& dbalias) const {
    const auto pool_map = pool_map_.Read();
    const auto it = pool_map->find(dbalias);
    if (it == pool_map->end()) return {};
    return it->second;
}

void MultiMongo::OnSecdistUpdate(const storages::secdist::SecdistConfig& secdist) {
    const auto pool_map = pool_map_.Read();
    for (const auto& [dbalias, pool] : *pool_map) {
        auto conn_string = secdist::GetSecdistConnectionString(secdist, dbalias);
        pool->SetConnectionString(conn_string);
    }
}

}  // namespace storages::mongo

USERVER_NAMESPACE_END
