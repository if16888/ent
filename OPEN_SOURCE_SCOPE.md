# ent Open-Source Scope

This repository publishes the **ent core**: a small, reusable, cross-platform C infrastructure library.

本文档定义 `ent` 公开仓库的长期功能边界。公开版本保持完整、可构建、可测试和可独立使用，但只覆盖通用基础能力；分布式、高可用和工业级扩展不属于本仓库范围。

## Public core

The public repository may contain and evolve the following capabilities:

- handle-based runtime initialization, execution, stop, close, and multi-instance isolation;
- message-code generation and public error semantics;
- logging, flush, rotation, and basic asynchronous writer support;
- threads, mutexes, condition variables, thread pools, and timers;
- basic socket and dynamic-library wrappers;
- SQLite, MariaDB/MySQL, and PostgreSQL access wrappers with optional build-time backends;
- optional Lua scripting support;
- local shared-memory mapping with explicitly documented lifecycle and synchronization requirements;
- CMake packaging, examples, tests, sanitizers, and release validation.

Public modules must remain independently buildable and must not require private repositories, private binaries, credentials, or private CI infrastructure.

## Shared-memory boundary

The public shared-memory module is a **local IPC primitive**, not a distributed state platform.

The public contract is limited to the behavior explicitly documented and tested in this repository, including:

- local named mapping/open/close operations;
- caller-synchronized lifecycle management;
- sequential close and handle invalidation;
- basic flush and size/pointer access according to the public API contract.

The public module does not promise multi-node consistency, replication, multi-primary writes, lock-free multi-writer behavior, transactional cross-process updates, or real-time distributed synchronization.

## Outside the public scope

The following categories are intentionally outside this repository and are maintained separately when needed:

- active/standby or primary/backup coordination;
- leader election, quorum, fencing, split-brain prevention, or failover orchestration;
- snapshot, journal, incremental, or cross-node data replication;
- advanced shared-memory engines, including multi-writer, RCU, version-chain, transactional, or lock-free designs;
- distributed synchronization and industrial real-time state replication;
- libevent, libev, or other event-loop integration layers;
- RPC frameworks or adapters, including XDR, gRPC, service discovery, retry, flow-control, or streaming layers;
- industrial high-availability protocols, operations tooling, licensing, or enterprise deployment control.

This list defines a scope boundary, not a public roadmap. The repository must not add placeholder APIs, unfinished stubs, speculative design documents, or TODO items for excluded capabilities.

## Dependency direction

The architectural dependency direction is one-way:

```text
private or product-specific extensions
        depend on
public ent core
```

The public core must never depend on a private extension. In particular:

- public headers must not include private headers;
- public CMake targets must not resolve private packages;
- public CI must not require private artifacts or secrets;
- public examples and tests must remain runnable using only public sources and declared third-party dependencies.

## Contribution policy

Feature requests and pull requests are evaluated against this scope before API design or implementation review.

A proposal is suitable for this repository only when it:

1. provides a generally reusable core capability;
2. has a clear public lifecycle and error contract;
3. can be tested on the supported public CI matrix;
4. does not expose or depend on excluded product-specific architecture.

Requests outside this boundary may be closed as out of scope without implying that the use case is invalid.

## Documentation and history policy

Public branches, tags, releases, issues, and documentation must describe only the public core. Internal product plans, private architecture, private dependency details, and excluded implementation designs must not be copied into this repository.

Before a public release, scope review is part of the release gate together with code quality, CI, packaging, licensing, secret scanning, and third-party ownership review.

## Stability

`ent` remains a `0.x / Preview` project. Only behavior explicitly documented by public headers, public documentation, and tests is part of the supported contract. API and ABI changes may occur before `1.0` and must be documented in release notes.
