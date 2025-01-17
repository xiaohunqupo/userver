#pragma once

#include <string>

#include <userver/formats/yaml.hpp>
#include <userver/yaml_config/yaml_config.hpp>

USERVER_NAMESPACE_BEGIN

namespace engine::ev {

struct ThreadPoolConfig {
    std::size_t threads = 2;
    std::string thread_name = "event-worker";
    bool ev_default_loop_disabled = false;
};

ThreadPoolConfig Parse(const yaml_config::YamlConfig& value, formats::parse::To<ThreadPoolConfig>);

}  // namespace engine::ev

USERVER_NAMESPACE_END
