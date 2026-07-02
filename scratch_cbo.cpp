#include "qo/catalog.h"
#include "qo/logical_plan.h"
#include "qo/logical_planner.h"
#include "qo/optimizer.h"
#include "qo/parser.h"

#include <iostream>

using namespace qo;

int main() {
    qo::TableMetadata t;
    t.name        = "products";
    t.row_count   = 500;
    t.primary_key = "id";
    t.columns["id"]    = {"id",    qo::ColumnType::Int,    true, false, 0, 0.0, 0.0, {}};
    t.columns["name"]  = {"name",  qo::ColumnType::String, false, false, 0, 0.0, 0.0, {}};
    t.columns["price"] = {"price", qo::ColumnType::Double, false, false, 0, 0.0, 0.0, {}};
    qo::Catalog::instance().register_table(std::move(t));

    qo::Parser parser;
    auto ast = parser.parse("SELECT * FROM users JOIN orders ON users.id = orders.user_id JOIN products ON orders.product_id = products.id");

    qo::LogicalPlanner planner(qo::Catalog::instance());
    auto plan = planner.plan(*ast);

    qo::Optimizer opt(qo::Catalog::instance());
    auto opt_plan = opt.pushDownPredicates(std::move(plan));
    opt_plan = opt.optimizeJoinOrder(std::move(opt_plan));
    
    double final_cost = opt.estimateStats(*opt_plan).cost;
    
    std::cout << "final_cost: " << final_cost << "\n";
    std::cout << "opt_plan JSON:\n" << opt_plan->toJson().dump(2) << "\n";
    
    return 0;
}
