// Pause / resume / stop of a scan, exercised with a real scanning thread.

#include "TempDir.hpp"
#include "TestFramework.hpp"

#include "core/FileManager.hpp"
#include "core/FolderManager.hpp"
#include "core/ScanControl.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <stop_token>
#include <thread>

namespace fs = std::filesystem;
using fc::test::TempDir;

namespace {

constexpr auto kSettleTime = std::chrono::milliseconds(150);

struct ScanFixture {
    TempDir dir;
    fs::path a = dir.makeFolder("A");
    fs::path b = dir.makeFolder("B");

    ScanFixture()
    {
        for (const char* name : {"one.txt", "two.txt", "sub/three.txt"}) {
            dir.write(fs::path("A") / name, "same");
            dir.write(fs::path("B") / name, "same");
        }
        dir.write("A/only_a.txt", "a");
    }
};

} // namespace

FC_TEST(scan_control_checkpoint_without_pause_or_stop_returns)
{
    const fc::ScanControl control;
    FC_CHECK_NOTHROW(control.checkpoint());

    fc::PauseControl pause;
    FC_CHECK(!pause.isPaused());
    pause.pause();
    FC_CHECK(pause.isPaused());
    pause.resume();
    FC_CHECK(!pause.isPaused());
    const fc::ScanControl withPause{std::stop_token{}, &pause};
    FC_CHECK_NOTHROW(withPause.checkpoint());
}

FC_TEST(scan_control_checkpoint_throws_once_stopped)
{
    std::stop_source source; // NOLINT(misc-const-correctness): request_stop() is not const everywhere
    source.request_stop();
    const fc::ScanControl control{source.get_token(), nullptr};
    FC_CHECK_THROWS_AS(control.checkpoint(), fc::OperationCancelled);
}

FC_TEST(scan_paused_compare_waits_then_completes_on_resume)
{
    const ScanFixture fixture;
    fc::PauseControl pause;
    pause.pause(); // paused before it even starts listing

    std::atomic<std::size_t> progressCalls{0};
    std::atomic<bool> finished{false};
    fc::ComparisonResult result;

    std::jthread worker([&] {
        result = fc::FolderManager::compare(
            fixture.a, fixture.b, {.deepComparison = true, .recursive = true},
            [&progressCalls](std::size_t, std::size_t, const fs::path&) { ++progressCalls; },
            fc::ScanControl{std::stop_token{}, &pause});
        finished = true;
    });

    std::this_thread::sleep_for(kSettleTime);
    FC_CHECK(!finished.load());
    FC_CHECK_EQ(progressCalls.load(), std::size_t{0});

    pause.resume();
    worker.join();
    FC_CHECK(finished.load());
    FC_CHECK_EQ(progressCalls.load(), std::size_t{3});
    FC_CHECK_EQ(result.identicalCount(), std::size_t{3});
    FC_CHECK_EQ(result.onlyACount(), std::size_t{1});
}

FC_TEST(scan_pause_during_comparison_holds_the_next_file)
{
    const ScanFixture fixture;
    fc::PauseControl pause;
    std::atomic<std::size_t> progressCalls{0};
    std::atomic<bool> finished{false};

    std::jthread worker([&] {
        (void)fc::FolderManager::compare(
            fixture.a, fixture.b, {.deepComparison = true, .recursive = true},
            [&](std::size_t, std::size_t, const fs::path&) {
                if (++progressCalls == 1) {
                    pause.pause(); // the user pauses while the first file is compared
                }
            },
            fc::ScanControl{std::stop_token{}, &pause});
        finished = true;
    });

    std::this_thread::sleep_for(kSettleTime);
    FC_CHECK(!finished.load());
    FC_CHECK_EQ(progressCalls.load(), std::size_t{1});

    pause.resume();
    worker.join();
    FC_CHECK_EQ(progressCalls.load(), std::size_t{3});
}

FC_TEST(scan_stop_while_paused_cancels_promptly)
{
    const ScanFixture fixture;
    fc::PauseControl pause;
    pause.pause();
    std::stop_source source; // NOLINT(misc-const-correctness): request_stop() is not const everywhere
    std::exception_ptr error;

    std::jthread worker([&] {
        try {
            (void)fc::FolderManager::compare(fixture.a, fixture.b, {.deepComparison = true, .recursive = true}, {},
                                             fc::ScanControl{source.get_token(), &pause});
        }
        catch (...) {
            error = std::current_exception();
        }
    });

    std::this_thread::sleep_for(kSettleTime);
    const auto stopRequested = std::chrono::steady_clock::now();
    source.request_stop(); // still paused: the wait must wake up
    worker.join();
    FC_CHECK(std::chrono::steady_clock::now() - stopRequested < std::chrono::seconds(2));

    FC_REQUIRE(error != nullptr);
    FC_CHECK_THROWS_AS(std::rethrow_exception(error), fc::OperationCancelled);
}

FC_TEST(scan_listing_honors_the_stop_request)
{
    const ScanFixture fixture;
    std::stop_source source; // NOLINT(misc-const-correctness): request_stop() is not const everywhere
    source.request_stop();
    FC_CHECK_THROWS_AS((void)fc::FileManager::listEntries(fixture.a, true, fc::ScanControl{source.get_token(), nullptr}),
                       fc::OperationCancelled);
}
