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
- Iterations: 1000 (each from clean state)
- Sub-DBIs: 1
- Database path: `/tmp/bench_lmdb_test`

**Output**:
- Console: System info + summary statistics
- File: `results/bench_db_init_results.txt` with complete system information and all iteration timings

**Running**:
```bash
# Using the convenience script
./utils/run_bench.sh

# Or directly
./build/bench_db_init

# With custom output file
./build/bench_db_init /path/to/custom/output.txt
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
