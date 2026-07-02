#include "qo/optimizer.h"

#include <iostream>

namespace qo {

// ── applySemanticRewrites ───────────────────────────────────────────────────
//
// Bottom-up traversal applying structural semantic rewrites:
//   1. SELECT * UNROLLING: replaces "*" in LogicalProject with explicit columns.
//   2. OR-to-UNION EXPANSION: splits single-scan OR branches into a LogicalUnion.

std::unique_ptr<LogicalNode>
Optimizer::applySemanticRewrites(std::unique_ptr<LogicalNode> plan) {
    if (!plan) return plan;

    // 1. Process children first (bottom-up)
    switch (plan->kind()) {
        case LogicalNodeKind::Filter: {
            auto* filter = static_cast<LogicalFilter*>(plan.get());
            filter->child = applySemanticRewrites(std::move(filter->child));
            break;
        }
        case LogicalNodeKind::Project: {
            auto* proj = static_cast<LogicalProject*>(plan.get());
            proj->child = applySemanticRewrites(std::move(proj->child));
            break;
        }
        case LogicalNodeKind::Join: {
            auto* join = static_cast<LogicalJoin*>(plan.get());
            join->left = applySemanticRewrites(std::move(join->left));
            join->right = applySemanticRewrites(std::move(join->right));
            break;
        }
        case LogicalNodeKind::Aggregate: {
            auto* agg = static_cast<LogicalAggregate*>(plan.get());
            agg->child = applySemanticRewrites(std::move(agg->child));
            break;
        }
        case LogicalNodeKind::Union: {
            auto* u = static_cast<LogicalUnion*>(plan.get());
            u->left = applySemanticRewrites(std::move(u->left));
            u->right = applySemanticRewrites(std::move(u->right));
            break;
        }
        case LogicalNodeKind::Scan:
            break;
    }

    // 2. Apply rewrites on the current node
    if (plan->kind() == LogicalNodeKind::Project) {
        auto* proj = static_cast<LogicalProject*>(plan.get());
        bool has_star = false;
        for (const auto& col : proj->columns) {
            if (col == "*") { has_star = true; break; }
        }
        if (has_star) {
            std::vector<std::string> new_cols;
            auto tables = collect_tables(*proj->child);
            // Sort tables to keep output deterministic
            std::vector<std::string> sorted_tables(tables.begin(), tables.end());
            std::sort(sorted_tables.begin(), sorted_tables.end());
            for (const auto& table_name : sorted_tables) {
                if (const auto* meta = catalog_.find_table(table_name)) {
                    // Extract and sort column names for deterministic unrolling
                    std::vector<std::string> col_names;
                    for (const auto& pair : meta->columns) col_names.push_back(pair.first);
                    std::sort(col_names.begin(), col_names.end());
                    for (const auto& col_name : col_names) {
                        new_cols.push_back(table_name + "." + col_name);
                    }
                }
            }
            std::vector<std::string> final_cols;
            for (const auto& col : proj->columns) {
                if (col == "*") {
                    final_cols.insert(final_cols.end(), new_cols.begin(), new_cols.end());
                } else {
                    final_cols.push_back(col);
                }
            }
            proj->columns = std::move(final_cols);
        }
    } else if (plan->kind() == LogicalNodeKind::Filter) {
        auto* filter = static_cast<LogicalFilter*>(plan.get());
        if (filter->compound_cond && filter->compound_cond->logical_op == "OR") {
            if (filter->compound_cond->left && filter->compound_cond->left->is_leaf &&
                filter->compound_cond->right && filter->compound_cond->right->is_leaf) {
                if (filter->child && filter->child->kind() == LogicalNodeKind::Scan) {
                    auto* scan = static_cast<LogicalScan*>(filter->child.get());
                    
                    auto left_cond = filter->compound_cond->left->condition;
                    auto right_cond = filter->compound_cond->right->condition;
                    
                    auto table_name = scan->table_name;
                    auto metadata = scan->metadata;
                    
                    auto left_scan = std::move(filter->child);
                    auto right_scan = std::make_unique<LogicalScan>(std::move(table_name), metadata);
                    
                    auto left_filter = std::make_unique<LogicalFilter>(std::move(left_cond), std::move(left_scan));
                    auto right_filter = std::make_unique<LogicalFilter>(std::move(right_cond), std::move(right_scan));
                    
                    return std::make_unique<LogicalUnion>(std::move(left_filter), std::move(right_filter));
                }
            }
        }
    }

    return plan;
}

// ── collect_tables ────────────────────────────────────────────────────────────
//
// Walks the subtree rooted at 'node' and returns the set of table names
// exposed by every LogicalScan leaf.  This lets the optimizer attribute
// a filter predicate to the left or right child of a join without
// depending on column-lineage tracking (which is overkill at this stage).

std::unordered_set<std::string>
Optimizer::collect_tables(const LogicalNode& node) const {
    switch (node.kind()) {

    case LogicalNodeKind::Scan: {
        const auto& scan = static_cast<const LogicalScan&>(node);
        return {scan.table_name};
    }

    case LogicalNodeKind::Filter: {
        const auto& filter = static_cast<const LogicalFilter&>(node);
        return filter.child ? collect_tables(*filter.child)
                            : std::unordered_set<std::string>{};
    }

    case LogicalNodeKind::Project: {
        const auto& project = static_cast<const LogicalProject&>(node);
        return project.child ? collect_tables(*project.child)
                             : std::unordered_set<std::string>{};
    }

    case LogicalNodeKind::Join: {
        const auto& join = static_cast<const LogicalJoin&>(node);
        auto tables = join.left ? collect_tables(*join.left)
                                : std::unordered_set<std::string>{};
        if (join.right) {
            auto right_tables = collect_tables(*join.right);
            tables.merge(right_tables);
        }
        return tables;
    }

    // ── Aggregate: transparent — delegate to child ────────────────────────────
    case LogicalNodeKind::Aggregate: {
        const auto& agg = static_cast<const LogicalAggregate&>(node);
        return agg.child ? collect_tables(*agg.child)
                         : std::unordered_set<std::string>{};
    }

    // ── Union ─────────────────────────────────────────────────────────────────
    case LogicalNodeKind::Union: {
        const auto& u = static_cast<const LogicalUnion&>(node);
        auto tables = u.left ? collect_tables(*u.left)
                             : std::unordered_set<std::string>{};
        if (u.right) {
            auto right_tables = collect_tables(*u.right);
            tables.merge(right_tables);
        }
        return tables;
    }

    }
    return {};
}

// ── column_in_tables ──────────────────────────────────────────────────────────
//
// Returns true when 'col' appears in the schema of at least one of the
// given table names, according to the Catalog.

bool Optimizer::column_in_tables(
        const std::string& col,
        const std::unordered_set<std::string>& tables) const {
    for (const auto& tbl : tables) {
        const auto* meta = catalog_.find_table(tbl);
        if (meta) {
            std::string col_name = col;
            if (col.find(tbl + ".") == 0) {
                col_name = col.substr(tbl.length() + 1);
            }
            if (meta->find_column(col_name) != nullptr) {
                return true;
            }
        }
    }
    return false;
}

// ── tryPushFilterThroughJoin ──────────────────────────────────────────────────
//
// Pre-condition: filter_node->kind() == Filter
//               filter->child->kind()  == Join
//
// The function takes ownership of filter_node, extracts the join child,
// determines which branch (left or right) owns the filter column, injects
// a new LogicalFilter beneath that branch, and returns the restructured
// join.  If attribution is ambiguous (column in neither or both sides), the
// original filter node is returned unchanged.

std::unique_ptr<LogicalNode>
Optimizer::tryPushFilterThroughJoin(std::unique_ptr<LogicalNode> filter_node) {
    auto* filter = static_cast<LogicalFilter*>(filter_node.get());
    auto* join   = static_cast<LogicalJoin*>(filter->child.get());

    const std::string& col = filter->condition.column;

    const auto left_tables  = collect_tables(*join->left);
    const auto right_tables = collect_tables(*join->right);

    const bool in_left  = column_in_tables(col, left_tables);
    const bool in_right = column_in_tables(col, right_tables);

    // ── Push to left subtree ──────────────────────────────────────────────────
    if (in_left && !in_right) {
        // Detach the join from the filter (takes ownership of the join)
        auto join_node = std::move(filter->child);
        auto* j        = static_cast<LogicalJoin*>(join_node.get());

        // Wrap the join's left child in a new filter (condition is copied)
        j->left = std::make_unique<LogicalFilter>(
            filter->condition,      // copy the predicate
            std::move(j->left));    // steal left child

        // filter_node is now hollow (child was moved); it destructs here.
        return join_node;
    }

    // ── Push to right subtree ─────────────────────────────────────────────────
    if (in_right && !in_left) {
        auto join_node = std::move(filter->child);
        auto* j        = static_cast<LogicalJoin*>(join_node.get());

        j->right = std::make_unique<LogicalFilter>(
            filter->condition,
            std::move(j->right));

        return join_node;
    }

    // ── Cannot push: column ambiguous or unknown ──────────────────────────────
    return filter_node;
}

// ── isPureAndTree ────────────────────────────────────────────────────────────
//
// Returns true when the entire PredicateTree consists of only leaf nodes or
// AND branches — with NO OR sub-tree anywhere in the tree.
//
// Safety rule: OR predicates span BOTH sides of a join logically
// (e.g. "a.x = 1 OR b.y = 2" must remain above the join because it cannot be
// decomposed into independent per-table filters).  Only pure AND trees can be
// safely split into individual conjuncts for independent push-down.

bool Optimizer::isPureAndTree(const PredicateTree* tree) noexcept {
    if (!tree) return true;                     // empty tree: trivially pure
    if (tree->is_leaf) return true;             // leaf: no logical operator
    if (tree->logical_op != "AND") return false; // OR or unknown: not pure AND
    return isPureAndTree(tree->left.get()) && isPureAndTree(tree->right.get());
}

// ── tryPushCompoundAndFilterThroughJoin ───────────────────────────────────────
//
// Pre-conditions:
//   filter_node->kind() == Filter
//   filter->compound_cond is a non-null, non-leaf, pure-AND PredicateTree
//   filter->child->kind() == Join
//
// Algorithm:
//   1. Collect all leaf BinaryConditions from the AND tree.
//   2. For each conjunct, use column_in_tables() to attribute it to the left
//      or right join operand (via the Catalog).  A conjunct is attributed to
//      the side whose schema contains the filter column.
//   3. Inject a new LogicalFilter wrapping the attributed child in-place.
//   4. Residual conjuncts (column unknown in either side, or present in both)
//      are re-stacked as simple LogicalFilter nodes above the join.
//   5. Return the join node (with pushed filters below) plus any residual
//      filters above it.
//
// Pointer-safety:
//   'join' is a raw pointer into the tree owned by filter_node->child.
//   We mutate join->left / join->right in-place, then steal the join via
//   std::move(filter->child).  The filter_node is left hollow and destructs
//   cleanly (both unique_ptrs are null after the move).

std::unique_ptr<LogicalNode>
Optimizer::tryPushCompoundAndFilterThroughJoin(
        std::unique_ptr<LogicalNode> filter_node) {

    auto* filter = static_cast<LogicalFilter*>(filter_node.get());
    auto* join   = static_cast<LogicalJoin*>(filter->child.get());

    // 1. Collect all independent leaf conjuncts
    std::vector<BinaryCondition> conjuncts;
    filter->compound_cond->collectLeaves(conjuncts);

    const auto left_tables  = collect_tables(*join->left);
    const auto right_tables = collect_tables(*join->right);

    std::vector<BinaryCondition> residual;

    for (const auto& cond : conjuncts) {
        const bool in_left  = column_in_tables(cond.column, left_tables);
        const bool in_right = column_in_tables(cond.column, right_tables);

        if (in_left && !in_right) {
            // 2a. Push conjunct under the join's left child
            join->left = std::make_unique<LogicalFilter>(
                cond, std::move(join->left));
        } else if (in_right && !in_left) {
            // 2b. Push conjunct under the join's right child
            join->right = std::make_unique<LogicalFilter>(
                cond, std::move(join->right));
        } else {
            // 2c. Ambiguous or unknown column: cannot push — keep above join
            residual.push_back(cond);
        }
    }

    // 3. Steal the join subtree from the (now hollow) filter_node
    auto join_node = std::move(filter->child);

    if (residual.empty()) {
        // Every conjunct was pushed — return the bare join
        return join_node;
    }

    // 4. Re-wrap residual conjuncts as stacked simple filters above the join.
    //    (Stacking is semantically equivalent to AND for selection predicates.)
    std::unique_ptr<LogicalNode> result = std::move(join_node);
    for (const auto& cond : residual)
        result = std::make_unique<LogicalFilter>(cond, std::move(result));
    return result;
}

// ── pushDownPredicates ────────────────────────────────────────────────────────
//
// Bottom-up tree rewrite.  Children are processed before parents so that
// newly pushed-down filters are themselves candidates for further pushdown
// in the same pass.

std::unique_ptr<LogicalNode>
Optimizer::pushDownPredicates(std::unique_ptr<LogicalNode> node) {
    if (!node) return node;

    switch (node->kind()) {

    // ── Filter: recurse into child, then attempt push ─────────────────────────
    case LogicalNodeKind::Filter: {
        auto* filter = static_cast<LogicalFilter*>(node.get());

        // Step 1 — process the sub-tree below the filter first (bottom-up)
        filter->child = pushDownPredicates(std::move(filter->child));

        // Step 1.5 — Index Scan Recognition
        // If this filter sits directly above a scan, check if we can use an index
        if (filter->child && filter->child->kind() == LogicalNodeKind::Scan) {
            auto* scan = static_cast<LogicalScan*>(filter->child.get());
            if (scan->metadata) {
                std::string col = filter->condition.column;
                auto dot_pos = col.find('.');
                std::string col_name = (dot_pos != std::string::npos) ? col.substr(dot_pos + 1) : col;

                if (scan->metadata->is_indexed(col_name)) {
                    scan->algorithm = ScanAlgorithm::IndexScan;
                } else if (filter->compound_cond) {
                    std::vector<BinaryCondition> leaves;
                    filter->compound_cond->collectLeaves(leaves);
                    for (const auto& leaf : leaves) {
                        std::string leaf_col = leaf.column;
                        auto leaf_dot = leaf_col.find('.');
                        std::string leaf_col_name = (leaf_dot != std::string::npos) ? leaf_col.substr(leaf_dot + 1) : leaf_col;
                        if (scan->metadata->is_indexed(leaf_col_name)) {
                            scan->algorithm = ScanAlgorithm::IndexScan;
                            break;
                        }
                    }
                }
            }
        }

        // Step 2 — only attempt push-down when the child is a Join
        if (!filter->child ||
            filter->child->kind() != LogicalNodeKind::Join)
            return node;  // cannot push; leave filter where it is

        // Step 3 — route by predicate type
        if (filter->compound_cond && !filter->compound_cond->is_leaf) {
            // Compound filter: decompose only for pure-AND trees.
            // OR sub-trees must remain above the join — they cannot be
            // attributed to a single side without losing semantics.
            if (isPureAndTree(filter->compound_cond.get()))
                return tryPushCompoundAndFilterThroughJoin(std::move(node));
            return node;  // OR/mixed: leave the filter where it is
        }

        // Simple single-condition filter: use the original push logic
        return tryPushFilterThroughJoin(std::move(node));
    }

    // ── Join: recurse into both children ─────────────────────────────────────
    case LogicalNodeKind::Join: {
        auto* join = static_cast<LogicalJoin*>(node.get());
        join->left  = pushDownPredicates(std::move(join->left));
        join->right = pushDownPredicates(std::move(join->right));
        return node;
    }

    // ── Project: recurse into child ──────────────────────────────────────────
    case LogicalNodeKind::Project: {
        auto* project = static_cast<LogicalProject*>(node.get());
        project->child = pushDownPredicates(std::move(project->child));
        return node;
    }

    // ── Aggregate: recurse into child (transparent to push-down) ─────────────
    case LogicalNodeKind::Aggregate: {
        auto* agg = static_cast<LogicalAggregate*>(node.get());
        agg->child = pushDownPredicates(std::move(agg->child));
        return node;
    }

    // ── Union: recurse into both children ─────────────────────────────────────
    case LogicalNodeKind::Union: {
        auto* u = static_cast<LogicalUnion*>(node.get());
        u->left = pushDownPredicates(std::move(u->left));
        u->right = pushDownPredicates(std::move(u->right));
        return node;
    }

    // ── Scan: leaf — nothing to do ────────────────────────────────────────────
    case LogicalNodeKind::Scan:
    default:
        return node;
    }
}

} // namespace qo (RBO functions above — CBO functions below)

