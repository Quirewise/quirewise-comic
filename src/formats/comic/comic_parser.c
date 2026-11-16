#include "quirewise/comic.h"

#include "quirewise/archive.h"
#include "quirewise/rar.h"
#include "quirewise/seven_zip.h"
#include "../../core/natural_sort.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct QwComicPage {
    uint8_t *name_bytes;
    size_t name_size;
    uint64_t uncompressed_size;
    size_t archive_index;
    size_t ordinal;
    const uint8_t *tar_data;
    uint8_t *owned_data;
} QwComicPage;

struct QwComicArchive {
    const uint8_t *input;
    size_t input_size;
    QwComicArchiveFormat format;
    QwZipArchiveReader *zip;
    QwComicPage *pages;
    size_t page_count;
    size_t page_capacity;
    char *password;
};

typedef struct QwComicTarCollect {
    QwComicArchive *archive;
    QwComicStatus status;
    size_t ordinal;
} QwComicTarCollect;

static unsigned char comic_ascii_lower(unsigned char value)
{
    return value >= 'A' && value <= 'Z'
        ? (unsigned char)(value - 'A' + 'a') : value;
}

static int comic_equal_extension(const uint8_t *extension, size_t size,
                                 const char *candidate)
{
    size_t i;
    if (strlen(candidate) != size) return 0;
    for (i = 0u; i < size; ++i) {
        if (comic_ascii_lower(extension[i]) !=
            comic_ascii_lower((unsigned char)candidate[i])) return 0;
    }
    return 1;
}

static int comic_name_is_image(const uint8_t *name, size_t name_size)
{
    static const char *const extensions[] = {
        "jpg", "jpeg", "png", "bmp", "gif", "tif", "tiff", "webp",
        "tga", "psd", "hdr", "pic", "pnm", "ppm", "pgm", "pbm", "pam",
        "qoi", "wbmp", "xbm", "ico", "cur", "jp2", "j2k", "j2c",
        "jpf", "jpx", "pcx", "pfm", "icns", "dds", "xpm", "sgi", "rgb",
        "rgba", "bw", "ras", "sun", "dcm", "dicom", "psb", "miff", "mng",
        "apng", "exr", "xcf", "pcd"
    };
    size_t base = 0u;
    size_t dot = SIZE_MAX;
    for (size_t i = 0u; i < name_size; ++i) {
        if (name[i] == '/' || name[i] == '\\') {
            base = i + 1u;
            dot = SIZE_MAX;
        } else if (name[i] == '.') {
            dot = i;
        }
    }
    if (dot == SIZE_MAX || dot < base || dot + 1u >= name_size) return 0;
    for (size_t i = 0u; i < sizeof(extensions) / sizeof(extensions[0]); ++i) {
        if (comic_equal_extension(name + dot + 1u, name_size - dot - 1u,
                                  extensions[i])) return 1;
    }
    return 0;
}

static int comic_reserve_page(QwComicArchive *archive, size_t required)
{
    size_t capacity;
    QwComicPage *grown;
    if (required <= archive->page_capacity) return 1;
    capacity = archive->page_capacity ? archive->page_capacity : 16u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) return 0;
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(*archive->pages)) return 0;
    grown = (QwComicPage *)realloc(archive->pages,
                                   capacity * sizeof(*archive->pages));
    if (!grown) return 0;
    archive->pages = grown;
    archive->page_capacity = capacity;
    return 1;
}

