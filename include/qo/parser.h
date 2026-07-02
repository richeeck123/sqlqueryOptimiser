#pragma once

/**
 * Lightweight SQL parser — Phase 1 expansion.
 *
 * Grammar (extended):
 *   SELECT <col>[, <col>...] FROM <table>
 *     [JOIN <table> ON <col> <op> <col>] ...
 *     [WHERE <predicate_expr>]
 *     [GROUP BY <col>[, <col>...]]
 *
 *   predicate_expr ::= and_expr { OR and_expr }        (OR  — lowest precedence)
 *   and_expr       ::= primary { AND primary }         (AND — higher precedence)
 *   primary        ::= <col> <op> <value>              (leaf comparison)
 *
 * Backward-compatibility guarantee:
 *   QueryNode::where (single WhereNode) is populated whenever the WHERE clause
 *   resolves to a plain leaf predicate (no AND/OR).  All existing callers that
 *   access root->where->column / op / value are unaffected.
 *
 * Returns ownership of a QueryNode AST root.
 * Throws std::runtime_error on any syntax violation.
 */

#include "qo/ast.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace qo {

class Parser {
public:
    /// Parse a SQL-subset query string and return the root QueryNode.
    /// @throws std::runtime_error on syntax errors.
    [[nodiscard]] std::unique_ptr<QueryNode> parse(std::string_view sql);

private:
    using Tokens = std::vector<std::string>;

    /// Split SQL on whitespace; commas separate column names but are NOT
    /// emitted as tokens.  AND, OR, JOIN, ON, GROUP, BY tokenise naturally
    /// because they are whitespace-delimited — no special lexer state needed.
    static Tokens tokenize(std::string_view sql);

    /// Return an upper-case copy of s.
    static std::string to_upper(std::string s);

    // ── Recursive-descent predicate parsers ───────────────────────────────────

    /// Top-level expression: and_expr { OR and_expr }   (OR — lowest precedence)
    static std::unique_ptr<PredicateExpr>
    parsePredicateExpr(const Tokens& tokens, std::size_t& i, const std::unordered_map<std::string, std::string>& aliases);

    /// And-expression: primary { AND primary }
    static std::unique_ptr<PredicateExpr>
    parseAndExpr(const Tokens& tokens, std::size_t& i, const std::unordered_map<std::string, std::string>& aliases);

    /// Leaf predicate: <col> <op> <value>
    static std::unique_ptr<PredicateExpr>
    parsePrimaryPred(const Tokens& tokens, std::size_t& i, const std::unordered_map<std::string, std::string>& aliases);
    
    /// Resolves u.id to users.id using the alias map
    static std::string resolveColumn(const std::string& col, const std::unordered_map<std::string, std::string>& aliases);
};

} // namespace qo