namespace qo {

// ── Selectivity Helpers ───────────────────────────────────────────────────────

#include <cassert>

static double computeSelectivity(const BinaryCondition& cond, const Catalog& catalog, const std::unordered_set<std::string>& tables) {
    if (cond.empty()) return 1.0;
    
    double sel = 0.10; // Default
    if (cond.op == "=") {
        sel = 0.05; // Default 5% if distinct_count is unavailable
        for (const auto& tbl : tables) {
            const auto* meta = catalog.find_table(tbl);
            if (meta) {
                const auto* c_meta = meta->find_column(cond.column);
                if (c_meta && c_meta->distinct_count > 0) {
                    sel = 1.0 / static_cast<double>(c_meta->distinct_count);
                    break;
                }
            }
        }
    } else if (cond.op == ">" || cond.op == "<" || cond.op == ">=" || cond.op == "<=") {
        sel = 0.33; // Default 33% for range
        for (const auto& tbl : tables) {
            const auto* meta = catalog.find_table(tbl);
            if (meta) {
                const auto* c_meta = meta->find_column(cond.column);
                if (c_meta) {
                    double val = 0.0;
                    try { 
                        val = std::stod(cond.value); 
                        if (!c_meta->histogram.empty()) {
                            std::size_t total = meta->row_count;
                            if (total > 0) {
                                std::size_t match = 0;
                                for (const auto& bucket : c_meta->histogram) {
                                    if (cond.op == ">" || cond.op == ">=") {
                                        if (bucket.max_val >= val) match += bucket.count;
                                    } else if (cond.op == "<" || cond.op == "<=") {
                                        if (bucket.min_val <= val) match += bucket.count;
                                    }
                                }
                                sel = static_cast<double>(match) / static_cast<double>(total);
                            }
                        } else if (c_meta->min_val < c_meta->max_val) {
                            // Continuous range selectivity estimation using min/max
                            double range = c_meta->max_val - c_meta->min_val;
                            if (cond.op == ">" || cond.op == ">=") {
                                sel = (c_meta->max_val - val) / range;
                            } else if (cond.op == "<" || cond.op == "<=") {
                                sel = (val - c_meta->min_val) / range;
                            }
                            if (sel < 0.0) sel = 0.0;
                            if (sel > 1.0) sel = 1.0;
                        }
                    } catch(...) { /* fallback to 0.33 */ }
                    break;
                }
            }
        }
    }
    
    if (sel < 0.0001) sel = 0.0001; // Avoid 0 selectivity entirely
    if (sel > 1.0) sel = 1.0;
    assert(sel >= 0.0 && sel <= 1.0 && "Selectivity must be between 0 and 1");
    return sel;
}

static double computeSelectivity(const PredicateTree* tree, const Catalog& catalog, const std::unordered_set<std::string>& tables) {
    if (!tree) return 1.0;
    if (tree->is_leaf) return computeSelectivity(tree->condition, catalog, tables);
    if (tree->logical_op == "AND") {
        return computeSelectivity(tree->left.get(), catalog, tables) * computeSelectivity(tree->right.get(), catalog, tables);
    }
    if (tree->logical_op == "OR") {
        double sl = computeSelectivity(tree->left.get(), catalog, tables);
        double sr = computeSelectivity(tree->right.get(), catalog, tables);
        double sel = sl + sr - (sl * sr);
        if (sel > 1.0) sel = 1.0;
        return sel;
    }
    return 0.10;
}

// ── estimateStats ─────────────────────────────────────────────────────────────
//
// Recursively applies the cost model described in optimizer.h.
// All costs are in abstract "row-read" units so they compare sensibly across
// different node types.

Optimizer::PlanStats
Optimizer::estimateStats(const LogicalNode& node) const {
    switch (node.kind()) {

    case LogicalNodeKind::Scan: {
        const auto& scan = static_cast<const LogicalScan&>(node);
        double rows = 0.0;
        if (scan.metadata) {
            rows = static_cast<double>(scan.metadata->row_count);
        } else if (const auto* t = catalog_.find_table(scan.table_name)) {
            rows = static_cast<double>(t->row_count);
        }
        assert(rows >= 0.0 && "Estimated rows cannot be negative");
        
        // If an IndexScan is selected, the I/O cost drops drastically to roughly logarithmic scale.
        double scan_cost = (scan.algorithm == ScanAlgorithm::IndexScan) ? (std::log2(rows + 2) * 5.0) : rows;
        return {scan_cost, rows, scan_cost, 0.0, 0.0, 0.0};
    }

    case LogicalNodeKind::Filter: {
        const auto& filter = static_cast<const LogicalFilter&>(node);
        PlanStats child = filter.child
                            ? estimateStats(*filter.child)
                            : PlanStats{0.0, 0.0};
        
        std::unordered_set<std::string> tables = collect_tables(*filter.child);
        double sel = filter.compound_cond ? computeSelectivity(filter.compound_cond.get(), catalog_, tables) 
                                          : computeSelectivity(filter.condition, catalog_, tables);
        double out_rows = child.rows * sel;
        // Filter evaluation cost is proportional to the number of incoming rows
        double filter_eval_cost = child.rows * 0.2; 
        
        assert(out_rows >= 0.0 && out_rows <= child.rows && "Filter out_rows must be between 0 and incoming rows");
        PlanStats stats = {child.cost + filter_eval_cost, out_rows, child.cost_scan, child.cost_filter + filter_eval_cost, child.cost_join, child.cost_project};
        assert(stats.cost >= child.cost && "Filter cannot reduce total cumulative cost");
        return stats;
    }

    case LogicalNodeKind::Join: {
        const auto& join = static_cast<const LogicalJoin&>(node);
        PlanStats left  = join.left  ? estimateStats(*join.left)  : PlanStats{0.0,0.0};
        PlanStats right = join.right ? estimateStats(*join.right) : PlanStats{0.0,0.0};
        
        // Compute cardinality dynamically based on intermediate child sizes
        // If there's no condition, it's a Cartesian product (selectivity = 1.0).
        // Otherwise, we use 1% of Cartesian product (0.01 selectivity) as an approximation.
        double join_sel = join.condition.empty() ? 1.0 : 0.01;
        double join_rows = (left.rows * right.rows) * join_sel;
        if (join_rows < 1.0 && (left.rows > 0 && right.rows > 0)) join_rows = 1.0;
        
        PlanStats stats = {left.cost + right.cost + join_rows, join_rows, 
                           left.cost_scan + right.cost_scan, 
                           left.cost_filter + right.cost_filter, 
                           left.cost_join + right.cost_join + join_rows, 
                           left.cost_project + right.cost_project};
        assert(stats.rows >= 0.0 && "Estimated rows cannot be negative");
        assert(stats.cost >= 0.0 && "Estimated cost cannot be negative");
        return stats;
    }

    case LogicalNodeKind::Project: {
        const auto& project = static_cast<const LogicalProject&>(node);
        PlanStats child = project.child ? estimateStats(*project.child) : PlanStats{0.0,0.0,0.0,0.0,0.0,0.0};
        double proj_cost = child.rows * 0.5; // Arbitrary cost for projection
        child.cost += proj_cost;
        child.cost_project += proj_cost;
        return child;
    }

    case LogicalNodeKind::Aggregate: {
        const auto& agg = static_cast<const LogicalAggregate&>(node);
        PlanStats child = agg.child ? estimateStats(*agg.child) : PlanStats{0.0,0.0,0.0,0.0,0.0,0.0};
        
        double hash_cost = child.rows * 1.5;
        // Introduce a cost component for grouping/sorting (20% overhead)
        double agg_rows = child.rows * 0.20;
        if (agg_rows < 1.0 && child.rows > 0) agg_rows = 1.0;
        PlanStats stats = child;
        stats.cost += hash_cost + agg_rows;
        stats.rows = agg_rows;
        // Add aggregate overhead into projection/misc cost
        stats.cost_project += hash_cost + agg_rows;
        return stats;
    }

    case LogicalNodeKind::Union: {
        const auto& u = static_cast<const LogicalUnion&>(node);
        PlanStats left  = u.left  ? estimateStats(*u.left)  : PlanStats{0.0,0.0,0.0,0.0,0.0,0.0};
        PlanStats right = u.right ? estimateStats(*u.right) : PlanStats{0.0,0.0,0.0,0.0,0.0,0.0};
        PlanStats stats = {left.cost + right.cost, left.rows + right.rows,
                           left.cost_scan + right.cost_scan,
                           left.cost_filter + right.cost_filter,
                           left.cost_join + right.cost_join,
                           left.cost_project + right.cost_project};
        return stats;
    }

    }
    return {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
}

// ── collect_tables ────────────────────────────────────────────────────────────
//
// Recursively collects all table names referenced by a logical plan subtree.
// Used by dp_join_order to match join conditions to operand pairs.

static std::unordered_set<std::string>
collect_tables(const LogicalNode& node) {
    std::unordered_set<std::string> tables;
    switch (node.kind()) {
        case LogicalNodeKind::Scan: {
            const auto& scan = static_cast<const LogicalScan&>(node);
            tables.insert(scan.table_name);
            break;
        }
        case LogicalNodeKind::Filter: {
            const auto& filter = static_cast<const LogicalFilter&>(node);
            if (filter.child) {
                auto child_tables = collect_tables(*filter.child);
                tables.insert(child_tables.begin(), child_tables.end());
            }
            break;
        }
        case LogicalNodeKind::Join: {
            const auto& join = static_cast<const LogicalJoin&>(node);
            if (join.left) {
                auto lt = collect_tables(*join.left);
                tables.insert(lt.begin(), lt.end());
            }
            if (join.right) {
                auto rt = collect_tables(*join.right);
                tables.insert(rt.begin(), rt.end());
            }
            break;
        }
        case LogicalNodeKind::Project: {
            const auto& proj = static_cast<const LogicalProject&>(node);
            if (proj.child) {
                auto ct = collect_tables(*proj.child);
                tables.insert(ct.begin(), ct.end());
            }
            break;
        }
        default:
            break;
    }
    return tables;
}

// ── flatten_join ──────────────────────────────────────────────────────────────
//
// Ownership-safe flattening that preserves join conditions.
//
// When a LogicalJoin node is stripped, its BinaryCondition and JoinAlgorithm
// are recorded as a JoinEdge.  The edge stores the operand indices
// (left_operand_idx, right_operand_idx) so dp_join_order can look up the
// correct condition when pairing two operands in the rebuilt tree.
//
// For a left-deep tree A ⋈₁ B ⋈₂ C, the edges are:
//   { cond₁, algo₁, idx(A), idx(B) }
//   { cond₂, algo₂, idx(A⋈B), idx(C) }
// Because the left child of the outer join is itself a join, we record the
// right-operand index at the time each join is peeled.

void Optimizer::flatten_join(std::unique_ptr<LogicalNode> node,
                             std::vector<std::unique_ptr<LogicalNode>>& operands,
                             std::vector<JoinEdge>& edges) {
    if (!node) return;

    if (node->kind() != LogicalNodeKind::Join) {
        operands.push_back(std::move(node));   // leaf operand
        return;
    }

    // Release the raw pointer so we own it, then strip the join wrapper.
    auto* raw  = static_cast<LogicalJoin*>(node.release());
    auto  left = std::move(raw->left);    // move children before delete
    auto  right= std::move(raw->right);
    auto  cond = std::move(raw->condition);
    auto  algo = raw->algorithm;
    delete raw;                            // destructor sees null unique_ptrs → safe

    // Record which operand index the left subtree starts at.
    // After flattening the left subtree, the right operand will be at the
    // current end of the operands vector.
    int left_start = static_cast<int>(operands.size());
    flatten_join(std::move(left),  operands, edges);
    int right_start = static_cast<int>(operands.size());
    flatten_join(std::move(right), operands, edges);

    // Record the edge: the "right_operand" of this join is the first operand
    // contributed by the right subtree.  The "left_operand" is the last
    // operand contributed by the left subtree (for left-deep trees this is
    // sufficient; for bushy trees we use the start index).
    if (!cond.empty()) {
        edges.push_back({std::move(cond), algo, left_start, right_start});
    }
}

// ── dp_join_order ─────────────────────────────────────────────────────────────
//
// System R subset-DP for left-deep join ordering.
//
// Representation
//   - Operands  : the leaf subtrees extracted from the join tree
//   - Edges     : join conditions collected during flattening
//   - Bitmask   : uint32_t where bit i means "operand i is in this subset"
//   - DPEntry   : {cost, rows, join_order}   where join_order is the
//                  left-to-right sequence of operand indices
//
// Complexity:  O(3^n · n)  enumeration,  practical for n ≤ ~20.

std::unique_ptr<LogicalNode>
Optimizer::dp_join_order(std::unique_ptr<LogicalNode> join_root) {

    // ── 1. Flatten (preserving join edges) ────────────────────────────────────
    std::vector<std::unique_ptr<LogicalNode>> operands;
    std::vector<JoinEdge> edges;
    flatten_join(std::move(join_root), operands, edges);

    const int n = static_cast<int>(operands.size());

    // Nothing to reorder if fewer than 2 operands.
    if (n == 1) return std::move(operands[0]);
    if (n == 0) return nullptr;

    // ── 1b. Build table→operand-index mapping for condition lookup ────────────
    // For each operand, collect the set of table names it exposes.  When we
    // need to pair two operands with a join condition, we check which edge
    // references a table in each operand's set.
    std::vector<std::unordered_set<std::string>> operand_tables(n);
    for (int i = 0; i < n; ++i)
        operand_tables[i] = collect_tables(*operands[i]);

    std::vector<bool> edge_applied(edges.size(), false);

    // ── 2. Base stats per operand ─────────────────────────────────────────────
    std::vector<PlanStats> base(n);
    for (int i = 0; i < n; ++i)
        base[i] = estimateStats(*operands[i]);

    // ── 3. DP table ───────────────────────────────────────────────────────────
    struct DPEntry {
        double   cost;
        double   rows;
        uint32_t left_mask = 0;
        uint32_t right_mask = 0;
        int      leaf_idx = -1;
    };

    std::unordered_map<uint32_t, DPEntry> dp;
    dp.reserve(1u << n);

    // Singleton subsets — each operand on its own.
    for (int i = 0; i < n; ++i) {
        uint32_t mask = 1u << i;
        dp[mask] = {base[i].cost, base[i].rows, 0, 0, i};
    }

    // Build subsets of increasing size from 2 to n.
    for (unsigned size = 2u; size <= static_cast<unsigned>(n); ++size) {
        for (uint32_t S = 1u; S < (1u << n); ++S) {
            if (static_cast<unsigned>(std::popcount(S)) != size) continue;

            // Enumerate all valid splits S1, S2 where S1 ∪ S2 = S and S1 ∩ S2 = ∅
            for (uint32_t S1 = (S - 1) & S; S1 > 0; S1 = (S1 - 1) & S) {
                uint32_t S2 = S ^ S1;
                
                // To avoid duplicate pairs (since S1⋈S2 is symmetric to S2⋈S1 for cost purposes),
                // we can enforce S1 < S2.
                if (S1 >= S2) continue;

                auto it1 = dp.find(S1);
                auto it2 = dp.find(S2);
                if (it1 == dp.end() || it2 == dp.end()) continue;

                const DPEntry& left = it1->second;
                const DPEntry& right = it2->second;

                // Check if there is any applicable edge between S1 and S2
                bool has_edge = false;
                for (const auto& edge : edges) {
                    if (edge.condition.empty()) continue;
                    const std::string& col_left = edge.condition.column;
                    const std::string& col_right = edge.condition.value;

                    auto check_in_set = [&](const std::string& col, uint32_t mask) {
                        for (int i = 0; i < n; ++i) {
                            if ((mask & (1u << i))) {
                                for (const auto& tbl : operand_tables[i]) {
                                    if (col == tbl + ".id" || col.find(tbl + ".") == 0) return true;
                                    const auto* meta = catalog_.find_table(tbl);
                                    if (meta && meta->find_column(col)) return true;
                                }
                            }
                        }
                        return false;
                    };

                    bool l_in_L = check_in_set(col_left, S1);
                    bool r_in_R = check_in_set(col_right, S2);
                    bool l_in_R = check_in_set(col_left, S2);
                    bool r_in_L = check_in_set(col_right, S1);

                    if ((l_in_L && r_in_R) || (l_in_R && r_in_L)) {
                        has_edge = true;
                        break;
                    }
                }

                double join_sel = has_edge ? 0.01 : 1.0;
                double join_rows = (left.rows * right.rows) * join_sel;
                if (join_rows < 1.0 && (left.rows > 0 && right.rows > 0)) join_rows = 1.0;
                
                double nl_cost = left.cost + right.cost + (left.rows * right.rows);
                double hj_cost = left.cost + right.cost + (left.rows + right.rows);
                
                // Merge Join Cost
                double mj_sort_cost = (left.rows > 1 ? left.rows * std::log2(left.rows) : 0) + 
                                      (right.rows > 1 ? right.rows * std::log2(right.rows) : 0);
                double mj_cost = left.cost + right.cost + mj_sort_cost + (left.rows + right.rows);

                // Heavily penalize Cartesian products
                if (!has_edge) {
                    nl_cost *= 100.0;
                    hj_cost *= 100.0;
                    mj_cost *= 100.0;
                }

                double join_cost = std::min({nl_cost, hj_cost, mj_cost});

                auto cur = dp.find(S);
                if (cur == dp.end() || join_cost < cur->second.cost) {
                    dp[S] = {join_cost, join_rows, S1, S2, -1};
                }
            }
        }
    }

    // ── 4. Rebuild the optimal bushy tree ─────────────────────────────────────
    uint32_t full_mask = (1u << n) - 1;
    
    // We need a recursive helper to rebuild the tree
    std::function<std::unique_ptr<LogicalNode>(uint32_t)> rebuild = [&](uint32_t mask) -> std::unique_ptr<LogicalNode> {
        const DPEntry& entry = dp.at(mask);
        if (entry.leaf_idx != -1) {
            // Leaf node
            return std::move(operands[entry.leaf_idx]);
        }

        auto left_node = rebuild(entry.left_mask);
        auto right_node = rebuild(entry.right_mask);

        // Find applicable edges between the left mask and right mask
        std::vector<int> left_indices;
        for (int i = 0; i < n; ++i) {
            if ((entry.left_mask & (1u << i))) left_indices.push_back(i);
        }
        // In a bushy tree, the right side is also a set of tables, so we need to collect them
        std::vector<int> right_indices;
        for (int i = 0; i < n; ++i) {
            if ((entry.right_mask & (1u << i))) right_indices.push_back(i);
        }

        std::unordered_set<std::string> left_tbls;
        for (int idx : left_indices) {
            left_tbls.insert(operand_tables[idx].begin(), operand_tables[idx].end());
        }
        std::unordered_set<std::string> right_tbls;
        for (int idx : right_indices) {
            right_tbls.insert(operand_tables[idx].begin(), operand_tables[idx].end());
        }

        std::vector<int> applicable;
        for (size_t i = 0; i < edges.size(); ++i) {
            if (edge_applied[i] || edges[i].condition.empty()) continue;
            
            const std::string& col_left = edges[i].condition.column;
            const std::string& col_right = edges[i].condition.value;

            bool l_in_L = false, r_in_R = false;
            bool l_in_R = false, r_in_L = false;

            auto check_in_set = [&](const std::string& col, const std::unordered_set<std::string>& tbls) {
                for (const auto& tbl : tbls) {
                    const auto* meta = catalog_.find_table(tbl);
                    if (meta && meta->find_column(col)) return true;
                    if (col == tbl + ".id" || col.find(tbl + ".") == 0) return true;
                }
                return false;
            };

            l_in_L = check_in_set(col_left, left_tbls);
            r_in_R = check_in_set(col_right, right_tbls);
            l_in_R = check_in_set(col_left, right_tbls);
            r_in_L = check_in_set(col_right, left_tbls);

            if ((l_in_L && r_in_R) || (l_in_R && r_in_L)) {
                applicable.push_back(static_cast<int>(i));
            }
        }

        BinaryCondition join_cond;
        if (!applicable.empty()) {
            join_cond = edges[applicable[0]].condition;
            edge_applied[applicable[0]] = true;
        }

        PlanStats left_stats = estimateStats(*left_node);
        PlanStats right_stats = estimateStats(*right_node);
        
        double nl_cost = left_stats.cost + right_stats.cost + (left_stats.rows * right_stats.rows);
        double hj_cost = left_stats.cost + right_stats.cost + (left_stats.rows + right_stats.rows);
        
        double mj_sort_cost = (left_stats.rows > 1 ? left_stats.rows * std::log2(left_stats.rows) : 0) + 
                              (right_stats.rows > 1 ? right_stats.rows * std::log2(right_stats.rows) : 0);
        double mj_cost = left_stats.cost + right_stats.cost + mj_sort_cost + (left_stats.rows + right_stats.rows);

        auto new_join = std::make_unique<LogicalJoin>(std::move(left_node), std::move(right_node), std::move(join_cond));

        if (!new_join->condition.empty() && new_join->condition.op == "=") {
            if (mj_cost < hj_cost && mj_cost < nl_cost) {
                new_join->algorithm = JoinAlgorithm::MergeJoin;
            } else {
                new_join->algorithm = (hj_cost < nl_cost) ? JoinAlgorithm::HashJoin : JoinAlgorithm::NestedLoop;
            }
        } else {
            new_join->algorithm = JoinAlgorithm::NestedLoop;
        }

        std::unique_ptr<LogicalNode> plan = std::move(new_join);

        for (size_t i = 1; i < applicable.size(); ++i) {
            int edge_idx = applicable[i];
            plan = std::make_unique<LogicalFilter>(edges[edge_idx].condition, std::move(plan));
            edge_applied[edge_idx] = true;
        }
        return plan;
    };

    return rebuild(full_mask);
}

// ── optimizeJoinOrder ─────────────────────────────────────────────────────────
//
// Entry point: recurse through Project/Filter wrappers transparently; when a
// Join node is found, hand it to dp_join_order for full reordering.

std::unique_ptr<LogicalNode>
Optimizer::optimizeJoinOrder(std::unique_ptr<LogicalNode> plan) {
    if (!plan) return plan;

    switch (plan->kind()) {

    case LogicalNodeKind::Project: {
        auto* project  = static_cast<LogicalProject*>(plan.get());
        project->child = optimizeJoinOrder(std::move(project->child));
        return plan;
    }

    case LogicalNodeKind::Filter: {
        auto* filter  = static_cast<LogicalFilter*>(plan.get());
        filter->child = optimizeJoinOrder(std::move(filter->child));
        return plan;
    }

    // ── Aggregate: transparent — recurse into child, just like Project ────────
    case LogicalNodeKind::Aggregate: {
        auto* agg  = static_cast<LogicalAggregate*>(plan.get());
        agg->child = optimizeJoinOrder(std::move(agg->child));
        return plan;
    }

    case LogicalNodeKind::Join:
        // Root of the join sub-tree — hand off to the DP engine.
        return dp_join_order(std::move(plan));

    case LogicalNodeKind::Scan:
    default:
        return plan;
    }
}

} // namespace qo
