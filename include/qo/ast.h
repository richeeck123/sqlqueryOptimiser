#pragma once

/**
 * Abstract Syntax Tree for a SQL subset.
 *
 * Hierarchy (Phase 1 — extended):
 *   ASTNode  (abstract base)
 *   ├── SelectNode   — list of projected column names
 *   ├── TableNode    — primary source table name (FROM clause)
 *   ├── WhereNode    — single binary predicate (PRESERVED for backward compat)
 *   ├── GroupByNode  — GROUP BY column list
 *   └── QueryNode    — root; owns all of the above plus join/predicate tree
 *
 * New types (Phase 1):
 *   JoinClause     — one explicit JOIN <table> ON <col> <op> <col> entry
 *   PredicateExpr  — recursive AND/OR binary expression tree for WHERE clauses
 *
 * Backward-compatibility guarantee
 * ─────────────────────────────────
 *   QueryNode::where (unique_ptr<WhereNode>) is RETAINED.  The parser fills it
 *   whenever the WHERE clause is a simple (non-compound) comparison, so all
 *   existing code that accesses root->where->column / op / value continues to
 *   compile and run without modification.
 *
 * All nodes are non-copyable and heap-allocated via std::unique_ptr.
 */

#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace qo {

// ── Node kind tag ──────────────────────────────────────────────────────────────
enum class NodeKind { Query, Select, Table, Where, GroupBy, Expr };

// ── Base ───────────────────────────────────────────────────────────────────────
class ASTNode {
public:
    ASTNode() = default;
    ASTNode(const ASTNode&)            = delete;
    ASTNode& operator=(const ASTNode&) = delete;
    virtual ~ASTNode()                 = default;

    [[nodiscard]] virtual NodeKind kind() const noexcept = 0;

    /// Pretty-print the node tree to an output stream.
    virtual void dump(std::ostream& os, int indent = 0) const = 0;

protected:
    static void pad(std::ostream& os, int indent) {
        for (int i = 0; i < indent; ++i) os << "  ";
    }
};

// ── SelectNode ─────────────────────────────────────────────────────────────────
/// Represents the SELECT clause: an ordered list of column names.
class SelectNode final : public ASTNode {
public:
    std::vector<std::string> columns;

    explicit SelectNode(std::vector<std::string> cols)
        : columns(std::move(cols)) {}

    [[nodiscard]] NodeKind kind() const noexcept override {
        return NodeKind::Select;
    }

    void dump(std::ostream& os, int indent = 0) const override {
        pad(os, indent);
        os << "SelectNode [";
        for (std::size_t i = 0; i < columns.size(); ++i) {
            os << " " << columns[i];
            if (i + 1 < columns.size()) os << ",";
        }
        os << " ]\n";
    }
};

// ── TableNode ──────────────────────────────────────────────────────────────────
/// Represents the FROM clause: a single primary source table name.
class TableNode final : public ASTNode {
public:
    std::string name;

    explicit TableNode(std::string n) : name(std::move(n)) {}

    [[nodiscard]] NodeKind kind() const noexcept override {
        return NodeKind::Table;
    }

    void dump(std::ostream& os, int indent = 0) const override {
        pad(os, indent);
        os << "TableNode [ " << name << " ]\n";
    }
};

// ── WhereNode ─────────────────────────────────────────────────────────────────
/// Single binary predicate:  <column> <op> <value>
/// e.g.  age > 21
///
/// PRESERVED FOR BACKWARD COMPATIBILITY.  The parser fills this field only
/// when the WHERE clause is a plain leaf comparison (no AND/OR).  New code
/// should prefer QueryNode::where_expr (the full predicate tree).
class WhereNode final : public ASTNode {
public:
    std::string column;   ///< Left-hand side identifier
    std::string op;       ///< Comparison operator: >, <, =, >=, <=, !=
    std::string value;    ///< Right-hand side literal

    WhereNode(std::string col, std::string op_, std::string val)
        : column(std::move(col)), op(std::move(op_)), value(std::move(val)) {}

    [[nodiscard]] NodeKind kind() const noexcept override {
        return NodeKind::Where;
    }

    void dump(std::ostream& os, int indent = 0) const override {
        pad(os, indent);
        os << "WhereNode [ " << column << " " << op << " " << value << " ]\n";
    }
};

// ── JoinClause ────────────────────────────────────────────────────────────────
/// One explicit  JOIN <table> ON <on_left> <on_op> <on_right>  entry.
/// Both on_left and on_right are column references (may include table
/// qualifiers, e.g. "orders.user_id").  This is a plain struct — not a node —
/// because it is stored inline in QueryNode::joins.
struct JoinClause {
    std::string table_name;  ///< Table being joined
    std::string on_left;     ///< LHS column of the ON predicate
    std::string on_op;       ///< Comparison operator (typically "=")
    std::string on_right;    ///< RHS column of the ON predicate
};

