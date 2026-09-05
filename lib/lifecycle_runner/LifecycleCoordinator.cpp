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

bool LifecycleCoordinator::CancellableStart::tryComplete() noexcept
{
    if (owner_ == nullptr) {
        return false;
    }

    if (!owner_->tryCompleteCancellableStart()) {
        // Cancellation won. Retain ownership so the caller can finish
        // startup rollback before resolving the lifecycle through fail().
        return false;
    }

    owner_ = nullptr;
    return true;
}

void LifecycleCoordinator::CancellableStart::fail() noexcept
{
    if (owner_ == nullptr) {
        return;
    }

    LifecycleCoordinator* owner = owner_;
    owner_ = nullptr;

    owner->failCancellableStart();
}

void LifecycleCoordinator::clearCancellableStartLocked() noexcept
{
    cancellableStartActive_ = false;
    startCancellationRequested_ = false;
    startStopSource_ = std::stop_source{std::nostopstate};
    startOwnerThread_ = {};
}

void LifecycleCoordinator::resolveTransition(
    State target,
    bool success) noexcept
{
    {
        std::scoped_lock lock(mutex_);

        if (target == State::starting) {
            // Abandoned or failed startup resolves to stopped.
            state_ = success ? State::running : State::stopped;

            // Ordinary and cancellable starts cannot coexist, but clear this
            // defensively so every resolved start begins from clean state.
            clearCancellableStartLocked();
        } else {
            // Teardown is never rolled back to running.
            state_ = State::stopped;
        }
    }

    cv_.notify_all();
}

LifecycleCoordinator::Transition
LifecycleCoordinator::beginStart()
{
    std::unique_lock lock(mutex_);

    for (;;) {
        switch (state_) {
            case State::stopped:
                clearCancellableStartLocked();
                state_ = State::starting;
                return Transition(this, State::starting);

            case State::running:
                return Transition{};

            case State::starting:
            case State::stopping:
                cv_.wait(lock, [this] {
                    return state_ == State::stopped ||
                           state_ == State::running;
                });
                break;
        }
    }
}

LifecycleCoordinator::CancellableStart
LifecycleCoordinator::beginCancellableStart()
{
    std::unique_lock lock(mutex_);

    for (;;) {
        switch (state_) {
            case State::stopped:
                state_ = State::starting;
                cancellableStartActive_ = true;
                startCancellationRequested_ = false;
                startStopSource_ = std::stop_source{};
                startOwnerThread_ = std::this_thread::get_id();

                return CancellableStart(
                    this,
                    startStopSource_.get_token());

            case State::running:
                return CancellableStart{};

            case State::starting:
            case State::stopping:
                cv_.wait(lock, [this] {
                    return state_ == State::stopped ||
                           state_ == State::running;
                });
                break;
        }
    }
}

LifecycleCoordinator::Transition
LifecycleCoordinator::beginStop(WaitPolicy waitPolicy)
{
    std::unique_lock lock(mutex_);

    for (;;) {
        switch (state_) {
            case State::running:
                state_ = State::stopping;
                return Transition(this, State::stopping);

            case State::stopped:
                return Transition{};

            case State::starting: {
                std::stop_source cancellationSource{
                    std::nostopstate};

                if (cancellableStartActive_) {
                    // Publish cancellation intent while holding mutex_. Once
                    // this flag is visible, tryComplete() cannot publish
                    // running.
                    startCancellationRequested_ = true;
                    cancellationSource = startStopSource_;
                }

                // request_stop() invokes stop callbacks synchronously. Those
                // callbacks may execute arbitrary owner code or re-enter this
                // coordinator, so it must run outside mutex_.
                lock.unlock();

                if (cancellationSource.stop_possible()) {
                    cancellationSource.request_stop();
                }

                if (waitPolicy == WaitPolicy::no_wait) {
                    return Transition{};
                }

                lock.lock();

                cv_.wait(lock, [this] {
                    return state_ != State::starting;
                });

                // A successful ordinary start is now running and must be
                // claimed for teardown. A failed or cancelled start is
                // stopped. Re-evaluate both cases.
                break;
            }

            case State::stopping:
                if (waitPolicy == WaitPolicy::no_wait) {
                    return Transition{};
                }

                cv_.wait(lock, [this] {
                    return state_ != State::stopping;
                });

                // The active teardown owns cleanup. Once it completes this
                // caller returns without claiming another transition.
                return Transition{};
        }
    }
}

bool LifecycleCoordinator::tryCompleteCancellableStart() noexcept
{
    {
        std::scoped_lock lock(mutex_);

        if (state_ != State::starting ||
            !cancellableStartActive_ ||
            startCancellationRequested_) {
            return false;
        }

        state_ = State::running;
        clearCancellableStartLocked();
    }

    cv_.notify_all();
    return true;
}

void LifecycleCoordinator::failCancellableStart() noexcept
{
    {
        std::scoped_lock lock(mutex_);

        if (state_ != State::starting ||
            !cancellableStartActive_) {
            return;
        }

        state_ = State::stopped;
        clearCancellableStartLocked();
    }

    cv_.notify_all();
}

bool LifecycleCoordinator::isStartOwnerThread() const noexcept
{
    std::scoped_lock lock(mutex_);

    return state_ == State::starting &&
           cancellableStartActive_ &&
           startOwnerThread_ != std::thread::id{} &&
           startOwnerThread_ == std::this_thread::get_id();
}

LifecycleCoordinator::State
LifecycleCoordinator::state() const noexcept
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
