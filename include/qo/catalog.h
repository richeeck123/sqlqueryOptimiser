#pragma once

/**
 * Database Catalog — schema and statistics registry.
 *
 * Types
 * ─────
 *   ColumnMetadata   name · type string · index flag
 *   TableMetadata    name · row_count · primary_key · column map
 *   Catalog          Meyers singleton; loaded from schema.json at startup.
 *
 * Schema file format (schema.json)
 * ─────────────────────────────────
 *   {
 *     "tables": [
 *       {
 *         "name":        "users",
 *         "row_count":   100000,
 *         "primary_key": "id",
 *         "columns": [
 *           { "name": "id",   "type": "int",    "has_index": true  },
 *           { "name": "name", "type": "string", "has_index": false }
 *         ]
 *       }
 *     ]
 *   }
 *
 * The singleton is initialised with the path supplied by the QO_SCHEMA_PATH
 * compile-time definition (set by CMake to the project-root schema.json).
 *
 * Usage
 * ─────
 *   const auto* tbl = qo::Catalog::instance().find_table("users");
 *   if (tbl) { ... tbl->row_count ... }
 */

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace qo {

// ── Column ─────────────────────────────────────────────────────────────────────

/// Type tag for a column's data type.
enum class ColumnType { Int, String, Double };

/// Returns the canonical name string for a ColumnType.
inline std::string to_string(ColumnType t) {
    switch (t) {
        case ColumnType::Int:    return "int";
        case ColumnType::String: return "string";
        case ColumnType::Double: return "double";
    }
    return "unknown";
}

/// Parse a type string from JSON ("int" | "string" | "double") to ColumnType.
/// @throws std::invalid_argument for unrecognised type strings.
inline ColumnType column_type_from_string(const std::string& s) {
    if (s == "int")    return ColumnType::Int;
    if (s == "string") return ColumnType::String;
    if (s == "double") return ColumnType::Double;
    throw std::invalid_argument("Catalog: unknown column type '" + s + "'");
}

/// Statistics for a value range.
struct HistogramBucket {
    double min_val;
    double max_val;
    std::size_t count;
};

/// Schema and statistics for a single column.
struct ColumnMetadata {
    std::string name;
    ColumnType  type;
    bool        has_index;  ///< true when a B-tree / hash index exists
    bool        clustered = false; ///< true when the table is physically sorted by this index
    std::size_t distinct_count = 0; ///< Used for dynamic selectivity estimation
    double      min_val = 0.0;
    double      max_val = 0.0;
    std::vector<HistogramBucket> histogram; ///< Range buckets
};

// ── Table ──────────────────────────────────────────────────────────────────────

/// Schema and statistics for a single table.
struct TableMetadata {
    std::string name;
    std::size_t row_count;
    std::string primary_key;

    /// Column map keyed by column name (lower-case).
    std::unordered_map<std::string, ColumnMetadata> columns;

    // ── Convenience accessors ─────────────────────────────────────────────────

    /// Returns nullptr if the column does not exist.
    [[nodiscard]] const ColumnMetadata*
    find_column(const std::string& col_name) const {
        auto it = columns.find(col_name);
        return it != columns.end() ? &it->second : nullptr;
    }

    /// Returns true when col_name exists and has an index.
    [[nodiscard]] bool is_indexed(const std::string& col_name) const {
        const auto* c = find_column(col_name);
        return c != nullptr && c->has_index;
    }
};

// ── Catalog ────────────────────────────────────────────────────────────────────

/**
 * Global schema + statistics registry.
 *
 * The singleton loads its table definitions from a JSON schema file whose path
 * is provided at construction time.  By default the path is the compile-time
 * constant QO_SCHEMA_PATH (injected by CMake), which points at schema.json in
 * the project root.
 *
 * New tables can be added at runtime via register_table().
 *
 * Access the singleton via Catalog::instance().
 */
class Catalog {
public:
    /// Returns the process-wide singleton (thread-safe, C++11 §6.7).
    /// The singleton is initialised once from QO_SCHEMA_PATH.
    static Catalog& instance() {
        static Catalog catalog;
        return catalog;
    }

    // Non-copyable / non-movable
    Catalog(const Catalog&)            = delete;
    Catalog& operator=(const Catalog&) = delete;
    Catalog(Catalog&&)                 = delete;
    Catalog& operator=(Catalog&&)      = delete;

    // ── Mutation ──────────────────────────────────────────────────────────────

    /// Register (or replace) a table in the catalog.
    void register_table(TableMetadata table) {
        tables_[table.name] = std::move(table);
    }

    // ── Queries ───────────────────────────────────────────────────────────────

    /// Returns nullptr when no table with that name is registered.
    [[nodiscard]] const TableMetadata*
    find_table(const std::string& name) const {
        auto it = tables_.find(name);
        return it != tables_.end() ? &it->second : nullptr;
    }

    /// @throws std::out_of_range when the table is not registered.
    [[nodiscard]] const TableMetadata&
    get_table(const std::string& name) const {
        const auto* t = find_table(name);
        if (!t) throw std::out_of_range("Catalog: unknown table '" + name + "'");
        return *t;
    }

    [[nodiscard]] bool has_table(const std::string& name) const {
        return tables_.count(name) != 0;
    }

    [[nodiscard]] std::size_t table_count() const noexcept {
        return tables_.size();
    }

private:
    /// Private constructor — loads schema from the compile-time schema path.
    Catalog();

    /// Parse and load all table definitions from a JSON file.
    /// @throws std::runtime_error if the file cannot be opened or is malformed.
    void load_from_file(const std::string& path);

    std::unordered_map<std::string, TableMetadata> tables_;
};

} // namespace qo