static int comic_add_page(QwComicArchive *archive, const uint8_t *name,
                          size_t name_size, uint64_t uncompressed_size,
                          size_t archive_index, size_t ordinal,
                          const uint8_t *tar_data)
{
    uint8_t *name_copy;
    if (!archive || !name || !name_size || memchr(name, 0, name_size))
        return 0;
    if (!comic_reserve_page(archive, archive->page_count + 1u)) return 0;
    if (name_size == SIZE_MAX) return 0;
    name_copy = (uint8_t *)malloc(name_size + 1u);
    if (!name_copy) return 0;
    memcpy(name_copy, name, name_size);
    name_copy[name_size] = 0u;
    archive->pages[archive->page_count].name_bytes = name_copy;
    archive->pages[archive->page_count].name_size = name_size;
    archive->pages[archive->page_count].uncompressed_size =
        uncompressed_size;
    archive->pages[archive->page_count].archive_index = archive_index;
    archive->pages[archive->page_count].ordinal = ordinal;
    archive->pages[archive->page_count].tar_data = tar_data;
    archive->pages[archive->page_count].owned_data = NULL;
    ++archive->page_count;
    return 1;
}

static int comic_compare_pages(const void *left, const void *right)
{
    const QwComicPage *a = (const QwComicPage *)left;
    const QwComicPage *b = (const QwComicPage *)right;
    int order = qw_comic_natural_name_compare((const char *)a->name_bytes,
                                        (const char *)b->name_bytes);
    if (!order) order = (a->ordinal > b->ordinal) - (a->ordinal < b->ordinal);
    return order;
}

static QwArchiveTarVisitAction comic_tar_collect(void *context,
                                                const char *name,
                                                const uint8_t *data,
                                                size_t size)
{
    QwComicTarCollect *collect = (QwComicTarCollect *)context;
    size_t name_size;
    size_t ordinal;
    if (!collect || !name) return (QwArchiveTarVisitAction)2;
    ordinal = collect->ordinal++;
    name_size = strlen(name);
    if (!comic_name_is_image((const uint8_t *)name, name_size))
        return QW_ARCHIVE_TAR_CONTINUE;
    if (!comic_add_page(collect->archive, (const uint8_t *)name, name_size,
                        (uint64_t)size, 0u, ordinal, data)) {
        collect->status = QW_COMIC_ERR_OUT_OF_MEMORY;
        return (QwArchiveTarVisitAction)2;
    }
    return QW_ARCHIVE_TAR_CONTINUE;
}

static void comic_release_contents(QwComicArchive *archive)
{
    if (!archive) return;
    if (archive->zip) qw_zip_archive_close(archive->zip);
    free(archive->password);
    for (size_t i = 0u; i < archive->page_count; ++i) {
        free(archive->pages[i].name_bytes);
        free(archive->pages[i].owned_data);
    }
    free(archive->pages);
}


typedef struct ComicSevenItem {
    char *name;
    size_t name_size;
    uint8_t *data;
    size_t size;
    size_t capacity;
    int failed;
} ComicSevenItem;

typedef struct ComicSevenCollect {
    QwComicArchive *archive;
    ComicSevenItem *items;
    size_t count;
    size_t capacity;
    size_t total_size;
    int oom;
} ComicSevenCollect;

static int comic_seven_add_item(ComicSevenCollect *collect, const char *name,
                                size_t name_size, size_t size,
                                ComicSevenItem **out_item)
{
    ComicSevenItem *items;
    ComicSevenItem *item;
    char *copy;
    if (collect->count >= 65536u || name_size > 4096u) return 0;
    if (collect->count >= collect->capacity) {
        size_t next = collect->capacity ? collect->capacity * 2u : 16u;
        items = (ComicSevenItem *)realloc(collect->items,
                                          next * sizeof(*items));
        if (!items) return 0;
        collect->items = items;
        collect->capacity = next;
    }
    item = &collect->items[collect->count];
    memset(item, 0, sizeof(*item));
    copy = (char *)malloc(name_size + 1u);
    if (!copy) return 0;
    memcpy(copy, name, name_size);
    copy[name_size] = '\0';
    item->name = copy;
    item->name_size = name_size;
    if (size) {
        item->data = (uint8_t *)malloc(size);
        if (!item->data) {
            free(copy);
            return 0;
        }
        item->capacity = size;
    }
    ++collect->count;
    *out_item = item;
    return 1;
}

