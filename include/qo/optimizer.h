#pragma once

/**
 * Query Optimiser — Rule-Based (RBO) and Cost-Based (CBO) passes.
 *
 * ┌─ Rule-Based Optimisation ───────────────────────────────────────────────┐
 │  pushDownPredicates                                                        │
 │    Traverses the plan bottom-up.  When a LogicalFilter sits above a        │
 │    LogicalJoin, the Catalog is consulted to determine unambiguously which  │
 │    join-child owns the filter column.  If attribution is clear the filter  │
 │    is re-linked beneath that subtree.                                      │
 │                                                                            │
 │    Before:  Filter(age>21) → Join(Scan(users), Scan(orders))              │
 │    After:   Join(Filter(age>21) → Scan(users), Scan(orders))              │
 └────────────────────────────────────────────────────────────────────────────┘
 *
 * ┌─ Cost-Based Optimisation ───────────────────────────────────────────────┐
 │  optimizeJoinOrder   (System R style dynamic programming)                  │
 │                                                                            │
 │  Cost model                                                                │
 │    LogicalScan    cost = row_count,  rows = row_count                      │
 │    LogicalFilter  rows = child.rows × 0.10  (flat 10% selectivity)        │
 │                   cost = child.cost + rows                                 │
 │    LogicalJoin    rows = max(left.rows, right.rows)                        │
 │                   cost = left.cost + right.cost + rows                     │
 │                                                                            │
 │  Algorithm                                                                 │
 │    1. Flatten the join tree into its leaf operands (Scan/Filter→Scan).    │
 │    2. Initialise DP table: dp[{i}] = base stats of operand i.             │
 │    3. For k = 2 … n: for each subset S of size k, for each j ∈ S:        │
 │         candidate = extend dp[S∖{j}] with operand j on the right.        │
 │         If cost(candidate) < cost(dp[S]), update dp[S].                   │
 │    4. Read the winning left-deep join order from dp[all tables].           │
 │    5. Rebuild the plan tree in that order.                                 │
 └────────────────────────────────────────────────────────────────────────────┘
 *
 * Usage
 * ─────
 *   qo::Optimizer opt(qo::Catalog::instance());
 *   auto plan = opt.pushDownPredicates(std::move(plan));   // RBO
 *   plan      = opt.optimizeJoinOrder(std::move(plan));    // CBO
 */

#include "qo/catalog.h"
#include "qo/logical_plan.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace qo {

class Optimizer {
public:
    explicit Optimizer(const Catalog& catalog) : catalog_(catalog) {}

    // ── Shared cost type ──────────────────────────────────────────────────────

    /// Estimated execution cost and output cardinality for a plan node.
    struct PlanStats {
        double cost;  ///< Estimated I/O + CPU cost (arbitrary units)
        double rows;  ///< Estimated output row count
        double cost_scan = 0.0;
        double cost_filter = 0.0;
        double cost_join = 0.0;
        double cost_project = 0.0;
    };

    // ── Semantic Rewrite pass ─────────────────────────────────────────────────

    /// Apply structural semantic rewrites (SELECT * unrolling, OR-to-UNION).
    /// Executed before rule-based optimisation.
    [[nodiscard]] std::unique_ptr<LogicalNode>
    applySemanticRewrites(std::unique_ptr<LogicalNode> plan);

    // ── Rule-Based pass ───────────────────────────────────────────────────────

    /// Apply predicate push-down recursively over the plan tree.
    /// Takes ownership of the root; returns the (possibly restructured) root.
    [[nodiscard]] std::unique_ptr<LogicalNode>
    pushDownPredicates(std::unique_ptr<LogicalNode> root);

    // ── Cost-Based pass ───────────────────────────────────────────────────────

    /// Recursively estimate the cost and row count of any plan subtree.
    /// Exposed publicly so tests can assert exact cost values.
    [[nodiscard]] PlanStats estimateStats(const LogicalNode& node) const;

    /// Reorder join operands using System R dynamic programming to minimise
    /// estimated cost.  Wrapper nodes (Project, Filter) are passed through
    /// transparently.  Takes ownership; returns the optimised root.
    [[nodiscard]] std::unique_ptr<LogicalNode>
    optimizeJoinOrder(std::unique_ptr<LogicalNode> plan);

private:
    const Catalog& catalog_;

    // ── RBO helpers ───────────────────────────────────────────────────────────

    [[nodiscard]] std::unordered_set<std::string>
    collect_tables(const LogicalNode& node) const;

    [[nodiscard]] bool
    column_in_tables(const std::string& col,
                     const std::unordered_set<std::string>& tables) const;

    [[nodiscard]] std::unique_ptr<LogicalNode>
    tryPushFilterThroughJoin(std::unique_ptr<LogicalNode> filter_node);

    /// Returns true when every node in the PredicateTree is either a leaf or
    /// an AND branch.  OR sub-trees cannot be independently pushed through a
    /// join — the whole OR expression must be evaluated together.
    [[nodiscard]] static bool isPureAndTree(const PredicateTree* tree) noexcept;

    /// Decompose a compound AND filter above a Join into per-conjunct pushes.
    /// Each leaf BinaryCondition is attributed to the left or right join-child
    /// via the Catalog.  Residual conjuncts (column ambiguous or unknown) are
    /// re-stacked as simple LogicalFilter nodes above the join.
    [[nodiscard]] std::unique_ptr<LogicalNode>
    tryPushCompoundAndFilterThroughJoin(std::unique_ptr<LogicalNode> filter_node);

    // ── CBO helpers ───────────────────────────────────────────────────────────

    /// An edge extracted from the join tree during flattening.
    /// Records the condition and algorithm from a LogicalJoin node, along with
    /// the indices of the two operand subtrees it connected.
    struct JoinEdge {
        BinaryCondition condition;
        JoinAlgorithm   algorithm;
        int left_operand_idx;   // index into the flattened operands vector
        int right_operand_idx;  // index into the flattened operands vector
    };

    /// Flatten any join tree (left-deep or bushy) into its leaf operands
    /// by recursively stripping LogicalJoin nodes.  Each non-Join subtree
    /// (Scan, Filter→Scan, …) is appended to 'operands' in left-to-right
    /// order.  Each stripped LogicalJoin's condition and algorithm are
    /// recorded as a JoinEdge in 'edges'.
    static void flatten_join(std::unique_ptr<LogicalNode> node,
                             std::vector<std::unique_ptr<LogicalNode>>& operands,
                             std::vector<JoinEdge>& edges);

    /// Run System R DP on the flattened operands of a join sub-tree and
    /// return the rebuilt join tree in optimal order.
    [[nodiscard]] std::unique_ptr<LogicalNode>
    dp_join_order(std::unique_ptr<LogicalNode> join_root);
};

} // namespace qo
