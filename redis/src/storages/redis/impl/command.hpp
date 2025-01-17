#pragma once

#include <userver/logging/log_extra.hpp>
#include <userver/storages/redis/fwd.hpp>

#include <storages/redis/impl/cmd_args.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::redis::impl {

struct Command;
using CommandPtr = std::shared_ptr<Command>;
using ReplyCallback = std::function<void(const CommandPtr& cmd, ReplyPtr reply)>;

struct Command : public std::enable_shared_from_this<Command> {
    Command(
        CmdArgs&& _args,
        ReplyCallback callback,
        CommandControl control,
        int counter,
        bool asking,
        size_t instance_idx,
        bool redirected,
        bool read_only
    );

    const std::string& GetName() const { return name; }

    ReplyCallback Callback() const;

    void ResetStartHandlingTime() { start_handling_time = std::chrono::steady_clock::now(); }

    std::chrono::steady_clock::time_point GetStartHandlingTime() const { return start_handling_time; }

    static logging::LogExtra PrepareLogExtra();

    // Returns Span details only if we are not already running within a Span.
    const logging::LogExtra& GetLogExtra() const;

    CmdArgs args;
    ReplyCallback callback;
    std::chrono::steady_clock::time_point start_handling_time;
    tracing::Span* original_span_debug{nullptr};
    logging::LogExtra log_extra;
    CommandControl control;
    size_t instance_idx = 0;
    uint32_t invoke_counter = 0;
    int counter = 0;
    bool asking = false;
    bool redirected = false;
    bool read_only = false;
    std::string name;
};

CommandPtr PrepareCommand(
    CmdArgs&& args,
    ReplyCallback callback,
    const CommandControl& command_control = {},
    int counter = 0,
    bool asking = false,
    size_t instance_idx = 0,
    bool redirected = false,
    bool read_only = false
);

}  // namespace storages::redis::impl

USERVER_NAMESPACE_END
