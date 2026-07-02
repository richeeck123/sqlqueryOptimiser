#include "qo/parser.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace qo {

// ── Private helpers ────────────────────────────────────────────────────────────

std::string Parser::to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}



/**
 * Tokenize SQL text:
 *   - Whitespace separates tokens.
 *   - Commas separate column names but are NOT emitted as tokens;
 *     e.g. "name, age" → ["name", "age"].
 *
 * AND, OR, JOIN, ON, GROUP, BY are already whitespace-delimited, so they
 * tokenise correctly without any special lexer state.
 */
Parser::Tokens Parser::tokenize(std::string_view sql) {
    Tokens tokens;
    std::string current;

    for (char c : sql) {
        if (c == ',') {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) tokens.push_back(std::move(current));
    return tokens;
}

// ── Keyword guard ─────────────────────────────────────────────────────────────
//
// Returns true when the upper-cased token is a SQL keyword that cannot be a
// column or value identifier.  parsePrimaryPred checks this to avoid silently
// consuming a keyword as part of a malformed predicate and to provide a clear
// error message instead.

static bool isReservedKeyword(const std::string& upper_tok) {
    return upper_tok == "AND"    || upper_tok == "OR"     ||
           upper_tok == "GROUP"  || upper_tok == "HAVING" ||
           upper_tok == "ORDER"  || upper_tok == "LIMIT"  ||
           upper_tok == "JOIN"   || upper_tok == "ON"     ||
           upper_tok == "WHERE"  || upper_tok == "FROM"   ||
           upper_tok == "SELECT" || upper_tok == "BY"     ||
           upper_tok == "LEFT"   || upper_tok == "RIGHT"  ||
           upper_tok == "INNER"  || upper_tok == "OUTER"  ||
           upper_tok == "CROSS"  || upper_tok == "FULL";
}

// ── Recursive-descent predicate parsers ───────────────────────────────────────

/**
 * parsePrimaryPred — leaf  <col> <op> <value>
 *
 * Consumes exactly three tokens and returns a leaf PredicateExpr.
 * Throws if:
 *   - fewer than three tokens remain starting at i, or
 *   - the first token is a reserved SQL keyword.
 */
std::unique_ptr<PredicateExpr>
Parser::parsePrimaryPred(const Tokens& tokens, std::size_t& i, const std::unordered_map<std::string, std::string>& aliases) {
    const std::size_t n = tokens.size();

    // Guard: need tokens[i], [i+1], [i+2]  →  i+2 must be a valid index.
    if (i + 2 >= n)
        throw std::runtime_error(
            "Parser: WHERE clause requires <column> <op> <value>");

    const std::string upper_col = to_upper(tokens[i]);
    if (isReservedKeyword(upper_col))
        throw std::runtime_error(
            "Parser: unexpected keyword '" + tokens[i] +
            "' where a column name was expected in WHERE predicate");

    std::string col = resolveColumn(tokens[i++], aliases);
    std::string op  = tokens[i++];
    std::string val = resolveColumn(tokens[i++], aliases);

    return std::make_unique<PredicateExpr>(
        std::move(col), std::move(op), std::move(val));
}

/**
 * parseAndExpr — primary { AND primary }
 *
 * AND has higher precedence than OR: it binds more tightly, so we handle
 * it first (inner loop) before the outer OR loop in parsePredicateExpr.
 */
std::unique_ptr<PredicateExpr>
Parser::parseAndExpr(const Tokens& tokens, std::size_t& i, const std::unordered_map<std::string, std::string>& aliases) {
    auto left = parsePrimaryPred(tokens, i, aliases);

    while (i < tokens.size() && to_upper(tokens[i]) == "AND") {
        ++i;  // consume AND
        auto right = parsePrimaryPred(tokens, i, aliases);
        left = std::make_unique<PredicateExpr>(
            "AND", std::move(left), std::move(right));
    }
    return left;
}

/**
 * parsePredicateExpr — and_expr { OR and_expr }
 *
 * OR has the lowest precedence.  Each operand is itself an and_expr so that
 * "a AND b OR c AND d" parses as "(a AND b) OR (c AND d)".
 */
std::unique_ptr<PredicateExpr>
Parser::parsePredicateExpr(const Tokens& tokens, std::size_t& i, const std::unordered_map<std::string, std::string>& aliases) {
    auto left = parseAndExpr(tokens, i, aliases);

    while (i < tokens.size() && to_upper(tokens[i]) == "OR") {
        ++i;  // consume OR
        auto right = parseAndExpr(tokens, i, aliases);
        left = std::make_unique<PredicateExpr>(
            "OR", std::move(left), std::move(right));
    }
    return left;
}

std::string Parser::resolveColumn(const std::string& col, const std::unordered_map<std::string, std::string>& aliases) {
    size_t dot_pos = col.find('.');
    if (dot_pos != std::string::npos) {
        std::string prefix = col.substr(0, dot_pos);
        auto it = aliases.find(prefix);
        if (it != aliases.end()) {
            return it->second + col.substr(dot_pos);
        }
    }
    return col;
}

// ── Public parse ───────────────────────────────────────────────────────────────

std::unique_ptr<QueryNode> Parser::parse(std::string_view sql) {
    const Tokens tokens = tokenize(sql);
    if (tokens.empty())
        throw std::runtime_error("Parser: empty input");

    auto root = std::make_unique<QueryNode>();
    std::size_t i = 0;
    const std::size_t n = tokens.size();
    std::unordered_map<std::string, std::string> aliases;

    // ── SELECT ────────────────────────────────────────────────────────────────
    if (to_upper(tokens[i]) != "SELECT")
        throw std::runtime_error(
            "Parser: expected SELECT, got '" + tokens[i] + "'");
    ++i;

    std::vector<std::string> columns;
    while (i < n && to_upper(tokens[i]) != "FROM") {
        columns.push_back(tokens[i++]);
    }

    if (columns.empty())
        throw std::runtime_error("Parser: no column names after SELECT");

    root->select = std::make_unique<SelectNode>(std::move(columns));

    // ── FROM ──────────────────────────────────────────────────────────────────
    if (i >= n || to_upper(tokens[i]) != "FROM")
        throw std::runtime_error("Parser: expected FROM keyword");
    ++i;

    if (i >= n)
        throw std::runtime_error("Parser: expected table name after FROM");

    root->table = std::make_unique<TableNode>(tokens[i++]);

    // Consume optional alias (e.g., "FROM users u")
    if (i < n && !isReservedKeyword(to_upper(tokens[i])) &&
        to_upper(tokens[i]) != "JOIN") {
        aliases[tokens[i]] = root->table->name;
        ++i;  // store and skip alias
    }
    
    // Now resolve SELECT columns that were parsed before FROM
    for (auto& col : columns) {
        col = resolveColumn(col, aliases);
    }

    // ── JOIN … ON … (zero or more) ────────────────────────────────────────────
    //
    // Each iteration consumes:  JOIN <table_name> [alias] ON <col> <op> <col>
    // Multiple joins chain naturally since we loop on "JOIN".
    while (i < n && to_upper(tokens[i]) == "JOIN") {
        ++i;  // consume JOIN

        if (i >= n)
            throw std::runtime_error(
                "Parser: expected table name after JOIN");
        std::string join_table = tokens[i++];

        // Consume optional alias (e.g., "JOIN orders o")
        if (i < n && to_upper(tokens[i]) != "ON" &&
            !isReservedKeyword(to_upper(tokens[i]))) {
            aliases[tokens[i]] = join_table;
            ++i;  // store and skip alias
        }

        if (i >= n || to_upper(tokens[i]) != "ON")
            throw std::runtime_error(
                "Parser: expected ON after JOIN table name '" +
                join_table + "'");
        ++i;  // consume ON

        // Need on_left (i), on_op (i+1), on_right (i+2)
        if (i + 2 >= n)
            throw std::runtime_error(
                "Parser: JOIN ON clause requires <col> <op> <col>");

        std::string on_left  = resolveColumn(tokens[i++], aliases);
        std::string on_op    = tokens[i++];
        std::string on_right = resolveColumn(tokens[i++], aliases);

        root->joins.push_back({
            std::move(join_table),
            std::move(on_left),
            std::move(on_op),
            std::move(on_right)
        });
    }

    // ── WHERE (optional) ──────────────────────────────────────────────────────
    if (i < n && to_upper(tokens[i]) == "WHERE") {
        ++i;  // consume WHERE

        // Build the full recursive predicate tree.
        root->where_expr = parsePredicateExpr(tokens, i, aliases);

        // ── Backward-compatibility bridge ─────────────────────────────────────
        // When the WHERE expression is a simple leaf (no AND/OR), populate the
        // old 'where' field so all existing tests that access
        // root->where->column / op / value continue to compile and pass.
        if (root->where_expr && root->where_expr->is_leaf) {
            root->where = std::make_unique<WhereNode>(
                root->where_expr->column,
                root->where_expr->op,
                root->where_expr->value);
        }
    }

    // ── GROUP BY (optional) ───────────────────────────────────────────────────
    if (i < n && to_upper(tokens[i]) == "GROUP") {
        ++i;  // consume GROUP

        if (i >= n || to_upper(tokens[i]) != "BY")
            throw std::runtime_error("Parser: expected BY after GROUP");
        ++i;  // consume BY

        std::vector<std::string> gb_cols;
        // Collect column names until a reserved keyword or end-of-input.
        while (i < n && !isReservedKeyword(to_upper(tokens[i])))
            gb_cols.push_back(tokens[i++]);

        if (gb_cols.empty())
            throw std::runtime_error(
                "Parser: GROUP BY requires at least one column name");

        root->group_by = std::make_unique<GroupByNode>(std::move(gb_cols));
    }

    // ── Trailing token guard ──────────────────────────────────────────────────
    if (i < n)
        throw std::runtime_error(
            "Parser: unexpected trailing token '" + tokens[i] + "'");

    return root;
}

} // namespace qo
