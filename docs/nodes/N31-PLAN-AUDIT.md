# N31 PLAN AUDIT (round 1 — FAIL, revised)

**Auditor**: Claude Code (claude-opus-5, effort max), watchdog task qbrain-n31-plan-audit-claude-20260815, 2026-08-15
**Audit object**: docs/nodes/N31-PLAN.md (draft as dispatched)
**Human authorization**: user instruction 2026-08-15 (quoted verbatim in dispatch log)

---

The audit is complete. The findings summary:

**VERDICT: FAIL** — 1 P0 + 2 P1 findings require plan text fixes before approval.

**What needs to change in the plan before approval:**

**P0-1 (required)**: Correct the registry count in AA1/D1/D2 from "104" to the verified count. `grep -c '^  register_one(' handlers.cpp` returns **108**. `capture`, `list_brains`, `run_dream` are confirmed registry members (lines 792/1283/1444). Either change the target to 108 and expand the inventory to all registered ops, or define an explicit gbrain-op predicate that filters extensions out of the comparison and state it in the plan.

**P1-1 (required)**: Add a section-level ownership matrix for `test_n31.cpp` to the Parallelism notes (A owns count/mapping functions; C owns negative-test functions; parent merges), matching the AMD-3 pattern N30 used for `http_server.cpp`.

**P1-2 (required)**: Specify in D6 whether unknown-field rejection tests target only ops already using `validate_allowed_args` (no new code) or require adding central schema enforcement to `registry::call()` (new scoped deliverable). The two paths have materially different implementation scope.

**P2-1 through P2-4** are non-blocking; the controller can adopt or skip them at discretion.

Once those three items are addressed and the plan updated, re-submit for a re-audit pass.


---

# Round 2 (task qbrain-n31-plan-audit2-claude-20260815)

The round-2 audit is complete. Summary:

**VERDICT: FAIL** — 1 new P1 blocks approval.

- **P0-1, P1-1, P1-2**: all resolved correctly by the controller's adopted changes.
- **P1-NEW-1**: The "每域 ≥2" claim in D6/AA5 is unverifiable against the D4 domain split. `files`, `pages`, and `search` domains have zero `validate_allowed_args` ops in the current codebase (confirmed by grep). AA5 as written cannot pass for those domains, making it a structurally unfalsifiable acceptance assertion.

The fix is a single paragraph in D6 and a matching AA5 edit: enumerate the three qualifying domains (jobs, schema, chronicle/misc) by name and explicitly document pages/search/files as out of scope for D6. Once that text change is applied the plan is ready for a round-3 re-audit.


---

# Round 3 — VERDICT: PASS (task qbrain-n31-plan-audit3-claude-20260815)

**VERDICT: PASS**

---

**P1-NEW-1 fix verification — CORRECT**

D6 (line 31): "限定合格域为 **jobs、schema、chronicle/misc**（每域 ≥2）；**pages/search/files 域当前无 validate_allowed_args op，明确列为 D6 范围外**（P1-NEW-1 采纳）" — exactly the required text: three qualifying domains named explicitly, pages/search/files explicitly excluded with stated rationale.

AA5 (line 55): "合格域（jobs、schema、chronicle/misc）各 ≥2 个 op 的未知字段/错误类型负测试返回结构化 `invalid_argument`（MCP 路径）且一致；pages/search/files 域为 D6 明确范围外（无既有校验点）。" — mirrors D6 precisely; the assertion is now falsifiable against a named, bounded domain set.

---

**Prior findings status**

- P0-1: remains resolved — registry count deferred to runtime PRE-GATE (value N), all four-way consistency assertions reference N throughout AA1/D1/D2. ✅
- P1-1: ownership matrix present at lines 33–39 (n31-a: A owns counts/mapping; n31-c: C owns negatives; parent merges). ✅
- P1-2: D6 explicitly excludes registry::call central schema enforcement as out of scope. ✅

---

**New scan — no P0 or P1 found**

One non-blocking observation: D6 lists "非法 source/brain 引用一致错误" as a sub-deliverable but no AA maps to it (AA5 covers only unknown-field/wrong-type paths; AA4 covers duplicate registration). This is a **P2** documentation gap — the implementing agent can satisfy D6 fully without a corresponding AA to falsify. Suggested (non-blocking): add "非法 source/brain 引用在合格域返回结构化错误" as a bullet in AA5. Controller may adopt or skip at discretion.

---

**Summary.** The P1-NEW-1 fix is correctly and completely applied in both D6 and AA5: qualifying domains are enumerated by name, pages/search/files are explicitly out of scope, and the "每域 ≥2" claim is now bounded to a falsifiable, codebase-verified set. All three prior blocking findings (P0-1, P1-1, P1-2) remain correctly resolved. The only new finding is a P2 gap (no AA for the "illegal source/brain reference" sub-deliverable in D6). The plan is approved to proceed to implementation.
