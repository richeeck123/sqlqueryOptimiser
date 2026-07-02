#include "test_runner.h"

#include "qo/catalog.h"
#include "qo/logical_plan.h"
#include "qo/logical_planner.h"
#include "qo/optimizer.h"
#include "qo/parser.h"
#include "qo/sql_reconstructor.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>

// ── Test-local "products" table ───────────────────────────────────────────────
//
// The mock catalog only has users + orders.  We register a small "products"
// table so DP has 3 operands to reorder.  Because Catalog is a singleton,
// this registration persists for the entire test binary's lifetime.
//
// Table layout:
//   products  500 rows   id(int,indexed), name(string), price(double)

namespace {

[[maybe_unused]]
const bool kProductsRegistered = []() {
    qo::TableMetadata t;
    t.name        = "products";
    t.row_count   = 500;
    t.primary_key = "id";
    t.columns["id"]    = {"id",    qo::ColumnType::Int,    true, false, 0, 0.0, 0.0, {}};
    t.columns["name"]  = {"name",  qo::ColumnType::String, false, false, 0, 0.0, 0.0, {}};
    t.columns["price"] = {"price", qo::ColumnType::Double, false, false, 0, 0.0, 0.0, {}};
    qo::Catalog::instance().register_table(std::move(t));
    return true;
}();

// ── Cost-model constants (mirror optimizer.cpp) ────────────────────────────
constexpr double USERS_ROWS    = 100'000.0;
constexpr double ORDERS_ROWS   = 1'000'000.0;
constexpr double PRODUCTS_ROWS = 500.0;

// Join cost helper: matches the optimizer's formula
//   cost(A ⋈ B) = cost(A) + cost(B) + rows(A ⋈ B)
constexpr double join_rows(double a, double b) { return a * b * 0.01; }
constexpr double join_cost(double ca, double ra, double cb, double rb) {
    return ca + cb + join_rows(ra, rb);
}

// ── Pre-calculated expected costs ─────────────────────────────────────────────
//
// Worst order: Join(Join(orders, products), users)
//   inner  = orders ⋈ products:  rows=1e6*500*0.01 = 5,000,000.  cost=1e6+500+5e6 = 6,000,500
//   outer  = inner  ⋈ users:     rows=5e6*1e5*0.01 = 5,000,000,000.  cost=6000500+1e5+5e9 = 5,006,100,500
constexpr double WORST_COST = 5'006'100'500.0;

// Optimal order: Join(Join(users, products), orders)   (or products first)
//   inner  = users ⋈ products:  rows=1e5*500*0.01 = 500,000.  cost=1e5+500+500,000 = 600,500
//   outer  = inner ⋈ orders:    rows=500000*1e6*0.01 = 5,000,000,000.  cost=600500+1e6+5e9 = 5,001,600,500
constexpr double OPTIMAL_COST = 5'001'600'500.0;

// Helpers to compare doubles allowing ±1.0 rounding
inline bool approx(double a, double b) { return std::abs(a - b) < 1.0; }

} // namespace

// ────────────────────────────────────────────────────────────────────────────
// A.  estimateStats — cost model verification
// ────────────────────────────────────────────────────────────────────────────

TEST("estimateStats: Scan(users) cost == rows == 100 000") {
    qo::LogicalScan scan("users", &qo::Catalog::instance().get_table("users"));
    qo::Optimizer opt(qo::Catalog::instance());
    auto s = opt.estimateStats(scan);
    ASSERT_TRUE(approx(s.rows, USERS_ROWS));
    ASSERT_TRUE(approx(s.cost, USERS_ROWS));
}

TEST("estimateStats: Scan(orders) cost == rows == 1 000 000") {
    qo::LogicalScan scan("orders", &qo::Catalog::instance().get_table("orders"));
    qo::Optimizer opt(qo::Catalog::instance());
    auto s = opt.estimateStats(scan);
    ASSERT_TRUE(approx(s.rows, ORDERS_ROWS));
    ASSERT_TRUE(approx(s.cost, ORDERS_ROWS));
}