static int comic_seven_begin(void *context, uint32_t archive_index,
                             const char *utf8_name, uint64_t declared_size,
                             int is_directory, void **out_entry)
{
    ComicSevenCollect *collect = (ComicSevenCollect *)context;
    ComicSevenItem *item = NULL;
    (void)archive_index;
    if (out_entry) *out_entry = NULL;
    if (!collect || !utf8_name || is_directory) return 0;
    if (!comic_name_is_image((const uint8_t *)utf8_name, strlen(utf8_name)))
        return 0;
    if (declared_size > 128u * 1024u * 1024u ||
        collect->total_size + declared_size > 512u * 1024u * 1024u)
        return 1;
    collect->total_size += (size_t)declared_size;
    if (!comic_seven_add_item(collect, utf8_name, strlen(utf8_name),
                              (size_t)declared_size, &item)) {
        collect->oom = 1;
        return 1;
    }
    if (out_entry) *out_entry = item;
    return 0;
}

static int comic_seven_write(void *context, void *entry, const void *data,
                             size_t size)
{
    ComicSevenItem *item = (ComicSevenItem *)entry;
    size_t needed;
    (void)context;
    if (!item) return 0;
    needed = item->size + size;
    if (needed > item->capacity) {
        size_t capacity = item->capacity ? item->capacity : 4096u;
        uint8_t *grown;
        while (capacity < needed) capacity *= 2u;
        grown = (uint8_t *)realloc(item->data, capacity);
        if (!grown) {
            item->failed = 1;
            return 1;
        }
        item->data = grown;
        item->capacity = capacity;
    }
    memcpy(item->data + item->size, data, size);
    item->size += size;
    return 0;
}

static void comic_seven_finish(void *context, void *entry, int operation_result)
{
    (void)context;
    if (entry && operation_result) ((ComicSevenItem *)entry)->failed = 1;
}

static void comic_seven_dispose(ComicSevenCollect *collect)
{
    size_t index;
    if (!collect) return;
    for (index = 0u; index < collect->count; ++index) {
        free(collect->items[index].name);
        free(collect->items[index].data);
    }
    free(collect->items);
    memset(collect, 0, sizeof(*collect));
}

static QwComicStatus comic_open_seven_zip(QwComicArchive *archive,
                                          const uint8_t *data, size_t size,
                                          const char *password)
{
    ComicSevenCollect collect;
    QwSevenZipSink sink;
    QwSevenZipStatus status;
    size_t index;
    int success = 0;
    memset(&collect, 0, sizeof(collect));
    collect.archive = archive;
    memset(&sink, 0, sizeof(sink));
    sink.context = &collect;
    sink.begin = comic_seven_begin;
    sink.write = comic_seven_write;
    sink.finish = comic_seven_finish;
    status = qw_seven_zip_extract(data, size, password, &sink);
    if (collect.oom) {
        comic_seven_dispose(&collect);
        return QW_COMIC_ERR_OUT_OF_MEMORY;
    }
    if (status != QW_SEVEN_ZIP_OK) {
        comic_seven_dispose(&collect);
        if (status == QW_SEVEN_ZIP_UNSUPPORTED)
            return QW_COMIC_ERR_UNSUPPORTED;
        if (status == QW_SEVEN_ZIP_RESOURCE_LIMIT)
            return QW_COMIC_ERR_RESOURCE_LIMIT;
        if (status == QW_SEVEN_ZIP_OUT_OF_MEMORY)
            return QW_COMIC_ERR_OUT_OF_MEMORY;
        return QW_COMIC_ERR_EXTRACT;
    }
    for (index = 0u; index < collect.count; ++index) {
        ComicSevenItem *item = &collect.items[index];
        QwComicPage *page;
        if (item->failed || !item->data) continue;
        if (!comic_add_page(archive, (const uint8_t *)item->name,
                            item->name_size, item->size, 0u, index, NULL)) {
            comic_seven_dispose(&collect);
            return QW_COMIC_ERR_OUT_OF_MEMORY;
        }
        page = &archive->pages[archive->page_count - 1u];
        page->owned_data = item->data;
        item->data = NULL;
        success = 1;
    }
    comic_seven_dispose(&collect);
    return success ? QW_COMIC_OK : QW_COMIC_ERR_FORMAT;
}


