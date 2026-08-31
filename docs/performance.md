# Performance baselines

Phase 6 adds an internal benchmark-style executable: `rmc_fabric_benchmarks`.

It currently records structured timing for:

- inventory manager refresh on representative synthetic source/field counts
- inventory D-Bus codec encode/decode
- network-observation candidate codec encode/decode
- observation event fan-out across multiple transports
- observation service start/stop latency

## Methodology

- build with the `benchmark` preset (`Release` + `RMC_FABRIC_BUILD_BENCHMARKS=ON`)
- run on an otherwise idle machine
- keep results as local or CI artifacts; do not treat one machine's absolute values as universal limits
- compare relative regressions over time instead of single absolute numbers

Example command:

```bash
cmake --preset benchmark
cmake --build --preset benchmark
./build/benchmark/tests/benchmarks/rmc_fabric_benchmarks > benchmark-results.jsonl
```

The output format is one JSON object per benchmark so CI or local tooling can archive
and diff it without scraping human-oriented text.
