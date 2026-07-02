#include "test_runner.h"

#include "qo/catalog.h"
#include "qo/logical_plan.h"
#include "qo/logical_planner.h"
#include "qo/optimizer.h"
#include "qo/parser.h"

#include <sstream>
#include <stdexcept>

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Build:  Filter(col op val) → Join(Scan(left_tbl), Scan(right_tbl))
static std::unique_ptr<qo::LogicalNode>
make_filter_over_join(const std::string& col,
                      const std::string& op,
                      const std::string& val,
                      const std::string& left_tbl,
                      const std::string& right_tbl)
{
    auto left  = std::make_unique<qo::LogicalScan>(left_tbl);
    auto right = std::make_unique<qo::LogicalScan>(right_tbl);
    auto join  = std::make_unique<qo::LogicalJoin>(
        std::move(left), std::move(right));
    return std::make_unique<qo::LogicalFilter>(
        qo::BinaryCondition{col, op, val}, std::move(join));
}

// ────────────────────────────────────────────────────────────────────────────
// A. LogicalPlanner — AST → Logical Plan
// ────────────────────────────────────────────────────────────────────────────

TEST("planner: simple SELECT produces Project → Scan") {
    qo::Parser p;
    auto ast  = p.parse("SELECT name FROM users");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);

    ASSERT_EQ(plan->kind(), qo::LogicalNodeKind::Project);
    auto* proj = static_cast<qo::LogicalProject*>(plan.get());

    ASSERT_EQ(proj->columns.size(), std::size_t{1});
    ASSERT_EQ(proj->columns[0], "name");

    ASSERT_TRUE(proj->child != nullptr);
    ASSERT_EQ(proj->child->kind(), qo::LogicalNodeKind::Scan);

    auto* scan = static_cast<qo::LogicalScan*>(proj->child.get());
    ASSERT_EQ(scan->table_name, "users");
    ASSERT_TRUE(scan->metadata != nullptr);
}

TEST("planner: SELECT with WHERE produces Project → Filter → Scan") {
    qo::Parser p;
    auto ast = p.parse("SELECT name FROM users WHERE age > 21");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);

    ASSERT_EQ(plan->kind(), qo::LogicalNodeKind::Project);
    auto* proj = static_cast<qo::LogicalProject*>(plan.get());

    ASSERT_TRUE(proj->child != nullptr);
    ASSERT_EQ(proj->child->kind(), qo::LogicalNodeKind::Filter);
    auto* filter = static_cast<qo::LogicalFilter*>(proj->child.get());

    ASSERT_EQ(filter->condition.column, "age");
    ASSERT_EQ(filter->condition.op,     ">");
    ASSERT_EQ(filter->condition.value,  "21");

    ASSERT_TRUE(filter->child != nullptr);
    ASSERT_EQ(filter->child->kind(), qo::LogicalNodeKind::Scan);
}

TEST("planner: multi-column SELECT lists all columns") {
    qo::Parser p;
    auto ast = p.parse("SELECT id, name, age FROM users");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);

    auto* proj = static_cast<qo::LogicalProject*>(plan.get());
    ASSERT_EQ(proj->columns.size(), std::size_t{3});
    ASSERT_EQ(proj->columns[0], "id");
    ASSERT_EQ(proj->columns[1], "name");
    ASSERT_EQ(proj->columns[2], "age");
}

TEST("planner: Scan carries catalog metadata pointer") {
    qo::Parser p;
    auto ast = p.parse("SELECT id FROM orders");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);

    auto* proj   = static_cast<qo::LogicalProject*>(plan.get());
    auto* scan   = static_cast<qo::LogicalScan*>(proj->child.get());
    ASSERT_TRUE(scan->metadata != nullptr);
    ASSERT_EQ(scan->metadata->row_count, std::size_t{1'000'000});
}

TEST("planner: unknown table throws") {
    qo::Parser p;
    auto ast = p.parse("SELECT x FROM ghost_table");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    bool threw = false;
    try { (void)planner.plan(*ast); }
    catch (const std::runtime_error&) { threw = true; }
    ASSERT_TRUE(threw);
}

// ────────────────────────────────────────────────────────────────────────────
// B. Optimizer — predicate push-down
// ────────────────────────────────────────────────────────────────────────────

