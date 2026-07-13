## Summary

<!-- One paragraph describing what this PR changes and why. -->

## Public Scope Check

<!-- Read OPEN_SOURCE_SCOPE.md before implementation. -->

- [ ] This change belongs to the reusable public ent core.
- [ ] It does not add or expose high availability, election, replication, advanced shared memory, distributed synchronization, event-loop integration, RPC, or private product infrastructure.
- [ ] Public headers, CMake targets, tests, examples, and CI remain independent of private repositories, binaries, services, and artifacts.
- [ ] The PR does not add placeholder APIs, speculative roadmap text, or TODOs for excluded capabilities.

## Modified Files

<!-- List of files changed and the reason for each. -->

## Behaviour Change

<!-- Describe observable differences before and after this PR. -->

## API / ABI and Lifecycle Impact

<!-- State whether public headers, exported symbols, lifecycle, threading, error semantics, or package dependencies change. -->

## Verification

<!-- Commands you actually ran, with their output or a summary. -->

```
git diff --check
cmake --build <build-dir> -j4
ctest --test-dir <build-dir> --output-on-failure
```

## Platform Impact

| Platform | Tested | Notes |
|---|---|---|
| Linux x64 | | |
| Windows x64 | | |
| Windows x86 | | |

## Release / Package Impact

<!-- State whether runtime/devel archives, CMake exports, third-party notices, or release validation change. -->

## Untested Items

<!-- List anything that could not be verified locally. -->

## Rollback Path

<!-- How to revert: commit hash(es) to revert, or steps to undo. -->
