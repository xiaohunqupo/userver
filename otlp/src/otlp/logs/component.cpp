#include <userver/otlp/logs/component.hpp>

#include <string>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/engine/sleep.hpp>
#include <userver/logging/component.hpp>
#include <userver/logging/impl/mem_logger.hpp>
#include <userver/logging/level_serialization.hpp>
#include <userver/logging/log.hpp>
#include <userver/logging/null_logger.hpp>

#include <userver/ugrpc/client/client_factory_component.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include "logger.hpp"

#include <opentelemetry/proto/collector/logs/v1/logs_service_client.usrv.pb.hpp>

USERVER_NAMESPACE_BEGIN

namespace otlp {

LoggerComponent::LoggerComponent(const components::ComponentConfig& config, const components::ComponentContext& context)
    : old_logger_(logging::GetDefaultLogger()) {
    auto& client_factory = context.FindComponent<ugrpc::client::ClientFactoryComponent>().GetFactory();

    auto endpoint = config["endpoint"].As<std::string>();
    auto client = client_factory.MakeClient<opentelemetry::proto::collector::logs::v1::LogsServiceClient>(
        "otlp-logger", endpoint
    );

    auto trace_client = client_factory.MakeClient<opentelemetry::proto::collector::trace::v1::TraceServiceClient>(
        "otlp-tracer", endpoint
    );

    LoggerConfig logger_config;
    logger_config.max_queue_size = config["max-queue-size"].As<size_t>(65535);
    logger_config.max_batch_delay = config["max-batch-delay"].As<std::chrono::milliseconds>(100);
    logger_config.service_name = config["service-name"].As<std::string>("unknown_service");
    logger_config.log_level = config["log-level"].As<USERVER_NAMESPACE::logging::Level>();
    logger_config.extra_attributes = config["extra-attributes"].As<std::unordered_map<std::string, std::string>>({});
    logger_config.attributes_mapping =
        config["attributes-mapping"].As<std::unordered_map<std::string, std::string>>({});
    logger_config.logs_sink = config["sinks"]["logs"].As<SinkType>(SinkType::kOtlp);
    logger_config.tracing_sink = config["sinks"]["tracing"].As<SinkType>(SinkType::kOtlp);

    logger_ = std::make_shared<Logger>(std::move(client), std::move(trace_client), std::move(logger_config));
    // We must init after the default logger is initialized
    auto& logging_component = context.FindComponent<components::Logging>();
    logging::LoggerPtr default_logger{};
    if (logger_config.logs_sink == SinkType::kOtlp && logger_config.tracing_sink == SinkType::kOtlp) {
        if (logging_component.GetLoggerOptional("default")) {
            throw std::runtime_error(
                "You've registered both the 'otlp-logger' component and the "
                "'default' logger in 'logging' component, but have opted to "
                "send both logs and traces using oltp. Either disable default "
                "logger or otlp logger."
            );
        }
    } else {
        try {
            default_logger = logging_component.GetLogger("default");
        } catch (const std::runtime_error&) {
            default_logger = nullptr;
        }
        if (!default_logger) {
            throw std::runtime_error(
                "You've opted to use the 'default' logger, but it's not registered "
                "in 'loggers' of the 'logging' component. Please register it, or "
                "your logs and/or traces won't be saved."
            );
        }
    }

    logger_->SetDefaultLogger(default_logger);

    logging::impl::SetDefaultLoggerRef(*logger_);
    old_logger_.ForwardTo(&*logger_);

    auto* const statistics_storage = context.FindComponentOptional<components::StatisticsStorage>();
    if (statistics_storage) {
        statistics_holder_ =
            statistics_storage->GetStorage().RegisterWriter("logger", [this](utils::statistics::Writer& writer) {
                writer.ValueWithLabels(logger_->GetStatistics(), {"logger", "default"});
            });
    }
}

LoggerComponent::~LoggerComponent() {
    old_logger_.ForwardTo(nullptr);
    logging::impl::SetDefaultLoggerRef(old_logger_);

    logger_->Stop();

    static std::shared_ptr<Logger> logger_pin;
    logger_pin = logger_;
}

yaml_config::Schema LoggerComponent::GetStaticConfigSchema() {
    return yaml_config::MergeSchemas<components::RawComponentBase>(R"(
type: object
description: >
    OpenTelemetry logger component
additionalProperties: false
properties:
    endpoint:
        type: string
        description: >
            Hostname:port of otel collector (gRPC).
    log-level:
        type: string
        description: log level
    max-queue-size:
        type: integer
        description: max async queue size
    max-batch-delay:
        type: string
        description: max delay between send batches (e.g. 100ms or 1s)
    service-name:
        type: string
        description: service name
    sinks:
        type: object
        description: sinks to send logs/traces to
        additionalProperties: false
        properties:
            logs:
                type: string
                enum: [otlp, default, both]
                description: logs sink
                defaultDescription: otlp
            tracing:
                type: string
                enum: [otlp, default, both]
                description: tracing sink
                defaultDescription: otlp
    attributes-mapping:
        type: object
        description: rename rules for OTLP attributes
        properties: {}
        additionalProperties:
            type: string
            description: new attribute name
    extra-attributes:
        type: object
        description: extra OTLP attributes
        properties: {}
        additionalProperties:
            type: string
            description: attribute value

)");
}

}  // namespace otlp

USERVER_NAMESPACE_END
