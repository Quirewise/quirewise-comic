# quirewise-comic usage guide

Task-level workflows for `Quirewise::Comic`. The whole API is declared in
`quirewise/comic.h`.

## 1. Build and integrate

Install `QuirewiseArchive` (which brings Runtime and Compression) into a prefix
first, then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/quirewise/prefix
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/quirewise/prefix
```

Consumer CMake:

```cmake
find_package(QuirewiseComic CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE Quirewise::Comic)
```

Nothing needs runtime initialization. `QuirewiseComicConfig.cmake` calls
`find_dependency(QuirewiseArchive)`, so you do not need a second
`find_package` unless you also call the Archive API yourself.

## 2. Core model and ownership

A `QwComicArchive` is a borrowed container plus a page table built at open
time. Page indexes are positions in that table, not archive entry indexes.

| Object | Owner | Lifetime / release |
|---|---|---|
| container bytes | caller | must stay alive until `qw_comic_archive_close` |
| `QwComicArchive *` | library | `qw_comic_archive_close` |
| `QwComicPageInfo::name_bytes` | archive | until `qw_comic_archive_close` |
| page bytes from `read_page` | caller | `qw_comic_page_data_free` |

- The password string is copied at open, so your buffer can be released
  immediately afterwards.
- The page table stores a copy of each name (NUL-terminated internally), but
  the public struct exposes only `name_bytes`/`name_size`; do not assume UTF-8.
- No page data is cached: every `read_page` decodes again.

## 3. Which entries become pages

The page table is a filtered, sorted view of the container:

- Directories are dropped (ZIP directory entries; USTAR non-regular types).
- Only files whose last extension matches a fixed image allow-list are kept.
  The compare is case-insensitive and runs over the extension after the final
  `/` or `\`. The list covers `jpg`, `jpeg`, `png`, `bmp`, `gif`, `tif`,
  `tiff`, `webp`, `tga`, `psd`, `hdr`, `pic`, `pnm`, `ppm`, `pgm`, `pbm`,
  `pam`, `qoi`, `wbmp`, `xbm`, `ico`, `cur`, `jp2`, `j2k`, `j2c`, `jpf`,
  `jpx`, `pcx`, `pfm`, `icns`, `dds`, `xpm`, `sgi`, `rgb`, `rgba`, `bw`,
  `ras`, `sun`, `dcm`, `dicom`, `psb`, `miff`, `mng`, `apng`, `exr`, `xcf`,
  `pcd`.
- Names containing a NUL byte are skipped.
- The survivors are sorted with a natural comparison: digit runs compare by
  numeric value (`page2` before `page10`), other characters compare
  case-insensitively, and equal names keep archive order.
- Zero-byte image entries stay in the table and are reported with
  `is_empty = 1`.

So a CBZ with `ComicInfo.xml`, a `cover.jpg` and `pages/001.png` ... yields one
page per image, ordered naturally, with the XML ignored.

## 4. List pages without decoding

```c
#include <quirewise/comic.h>

#include <stdio.h>

void list_pages(QwComicArchive *archive)
{
    size_t count = qw_comic_archive_page_count(archive);
    size_t index;

    for (index = 0u; index < count; ++index) {
        QwComicPageInfo info;
        if (qw_comic_archive_page_info(archive, index, &info) != QW_COMIC_OK)
            continue;
        printf("%3zu %10llu%s  %.*s\n", index,
               (unsigned long long)info.uncompressed_size,
               info.is_empty ? " empty" : "      ",
               (int)info.name_size, (const char *)info.name_bytes);
    }
}
```

`qw_comic_archive_page_info` returns `QW_COMIC_ERR_INVALID_ARGUMENT` for an
index outside `[0, page_count)`, including every index when the archive has no
image entries.

## 5. Read pages in order

```c
#include <quirewise/comic.h>

#include <stdio.h>

int read_pages(QwComicArchive *archive)
{
    size_t count = qw_comic_archive_page_count(archive);
    size_t index;

    for (index = 0u; index < count; ++index) {
        uint8_t *data = NULL;
        size_t size = 0u;
        QwComicStatus status = qw_comic_archive_read_page(archive, index,
                                                          &data, &size);
        if (status != QW_COMIC_OK) {
            fprintf(stderr, "page %zu: %d\n", index, (int)status);
            continue;
        }
        /* Hand data/size to your image decoder (for example Quirewise::Image). */
        printf("page %zu: %zu bytes\n", index, size);
        qw_comic_page_data_free(data);
    }
    return 0;
}
```

Read one page at a time and free it before reading the next: a 256 MiB page
cap applies per call, but holding many decoded pages is your memory, not the
library's.

## 6. Open a password-protected CBZ

```c
#include <quirewise/comic.h>

#include <stdio.h>

