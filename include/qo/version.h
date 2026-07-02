#pragma once

#include <string_view>

namespace qo {

/// Returns the engine version string.
constexpr std::string_view version() noexcept { return "0.1.0"; }

} // namespace qo
