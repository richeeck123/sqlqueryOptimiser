#include "qo/sql_reconstructor.h"

namespace qo {

namespace {

class ReconstructorVisitor {
public:
    std::string build(const LogicalNode& node, int depth) {
        switch(node.kind()) {
            case LogicalNodeKind::Scan: {
                const auto& scan = static_cast<const LogicalScan&>(node);
                return scan.table_name;
            }
            case LogicalNodeKind::Filter: {
                const auto& filter = static_cast<const LogicalFilter&>(node);
                std::string childSql = filter.child ? build(*filter.child, depth + 1) : "";
                std::string condSql = filter.compound_cond ? formatTree(filter.compound_cond.get()) : formatCond(filter.condition);
                
                if (filter.child && filter.child->kind() == LogicalNodeKind::Scan) {
                    return "(SELECT * FROM " + childSql + " WHERE " + condSql + ") AS " + childSql;
                } else {
                    return "(SELECT * FROM " + childSql + " WHERE " + condSql + ") AS T" + std::to_string(depth);
                }
            }
            case LogicalNodeKind::Join: {
                const auto& join = static_cast<const LogicalJoin&>(node);
                std::string leftSql = join.left ? build(*join.left, depth + 1) : "";
                std::string rightSql = join.right ? build(*join.right, depth + 1) : "";
                
                std::string joinSql = leftSql + "\n  JOIN " + rightSql;
                if (!join.condition.empty()) {
                    joinSql += " ON " + formatCond(join.condition);
                } else {
                    joinSql += " ON true";
                }
                
                if (depth == 0) return "SELECT * FROM " + joinSql;
                return "(SELECT * FROM " + joinSql + "\n) AS J" + std::to_string(depth);
            }
            case LogicalNodeKind::Project: {
                const auto& proj = static_cast<const LogicalProject&>(node);
                std::string childSql = proj.child ? build(*proj.child, depth + 1) : "";
                std::string cols;
                for (size_t i = 0; i < proj.columns.size(); ++i) {
                    cols += proj.columns[i];
                    if (i + 1 < proj.columns.size()) cols += ", ";
                }
                
                if (depth == 0) {
                    if (proj.child && proj.child->kind() == LogicalNodeKind::Scan) {
                        return "SELECT " + cols + "\nFROM " + childSql;
                    } else if (childSql.length() >= 15 && childSql.substr(0, 15) == "(SELECT * FROM ") {
                        // Keep the inner formatting but prepend the project list
                        return "SELECT " + cols + "\nFROM " + childSql;
                    }
                    return "SELECT " + cols + "\nFROM " + childSql;
                } else {
                    return "(SELECT " + cols + " FROM " + childSql + ") AS P" + std::to_string(depth);
                }
            }
            case LogicalNodeKind::Aggregate: {
                const auto& agg = static_cast<const LogicalAggregate&>(node);
                std::string childSql = agg.child ? build(*agg.child, depth + 1) : "";
                std::string cols;
                for (size_t i = 0; i < agg.group_columns.size(); ++i) {
                    cols += agg.group_columns[i];
                    if (i + 1 < agg.group_columns.size()) cols += ", ";
                }
                
                if (depth == 0) {
                    return "SELECT * FROM " + childSql + "\nGROUP BY " + cols;
                } else {
                    return "(SELECT * FROM " + childSql + " GROUP BY " + cols + ") AS A" + std::to_string(depth);
                }
            }
            case LogicalNodeKind::Union: {
                const auto& u = static_cast<const LogicalUnion&>(node);
                std::string leftSql = u.left ? build(*u.left, depth + 1) : "";
                std::string rightSql = u.right ? build(*u.right, depth + 1) : "";
                
                std::string unionSql = leftSql + "\n UNION \n" + rightSql;
                if (depth == 0) return unionSql;
                return "(" + unionSql + ") AS U" + std::to_string(depth);
            }
        }
        return "";
    }

private:
    std::string formatCond(const BinaryCondition& cond) {
        return cond.column + " " + cond.op + " " + cond.value;
    }
    std::string formatTree(const PredicateTree* tree) {
        if (!tree) return "";
        if (tree->is_leaf) return formatCond(tree->condition);
        return "(" + formatTree(tree->left.get()) + " " + tree->logical_op + " " + formatTree(tree->right.get()) + ")";
    }
};

} // namespace

std::string SqlReconstructor::reconstruct(const LogicalNode& plan) {
    ReconstructorVisitor visitor;
    return visitor.build(plan, 0) + ";";
}

} // namespace qo
