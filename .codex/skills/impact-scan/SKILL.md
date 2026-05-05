---
name: impact-scan
description: Use this skill when analyzing repository impact before implementation. It must be read-only and must not modify files.
---

# Impact Scan

在实现前做只读影响面扫描，适用于 runtime、log、`ent.msg`、db、thread / tpool / timer、shm、public header、CI 等高风险变更。

## 强制约束

1. 读取 `AGENTS.md`。
2. 读取 `docs/ai/AI_GUARDRAILS.md`。
3. 不修改文件。
4. 不提交。
5. 不创建补丁草案。

## 扫描范围

- 相关 public header。
- 相关 `comm/` 实现。
- 相关 `test/` 测试。
- `msg/ent.msg` 和生成消息码使用点。
- CMake、CI、example、docs 中的调用方或构建入口。

## 建议命令

```bash
git status --short
rg "<symbol-or-prefix>" inc comm test msg docs .github CMakeLists.txt
rg "return\\s+[-]?[0-9]+\\s*;" comm inc test
git diff --stat
```

## 输出格式

### 影响面表格

| File | Function/Symbol | Current Behavior | Risk | Recommendation |
| --- | --- | --- | --- | --- |
| | | | | |

### 调用链

```text
入口：
核心函数：
下游调用：
测试覆盖：
CI 覆盖：
```

### 风险判断

```text
风险等级：
主要风险：
兼容性影响：
跨平台影响：
测试缺口：
```

### 推荐分阶段实施计划

```text
阶段 1：
阶段 2：
阶段 3：
```

### 验证命令

```text
本地命令：
CI 验证：
未验证项：
```
