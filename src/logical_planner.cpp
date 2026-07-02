#include "qo/logical_planner.h"
#include <stdexcept>

namespace qo {

// ── buildPredicateTree ────────────────────────────────────────────────────────
//
// Recursively mirrors a PredicateExpr (AST layer) into a PredicateTree
// (logical plan IR layer).  The two types are structurally identical but live
// in separate layers: AST types are parsed strings; IR types reference catalog
// BinaryConditions.

std::unique_ptr<PredicateTree>
LogicalPlanner::buildPredicateTree(const PredicateExpr& expr) {
    if (expr.is_leaf) {
        return std::make_unique<PredicateTree>(
            BinaryCondition{expr.column, expr.op, expr.value});
    }
    // Branch: recurse on both children.
    return std::make_unique<PredicateTree>(
        expr.logical_op,
        buildPredicateTree(*expr.left),
        buildPredicateTree(*expr.right));
}

// ── plan ──────────────────────────────────────────────────────────────────────
//
// Translates the QueryNode AST into a logical plan tree in five steps:
//
//   Step 1 — Validate mandatory AST fields.
//   Step 2 — Build leaf LogicalScan for the primary FROM table.
//   Step 3 — Chain any explicit JOINs, each producing a LogicalJoin.
//   Step 4 — Wrap with LogicalFilter if a WHERE clause is present.
//   Step 5 — Wrap with LogicalAggregate if a GROUP BY clause is present.
//   Step 6 — Top: LogicalProject.

std::unique_ptr<LogicalNode>
LogicalPlanner::plan(const QueryNode& query) const {

    // ── 1. Validate mandatory fields ──────────────────────────────────────────
    if (!query.table)
        throw std::runtime_error("LogicalPlanner: QueryNode has no table");
    if (!query.select)
        throw std::runtime_error("LogicalPlanner: QueryNode has no SELECT list");

    const std::string& tbl_name = query.table->name;
    const TableMetadata* meta   = catalog_.find_table(tbl_name);
    if (!meta)
        throw std::runtime_error(
            "LogicalPlanner: table '" + tbl_name + "' not found in catalog");

    // ── 2. Leaf: LogicalScan for the primary FROM table ───────────────────────
    std::unique_ptr<LogicalNode> node =
        std::make_unique<LogicalScan>(tbl_name, meta);

    // ── 3. Chain explicit JOIN clauses ────────────────────────────────────────
    //
    // Each JoinClause adds a LogicalJoin whose:
    //   left  = the current plan subtree (growing left-deep)
    //   right = a new LogicalScan for the joined table
    //   cond  = the ON predicate, stored as a BinaryCondition
    //
    // The Catalog is consulted for every joined table to attach its metadata.
    for (const auto& jc : query.joins) {
        const TableMetadata* join_meta = catalog_.find_table(jc.table_name);
        if (!join_meta)
            throw std::runtime_error(
                "LogicalPlanner: JOIN table '" +
                jc.table_name + "' not found in catalog");

        auto join_scan = std::make_unique<LogicalScan>(jc.table_name, join_meta);
        BinaryCondition on_cond{jc.on_left, jc.on_op, jc.on_right};

        node = std::make_unique<LogicalJoin>(
            std::move(node), std::move(join_scan), std::move(on_cond));
    }

    // ── 4. Optional WHERE → LogicalFilter ────────────────────────────────────
    //
    // Two paths:
    //   a) Compound predicate (AND/OR tree) — use the three-argument
    //      LogicalFilter constructor with the full PredicateTree.
    //   b) Simple single predicate — use the backward-compatible two-argument
    //      constructor so the existing plan/optimizer code paths are unchanged.
    //
    // The safety fallback reads query.where directly for callers that bypass
    // the new parser (e.g., direct AST construction in test helpers).
    if (query.where_expr) {
        if (query.where_expr->is_leaf) {
            // ── Simple leaf: backward-compat two-argument path ──────────────
            BinaryCondition cond{
                query.where_expr->column,
                query.where_expr->op,
                query.where_expr->value
            };
            node = std::make_unique<LogicalFilter>(std::move(cond), std::move(node));
        } else {
            // ── Compound AND/OR: three-argument path ───────────────────────
            // The representative 'condition' field gets the first leaf for any
            // code that still reads filter->condition directly (Phase 3 will
            // replace this with proper compound-aware push-down).
            std::vector<BinaryCondition> leaves;
            buildPredicateTree(*query.where_expr)->collectLeaves(leaves);
            BinaryCondition rep_cond = leaves.empty()
                ? BinaryCondition{}
                : leaves.front();

            auto compound = buildPredicateTree(*query.where_expr);
            node = std::make_unique<LogicalFilter>(
                std::move(rep_cond), std::move(node), std::move(compound));
        }
    } else if (query.where) {
        // ── Safety fallback: direct WhereNode (pre-Phase-1 construction) ────
        BinaryCondition cond{
            query.where->column,
            query.where->op,
            query.where->value
        };
        node = std::make_unique<LogicalFilter>(std::move(cond), std::move(node));
    }

    // ── 5. Optional GROUP BY → LogicalAggregate ───────────────────────────────
    if (query.group_by && !query.group_by->columns.empty()) {
        node = std::make_unique<LogicalAggregate>(
            query.group_by->columns, std::move(node));
    }

    // ── 6. Root: LogicalProject ───────────────────────────────────────────────
    node = std::make_unique<LogicalProject>(
        query.select->columns, std::move(node));

    return node;
}

} // namespace qo
