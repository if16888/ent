# CI Triage

本文档定义 ent 仓库 CI 失败排障规则。目标是先定位事实，再做最小安全修复，禁止用无关重构掩盖失败。

## CI 失败分类

| 类型 | 判定线索 | 处理方向 |
| --- | --- | --- |
| 代码问题 | 编译错误、断言失败、确定性测试失败 | 最小代码或测试修复，补充本地复现 |
| 测试假设问题 | 测试依赖顺序、时间、路径或环境状态 | 修正测试前提，避免弱化断言 |
| 环境依赖问题 | 缺少 cmake、编译器、数据库库、DLL、包 | 明确依赖，修正安装步骤或 CI 环境 |
| 平台差异问题 | 只在 Windows 或 Linux 失败 | 分析路径、权限、shell、换行、toolchain、依赖版本 |
| flaky 问题 | 重跑结果不稳定、时间竞争、并发敏感 | 找 race 或超时根因，不简单加 sleep |
| 超时问题 | job 或测试超过 timeout | 找阻塞点、死锁、等待条件，不直接扩大 timeout |
| 权限 / 路径问题 | access denied、path not found、long path、working directory 错误 | 修正路径构造、目录创建、权限和 cleanup |

## 排障顺序

1. 先看失败 job 和失败平台。
2. 再看第一处真实错误，不从最后一屏日志倒推。
3. 区分 warning 和 error。
4. 区分编译失败、链接失败、测试失败、环境失败。
5. 找到最小复现命令。
6. 只修改最小必要范围。
7. 不做无关重构。
8. 修复后记录未验证项和需要 CI 再验证的平台。

## 常见阶段判断

- Configure 失败：优先看 CMake options、依赖发现、toolchain、路径。
- Build 失败：优先看编译器错误、include path、宏、导出符号。
- Link 失败：优先看库架构、符号导出、依赖传播、Windows import lib。
- Test 失败：优先看第一个失败测试、工作目录、临时文件、返回码断言。
- Install / consumer 失败：优先看 CMake package export、include install、target link interface。
- Package / release 失败：优先看 artifact 路径、组件、runtime DLL、权限。

## 输出模板

```text
CI 链接：
失败平台：
失败阶段：
第一处关键错误：
判断类型：
最小复现命令：
最小修复建议：
需要补充的测试：
未验证项：
```

## 禁止事项

- 不把 CI 红灯描述为已通过。
- 不在未定位第一处关键错误前开始重构。
- 不通过删除测试、跳过 job、放宽断言来掩盖失败。
- 不把环境失败算作代码验证通过。
- 不把只在一个平台验证过的修复描述为跨平台已验证。
