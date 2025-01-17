#include <userver/dynamic_config/updater/component.hpp>

#include <fmt/format.h>

#include <userver/cache/update_type.hpp>
#include <userver/clients/http/component.hpp>
#include <userver/components/component.hpp>
#include <userver/dynamic_config/client/component.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/dynamic_config/updates_sink/find.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/fs/read.hpp>
#include <userver/utils/algo.hpp>
#include <userver/utils/fast_scope_guard.hpp>
#include <userver/utils/string_to_duration.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

USERVER_NAMESPACE_BEGIN

namespace components {

namespace {

std::optional<cache::AllowedUpdateTypes> ParseDeduplicateUpdateTypes(const yaml_config::YamlConfig& value) {
    const auto str = value.As<std::optional<std::string>>();
    if (!str) return cache::AllowedUpdateTypes::kFullAndIncremental;
    if (str == "none") return std::nullopt;
    return value.As<cache::AllowedUpdateTypes>();
}

bool ShouldDeduplicate(
    const std::optional<cache::AllowedUpdateTypes>& update_types,
    cache::UpdateType current_update_type
) {
    switch (current_update_type) {
        case cache::UpdateType::kFull:
            return update_types == cache::AllowedUpdateTypes::kOnlyFull ||
                   update_types == cache::AllowedUpdateTypes::kFullAndIncremental;
        case cache::UpdateType::kIncremental:
            return update_types == cache::AllowedUpdateTypes::kOnlyIncremental ||
                   update_types == cache::AllowedUpdateTypes::kFullAndIncremental;
    }
    UINVARIANT(false, "Invalid cache::UpdateType");
}

const ComponentConfig& EnsureCacheConfigIsValid(const ComponentConfig& component_config) {
    UINVARIANT(
        !component_config["config-settings"].As<bool>(true),
        "dynamic-config-client-updater.config-settings must be false, "
        "otherwise it will create a cyclic dependency of components"
    );
    UINVARIANT(
        component_config["first-update-fail-ok"].As<bool>(false),
        "dynamic-config-client-updater.first-update-fail-ok must be true, "
        "otherwise dynamic-config.fs-cache-path will not work properly"
    );
    return component_config;
}

}  // namespace

DynamicConfigClientUpdater::DynamicConfigClientUpdater(
    const ComponentConfig& component_config,
    const ComponentContext& component_context
)
    : CachingComponentBase(EnsureCacheConfigIsValid(component_config), component_context),
      updates_sink_(dynamic_config::FindUpdatesSink(component_config, component_context)),
      load_only_my_values_(component_config["load-only-my-values"].As<bool>(true)),
      store_enabled_(component_config["store-enabled"].As<bool>(true)),
      deduplicate_update_types_(ParseDeduplicateUpdateTypes(component_config["deduplicate-update-types"])),
      config_client_(component_context.FindComponent<components::DynamicConfigClient>().GetClient()),
      docs_map_defaults_(component_context.FindComponent<components::DynamicConfig>().GetDefaultDocsMap()),
      docs_map_keys_(utils::AsContainer<DocsMapKeys>(docs_map_defaults_.GetNames())) {
    StartPeriodicUpdates();
}

DynamicConfigClientUpdater::~DynamicConfigClientUpdater() { StopPeriodicUpdates(); }

void DynamicConfigClientUpdater::SetDisabledKillSwitchesToDefault(
    dynamic_config::DocsMap& docs_map,
    const std::vector<std::string>& kill_switches_disabled
) {
    for (const auto& kill_switch : kill_switches_disabled) {
        if (!docs_map_defaults_.Has(kill_switch)) {
            throw std::runtime_error(
                fmt::format("Default value is not found for disabled kill-switch '{}'", kill_switch)
            );
        }
        docs_map.Set(kill_switch, docs_map_defaults_.Get(kill_switch));
    }
}

dynamic_config::DocsMap DynamicConfigClientUpdater::MergeDocsMap(
    const dynamic_config::DocsMap& current,
    dynamic_config::DocsMap&& update,
    const std::vector<std::string>& removed
) {
    dynamic_config::DocsMap combined(std::move(update));
    combined.MergeMissing(current);
    combined.SetConfigsExpectedToBeUsed(docs_map_keys_, utils::impl::InternalTag{});
    for (const auto& key : removed) {
        combined.Remove(key);
    }
    return combined;
}

void DynamicConfigClientUpdater::StoreIfEnabled(const dynamic_config::DocsMap& value) {
    if (store_enabled_) {
        updates_sink_.SetConfig(kName, value);
    }
}

std::vector<std::string> DynamicConfigClientUpdater::GetDocsMapKeysToFetch(
    AdditionalDocsMapKeys& additional_docs_map_keys
) {
    std::vector<std::string> result;

    if (!load_only_my_values_) {
        return result;
    }

    auto docs_map_keys = docs_map_keys_;

    for (auto it = additional_docs_map_keys.begin(); it != additional_docs_map_keys.end();) {
        // Use reference counting to make sure that all consumers of config keys
        // may .Get() their keys. We may not guarantee that by any synchronization
        // on Update()/SetAdditionalKeys() as consumers may outlive these calls
        // and die well after Update()/SetAdditionalKeys() return.
        if (it->use_count() > 1) {
            UASSERT(*it);
            for (const auto& additional_key : **it) {
                docs_map_keys.insert(additional_key);
            }
            ++it;
        } else {
            it = additional_docs_map_keys.erase(it);
        }
    }

    result.reserve(docs_map_keys.size());
    while (!docs_map_keys.empty()) {
        auto node = docs_map_keys.extract(docs_map_keys.begin());
        result.push_back(std::move(node.value()));
    }
    return result;
}

dynamic_config::AdditionalKeysToken DynamicConfigClientUpdater::SetAdditionalKeys(std::vector<std::string> keys) {
    if (!load_only_my_values_ || keys.empty()) return dynamic_config::AdditionalKeysToken{nullptr};

    auto keys_ptr = std::make_shared<std::vector<std::string>>(std::move(keys));
    {
        auto additional_docs_map_keys = additional_docs_map_keys_.Lock();
        additional_docs_map_keys->insert(keys_ptr);
    }
    UpdateAdditionalKeys(*keys_ptr);
    return dynamic_config::AdditionalKeysToken{std::move(keys_ptr)};
}

void DynamicConfigClientUpdater::Update(
    cache::UpdateType update_type,
    const std::chrono::system_clock::time_point& /*last_update*/,
    const std::chrono::system_clock::time_point& /*now*/,
    cache::UpdateStatisticsScope& stats
) {
    try {
        auto additional_docs_map_keys = additional_docs_map_keys_.Lock();
        const auto docs_map_keys = GetDocsMapKeysToFetch(*additional_docs_map_keys);

        if (update_type == cache::UpdateType::kFull) {
            UpdateFull(docs_map_keys, stats);
        } else {
            UpdateIncremental(docs_map_keys, stats);
        }
    } catch (const std::exception& ex) {
        updates_sink_.NotifyLoadingFailed(kName, ex.what());
        throw;
    }
}

void DynamicConfigClientUpdater::UpdateFull(
    const std::vector<std::string>& docs_map_keys,
    cache::UpdateStatisticsScope& stats
) {
    auto reply = config_client_.FetchDocsMap(std::nullopt, docs_map_keys);
    auto& docs_map = reply.docs_map;
    SetDisabledKillSwitchesToDefault(docs_map, reply.kill_switches_disabled);

    stats.IncreaseDocumentsReadCount(docs_map.Size());

    // Don't check for timestamp, accept any timestamp.
    // Otherwise, we might end up with constantly failing to make full update
    // as every full update we get a bit outdated reply.

    const auto size = docs_map.Size();

    {
        const std::lock_guard lock(update_config_mutex_);
        if (IsDuplicate(cache::UpdateType::kFull, docs_map)) {
            stats.FinishNoChanges();
            server_timestamp_ = reply.timestamp;
            return;
        }

        StoreIfEnabled(docs_map);
        Set(std::move(docs_map));
    }

    stats.Finish(size);
    server_timestamp_ = reply.timestamp;
}

void DynamicConfigClientUpdater::UpdateIncremental(
    const std::vector<std::string>& docs_map_keys,
    cache::UpdateStatisticsScope& stats
) {
    auto reply = config_client_.FetchDocsMap(server_timestamp_, docs_map_keys);
    auto& docs_map = reply.docs_map;
    SetDisabledKillSwitchesToDefault(docs_map, reply.kill_switches_disabled);

    /* Timestamp can be compared lexicographically */
    if (reply.timestamp < server_timestamp_) {
        stats.FinishNoChanges();
        return;
    }

    if (reply.IsEmpty()) {
        stats.FinishNoChanges();
        server_timestamp_ = reply.timestamp;
        return;
    }

    stats.IncreaseDocumentsReadCount(docs_map.Size());

    {
        const std::lock_guard lock(update_config_mutex_);
        auto ptr = Get();
        auto combined = MergeDocsMap(*ptr, std::move(docs_map), reply.removed);

        if (IsDuplicate(cache::UpdateType::kIncremental, combined)) {
            stats.FinishNoChanges();
            server_timestamp_ = reply.timestamp;
            return;
        }

        // May throw a config parsing error, in which case the cache update will be
        // marked as unsuccessful.
        StoreIfEnabled(combined);

        auto size = combined.Size();
        Set(std::move(combined));
        stats.Finish(size);
    }
    server_timestamp_ = reply.timestamp;
}

void DynamicConfigClientUpdater::UpdateAdditionalKeys(const std::vector<std::string>& keys) {
    auto reply = config_client_.FetchDocsMap(std::nullopt, keys);
    auto& additional_configs = reply.docs_map;

    {
        const std::lock_guard lock(update_config_mutex_);
        auto ptr = Get();
        dynamic_config::DocsMap docs_map = *ptr;
        auto combined = MergeDocsMap(additional_configs, std::move(docs_map), reply.removed);

        StoreIfEnabled(combined);
        Emplace(std::move(combined));
    }
}

bool DynamicConfigClientUpdater::IsDuplicate(cache::UpdateType update_type, const dynamic_config::DocsMap& new_value)
    const {
    if (ShouldDeduplicate(deduplicate_update_types_, update_type)) {
        if (const auto old_config = GetUnsafe(); old_config && old_config->AreContentsEqual(new_value)) {
            return true;
        }
    }
    return false;
}

yaml_config::Schema DynamicConfigClientUpdater::GetStaticConfigSchema() {
    return yaml_config::MergeSchemas<CachingComponentBase>(R"(
type: object
description: Component that does a periodic update of runtime configs.
additionalProperties: false
properties:
    updates-sink:
        type: string
        description: components::DynamicConfigUpdatesSinkBase descendant to be used for storing received updates
        defaultDescription: dynamic-config
    store-enabled:
        type: boolean
        description: store the retrieved values into the updates sink component
        defaultDescription: true
    load-only-my-values:
        type: boolean
        description: request from the client only the values used by this service
        defaultDescription: true
    deduplicate-update-types:
        type: string
        description: config update types for best-effort deduplication
        defaultDescription: full-and-incremental
        enum:
          - none
          - only-full
          - only-incremental
          - full-and-incremental
)");
}

}  // namespace components

USERVER_NAMESPACE_END
