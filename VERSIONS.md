# Vendored dependencies & versions

Pinned for reproducible builds. Verify SHA-256 before building.

| Dependency | Version | Upstream | License | SHA-256 (source tarball) |
|---|---|---|---|---|
| libuv | 1.50.0 | https://github.com/libuv/libuv/archive/refs/tags/v1.50.0.tar.gz | MIT (vendor/libuv/LICENSE) | `b1ec56444ee3f1e10c8bd3eed16ba47016ed0b94fe42137435aaf2e0bd574579` |
| llhttp | 9.2.1 (release) | https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.2.1.tar.gz | MIT (vendor/llhttp/LICENSE-MIT) | `3c163891446e529604b590f9ad097b2e98b5ef7e4d3ddcf1cf98b62ca668f23e` |

## Tools (not vendored, verified by `cerco doctor`)

| Tool | Used for | Minimum |
|---|---|---|
| clang (LLVM) | native + wasm32 compilation | 16 |
| lld / wasm-ld | wasm + native linking | 16 |
| llvm-ar | static libs | 16 |

macOS: `brew install llvm lld` — Linux: `apt install clang lld llvm` (or llvm.sh).

## Tailwind (downloaded on demand by the CLI)

| Tool | Version | Platforms | SHA-256 manifest |
|---|---|---|---|
| tailwindcss standalone | v4.3.3 (pinned) | macOS arm64/x64, Linux arm64/x64 | `cli/tailwind_manifest.h` |

The CLI refuses to execute a downloaded binary whose SHA-256 does not match the
manifest. `CERCO_SKIP_TAILWIND=1` disables Tailwind entirely.
