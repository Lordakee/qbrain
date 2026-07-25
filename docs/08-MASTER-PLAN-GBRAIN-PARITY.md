# Qbrain ↔ gbrain 全功能对齐 — 整体规划（Master Plan）

**版本**: 1.1.0  
**日期**: 2026-07-25  
**状态**: REVISED after Claude Code N0 hard audit (`docs/reviews/CLAUDE_MASTER_PLAN_HARD_AUDIT.md` → PLAN_REVISE; all P0 adopted)  
**目标**: 纯 Windows 11 原生 C++ 下，Qbrain 达到 gbrain **能力对等**（非位级移植）。

---

## Changelog

| Ver | Change |
|-----|--------|
| 1.0.0-draft | Initial plan |
| 1.1.0 | Adopt N0 audit P0s: embedded schema, ops ledger, write-default-deny retained, evidence §1.4, node DAG (N2.5, N4a/N4b), abort rule, N6 stretch option |

---

## 0. 用户需求

1. Qbrain 拥有 gbrain 的所有**产品功能**（能力对齐）。  
2. 节点循环：计划 → Claude 审 → 采纳/更新 → 开发 → **硬审核** → 下一节点。  
3. 先整体规划硬审；最后全项目硬审。  
4. 无 WSL / 强制 Docker；C++。

---

## 1. 「所有功能」操作定义

**能力对齐**：D1–D25 每域达到可证伪的「可用」；ops 以 `docs/OPS-PARITY-LEDGER.md` 为账本。  
**非** 132 ops 一次性实现，但每节点必须移动账本行；`out-of-scope` 必须有理由。

### 1.2 能力域 + 验收句

| 域 | 能力 | 节点 | 可用定义（一句） |
|----|------|------|------------------|
| D1 | 页面 CRUD/版本 | N2 | put/get/list/delete/restore + 版本表可回滚 |
| D2 | capture/溯源 | N1 | capture CLI+MCP(allow-write)；source_kind 可查 |
| D3 | hybrid 检索 | N3 | 词+向量 RRF；recency/title/backlink 可观测 |
| D4 | rerank/autocut/模式 | N3 | 至少一种 rerank 或显式 stub + 文档 |
| D5 | 图谱 | N2 | 抽边+邻接+backlinks |
| D6 | think | N4b | 多轮可选；save 策略与 remote 一致 |
| D7 | MCP stdio | done | NDJSON；tools/list+call |
| D8 | MCP HTTP | N7 | loopback+token；admin 默关 |
| D9 | 写后 embed | N1 | 入队 jobs；有 key 时可 drain |
| D10 | inbox 监视 | N5 | 目录新文件自动 import |
| D11 | live-sync | N5 | 监视目录增量同步 |
| D12 | webhook | N5 | 本地 HTTP 收 markdown |
| D13 | IngestionSource | N5–N6 | 插件接口+≥1 实现 |
| D14 | Minions | N6 | claim/complete worker |
| D15 | Dream/cron | N6* | 至少 1 阶段 dry-run；可 descoped |
| D16 | doctor remediate | N6 | 子集自动修 |
| D17 | multi-brain | N8 | mount 路由 |
| D18 | multi-source | **N2.5** | source 校验+远程 allow-list |
| D19 | Skills | N9 | RESOLVER+list/get_skill |
| D20 | code-intel | N10 | 1–2 语言 tree-sitter |
| D21 | 多模态 | N10 | 可选/可 out-of-scope |
| D22 | facts/trajectory | N10 | 子集 |
| D23 | 评测 | N11 | 固定语料+回归 |
| D24 | AI 网关 | **N4a** | 多 base_url/recipe |
| D25 | 迁移/发布 | N11 | 迁移框架+文档 |

\* N6 dream 可为 v1.1 stretch；须在账本记录，不可静默放弃。

### 1.3 边界

- Windows 等价物替换 unix socket。  
- 自升级（D25 子集）可 out-of-scope 并写理由。

