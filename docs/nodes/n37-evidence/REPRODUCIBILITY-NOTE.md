# N37 D1 — Packaging reproducibility note (AA2 / P0-3)

Date: 2026-08-16
Scope: `scripts/package.ps1` double-run reproducibility (private tree
`%TEMP%\n37a-verify`, a robocopy of the repo excluding `.git` / `build` /
`gbrain-upstream` / `tmp`; each run performs its own clean cl-path build via
the canonical scripts plus the full unit-suite gate).

## What was measured

Two consecutive full invocations of `scripts\package.ps1` (each = clean
`build-cl.ps1` build + `build-tests-cl.ps1` link/run of the whole suite +
staging + zip + MANIFEST.json) were compared byte-for-byte BEFORE the
determinism post-processing was added to the script:

- `qbrain.exe` (3,279,360 bytes, identical sizes) diverged in exactly **4
  bytes per build pair** — the low 2 bytes of the 4-byte PE COFF
  `TimeDateStamp`, present at **2 sites**: the PE header (offset 0x110) and
  one interior copy of the same 4-byte value. Every other byte was identical.
- `qbrain_tests.exe` showed the same pattern (4 bytes, 2 sites).
- Root cause: MSVC `link.exe` stamps the wall-clock time into the PE
  `TimeDateStamp`; `scripts/build-cl.ps1` does not pass `/Brepro`.
- Doc/text payload files (LICENSE, README.md, docs/10-STORAGE-CONTRACT.md)
  were byte-identical across runs.

Example measured pair (`qbrain.exe`):

```
run1 TimeDateStamp = 0x6a8124ee   run2 TimeDateStamp = 0x6a812a41
diff sites: 0x110 (PE header) and 0x2bcc24 (interior duplicate)
total differing bytes: 4
```

## Determinism handling inside scripts/package.ps1

1. **PE TimeDateStamp normalization (per staged exe).** Every occurrence of
   the link timestamp value (header + interior duplicate) is zeroed, then the
   first 4 bytes of the sha256 of the zeroed image are written into those
   sites — a content-addressed stamp with the same semantics as
   `/Brepro`. The field is advisory metadata (the Windows loader ignores it);
   the script's `--version` gate executes the NORMALIZED binary to prove it
   still runs. The normalized staged executables are therefore bit-for-bit
   reproducible across clean rebuilds.
2. **Fixed staged file mtimes.** All staged files get `LastWriteTime`
   pinned to 2020-01-01 00:00:00 local before `Compress-Archive`, so zip
   entries embed a fixed timestamp instead of the build time.
3. **MANIFEST.json discipline.** No timestamps and no absolute paths;
   sorted file list; UTF-8 without BOM.

## Residual limitations

- The zip container bytes are reproducible only up to the properties the
  pinned mtimes control. If the whole-zip sha256 still differs between two
  runs, the accepted criterion per the approved plan remains **MANIFEST.json
  content equality (field-by-field) + zip file-list equality
   (entry path + entry-content sha256 pairs)**; any whole-zip sha256
  difference is a container-metadata artifact, not a payload difference.
- The pinned mtime is written in local time; builds in a different time
  zone can therefore produce a different zip container (payload and
  MANIFEST remain equal).

## Double-run result (with the determinism post-processing in place)

Both runs exit 0 and are fully reproducible — see `PACKAGING-RUNS.txt`
(raw hashes, zip entry list) and `MANIFEST-RUN{1,2}.json` (the two manifests,
byte-identical):

- MANIFEST.json content equality (byte-for-byte / field-by-field): **PASS**
  (sha256 `60ed06f972febe0f1f95689e66776541487bb126f165a51d22baefca8a71b6c4`
  for both runs)
- Zip file-list equality (entry path + entry-content sha256 pairs): **PASS**
  (5 entries: qbrain.exe, qbrain_tests.exe, LICENSE, README.md,
  docs/10-STORAGE-CONTRACT.md)
- Whole-zip sha256 equality (the STRONGER criterion): **PASS**
  (`072587d02d0fb29b5b6957841ee5167da0c9d7865cec4e37426532cdf4d37baf` for
  both runs) — no residual container variance on this machine.
- The packaged `qbrain.exe --version` gate printed
  `Qbrain 2.0.0 (windows-native c++)` in both runs (AA2).

