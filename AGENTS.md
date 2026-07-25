# Qbrain Agent Notes

- **Platform**: Windows 11 native only. No WSL, no Docker required.
- **Language**: C++20 (MSVC preferred).
- **Data**: `%LOCALAPPDATA%\Qbrain\`
- **Docs**: `docs/01-ANALYSIS.md`, `docs/02-DEVELOPMENT.md`, `docs/03-BUILD-WINDOWS.md`
- **Upstream inspiration**: https://github.com/garrytan/gbrain (TypeScript/Bun) — reimplemented, not vendored.
- **Build blocker on some machines**: incomplete VS BuildTools (need Windows SDK + UCRT). See `docs/03-BUILD-WINDOWS.md`.
- Do not introduce Linux-only scripts as the primary path; PowerShell is first-class.
