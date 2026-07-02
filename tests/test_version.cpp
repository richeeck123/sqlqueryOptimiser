#include "test_runner.h"
#include "qo/version.h"

// ── Sanity / smoke tests ───────────────────────────────────────────────────────

TEST("version string is non-empty") {
    ASSERT_TRUE(!qo::version().empty());
}

TEST("version string equals expected value") {
    ASSERT_EQ(qo::version(), "0.1.0");
}

TEST("basic arithmetic") {
    ASSERT_EQ(1 + 1, 2);
    ASSERT_TRUE(42 > 0);
}

int main() { return qo::test::run_all(); }
