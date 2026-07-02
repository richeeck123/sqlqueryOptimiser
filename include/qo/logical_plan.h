#pragma once

/**
 * Logical Plan IR — relational algebra tree.
 *
 * Node hierarchy (Phase 2 — extended)
 * ──────────────────────────────────────
 *   LogicalNode   (abstract base)
 *   ├── LogicalScan      leaf   — full table scan, carries TableMetadata*
 *   ├── LogicalFilter    unary  — selection predicate over a child subtree
 *   ├── LogicalProject   unary  — column projection over a child subtree
 *   ├── LogicalJoin      binary — Cartesian / equi-join of two subtrees
 *   └── LogicalAggregate unary  — GROUP BY grouping over a child subtree  [NEW]
 *
 * Ownership model
 * ───────────────
 *   Each node owns its child(ren) via std::unique_ptr, forming an
 *   exclusively-owned, acyclic tree.  Moving a subtree pointer transfers
 *   ownership without copying.
 *
 * BinaryCondition
 * ───────────────
 *   Shared predicate type used by LogicalFilter and LogicalJoin.
 *   PRESERVED UNCHANGED for backward compatibility.
 *
 * PredicateTree  [NEW — Phase 2]
 * ──────────────────────────────
 *   Recursive AND/OR predicate tree stored alongside LogicalFilter's
 *   existing 'condition' field.  Only populated for compound WHERE clauses.
 *   Leaf nodes wrap a BinaryCondition; branch nodes hold logical_op + children.
 *
 * Backward-compatibility guarantee
 * ─────────────────────────────────
 *   LogicalFilter::condition (single BinaryCondition) is RETAINED.
 *   The existing two-argument constructor  LogicalFilter(cond, child)  is
 *   preserved.  compound_cond is nullptr by default.  All 11 existing
 *   optimizer tests compile and pass without modification.
 */

#include "qo/catalog.h"
#include <nlohmann/json.hpp>

#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace qo {

// ── Shared predicate type ─────────────────────────────────────────────────────

/// A simple binary predicate:  <column> <op> <value>
/// e.g.  age > 21,  status = active,  total >= 100
/// PRESERVED UNCHANGED (existing optimizer tests depend on this layout).
struct BinaryCondition {
    std::string column;  ///< LHS column identifier
    std::string op;      ///< Comparison operator: >, <, =, >=, <=, !=
    std::string value;   ///< RHS literal (untyped string)

    [[nodiscard]] bool empty() const noexcept { return column.empty(); }
};

// ── PredicateTree  [Phase 2 — NEW] ────────────────────────────────────────────

/// Recursive AND/OR predicate tree for compound WHERE clauses in LogicalFilter.
///
/// Leaf node  (is_leaf == true)  : wraps a single BinaryCondition.
/// Branch node (is_leaf == false): holds logical_op ("AND"/"OR") and children.
///
/// Phase 3 use: pushDownPredicates() will walk the tree, collect independent
/// conjunctive leaves (collectLeaves()), and push each one independently.
struct PredicateTree {
    bool is_leaf;

    // ── Leaf fields ───────────────────────────────────────────────────────────
    BinaryCondition condition;   ///< Single comparison (valid when is_leaf)

    // ── Branch fields ─────────────────────────────────────────────────────────
    std::string                   logical_op;  ///< "AND" or "OR"
    std::unique_ptr<PredicateTree> left;
    std::unique_ptr<PredicateTree> right;

    // Non-copyable
    PredicateTree(const PredicateTree&)            = delete;
    PredicateTree& operator=(const PredicateTree&) = delete;

    /// Leaf constructor
    explicit PredicateTree(BinaryCondition cond)
        : is_leaf(true), condition(std::move(cond)) {}

    /// Branch constructor
    PredicateTree(std::string lop,
                  std::unique_ptr<PredicateTree> l,
                  std::unique_ptr<PredicateTree> r)
        : is_leaf(false),
          logical_op(std::move(lop)),
          left(std::move(l)), right(std::move(r)) {}

    /// Collect every leaf BinaryCondition into 'out' (depth-first left-to-right).
    /// Used by Phase 3 optimizer to decompose AND chains for push-down.
    void collectLeaves(std::vector<BinaryCondition>& out) const {
        if (is_leaf) {
            out.push_back(condition);
        } else {
            if (left)  left->collectLeaves(out);
            if (right) right->collectLeaves(out);
        }
    }

