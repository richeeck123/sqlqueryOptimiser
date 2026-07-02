#pragma once

/**
 * Minimal header-only test harness (zero dependencies).
 *
 * Usage:
 *   #include "test_runner.h"
 *
 *   TEST("my test") { ASSERT_EQ(1 + 1, 2); }
 *
 *   int main() { return qo::test::run_all(); }
 */

#include <cassert>
#include <cstdio>
#include <functional>
#include <string_view>
#include <vector>

namespace qo::test {

struct Case {
    std::string_view name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Registrar {
    Registrar(std::string_view name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline int run_all() {
    int passed = 0, failed = 0;
    for (auto& tc : registry()) {
        std::printf("[ RUN  ] %.*s\n",
                    static_cast<int>(tc.name.size()), tc.name.data());
        try {
            tc.fn();
            std::printf("[ PASS ] %.*s\n",
                        static_cast<int>(tc.name.size()), tc.name.data());
            ++passed;
        } catch (const std::exception& e) {
            std::printf("[ FAIL ] %.*s  (%s)\n",
                        static_cast<int>(tc.name.size()), tc.name.data(),
                        e.what());
            ++failed;
        } catch (...) {
            std::printf("[ FAIL ] %.*s  (unknown exception)\n",
                        static_cast<int>(tc.name.size()), tc.name.data());
            ++failed;
        }
    }
    std::printf("\nResults: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

} // namespace qo::test

// ── Assertion macro ────────────────────────────────────────────────────────────
#include <stdexcept>
#include <sstream>

#define ASSERT_TRUE(expr)                                                      \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::ostringstream _oss;                                           \
            _oss << "ASSERT_TRUE failed: " #expr                               \
                 << "  (" __FILE__ ":" << __LINE__ << ")";                    \
            throw std::runtime_error(_oss.str());                              \
        }                                                                      \
    } while (false)

#define ASSERT_EQ(a, b)                                                        \
    do {                                                                       \
        if (!((a) == (b))) {                                                   \
            std::ostringstream _oss;                                           \
            _oss << "ASSERT_EQ failed: " #a " == " #b                         \
                 << "  (" __FILE__ ":" << __LINE__ << ")";                    \
            throw std::runtime_error(_oss.str());                              \
        }                                                                      \
    } while (false)

// ── Test registration macro ────────────────────────────────────────────────────
// Two levels of indirection are required so that __COUNTER__ is fully
// expanded by the preprocessor BEFORE token-pasting (##) takes place.
// Without _QO_CONCAT2, the literal text "__COUNTER__" would be pasted,
// producing identical names across every TEST() call.
#define _QO_CONCAT2(a, b)   a##b
#define _QO_CONCAT(a, b)    _QO_CONCAT2(a, b)

#define _QO_TEST_IMPL(name, ctr)                                               \
    [[maybe_unused]]                                                            \
    static void _QO_CONCAT(_qo_test_fn_, ctr)();                               \
    [[maybe_unused]]                                                            \
    static ::qo::test::Registrar                                                \
        _QO_CONCAT(_qo_reg_, ctr){name, _QO_CONCAT(_qo_test_fn_, ctr)};        \
    [[maybe_unused]]                                                            \
    static void _QO_CONCAT(_qo_test_fn_, ctr)()

#define TEST(name) _QO_TEST_IMPL(name, __COUNTER__)
