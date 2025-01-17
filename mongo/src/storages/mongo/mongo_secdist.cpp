#include "mongo_secdist.hpp"

#include <unordered_map>

#include <userver/formats/json/value.hpp>
#include <userver/storages/mongo/exception.hpp>
#include <userver/storages/secdist/exceptions.hpp>
#include <userver/storages/secdist/helpers.hpp>

#include <boost/range/adaptor/map.hpp>

#include <fmt/format.h>
#include <fmt/ranges.h>

USERVER_NAMESPACE_BEGIN

namespace storages::mongo::secdist {
namespace {

class MongoSettings {
public:
    explicit MongoSettings(const formats::json::Value& doc);

    const std::string& GetConnectionString(const std::string& dbalias) const;

private:
    std::unordered_map<std::string, std::string> settings_;
};

MongoSettings::MongoSettings(const formats::json::Value& doc) {
    const formats::json::Value& mongo_settings = doc["mongo_settings"];
    // Mongo library can be used just for BSON without mongo configuration
    if (mongo_settings.IsMissing()) return;

    storages::secdist::CheckIsObject(mongo_settings, "mongo_settings");

    for (auto it = mongo_settings.begin(); it != mongo_settings.end(); ++it) {
        const std::string& dbalias = it.GetName();
        const formats::json::Value& dbsettings = *it;
        storages::secdist::CheckIsObject(dbsettings, "dbsettings");
        settings_[dbalias] = storages::secdist::GetString(dbsettings, "uri");
    }
}

const std::string& MongoSettings::GetConnectionString(const std::string& dbalias) const {
    auto it = settings_.find(dbalias);

    if (it == settings_.end())
        throw storages::secdist::UnknownMongoDbAlias(fmt::format(
            "dbalias {} not found in secdist config. Available aliases: [{}]",
            dbalias,
            fmt::join(settings_ | boost::adaptors::map_keys, ", ")
        ));

    return it->second;
}

}  // namespace

std::string GetSecdistConnectionString(const storages::secdist::Secdist& secdist, const std::string& dbalias) {
    auto snapshot = secdist.GetSnapshot();
    return GetSecdistConnectionString(*snapshot, dbalias);
}

std::string GetSecdistConnectionString(const storages::secdist::SecdistConfig& secdist, const std::string& dbalias) {
    try {
        return secdist.Get<storages::mongo::secdist::MongoSettings>().GetConnectionString(dbalias);
    } catch (const storages::secdist::SecdistError& ex) {
        throw storages::mongo::InvalidConfigException("Failed to load mongo config for dbalias ")
            << dbalias << ": " << ex.what();
    }
}

}  // namespace storages::mongo::secdist

USERVER_NAMESPACE_END
