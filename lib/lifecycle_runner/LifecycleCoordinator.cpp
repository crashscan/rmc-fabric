#include "LifecycleCoordinator.h"

namespace RSCGroup {

void LifecycleCoordinator::Transition::resolve(bool success) noexcept
{
    if (owner_ == nullptr) {
        return;
    }
    LifecycleCoordinator* owner = owner_;
    owner_ = nullptr;
    owner->resolveTransition(target_, success);
}

void LifecycleCoordinator::resolveTransition(State target, bool success) noexcept
{
    {
        std::scoped_lock lock(mutex_);
        if (target == State::starting) {
            // Abandoned/unresolved start → stopped.
            state_ = success ? State::running : State::stopped;
        } else {
            // Abandoned/unresolved stop → stopped.  Teardown is never rolled
            // back to running, and must never stay stuck in stopping.
            state_ = State::stopped;
        }
    }
    // Every success/failure/exception path notifies waiters.
    cv_.notify_all();
}

LifecycleCoordinator::Transition LifecycleCoordinator::beginStart()
{
    std::unique_lock lock(mutex_);
    for (;;) {
        switch (state_) {
            case State::stopped:
                state_ = State::starting;
                return Transition(this, State::starting);
            case State::running:
                // Already running: the service applies its own health policy.
                return Transition{};
            case State::starting:
            case State::stopping:
                // Wait for the in-flight transition to resolve, then re-evaluate.
                cv_.wait(lock, [this] {
                    return state_ == State::stopped || state_ == State::running;
                });
                break;
        }
    }
}

LifecycleCoordinator::Transition LifecycleCoordinator::beginStop()
{
    std::unique_lock lock(mutex_);
    for (;;) {
        switch (state_) {
            case State::running:
                state_ = State::stopping;
                return Transition(this, State::stopping);
            case State::stopped:
                return Transition{};
            case State::starting:
                // Wait for startup resolution, then claim stop if it succeeded.
                cv_.wait(lock, [this] { return state_ != State::starting; });
                break;
            case State::stopping:
                // A concurrent stop returns only once the active teardown has
                // finished and stopped has become visible.
                cv_.wait(lock, [this] { return state_ != State::stopping; });
                return Transition{};
        }
    }
}

LifecycleCoordinator::State LifecycleCoordinator::state() const noexcept
{
    std::scoped_lock lock(mutex_);
    return state_;
}

bool LifecycleCoordinator::isRunning() const noexcept
{
    std::scoped_lock lock(mutex_);
    return state_ == State::running;
}

bool LifecycleCoordinator::isStopped() const noexcept
{
    std::scoped_lock lock(mutex_);
    return state_ == State::stopped;
}

} // namespace RSCGroup
