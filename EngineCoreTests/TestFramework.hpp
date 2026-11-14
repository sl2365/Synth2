#pragma once

// Minimal single-header test framework for the engine core.
// Tests self-register via OXO_TEST; main.cpp calls oxo::test::RunAllTests().

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace oxo {
namespace test {

struct TestCase {
    const char *name;
    std::function<void()> fn;
};

inline std::vector<TestCase> &Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

struct Registrar {
    Registrar(const char *name, std::function<void()> fn) {
        Registry().push_back(TestCase{name, std::move(fn)});
    }
};

inline int &FailureCount() {
    static int count = 0;
    return count;
}

inline const char *&CurrentTestName() {
    static const char *name = "";
    return name;
}

inline void Fail(const char *file, int line, const std::string &message) {
    std::printf("FAIL %s (%s:%d): %s\n", CurrentTestName(), file, line, message.c_str());
    FailureCount()++;
}

template <typename T>
inline std::string ToString(const T &value) {
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

template <typename A, typename B>
inline void ExpectEq(const char *file, int line,
                     const char *exprA, const char *exprB,
                     const A &a, const B &b) {
    if (!(a == b)) {
        Fail(file, line, std::string(exprA) + " == " + exprB +
                             " (" + ToString(a) + " vs " + ToString(b) + ")");
    }
}

inline void ExpectNear(const char *file, int line,
                       const char *exprA, const char *exprB,
                       double a, double b, double tolerance) {
    if (std::fabs(a - b) > tolerance) {
        Fail(file, line, std::string(exprA) + " ~= " + exprB +
                             " (" + ToString(a) + " vs " + ToString(b) +
                             ", tolerance " + ToString(tolerance) + ")");
    }
}

// filter: run only tests whose name contains it, and announce each PASS
// (the full run stays quiet on success).
inline int RunAllTests(const char *filter = nullptr) {
    int failedTests = 0;
    size_t ranTests = 0;
    for (const TestCase &testCase : Registry()) {
        if (filter && std::string(testCase.name).find(filter) == std::string::npos) continue;
        ranTests++;
        CurrentTestName() = testCase.name;
        int failuresBefore = FailureCount();
        testCase.fn();
        if (FailureCount() != failuresBefore) {
            failedTests++;
        } else if (filter) {
            std::printf("PASS %s\n", testCase.name);
        }
    }
    std::printf("%zu tests, %d failed\n", ranTests, failedTests);
    return failedTests == 0 && ranTests > 0 ? 0 : 1;
}

} // namespace test
} // namespace oxo

#define OXO_TEST(name)                                                        \
    static void oxo_test_##name();                                            \
    static ::oxo::test::Registrar oxo_registrar_##name(#name, oxo_test_##name); \
    static void oxo_test_##name()

#define OXO_EXPECT(condition)                                                 \
    do {                                                                      \
        if (!(condition)) {                                                   \
            ::oxo::test::Fail(__FILE__, __LINE__, "expected: " #condition);   \
        }                                                                     \
    } while (0)

#define OXO_EXPECT_EQ(a, b)                                                   \
    ::oxo::test::ExpectEq(__FILE__, __LINE__, #a, #b, (a), (b))

#define OXO_EXPECT_NEAR(a, b, tolerance)                                      \
    ::oxo::test::ExpectNear(__FILE__, __LINE__, #a, #b, (a), (b), (tolerance))
