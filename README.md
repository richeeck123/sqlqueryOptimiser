# SQL Query Optimiser — Relational Query Compiler & In-Memory Execution Engine

SQL Query Optimiser is a lightweight, high-performance relational database query optimiser and execution engine simulator built from scratch in C++. The project models modern RDBMS kernel architectures, combining classic compiler phases with an asynchronous full-stack dashboard via a low-latency Python-based Inter-Process Communication (IPC) microservice bridge.

---


### Optimiser Architecture Pipeline

```mermaid
graph TD
    A[Raw SQL Query] --> B[SQL Parser]
    B --> C[Logical Planner]
    C --> D[Rule-Based Optimiser<br>Predicate Pushdown]
    D --> E[Cost-Based Optimiser<br>System-R Join Enumeration]
    E --> F[Physical Plan Generator<br>Cost Model & Join Algorithms]
    F --> G[Volcano Execution Engine<br>& Dashboard Renderer]
```
---

##  Directory Structure

<pre>
QueryOptimizer/
│
├── build/                      # Compiled binary artifacts and CMake cache layouts
│   └── src/
│       └── Release/            # Optimised native production-grade executables
│           └── query_optimizer.exe
│
├── include/                    # Public C++ interface blueprints (Headers)
│   └── qo/
│       ├── ast.h               # SQL Abstract Syntax Tree structure nodes
│       ├── catalog.h           # In-memory database catalog schema metadata
│       ├── executor.h          # Volcano Iterator abstract interface definitions
│       ├── html_reporter.h     # Structural layout compiler for dashboard pages
│       ├── logical_plan.h      # Relational algebraic execution operators
│       ├── logical_planner.h   # AST-to-Logical plan translational layer
│       ├── optimizer.h         # Rule-based (RBO) & Cost-based (CBO) logic passes
│       ├── parser.h            # Lexer & Grammar query text analysis tokenizer
│       └── version.h           # Engine pipeline build metadata metrics
│
├── src/                        # Core system pipeline implementation (Source files)
│   ├── CMakeLists.txt          # Module build registry configuration rules
│   ├── catalog.cpp             # Database catalog lookups and statistics
│   ├── executor.cpp            # Volcano engine row streaming logic loops
│   ├── html_reporter.cpp       # Sleek Glassmorphic HTML layout dashboard builder
│   ├── logical_planner.cpp     # Strategic algebraic transformation engine
│   ├── main.cpp                # Hybrid query router & JSON microservice entrance point
│   ├── optimizer.cpp           # System-R join ordering dynamic programming arrays
│   └── parser.cpp              # String grammar validation scanner
│
├── tests/                      # Validation and behavior unit testing suite frameworks
│   ├── CMakeLists.txt          # Test runner compiler specifications
│   ├── test_catalog.cpp
│   ├── test_cbo.cpp
│   ├── test_optimizer.cpp
│   └── test_parser.cpp
│
├── CMakeLists.txt              # Global project workspace toolchain specifications
├── optimizer_dashboard.html    # Generated dynamic visual interface workspace console
├── schema.json                 # Core system mock catalog table definitions matrix
└── server.py                   # Zero-dependency async Python microservice bridge router
</pre>

---

##  Performance Showcase: Multi-Join Optimisation

The **Multi-Join Cost Optimisation** benchmark evaluates the effectiveness of the complete compiler pipeline on a complex three-table join containing qualified alias predicates. 

**Query:**
```sql
SELECT * FROM users u 
JOIN orders o ON u.id = o.user_id 
JOIN products p ON o.product_id = p.id 
WHERE u.age > 30 AND o.total > 1500 AND p.price < 500;
```

### Verified Cost Metrics Output

* **Initial Cost:** `609,985,400,000`
* **Optimised Cost:** `28,219,910,000`
* **Performance Improvement:** `95.37%`

This immense cardinality reduction represents the compounded successes of:
1. **Predicate Pushdown:** Extracting the conditions from the compound root and mapping them exactly above their corresponding logical scan operations (`users.age`, `orders.total`, `products.price`).
2. **Reduced Cardinality:** Applying statistical filter selectivity models to slash processing blocks *before* Cartesian boundaries.
3. **Better Join Ordering:** Rebuilding the left-deep tree to merge the smallest rowsets early in the graph using System-R dynamic programming evaluation.
4. **HashJoin Selection:** Replacing the massive outer boundary evaluations with linear-time `HashJoin` hashing models where selectivity demands it.