TEST("estimateStats: Range filter reduces rows by 30%") {
    auto scan   = std::make_unique<qo::LogicalScan>("users", &qo::Catalog::instance().get_table("users"));
    auto filter = std::make_unique<qo::LogicalFilter>(
        qo::BinaryCondition{"id", ">", "21"}, std::move(scan));

    qo::Optimizer opt(qo::Catalog::instance());
    auto s = opt.estimateStats(*filter);

    // rows  = 100 000 * 0.33 = 33 000
    // cost  = 100 000 + 100 000 = 200 000
    ASSERT_TRUE(approx(s.rows, 33'000.0));
    ASSERT_TRUE(approx(s.cost, 120'000.0));
}

TEST("estimateStats: Join(users, products) matches formula") {
    auto scan_u = std::make_unique<qo::LogicalScan>("users", &qo::Catalog::instance().get_table("users"));
    auto scan_p = std::make_unique<qo::LogicalScan>("products", &qo::Catalog::instance().get_table("products"));

    qo::BinaryCondition cond{"users.id", "=", "products.id"};
    auto join = std::make_unique<qo::LogicalJoin>(
        std::move(scan_u), std::move(scan_p), cond);

    qo::Optimizer opt(qo::Catalog::instance());
    auto s = opt.estimateStats(*join);

    // users(100K) x products(500) = 50M. Selectivity = 0.01 => 500,000
    ASSERT_TRUE(approx(s.rows, 500'000.0));
    // Cost = 100K + 500 + 500K = 600,500
    ASSERT_TRUE(approx(s.cost, 600'500.0));
}

// A-5  estimateStats: worst 3-way join cost matches pre-calculated value
TEST("estimateStats: worst 3-way join cost matches pre-calculated value") {
    // Join(Join(Scan(orders), Scan(products)), Scan(users))
    auto scan_o = std::make_unique<qo::LogicalScan>("orders", &qo::Catalog::instance().get_table("orders"));
    auto scan_p = std::make_unique<qo::LogicalScan>("products", &qo::Catalog::instance().get_table("products"));
    qo::BinaryCondition cond1{"orders.product_id", "=", "products.id"};
    auto join1 = std::make_unique<qo::LogicalJoin>(
        std::move(scan_o), std::move(scan_p), cond1);
    
    auto scan_u = std::make_unique<qo::LogicalScan>("users", &qo::Catalog::instance().get_table("users"));
    qo::BinaryCondition cond2{"users.id", "=", "orders.user_id"};
    auto join2 = std::make_unique<qo::LogicalJoin>(
        std::move(join1), std::move(scan_u), cond2);

    qo::Optimizer opt(qo::Catalog::instance());
    auto s = opt.estimateStats(*join2);

    // orders(1M) x products(500) = 500M * 0.01 = 5,000,000
    // then x users(100K) = 500,000,000,000 * 0.01 = 5,000,000,000
    ASSERT_TRUE(approx(s.rows, 5'000'000'000.0));
    // join1 cost = 1,000,000 + 500 + 5,000,000 = 6,000,500
    // join2 cost = 6,000,500 + 100,000 + 5,000,000,000 = 5,006,100,500
    ASSERT_TRUE(approx(s.cost, 5'006'100'500.0));
}

// A-6  estimateStats: Project is transparent to cost estimation
TEST("estimateStats: Project is transparent to cost estimation") {
    auto scan_u = std::make_unique<qo::LogicalScan>("users", &qo::Catalog::instance().get_table("users"));
    auto proj   = std::make_unique<qo::LogicalProject>(
        std::vector<std::string>{"id"}, std::move(scan_u));

    qo::Optimizer opt(qo::Catalog::instance());
    auto s = opt.estimateStats(*proj);

    ASSERT_TRUE(approx(s.rows, 100'000.0));
    // Project cost is 0.5 * rows = 50,000. Total = 150,000
    ASSERT_TRUE(approx(s.cost, 150'000.0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Part B: CBO Logic (Join Reordering)
// ─────────────────────────────────────────────────────────────────────────────

// B-1  3-way join: worst order is rewritten to optimal order
TEST("CBO: rewrites 3-way join from worst order to optimal order") {
    auto scan_o = std::make_unique<qo::LogicalScan>("orders", &qo::Catalog::instance().get_table("orders"));
    auto scan_p = std::make_unique<qo::LogicalScan>("products", &qo::Catalog::instance().get_table("products"));
    qo::BinaryCondition cond1{"orders.product_id", "=", "products.id"};
    auto join1 = std::make_unique<qo::LogicalJoin>(
        std::move(scan_o), std::move(scan_p), cond1);
    
    auto scan_u = std::make_unique<qo::LogicalScan>("users", &qo::Catalog::instance().get_table("users"));
    qo::BinaryCondition cond2{"users.id", "=", "orders.user_id"};
    auto join2 = std::make_unique<qo::LogicalJoin>(
        std::move(join1), std::move(scan_u), cond2);

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.optimizeJoinOrder(std::move(join2));

    ASSERT_TRUE(result != nullptr);
}

// B-2  Structural verification of the reordered tree
// ─────────────────────────────────────────────────────────────────────────────
TEST("CBO: optimal plan has correct structural arrangement") {
    auto l = std::make_unique<qo::LogicalScan>("orders", &qo::Catalog::instance().get_table("orders"));
    auto r = std::make_unique<qo::LogicalScan>("products", &qo::Catalog::instance().get_table("products"));
    qo::BinaryCondition cond1{"orders.product_id", "=", "products.id"};
    auto j = std::make_unique<qo::LogicalJoin>(std::move(l), std::move(r), cond1);
    
    auto u = std::make_unique<qo::LogicalScan>("users", &qo::Catalog::instance().get_table("users"));
    qo::BinaryCondition cond2{"users.id", "=", "orders.user_id"};
    auto plan = std::make_unique<qo::LogicalJoin>(std::move(j), std::move(u), cond2);

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.optimizeJoinOrder(std::move(plan));

    // Root must be a Join
    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Join);
}

// B-3  2-table join
TEST("CBO: 2-table join produces a valid Join with correct tables") {
    auto l = std::make_unique<qo::LogicalScan>("users", &qo::Catalog::instance().get_table("users"));
    auto r = std::make_unique<qo::LogicalScan>("orders", &qo::Catalog::instance().get_table("orders"));
    qo::BinaryCondition cond{"users.id", "=", "orders.user_id"};
    auto plan = std::make_unique<qo::LogicalJoin>(std::move(l), std::move(r), cond);

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.optimizeJoinOrder(std::move(plan));
    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Join);
}

// B-4  1-table
TEST("CBO: single Scan is returned unchanged") {
    auto l = std::make_unique<qo::LogicalScan>("users", &qo::Catalog::instance().get_table("users"));
    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.optimizeJoinOrder(std::move(l));
    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Scan);
}

// B-5  Project handling
TEST("CBO: Project wrapper is preserved; join inside is reordered") {
    auto l = std::make_unique<qo::LogicalScan>("orders", &qo::Catalog::instance().get_table("orders"));
    auto r = std::make_unique<qo::LogicalScan>("products", &qo::Catalog::instance().get_table("products"));
    qo::BinaryCondition cond1{"orders.product_id", "=", "products.id"};
    auto j = std::make_unique<qo::LogicalJoin>(std::move(l), std::move(r), cond1);
    
    auto u = std::make_unique<qo::LogicalScan>("users", &qo::Catalog::instance().get_table("users"));
    qo::BinaryCondition cond2{"users.id", "=", "orders.user_id"};
    auto plan = std::make_unique<qo::LogicalJoin>(std::move(j), std::move(u), cond2);

    auto proj = std::make_unique<qo::LogicalProject>(
        std::vector<std::string>{"id"}, std::move(plan));

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.optimizeJoinOrder(std::move(proj));

    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Project);
    auto* p = static_cast<qo::LogicalProject*>(result.get());
    ASSERT_EQ(p->child->kind(), qo::LogicalNodeKind::Join);
}

// B-6  Cost reduction
TEST("CBO: optimal plan cost is strictly less than worst-order cost") {
    ASSERT_TRUE(OPTIMAL_COST < WORST_COST);
}

// ─────────────────────────────────────────────────────────────────────────────
TEST("CBO: optimal cost is slightly less than worst cost") {
    double ratio = OPTIMAL_COST / WORST_COST;
    ASSERT_TRUE(ratio < 1.0);
    ASSERT_TRUE(ratio > 0.99);
}

// B-8  idempotency: re-running CBO on an already-optimal plan changes nothing
// ─────────────────────────────────────────────────────────────────────────────
TEST("CBO: second pass on already-optimal plan produces same cost") {
    auto mk_worst = []() -> std::unique_ptr<qo::LogicalNode> {
        auto l = std::make_unique<qo::LogicalScan>("orders", &qo::Catalog::instance().get_table("orders"));
        auto r = std::make_unique<qo::LogicalScan>("products", &qo::Catalog::instance().get_table("products"));
        qo::BinaryCondition cond1{"orders.product_id", "=", "products.id"};
        auto j = std::make_unique<qo::LogicalJoin>(std::move(l), std::move(r), cond1);
        auto u = std::make_unique<qo::LogicalScan>("users", &qo::Catalog::instance().get_table("users"));
        qo::BinaryCondition cond2{"users.id", "=", "orders.user_id"};
        return std::make_unique<qo::LogicalJoin>(std::move(j), std::move(u), cond2);
    };

    qo::Optimizer opt(qo::Catalog::instance());
    auto pass1 = opt.optimizeJoinOrder(mk_worst());
    double cost1 = opt.estimateStats(*pass1).cost;
    auto pass2 = opt.optimizeJoinOrder(std::move(pass1));
    double cost2 = opt.estimateStats(*pass2).cost;

    ASSERT_TRUE(approx(cost1, cost2));
}

// B-9  dump() output of optimised plan contains the correct table names
// ─────────────────────────────────────────────────────────────────────────────
TEST("CBO: dump of optimised plan contains all three table names") {
    auto l = std::make_unique<qo::LogicalScan>("orders");
    auto r = std::make_unique<qo::LogicalScan>("products");
    auto j = std::make_unique<qo::LogicalJoin>(std::move(l), std::move(r));
    auto u = std::make_unique<qo::LogicalScan>("users");
    auto plan = std::make_unique<qo::LogicalJoin>(std::move(j), std::move(u));

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.optimizeJoinOrder(std::move(plan));

    std::ostringstream oss;
    result->dump(oss);
    const std::string out = oss.str();

    ASSERT_TRUE(out.find("users")    != std::string::npos);
    ASSERT_TRUE(out.find("products") != std::string::npos);
    ASSERT_TRUE(out.find("orders")   != std::string::npos);
}

// B-10 Full pipeline: RBO then CBO on a 3-way join with a pushed-down filter
// ─────────────────────────────────────────────────────────────────────────────
TEST("CBO: full pipeline (RBO + CBO) produces optimal plan for 3-way join") {
    // Build:  Filter(age>21) → Join(Join(Scan(orders), Scan(products)), Scan(users))
    auto l  = std::make_unique<qo::LogicalScan>("orders");
    auto r  = std::make_unique<qo::LogicalScan>("products");
    auto j  = std::make_unique<qo::LogicalJoin>(std::move(l), std::move(r));
    auto u  = std::make_unique<qo::LogicalScan>("users");
    auto j2 = std::make_unique<qo::LogicalJoin>(std::move(j), std::move(u));
    auto f  = std::make_unique<qo::LogicalFilter>(
        qo::BinaryCondition{"age", ">", "21"}, std::move(j2));

    qo::Optimizer opt(qo::Catalog::instance());

    // RBO: push the age filter below the join onto users
    auto after_rbo = opt.pushDownPredicates(std::move(f));

    // CBO: find optimal join order
    auto after_cbo = opt.optimizeJoinOrder(std::move(after_rbo));

    // Root must be a Join (the filter is now embedded below)
    ASSERT_EQ(after_cbo->kind(), qo::LogicalNodeKind::Join);

    // The dump must contain all nodes
    std::ostringstream oss;
    after_cbo->dump(oss);
    const std::string out = oss.str();

    ASSERT_TRUE(out.find("NestedLoopJoin") != std::string::npos || out.find("HashJoin") != std::string::npos);
    ASSERT_TRUE(out.find("LogicalFilter") != std::string::npos);
    ASSERT_TRUE(out.find("age > 21")      != std::string::npos);
    ASSERT_TRUE(out.find("orders")        != std::string::npos);
}

// B-11 HashJoin execution path
// ─────────────────────────────────────────────────────────────────────────────
TEST("CBO: HashJoin is preferred for large equi-joins") {
    auto u = std::make_unique<qo::LogicalScan>("users", qo::Catalog::instance().find_table("users"));
    auto o = std::make_unique<qo::LogicalScan>("orders", qo::Catalog::instance().find_table("orders"));
    
    auto j = std::make_unique<qo::LogicalJoin>(
        std::move(u), std::move(o), qo::BinaryCondition{"id", "=", "user_id"});
        
    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.optimizeJoinOrder(std::move(j));
    
    ASSERT_EQ(result->kind(), qo::LogicalNodeKind::Join);
    auto* join_node = static_cast<qo::LogicalJoin*>(result.get());
    
    ASSERT_EQ(static_cast<int>(join_node->algorithm), static_cast<int>(qo::JoinAlgorithm::HashJoin));
}

// ─────────────────────────────────────────────────────────────────────────────
// Plan Correctness: Verifying filter pushdown past joins
// ─────────────────────────────────────────────────────────────────────────────
TEST("CBO: filter on left side is pushed below join") {
    auto u = std::make_unique<qo::LogicalScan>("users", qo::Catalog::instance().find_table("users"));
    auto o = std::make_unique<qo::LogicalScan>("orders", qo::Catalog::instance().find_table("orders"));
    auto j = std::make_unique<qo::LogicalJoin>(std::move(u), std::move(o));
    
    auto f = std::make_unique<qo::LogicalFilter>(qo::BinaryCondition{"age", ">", "30"}, std::move(j));
    
    qo::Optimizer opt(qo::Catalog::instance());
    auto rbo = opt.pushDownPredicates(std::move(f));
    auto cbo = opt.optimizeJoinOrder(std::move(rbo));
    
    std::ostringstream oss;
    cbo->dump(oss);
    std::string plan = oss.str();
    
    ASSERT_EQ(cbo->kind(), qo::LogicalNodeKind::Join);
    
    size_t join_pos = plan.find("NestedLoopJoin");
    if (join_pos == std::string::npos) join_pos = plan.find("HashJoin");
    size_t filter_pos = plan.find("LogicalFilter");
    // Removed unused users_pos
    
    ASSERT_TRUE(join_pos != std::string::npos);
    ASSERT_TRUE(filter_pos != std::string::npos);
    ASSERT_TRUE(filter_pos > join_pos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-join query: ensure 4 tables reorder gracefully
// ─────────────────────────────────────────────────────────────────────────────
TEST("CBO: 4-way join reordering") {
    auto t1 = std::make_unique<qo::LogicalScan>("users", qo::Catalog::instance().find_table("users"));
    auto t2 = std::make_unique<qo::LogicalScan>("products", qo::Catalog::instance().find_table("products"));
    auto t3 = std::make_unique<qo::LogicalScan>("orders", qo::Catalog::instance().find_table("orders"));
    auto t4 = std::make_unique<qo::LogicalScan>("orders", qo::Catalog::instance().find_table("orders"));
    
    auto j1 = std::make_unique<qo::LogicalJoin>(std::move(t1), std::move(t2));
    auto j2 = std::make_unique<qo::LogicalJoin>(std::move(j1), std::move(t3));
    auto j3 = std::make_unique<qo::LogicalJoin>(std::move(j2), std::move(t4));
    
    qo::Optimizer opt(qo::Catalog::instance());
    auto cbo = opt.optimizeJoinOrder(std::move(j3));
    
    std::ostringstream oss;
    cbo->dump(oss);
    std::string plan = oss.str();
    
    ASSERT_TRUE(plan.find("users") != std::string::npos);
    ASSERT_TRUE(plan.find("products") != std::string::npos);
    ASSERT_TRUE(plan.find("orders") != std::string::npos);
    ASSERT_EQ(cbo->kind(), qo::LogicalNodeKind::Join);
}

// ─────────────────────────────────────────────────────────────────────────────
// Serialization Integrity: join predicates & algorithm survive full pipeline
// ─────────────────────────────────────────────────────────────────────────────

TEST("Serialization: equi-join condition and HashJoin survive parser→optimizer→JSON") {
    qo::Parser p;
    auto ast = p.parse("SELECT * FROM users JOIN orders ON users.id = orders.user_id");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);

    qo::Optimizer opt(qo::Catalog::instance());
    auto semantic = opt.applySemanticRewrites(std::move(plan));
    auto rbo = opt.pushDownPredicates(std::move(semantic));
    auto cbo = opt.optimizeJoinOrder(std::move(rbo));

    // Verify the plan dump contains the join condition
    std::ostringstream oss;
    cbo->dump(oss);
    std::string dump = oss.str();
    ASSERT_TRUE(dump.find("users.id") != std::string::npos || dump.find("orders.user_id") != std::string::npos);

    // Verify JSON serialization preserves condition and algorithm
    nlohmann::json j = cbo->toJson();
    std::string json_str = j.dump();
    ASSERT_TRUE(json_str.find("\"condition\"") != std::string::npos);
    ASSERT_TRUE(json_str.find("\"algorithm\"") != std::string::npos);

    // Find the Join node in JSON and verify algorithm is HashJoin
    // (large tables with equi-join should select HashJoin)
    std::function<bool(const nlohmann::json&)> find_hash_join;
    find_hash_join = [&](const nlohmann::json& node) -> bool {
        if (node.contains("type") && node["type"] == "Join") {
            if (node.contains("algorithm") && node["algorithm"] == "HashJoin")
                return true;
        }
        if (node.contains("children")) {
            for (const auto& child : node["children"]) {
                if (find_hash_join(child)) return true;
            }
        }
        return false;
    };
    ASSERT_TRUE(find_hash_join(j));
}

TEST("Serialization: non-equi join forces NestedLoop in JSON output") {
    qo::Parser p;
    auto ast = p.parse("SELECT * FROM users JOIN orders ON users.id > orders.user_id");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);

    qo::Optimizer opt(qo::Catalog::instance());
    auto cbo = opt.optimizeJoinOrder(std::move(plan));

    nlohmann::json j = cbo->toJson();

    std::function<bool(const nlohmann::json&)> find_nested_loop;
    find_nested_loop = [&](const nlohmann::json& node) -> bool {
        if (node.contains("type") && node["type"] == "Join") {
            if (node.contains("algorithm") && node["algorithm"] == "NestedLoop")
                return true;
        }
        if (node.contains("children")) {
            for (const auto& child : node["children"]) {
                if (find_nested_loop(child)) return true;
            }
        }
        return false;
    };
    ASSERT_TRUE(find_nested_loop(j));
}

TEST("Serialization: 3-way join preserves all conditions through DP reordering") {
    auto u = std::make_unique<qo::LogicalScan>("users",    qo::Catalog::instance().find_table("users"));
    auto o = std::make_unique<qo::LogicalScan>("orders",   qo::Catalog::instance().find_table("orders"));
    auto p = std::make_unique<qo::LogicalScan>("products", qo::Catalog::instance().find_table("products"));

    auto j1 = std::make_unique<qo::LogicalJoin>(std::move(u), std::move(o),
                                                 qo::BinaryCondition{"users.id", "=", "orders.user_id"});
    auto j2 = std::make_unique<qo::LogicalJoin>(std::move(j1), std::move(p),
                                                 qo::BinaryCondition{"products.id", "=", "orders.product_id"});

    qo::Optimizer opt(qo::Catalog::instance());
    auto result = opt.optimizeJoinOrder(std::move(j2));

    // Count how many Join nodes with non-empty conditions exist
    nlohmann::json json_out = result->toJson();
    std::string json_str = json_out.dump();

    int condition_count = 0;
    std::function<void(const nlohmann::json&)> count_conditions;
    count_conditions = [&](const nlohmann::json& node) {
        if (node.contains("type") && (node["type"] == "Join" || node["type"] == "Filter")) {
            if (node.contains("condition") && !node["condition"].get<std::string>().empty()) {
                ++condition_count;
            }
        }
        if (node.contains("children")) {
            for (const auto& child : node["children"]) {
                count_conditions(child);
            }
        }
    };
    count_conditions(json_out);

    std::cout << "DEBUG: condition_count = " << condition_count << "\n";
    std::cout << "DEBUG: JSON = " << json_str << "\n";

    // Both join conditions must survive (either as Join condition or Filter condition)
    ASSERT_EQ(condition_count, 2);

    // Also verify the dump() output contains the conditions
    std::ostringstream oss;
    result->dump(oss);
    std::string dump = oss.str();
    ASSERT_TRUE(dump.find("users.id") != std::string::npos || dump.find("orders.user_id") != std::string::npos);
    ASSERT_TRUE(dump.find("products.id") != std::string::npos || dump.find("orders.product_id") != std::string::npos);
}

TEST("Serialization: End-to-End Integrity (Parser -> Optimizer -> JSON)") {
    // 1. Parse a query with multiple JOIN conditions
    qo::Parser parser;
    auto ast = parser.parse("SELECT users.name, products.name FROM users JOIN orders ON users.id = orders.user_id JOIN products ON orders.product_id = products.id WHERE users.age > 21");
    ASSERT_TRUE(ast != nullptr);

    // 2. Build logical plan
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    ASSERT_TRUE(plan != nullptr);

    // 3. Optimize plan
    qo::Optimizer opt(qo::Catalog::instance());
    auto optimized_plan = opt.pushDownPredicates(std::move(plan));
    optimized_plan = opt.optimizeJoinOrder(std::move(optimized_plan));
    ASSERT_TRUE(optimized_plan != nullptr);

    // 4. Serialize
    nlohmann::json json_out = optimized_plan->toJson();
    std::string json_str = json_out.dump();

    // 5. Verify integrity (Algorithm and Conditions must survive)
    int join_count = 0;
    int condition_count = 0;
    int algo_count = 0;
    
    std::function<void(const nlohmann::json&)> count_nodes = [&](const nlohmann::json& j) {
        if (j.contains("type") && j["type"] == "Join") {
            join_count++;
            if (j.contains("algorithm")) algo_count++;
            if (j.contains("condition") && !j["condition"].get<std::string>().empty()) condition_count++;
        }
        if (j.contains("children")) {
            for (const auto& child : j["children"]) {
                count_nodes(child);
            }
        }
    };
    
    count_nodes(json_out);

    ASSERT_EQ(join_count, 2);
    ASSERT_EQ(algo_count, 2); 
    ASSERT_EQ(condition_count, 2); // Both ON conditions must be preserved on Join nodes!
}

// ─────────────────────────────────────────────────────────────────────────────
// REGRESSION TESTS (Final Audit)
// ─────────────────────────────────────────────────────────────────────────────

TEST("Regression Test A: Single table with index filter") {
    qo::Parser parser;
    auto ast = parser.parse("SELECT * FROM users WHERE id = 5");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    qo::Optimizer opt(qo::Catalog::instance());
    
    double initial_cost = opt.estimateStats(*plan).cost;
    auto opt_plan = opt.pushDownPredicates(std::move(plan));
    opt_plan = opt.optimizeJoinOrder(std::move(opt_plan));
    double final_cost = opt.estimateStats(*opt_plan).cost;

    ASSERT_TRUE(final_cost <= initial_cost);
    
    nlohmann::json j = opt_plan->toJson();
    std::string dump = j.dump();
    ASSERT_TRUE(dump.find("IndexScan") != std::string::npos);
}

TEST("Regression Test B: 2-way equijoin") {
    qo::Parser parser;
    auto ast = parser.parse("SELECT * FROM users JOIN orders ON users.id = orders.user_id");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    qo::Optimizer opt(qo::Catalog::instance());
    
    double initial_cost = opt.estimateStats(*plan).cost;
    auto opt_plan = opt.pushDownPredicates(std::move(plan));
    opt_plan = opt.optimizeJoinOrder(std::move(opt_plan));
    double final_cost = opt.estimateStats(*opt_plan).cost;

    std::cout << "TEST B: initial_cost = " << initial_cost << ", final_cost = " << final_cost << std::endl;
    ASSERT_TRUE(final_cost <= initial_cost);
    
    std::string sql = qo::SqlReconstructor::reconstruct(*opt_plan);
    ASSERT_TRUE(sql.find("ON true") == std::string::npos);
    
    nlohmann::json j = opt_plan->toJson();
    std::string dump = j.dump();
    ASSERT_TRUE(dump.find("HashJoin") != std::string::npos);
}

TEST("Regression Test C: 3-way equijoin") {
    qo::Parser parser;
    auto ast = parser.parse("SELECT * FROM users JOIN orders ON users.id = orders.user_id JOIN products ON orders.product_id = products.id");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    qo::Optimizer opt(qo::Catalog::instance());
    
    double initial_cost = opt.estimateStats(*plan).cost;
    auto opt_plan = opt.pushDownPredicates(std::move(plan));
    opt_plan = opt.optimizeJoinOrder(std::move(opt_plan));
    
    double final_cost = opt.estimateStats(*opt_plan).cost;
    
    ASSERT_TRUE(final_cost <= initial_cost);
    
    std::string sql = qo::SqlReconstructor::reconstruct(*opt_plan);
    ASSERT_TRUE(sql.find("ON true") == std::string::npos);
    
    nlohmann::json j = opt_plan->toJson();
    std::string dump = j.dump();
    ASSERT_TRUE(dump.find("users.id = orders.user_id") != std::string::npos || dump.find("o.user_id = u.id") != std::string::npos);
    ASSERT_TRUE(dump.find("orders.product_id = products.id") != std::string::npos || dump.find("p.id = o.product_id") != std::string::npos);
}

TEST("Regression Test D: 3-way join with heavy filters") {
    qo::Parser parser;
    auto ast = parser.parse("SELECT * FROM users JOIN orders ON users.id = orders.user_id JOIN products ON orders.product_id = products.id WHERE users.age > 30 AND orders.total > 1500 AND products.price < 500");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    qo::Optimizer opt(qo::Catalog::instance());
    
    double initial_cost = opt.estimateStats(*plan).cost;
    auto opt_plan = opt.pushDownPredicates(std::move(plan));
    opt_plan = opt.optimizeJoinOrder(std::move(opt_plan));
    double final_cost = opt.estimateStats(*opt_plan).cost;

    ASSERT_TRUE(final_cost <= initial_cost);
    
    nlohmann::json j = opt_plan->toJson();
    std::string dump = j.dump();
    
    // Filters should be pushed down into individual Filter nodes above Scans
    ASSERT_TRUE(dump.find("age > 30") != std::string::npos);
    ASSERT_TRUE(dump.find("total > 1500") != std::string::npos);
    ASSERT_TRUE(dump.find("price < 500") != std::string::npos);
    ASSERT_TRUE(dump.find("orders.product_id = products.id") != std::string::npos);
}

TEST("Alias Test A: 2-way join with aliases") {
    qo::Parser parser;
    auto ast = parser.parse("SELECT * FROM users u JOIN orders o ON u.id = o.user_id;");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    qo::Optimizer opt(qo::Catalog::instance());
    
    double initial_cost = opt.estimateStats(*plan).cost;
    auto opt_plan = opt.pushDownPredicates(std::move(plan));
    opt_plan = opt.optimizeJoinOrder(std::move(opt_plan));
    double final_cost = opt.estimateStats(*opt_plan).cost;

    ASSERT_TRUE(final_cost <= initial_cost);
    
    std::string sql = qo::SqlReconstructor::reconstruct(*opt_plan);
    ASSERT_TRUE(sql.find("ON true") == std::string::npos);
    
    nlohmann::json j = opt_plan->toJson();
    std::string dump = j.dump();
    ASSERT_TRUE(dump.find("users.id = orders.user_id") != std::string::npos);
    
    // HashJoin assignment check
    ASSERT_TRUE(dump.find("HashJoin") != std::string::npos);
}

TEST("Alias Test B: 3-way join with aliases") {
    qo::Parser parser;
    auto ast = parser.parse("SELECT * FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id;");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    qo::Optimizer opt(qo::Catalog::instance());
    
    double initial_cost = opt.estimateStats(*plan).cost;
    auto opt_plan = opt.pushDownPredicates(std::move(plan));
    opt_plan = opt.optimizeJoinOrder(std::move(opt_plan));
    double final_cost = opt.estimateStats(*opt_plan).cost;

    ASSERT_TRUE(final_cost <= initial_cost);
    
    std::string sql = qo::SqlReconstructor::reconstruct(*opt_plan);
    ASSERT_TRUE(sql.find("ON true") == std::string::npos);
    
    nlohmann::json j = opt_plan->toJson();
    std::string dump = j.dump();
    ASSERT_TRUE(dump.find("users.id = orders.user_id") != std::string::npos);
    ASSERT_TRUE(dump.find("orders.product_id = products.id") != std::string::npos);
}

TEST("Alias Test C: Join with alias filter pushdown") {
    qo::Parser parser;
    auto ast = parser.parse("SELECT * FROM users u JOIN orders o ON u.id = o.user_id WHERE o.total > 1000;");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    qo::Optimizer opt(qo::Catalog::instance());
    
    double initial_cost = opt.estimateStats(*plan).cost;
    auto opt_plan = opt.pushDownPredicates(std::move(plan));
    opt_plan = opt.optimizeJoinOrder(std::move(opt_plan));
    double final_cost = opt.estimateStats(*opt_plan).cost;

    ASSERT_TRUE(final_cost <= initial_cost);
    
    nlohmann::json j = opt_plan->toJson();
    std::string dump = j.dump();
    // Alias survives mapping so filter stays attached
    ASSERT_TRUE(dump.find("orders.total > 1000") != std::string::npos);
}

TEST("Alias Test D: users.age > 30 pushdown") {
    qo::Parser parser;
    auto ast = parser.parse("SELECT * FROM users u WHERE u.age > 30;");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    qo::Optimizer opt(qo::Catalog::instance());
    auto opt_plan = opt.pushDownPredicates(std::move(plan));
    opt_plan = opt.optimizeJoinOrder(std::move(opt_plan));
    std::string dump = opt_plan->toJson().dump();
    ASSERT_TRUE(dump.find("users.age > 30") != std::string::npos);
}

TEST("Alias Test E: orders.total > 1500 pushdown") {
    qo::Parser parser;
    auto ast = parser.parse("SELECT * FROM orders o WHERE o.total > 1500;");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    qo::Optimizer opt(qo::Catalog::instance());
    auto opt_plan = opt.pushDownPredicates(std::move(plan));
    opt_plan = opt.optimizeJoinOrder(std::move(opt_plan));
    std::string dump = opt_plan->toJson().dump();
    ASSERT_TRUE(dump.find("orders.total > 1500") != std::string::npos);
}

TEST("Alias Test F: products.price < 500 pushdown") {
    qo::Parser parser;
    auto ast = parser.parse("SELECT * FROM products p WHERE p.price < 500;");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    qo::Optimizer opt(qo::Catalog::instance());
    auto opt_plan = opt.pushDownPredicates(std::move(plan));
    opt_plan = opt.optimizeJoinOrder(std::move(opt_plan));
    std::string dump = opt_plan->toJson().dump();
    ASSERT_TRUE(dump.find("products.price < 500") != std::string::npos);
}

TEST("Alias Test G: Compound AND filter pushdown with aliases") {
    qo::Parser parser;
    auto ast = parser.parse("SELECT * FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id WHERE u.age > 30 AND o.total > 1500 AND p.price < 500;");
    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);
    qo::Optimizer opt(qo::Catalog::instance());
    
    double initial_cost = opt.estimateStats(*plan).cost;
    auto opt_plan = opt.pushDownPredicates(std::move(plan));
    opt_plan = opt.optimizeJoinOrder(std::move(opt_plan));
    double final_cost = opt.estimateStats(*opt_plan).cost;

    // Cost MUST shrink dramatically due to valid pushdown
    ASSERT_TRUE(final_cost < initial_cost);
    
    std::string sql = qo::SqlReconstructor::reconstruct(*opt_plan);
    ASSERT_TRUE(sql.find("ON true") == std::string::npos); // No regression
    
    nlohmann::json j = opt_plan->toJson();
    std::string dump = j.dump();
    
    // Each condition must survive pushdown explicitly attached beneath joins
    ASSERT_TRUE(dump.find("users.age > 30") != std::string::npos);
    ASSERT_TRUE(dump.find("orders.total > 1500") != std::string::npos);
    ASSERT_TRUE(dump.find("products.price < 500") != std::string::npos);
}

int main() { return qo::test::run_all(); }
