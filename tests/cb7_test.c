/* CB7 (7z) comic archives expose image pages like CBZ/CBT. */

#include <quirewise/comic.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef QUIREWISE_CB7_SAMPLE
#error "QUIREWISE_CB7_SAMPLE must be defined"
#endif

static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *data;
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    data = (uint8_t *)malloc((size_t)size);
    if (!data) { fclose(file); return NULL; }
    if (fread(data, 1u, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = (size_t)size;
    return data;
}

int main(void)
{
    uint8_t *data;
    size_t size = 0u;
    QwComicArchive *archive = NULL;
    QwComicStatus status;
    size_t index;

    data = read_file(QUIREWISE_CB7_SAMPLE, &size);
    if (!data || !size) {
        fputs("FAIL: cannot read the CB7 fixture\n", stderr);
        return 1;
    }
    status = qw_comic_archive_open(data, size, QW_COMIC_ARCHIVE_SEVEN_ZIP, NULL,
                                   &archive);
    if (status != QW_COMIC_OK || !archive) {
        fprintf(stderr, "FAIL: CB7 open status %d\n", (int)status);
        free(data);
        return 1;
    }
    if (qw_comic_archive_page_count(archive) != 2u) {
        fprintf(stderr, "FAIL: CB7 page count %zu\n",
                qw_comic_archive_page_count(archive));
        qw_comic_archive_close(archive);
        free(data);
        return 1;
    }
    for (index = 0u; index < 2u; ++index) {
        QwComicPageInfo info;
        uint8_t *page = NULL;
        size_t page_size = 0u;
        if (qw_comic_archive_page_info(archive, index, &info) != QW_COMIC_OK ||
            !info.name_bytes || info.name_size < 7u ||
            memcmp(info.name_bytes, "page", 4u) != 0) {
            fprintf(stderr, "FAIL: CB7 page %zu name\n", index);
            qw_comic_archive_close(archive);
            free(data);
            return 1;
        }
        if (qw_comic_archive_read_page(archive, index, &page, &page_size) !=
                QW_COMIC_OK ||
            !page || page_size < 8u || page[0] != 0x89u || page[1] != 'P' ||
            page[2] != 'N' || page[3] != 'G') {
            fprintf(stderr, "FAIL: CB7 page %zu data\n", index);
            qw_comic_page_data_free(page);
            qw_comic_archive_close(archive);
            free(data);
            return 1;
        }
        qw_comic_page_data_free(page);
    }
    qw_comic_archive_close(archive);
    free(data);
    puts("Quirewise CB7 comic tests passed");
    return 0;
}
