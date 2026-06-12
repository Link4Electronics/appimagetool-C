# appimagetool-C

Create AppImages from an AppDir using DWARFS compression and the [uruntime-C](https://github.com/Link4Electronics/uruntime-C) AppImage runtime.

## Quick start

```sh
cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build
./build/appimagetool ./AppDir
```

## Supported architectures:
* aarch64 (ARM 64-bit) — musl-static (CI) or glibc
* arm32v7 (ARM 32-bit) — musl-static (CI) or glibc
* x86_64 (Intel/AMD 64-bit) — musl-static (CI) or glibc
* i386 (Intel/AMD 32-bit) — musl-static (CI) or glibc
* loongarch64 (LoongArch 64-bit) — musl-static (CI) or glibc
* ppc64le (POWER 64-bit little-endian ELFv2) — musl-static (CI) or glibc
* ppc64 (POWER 64-bit big-endian ELFv2) — glibc only (CI via `kth5/archpower` Docker)
* ppc (POWER 32-bit big-endian) — glibc only (requires local build for now)
* s390x (IBM Z 64-bit) — musl-static (CI) or glibc

## Build options

```
-DAPPIMAGETOOL_STATIC=ON   # Static linking (default: ON)
```

## Dependencies

- **libcurl** — HTTP downloads
- **zlib** — zsync gzip compression
- C23 compiler (GCC 14+, Clang 16+)

For static builds on Alpine: `apk add curl-dev curl-static zlib-dev zlib-static openssl-dev openssl-libs-static nghttp2-static brotli-static c-ares-static`

## Features

- Validate AppDir (checks for `AppRun`, `.DirIcon`, one `.desktop` file).
- Download `mkdwarfs` and `uruntime` if not already cached.
- Write `X-AppImage-*` metadata into the desktop entry.
- Build the DWARFS image with the runtime embedded as the ELF header.
- Optional profile-guided optimization (DWARFS hotness profiling).
- Built-in zsync generation.
- No central repository, no daemon, no extra runtime deps.

## License

MIT
