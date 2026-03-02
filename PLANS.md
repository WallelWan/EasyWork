# PLANS.md — EasyWork 优先级重排计划（AI Coding + 嵌入式上线）

更新日期：2026-03-02

## 0. 目标与硬约束
- 开发态允许 Python（快速迭代）。
- 生产态禁止 Python 运行时（`EASYWORK_BUILD_PYTHON=OFF`），只允许 C++ Runtime + IR。
- 交叉编译与离线构建必须可用（`EASYWORK_FETCH_DEPS=OFF`）。
- AI 改造必须可被门禁脚本自动验证（构建、测试、回放、图校验）。

## 1. 当前已完成
- [x] CMake 目标拆分与可选依赖（Python/OpenCV 可选）。
- [x] runtime-only 构建路径 + `easywork-run`。
- [x] Python 导出 IR（`Pipeline.export()`）。
- [x] runtime contract tests 初版。
- [x] Presets 与 cross 模板文件。

## 2. 优先级重排

### P0（必须先做，阻塞上线）
1. 日志系统与可观测性最小闭环
- [ ] 增加统一 runtime logger（初始化、级别、输出目标、格式）。
- [ ] `easywork-run` 支持 `--log-level`、`--log-file`、`--log-format`（text/json）。
- [ ] 核心路径日志点：图加载、节点创建、连接、调度异常、停止原因、运行摘要。
- [ ] 文档定义日志字段（time/level/node/method/error_code/trace_id）。

2. 生产无 Python 强制门禁
- [ ] 增加 `scripts/check_production_graph.py`：禁止 Python 节点、非法参数、非法 schema。
- [ ] CI Prod Gate 强制 `EASYWORK_BUILD_PYTHON=OFF`。
- [ ] 产物清单门禁：禁止打包 Python 扩展。

3. Dev/Prod/Cross 三道门禁脚本
- [ ] `scripts/ci/dev_gate.sh`：full build + ctest + pytest。
- [ ] `scripts/ci/prod_gate.sh`：runtime-only build + ctest + runner smoke + graph 校验。
- [ ] `scripts/ci/cross_gate.sh`：cross configure/build；无工具链时明确 skip；版本不符时 fail-fast。

4. 交叉编译可用性与版本矩阵
- [ ] 写清最低编译器矩阵（host/cross）。
- [ ] CMake fail-fast 检查（版本过低直接报错并提示修复）。
- [ ] toolchain/sysroot 健康检查。

5. 错误码与稳定失败语义
- [ ] 定义错误码体系（代替纯字符串错误）。
- [ ] 统一异常到错误码映射。
- [ ] 日志/指标里带 error_code。

### P1（上线前必须完成）
1. 运行时确定性与资源控制
- [ ] 背压策略（阻塞/丢帧/降采样）标准化。
- [ ] 超时策略与队列上限策略。
- [ ] 内存预算与零拷贝约束说明 + 测试。
- [ ] 线程/CPU 亲和策略（部署 profile）。

2. 嵌入式回放验证闭环
- [ ] 回放包格式（binary + graph + sample + expected metrics）。
- [ ] target replay runner（输出吞吐/延迟/丢帧/错误）。
- [ ] host comparator（自动比对并门禁）。

3. IR 治理
- [ ] `schema_version` 兼容策略。
- [ ] 旧 IR 迁移工具。
- [ ] 变更日志与回滚策略。

4. 配置系统
- [ ] 统一配置来源（CLI/env/file）与优先级。
- [ ] 配置版本校验与默认值治理。

### P2（持续演进）
1. Python Node -> C++ Node AI 迁移体系
- [ ] `doc/node_contract.md`（类型、参数、时间戳、异常、顺序语义）。
- [ ] golden harness（Python 参考 vs C++ 实现）。
- [ ] 迁移 playbook（原型→翻译→验证→发布）。

2. 安全与供应链
- [ ] IR 输入安全校验与白名单。
- [ ] SBOM、签名、许可证检查、可复现构建。
- [ ] 插件/节点注册 allowlist。

3. 硬件加速路线
- [ ] CPU/OpenCV/CUDA/NPU/V4L2/GStreamer 抽象边界。
- [ ] 后端能力协商与 profile。
- [ ] 后端回放基线。

4. 团队与 AI 协作规范
- [ ] PR 模板 + 必跑门禁清单。
- [ ] AI 改动提交粒度规范。
- [ ] 受保护目录策略（toolchain/schema/release scripts）。

## 3. 里程碑

### M1（2026-03-12）— 生产可控最小闭环
范围：P0 的 1/2/3
验收：
- 有统一日志系统且 `easywork-run` 可配置日志输出。
- Prod Gate 可拦截 Python 节点与 Python 产物。
- Dev/Prod/Cross gate 脚本可在本地跑通。

### M2（2026-03-22）— 交叉编译可信
范围：P0 的 4/5
验收：
- 编译器矩阵写入文档并在 CMake 中 fail-fast。
- cross gate 对“缺工具链/版本不足/正常通过”三种情况行为明确。
- 错误码体系落地并接入日志。

### M3（2026-04-10）— 上线稳定性基线
范围：P1 的 1/2
验收：
- 背压/超时/队列策略可配置且有测试。
- target replay + host comparator 可自动出报告并门禁。

### M4（2026-04-25）— IR 与配置治理
范围：P1 的 3/4
验收：
- IR 版本兼容策略与迁移工具可用。
- 配置系统统一且有版本校验。

### M5（2026-05-20）— AI 迁移工程化
范围：P2 的 1
验收：
- node contract + golden harness 完整。
- 至少 1 个 Python 节点通过 AI 迁移到 C++ 并过全部门禁。

### M6（2026-06-20）— 安全与扩展能力
范围：P2 的 2/3/4
验收：
- 安全与供应链检查进入发布流程。
- 至少 1 条硬件加速 profile 完成回放验证。
- 团队 AI 协作规范执行落地。

## 4. 验证命令（当前基线）
Runtime-only:
```bash
cmake -S . -B build_rt -DEASYWORK_BUILD_PYTHON=OFF -DEASYWORK_WITH_OPENCV=OFF
cmake --build build_rt
ctest --test-dir build_rt --output-on-failure
./build_rt/easywork_runtime_example
./build_rt/easywork-run --graph /tmp/pipeline.json
```

Full/dev:
```bash
cmake -S . -B build_full -DEASYWORK_BUILD_PYTHON=ON -DEASYWORK_WITH_OPENCV=ON
cmake --build build_full
ctest --test-dir build_full --output-on-failure
PYTHONPATH=python python -m pytest tests
```

Cross:
```bash
cmake --preset cross-aarch64-release
cmake --build build/cross-aarch64-release
```

## 5. 已知风险
- 当前环境可能缺少 aarch64 交叉编译器。
- `tests/test_error_policy.py` 存在进程级 abort，需纳入 dev gate 例外与修复计划。