    void dump(std::ostream& os, int indent = 0) const {
        for (int k = 0; k < indent; ++k) os << "  ";
        if (is_leaf) {
            os << "Pred [ " << condition.column << " " << condition.op
               << " " << condition.value << " ]\n";
        } else {
            os << logical_op << "\n";
            if (left)  left->dump(os, indent + 1);
            if (right) right->dump(os, indent + 1);
        }
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        if (is_leaf) {
            j["type"] = "Condition";
            j["condition"] = condition.column + " " + condition.op + " " + condition.value;
        } else {
            j["type"] = logical_op;
            j["children"] = nlohmann::json::array();
            if (left) j["children"].push_back(left->toJson());
            if (right) j["children"].push_back(right->toJson());
        }
        return j;
    }

    std::unique_ptr<PredicateTree> clone() const {
        if (is_leaf) {
            return std::make_unique<PredicateTree>(condition);
        } else {
            return std::make_unique<PredicateTree>(logical_op, 
                left ? left->clone() : nullptr, 
                right ? right->clone() : nullptr);
        }
    }
};

// ── Node kind tag ─────────────────────────────────────────────────────────────

enum class LogicalNodeKind { Scan, Filter, Project, Join, Aggregate, Union };

// ── Abstract base ─────────────────────────────────────────────────────────────

class LogicalNode {
public:
    LogicalNode() = default;
    LogicalNode(const LogicalNode&)            = delete;
    LogicalNode& operator=(const LogicalNode&) = delete;
    virtual ~LogicalNode()                     = default;

    [[nodiscard]] virtual LogicalNodeKind kind() const noexcept = 0;
    virtual void dump(std::ostream& os, int indent = 0) const   = 0;
    virtual nlohmann::json toJson() const = 0;
    virtual std::unique_ptr<LogicalNode> clone() const = 0;

protected:
    static void pad(std::ostream& os, int indent) {
        for (int i = 0; i < indent; ++i) os << "  ";
    }
};

enum class ScanAlgorithm {
    SequentialScan,
    IndexScan
};

// ── LogicalScan ───────────────────────────────────────────────────────────────

/// Leaf operator: a full sequential scan or index scan of a single table.
/// 'metadata' may be nullptr when the node is constructed without catalog
/// resolution (e.g., directly in tests).
class LogicalScan final : public LogicalNode {
public:
    std::string           table_name;
    const TableMetadata*  metadata;   ///< nullable — set by LogicalPlanner
    ScanAlgorithm         algorithm = ScanAlgorithm::SequentialScan;

    explicit LogicalScan(std::string name,
                         const TableMetadata* meta = nullptr)
        : table_name(std::move(name)), metadata(meta) {}

    [[nodiscard]] LogicalNodeKind kind() const noexcept override {
        return LogicalNodeKind::Scan;
    }

    void dump(std::ostream& os, int indent = 0) const override {
        pad(os, indent);
        if (algorithm == ScanAlgorithm::IndexScan) {
            os << "IndexScan [ " << table_name;
        } else {
            os << "LogicalScan [ " << table_name;
        }
        if (metadata)
            os << "  rows=" << metadata->row_count;
        os << " ]\n";
    }

    nlohmann::json toJson() const override {
        nlohmann::json j;
        j["type"] = "Scan";
        j["algorithm"] = (algorithm == ScanAlgorithm::IndexScan) ? "IndexScan" : "SequentialScan";
        j["table"] = table_name;
        if (metadata) {
            j["rows"] = metadata->row_count;
        }
        return j;
    }

    std::unique_ptr<LogicalNode> clone() const override {
        auto c = std::make_unique<LogicalScan>(table_name, metadata);
        c->algorithm = algorithm;
        return c;
    }
};

// ── LogicalFilter ─────────────────────────────────────────────────────────────

/// Unary operator: applies a filter predicate to its child subtree.
/// This is the relational-algebra Selection operator (σ).
///
/// Backward-compatibility layout:
///   'condition'     — PRESERVED single BinaryCondition (authoritative for
///                     simple predicates; always set even for compound ones,
///                     where it holds the first conjunct as a representative).
///   'compound_cond' — NEW nullable PredicateTree; set only for AND/OR WHERE
///                     clauses.  nullptr for all existing single-predicate nodes.
///
/// The two-argument constructor LogicalFilter(cond, child) leaves
/// compound_cond == nullptr, ensuring all 11 existing optimizer tests compile
/// and pass with zero changes.
class LogicalFilter final : public LogicalNode {
public:
    BinaryCondition              condition;      ///< Simple predicate (always valid)
    std::unique_ptr<LogicalNode> child;
    std::unique_ptr<PredicateTree> compound_cond;  ///< Compound AND/OR tree (nullable)

