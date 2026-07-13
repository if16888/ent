---
name: Feature Request
about: Suggest a new public-core capability or API change for ent
title: "[Feature] "
labels: enhancement
assignees: ''
---

## Scope Check

<!-- Read ../../OPEN_SOURCE_SCOPE.md before filing.
     Confirm that this request belongs to the reusable public core and does not
     require or expose high availability, election, replication, advanced
     shared memory, distributed synchronization, event-loop integration, RPC,
     or private product infrastructure. -->

- [ ] I reviewed `OPEN_SOURCE_SCOPE.md`.
- [ ] This request does not depend on private repositories, binaries, services, or CI artifacts.
- [ ] This request does not introduce placeholder APIs or roadmap items for excluded capabilities.

## Problem / Motivation

<!-- Describe the generally reusable problem you are trying to solve or the
     use-case that is currently impossible or awkward with the public core. -->

## Proposed API or Behaviour

<!-- If you have a concrete proposal, show a sketch of the API surface and its
     lifecycle, error, threading, and platform contract. -->

```c
/* Example */
MSG_ID_T ENT_NewFunction(ENT_HANDLE handle, ...);
```

## Public-Core Justification

<!-- Explain why this capability belongs in a small cross-platform C
     infrastructure library rather than a product-specific or private layer. -->

## Test and Platform Plan

<!-- List expected Linux/Windows tests, failure paths, concurrency cases, and
     any optional dependency impact. -->

## Alternatives Considered

<!-- Other approaches you have considered and why you prefer this one. -->

## Additional Context

<!-- Links to related public issues, prior art, or documentation. Do not include
     private architecture, customer information, internal product plans, or
     private repository links. -->
