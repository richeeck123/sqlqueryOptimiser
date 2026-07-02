#include "test_runner.h"
#include "qo/parser.h"
#include <sstream>
#include <stdexcept>

// ── Helpers ───────────────────────────────────────────────────────────────────

static qo::QueryNode* parse_ok(const std::string& sql,
                                std::unique_ptr<qo::QueryNode>& out) {
    qo::Parser p;
    out = p.parse(sql);
    ASSERT_TRUE(out != nullptr);
    return out.get();
}

// ── SELECT / FROM ─────────────────────────────────────────────────────────────

TEST("single column SELECT") {
    std::unique_ptr<qo::QueryNode> root;
    parse_ok("SELECT name FROM users", root);

    ASSERT_EQ(root->kind(), qo::NodeKind::Query);
    ASSERT_TRUE(root->select != nullptr);
    ASSERT_EQ(root->select->columns.size(), std::size_t{1});
    ASSERT_EQ(root->select->columns[0], "name");
    ASSERT_TRUE(root->table != nullptr);
    ASSERT_EQ(root->table->name, "users");
    ASSERT_TRUE(root->where == nullptr);
}

TEST("multi-column SELECT with commas") {
    std::unique_ptr<qo::QueryNode> root;
    parse_ok("SELECT id, name, email FROM accounts", root);

    ASSERT_EQ(root->select->columns.size(), std::size_t{3});
    ASSERT_EQ(root->select->columns[0], "id");
    ASSERT_EQ(root->select->columns[1], "name");
    ASSERT_EQ(root->select->columns[2], "email");
    ASSERT_EQ(root->table->name, "accounts");
}

// ── WHERE clause ──────────────────────────────────────────────────────────────

TEST("WHERE greater-than predicate") {
    std::unique_ptr<qo::QueryNode> root;
    parse_ok("SELECT name FROM users WHERE age > 21", root);

    ASSERT_TRUE(root->where != nullptr);
    ASSERT_EQ(root->where->column, "age");
    ASSERT_EQ(root->where->op,     ">");
    ASSERT_EQ(root->where->value,  "21");
}

TEST("WHERE equality predicate") {
    std::unique_ptr<qo::QueryNode> root;
    parse_ok("SELECT id FROM orders WHERE status = active", root);

    ASSERT_EQ(root->where->column, "status");
    ASSERT_EQ(root->where->op,     "=");
    ASSERT_EQ(root->where->value,  "active");
}

TEST("WHERE less-than predicate") {
    std::unique_ptr<qo::QueryNode> root;
    parse_ok("SELECT price FROM products WHERE price < 100", root);

    ASSERT_EQ(root->where->column, "price");
    ASSERT_EQ(root->where->op,     "<");
    ASSERT_EQ(root->where->value,  "100");
}

// ── AST node kinds ────────────────────────────────────────────────────────────

TEST("node kind tags are correct") {
    std::unique_ptr<qo::QueryNode> root;
    parse_ok("SELECT col FROM tbl WHERE x != y", root);

    ASSERT_EQ(root->kind(),         qo::NodeKind::Query);
    ASSERT_EQ(root->select->kind(), qo::NodeKind::Select);
    ASSERT_EQ(root->table->kind(),  qo::NodeKind::Table);
    ASSERT_EQ(root->where->kind(),  qo::NodeKind::Where);
}

// ── dump() output ─────────────────────────────────────────────────────────────

TEST("dump produces expected lines") {
    std::unique_ptr<qo::QueryNode> root;
    parse_ok("SELECT name FROM users WHERE age > 21", root);

    std::ostringstream oss;
    root->dump(oss);
    const std::string out = oss.str();

    ASSERT_TRUE(out.find("QueryNode")           != std::string::npos);
    ASSERT_TRUE(out.find("SelectNode")          != std::string::npos);
    ASSERT_TRUE(out.find("name")                != std::string::npos);
    ASSERT_TRUE(out.find("TableNode")           != std::string::npos);
    ASSERT_TRUE(out.find("users")               != std::string::npos);
    ASSERT_TRUE(out.find("WhereNode")           != std::string::npos);
    ASSERT_TRUE(out.find("age > 21")            != std::string::npos);
}

// ── Error handling ────────────────────────────────────────────────────────────

TEST("missing SELECT throws") {
    qo::Parser p;
    bool threw = false;
    try { (void)p.parse("FROM users"); }
    catch (const std::runtime_error&) { threw = true; }
    ASSERT_TRUE(threw);
}

TEST("missing FROM throws") {
    qo::Parser p;
    bool threw = false;
    try { (void)p.parse("SELECT name"); }
    catch (const std::runtime_error&) { threw = true; }
    ASSERT_TRUE(threw);
}

TEST("empty input throws") {
    qo::Parser p;
    bool threw = false;
    try { (void)p.parse(""); }
    catch (const std::runtime_error&) { threw = true; }
    ASSERT_TRUE(threw);
}

TEST("incomplete WHERE throws") {
    qo::Parser p;
    bool threw = false;
    try { (void)p.parse("SELECT x FROM t WHERE only_col"); }
    catch (const std::runtime_error&) { threw = true; }
    ASSERT_TRUE(threw);
}

// ── Case-insensitive keywords ──────────────────────────────────────────────────

TEST("keywords are case-insensitive") {
    std::unique_ptr<qo::QueryNode> root;
    qo::Parser p;
    root = p.parse("select name from users where age > 18");
    ASSERT_EQ(root->table->name, "users");
    ASSERT_EQ(root->where->op,   ">");
}

int main() { return qo::test::run_all(); }