int open_protected(const uint8_t *cbz, size_t cbz_size, const char *password)
{
    QwComicArchive *archive = NULL;
    uint8_t *page = NULL;
    size_t page_size = 0u;
    QwComicStatus status = qw_comic_archive_open(cbz, cbz_size,
                                                 QW_COMIC_ARCHIVE_ZIP,
                                                 password, &archive);

    if (status != QW_COMIC_OK) {
        fprintf(stderr, "container: %d\n", (int)status);
        return 1;
    }
    /* Opening only reads the ZIP directory, so the password is checked here: */
    status = qw_comic_archive_read_page(archive, 0u, &page, &page_size);
    if (status == QW_COMIC_ERR_EXTRACT) {
        fputs("wrong password or corrupt page\n", stderr);
    } else if (status == QW_COMIC_ERR_UNSUPPORTED) {
        fputs("WinZip AES or unsupported compression method\n", stderr);
    }
    qw_comic_page_data_free(page);
    qw_comic_archive_close(archive);
    return status == QW_COMIC_OK ? 0 : 1;
}
```

Because the page list comes from the ZIP central directory, `open` succeeds
even when the password is wrong. The failure surfaces on the first
`read_page`. Pass `NULL` for unencrypted archives; the password is ignored for
`QW_COMIC_ARCHIVE_USTAR`.

## 7. Open a CBT (USTAR) archive

```c
#include <quirewise/comic.h>

int open_cbt(const uint8_t *tar_bytes, size_t tar_size)
{
    QwComicArchive *archive = NULL;
    QwComicStatus status = qw_comic_archive_open(tar_bytes, tar_size,
                                                 QW_COMIC_ARCHIVE_USTAR, NULL,
                                                 &archive);
    if (status != QW_COMIC_OK) return 1;
    /* same page_count / page_info / read_page API */
    qw_comic_archive_close(archive);
    return 0;
}
```

USTAR pages are copied out of the borrowed container on each `read_page`; no
DEFLATE work happens, so the cost is a `memcpy` per page. `qw_comic_archive_format`
returns the format you passed in, or 0 for a NULL handle.

## 8. Status values

| Value | Meaning |
|---|---|
| `QW_COMIC_OK` = 0 | success |
| `QW_COMIC_ERR_INVALID_ARGUMENT` = -1 | NULL argument, empty input, page index out of range |
| `QW_COMIC_ERR_OUT_OF_MEMORY` = -2 | allocation failed |
| `QW_COMIC_ERR_FORMAT` = -3 | invalid container structure or size mismatch |
| `QW_COMIC_ERR_UNSUPPORTED` = -4 | unknown format enum, AES entry, unsupported ZIP method |
| `QW_COMIC_ERR_RESOURCE_LIMIT` = -5 | an Archive input/entry/total limit was exceeded |
| `QW_COMIC_ERR_EXTRACT` = -6 | a ZIP page could not be decrypted or inflated |

Mapping from the Archive layer, as implemented:

- `QW_ARCHIVE_ERR_OUT_OF_MEMORY` and `QW_ARCHIVE_ERR_RESOURCE_LIMIT` keep their
  meaning when the Archive call returns them.
- `QW_ARCHIVE_ERR_UNSUPPORTED` becomes `QW_COMIC_ERR_UNSUPPORTED`.
- every other open failure, including `QW_ARCHIVE_ERR_OPEN`, becomes
  `QW_COMIC_ERR_FORMAT`. ZIP limit violations take this path because the
  Archive open call reports them as `QW_ARCHIVE_ERR_OPEN`.
- a failed ZIP page extraction becomes `QW_COMIC_ERR_EXTRACT` unless the
  Archive call reports out-of-memory or unsupported; a page whose extracted
  size differs from the directory size becomes `QW_COMIC_ERR_FORMAT`.

## 9. Memory, lifetime and threading

- The archive never copies the container. Keep the bytes alive (and unchanged)
  until `qw_comic_archive_close`.
- Page names live in the archive and die with it; copy them if you need them
  later.
- Every successful `read_page` transfers ownership of a fresh buffer to you.
  `qw_comic_page_data_free(NULL)` is safe.
- A ZIP-backed archive owns a `QwZipArchiveReader`, which carries mutable error
  state and decodes in place, so one archive must not be read from two threads
  at once. Independent archives over independent containers are fine; the
  library has no globals or locks.
- `qw_comic_archive_close(NULL)` is a no-op.

## 10. Limits and format notes

| Item | Limit |
|---|---|
| container size | 1 GiB |
| entries in the container | 50,000 (image entries become pages) |
| page size | 256 MiB uncompressed |
| total expanded size | 1 GiB |

- The format enum is required: `.cbz`-style ZIP maps to
  `QW_COMIC_ARCHIVE_ZIP`, `.cbt`-style USTAR to `QW_COMIC_ARCHIVE_USTAR`.
  Pass anything else and open returns `QW_COMIC_ERR_UNSUPPORTED`.
- ZIP limit violations are detected while `Quirewise::Archive` opens the
  central directory, and that open call reports them as
  `QW_ARCHIVE_ERR_OPEN`, which this package folds into
  `QW_COMIC_ERR_FORMAT`. CBT limit violations keep
  `QW_COMIC_ERR_RESOURCE_LIMIT`.
- ZIP pages may be Stored (0) or Deflate (8); other methods report
  `QW_COMIC_ERR_UNSUPPORTED` when read.
- `ComicInfo.xml`, `info.txt` and other non-image entries are ignored; adding
  or removing one renumbers the pages because indexes follow the page table.
- The page name is the stored path, not a sanitized filename. It is for display
  and logging; the library writes nothing to disk.

## 11. Related packages

- `Quirewise::Archive` provides the ZIP reader and USTAR visitor underneath.
- `Quirewise::Image` decodes the page bytes returned by `read_page`.
- `Quirewise::Pipeline` uses this package to turn CBZ/CBT input into pages.

## 12. License

MIT. See `LICENSE`.
