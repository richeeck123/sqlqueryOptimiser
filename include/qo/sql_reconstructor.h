#pragma once

#include "qo/logical_plan.h"
#include <string>

namespace qo {

class SqlReconstructor {
public:
    static std::string reconstruct(const LogicalNode& plan);
};

} // namespace qo