---

##  Tech Stack & Dependencies

* **Systems Core:** C++17 (or higher)
* **Data Serialization:** `nlohmann/json` configuration parser
* **Microservice Intercept:** Python 3 (Native HTTP & Subprocess runtime)
* **Frontend Interface:** Modern Glassmorphic Dashboard (HTML5, Vanilla CSS3, Async Fetch API)

---

##  Setup & Execution Sequence

Follow these exact steps sequentially from your terminal to launch the full-stack system layout cleanly:

### Terminal 1: Build Infrastructure and Boot the IPC Server

First, configure the project layout and compile the native C++ targets using the Release profile configuration:

# 1. Generate CMake configuration cache layout
cmake -S . -B build

# 2. Compile target execution binaries cleanly
cmake --build build --config Release

# 3. Spin up the background microservice routing bridge
python server.py
(Keep this terminal tab open! The server will lock this context window to process runtime subprocess piping commands).

Terminal 2: Launch the Interactive Dashboard
Now, open a completely new terminal tab or console context window within the same repository path to boot the user interface:

# 4. Open the dynamic visualization interface console in your browser
start optimizer_dashboard.html

##  Testing the Engine Components
Once the dashboard loads into Google Chrome or Edge, you can evaluate the dynamic heuristics and Volcano execution passes by copying these test scenarios into the Interactive SQL Input Console:

Dynamic Parser Evaluation:
SELECT products.name, products.price FROM products WHERE products.price < 80

Multi-Table Optimisation Pipeline:
rs.name, products.name FROM orders JOIN users ON orders.user_id = users.id JOIN products ON orders.product_id = products.id WHERE orders.total > 4000


Click Run Live Optimisation  to see cost models, query evaluation plans, and real-time execution rows rendered instantly without blocking the browser interface pipeline.


## Limitations :
While SQL Query Optimiser implements several real-world query optimisation techniques such as Predicate Pushdown, Rule-Based Optimisation (RBO), Dynamic Programming Cost-Based Optimisation (CBO), Join Reordering, and Physical Operator Selection, the current version focuses on a simplified subset of SQL and does not support all features found in production database systems.

 Self-Join Support

Self-joins are not currently supported. The optimizer resolves table aliases to their underlying base table names during optimisation, which causes multiple references to the same table to be treated as a single relation. This can lead to incorrect predicate ownership, join graph construction, predicate pushdown decisions, and SQL reconstruction. Queries that join the same table multiple times using aliases should be avoided.

 Aggregation Optimisation

Optimisation techniques involving GROUP BY, HAVING, and aggregate functions such as COUNT, SUM, AVG, MIN, and MAX are not implemented. Aggregate pushdown and partial aggregation strategies are outside the scope of the current system.

 ORDER BY Optimisation

Queries containing ORDER BY clauses are not optimised. The optimizer does not perform sort elimination, index-aware ordering, or Top-N optimisations.

 Index-Aware Cost Estimation

The cost model assumes scan-based execution and does not consider the presence of indexes. B+ Trees, hash indexes, covering indexes, and clustered indexes are not incorporated into cost estimation. As a result, estimated costs may differ from those produced by a real database system.

 Simplified Cardinality Estimation

Cardinality estimation is based on simplified selectivity assumptions and does not use histograms, column statistics, or correlation information. Predicates are generally assumed to be independent, which may lead to inaccurate cost estimates for complex queries.

 Limited SQL Grammar Coverage

The parser supports a core subset of SQL focused on SELECT, FROM, WHERE, JOIN, AND, and OR clauses. Advanced SQL features such as UNION ALL, INTERSECT, EXCEPT, Common Table Expressions (CTEs), window functions, CASE expressions, and DISTINCT are not currently supported.



 Educational Cost Model

The implemented cost model is intended for educational purposes and demonstration of query optimisation concepts. It does not account for many real-world factors including memory availability, CPU cache behavior, disk I/O patterns, parallel execution, network transfer costs, buffer pool management, or adaptive query execution.
