#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdbool.h>
#include "lz77.h"

#define MAX_FILE_SIZE (100 * 1024 * 1024)

bool compare(const char *name, const uint8_t *a, const uint8_t *b, int size)
{
    bool bad = false;

    for (int i = 0; i < size; ++i) {
        if (a[i] != b[i]) {
            bad = true;
            printf("Error on %s!\n", name);
            printf("Different at index %d: expecting %02x,actual %02x\n", i,
                   a[i], b[i]);
            break;
        }
    }

    return bad;
}

void test_roundtrip_lz77(const char *name, const char *file_name)
{
    FILE *f = fopen(file_name, "rb");
    if (!f) {
        printf("Error: can not open %s!\n", file_name);
        exit(1);
    }
    fseek(f, 0L, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    if (file_size > MAX_FILE_SIZE) {
        fclose(f);
        printf("%25s %10ld [skipped, file too big]\n", name, file_size);
        return;
    }

    uint8_t *file_buffer = malloc(file_size);
    if (!file_buffer) {
        fclose(f);
        printf("Error: cannot allocate %ld bytes for %s\n", file_size,
               file_name);
        return;
    }
    long read = fread(file_buffer, 1, file_size, f);
    fclose(f);
    if (read != file_size) {
        free(file_buffer);
        printf("Error: only read %ld bytes!\n", read);
        exit(1);
    }

    uint8_t *compressed_buffer = malloc((size_t) (1.05 * file_size));
    if (!compressed_buffer) {
        free(file_buffer);
        printf("Error: cannot allocate compression buffer for %s\n", file_name);
        return;
    }
    uint8_t *workmem = malloc(LZ77_WORKMEM_SIZE);
    if (!workmem) {
        free(file_buffer);
        free(compressed_buffer);
        printf("Error: cannot allocate workmem for %s\n", file_name);
        return;
    }
    int compressed_size =
        lz77_compress(file_buffer, file_size, compressed_buffer, workmem);
    double ratio = (100.0 * compressed_size) / file_size;

    uint8_t *uncompressed_buffer = malloc(file_size);
    if (uncompressed_buffer == NULL) {
        free(file_buffer);
        free(compressed_buffer);
        free(workmem);
        printf("%25s %10ld  -> %10d  (%.2f%%)  skipped, can't decompress\n",
               name, file_size, compressed_size, ratio);
        return;
    }
    memset(uncompressed_buffer, '-', file_size);
    lz77_decompress(compressed_buffer, compressed_size, uncompressed_buffer,
                    file_size);
    bool result =
        compare(file_name, file_buffer, uncompressed_buffer, file_size);
    if (result) {
        free(file_buffer);
        free(compressed_buffer);
        free(workmem);
        free(uncompressed_buffer);
        exit(1);
    }

    free(file_buffer);
    free(compressed_buffer);
    free(workmem);
    free(uncompressed_buffer);
    printf("%25s %10ld  -> %10d  (%.2f%%)\n", name, file_size, compressed_size,
           ratio);
    return;
}

int main(int argc, char **argv)
{
    const char *default_prefix = "dataset/";
    const char *names[] = {
        "canterbury/alice29.txt",
        "canterbury/asyoulik.txt",
        "canterbury/cp.html",
        "canterbury/fields.c",
        "canterbury/grammar.lsp",
        "canterbury/kennedy.xls",
        "canterbury/lcet10.txt",
        "canterbury/plrabn12.txt",
        "canterbury/ptt5",
        "canterbury/sum",
        "canterbury/xargs.1",
        "silesia/dickens",
        "silesia/osdb",
        "silesia/reymont",
        "silesia/samba",
        "silesia/sao",
        "silesia/webster",
        "silesia/x-ray",
        "silesia/xml",
        "enwik/enwik8.txt",
    };

    const char *prefix = (argc == 2) ? argv[1] : default_prefix;

    const int count = sizeof(names) / sizeof(names[0]);

    printf("Test round-trip for lz77\n\n");
    for (int i = 0; i < count; ++i) {
        const char *name = names[i];
        size_t filename_len = strlen(prefix) + strlen(name) + 1;
        char *filename = malloc(filename_len);
        if (!filename) {
            printf("Error: cannot allocate filename for %s\n", name);
            continue;
        }
        snprintf(filename, filename_len, "%s%s", prefix, name);
        test_roundtrip_lz77(name, filename);
        free(filename);
    }
    printf("\n");

    return 0;
}