// ── PredicateExpr ─────────────────────────────────────────────────────────────
/// Recursive binary expression tree for WHERE clauses.
///
/// Leaf node  (is_leaf == true)  : single comparison (column op value).
/// Branch node (is_leaf == false): logical AND / OR with left + right children.
///
/// Examples:
///   age > 21                 → Leaf("age",  ">", "21")
///   age > 21 AND total > 500 → Branch("AND", Leaf(...), Leaf(...))
///   a=1 OR (b=2 AND c=3)     → Branch("OR",  Leaf(...), Branch(...))
class PredicateExpr {
public:
    bool is_leaf;

    // ── Leaf fields (valid when is_leaf == true) ──────────────────────────────
    std::string column;  ///< LHS identifier
    std::string op;      ///< Comparison operator
    std::string value;   ///< RHS literal or column reference

    // ── Branch fields (valid when is_leaf == false) ───────────────────────────
    std::string                    logical_op;  ///< "AND" or "OR"
    std::unique_ptr<PredicateExpr> left;
    std::unique_ptr<PredicateExpr> right;

    // Non-copyable
    PredicateExpr(const PredicateExpr&)            = delete;
    PredicateExpr& operator=(const PredicateExpr&) = delete;

    /// Leaf constructor:  col op val
    PredicateExpr(std::string col, std::string op_, std::string val)
        : is_leaf(true),
          column(std::move(col)), op(std::move(op_)), value(std::move(val)) {}

    /// Branch constructor:  lop ( left, right )
    PredicateExpr(std::string lop,
                  std::unique_ptr<PredicateExpr> l,
                  std::unique_ptr<PredicateExpr> r)
        : is_leaf(false),
          logical_op(std::move(lop)),
          left(std::move(l)), right(std::move(r)) {}

    void dump(std::ostream& os, int indent = 0) const {
        for (int k = 0; k < indent; ++k) os << "  ";
        if (is_leaf) {
            os << "Pred [ " << column << " " << op << " " << value << " ]\n";
        } else {
            os << logical_op << "\n";
            if (left)  left->dump(os, indent + 1);
            if (right) right->dump(os, indent + 1);
        }
    }
};

// ── GroupByNode ───────────────────────────────────────────────────────────────
/// Represents a GROUP BY clause: an ordered list of grouping column names.
class GroupByNode final : public ASTNode {
public:
    std::vector<std::string> columns;

    explicit GroupByNode(std::vector<std::string> cols)
        : columns(std::move(cols)) {}

    [[nodiscard]] NodeKind kind() const noexcept override {
        return NodeKind::GroupBy;
    }

    void dump(std::ostream& os, int indent = 0) const override {
        pad(os, indent);
        os << "GroupByNode [";
        for (std::size_t i = 0; i < columns.size(); ++i) {
            os << " " << columns[i];
            if (i + 1 < columns.size()) os << ",";
        }
        os << " ]\n";
    }
};

// ── QueryNode (root) ───────────────────────────────────────────────────────────
/// Root of a parsed SELECT query.  Owns all child nodes via unique_ptr.
///
/// Backward-compatibility contract:
///   'where' (single WhereNode*) is populated whenever the WHERE clause is a
///   plain  <col> <op> <val>  predicate with no AND/OR, so all pre-Phase-1
///   callers of root->where->column / op / value compile and pass unmodified.
class QueryNode final : public ASTNode {
public:
    // ── Original fields ───────────────────────────────────────────────────────
    std::unique_ptr<SelectNode> select;
    std::unique_ptr<TableNode>  table;
    std::unique_ptr<WhereNode>  where;   ///< nullptr when WHERE is absent or compound

    // ── Phase 1 additions ─────────────────────────────────────────────────────
    std::vector<JoinClause>          joins;       ///< Explicit JOIN clauses (may be empty)
    std::unique_ptr<PredicateExpr>   where_expr;  ///< Full predicate tree (nullable)
    std::unique_ptr<GroupByNode>     group_by;    ///< GROUP BY clause (nullable)

    [[nodiscard]] NodeKind kind() const noexcept override {
        return NodeKind::Query;
    }

    void dump(std::ostream& os, int indent = 0) const override {
        pad(os, indent);
        os << "QueryNode\n";
        if (select) select->dump(os, indent + 1);
        if (table)  table->dump(os, indent + 1);
        for (const auto& jc : joins) {
            pad(os, indent + 1);
            os << "JoinClause [ " << jc.table_name
               << " ON " << jc.on_left << " " << jc.on_op << " " << jc.on_right
               << " ]\n";
        }
        // For simple (leaf) predicates: dump the WhereNode to preserve the
        // existing dump format "WhereNode [ col op val ]" that tests assert on.
        // For compound AND/OR trees: show the structured WhereExpr: tree.
        if (where_expr && !where_expr->is_leaf) {
            pad(os, indent + 1);
            os << "WhereExpr:\n";
            where_expr->dump(os, indent + 2);
        } else if (where) {
            where->dump(os, indent + 1);
        }
        if (group_by) group_by->dump(os, indent + 1);
    }
};

} // namespace qo
