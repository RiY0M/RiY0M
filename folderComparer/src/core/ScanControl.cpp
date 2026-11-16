#include "core/ScanControl.hpp"

namespace fc {

void PauseControl::pause() noexcept
{
    paused_.store(true);
}

void PauseControl::resume()
{
    {
        const std::scoped_lock lock(mutex_);
        paused_.store(false);
    }
    resumed_.notify_all();
}

void PauseControl::waitWhilePaused(const std::stop_token& stopToken) const
{
    if (!paused_.load()) {
        return; // fast path: no lock while running
    }
    std::unique_lock lock(mutex_);
    // Wakes up on resume() or when a stop is requested (C++20 interruptible wait).
    resumed_.wait(lock, stopToken, [this] { return !paused_.load(); });
}

void ScanControl::checkpoint() const
{
    if (pause != nullptr) {
        pause->waitWhilePaused(stopToken);
    }
    if (stopToken.stop_requested()) {
        throw OperationCancelled{};
    }
}

} // namespace fc
