#pragma once

/// @file userver/os_signals/component.hpp
/// @brief @copybrief os_signals::ProcessorComponent

#include <csignal>

#include <userver/components/component_fwd.hpp>
#include <userver/components/raw_component_base.hpp>
#include <userver/os_signals/processor.hpp>

USERVER_NAMESPACE_BEGIN

/// @brief Components and functions to work with OS signals
namespace os_signals {

/// @ingroup userver_components
///
/// @brief A component for os_signals::Processor
///
/// Declaration in static config file may be skipped.
///
/// @see @ref scripts/docs/en/userver/os_signals.md
class ProcessorComponent final : public components::RawComponentBase {
public:
    /// @ingroup userver_component_names
    /// @brief The default name of os_signals::ProcessorComponent
    static constexpr std::string_view kName = "os-signal-processor";

    ProcessorComponent(const components::ComponentConfig& config, const components::ComponentContext& context);

    os_signals::Processor& Get();

private:
    os_signals::Processor manager_;
};

}  // namespace os_signals

template <>
inline constexpr auto components::kConfigFileMode<os_signals::ProcessorComponent> = ConfigFileMode::kNotRequired;

USERVER_NAMESPACE_END
