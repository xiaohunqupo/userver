#pragma once

/// @file userver/storages/mongo/pool.hpp
/// @brief @copybrief storages::mongo::Pool

#include <memory>
#include <string>
#include <vector>

#include <userver/clients/dns/resolver_fwd.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/storages/mongo/collection.hpp>
#include <userver/storages/mongo/pool_config.hpp>
#include <userver/utils/statistics/fwd.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::mongo {

namespace impl {
class PoolImpl;
}  // namespace impl

/// @ingroup userver_clients
///
/// @brief MongoDB client pool.
///
/// Use constructor only for tests, in production the pool should be retrieved
/// from @ref userver_components "the components" via
/// components::Mongo::GetPool() or components::MultiMongo::GetPool().
///
/// ## Example usage:
///
/// @snippet storages/mongo/collection_mongotest.hpp  Sample Mongo usage
class Pool {
public:
    Pool(Pool&&) noexcept;
    Pool& operator=(Pool&&) noexcept;
    ~Pool();

    /// Checks whether a collection exists
    bool HasCollection(const std::string& name) const;

    /// Returns a handle for the specified collection
    Collection GetCollection(std::string name) const;

    /// Drops the associated database if it exists. New modifications of
    /// collections will attempt to re-create the database automatically.
    void DropDatabase();

    /// Get a list of all the collection names in the associated database
    std::vector<std::string> ListCollectionNames() const;

    /// @throws storages::mongo::MongoException if failed to connect to the mongo server.
    void Ping();

    /// @cond
    // For internal use only
    Pool(
        std::string id,
        const std::string& uri,
        const PoolConfig& pool_config,
        clients::dns::Resolver* dns_resolver,
        dynamic_config::Source config_source
    );

    // Writes pool statistics
    friend void DumpMetric(utils::statistics::Writer& writer, const Pool& pool);

    // Sets new dynamic pool settings
    void SetPoolSettings(const PoolSettings& pool_settings);

    void SetConnectionString(const std::string& connection_string);
    /// @endcond

private:
    std::shared_ptr<impl::PoolImpl> impl_;
};

using PoolPtr = std::shared_ptr<Pool>;

}  // namespace storages::mongo

USERVER_NAMESPACE_END
