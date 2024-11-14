#pragma once

#include <engine/task/task_counter.hpp>

USERVER_NAMESPACE_BEGIN

namespace engine {

class RunningTaskToken final {
public:
    RunningTaskToken(impl::TaskCounter& counter) : counter_(counter) { counter_.AccountTaskIsRunning(); }

    ~RunningTaskToken() { counter_.AccountTaskIsNotRunning(); }

private:
    impl::TaskCounter& counter_;
};

}  // namespace engine

USERVER_NAMESPACE_END
