# N33 Plan — 真实多模态摄取与搜索

**Status**: done
**Depends on**: N31 done；docs/08 v2.0.0 §4 N33；docs/RESOLUTION-2026-08-15.md（N33 定义含可证伪验收）
**Plan audit**: PASS round 2 (`N33-PLAN-AUDIT.md`)
**Outcome audit**: pending (`N33-HARD-AUDIT.md`)

## Goal

将 N26-N27 的文件名启发式 image 处理升级为**内容级**支持：PNG/JPEG 的魔数+结构解析（无外部依赖的自研最小解析器：PNG IHDR/尺寸/位深、JPEG SOF 帧），MIME 由内容判定（拒绝伪造扩展名）；记录尺寸/元数据入库（有界）；`search_by_image` 在有 embedding provider 凭据时走真实多模态向量（可选 embedding-provider 契约：OpenAI 兼容 images/embeddings 端点），无凭据时确定性 fail-open（文本搜索不受影响，返回结构化 unavailable 而非错误）；`put_raw_data`/`file_upload` 摄取图片时填充新元数据字段。**不**引入图像解码库依赖（仅头部解析，不解码像素——嵌入由 provider 完成，本地无凭据时无像素处理）。

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| search_by_image | N33: 内容级 MIME/元数据 + 可选 provider 多模态嵌入；无凭据 fail-open 结构化 unavailable |
| file_upload / put_raw_data / get_raw_data | N33: 图片元数据（宽/高/位深/格式）内容级填充（image 子集） |

## Deliverables

1. **D1 最小图像解析**: `src/qbrain/files/image_meta.cpp` + `include/qbrain/files/image_meta.hpp` — PNG（魔数+IHDR：宽/高/位深/颜色类型）、JPEG（SOI+APPn 跳过+SOF0/2：宽/高/分量数）；上限：文件 ≤32MiB、头部扫描 ≤64KiB；非图/畸形 → 无元数据+格式 "unknown"（摄取不失败）。
2. **D2 MIME 判定**: **手工内容魔数检测（P0-5 采纳，零外部依赖）**：读前 16 字节，PNG 签名 `PNG

` → image/png；JPEG `ÿØÿ` → image/jpeg；其余回退扩展名提示；伪造 fixture（.png 实为文本 / .jpg 实为 PNG）→ 按**内容**分类并标注 `declared_ext_mismatch: true`。
3. **D3 provider 契约（可选）**: `include/qbrain/ai/embed.hpp` 扩展 `embed_image(path|bytes)`：凭据存在（沿用现有 chat/embed 的 env 配置约定）→ POST base64 至配置端点，超时 30s、响应 ≤2MiB、失败降级 unavailable；无凭据 → 立即 unavailable（不网络请求）。**错误脱敏包装（P0-4 采纳）**：embed_image 失败消息剥离 base_url 与一切凭据材料并截断至 200 字符；负 fixture：provider 返回 401+WWW-Authenticate（含 realm/token）→ 输出不含 URL/凭据。确定性 mock provider（`QBRAIN_EMBED_MOCK=1` 扩展）：**图像内容前 4KiB 哈希 → 64 位种子 → 确定性 RNG 生成 N 维向量（P1-2 采纳）**。
3b. **凭证隔离**: provider 调用错误消息不含 URL/密钥片段。
4. **D4 摄取集成（P0-1 采纳，范围收窄）**：图片元数据**仅存储于 raw_data 的既有 JSON 字段**（put_raw_data/get_raw_data 路径）；`file_upload` 保持现有 file_index 存储（仅 MIME-by-content 与 size），**解析出的元数据仅出现在响应中（临时性，不落库）**；**无任何 schema 迁移、无新列**；`search_by_image`: 有凭据→真向量+余弦；无凭据→`{"results":[],"mode":"unavailable","reason":"no provider credentials"}` 且 exit 0（fail-open；P2-2 采纳）。**32MiB 上限执行点（P1-1 采纳）**：file_upload 与 put_raw_data 的 handler 入口、磁盘写入之前检查；仅作用于图片元数据提取尝试的输入，非图片上传不受影响。
5. **D5 fixtures+测试**: `tests/fixtures/img/`（合法 PNG/JPEG 各 ≥2、伪造扩展 ≥2、畸形截断 ≥2、超限大文件 1 个用构造头+填充）；`tests/test_n33.cpp`。
6. **D6 证据**: n33-evidence/（PRE-GATE；双路径两轮全绿；mock provider 行为证明）。

## Tests

- 元数据精确匹配（已知尺寸的构造图像）；MIME 伪造检出；畸形/超限有界处理；mock 向量确定性（同图两次同向量）；无凭据 fail-open 响应形状；provider 失败注入（端点不可达）→ unavailable 且文本 search 正常；泄漏检查（错误消息无 URL/密钥）。
- 全套件为当前注册数（33）+ test_n33 注册项，精确计数以可执行输出为准记录于 n33-evidence（AMD-6；P0-3 采纳）。

## Acceptance assertions (falsifiable)

1. 合法 PNG/JPEG fixture 的解析元数据与预置真值精确相等（宽/高/位深/格式）。
2. 伪造扩展 fixture → 内容分类 + `declared_ext_mismatch:true`。
3. 畸形/截断/超限 → 不崩溃、格式 unknown 或部分元数据 + 有界原因标注。
4. `QBRAIN_EMBED_MOCK=1` 下同图两次 `search_by_image` 向量 byte-identical；不同图向量不同。
5. 无凭据（清空相关 env）→ `search_by_image` 返回 mode=unavailable + exit 0；同 brain 文本 `search` 结果不受影响。
6. provider 失败注入（mock 端点拒绝）→ unavailable + 无 URL/密钥泄漏。
7. 既有 files/raw_data 测试零修改通过（附加字段不破坏）。
8. 全套件双路径两轮全绿（= 33 + test_n33 注册数，精确值来自可执行输出并记录）。

## Rollback

- image_meta 与 embed_image 均为新增路径；摄取元数据为附加 JSON 字段；无 schema 迁移；整节点可独立回退。

## Security notes

- 图像为不可信输入：头部扫描有界（**PNG chunk ≤1000、JPEG marker ≤100，未在限内找到 IHDR/SOF → format=unknown，P2-3 采纳**）、无像素解码、无执行；provider 凭据仅从 env、不入库不入日志；网络仅在有凭据时发生且限配置端点。**向后兼容（P2-1 采纳）**：meta_json 消费者检查字段存在性，缺失按非图片处理。

## Parallelism notes

- 与 N32/N34 并行（所有权：files/image_meta.*、ai/embed 图像扩展、tests/test_n33*、fixtures/img 独占；**不触碰** handlers.cpp 的 code ops 与 jobs 路径、codeintel/**）。handlers.cpp 内 files/raw_data/search_by_image 段仅本节点改；N32 改 code ops 段、N34 改 jobs 段，三段结构不相交。**新模块（astlite vs image_meta）完全并行；handlers.cpp 的集成步骤由父代理串行合并（AMD-3 共享热文件；P0-2 采纳）**。
- 子代理切片：A=image_meta+fixtures；B=集成+embed_image+test_n33；父代理合并。
