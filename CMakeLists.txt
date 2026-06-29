cmake_minimum_required(VERSION 3.20)
project(SQLQueryOptimiser VERSION 0.1.0 LANGUAGES CXX)

# ── Language standard ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler warnings ──────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ── Global include path ────────────────────────────────────────────────────────
include_directories(${PROJECT_SOURCE_DIR}/include)

# ── nlohmann/json via FetchContent ────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Header-only; disable tests + install to keep the build lean.
set(JSON_BuildTests   OFF CACHE INTERNAL "")
set(JSON_Install      OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(json)

# ── Pass the schema file path to the compiler ─────────────────────────────────
# All targets that include catalog.h will know the absolute path at compile time.
add_compile_definitions(
    QO_SCHEMA_PATH="${PROJECT_SOURCE_DIR}/schema.json"
)

# ── Copy schema.json into every build-directory config sub-folder ─────────────
# This ensures CTest (which runs from the build tree) can also open the file
# via a relative "schema.json" path if needed in the future.
configure_file(
    ${PROJECT_SOURCE_DIR}/schema.json
    ${CMAKE_BINARY_DIR}/schema.json
    COPYONLY
)

# ── Testing ────────────────────────────────────────────────────────────────────
# enable_testing() must be called at the root level for CTest discovery.
enable_testing()

# ── Sub-directories ────────────────────────────────────────────────────────────
add_subdirectory(src)
add_subdirectory(tests)
