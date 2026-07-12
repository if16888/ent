## Summary

<!-- One paragraph describing what this PR changes and why. -->

## Modified Files

<!-- List of files changed and the reason for each. -->

## Behaviour Change

<!-- Describe observable differences before and after this PR. -->

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

## Untested Items

<!-- List anything that could not be verified locally. -->

## Rollback Path

<!-- How to revert: commit hash(es) to revert, or steps to undo. -->