// B-1  Filter above Join  →  filter pushed to LEFT child (age is in users)
// ─────────────────────────────────────────────────────────────────────────────
TEST("optimizer: left-table filter is pushed below join to left child") {
    // Input: Filter(age>21) → Join(Scan(users), Scan(orders))
    auto plan = make_filter_over_join("age", ">", "21", "users", "orders");

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.pushDownPredicates(std::move(plan));

    // Root must now be Join
    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Join);
    auto* join = static_cast<qo::LogicalJoin*>(result.get());

    // Left child must be Filter
    ASSERT_TRUE(join->left != nullptr);
    ASSERT_EQ(join->left->kind(), qo::LogicalNodeKind::Filter);

    auto* pushed = static_cast<qo::LogicalFilter*>(join->left.get());
    ASSERT_EQ(pushed->condition.column, "age");
    ASSERT_EQ(pushed->condition.op,     ">");
    ASSERT_EQ(pushed->condition.value,  "21");

    // Filter's child must be the original Scan(users)
    ASSERT_TRUE(pushed->child != nullptr);
    ASSERT_EQ(pushed->child->kind(), qo::LogicalNodeKind::Scan);
    auto* scan = static_cast<qo::LogicalScan*>(pushed->child.get());
    ASSERT_EQ(scan->table_name, "users");

    // Right child must remain unchanged Scan(orders)
    ASSERT_TRUE(join->right != nullptr);
    ASSERT_EQ(join->right->kind(), qo::LogicalNodeKind::Scan);
    auto* rhs = static_cast<qo::LogicalScan*>(join->right.get());
    ASSERT_EQ(rhs->table_name, "orders");
}

// B-2  Filter above Join  →  filter pushed to RIGHT child (total is in orders)
// ─────────────────────────────────────────────────────────────────────────────
TEST("optimizer: right-table filter is pushed below join to right child") {
    // Input: Filter(total>100) → Join(Scan(users), Scan(orders))
    auto plan = make_filter_over_join("total", ">", "100", "users", "orders");

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.pushDownPredicates(std::move(plan));

    // Root must now be Join
    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Join);
    auto* join = static_cast<qo::LogicalJoin*>(result.get());

    // Left child must remain unchanged Scan(users)
    ASSERT_EQ(join->left->kind(), qo::LogicalNodeKind::Scan);
    ASSERT_EQ(static_cast<qo::LogicalScan*>(join->left.get())->table_name, "users");

    // Right child must be Filter
    ASSERT_TRUE(join->right != nullptr);
    ASSERT_EQ(join->right->kind(), qo::LogicalNodeKind::Filter);

    auto* pushed = static_cast<qo::LogicalFilter*>(join->right.get());
    ASSERT_EQ(pushed->condition.column, "total");
    ASSERT_EQ(pushed->condition.op,     ">");
    ASSERT_EQ(pushed->condition.value,  "100");

    // Filter's child must be Scan(orders)
    ASSERT_EQ(pushed->child->kind(), qo::LogicalNodeKind::Scan);
    ASSERT_EQ(static_cast<qo::LogicalScan*>(pushed->child.get())->table_name, "orders");
}

// B-3  Filter above Scan  →  no push (not a join below)
// ─────────────────────────────────────────────────────────────────────────────
TEST("optimizer: filter above Scan is not moved") {
    auto scan   = std::make_unique<qo::LogicalScan>("users");
    auto filter = std::make_unique<qo::LogicalFilter>(
        qo::BinaryCondition{"age", ">", "21"}, std::move(scan));

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.pushDownPredicates(std::move(filter));

    // Root remains a Filter
    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Filter);
    auto* f = static_cast<qo::LogicalFilter*>(result.get());
    ASSERT_EQ(f->child->kind(), qo::LogicalNodeKind::Scan);
}

// B-4  Unknown column  →  filter stays above join
// ─────────────────────────────────────────────────────────────────────────────
TEST("optimizer: filter on unknown column stays above join") {
    // 'salary' is not in users or orders
    auto plan = make_filter_over_join("salary", ">", "50000", "users", "orders");

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.pushDownPredicates(std::move(plan));

    // Root must remain Filter (couldn't attribute column to either side)
    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Filter);
    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Filter);
    auto* f = static_cast<qo::LogicalFilter*>(result.get());
    ASSERT_EQ(f->child->kind(), qo::LogicalNodeKind::Join);
}

// B-5  Condition values preserved exactly after push
// ─────────────────────────────────────────────────────────────────────────────
TEST("optimizer: pushed filter preserves operator and value unchanged") {
    auto plan = make_filter_over_join("user_id", "!=", "0", "users", "orders");

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.pushDownPredicates(std::move(plan));

    // user_id is in orders  →  pushed to right
    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Join);
    auto* join   = static_cast<qo::LogicalJoin*>(result.get());
    auto* pushed = static_cast<qo::LogicalFilter*>(join->right.get());

    ASSERT_EQ(pushed->condition.column, "user_id");
    ASSERT_EQ(pushed->condition.op,     "!=");
    ASSERT_EQ(pushed->condition.value,  "0");
}

