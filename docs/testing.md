# Testing

## Test matrix

| Label | Scope | Default |
|---|---|---|
| `unit` | service, contract, codec, lifecycle, file-source tests | yes |
| `integration` | hermetic private-bus end-to-end tests | opt-in |
| `architecture` | boundary and transport-isolation checks | yes |
| `resilience` | lifecycle failure, restart, degradation, and recovery tests | yes |
| `soak-short` | bounded repeated start/stop and fd-stability smoke | yes |
| `package` | install-tree consumer and runtime-layout verification | yes |
| `fuzz-smoke` | bounded libFuzzer smoke runs | opt-in |

## Common commands

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

### Integration

```bash
cmake --preset integration
cmake --build --preset integration
ctest --test-dir build/integration -L integration --output-on-failure
```

### Fuzz smoke

```bash
cmake --preset fuzz
cmake --build --preset fuzz
ctest --test-dir build/fuzz -L fuzz-smoke --output-on-failure
```

### Package/install verification

```bash
cmake --preset release-package
cmake --build --preset release-package
ctest --test-dir build/release-package -L package --output-on-failure
cpack --config build/release-package/CPackConfig.cmake -G TGZ
```

### Resilience / soak

```bash
cmake --preset dev
cmake --build --preset dev --target inventory_service_tests network_observation_service_tests service_soak_short_tests
ctest --test-dir build/dev -L resilience --output-on-failure --no-tests=error
ctest --test-dir build/dev -L soak-short --output-on-failure --no-tests=error
```

### Benchmarks

```bash
cmake --preset benchmark
cmake --build --preset benchmark
./build/benchmark/tests/benchmarks/rmc_fabric_benchmarks
```

## Notes

- Integration tests start a private `dbus-daemon`; they do not touch the host system bus.
- Fuzz targets are bounded smoke checks in CI and are not intended to run indefinitely there.
- Package verification installs into a temporary prefix, validates runtime assets, and builds public consumers against that install tree.
- Scheduled/manual CI runs extend soak, fuzz, and benchmark coverage beyond bounded PR jobs.
