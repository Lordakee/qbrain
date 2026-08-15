# N32 astlite fixtures (D3)

Golden inputs plus expected `astlite::to_json(parse_content(...))` output for
the bounded structured parser (`src/qbrain/codeintel/astlite.cpp`,
`include/qbrain/codeintel/astlite.hpp`). Each `.json` file is the exact,
byte-identical serialization the parser produces for the paired source file;
fixtures are validated by parsing the input twice and comparing both runs
against the expected JSON (determinism + regression lock).

Language mapping: `.cpp` -> `astlite::Language::Cpp`, `.ts` ->
`astlite::Language::TypeScript`.

## Files

| fixture | language | covers | mode / degraded_reason |
|---------|----------|--------|------------------------|
| `cpp_basic.cpp/.json` | C++ | namespaces (nested), classes/structs, ctors/dtor, method + function overloads, static methods, templates (function + class), out-of-line methods, calls, references | structured / "" |
| `cpp_traps.cpp/.json` | C++ | comment traps, code in plain/raw strings, char literals (`'{'`, `'\''`), digit separators, preprocessor with line continuation, `extern "C"`, namespace alias / using-namespace, forward declarations, ctor member-initializer lists, pure virtual, `= default`, operator overloads (in-class and out-of-line), default-argument calls, member/arrow/macro calls | structured / "" |
| `cpp_depth_over.cpp/.json` | C++ | 70 nested blocks -> depth over limit; partial result cut at the deterministic 65th brace push (def `outer` + 63 `step` calls) | heuristic / "depth-limit" |
| `cpp_timeout_trigger.cpp/.json` | C++ | synthetic time-budget trigger: exactly 1000 lines, ~2 MiB, 60 nested structs, lexeme-dense comment/string filler (no identifiers). Every definition body is closed via the bounded brace rescan (N22-style), so total work is ~60 x 2 MiB; the single 1000-line time sample fires at EOF with the complete symbol table (60 defs). Measured parse time on the dev machine (cl /O2): 0.8-1.2 s vs the 50 ms budget (~16-23x margin). | heuristic / "timeout" |
| `ts_basic.ts/.json` | TS | export/default functions, class with constructor/get/static/private `#`/async methods, const arrows (typed params, single-param, async), plain const/let (reference only), function overloads, calls incl. `new` | structured / "" |
| `ts_traps.ts/.json` | TS | comments with code, code in strings, template literals with `${}` interpolation and nested templates (skipped as literal), tagged templates (recorded as call), interface/enum bodies (references only), object-literal method, decorator, optional-chained calls | structured / "" |
| `ts_depth_over.ts/.json` | TS | 70 nested arrow bodies -> depth over limit; partial result = `root` + `f1..f64` arrow defs (defs emit at `=>` before the brace push) | heuristic / "depth-limit" |

## Documented bounded-heuristic behaviors (locked into the goldens)

- `Config global_cfg(3);` (C++) is recorded as a definition and `Traps
  nested_call(2);` (local) as a call: constructor-style declarations are
  syntactically indistinguishable from function declarations without type
  information. This matches the class of false positives of the legacy regex
  heuristic (N22 `typed_function_` pattern) that astlite replaces.
- Enumerators, interface members, and enum/union names produce references
  only (plan definition patterns: namespace/class/struct/function for C++,
  function/class/const-arrow for TS).
- Template-literal interpolation bodies are skipped together with the literal
  (calls inside `${...}` are not recorded); a tagged template's tag is
  recorded as a call.
- Object-literal method names (`run() {}` inside an object literal) are
  recorded as calls (declaration contexts are not tracked inside expression
  object literals).
- `import ... from "..."` produces a stray `from` reference: `from` is not a
  keyword so that `static from(...)` methods are still found as definitions.
- TS generic arrows with explicit type parameters before the parameter list
  (`const f = <T,>(x: T) => ...`) are recorded as references only.

## Regeneration (only after intentional parser changes)

Depth and timeout fixtures are generated; the small fixtures are hand-written.

```python
# cpp_depth_over.cpp / ts_depth_over.ts / cpp_timeout_trigger.cpp generators
# are recorded in the N32 node history; parameters:
#   depth fixtures: one function + 70 nested blocks (C++), 70 nested arrows (TS)
#   timeout fixture: exactly 1000 lines, <= 2 MiB, 60 nested structs,
#     filler lines of '/*x*/"y"/*z*/\'0\'' repeated to the size budget
```

After regenerating inputs, produce expected JSON with a harness that calls
`astlite::parse_content` + `astlite::to_json` (the same serializer the tests
use), then re-validate: two fresh parses must equal the stored JSON
byte-for-byte. The over-2 MiB and over-10000-symbol degradation cases
(size-limit / symbol-limit) are exercised with synthetic in-memory bodies in
`tests/test_n32.cpp`; no multi-megabyte fixture is needed for them.
