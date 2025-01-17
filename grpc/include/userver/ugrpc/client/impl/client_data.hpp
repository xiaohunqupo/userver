#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include <grpcpp/channel.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/security/credentials.h>

#include <userver/dynamic_config/source.hpp>
#include <userver/engine/task/task_processor_fwd.hpp>
#include <userver/testsuite/grpc_control.hpp>
#include <userver/utils/fixed_array.hpp>

#include <userver/ugrpc/client/client_factory_settings.hpp>
#include <userver/ugrpc/client/fwd.hpp>
#include <userver/ugrpc/client/middlewares/fwd.hpp>
#include <userver/ugrpc/impl/static_metadata.hpp>
#include <userver/ugrpc/impl/statistics.hpp>
#include <userver/ugrpc/impl/to_string.hpp>

USERVER_NAMESPACE_BEGIN

namespace ugrpc::impl {
class StatisticsStorage;
class CompletionQueuePoolBase;
}  // namespace ugrpc::impl

namespace ugrpc::client {
struct ClientFactorySettings;
}

namespace ugrpc::client::impl {

/// Contains all non-code-generated dependencies for creating a gRPC client
struct ClientDependencies final {
    std::string client_name;
    std::string endpoint;
    Middlewares mws;
    ugrpc::impl::CompletionQueuePoolBase& completion_queues;
    ugrpc::impl::StatisticsStorage& statistics_storage;
    const dynamic_config::Source config_source;
    testsuite::GrpcControl& testsuite_grpc;
    const dynamic_config::Key<ClientQos>* qos{nullptr};
    const ClientFactorySettings& settings;
    engine::TaskProcessor& channel_task_processor;
    DedicatedMethodsConfig dedicated_methods_config;
};

struct GenericClientTag final {
    explicit GenericClientTag() = default;
};

/// The internal state of generated gRPC clients
class ClientData final {
public:
    template <typename Service>
    using Stub = typename Service::Stub;

    ClientData() = delete;

    template <typename Service>
    ClientData(ClientDependencies&& dependencies, ugrpc::impl::StaticServiceMetadata metadata, std::in_place_type_t<Service>)
        : dependencies_(std::move(dependencies)),
          metadata_(metadata),
          service_statistics_(&GetServiceStatistics()),
          channels_(CreateChannels(dependencies_)),
          stubs_(MakeStubs<Service>(channels_)),
          dedicated_stubs_(MakeDedicatedStubs<Service>(dependencies_, metadata)) {}

    template <typename Service>
    ClientData(ClientDependencies&& dependencies, GenericClientTag, std::in_place_type_t<Service>)
        : dependencies_(std::move(dependencies)),
          channels_(CreateChannels(dependencies_)),
          stubs_(MakeStubs<Service>(channels_)) {}

    ClientData(ClientData&&) noexcept = default;
    ClientData& operator=(ClientData&&) = delete;

    ClientData(const ClientData&) = delete;
    ClientData& operator=(const ClientData&) = delete;

    template <typename Service>
    Stub<Service>& NextStubFromMethodId(std::size_t method_id) const {
        if (!dedicated_stubs_[method_id].empty()) {
            return *static_cast<Stub<Service>*>(NextStubPtr(dedicated_stubs_[method_id]).get());
        }
        return NextGenericStub<Service>();
    }

    template <typename Service>
    Stub<Service>& NextGenericStub() const {
        return *static_cast<Stub<Service>*>(NextStubPtr(stubs_).get());
    }

    grpc::CompletionQueue& NextQueue() const;

    dynamic_config::Snapshot GetConfigSnapshot() const { return dependencies_.config_source.GetSnapshot(); }

    ugrpc::impl::MethodStatistics& GetStatistics(std::size_t method_id) const;

    ugrpc::impl::MethodStatistics& GetGenericStatistics(std::string_view call_name) const;

    const utils::FixedArray<std::shared_ptr<grpc::Channel>>& GetChannels() { return channels_; }

    std::string_view GetClientName() const { return dependencies_.client_name; }

    const Middlewares& GetMiddlewares() const { return dependencies_.mws; }

    const ugrpc::impl::StaticServiceMetadata& GetMetadata() const;

    const testsuite::GrpcControl& GetTestsuiteControl() const { return dependencies_.testsuite_grpc; }

    const dynamic_config::Key<ClientQos>* GetClientQos() const;

    std::size_t GetDedicatedChannelCount(std::size_t method_id) const;

private:
    static std::shared_ptr<grpc::Channel>
    CreateChannelImpl(const ClientDependencies& dependencies, const grpc::string& endpoint);

    static utils::FixedArray<std::shared_ptr<grpc::Channel>> CreateChannels(const ClientDependencies& dependencies);

    static std::size_t GetDedicatedChannelCountImpl(
        const ClientDependencies& dependencies,
        std::size_t method_id,
        const ugrpc::impl::StaticServiceMetadata& meta
    );

    using StubDeleterType = void (*)(void*);
    using StubPtr = std::unique_ptr<void, StubDeleterType>;

    using StubPool = utils::FixedArray<StubPtr>;

    template <typename Service>
    static void StubDeleter(void* ptr) noexcept {
        delete static_cast<Stub<Service>*>(ptr);
    }

    template <typename Service>
    static utils::FixedArray<StubPtr> MakeStubs(const utils::FixedArray<std::shared_ptr<grpc::Channel>>& channels) {
        return utils::GenerateFixedArray(channels.size(), [&](std::size_t index) {
            return StubPtr(Service::NewStub(channels[index]).release(), &StubDeleter<Service>);
        });
    }

    template <typename Service>
    static utils::FixedArray<StubPool>
    MakeDedicatedStubs(const ClientDependencies& dependencies, const ugrpc::impl::StaticServiceMetadata& meta) {
        const auto& method_full_names = meta.method_full_names;
        const auto endpoint_string = ugrpc::impl::ToGrpcString(dependencies.endpoint);
        return utils::GenerateFixedArray(method_full_names.size(), [&](std::size_t method_id) {
            const auto dedicated_channel_count = GetDedicatedChannelCountImpl(dependencies, method_id, meta);
            return utils::GenerateFixedArray(dedicated_channel_count, [&](std::size_t) {
                return StubPtr(Service::NewStub(CreateChannelImpl(dependencies, endpoint_string)).release(), &StubDeleter<Service>);
            });
        });
    }

    const StubPtr& NextStubPtr(const utils::FixedArray<StubPtr>& stubs) const;

    ugrpc::impl::ServiceStatistics& GetServiceStatistics();

    ClientDependencies dependencies_;
    std::optional<ugrpc::impl::StaticServiceMetadata> metadata_{std::nullopt};
    ugrpc::impl::ServiceStatistics* service_statistics_{nullptr};

    utils::FixedArray<std::shared_ptr<grpc::Channel>> channels_;

    utils::FixedArray<StubPtr> stubs_;
    // method_id -> stub_pool
    utils::FixedArray<StubPool> dedicated_stubs_;
};

template <typename Client>
ClientData& GetClientData(Client& client) {
    return client.impl_;
}

}  // namespace ugrpc::client::impl

USERVER_NAMESPACE_END
