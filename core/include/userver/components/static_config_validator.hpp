#pragma once

#include <userver/components/component_config.hpp>
#include <userver/components/raw_component_base.hpp>
#include <userver/yaml_config/impl/validate_static_config.hpp>
#include <userver/yaml_config/schema.hpp>

USERVER_NAMESPACE_BEGIN

namespace components {

enum class ValidationMode {
    kOnlyTurnedOn,
    kAll,
};

ValidationMode Parse(const yaml_config::YamlConfig& value, formats::parse::To<ValidationMode>);

namespace impl {
template <typename Component>
void TryValidateStaticConfig(
    std::string_view component_name,
    const components::ComponentConfig& static_config,
    ValidationMode validation_condition
) {
    if (components::kHasValidate<Component> || validation_condition == ValidationMode::kAll) {
        yaml_config::Schema schema = Component::GetStaticConfigSchema();
        schema.path = component_name;

        yaml_config::impl::Validate(static_config, schema);
    }
}

template <typename Component>
yaml_config::Schema GetStaticConfigSchema() {
    // TODO: implement for kOnlyTurnedOn
    return Component::GetStaticConfigSchema();
}

}  // namespace impl
}  // namespace components

USERVER_NAMESPACE_END