typedef struct ComicRarCollect {
    QwComicArchive *archive;
    size_t ordinal;
    int unsupported;
    int oom;
} ComicRarCollect;

static QwRarVisitAction comic_rar_visit(void *context,
                                        const QwRarEntryView *entry)
{
    ComicRarCollect *collect = (ComicRarCollect *)context;
    if (!collect || !entry) return QW_RAR_VISIT_STOP;
    if (entry->is_directory || !entry->name || !entry->name_size ||
        memchr(entry->name, 0, entry->name_size) ||
        !comic_name_is_image(entry->name, entry->name_size))
        return QW_RAR_VISIT_CONTINUE;
    if (!entry->is_stored || entry->packed_size != entry->uncompressed_size ||
        !entry->packed_data || !entry->packed_size) {
        collect->unsupported = 1;
        return QW_RAR_VISIT_CONTINUE;
    }
    if (entry->uncompressed_size > 128u * 1024u * 1024u) {
        collect->oom = 1;
        return QW_RAR_VISIT_STOP;
    }
    if (!comic_add_page(collect->archive, entry->name, entry->name_size,
                        entry->uncompressed_size, 0u, collect->ordinal++,
                        NULL)) {
        collect->oom = 1;
        return QW_RAR_VISIT_STOP;
    }
    {
        QwComicPage *page =
            &collect->archive->pages[collect->archive->page_count - 1u];
        page->owned_data = (uint8_t *)malloc((size_t)entry->uncompressed_size);
        if (!page->owned_data) {
            collect->oom = 1;
            return QW_RAR_VISIT_STOP;
        }
        memcpy(page->owned_data, entry->packed_data,
               (size_t)entry->uncompressed_size);
    }
    return QW_RAR_VISIT_CONTINUE;
}

static QwComicStatus comic_open_rar(QwComicArchive *archive,
                                    const uint8_t *data, size_t size)
{
    ComicRarCollect collect;
    QwRarParseLimits limits;
    QwRarStatus status;
    memset(&collect, 0, sizeof(collect));
    collect.archive = archive;
    limits.max_entries = 65536u;
    limits.max_entry_bytes = 128u * 1024u * 1024u;
    limits.max_total_bytes = 512u * 1024u * 1024u;
    status = qw_rar_archive_visit(data, size, &limits, comic_rar_visit,
                                  &collect);
    if (collect.oom) return QW_COMIC_ERR_OUT_OF_MEMORY;
    if (status == QW_RAR_ERR_RESOURCE_LIMIT)
        return QW_COMIC_ERR_RESOURCE_LIMIT;
    if (status != QW_RAR_OK && status != QW_RAR_STOPPED)
        return status == QW_RAR_ERR_UNSUPPORTED ? QW_COMIC_ERR_UNSUPPORTED
                                                : QW_COMIC_ERR_FORMAT;
    if (collect.unsupported) return QW_COMIC_ERR_UNSUPPORTED;
    return archive->page_count ? QW_COMIC_OK : QW_COMIC_ERR_FORMAT;
}

