#include "test_runner.h"
#include "qo/catalog.h"

// ── Catalog existence ──────────────────────────────────────────────────────────

TEST("catalog contains exactly eight schema tables") {
    ASSERT_EQ(qo::Catalog::instance().table_count(), std::size_t{8});
}

TEST("users table is registered") {
    ASSERT_TRUE(qo::Catalog::instance().has_table("users"));
}

TEST("orders table is registered") {
    ASSERT_TRUE(qo::Catalog::instance().has_table("orders"));
}

TEST("products table is registered") {
    ASSERT_TRUE(qo::Catalog::instance().has_table("products"));
}

TEST("unknown table is not registered") {
    ASSERT_TRUE(!qo::Catalog::instance().has_table("no_such_table"));
}

TEST("find_table returns nullptr for unknown table") {
    ASSERT_TRUE(qo::Catalog::instance().find_table("no_such_table") == nullptr);
}

TEST("get_table throws for unknown table") {
    bool threw = false;
    try { (void)qo::Catalog::instance().get_table("ghost"); }
    catch (const std::out_of_range&) { threw = true; }
    ASSERT_TRUE(threw);
}

// ── Row counts ────────────────────────────────────────────────────────────────

TEST("users row_count is 100 000") {
    const auto& t = qo::Catalog::instance().get_table("users");
    ASSERT_EQ(t.row_count, std::size_t{100'000});
}

TEST("orders row_count is 1 000 000") {
    const auto& t = qo::Catalog::instance().get_table("orders");
    ASSERT_EQ(t.row_count, std::size_t{1'000'000});
}

// ── Primary keys ──────────────────────────────────────────────────────────────

TEST("users primary_key is id") {
    ASSERT_EQ(qo::Catalog::instance().get_table("users").primary_key, "id");
}

TEST("orders primary_key is id") {
    ASSERT_EQ(qo::Catalog::instance().get_table("orders").primary_key, "id");
}

// ── Column counts ─────────────────────────────────────────────────────────────

TEST("users has 3 columns") {
    ASSERT_EQ(qo::Catalog::instance().get_table("users").columns.size(),
              std::size_t{3});
}

TEST("orders has 3 columns") {
    ASSERT_EQ(qo::Catalog::instance().get_table("orders").columns.size(),
              std::size_t{3});
}

TEST("products has 4 columns") {
    ASSERT_EQ(qo::Catalog::instance().get_table("products").columns.size(),
              std::size_t{4});
}

// ── Column types ──────────────────────────────────────────────────────────────

TEST("users.id is int") {
    const auto& t = qo::Catalog::instance().get_table("users");
    const auto* c = t.find_column("id");
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->type, qo::ColumnType::Int);
}

TEST("users.name is string") {
    const auto& t = qo::Catalog::instance().get_table("users");
    const auto* c = t.find_column("name");
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->type, qo::ColumnType::String);
}

TEST("users.age is int") {
    const auto& t = qo::Catalog::instance().get_table("users");
    const auto* c = t.find_column("age");
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->type, qo::ColumnType::Int);
}

TEST("orders.total is double") {
    const auto& t = qo::Catalog::instance().get_table("orders");
    const auto* c = t.find_column("total");
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->type, qo::ColumnType::Double);
}

TEST("orders.user_id is int") {
    const auto& t = qo::Catalog::instance().get_table("orders");
    const auto* c = t.find_column("user_id");
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->type, qo::ColumnType::Int);
}

// ── Index availability ────────────────────────────────────────────────────────

TEST("users.id has an index") {
    const auto& t = qo::Catalog::instance().get_table("users");
    ASSERT_TRUE(t.is_indexed("id"));
}

TEST("users.name has no index") {
    const auto& t = qo::Catalog::instance().get_table("users");
    ASSERT_TRUE(!t.is_indexed("name"));
}

TEST("users.age has no index") {
    const auto& t = qo::Catalog::instance().get_table("users");
    ASSERT_TRUE(!t.is_indexed("age"));
}

TEST("orders.id has an index") {
    const auto& t = qo::Catalog::instance().get_table("orders");
    ASSERT_TRUE(t.is_indexed("id"));
}

TEST("orders.user_id has no index") {
    const auto& t = qo::Catalog::instance().get_table("orders");
    ASSERT_TRUE(!t.is_indexed("user_id"));
}

TEST("orders.total has no index") {
    const auto& t = qo::Catalog::instance().get_table("orders");
    ASSERT_TRUE(!t.is_indexed("total"));
}

// ── find_column on missing column ─────────────────────────────────────────────

TEST("find_column returns nullptr for missing column") {
    const auto& t = qo::Catalog::instance().get_table("users");
    ASSERT_TRUE(t.find_column("salary") == nullptr);
}

TEST("is_indexed returns false for missing column") {
    const auto& t = qo::Catalog::instance().get_table("users");
    ASSERT_TRUE(!t.is_indexed("nonexistent"));
}

// ── products table ────────────────────────────────────────────────────────────

TEST("products row_count is 50 000") {
    const auto& t = qo::Catalog::instance().get_table("products");
    ASSERT_EQ(t.row_count, std::size_t{50'000});
}

TEST("products primary_key is id") {
    ASSERT_EQ(qo::Catalog::instance().get_table("products").primary_key, "id");
}

TEST("products.id is int and indexed") {
    const auto& t = qo::Catalog::instance().get_table("products");
    const auto* c = t.find_column("id");
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->type, qo::ColumnType::Int);
    ASSERT_TRUE(t.is_indexed("id"));
}

TEST("products.name is string and not indexed") {
    const auto& t = qo::Catalog::instance().get_table("products");
    const auto* c = t.find_column("name");
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->type, qo::ColumnType::String);
    ASSERT_TRUE(!t.is_indexed("name"));
}

TEST("products.price is double and not indexed") {
    const auto& t = qo::Catalog::instance().get_table("products");
    const auto* c = t.find_column("price");
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->type, qo::ColumnType::Double);
    ASSERT_TRUE(!t.is_indexed("price"));
}

TEST("products.order_id is int and not indexed") {
    const auto& t = qo::Catalog::instance().get_table("products");
    const auto* c = t.find_column("order_id");
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->type, qo::ColumnType::Int);
    ASSERT_TRUE(!t.is_indexed("order_id"));
}

// ── Runtime registration ──────────────────────────────────────────────────────

TEST("dynamically registered table is retrievable") {
    qo::TableMetadata t;
    t.name        = "temp_test_table";
    t.row_count   = 42;
    t.primary_key = "pk";
    t.columns["pk"] = {"pk", qo::ColumnType::Int, true, false, 0, 0.0, 0.0, {}};

    qo::Catalog::instance().register_table(t);

    const auto* found = qo::Catalog::instance().find_table("temp_test_table");
    ASSERT_TRUE(found != nullptr);
    ASSERT_EQ(found->row_count, std::size_t{42});
    ASSERT_TRUE(found->is_indexed("pk"));
}

// ── ColumnType helper ─────────────────────────────────────────────────────────

TEST("to_string for ColumnType works") {
    ASSERT_EQ(qo::to_string(qo::ColumnType::Int),    "int");
    ASSERT_EQ(qo::to_string(qo::ColumnType::String), "string");
    ASSERT_EQ(qo::to_string(qo::ColumnType::Double), "double");
}

// ── Singleton identity ────────────────────────────────────────────────────────

TEST("Catalog::instance returns the same object on repeated calls") {
    ASSERT_TRUE(&qo::Catalog::instance() == &qo::Catalog::instance());
}

int main() { return qo::test::run_all(); }
