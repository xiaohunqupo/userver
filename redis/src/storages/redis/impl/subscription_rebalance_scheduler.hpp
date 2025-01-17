#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

#include <engine/ev/thread_control.hpp>
#include <engine/ev/thread_pool.hpp>

#include <userver/storages/redis/base.hpp>
#include "subscription_storage.hpp"

USERVER_NAMESPACE_BEGIN

namespace storages::redis::impl {

class SubscriptionRebalanceScheduler {
public:
    SubscriptionRebalanceScheduler(
        engine::ev::ThreadPool& thread_pool,
        SubscriptionStorageBase& storage,
        size_t shard_idx
    );
    ~SubscriptionRebalanceScheduler();

    using ServerWeights = SubscriptionStorageBase::ServerWeights;

    void RequestRebalance(ServerWeights weights);

    void Stop();

    void SetRebalanceMinInterval(std::chrono::milliseconds interval);

    std::chrono::milliseconds GetRebalanceMinInterval() const;

private:
    void DoRebalance();

    static void OnRebalanceRequested(struct ev_loop*, ev_async* w, int) noexcept;
    static void OnTimer(struct ev_loop*, ev_timer* w, int) noexcept;

    engine::ev::ThreadControl thread_control_;
    SubscriptionStorageBase& storage_;
    const size_t shard_idx_;

    ev_timer timer_{};
    ev_async rebalance_request_watcher_{};

    mutable std::mutex mutex_;
    ServerWeights weights_;
    bool next_rebalance_scheduled_{false};
    std::chrono::milliseconds rebalance_min_interval_;
};

}  // namespace storages::redis::impl

USERVER_NAMESPACE_END
