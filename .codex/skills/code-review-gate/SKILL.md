---
name: code-review-gate
description: Use this skill when reviewing a branch or diff before merge. It must prioritize correctness, API/ABI stability, lifecycle safety, tests, and cross-platform behavior.
---

# Code Review Gate

对分支或 diff 做合并前 review。默认只读，不修改代码。

## 基准

- 默认审查当前分支相对 `origin/master`。
- 如果用户指定 base，以用户指定 base 为准。
- 如果没有 `origin/master`，使用可用的 merge-base 并说明限制。

## 关注重点

- API / ABI：public header、导出符号、结构体布局、返回语义。
- 生命周期：init / close、重复 close、失败路径释放、并发 close。
- 错误码：`ent.msg`、裸数字、OK / invalid / busy / fatal / nonfatal。
- 并发：锁、条件变量、active operation、线程退出。
- 资源释放：内存、文件句柄、mapping handle、socket、DB handle。
- 跨平台：Windows / Linux 路径、权限、条件编译、toolchain。
- 测试完整性：失败路径、边界、重复调用、CI 环境。

## 输出分级

```text
P0 必改：
P1 建议：
P2 优化：
验证依据：
未验证项：
结论：
```

## 规则

- 不要只给摘要，findings 优先。
- 每个 finding 尽量给出文件和行号。
- 如果没有发现问题，也必须列出验证依据和未验证项。
- 不要替实现者修代码，除非用户明确要求进入修复阶段。
