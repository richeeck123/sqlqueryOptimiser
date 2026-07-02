#pragma once

#include "qo/logical_plan.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace qo {

using Tuple = std::vector<std::string>;

class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;

    virtual void init() = 0;
    virtual bool next(Tuple& tuple) = 0;
    virtual void close() = 0;
};

class SeqScanExecutor : public AbstractExecutor {
public:
    SeqScanExecutor(std::string table_name, const TableMetadata* meta);

    void init() override;
    bool next(Tuple& tuple) override;
    void close() override;

private:
    std::string table_name_;
    const TableMetadata* meta_;
    std::size_t current_row_;
    std::size_t max_rows_;
};

class FilterExecutor : public AbstractExecutor {
public:
    FilterExecutor(BinaryCondition condition, std::unique_ptr<AbstractExecutor> child);

    void init() override;
    bool next(Tuple& tuple) override;
    void close() override;

private:
    BinaryCondition condition_;
    std::unique_ptr<AbstractExecutor> child_;
    unsigned filter_counter_;
};

class NestedLoopJoinExecutor : public AbstractExecutor {
public:
    NestedLoopJoinExecutor(BinaryCondition condition,
                           std::unique_ptr<AbstractExecutor> left,
                           std::unique_ptr<AbstractExecutor> right);

    void init() override;
    bool next(Tuple& tuple) override;
    void close() override;

private:
    BinaryCondition condition_;
    std::unique_ptr<AbstractExecutor> left_;
    std::unique_ptr<AbstractExecutor> right_;
    Tuple current_left_tuple_;
    bool has_left_;
    unsigned match_counter_;
};

class HashJoinExecutor : public AbstractExecutor {
public:
    HashJoinExecutor(BinaryCondition condition,
                     std::unique_ptr<AbstractExecutor> build_left,
                     std::unique_ptr<AbstractExecutor> probe_right);

    void init() override;
    bool next(Tuple& tuple) override;
    void close() override;

private:
    BinaryCondition condition_;
    std::unique_ptr<AbstractExecutor> build_left_;
    std::unique_ptr<AbstractExecutor> probe_right_;
    std::unordered_map<std::string, std::vector<Tuple>> hash_table_;
    std::vector<Tuple> current_matches_;
    std::size_t match_index_;
    unsigned match_counter_;
};
class ProjectExecutor : public AbstractExecutor {
public:
    ProjectExecutor(std::vector<std::string> columns,
                    std::unique_ptr<AbstractExecutor> child);

    void init() override;
    bool next(Tuple& tuple) override;
    void close() override;

private:
    std::vector<std::string> columns_;
    std::unique_ptr<AbstractExecutor> child_;
};

// ── HashAggregateExecutor  [Phase 4] ─────────────────────────────────────────
//
// Implements GROUP BY via an in-memory hash table (Volcano Iterator model).
//
// init()  — pulls ALL tuples from child, inserts them into an unordered_map
//           keyed on a serialised tuple string.  Each unique key represents
//           one distinct group.  The map values hold the representative tuple
//           for that group.
//
// next()  — returns one unique group tuple per call, iterating the materialised
//           groups_ vector from front to back.
//
// close() — resets internal state; child's close() is called to release
//           resources downstream.
//
// Trade-offs and limitations (acceptable for the simulation model):
//   • Full materialisation in init() — results are held in memory.
//   • No aggregate functions (COUNT, SUM, …) — only distinct groups are
//     returned.  Phase 4+ can extend group_columns_ for aggregations.
//   • Tuple content is the key: since SeqScanExecutor emits {table, row_id},
//     each unique {table, row_id} pair represents an independent group.
class HashAggregateExecutor : public AbstractExecutor {
public:
    HashAggregateExecutor(std::vector<std::string> group_columns,
                          std::unique_ptr<AbstractExecutor> child);

    void init() override;
    bool next(Tuple& tuple) override;
    void close() override;

private:
    /// GROUP BY column names (used for documentation; key is full tuple for now)
    std::vector<std::string>          group_columns_;
    std::unique_ptr<AbstractExecutor> child_;

    /// One entry per unique group (populated during init())
    std::vector<Tuple>  groups_;
    std::size_t         groups_idx_;
    bool                materialized_;
};

class UnionExecutor : public AbstractExecutor {
public:
    UnionExecutor(std::unique_ptr<AbstractExecutor> left,
                  std::unique_ptr<AbstractExecutor> right);

    void init() override;
    bool next(Tuple& tuple) override;
    void close() override;

private:
    std::unique_ptr<AbstractExecutor> left_;
    std::unique_ptr<AbstractExecutor> right_;
    bool drain_left_;
};

/// Compiles a logical/physical plan tree into a Volcano Iterator execution tree.
std::unique_ptr<AbstractExecutor> compilePlan(const LogicalNode* node);

} // namespace qo
