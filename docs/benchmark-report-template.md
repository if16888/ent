# Benchmark Report Template

This template is for ent benchmark and performance smoke reports.
It keeps result evidence reproducible without forcing a benchmark framework.

## Title

- Benchmark name:
- Target module:
- Report owner:
- Date:

## Commit

- Commit hash:
- Branch:
- PR / change reference:

## Environment

- OS:
- Kernel / Windows version:
- CPU model:
- CPU core count:
- Memory:
- Disk type:
- Repository path:
- Test path:

## Build

- Generator:
- Architecture:
- Build type:
- Toolchain:
- CMake version:
- Compiler version:
- Feature flags:

## Benchmark Parameters

- Benchmark command:
- Inputs / sizes:
- Warm-up runs:
- Sample count:
- Iterations per sample:
- Concurrency:
- Cold or warm cache:

## Data Files

- CSV path:
- Markdown report path:
- Raw log path:

## CSV Schema

The CSV file should keep one row per sample.

Suggested columns:

- `sample_id`
- `benchmark`
- `commit`
- `os`
- `arch`
- `iterations`
- `duration_ms`
- `throughput`
- `latency_min`
- `latency_max`
- `latency_avg`
- `latency_median`
- `latency_p95`
- `status`
- `invalid_reason`

## Sample Summary

- Valid samples:
- Invalid samples:
- Invalid sample reasons:

## Statistics

- Min:
- Max:
- Avg:
- Median:
- P95:

## Result Notes

- What changed:
- What did not change:
- Any outliers:
- Any environmental caveats:

## Reproduce

```bash
<exact command here>
```

## Validation

- `git diff --check`
- Benchmark command output:
- CI run / artifact:

## Known Gaps

- Unsupported machine / OS combinations:
- Missing metadata:
- Any unverified items:

## Conclusion

- Smoke result:
- Performance result:
- Recommendation:

