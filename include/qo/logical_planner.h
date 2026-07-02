#pragma once

/**
 * LogicalPlanner — translates a QueryNode AST into a logical plan tree.
 *
 * Input  → qo::QueryNode  (output of the Parser)
 * Output → std::unique_ptr<LogicalNode>  (root of the logical plan)
 *
 * Translation rules (Phase 2 — extended):
 * ──────────────────────────────────────────
 *   SELECT cols FROM table
 *     → Project( cols, Scan(table) )
 *
 *   SELECT cols FROM t JOIN t2 ON c1=c2
 *     → Project( cols, Join( Scan(t), Scan(t2), cond ) )
 *
 *   SELECT cols FROM t [JOIN ...] WHERE pred
 *     → Project( cols, Filter( pred, ... ) )
 *
 *   SELECT cols FROM t [JOIN ...] [WHERE pred] GROUP BY gc
 *     → Project( cols, Aggregate( gc, Filter?( pred, ... ) ) )
 *
 * The planner validates all table names against the Catalog and copies
 * TableMetadata pointers into every LogicalScan node.
 *
 * Throws std::runtime_error when any referenced table is not in the Catalog.
 */

#include "qo/ast.h"
#include "qo/catalog.h"
#include "qo/logical_plan.h"

#include <memory>

namespace qo {

class LogicalPlanner {
public:
    explicit LogicalPlanner(const Catalog& catalog) : catalog_(catalog) {}

    /// Build a logical plan from a fully parsed QueryNode.
    /// @throws std::runtime_error if any referenced table is not in the catalog.
    [[nodiscard]] std::unique_ptr<LogicalNode> plan(const QueryNode& query) const;

private:
    const Catalog& catalog_;

    /// Recursively convert a PredicateExpr (AST layer) into a PredicateTree
    /// (logical plan IR layer).  Called when the WHERE clause is compound.
    static std::unique_ptr<PredicateTree>
    buildPredicateTree(const PredicateExpr& expr);
};

} // namespace qo
