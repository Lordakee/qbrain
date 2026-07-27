# Qbrain Project Rules (Hard Requirements for All Agents)

These rules are **mandatory** and written in English. Speed, user pressure to "continue", subagent timeouts, or gateway failures do **not** waive them. If a gate cannot be completed, **stop** and report the blocker; do not invent a PASS or skip the gate.

---

## 1. Platform and stack

- **OS**: Windows 11 native only. No WSL, no Docker as a required runtime.
- **Language**: C++20 (MSVC preferred).
- **Data root**: `%LOCALAPPDATA%\Qbrain\`
- **Primary scripts**: PowerShell first-class; do not make Linux-only scripts the main path.
- **Docs**: `docs/01-ANALYSIS.md`, `docs/02-DEVELOPMENT.md`, `docs/03-BUILD-WINDOWS.md`
- **Upstream inspiration**: https://github.com/garrytan/gbrain (TypeScript/Bun) — reimplemented, not vendored.
- **Build**: See `docs/03-BUILD-WINDOWS.md`. Prefer `scripts/build-cl.ps1` / `scripts/build-tests-cl.ps1` when CMake/SDK is incomplete.

---

## 2. Node development loop (HARD GATE)

Every capability wave is a **node** `N{k}` under `docs/nodes/`.

### 2.1 Required artifacts

| File | Purpose |
|------|---------|
| `docs/nodes/N{k}-PLAN.md` | Goals, ledger rows, deliverables, falsifiable acceptance, tests, rollback, security |
| `docs/nodes/N{k}-PLAN-AUDIT.md` | **Plan** hard audit (Claude Code) — required **before** implementation |
| `docs/nodes/N{k}-HARD-AUDIT.md` | **Outcome** hard audit (Claude Code) vs the **approved** plan — required **before** marking the node done |

Templates: `docs/nodes/_TEMPLATE-PLAN.md`, `_TEMPLATE-PLAN-AUDIT.md`, `_TEMPLATE-HARD-AUDIT.md`.  
Process: `docs/nodes/README.md`.

### 2.2 Ordered steps (no reordering)

1. **Draft plan** → write/update `N{k}-PLAN.md` with status `draft`.
2. **Plan hard audit (BLOCKING)** → Claude Code audits the plan only (scope, acceptance, security, testability, fit with master plan / ops ledger). Write `N{k}-PLAN-AUDIT.md`.
   - **VERDICT must be PASS** (or PASS with documented non-blocking P2 only).
   - On **FAIL**: revise the plan, re-audit. **Do not implement.**
3. Set plan status to **`approved`** only after plan audit PASS.
4. **Implement** only against the approved plan (code, tests, ledger updates).
5. **Outcome hard audit (BLOCKING)** → Claude Code audits implementation vs approved plan + runtime/unit evidence. Write `N{k}-HARD-AUDIT.md`.
   - **VERDICT must be PASS**.
   - On **FAIL**: fix, re-audit. Do not mark the node done.
6. Mark plan status **`done`**, update `docs/OPS-PARITY-LEDGER.md`. Commit/push only when the user asks (or standing push policy).

### 2.3 Forbidden

- Implementing while plan status is `draft` or plan audit is missing/FAIL.
- Writing `N{k}-HARD-AUDIT.md` or `N{k}-PLAN-AUDIT.md` with **PASS** without Claude Code, unless the **human user** explicitly waives that node in writing.
- Using "evidence-gate PASS", "optional re-check", or bare rubber-stamp text **instead of** a real plan or outcome audit.
- Skipping plan audit because the plan is "small" or "obvious".

### 2.4 Auditor

- Default: **Claude Code** (model `claude-opus-5`, high/max effort when using that stack).
- If Claude Code is unavailable (timeout, 502, Terms): **stop at the gate**, report the error, retry. Do not self-PASS.

---

## 3. Parallelism (required preference; does not waive gates)

### 3.1 Multi-node parallel development

- Run **as many independent nodes in parallel** as the dependency graph allows.
- Each parallel node still needs its **own** full loop: PLAN → PLAN-AUDIT PASS → implement → HARD-AUDIT PASS.
- Do not share one audit file across nodes.
- Serialize only for real conflicts (schema version, same hot files, plan FAIL).

### 3.2 Multi-subagent parallel development (within a node)

- After plan **approval**, prefer **multiple subagents in parallel** on disjoint slices (e.g. storage / handlers / tests / docs).
- Parent agent owns: both audit gates, merge, build, full test run, ledger.
- Subagents must not mark the node done or write a forged PASS audit.
- If subagents hang: kill, reassign, or finish in parent; **still complete audits**.

### 3.3 Throughput vs gates

Parallelism applies to **implementation after approval** and to **independent nodes**. It never authorizes skipping plan or outcome hard audits.

---

## 4. Audit quality bar

### 4.1 Plan audit (`N{k}-PLAN-AUDIT.md`)

Must include: **VERDICT**, **Auditor: Claude Code**, checklist (goal, acceptance, tests, ledger, security, dependencies, Windows/C++ fit), P0/P1/P2. **P0 blocks approval.**

### 4.2 Outcome audit (`N{k}-HARD-AUDIT.md`)

Must include: **VERDICT**, **Auditor: Claude Code**, acceptance table with **evidence**, deliverables check, P0/P1/P2, comparison to the **approved** plan. **P0 blocks done.**

---

## 5. Build, test, ledger

- Before outcome audit: build and run unit tests (`scripts/build-tests-cl.ps1` / `qbrain_tests.exe`).
- Update `docs/OPS-PARITY-LEDGER.md` when ops become implemented.
- Master plan: `docs/08-MASTER-PLAN-GBRAIN-PARITY.md`.
- Completion notes: `docs/09-PROJECT-COMPLETION.md` (descriptive only; does not override hard gates).

---

## 6. Security and product constraints

- MCP write default-deny unless `--allow-write` / explicit allow.
- No secrets in git.
- Loopback HTTP MCP + token where applicable.
- Usable gbrain-class capability is the product bar; ledger claims must match code + tests.

---

## 7. Git

- Do not commit or push unless the user asks (or an explicit standing order).
- Never commit secrets.

---

## 8. Rule precedence

1. This file (`AGENTS.md`)  
2. `docs/nodes/README.md`  
3. Master plan / ops ledger  
4. Convenience scripts and speed optimizations  

Higher precedence always wins. Silently weakening gates is a rule violation.
