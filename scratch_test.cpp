#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

bool check_in_set(const std::string& col, const std::unordered_set<std::string>& tbls) {
    for (const auto& tbl : tbls) {
        if (col == tbl + ".id" || col.find(tbl + ".") == 0) return true;
    }
    return false;
}

int main() {
    std::unordered_set<std::string> left_tbls = {"orders", "products"};
    std::unordered_set<std::string> right_tbls = {"users"};

    std::string cL = "users.id";
    std::string cR = "orders.user_id";

    bool l_in_L = check_in_set(cL, left_tbls);
    bool r_in_R = check_in_set(cR, right_tbls);
    bool l_in_R = check_in_set(cL, right_tbls);
    bool r_in_L = check_in_set(cR, left_tbls);

    std::cout << "l_in_L: " << l_in_L << "\n";
    std::cout << "r_in_R: " << r_in_R << "\n";
    std::cout << "l_in_R: " << l_in_R << "\n";
    std::cout << "r_in_L: " << r_in_L << "\n";
    std::cout << "match: " << ((l_in_L && r_in_R) || (l_in_R && r_in_L)) << "\n";

    return 0;
}
