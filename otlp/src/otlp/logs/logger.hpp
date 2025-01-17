#pragma once

#include <cstdint>
#include <memory>
#include <variant>

#include <opentelemetry/proto/collector/logs/v1/logs_service_client.usrv.pb.hpp>
#include <opentelemetry/proto/collector/trace/v1/trace_service_client.usrv.pb.hpp>

#include <userver/concurrent/queue.hpp>
#include <userver/engine/task/task.hpp>
#include <userver/formats/yaml.hpp>
#include <userver/logging/impl/log_stats.hpp>
#include <userver/logging/impl/logger_base.hpp>
#include <userver/yaml_config/yaml_config.hpp>

USERVER_NAMESPACE_BEGIN

namespace otlp {

enum class SinkType : std::uint8_t {
    kDefault = 0x1,
    kOtlp = 0x2,
    kBoth = kDefault | kOtlp,
};

SinkType Parse(const yaml_config::YamlConfig& value, formats::parse::To<SinkType>);

struct LoggerConfig {
    size_t max_queue_size{10000};
    std::chrono::milliseconds max_batch_delay{};
    SinkType logs_sink{SinkType::kOtlp};
    SinkType tracing_sink{SinkType::kOtlp};
    std::string service_name;
    std::unordered_map<std::string, std::string> extra_attributes;
    std::unordered_map<std::string, std::string> attributes_mapping;
    logging::Level log_level{logging::Level::kInfo};
};

class Logger final : public logging::impl::LoggerBase {
public:
    using LogClient = opentelemetry::proto::collector::logs::v1::LogsServiceClient;
    using TraceClient = opentelemetry::proto::collector::trace::v1::TraceServiceClient;

    explicit Logger(LogClient client, TraceClient trace_client, LoggerConfig&& config);

    ~Logger() override;

    void Log(logging::Level level, std::string_view msg) override;

    void Trace(logging::Level level, std::string_view msg) override;

    void PrependCommonTags(logging::impl::TagWriter writer) const override;

    void Stop() noexcept;

    const logging::impl::LogStatistics& GetStatistics() const;

    void SetDefaultLogger(logging::LoggerPtr default_logger) { default_logger_ = default_logger; }

protected:
    bool DoShouldLog(logging::Level level) const noexcept override;

private:
    using Action = std::variant<::opentelemetry::proto::logs::v1::LogRecord, ::opentelemetry::proto::trace::v1::Span>;
    using Queue = concurrent::NonFifoMpscQueue<Action>;

    void SendingLoop(Queue::Consumer& consumer, LogClient& log_client, TraceClient& trace_client);

    void FillAttributes(::opentelemetry::proto::resource::v1::Resource& resource);

    void DoLog(const opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest& request, LogClient& client);

    void DoTrace(
        const opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest& request,
        TraceClient& trace_client
    );

    std::string_view MapAttribute(std::string_view attr) const;

    logging::impl::LogStatistics stats_;
    const LoggerConfig config_;
    std::shared_ptr<Queue> queue_;
    Queue::MultiProducer queue_producer_;
    logging::LoggerPtr default_logger_{};
    engine::Task sender_task_;  // Must be the last member
};

}  // namespace otlp

USERVER_NAMESPACE_END
