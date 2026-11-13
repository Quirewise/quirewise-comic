# quirewise-comic

Standalone C11 comic-archive reader.

CMake target: `Quirewise::Comic`. Public header: `quirewise/comic.h`.

## Capabilities

- Opens CBZ (ZIP, optional ZipCrypto password) and CBT (USTAR) archives.
- Reports page count, per-page name/empty flag, and returns the raw page bytes
  for the caller to decode.
- Natural-number page ordering; empty directory entries are skipped.

## Limits

- Only ZIP and USTAR containers: CBR (RAR) and CB7 (7z) are **not** supported.
  `quirewise-archive` parses RAR headers only (no decompression), so CBR cannot
  be extracted anywhere in the release.
- The package does not decode images, render, or write archives; image decoding
  is the caller's job (`Quirewise::Image`).
- Archive/entry count, expanded size and input size are bounded; encrypted
  WinZip-AES members are unsupported.

## Build

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build
```
