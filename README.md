# quirewise-comic

Reader for CBZ (ZIP) and CBT (USTAR) comic archives: an ordered page list and
the raw bytes of each page, without decoding images.

`Quirewise::Comic` is an independent, MIT-licensed C11 library. It is installed
as a CMake package and can be consumed without the Quirewise application.

## What it does

- Opens a comic archive held in memory and reports its container format.
- Builds a page list from the image entries, sorted in natural reading order
  (`page2.jpg` before `page10.jpg`).
- Skips directory entries, non-image files such as `ComicInfo.xml`, and names
  containing NUL bytes.
- Reports each page's stored name, its uncompressed size, and whether it is
  empty.
- Returns the raw page bytes for the caller to decode.

It deliberately does **not** decode or render images, does not read comic
metadata, does not write archives, and does not support CBR (RAR) or CB7 (7z)
containers. It also does not sniff the container format: you pass
`QW_COMIC_ARCHIVE_ZIP` or `QW_COMIC_ARCHIVE_USTAR` explicitly.

The application's comic conversion route additionally reads **stored-entry** CBR
and CB7 through `Quirewise::Archive`; those containers are not part of the
`Quirewise::Comic` API, and compressed RAR payloads remain unsupported.

## Requirements

- C11 compiler and CMake 3.21 or newer.
- The `QuirewiseArchive` package installed in a prefix that CMake can find; it
  brings `QuirewiseRuntime` and `QuirewiseCompression` with it.
- CTest is used for the test suite.

## Build, test and install

Install Archive (and its dependencies) into the same prefix first, then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/quirewise/prefix
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/quirewise/prefix
```

Shared builds are available with `-DQUIREWISE_BUILD_SHARED=ON`.

## Use it from CMake

```cmake
find_package(QuirewiseComic CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE Quirewise::Comic)
```

The package config also loads `QuirewiseArchive`, which `Quirewise::Comic`
links privately.

## Quick start

Read a CBZ file into memory, list its pages, and read the first page:

```c
#include <quirewise/comic.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    QwComicArchive *archive = NULL;
    QwComicPageInfo info;
    QwComicStatus status;
    uint8_t *page = NULL;
    size_t page_size = 0u;
    uint8_t *cbz = NULL;
    size_t cbz_size = 0u;
    FILE *file;
    long length;

    if (argc < 2) return 2;
    file = fopen(argv[1], "rb");
    if (!file) return 2;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0 ||
        !(cbz = (uint8_t *)malloc((size_t)length ? (size_t)length : 1u)) ||
        fread(cbz, 1u, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(cbz);
        return 2;
    }
    fclose(file);
    cbz_size = (size_t)length;

    status = qw_comic_archive_open(cbz, cbz_size, QW_COMIC_ARCHIVE_ZIP, NULL,
                                   &archive);
    if (status != QW_COMIC_OK) {
        fprintf(stderr, "open failed: %d\n", (int)status);
        free(cbz);
        return 1;
    }
    printf("%zu pages\n", qw_comic_archive_page_count(archive));
    if (qw_comic_archive_page_info(archive, 0u, &info) == QW_COMIC_OK) {
        printf("first page: %.*s (%llu bytes)\n", (int)info.name_size,
               (const char *)info.name_bytes,
               (unsigned long long)info.uncompressed_size);
        status = qw_comic_archive_read_page(archive, 0u, &page, &page_size);
        if (status == QW_COMIC_OK) {
            printf("read %zu bytes\n", page_size);
            qw_comic_page_data_free(page);
        }
    }
    qw_comic_archive_close(archive);
    free(cbz); /* the archive borrowed this buffer until close */
    return 0;
}
```

The page index is the sorted position, not the archive entry index. A page
index outside `[0, page_count)` returns `QW_COMIC_ERR_INVALID_ARGUMENT`.

## API overview

| Header | Purpose |
|---|---|
| `quirewise/comic.h` | `QwComicStatus`, `QwComicArchiveFormat`, page access |

| Task | Functions |
|---|---|
| Open and close | `qw_comic_archive_open`, `qw_comic_archive_close` |
| Container format | `qw_comic_archive_format` |
| Page list | `qw_comic_archive_page_count`, `qw_comic_archive_page_info` |
| Page bytes | `qw_comic_archive_read_page`, `qw_comic_page_data_free` |

## Behavior and limits

- The input container is borrowed and must stay alive until
  `qw_comic_archive_close`; the archive keeps the pointer.
- Page names are borrowed from the archive. Use `name_size`: the bytes keep the
  archive encoding and are not guaranteed to be UTF-8.
- `qw_comic_archive_read_page` allocates a copy; release it with
  `qw_comic_page_data_free`. For ZIP containers the entry is decompressed again
  on every call, so pages are not cached.
- `is_empty` is `uncompressed_size == 0`; empty image entries are listed, not
  dropped.
- Limits come from `Quirewise::Archive`: 1 GiB container, 50,000 entries,
  256 MiB per page, 1 GiB total expanded. A CBT archive that exceeds them
  fails with `QW_COMIC_ERR_RESOURCE_LIMIT`; for CBZ the Archive open call
  validates them and the failure arrives as `QW_COMIC_ERR_FORMAT`.
- WinZip AES members are rejected with `QW_COMIC_ERR_UNSUPPORTED` when read;
  traditional ZipCrypto pages work when a password is passed to open.
- The archive object holds the mutable ZIP reader state, so one archive must
  not be read from two threads at once.

## Testing

`ctest` runs `quirewise_comic_fuzz_smoke`, which mutates a CBZ and a CBT seed
250 times each, opens every mutation, reads page 0 of ZIP archives that opened,
and reports accept/reject counts without asserting on them.

## Documentation

See [docs/usage.md](docs/usage.md) for recipes: listing pages, reading and
freeing page data, password-protected CBZ files, CBT archives, and the
extension rule that decides what counts as a page.

## License

MIT. See [LICENSE](LICENSE).
