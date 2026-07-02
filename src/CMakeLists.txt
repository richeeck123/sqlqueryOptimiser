# ── Core library (parser + engine code) ───────────────────────────────────────
add_library(qo_core STATIC
    parser.cpp
    catalog.cpp
    logical_planner.cpp
    optimizer.cpp
    html_reporter.cpp
    executor.cpp
    sql_reconstructor.cpp
)

# nlohmann/json is header-only; PRIVATE linkage propagates include paths only
# to qo_core's own translation units — consumers get it transitively via qo_core.
target_link_libraries(qo_core PUBLIC nlohmann_json::nlohmann_json)

# ── Main executable ────────────────────────────────────────────────────────────
add_executable(query_optimizer main.cpp)
target_link_libraries(query_optimizer PRIVATE qo_core)