QwComicStatus qw_comic_archive_open(const void *data, size_t size,
                                   QwComicArchiveFormat format,
                                   const char *password,
                                   QwComicArchive **out_archive)
{
    QwComicArchive *archive;
    if (!out_archive) return QW_COMIC_ERR_INVALID_ARGUMENT;
    *out_archive = NULL;
    if (!data || !size) return QW_COMIC_ERR_INVALID_ARGUMENT;
    if (format != QW_COMIC_ARCHIVE_ZIP && format != QW_COMIC_ARCHIVE_USTAR &&
        format != QW_COMIC_ARCHIVE_SEVEN_ZIP &&
        format != QW_COMIC_ARCHIVE_RAR)
        return QW_COMIC_ERR_UNSUPPORTED;
    archive = (QwComicArchive *)calloc(1u, sizeof(*archive));
    if (!archive) return QW_COMIC_ERR_OUT_OF_MEMORY;
    archive->input = (const uint8_t *)data;
    archive->input_size = size;
    archive->format = format;
    if (password && *password) {
        size_t length = strlen(password);
        archive->password = (char *)malloc(length + 1u);
        if (!archive->password) {
            comic_release_contents(archive);
            free(archive);
            return QW_COMIC_ERR_OUT_OF_MEMORY;
        }
        memcpy(archive->password, password, length + 1u);
    }
    if (format == QW_COMIC_ARCHIVE_RAR) {
        QwComicStatus rar_status = comic_open_rar(
            archive, (const uint8_t *)data, size);
        if (rar_status != QW_COMIC_OK) {
            comic_release_contents(archive);
            free(archive);
            return rar_status;
        }
    } else if (format == QW_COMIC_ARCHIVE_SEVEN_ZIP) {
        QwComicStatus seven_status = comic_open_seven_zip(
            archive, (const uint8_t *)data, size, password);
        if (seven_status != QW_COMIC_OK) {
            comic_release_contents(archive);
            free(archive);
            return seven_status;
        }
    } else if (format == QW_COMIC_ARCHIVE_ZIP) {
        size_t count;
        const char *error = NULL;
        QwArchiveStatus status = qw_zip_archive_open(
            data, size, &archive->zip, &error);
        (void)error;
        if (status != QW_ARCHIVE_OK) {
            comic_release_contents(archive);
            free(archive);
            if (status == QW_ARCHIVE_ERR_OUT_OF_MEMORY)
                return QW_COMIC_ERR_OUT_OF_MEMORY;
            if (status == QW_ARCHIVE_ERR_RESOURCE_LIMIT)
                return QW_COMIC_ERR_RESOURCE_LIMIT;
            return status == QW_ARCHIVE_ERR_UNSUPPORTED
                ? QW_COMIC_ERR_UNSUPPORTED : QW_COMIC_ERR_FORMAT;
        }
        count = qw_zip_archive_entry_count(archive->zip);
        for (size_t i = 0u; i < count; ++i) {
            QwArchiveEntry entry;
            QwArchiveStatus entry_status = qw_zip_archive_get_entry(
                archive->zip, i, &entry);
            if (entry_status != QW_ARCHIVE_OK) {
                comic_release_contents(archive);
                free(archive);
                return entry_status == QW_ARCHIVE_ERR_OUT_OF_MEMORY
                    ? QW_COMIC_ERR_OUT_OF_MEMORY : QW_COMIC_ERR_FORMAT;
            }
            if (entry.is_directory || !entry.name_bytes ||
                memchr(entry.name_bytes, 0, entry.name_size) ||
                !comic_name_is_image(entry.name_bytes, entry.name_size))
                continue;
            if (!comic_add_page(archive, entry.name_bytes, entry.name_size,
                                entry.uncompressed_size, i, i, NULL)) {
                comic_release_contents(archive);
                free(archive);
                return QW_COMIC_ERR_OUT_OF_MEMORY;
            }
        }
    } else {
        QwComicTarCollect collect;
        QwArchiveStatus status;
        memset(&collect, 0, sizeof(collect));
        collect.archive = archive;
        collect.status = QW_COMIC_OK;
        status = qw_tar_archive_visit_ustar(data, size, comic_tar_collect,
                                            &collect);
        if (collect.status != QW_COMIC_OK) {
            comic_release_contents(archive);
            free(archive);
            return collect.status;
        }
        if (status != QW_ARCHIVE_OK) {
            comic_release_contents(archive);
            free(archive);
            return status == QW_ARCHIVE_ERR_RESOURCE_LIMIT
                ? QW_COMIC_ERR_RESOURCE_LIMIT : QW_COMIC_ERR_FORMAT;
        }
    }
    if (archive->page_count > 1u) {
        qsort(archive->pages, archive->page_count, sizeof(*archive->pages),
              comic_compare_pages);
    }
    *out_archive = archive;
    return QW_COMIC_OK;
}

