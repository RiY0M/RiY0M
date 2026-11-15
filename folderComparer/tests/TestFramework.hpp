#pragma once

// Minimal self-contained test framework: no download, no dependency, builds
// with the same strict warnings as the application.
//
//   FC_TEST(name) { FC_CHECK(expr); FC_CHECK_EQ(a, b); FC_REQUIRE(expr); }
//
// FC_CHECK* records a failure and continues, FC_REQUIRE* stops the test.

#include <exception>
#include <functional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fc::test {

struct TestCase {
    std::string_view name;
    std::string_view file;
    std::function<void()> body;
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(std::string_view name, std::string_view file, std::function<void()> body);
};

/// Thrown by FC_REQUIRE* to abort the current test.
class RequireFailure final : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override { return "requirement failed"; }
};

void reportFailure(std::string_view file, int line, std::string_view message);

template <typename T>
std::string describe(const T& value)
{
    if constexpr (requires(std::ostream& stream) { stream << value; }) {
        std::ostringstream stream;
        stream << value;
        return stream.str();
    }
    else if constexpr (std::is_enum_v<T>) {
        return "enum(" + std::to_string(static_cast<long long>(value)) + ")";
    }
    else {
        return "<unprintable>";
    }
}

template <typename A, typename B>
bool checkEqual(const A& actual, const B& expected, std::string_view actualText,
                std::string_view expectedText, std::string_view file, int line)
{
    if (actual == expected) {
        return true;
    }
    std::string message;
    message.append(actualText).append(" == ").append(expectedText);
    message.append("\n      actual:   ").append(describe(actual));
    message.append("\n      expected: ").append(describe(expected));
    reportFailure(file, line, message);
    return false;
}

} // namespace fc::test

// NOLINTBEGIN(cppcoreguidelines-macro-usage, bugprone-macro-parentheses)

#define FC_TEST_CONCAT_INNER(a, b) a##b
#define FC_TEST_CONCAT(a, b) FC_TEST_CONCAT_INNER(a, b)

#define FC_TEST(name)                                                                              \
    static void name();                                                                            \
    namespace {                                                                                    \
    const ::fc::test::Registrar FC_TEST_CONCAT(name, _registrar){#name, __FILE__, &name}; /* NOLINT(cert-err58-cpp) */ \
    }                                                                                              \
    static void name()

#define FC_CHECK(expr)                                                                             \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            ::fc::test::reportFailure(__FILE__, __LINE__, #expr);                                  \
        }                                                                                          \
    } while (false)

#define FC_REQUIRE(expr)                                                                           \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            ::fc::test::reportFailure(__FILE__, __LINE__, #expr);                                  \
            throw ::fc::test::RequireFailure{};                                                    \
        }                                                                                          \
    } while (false)

#define FC_CHECK_EQ(actual, expected)                                                              \
    do {                                                                                           \
        (void)::fc::test::checkEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__); \
    } while (false)

#define FC_REQUIRE_EQ(actual, expected)                                                            \
    do {                                                                                           \
        if (!::fc::test::checkEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)) { \
            throw ::fc::test::RequireFailure{};                                                    \
        }                                                                                          \
    } while (false)

#define FC_CHECK_THROWS_AS(expr, exception_type)                                                   \
    do {                                                                                           \
        bool fc_thrown_expected = false;                                                           \
        try {                                                                                      \
            expr;                                                                                  \
        }                                                                                          \
        catch (const exception_type&) {                                                            \
            fc_thrown_expected = true;                                                             \
        }                                                                                          \
        catch (...) {                                                                              \
        }                                                                                          \
        if (!fc_thrown_expected) {                                                                 \
            ::fc::test::reportFailure(__FILE__, __LINE__,                                          \
                                      #expr " does not throw " #exception_type);                   \
        }                                                                                          \
    } while (false)

#define FC_CHECK_NOTHROW(expr)                                                                     \
    do {                                                                                           \
        try {                                                                                      \
            expr;                                                                                  \
        }                                                                                          \
        catch (const std::exception& fc_error) {                                                   \
            ::fc::test::reportFailure(__FILE__, __LINE__,                                          \
                                      std::string(#expr " throws: ") + fc_error.what());           \
        }                                                                                          \
    } while (false)

// NOLINTEND(cppcoreguidelines-macro-usage, bugprone-macro-parentheses)
