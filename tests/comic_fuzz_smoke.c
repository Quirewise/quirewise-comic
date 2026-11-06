#include <quirewise/comic.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FUZZ_SEED UINT32_C(0x51f06622)
#define FUZZ_RUNS 250u

static uint32_t rng_state = FUZZ_SEED;

static const uint8_t cbz_seed[116] = {
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0xae, 0x9a,
    0x66, 0x5d, 0xa4, 0x09, 0x07, 0x7a, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00,
    0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x31, 0x2e, 0x70, 0x6e, 0x67, 0x89,
    0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x50, 0x4b, 0x01, 0x02, 0x14,
    0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0xae, 0x9a, 0x66, 0x5d, 0xa4,
    0x09, 0x07, 0x7a, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x31, 0x2e, 0x70, 0x6e, 0x67, 0x50, 0x4b,
    0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x33, 0x00,
    0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint32_t next_random(void)
{
    uint32_t value = rng_state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    rng_state = value;
    return value;
}

static void put_octal(char *field, size_t width, unsigned int value)
{
    size_t index;
    for (index = 0u; index < width - 1u; ++index) field[index] = '0';
    field[width - 1u] = '\0';
    for (index = width - 1u; index > 0u && value; --index) {
        field[index - 1u] = (char)('0' + (value & 7u));
        value >>= 3;
    }
}

static size_t build_ustar(uint8_t *archive, size_t capacity)
{
    unsigned int checksum = 0u;
    size_t index;
    if (capacity < 1536u) return 0u;
    memset(archive, 0, 1536u);
    memcpy(archive, "1.png", 5u);
    put_octal((char *)archive + 100, 8u, 0644u);
    put_octal((char *)archive + 124, 12u, 8u);
    memset(archive + 148, ' ', 8u);
    archive[156] = '0';
    memcpy(archive + 257, "ustar", 6u);
    memcpy(archive + 263, "00", 2u);
    for (index = 0u; index < 512u; ++index) checksum += archive[index];
    put_octal((char *)archive + 148, 7u, checksum);
    archive[155] = ' ';
    memcpy(archive + 512, "\x89PNG\r\n\x1a\n", 8u);
    return 1024u;
}

static void mutate_bytes(const uint8_t *seed, size_t seed_size,
                         uint8_t *buffer, size_t capacity, size_t *out_size)
{
    size_t length = seed_size > capacity ? capacity : seed_size;
    unsigned int flips = 1u + next_random() % 4u;
    size_t index;
    memcpy(buffer, seed, length);
    if (next_random() % 8u == 0u && length > 1u)
        length = 1u + next_random() % length;
    for (index = 0u; index < flips && length; ++index) {
        size_t position = next_random() % length;
        buffer[position] = (uint8_t)(buffer[position] ^
                                     (uint8_t)(1u << (next_random() % 8u)));
    }
    *out_size = length;
}

int main(void)
{
    uint8_t buffer[1536];
    size_t run;
    size_t ok = 0u;
    size_t rejected = 0u;
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        QwComicArchive *archive = NULL;
        size_t size = 0u;
        mutate_bytes(cbz_seed, sizeof(cbz_seed), buffer, sizeof(buffer), &size);
        if (qw_comic_archive_open(buffer, size, QW_COMIC_ARCHIVE_ZIP, NULL,
                                  &archive) == QW_COMIC_OK && archive) {
            ++ok;
            if (qw_comic_archive_page_count(archive) > 0u) {
                uint8_t *page = NULL;
                size_t page_size = 0u;
                if (qw_comic_archive_read_page(archive, 0u, &page, &page_size) ==
                    QW_COMIC_OK)
                    qw_comic_page_data_free(page);
            }
            qw_comic_archive_close(archive);
        } else {
            ++rejected;
        }
    }
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        QwComicArchive *archive = NULL;
        size_t size = 0u;
        size_t tar_size;
        uint8_t tar[1536];
        tar_size = build_ustar(tar, sizeof(tar));
        mutate_bytes(tar, tar_size, buffer, sizeof(buffer), &size);
        if (qw_comic_archive_open(buffer, size, QW_COMIC_ARCHIVE_USTAR, NULL,
                                  &archive) == QW_COMIC_OK && archive) {
            ++ok;
            qw_comic_archive_close(archive);
        } else {
            ++rejected;
        }
    }
    printf("Comic fuzz smoke: %zu ok, %zu rejected, seed=0x%08x runs=%u\n", ok,
           rejected, FUZZ_SEED, FUZZ_RUNS);
    return 0;
}
