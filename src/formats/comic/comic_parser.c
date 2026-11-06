#include "quirewise/comic.h"

#include "quirewise/archive.h"
#include "../../core/natural_sort.h"

#include <stdlib.h>
#include <string.h>

typedef struct QwComicPage {
    uint8_t *name_bytes;
    size_t name_size;
    uint64_t uncompressed_size;
    size_t archive_index;
    size_t ordinal;
    const uint8_t *tar_data;
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
    for (size_t i = 0u; i < archive->page_count; ++i)
        free(archive->pages[i].name_bytes);
    free(archive->pages);
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
    if (format != QW_COMIC_ARCHIVE_ZIP && format != QW_COMIC_ARCHIVE_USTAR)
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
    if (format == QW_COMIC_ARCHIVE_ZIP) {
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
