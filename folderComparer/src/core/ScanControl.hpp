#pragma once

// Pause / resume / stop of a running scan.
//
// The scanning thread calls ScanControl::checkpoint() regularly (per listed
// folder, per compared file, per 1 MiB chunk of a deep comparison): it blocks
// there while the scan is paused and throws OperationCancelled once a stop is
// requested, paused or not.

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <stop_token>

namespace fc {

/// Raised by a scan interrupted through its std::stop_token.
class OperationCancelled final : public std::runtime_error {
public:
    OperationCancelled() : std::runtime_error("Operation cancelled") {}
};

/// Shared between the thread that controls the scan (pause / resume) and the
/// scanning thread (waitWhilePaused). Thread safe.
class PauseControl {
public:
    void pause() noexcept;
    void resume();
    [[nodiscard]] bool isPaused() const noexcept { return paused_.load(); }

    /// Blocks while paused; returns as soon as resumed or `stopToken` is triggered.
    void waitWhilePaused(const std::stop_token& stopToken) const;

private:
    std::atomic<bool> paused_{false};
    mutable std::mutex mutex_;
    mutable std::condition_variable_any resumed_;
};

/// What a scan checks: a stop request and, optionally, a pause.
struct ScanControl {
    std::stop_token stopToken;
    const PauseControl* pause = nullptr;

    /// Waits while paused, then throws OperationCancelled if a stop was requested.
    void checkpoint() const;
};

} // namespace fc
