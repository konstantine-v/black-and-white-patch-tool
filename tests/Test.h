#pragma once

// Minimal self-registering test framework (no third-party dependency).

#include <format>
#include <functional>
#include <string>
#include <vector>

namespace bwtest {

struct TestCase {
    const char* name;
    std::function<void()> body;
};

std::vector<TestCase>& Registry();
void Fail(const char* file, int line, const std::string& message);

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { Registry().push_back({name, std::move(body)}); }
};

}  // namespace bwtest

#define BWTEST_CONCAT_INNER(a, b) a##b
#define BWTEST_CONCAT(a, b) BWTEST_CONCAT_INNER(a, b)

#define TEST(name)                                                                                    \
    static void BWTEST_CONCAT(test_, name)();                                                         \
    static const ::bwtest::Registrar BWTEST_CONCAT(registrar_, name){#name, &BWTEST_CONCAT(test_, name)}; \
    static void BWTEST_CONCAT(test_, name)()

#define CHECK(condition)                                                                              \
    do {                                                                                              \
        if (!(condition)) {                                                                           \
            ::bwtest::Fail(__FILE__, __LINE__, "CHECK(" #condition ")");                               \
        }                                                                                             \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                    \
    do {                                                                                              \
        const auto& bwtestActual = (actual);                                                          \
        const auto& bwtestExpected = (expected);                                                      \
        if (!(bwtestActual == bwtestExpected)) {                                                      \
            ::bwtest::Fail(__FILE__, __LINE__,                                                        \
                           std::format("CHECK_EQ({}, {}): {} != {}", #actual, #expected, bwtestActual, \
                                       bwtestExpected));                                              \
        }                                                                                             \
    } while (false)
