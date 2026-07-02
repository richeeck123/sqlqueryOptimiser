#include "qo/catalog.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string>

// QO_SCHEMA_PATH is injected by CMake as an add_compile_definitions() value.
// It expands to the absolute path of schema.json in the project source root.
#ifndef QO_SCHEMA_PATH
#  define QO_SCHEMA_PATH "schema.json"
#endif

namespace qo {

// ── load_from_file ────────────────────────────────────────────────────────────
//
// Parses the JSON schema file and populates the tables_ map.
//
// Expected JSON shape:
//   {
//     "tables": [
//       {
//         "name":        "<table>",
//         "row_count":   <integer>,
//         "primary_key": "<column>",
//         "columns": [
//           { "name": "<col>", "type": "int|string|double", "has_index": bool }
//         ]
//       }
//     ]
//   }

void Catalog::load_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error(
            "Catalog: cannot open schema file '" + path + "'");
    }

    nlohmann::json doc;
    try {
        file >> doc;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(
            std::string("Catalog: JSON parse error in '") + path + "': " + e.what());
    }

    if (!doc.contains("tables") || !doc["tables"].is_array()) {
        throw std::runtime_error(
            "Catalog: schema file '" + path + "' is missing a top-level \"tables\" array");
    }

    for (const auto& jtable : doc["tables"]) {
        TableMetadata t;
        t.name        = jtable.at("name").get<std::string>();
        t.row_count   = jtable.at("row_count").get<std::size_t>();
        t.primary_key = jtable.at("primary_key").get<std::string>();

        if (!jtable.contains("columns") || !jtable["columns"].is_array()) {
            throw std::runtime_error(
                "Catalog: table '" + t.name + "' has no \"columns\" array");
        }

        for (const auto& jcol : jtable["columns"]) {
            ColumnMetadata col;
            col.name      = jcol.at("name").get<std::string>();
            col.type      = column_type_from_string(jcol.at("type").get<std::string>());
            col.has_index = jcol.at("has_index").get<bool>();
            if (jcol.contains("clustered")) {
                col.clustered = jcol.at("clustered").get<bool>();
            }
            if (jcol.contains("distinct_count")) {
                col.distinct_count = jcol.at("distinct_count").get<std::size_t>();
            }
            if (jcol.contains("min_val")) {
                col.min_val = jcol.at("min_val").get<double>();
            }
            if (jcol.contains("max_val")) {
                col.max_val = jcol.at("max_val").get<double>();
            }
            if (jcol.contains("histogram") && jcol["histogram"].is_array()) {
                for (const auto& bucket : jcol["histogram"]) {
                    HistogramBucket hb;
                    hb.min_val = bucket.at("min").get<double>();
                    hb.max_val = bucket.at("max").get<double>();
                    hb.count   = bucket.at("count").get<std::size_t>();
                    col.histogram.push_back(hb);
                }
            }
            t.columns[col.name] = std::move(col);
        }

        tables_[t.name] = std::move(t);
    }
}

// ── Catalog constructor ───────────────────────────────────────────────────────
//
// Delegates to load_from_file using the compile-time schema path.
// If the file cannot be found or is malformed, the error propagates as a
// std::runtime_error from the singleton's first access point.

Catalog::Catalog() {
    load_from_file(QO_SCHEMA_PATH);
}

} // namespace qo
