# Database Benchmarks

This directory contains performance benchmarks for the LMDB database wrapper.

## Available Benchmarks

### bench_db_init - Database Initialization Benchmark

**Purpose**: Measures the time to initialize a database from scratch (folder creation + environment setup).

**What is measured**:

- ONLY the initialization time (`db_core_init`)
- Includes directory creation and environment setup
- Each test iteration starts from a completely clean state

**What is NOT measured**:

- Shutdown/cleanup time (excluded from timing)

**Configuration**:

- Iterations: 100 (each from clean state)
- Sub-DBIs: 1
- Database path: `/tmp/bench_lmdb_test`

**Output**:

- Console: System info + summary statistics
- Files:
  - `results/bench_db_init_results_1dbi.txt`
  - `results/bench_db_init_results_10dbis.txt`
  Each contains complete system information and all iteration timings for its configuration.

**Running**:

```bash
# Using the convenience script
./utils/bench/create_db.sh

# Or directly
./build/bench_db_init

# With custom output file
./build/bench_db_init /path/to/custom/output.txt
```

### bench_db_write - PUT Operations Benchmark (single vs batched, append vs normal)

**Purpose**: Compares inserting user records into a single sub-DBI with and without the append fast-path:

- Non-batched: one `PUT` per `db_core_exec_ops` call
- Batched: groups of 8 `PUT`s per `db_core_exec_ops`
- Each mode is measured twice: normal write and appendable write (strictly increasing keys)
- Normal writes feed keys in descending order to force worst-case page movement; appendable writes use ascending keys to match LMDB's append fast-path.

**What is measured**:

- ONLY the time spent in `db_core_set_op` + `db_core_exec_ops`
- Database environment/DBI creation and shutdown are excluded
- Each run starts from a completely clean database directory

**Configuration**:

- Users per run: 100
- Value size: 512 bytes
- Batch sizes:
  - 1 (non-batched)
  - 8 (batched)
- Runs per pattern: 10
- Sub-DBIs: 1
- Database path: `/tmp/bench_lmdb_write`

**Output**:

- Console: System info + summary statistics for each pattern
- Files:
  - `results/bench_write_single.txt`
  - `results/bench_write_single_append.txt`
  - `results/bench_write_batch8.txt`
  - `results/bench_write_batch8_append.txt`
  Each contains system information, per-run totals and per-operation statistics.

**Running**:

```bash
# Using the convenience script
./utils/bench/write_db.sh

# Or directly
./build/bench_db_write
```

### bench_db_get_batch - GET Operations Benchmark (single vs batched)

**Purpose**: Compares fetching random user records from a single sub-DBI:

- Non-batched: one `GET` per `db_core_exec_ops` call
- Batched: groups of 8 `GET`s per `db_core_exec_ops`

**What is measured**:

- ONLY the time spent in `db_core_set_op` + `db_core_exec_ops` for GET operations
- Database environment/DBI creation, initial population, and shutdown are excluded
- Each run starts from a completely clean database directory, then:
  - Initializes environment + one DBI
  - Populates 1000 key/value pairs (not timed)
  - Executes the GET workload (timed)

**Configuration**:

- Users stored: 1000
- Value size: 1024 bytes
- GETs per run: 1000
- Batch sizes:
  - 1 (non-batched)
  - 8 (batched)
- Runs per pattern: 10
- Sub-DBIs: 1
- Database path: `/tmp/bench_lmdb_get`

**Output**:

- Console: System info + summary statistics for each pattern
- Files:
  - `results/bench_get_users_single.txt`
  - `results/bench_get_users_batch8.txt`
  Each contains system information, per-run totals and per-operation statistics.

**Running**:

```bash
# Using the convenience script
./utils/bench/read_db.sh

# Or directly
./build/bench_db_get_batch
```

## Results Format

The benchmark outputs include:

1. **System Information**:
   - Hostname, OS, CPU model, cores, frequency
   - RAM size
   - Storage type (SSD/HDD)
   - Filesystem type

2. **Per-Operation Statistics**:
   - Mean, Standard Deviation, Median
   - Minimum and Maximum times
   - All values in microseconds (μs) and milliseconds (ms)

3. **Detailed Timing Data**:
   - Individual timing for every iteration
   - Useful for analyzing performance distribution

## Notes

- All tests are run from scratch (directory deleted between iterations)
- Results are stored in `tests/benchmarks/results/`
- The benchmark ensures no cached state affects measurements
