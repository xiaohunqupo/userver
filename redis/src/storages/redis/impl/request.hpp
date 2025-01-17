#pragma once

#include <chrono>

#include <userver/engine/deadline.hpp>
#include <userver/engine/future.hpp>

#include <userver/storages/redis/base.hpp>
#include <userver/storages/redis/fwd.hpp>

USERVER_NAMESPACE_BEGIN

namespace tracing {
class Span;
}

namespace storages::redis::impl {

class CmdArgs;
struct Command;
using CommandPtr = std::shared_ptr<Command>;
class Sentinel;

class Request {
public:
    Request(const Request&) = delete;
    Request(Request&& r) noexcept = default;
    Request& operator=(const Request&) = delete;
    Request& operator=(Request&& r) noexcept = default;

    ReplyPtr Get();

    engine::impl::ContextAccessor* TryGetContextAccessor() noexcept;

private:
    friend class Sentinel;

    Request(
        Sentinel& sentinel,
        CmdArgs&& args,
        const std::string& key,
        bool master,
        const CommandControl& command_control,
        size_t replies_to_skip
    );

    Request(
        Sentinel& sentinel,
        CmdArgs&& args,
        size_t shard,
        bool master,
        const CommandControl& command_control,
        size_t replies_to_skip
    );

    CommandPtr PrepareRequest(CmdArgs&& args, const CommandControl& command_control, size_t replies_to_skip);

    engine::Future<ReplyPtr> future_;
    engine::Deadline deadline_;
};

}  // namespace storages::redis::impl

USERVER_NAMESPACE_END
