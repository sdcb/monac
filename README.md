# Monac

An ultra-lightweight, pure **C17** code editor engine and out-of-process LSP client library for Windows, built directly on **Direct2D 1.0**, **DirectWrite**, and **Win32** with a strict **Windows 7 RTM** binary baseline.

## Features

- **Core Text & Editing**: Red-Black Piece Tree with binary-searchable line tables, grouped atomic Undo/Redo, multi-cursor editing, and incremental find/replace.
- **Decorations, Folding & Projection**: Lazy-delta augmented RB interval tree, inline & multiline code folding (`{…}` / `[…]` badges via `IDWriteInlineObject`), inlay hints, DirectWrite-backed soft word wrap, and Fenwick-tree view projection.
- **Syntax & Rendering**: Incremental line-state lexer cache (built-in JSON/JSONC lexer + custom lexer API), Direct2D 1.0 hardware/offscreen rendering, diagnostic squigglies, IME composition, Per-Monitor High-DPI (progressive Win10/Win8.1/Win7 fallback), and Win8.1+ Color Emoji support.
- **Built-in Overlays & Accessibility**: Native D2D completion popup, hover bubble, find/replace bar, and Win32 UI Automation (`ITextProvider` / `ITextRangeProvider`).
- **Independent LSP Client (`monac_lsp`)**: Out-of-process JSON-RPC over stdio pipes with Windows Job Object lifecycle cleanup, debounced `didChange`, diagnostics, completion, and hover (verified with `csharp-ls`).
- **Tiny Footprint, Zero External Runtime Dependencies**:
  - MinGW-w64 Release: `monac.dll` ~152 KB, `monac_lsp.dll` ~81 KB
  - Standalone GUI demos (statically linked, zero non-system DLLs): `demo_json_fold.exe` ~156 KB, `demo_lsp_csharp.exe` ~192 KB

## Build & Test

Requires CMake 3.24+, Ninja, and either **MinGW-w64 (GCC)** or **MSVC (C17)**.

### MinGW-w64

```powershell
cmake --preset mingw-release
cmake --build --preset mingw-release
ctest --preset mingw-release --output-on-failure
```

*(Adjust compiler paths in `CMakePresets.json` or pass `-DCMAKE_C_COMPILER=gcc` if MinGW-w64 is installed in another location.)*

### MSVC

Run from an x64 Native Tools Command Prompt (or after `vcvars64.bat`):

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release --output-on-failure
```

## Build Outputs

- `<build>/monac.dll`, `<build>/monac_lsp.dll` (+ static libraries) — Core editor and LSP libraries
- `<build>/demo/` — Standalone single-file GUI demos (statically linked; can be copied and run anywhere):
  - `demo_json_fold.exe` — JSONC syntax highlighting, inline/multiline folding, soft wrap, inlays, and Color Emoji
  - `demo_lsp_csharp.exe` — C# editor + out-of-process LSP (`csharp-ls` if on `PATH`, or `--mock`)
- `<build>/test/` — Unit tests, UI Automation tests, LSP integration tests, and `bench_core`

## Public Headers

- `include/monac/monac.h` — Pure C17 editor API (no `<windows.h>` dependency)
- `include/monac/monac_win32.h` — Win32 `HWND` hosting API
- `include/monac/monac_lsp.h` — Independent LSP client API

## License

[MIT](LICENSE)