### 1.4 证据来源

| 域 | 置信度 |
|----|--------|
| D1–D9,D18 部分 | 源码已读（Qbrain + 部分 gbrain 镜像） |
| D10–D16,D19–D22 | **部分镜像缺失**（minions/ingestion 仅 barrel）；N5 前须补镜像或从公开文档重推并标注 |

---

## 2. 架构目标态

（同 v1.0：CLI + MCP stdio/HTTP + ops registry + SQLite + search + graph + ingest + jobs + cycle + ai + skills）

---

## 3. 节点 DAG

```
N0 ──► N1 ──► N2 ──► N2.5 ──► N3 ──► N4b
              │         │
              ├─────────┴──► N4a ──► N6* ──► N10
              │                      │
              ├──► N5 ───────────────┘
              ├──► N9 (after N1)
              └──► N7 (after N4a) ──► N8 ──► N11
```

| 节点 | 名称 | 依赖 |
|------|------|------|
| N0 | 规划+schema 基础 | — |
| N1 | 写入闭环（embed 队列；**保持 MCP write 默认拒绝**；provenance） | N0 |
| N2 | 页面/图谱契约 | N1 |
| N2.5 | source 轴 | N2 |
| N3 | 检索 parity | N2.5 |
| N4a | AI gateway | N1 |
| N4b | think 增强 | N3,N4a |
| N5 | 自动摄入 | N1,N2 + 证据补全 |
| N6 | jobs/dream* | N4a,N5 |
| N7 | HTTP MCP | N4a |
| N8 | multi-brain | N2.5 |
| N9 | skills | N1 |
| N10 | 高级 | N3,N6 |
| N11 | 质量+终审 | 全部 declared |

### 节点流程

1. `docs/nodes/N{k}-PLAN.md`  
2. Claude 审 → 采纳更新  
3. 实现+测试  
4. `N{k}-HARD-AUDIT.md` 硬审  
5. **FAIL → 一轮修订 → 再 FAIL 则缩 scope**（禁止无标记自审关闭安全/schema 节点）  
6. PASS → 下一节点  

---

## 4. 全局门槛

- 无 WSL/Docker；无密钥入库  
- 计划一致；单测/smoke  
- 迁移；文档  
- **网络节点**：默认 bind、鉴权、argv 禁 token  
- **账本**：节点只声明移动的行  

---

## 5. N1 决策（采纳审计）

| 项 | 决策 |
|----|------|
| MCP write | **保持默认 deny**；`--allow-write` 显式开启（与 gbrain 差异写入 README） |
| capture on MCP | 允许作为 **扩展**（gbrain capture 为 CLI-only） |
| put_page remote | 默认仍 local_only；allow-write 时：跳过 auto-link **或** 仅 markdown 链 + 审计日志（N1 实现） |
| 写后 embed | **入队 jobs**，不阻塞写；`embed --drain` 或 N6 worker 消费 |
| 无 key | 写成功，不入队失败 |

---

## 6. 风险

| 风险 | 缓解 |
|------|------|
| N6 过大 | 可 stretch；dry-run；provenance 可批量回滚 |
| 向量规模 | N3 声明语料上限或 ANN |
| 审核不稳定 | 落盘；禁止无标记自审关安全节点 |

---

## 7. N0 完成标准（本轮）

- [x] 主规划修订 1.1.0  
- [x] 嵌入式 schema + 删除 fallback DDL + migrate v3 修复索引  
- [x] doctor schema integrity  
- [x] OPS-PARITY-LEDGER.md  
- [x] nodes/ 模板  
- [x] MCP 审核 framing SUPERSEDED 标记  
- [ ] 重建测试 + doctor on fresh CWD  
- [ ] N0 硬审复审 PASS  

---

## 8. 基线

- MVP：search/put/think/MCP NDJSON  
- Embedding：智谱；Chat：可配  
