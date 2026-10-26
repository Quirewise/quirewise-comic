#ifndef QUIREWISE_COMIC_H
#define QUIREWISE_COMIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum QwComicStatus {
    QW_COMIC_OK = 0,
    QW_COMIC_ERR_INVALID_ARGUMENT = -1,
    QW_COMIC_ERR_OUT_OF_MEMORY = -2,
    QW_COMIC_ERR_FORMAT = -3,
    QW_COMIC_ERR_UNSUPPORTED = -4,
    QW_COMIC_ERR_RESOURCE_LIMIT = -5,
    QW_COMIC_ERR_EXTRACT = -6
} QwComicStatus;

typedef enum QwComicArchiveFormat {
    QW_COMIC_ARCHIVE_ZIP = 1,
    QW_COMIC_ARCHIVE_USTAR = 2
} QwComicArchiveFormat;

typedef struct QwComicArchive QwComicArchive;

typedef struct QwComicPageInfo {
    /* Filename bytes preserve the archive's encoding and are borrowed until
     * the archive is closed; they are not guaranteed to be UTF-8. */
    const uint8_t *name_bytes;
    size_t name_size;
    uint64_t uncompressed_size;
    int is_empty;
} QwComicPageInfo;

/* The container input is borrowed until archive close. password may be NULL
 * for unencrypted ZIP archives and is ignored for USTAR containers. */
QwComicStatus qw_comic_archive_open(const void *data, size_t size,
                                   QwComicArchiveFormat format,
                                   const char *password,
                                   QwComicArchive **out_archive);
QwComicArchiveFormat qw_comic_archive_format(
    const QwComicArchive *archive);
size_t qw_comic_archive_page_count(const QwComicArchive *archive);
QwComicStatus qw_comic_archive_page_info(const QwComicArchive *archive,
                                         size_t page_index,
                                         QwComicPageInfo *out_info);
/* Returned page bytes are owned by the caller and released with
 * qw_comic_page_data_free. */
QwComicStatus qw_comic_archive_read_page(QwComicArchive *archive,
                                         size_t page_index,
                                         uint8_t **out_data,
                                         size_t *out_size);
void qw_comic_page_data_free(uint8_t *data);
void qw_comic_archive_close(QwComicArchive *archive);

#ifdef __cplusplus
}
#endif

#endif
