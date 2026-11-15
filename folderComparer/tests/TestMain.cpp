#include "TestFramework.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <utility>

namespace fc::test {

namespace {

std::size_t& failureCount()
{
    static std::size_t count = 0;
    return count;
}

} // namespace

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

Registrar::Registrar(std::string_view name, std::string_view file, std::function<void()> body)
{
    registry().push_back(TestCase{name, file, std::move(body)});
}

void reportFailure(std::string_view file, int line, std::string_view message)
{
    ++failureCount();
    std::cerr << "    " << file << ':' << line << ": FAILED: " << message << '\n';
}

} // namespace fc::test

namespace {

int run(std::span<char*> arguments)
{
    std::string filter;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string argument = arguments[index];
        if (argument == "--list") {
            for (const auto& test : fc::test::registry()) {
                std::cout << test.name << '\n';
            }
            return EXIT_SUCCESS;
        }
        if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: fc_tests [--list] [name-filter]\n";
            return EXIT_SUCCESS;
        }
        filter = argument;
    }

    std::size_t executed = 0;
    std::size_t failed = 0;
    for (const auto& test : fc::test::registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string_view::npos) {
            continue;
        }

        ++executed;
        const std::size_t failuresBefore = fc::test::failureCount();
        try {
            test.body();
        }
        catch (const fc::test::RequireFailure&) { // NOLINT(bugprone-empty-catch): already reported
        }
        catch (const std::exception& error) {
            fc::test::reportFailure(test.file, 0, std::string("unexpected exception: ") + error.what());
        }
        catch (...) {
            fc::test::reportFailure(test.file, 0, "unexpected non-standard exception");
        }

        const bool passed = fc::test::failureCount() == failuresBefore;
        if (!passed) {
            ++failed;
        }
        std::cout << (passed ? "[  OK  ] " : "[FAILED] ") << test.name << '\n';
    }

    std::cout << '\n' << executed - failed << '/' << executed << " tests passed\n";
    if (executed == 0) {
        std::cerr << "No test matches the filter\n";
        return EXIT_FAILURE;
    }
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        return run(std::span(argv, static_cast<std::size_t>(argc)));
    }
    catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
