#include <userver/utest/utest.hpp>

#include <components/component_list_test.hpp>
#include <userver/alerts/component.hpp>
#include <userver/components/component_base.hpp>
#include <userver/components/run.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/logging/component.hpp>
#include <userver/os_signals/component.hpp>
#include <userver/tracing/component.hpp>
#include <userver/utils/algo.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

USERVER_NAMESPACE_BEGIN

namespace {

std::string expected_greeting;

class ConfigNotRequiredComponent final : public components::ComponentBase {
public:
    static constexpr std::string_view kName = "config-not-required";

    ConfigNotRequiredComponent(const components::ComponentConfig& config, const components::ComponentContext& context)
        : components::ComponentBase(config, context) {
        EXPECT_EQ(config["greeting"].As<std::string>("default"), expected_greeting);
    }

    static yaml_config::Schema GetStaticConfigSchema() {
        return yaml_config::MergeSchemas<components::ComponentBase>(R"(
type: object
description: Component with a non-required static config
additionalProperties: false
properties:
    greeting:
        type: string
        description: greeting
        defaultDescription: default
)");
    }
};

}  // namespace

template <>
inline constexpr bool components::kHasValidate<ConfigNotRequiredComponent> = true;

template <>
inline constexpr auto components::kConfigFileMode<ConfigNotRequiredComponent> = ConfigFileMode::kNotRequired;

namespace {

constexpr std::string_view kStaticConfigBase = R"(
components_manager:
  event_thread_pool:
    threads: 1
  default_task_processor: main-task-processor
  task_processors:
    main-task-processor:
      worker_threads: 1
  components:
    logging:
      fs-task-processor: main-task-processor
      loggers:
        default:
          file_path: '@null'
)";

constexpr std::string_view kCustomGreetingConfig = R"(
    config-not-required:
      greeting: custom
)";

components::ComponentList MakeComponentList() {
    return components::ComponentList()
        .Append<os_signals::ProcessorComponent>()
        .Append<components::StatisticsStorage>()
        .Append<components::Logging>()
        .Append<components::Tracer>()
        .Append<ConfigNotRequiredComponent>()
        .Append<alerts::StorageComponent>();
}

}  // namespace

TEST_F(ComponentList, ConfigNotRequiredDefault) {
    expected_greeting = "default";
    components::RunOnce(components::InMemoryConfig{std::string{kStaticConfigBase}}, MakeComponentList());
}

TEST_F(ComponentList, ConfigNotRequiredCustom) {
    expected_greeting = "custom";
    components::RunOnce(
        components::InMemoryConfig{utils::StrCat(kStaticConfigBase, kCustomGreetingConfig)}, MakeComponentList()
    );
}

USERVER_NAMESPACE_END