// B-6  Project → Filter → Join  →  optimizer recursively pushes through Project
// ─────────────────────────────────────────────────────────────────────────────
TEST("optimizer: recursively pushes filter through Project wrapper") {
    // Build: Project([name]) → Filter(age>21) → Join(Scan(users), Scan(orders))
    auto inner = make_filter_over_join("age", ">", "21", "users", "orders");
    auto plan  = std::make_unique<qo::LogicalProject>(
        std::vector<std::string>{"name"}, std::move(inner));

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.pushDownPredicates(std::move(plan));

    // Root remains Project
    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Project);
    auto* proj = static_cast<qo::LogicalProject*>(result.get());

    // Project's child must now be Join (filter was pushed through it)
    ASSERT_TRUE(proj->child != nullptr);
    ASSERT_EQ(proj->child->kind(), qo::LogicalNodeKind::Join);

    auto* join = static_cast<qo::LogicalJoin*>(proj->child.get());

    // Filter must now be on the left (age is in users)
    ASSERT_EQ(join->left->kind(), qo::LogicalNodeKind::Filter);
    ASSERT_EQ(join->right->kind(), qo::LogicalNodeKind::Scan);
}

// B-7  Optimizer is idempotent: re-running on already-optimized plan is safe
// ─────────────────────────────────────────────────────────────────────────────
TEST("optimizer: idempotent — second pass on already-optimized plan is a no-op") {
    auto plan = make_filter_over_join("age", ">", "21", "users", "orders");

    qo::Optimizer opt(qo::Catalog::instance());
    auto pass1 = opt.pushDownPredicates(std::move(plan));
    auto pass2 = opt.pushDownPredicates(std::move(pass1));

    // After two passes root is still a Join with filter on the left
    ASSERT_EQ(pass2->kind(), qo::LogicalNodeKind::Join);
    auto* join = static_cast<qo::LogicalJoin*>(pass2.get());
    ASSERT_EQ(join->left->kind(),  qo::LogicalNodeKind::Filter);
    ASSERT_EQ(join->right->kind(), qo::LogicalNodeKind::Scan);
}

// B-8  End-to-end: parse → plan → optimize
// ─────────────────────────────────────────────────────────────────────────────
TEST("e2e: parse → plan → optimizer produces valid plan for simple query") {
    qo::Parser p;
    auto ast = p.parse("SELECT name FROM users WHERE age > 21");

    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    // Before optimization: Project → Filter → Scan

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.pushDownPredicates(std::move(plan));
    // No Join present, so optimizer leaves the tree unchanged

    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Project);
    auto* proj = static_cast<qo::LogicalProject*>(result.get());
    ASSERT_EQ(proj->child->kind(), qo::LogicalNodeKind::Filter);
    auto* filter = static_cast<qo::LogicalFilter*>(proj->child.get());
    ASSERT_EQ(filter->child->kind(), qo::LogicalNodeKind::Scan);
}

// B-9  dump() output contains correct labels after optimization
// ─────────────────────────────────────────────────────────────────────────────
TEST("optimizer: dump of optimized tree contains expected node labels") {
    auto plan = make_filter_over_join("age", ">", "21", "users", "orders");

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.pushDownPredicates(std::move(plan));

    std::ostringstream oss;
    result->dump(oss);
    const std::string out = oss.str();

    ASSERT_TRUE(out.find("NestedLoopJoin") != std::string::npos || out.find("HashJoin") != std::string::npos);
    ASSERT_TRUE(out.find("LogicalFilter") != std::string::npos);
    ASSERT_TRUE(out.find("LogicalScan")   != std::string::npos);
    ASSERT_TRUE(out.find("age > 21")      != std::string::npos);
    ASSERT_TRUE(out.find("users")         != std::string::npos);
    ASSERT_TRUE(out.find("orders")        != std::string::npos);
}

// B-10  BinaryCondition::empty() helper
// ─────────────────────────────────────────────────────────────────────────────
TEST("BinaryCondition::empty returns true for default-constructed condition") {
    qo::BinaryCondition cond;
    ASSERT_TRUE(cond.empty());
    cond.column = "age";
    ASSERT_TRUE(!cond.empty());
}

// B-11  LogicalJoin with condition is preserved (join predicate not corrupted)
// ─────────────────────────────────────────────────────────────────────────────
TEST("optimizer: join condition is preserved during push-down") {
    auto left  = std::make_unique<qo::LogicalScan>("users");
    auto right = std::make_unique<qo::LogicalScan>("orders");
    qo::BinaryCondition join_cond{"user_id", "=", "id"};
    auto join = std::make_unique<qo::LogicalJoin>(
        std::move(left), std::move(right), join_cond);

    // Wrap in filter on a users column
    auto filter = std::make_unique<qo::LogicalFilter>(
        qo::BinaryCondition{"age", ">", "21"}, std::move(join));

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.pushDownPredicates(std::move(filter));

    // Root is the join
    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Join);
    auto* j = static_cast<qo::LogicalJoin*>(result.get());

    // Join condition itself must be untouched
    ASSERT_EQ(j->condition.column, "user_id");
    ASSERT_EQ(j->condition.op,     "=");
    ASSERT_EQ(j->condition.value,  "id");
}

int main() { return qo::test::run_all(); }
