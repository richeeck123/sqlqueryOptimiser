# ── Smoke / unit tests (no external deps) ─────────────────────────────────────
add_executable(run_tests test_version.cpp)
target_include_directories(run_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ── Parser tests ───────────────────────────────────────────────────────────────
add_executable(test_parser test_parser.cpp)
target_include_directories(test_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_parser PRIVATE qo_core)

# ── Catalog tests ──────────────────────────────────────────────────────────────
add_executable(test_catalog test_catalog.cpp)
target_include_directories(test_catalog PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_catalog PRIVATE qo_core)

# ── Optimizer (RBO) tests ──────────────────────────────────────────────────────
add_executable(test_optimizer test_optimizer.cpp)
target_include_directories(test_optimizer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_optimizer PRIVATE qo_core)

# ── Cost-Based Optimizer (CBO) tests ──────────────────────────────────────────
add_executable(test_cbo test_cbo.cpp)
target_include_directories(test_cbo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_cbo PRIVATE qo_core)

enable_testing()
add_test(NAME unit_tests      COMMAND run_tests)
add_test(NAME parser_tests    COMMAND test_parser)
add_test(NAME catalog_tests   COMMAND test_catalog)
add_test(NAME optimizer_tests COMMAND test_optimizer)
add_test(NAME cbo_tests       COMMAND test_cbo)
