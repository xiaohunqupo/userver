#include "deadline.hpp"

#include <storages/postgres/experiments.hpp>
#include <storages/postgres/postgres_config.hpp>
#include <userver/server/request/task_inherited_data.hpp>
#include <userver/storages/postgres/exceptions.hpp>
#include <userver/utils/impl/userver_experiments.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::postgres {

void CheckDeadlineIsExpired(const dynamic_config::Snapshot& config) {
    if (config[kDeadlinePropagationVersionConfig] != kDeadlinePropagationExperimentVersion) {
        return;
    }

    const auto inherited_deadline = server::request::GetTaskInheritedDeadline();
    if (inherited_deadline.IsReached()) {
        throw ConnectionInterrupted("Cancelled by deadline");
    }
}

TimeoutDuration AdjustTimeout(TimeoutDuration timeout) {
    if (!USERVER_NAMESPACE::utils::impl::kPgDeadlinePropagationExperiment.IsEnabled()) {
        return timeout;
    }

    const auto inherited_deadline = server::request::GetTaskInheritedDeadline();
    if (!inherited_deadline.IsReachable()) return timeout;

    auto left = std::chrono::duration_cast<TimeoutDuration>(inherited_deadline.TimeLeft());
    return std::min(timeout, left);
}

}  // namespace storages::postgres

USERVER_NAMESPACE_END