    /// Original two-argument constructor — backward compatible.
    /// compound_cond is left nullptr (default-initialized unique_ptr).
    LogicalFilter(BinaryCondition cond, std::unique_ptr<LogicalNode> c)
        : condition(std::move(cond)), child(std::move(c)) {}

    /// Extended three-argument constructor for compound WHERE predicates.
    LogicalFilter(BinaryCondition rep_cond,
                  std::unique_ptr<LogicalNode> c,
                  std::unique_ptr<PredicateTree> compound)
        : condition(std::move(rep_cond)),
          child(std::move(c)),
          compound_cond(std::move(compound)) {}

    [[nodiscard]] LogicalNodeKind kind() const noexcept override {
        return LogicalNodeKind::Filter;
    }

    void dump(std::ostream& os, int indent = 0) const override {
        pad(os, indent);
        if (compound_cond) {
            os << "LogicalFilter [compound]:\n";
            compound_cond->dump(os, indent + 1);
        } else {
            os << "LogicalFilter [ "
               << condition.column << " " << condition.op << " " << condition.value
               << " ]\n";
        }
        if (child) child->dump(os, indent + 1);
    }

    nlohmann::json toJson() const override {
        nlohmann::json j;
        j["type"] = "Filter";
        if (compound_cond) {
            j["condition"] = compound_cond->toJson();
        } else {
            j["condition"] = condition.column + " " + condition.op + " " + condition.value;
        }
        j["children"] = nlohmann::json::array();
        if (child) j["children"].push_back(child->toJson());
        return j;
    }

    std::unique_ptr<LogicalNode> clone() const override {
        std::unique_ptr<PredicateTree> comp_cond = nullptr;
        if (compound_cond) {
            comp_cond = compound_cond->clone();
        }
        return std::make_unique<LogicalFilter>(condition, child->clone(), std::move(comp_cond));
    }
};

// ── LogicalProject ────────────────────────────────────────────────────────────

/// Unary operator: projects a fixed list of columns from its child subtree.
/// This is the relational-algebra Projection operator (π).
class LogicalProject final : public LogicalNode {
public:
    std::vector<std::string>     columns;
    std::unique_ptr<LogicalNode> child;

    LogicalProject(std::vector<std::string> cols,
                   std::unique_ptr<LogicalNode> c)
        : columns(std::move(cols)), child(std::move(c)) {}

    [[nodiscard]] LogicalNodeKind kind() const noexcept override {
        return LogicalNodeKind::Project;
    }

    void dump(std::ostream& os, int indent = 0) const override {
        pad(os, indent);
        os << "LogicalProject [";
        for (std::size_t i = 0; i < columns.size(); ++i) {
            os << " " << columns[i];
            if (i + 1 < columns.size()) os << ",";
        }
        os << " ]\n";
        if (child) child->dump(os, indent + 1);
    }

    nlohmann::json toJson() const override {
        nlohmann::json j;
        j["type"] = "Project";
        j["columns"] = columns;
        j["children"] = nlohmann::json::array();
        if (child) j["children"].push_back(child->toJson());
        return j;
    }

    std::unique_ptr<LogicalNode> clone() const override {
        return std::make_unique<LogicalProject>(columns, child->clone());
    }
};

enum class JoinAlgorithm {
    NestedLoop,
    HashJoin,
    MergeJoin
};

// ── LogicalJoin ───────────────────────────────────────────────────────────────

/// Binary operator: joins its left and right child subtrees.
/// 'condition' is the join predicate (may be empty for a cross join).
/// UNCHANGED — all existing join tests continue to work.
class LogicalJoin final : public LogicalNode {
public:
    std::unique_ptr<LogicalNode> left;
    std::unique_ptr<LogicalNode> right;
    BinaryCondition              condition;  ///< join predicate (may be empty)
    JoinAlgorithm                algorithm = JoinAlgorithm::NestedLoop; ///< the chosen algorithm

    LogicalJoin(std::unique_ptr<LogicalNode> l,
                std::unique_ptr<LogicalNode> r,
                BinaryCondition cond = {})
        : left(std::move(l)), right(std::move(r)), condition(std::move(cond)) {}

