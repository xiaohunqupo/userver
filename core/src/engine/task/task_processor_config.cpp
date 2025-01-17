#include <engine/task/task_processor_config.hpp>

#include <cstdint>

#include <userver/formats/json/value.hpp>
#include <userver/logging/log.hpp>
#include <userver/utils/assert.hpp>
#include <userver/utils/text_light.hpp>
#include <userver/utils/trivial_map.hpp>
#include <userver/yaml_config/yaml_config.hpp>

USERVER_NAMESPACE_BEGIN

namespace engine {

namespace {

std::string GenerateWorkerThreadName(std::string_view tp_name) {
    static constexpr std::string_view kExpectedSuffix = "-task-processor";
    if (utils::text::EndsWith(tp_name, kExpectedSuffix)) {
        std::string_view short_name{tp_name};
        short_name.remove_suffix(kExpectedSuffix.size());
        if (!short_name.empty() && short_name.size() <= 5) {
            return fmt::format("{}-worker", short_name);
        }
    }
    throw std::runtime_error(fmt::format(
        "thread_name is only automatically derived for task processors with "
        "names in the form: \"\\w{{1,5}}-task-processor\". Please specify "
        "thread_name in the config explicitly for '{}' task processor",
        tp_name
    ));
}

}  // namespace

OsScheduling Parse(const yaml_config::YamlConfig& value, formats::parse::To<OsScheduling>) {
    static constexpr utils::TrivialBiMap kMap([](auto selector) {
        return selector()
            .Case(OsScheduling::kNormal, "normal")
            .Case(OsScheduling::kLowPriority, "low-priority")
            .Case(OsScheduling::kIdle, "idle");
    });

    return utils::ParseFromValueString(value, kMap);
}

TaskQueueType Parse(const yaml_config::YamlConfig& value, formats::parse::To<TaskQueueType>) {
    static constexpr utils::TrivialBiMap kMap([](auto selector) {
        return selector()
            .Case(TaskQueueType::kGlobalTaskQueue, "global-task-queue")
            .Case(TaskQueueType::kWorkStealingTaskQueue, "work-stealing-task-queue");
    });

    return utils::ParseFromValueString(value, kMap);
}

TaskProcessorConfig Parse(const yaml_config::YamlConfig& value, formats::parse::To<TaskProcessorConfig>) {
    TaskProcessorConfig config;
    config.should_guess_cpu_limit = value["guess-cpu-limit"].As<bool>(config.should_guess_cpu_limit);
    config.worker_threads = value["worker_threads"].As<std::size_t>();
    config.thread_name = value["thread_name"].As<std::string>({});
    config.os_scheduling = value["os-scheduling"].As<OsScheduling>(config.os_scheduling);
    config.spinning_iterations = value["spinning-iterations"].As<int>(config.spinning_iterations);
    config.task_processor_queue = value["task-processor-queue"].As<TaskQueueType>(config.task_processor_queue);

    const auto task_trace = value["task-trace"];
    if (!task_trace.IsMissing()) {
        config.task_trace_every = task_trace["every"].As<std::size_t>(config.task_trace_every);
        config.task_trace_max_csw = task_trace["max-context-switch-count"].As<std::size_t>(1000);
        config.task_trace_logger_name = task_trace["logger"].As<std::string>();
    }

    return config;
}

void TaskProcessorConfig::SetName(const std::string& new_name) {
    name = new_name;
    if (thread_name.empty()) {
        thread_name = GenerateWorkerThreadName(name);
    }
}

using OverloadAction = TaskProcessorSettings::OverloadAction;

/// [sample enum parser]
OverloadAction Parse(const formats::json::Value& value, formats::parse::To<OverloadAction>) {
    static constexpr utils::TrivialBiMap kMap([](auto selector) {
        return selector().Case(OverloadAction::kCancel, "cancel").Case(OverloadAction::kIgnore, "ignore");
    });

    return utils::ParseFromValueString(value, kMap);
}
/// [sample enum parser]

TaskProcessorSettings Parse(const formats::json::Value& value, formats::parse::To<TaskProcessorSettings>) {
    engine::TaskProcessorSettings settings;

    const auto overload_doc = value["wait_queue_overload"];

    settings.wait_queue_time_limit = std::chrono::microseconds(overload_doc["time_limit_us"].As<std::int64_t>());
    settings.wait_queue_length_limit = overload_doc["length_limit"].As<std::int64_t>();
    settings.sensor_wait_queue_time_limit =
        std::chrono::microseconds(overload_doc["sensor_time_limit_us"].As<std::int64_t>(3000));
    settings.overload_action = overload_doc["action"].As<OverloadAction>(OverloadAction::kIgnore);

    return settings;
}

}  // namespace engine

USERVER_NAMESPACE_END