QwComicArchiveFormat qw_comic_archive_format(const QwComicArchive *archive)
{
    return archive ? archive->format : (QwComicArchiveFormat)0;
}

size_t qw_comic_archive_page_count(const QwComicArchive *archive)
{
    return archive ? archive->page_count : 0u;
}

QwComicStatus qw_comic_archive_page_info(const QwComicArchive *archive,
                                         size_t page_index,
                                         QwComicPageInfo *out_info)
{
    const QwComicPage *page;
    if (!archive || !out_info || page_index >= archive->page_count)
        return QW_COMIC_ERR_INVALID_ARGUMENT;
    page = &archive->pages[page_index];
    out_info->name_bytes = page->name_bytes;
    out_info->name_size = page->name_size;
    out_info->uncompressed_size = page->uncompressed_size;
    out_info->is_empty = page->uncompressed_size == 0u;
    return QW_COMIC_OK;
}

QwComicStatus qw_comic_archive_read_page(QwComicArchive *archive,
                                         size_t page_index,
                                         uint8_t **out_data,
                                         size_t *out_size)
{
    QwComicPage *page;
    if (!archive || !out_data || !out_size || page_index >= archive->page_count)
        return QW_COMIC_ERR_INVALID_ARGUMENT;
    *out_data = NULL;
    *out_size = 0u;
    page = &archive->pages[page_index];
    if (page->uncompressed_size > SIZE_MAX)
        return QW_COMIC_ERR_RESOURCE_LIMIT;
    if (archive->format == QW_COMIC_ARCHIVE_RAR ||
        archive->format == QW_COMIC_ARCHIVE_SEVEN_ZIP) {
        uint8_t *copy = (uint8_t *)malloc(page->uncompressed_size
                                              ? (size_t)page->uncompressed_size
                                              : 1u);
        if (!copy) return QW_COMIC_ERR_OUT_OF_MEMORY;
        if (page->uncompressed_size)
            memcpy(copy, page->owned_data, (size_t)page->uncompressed_size);
        *out_data = copy;
        *out_size = (size_t)page->uncompressed_size;
        return QW_COMIC_OK;
    }
    if (archive->format == QW_COMIC_ARCHIVE_USTAR) {
        uint8_t *copy = (uint8_t *)malloc(page->uncompressed_size
                                              ? (size_t)page->uncompressed_size
                                              : 1u);
        if (!copy) return QW_COMIC_ERR_OUT_OF_MEMORY;
        if (page->uncompressed_size)
            memcpy(copy, page->tar_data, (size_t)page->uncompressed_size);
        *out_data = copy;
        *out_size = (size_t)page->uncompressed_size;
        return QW_COMIC_OK;
    }
    {
        QwArchiveBuffer buffer = {0};
        QwArchiveStatus status = qw_zip_archive_extract(
            archive->zip, page->archive_index, archive->password, &buffer);
        if (status != QW_ARCHIVE_OK) {
            if (status == QW_ARCHIVE_ERR_OUT_OF_MEMORY)
                return QW_COMIC_ERR_OUT_OF_MEMORY;
            if (status == QW_ARCHIVE_ERR_RESOURCE_LIMIT)
                return QW_COMIC_ERR_RESOURCE_LIMIT;
            if (status == QW_ARCHIVE_ERR_UNSUPPORTED)
                return QW_COMIC_ERR_UNSUPPORTED;
            return QW_COMIC_ERR_EXTRACT;
        }
        if ((uint64_t)buffer.size != page->uncompressed_size) {
            qw_archive_buffer_free(&buffer);
            return QW_COMIC_ERR_FORMAT;
        }
        *out_data = buffer.data;
        *out_size = buffer.size;
        return QW_COMIC_OK;
    }
}

void qw_comic_page_data_free(uint8_t *data)
{
    free(data);
}

void qw_comic_archive_close(QwComicArchive *archive)
{
    if (!archive) return;
    comic_release_contents(archive);
    free(archive);
}