    [[nodiscard]] LogicalNodeKind kind() const noexcept override {
        return LogicalNodeKind::Join;
    }

    void dump(std::ostream& os, int indent = 0) const override {
        pad(os, indent);
        // Show the physical operator name instead of generic "LogicalJoin"
        if (algorithm == JoinAlgorithm::HashJoin) os << "HashJoin";
        else if (algorithm == JoinAlgorithm::MergeJoin) os << "MergeJoin";
        else os << "NestedLoopJoin";

        if (!condition.empty())
            os << " [ " << condition.column << " "
               << condition.op << " " << condition.value << " ]";
        os << "\n";
        if (left)  left->dump(os, indent + 1);
        if (right) right->dump(os, indent + 1);
    }

    nlohmann::json toJson() const override {
        nlohmann::json j;
        j["type"] = "Join";
        if (algorithm == JoinAlgorithm::HashJoin) j["algorithm"] = "HashJoin";
        else if (algorithm == JoinAlgorithm::MergeJoin) j["algorithm"] = "MergeJoin";
        else j["algorithm"] = "NestedLoop";
        
        if (!condition.empty()) {
            j["condition"] = condition.column + " " + condition.op + " " + condition.value;
        }
        j["children"] = nlohmann::json::array();
        if (left) j["children"].push_back(left->toJson());
        if (right) j["children"].push_back(right->toJson());
        return j;
    }

    std::unique_ptr<LogicalNode> clone() const override {
        auto c = std::make_unique<LogicalJoin>(left->clone(), right->clone(), condition);
        c->algorithm = algorithm;
        return c;
    }
};

// ── LogicalAggregate  [Phase 2 — NEW] ────────────────────────────────────────

/// Unary operator: groups its child's output by the specified columns.
/// This is the relational-algebra Grouping/Aggregation operator (γ).
///
/// Phase 4 will compile this to a HashAggregateExecutor that inherits from
/// AbstractExecutor and uses an in-memory map during next() calls.
class LogicalAggregate final : public LogicalNode {
public:
    std::vector<std::string>     group_columns;  ///< GROUP BY column names
    std::unique_ptr<LogicalNode> child;

    LogicalAggregate(std::vector<std::string> cols,
                     std::unique_ptr<LogicalNode> c)
        : group_columns(std::move(cols)), child(std::move(c)) {}

    [[nodiscard]] LogicalNodeKind kind() const noexcept override {
        return LogicalNodeKind::Aggregate;
    }

    void dump(std::ostream& os, int indent = 0) const override {
        pad(os, indent);
        os << "LogicalAggregate [ GROUP BY";
        for (const auto& col : group_columns)
            os << " " << col;
        os << " ]\n";
        if (child) child->dump(os, indent + 1);
    }

    nlohmann::json toJson() const override {
        nlohmann::json j;
        j["type"] = "Aggregate";
        j["group_columns"] = group_columns;
        j["children"] = nlohmann::json::array();
        if (child) j["children"].push_back(child->toJson());
        return j;
    }

    std::unique_ptr<LogicalNode> clone() const override {
        return std::make_unique<LogicalAggregate>(group_columns, child->clone());
    }
};

// ── LogicalUnion  [Phase 5 — NEW] ──────────────────────────────────────────

/// Binary operator: computes the union of two child subtrees.
class LogicalUnion final : public LogicalNode {
public:
    std::unique_ptr<LogicalNode> left;
    std::unique_ptr<LogicalNode> right;

    LogicalUnion(std::unique_ptr<LogicalNode> l, std::unique_ptr<LogicalNode> r)
        : left(std::move(l)), right(std::move(r)) {}

    [[nodiscard]] LogicalNodeKind kind() const noexcept override {
        return LogicalNodeKind::Union;
    }

    void dump(std::ostream& os, int indent = 0) const override {
        pad(os, indent);
        os << "LogicalUnion\n";
        if (left)  left->dump(os, indent + 1);
        if (right) right->dump(os, indent + 1);
    }

    nlohmann::json toJson() const override {
        nlohmann::json j;
        j["type"] = "Union";
        j["children"] = nlohmann::json::array();
        if (left) j["children"].push_back(left->toJson());
        if (right) j["children"].push_back(right->toJson());
        return j;
    }

    std::unique_ptr<LogicalNode> clone() const override {
        return std::make_unique<LogicalUnion>(left->clone(), right->clone());
    }
};

} // namespace qo
