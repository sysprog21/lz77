#include <libgen.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lz77.h"

/* Compression block size (128 KiB).
 * Trade-off:
 * - Larger blocks → better compression (more match opportunities)
 * - Smaller blocks → less memory, faster random access
 *
 * 128 KiB is chosen: Balances memory usage with compression efficiency, aligns
 * with typical L2 cache sizes, reduces I/O syscall overhead.
 */
#define BLOCK_SIZE (2 * 64 * 1024)

/* Resource limits to prevent zip-bomb attacks */
#define MAX_COMPRESSED_CHUNK (8 * 1024 * 1024)    /* 8 MiB */
#define MAX_DECOMPRESSED_CHUNK (16 * 1024 * 1024) /* 16 MiB */

/* Named constants for magic numbers and chunk types */
#define MZIP_MAGIC_SIZE 8
#define MZIP_CHUNK_HEADER_SIZE 16
#define MZIP_FILEINFO_CHUNK_ID 1
#define MZIP_DATA_CHUNK_ID 17
#define MZIP_FILEINFO_FIXED_SIZE 10

/* magic identifier for mzip file */
static const uint8_t mzip_magic[MZIP_MAGIC_SIZE] = {
    '$', 'm', 'z', 'i', 'p', '$', '$', '$',
};

static void write_magic(FILE *file)
{
    fwrite(mzip_magic, MZIP_MAGIC_SIZE, 1, file);
}

static bool detect_magic(FILE *file)
{
    uint8_t buffer[MZIP_MAGIC_SIZE];

    fseek(file, 0, SEEK_SET);
    size_t bytes_read = fread(buffer, 1, MZIP_MAGIC_SIZE, file);
    fseek(file, 0, SEEK_SET);

    if (bytes_read < MZIP_MAGIC_SIZE)
        return false;

    for (int c = 0; c < MZIP_MAGIC_SIZE; c++) {
        if (buffer[c] != mzip_magic[c])
            return false;
    }

    return true;
}

static void write_chunk_header(FILE *file,
                               uint16_t id,
                               uint16_t options,
                               uint32_t size,
                               uint32_t checksum,
                               uint32_t extra)
{
    uint8_t buffer[16] = {
        [0] = id & 255,
        [1] = id >> 8,
        [2] = options & 255,
        [3] = options >> 8,
        [4] = size & 255,
        [5] = (size >> 8) & 255,
        [6] = (size >> 16) & 255,
        [7] = (size >> 24) & 255,
        [8] = checksum & 255,
        [9] = (checksum >> 8) & 255,
        [10] = (checksum >> 16) & 255,
        [11] = (checksum >> 24) & 255,
        [12] = extra & 255,
        [13] = (extra >> 8) & 255,
        [14] = (extra >> 16) & 255,
        [15] = (extra >> 24) & 255,
    };

    fwrite(buffer, 16, 1, file);
}

/* Adler-32 checksum (RFC 1950 Section 8.2) */
#define ADLER32_BASE 65521
static uint32_t update_adler32(uint32_t checksum, const void *buf, size_t len)
{
    const uint8_t *ptr = (const uint8_t *) buf;
    uint32_t s1 = checksum & 0xffff;
    uint32_t s2 = (checksum >> 16) & 0xffff;

    /* Process in chunks to defer modulo operations */
    while (len > 0) {
        size_t k = len < 5552 ? len : 5552;
        len -= k;
        while (k--)
            s2 += (s1 += *ptr++);
        s1 %= ADLER32_BASE;
        s2 %= ADLER32_BASE;
    }

    return (s2 << 16) | s1;
}

static inline uint16_t read_u16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

static inline uint32_t read_u32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

/**
 * Validate filename for security against path traversal attacks.
 *
 * Rejects filenames containing:
 * - Directory traversal sequences (..)
 * - Absolute paths (/ or \)
 * - Path separators (enforce basename only)
 * - Control characters (ASCII < 32 or DEL)
 * - Empty or excessively long names (>255 chars)
 *
 * @param filename Path to validate
 * @return true if safe for use, false if potentially malicious
 */
static bool is_safe_filename(const char *filename)
{
    if (!filename || !*filename)
        return false;

    /* Reject empty or too-long filenames */
    size_t len = strlen(filename);
    if (len == 0 || len > 255)
        return false;

    /* Reject absolute paths */
    if (filename[0] == '/' || filename[0] == '\\')
        return false;

    /* Check each character for safety */
    for (const char *p = filename; *p; p++) {
        /* Reject control characters */
        if ((unsigned char) *p < 32 || *p == 127)
            return false;

        /* Reject directory traversal (..) */
        if (*p == '.' && *(p + 1) == '.')
            return false;

        /* Reject path separators (enforce basename only) */
        if (*p == '/' || *p == '\\')
            return false;
    }

    /* Reject special directory names */
    if (!strcmp(filename, ".") || !strcmp(filename, ".."))
        return false;

    return true;
}

static bool read_chunk_header(FILE *file,
                              uint16_t *id,
                              uint16_t *options,
                              uint32_t *size,
                              uint32_t *checksum,
                              uint32_t *extra)
{
    uint8_t buffer[MZIP_CHUNK_HEADER_SIZE];
    if (fread(buffer, 1, MZIP_CHUNK_HEADER_SIZE, file) !=
        MZIP_CHUNK_HEADER_SIZE)
        return false;

    *id = read_u16(buffer);
    *options = read_u16(buffer + 2);
    *size = read_u32(buffer + 4);
    *checksum = read_u32(buffer + 8);
    *extra = read_u32(buffer + 12);
    return true;
}

int pack_file_compressed(const char *ifile, FILE *ofile)
{
    FILE *in = fopen(ifile, "rb");
    if (!in) {
        printf("Error: could not open %s\n", ifile);
        return -1;
    }

    /* Use fseeko/ftello for 64-bit file size support */
    if (fseeko(in, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: could not seek to end of %s\n", ifile);
        fclose(in);
        return -1;
    }
    off_t sz = ftello(in);
    if (sz < 0) {
        fprintf(stderr, "Error: could not determine size of %s\n", ifile);
        fclose(in);
        return -1;
    }
    uint64_t file_size = (uint64_t) sz;
    if (fseeko(in, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Error: could not seek to start of %s\n", ifile);
        fclose(in);
        return -1;
    }

    if (detect_magic(in)) {
        printf("Error: file %s is already a mzip archive!\n", ifile);
        fclose(in);
        return -1;
    }

    /* truncate directory prefix, e.g. "/path/to/FILE.txt" becomes "FILE.txt" */
    const char *slash = strrchr(ifile, '/');
    const char *shown_name = slash ? slash + 1 : ifile;

    uint8_t buffer[BLOCK_SIZE] = {
        [0] = file_size & 255,
        [1] = (file_size >> 8) & 255,
        [2] = (file_size >> 16) & 255,
        [3] = (file_size >> 24) & 255,
        [4] = (file_size >> 32) & 255,
        [5] = (file_size >> 40) & 255,
        [6] = (file_size >> 48) & 255,
        [7] = (file_size >> 56) & 255,
        [8] = (strlen(shown_name) + 1) & 255,
        [9] = (strlen(shown_name) + 1) >> 8,
    };
    uint8_t result[BLOCK_SIZE * 2];
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    uint32_t checksum = update_adler32(1L, buffer, 10);
    checksum = update_adler32(checksum, shown_name, strlen(shown_name) + 1);
    write_chunk_header(ofile, 1, 0, 10 + strlen(shown_name) + 1, checksum, 0);
    fwrite(buffer, 10, 1, ofile);
    fwrite(shown_name, strlen(shown_name) + 1, 1, ofile);

    uint64_t total_read = 0;
    while (1) {
        size_t bytes_read = fread(buffer, 1, BLOCK_SIZE, in);
        total_read += bytes_read;

        if (bytes_read == 0)
            break;

        int chunk_size = lz77_compress(buffer, bytes_read, result, workmem);
        if (chunk_size <= 0 || chunk_size > (int) sizeof(result)) {
            fprintf(stderr,
                    "Error: compression failed or returned invalid size %d\n",
                    chunk_size);
            fclose(in);
            return -1;
        }
        checksum = update_adler32(1L, result, chunk_size);
        write_chunk_header(ofile, 17, 1, chunk_size, checksum, bytes_read);
        fwrite(result, 1, chunk_size, ofile);
    }
    fclose(in);

    if (total_read != file_size) {
        fprintf(stderr,
                "Error: reading %s failed (read %llu bytes, expected %llu)!\n",
                ifile, (unsigned long long) total_read,
                (unsigned long long) file_size);
        return -1;
    }

    return 0;
}

static int pack_file(const char *ifile, const char *ofile)
{
    /* Guard against NULL inputs */
    if (!ifile || !ofile) {
        fprintf(stderr, "Error: NULL file path provided\n");
        return -1;
    }

    FILE *file = fopen(ofile, "rb");
    if (file) {
        printf("Error: file %s already exists. Aborted.\n\n", ofile);
        fclose(file);
        return -1;
    }

    file = fopen(ofile, "wb");
    if (!file) {
        printf("Error: could not create %s. Aborted.\n\n", ofile);
        return -1;
    }

    write_magic(file);
    int ret = pack_file_compressed(ifile, file);
    fclose(file);

    return ret;
}

static void show_usage(bool is_compress)
{
    if (is_compress) {
        printf(
            "mzip: small file compression tool\n"
            "Usage: mzip [options] input-file output-file\n"
            "\n");
    } else {
        printf(
            "munzip: uncompress mzip archive\n"
            "Usage: munzip archive-file\n"
            "\n");
    }
}

static int handle_common_args(int argc, char **argv, bool is_compress)
{
    if (argc == 1) {
        show_usage(is_compress);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (!arg || arg[0] != '-')
            continue;

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            show_usage(is_compress);
            return 0;
        }

        printf(
            "Error: unknown option %s\n\n"
            "To get help on usage:\n"
            "  %s --help\n\n",
            arg, is_compress ? "mzip" : "munzip");
        return -1;
    }

    return 1; /* Continue processing */
}

static int compress(int argc, char **argv)
{
    char *ifile = NULL, *ofile = NULL;

    /* Handle common arguments (-h, --help, -v, --version, unknown options) */
    int result = handle_common_args(argc, argv, true);
    if (result <= 0)
        return result; /* 0 = help/version shown, -1 = error */

    /* Parse input and output file arguments */
    for (int i = 1; i < argc; i++) {
        char *argument = argv[i];

        if (!argument || argument[0] == '-')
            continue;

        /* first specified file is input */
        if (!ifile) {
            ifile = argument;
            continue;
        }

        /* next specified file is output */
        if (!ofile) {
            ofile = argument;
            continue;
        }
    }

    /* Validate that both input and output files were provided */
    if (!ifile || !ofile) {
        fprintf(stderr, "Error: missing input or output file\n\n");
        show_usage(true);
        return -1;
    }

    return pack_file(ifile, ofile);
}

static int unpack_file(const char *ifile)
{
    /* sanity check */
    FILE *in = fopen(ifile, "rb");
    if (!in) {
        printf("Error: could not open %s\n", ifile);
        return -1;
    }

    /* find size of the file */
    fseek(in, 0, SEEK_END);
    long fsize = ftell(in);
    fseek(in, 0, SEEK_SET);

    /* Initialize variables before early-exit checks */
    FILE *out = NULL;
    uint8_t *compressed_buffer = NULL, *decompressed_buffer = NULL;
    char *ofile_name = NULL;
    int status = -1;

    /* not a mzip archive */
    if (!detect_magic(in)) {
        fclose(in);
        in = NULL;
        fprintf(stderr, "Error: file %s is not a mzip archive!\n", ifile);
        goto cleanup;
    }

    /* position of first chunk */
    fseek(in, 8, SEEK_SET);

    uint8_t buffer[BLOCK_SIZE];
    uint32_t decompressed_size = 0;
    size_t total_extracted __attribute__((unused)) = 0;
    size_t compressed_bufsize = 0, decompressed_bufsize = 0;

    while (1) {
        long pos = ftell(in);
        if (pos >= fsize)
            break;

        uint16_t chunk_id, chunk_options;
        uint32_t chunk_size, chunk_checksum, chunk_extra;
        if (!read_chunk_header(in, &chunk_id, &chunk_options, &chunk_size,
                               &chunk_checksum, &chunk_extra)) {
            fprintf(stderr, "Error: failed to read chunk header\n");
            goto cleanup;
        }

        if ((chunk_id == MZIP_FILEINFO_CHUNK_ID) && (chunk_size > 10) &&
            (chunk_size < BLOCK_SIZE)) {
            if (fread(buffer, 1, chunk_size, in) != chunk_size) {
                fprintf(stderr, "Error: failed to read file info chunk\n");
                goto cleanup;
            }
            uint32_t checksum = update_adler32(1L, buffer, chunk_size);

            if (checksum != chunk_checksum) {
                fprintf(stderr, "\nError: checksum mismatch!\n");
                fprintf(stderr, "Got %08X Expecting %08X\n", checksum,
                        chunk_checksum);
                goto cleanup;
            }

            total_extracted = 0;
            decompressed_size = read_u32(buffer);

            size_t fname_len = read_u16(buffer + 8);
            if (fname_len > chunk_size - MZIP_FILEINFO_FIXED_SIZE)
                fname_len = chunk_size - 10;

            free(ofile_name);
            ofile_name = malloc(fname_len + 1);
            if (!ofile_name) {
                fprintf(stderr,
                        "Error: cannot allocate output filename buffer\n");
                goto cleanup;
            }
            memset(ofile_name, 0, fname_len + 1);
            for (int c = 0; c < fname_len; c++)
                ofile_name[c] = buffer[10 + c];

            /* Validate filename for security (prevent path traversal) */
            if (!is_safe_filename(ofile_name)) {
                fprintf(stderr,
                        "Error: unsafe filename '%s' rejected (potential path "
                        "traversal attack)\n",
                        ofile_name);
                goto cleanup;
            }

            /* Create file exclusively (fails if already exists) */
            /* Note: "wx" mode is C11, falls back to manual check for C99 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
            out = fopen(ofile_name, "wbx");
            if (!out) {
                fprintf(
                    stderr,
                    "File %s already exists or cannot be created. Skipped.\n",
                    ofile_name);
                goto cleanup;
            }
#else
            /* C99 fallback: check then create (minor TOCTOU window) */
            out = fopen(ofile_name, "rb");
            if (out) {
                fprintf(stderr, "File %s already exists. Skipped.\n",
                        ofile_name);
                fclose(out);
                out = NULL;
                goto cleanup;
            }
            out = fopen(ofile_name, "wb");
            if (!out) {
                fprintf(stderr, "Can't create file %s. Skipped.\n", ofile_name);
                goto cleanup;
            }
#endif
        }

        if ((chunk_id == MZIP_DATA_CHUNK_ID) && out && ofile_name &&
            decompressed_size) {
            /* Enforce resource limits to prevent zip-bomb attacks */
            if (chunk_size > MAX_COMPRESSED_CHUNK) {
                fprintf(stderr,
                        "Error: compressed chunk size %u exceeds limit %u\n",
                        chunk_size, MAX_COMPRESSED_CHUNK);
                goto cleanup;
            }
            if (chunk_extra > MAX_DECOMPRESSED_CHUNK) {
                fprintf(stderr,
                        "Error: decompressed chunk size %u exceeds limit %u\n",
                        chunk_extra, MAX_DECOMPRESSED_CHUNK);
                goto cleanup;
            }

            /* enlarge input buffer if necessary */
            if (chunk_size > compressed_bufsize) {
                compressed_bufsize = chunk_size;
                uint8_t *tmp = realloc(compressed_buffer, compressed_bufsize);
                if (!tmp) {
                    fprintf(stderr,
                            "Error: cannot allocate compressed buffer\n");
                    goto cleanup;
                }
                compressed_buffer = tmp;
            }

            /* enlarge output buffer if necessary */
            if (chunk_extra > decompressed_bufsize) {
                decompressed_bufsize = chunk_extra;
                uint8_t *tmp =
                    realloc(decompressed_buffer, decompressed_bufsize);
                if (!tmp) {
                    fprintf(stderr,
                            "Error: cannot allocate decompressed buffer\n");
                    goto cleanup;
                }
                decompressed_buffer = tmp;
            }

            /* read and check checksum */
            if (!compressed_buffer ||
                fread(compressed_buffer, 1, chunk_size, in) != chunk_size) {
                fprintf(stderr, "Error: cannot read compressed chunk\n");
                goto cleanup;
            }
            uint32_t checksum =
                update_adler32(1L, compressed_buffer, chunk_size);
            total_extracted += chunk_extra;

            /* verify that the chunk data is correct */
            if (checksum != chunk_checksum) {
                fprintf(stderr, "\nError: checksum mismatch. Skipped.\n");
                fprintf(stderr, "Got %08X Expecting %08X\n", checksum,
                        chunk_checksum);
                goto cleanup;
            } else {
                /* decompress and verify */
                uint32_t remaining =
                    lz77_decompress(compressed_buffer, chunk_size,
                                    decompressed_buffer, chunk_extra);
                if (remaining != chunk_extra) {
                    fprintf(stderr,
                            "\nError: decompression failed. Skipped.\n");
                    goto cleanup;
                } else {
                    fwrite(decompressed_buffer, 1, chunk_extra, out);
                }
            }
        }

        /* position of next chunk */
        fseek(in, pos + 16 + chunk_size, SEEK_SET);
    }

    status = 0;

cleanup:
    free(compressed_buffer);
    free(decompressed_buffer);
    free(ofile_name);
    if (out)
        fclose(out);
    if (in)
        fclose(in);

    return status;
}

static int decompress(int argc, char **argv)
{
    /* Handle common arguments (-h, --help, -v, --version, unknown options) */
    int result = handle_common_args(argc, argv, false);
    if (result <= 0)
        return result; /* 0 = help/version shown, -1 = error */

    /* needs at least one non-option argument (archive file) */
    if (argc <= 1) {
        show_usage(false);
        return 0;
    }

    return unpack_file(argv[1]);
}

/* Busybox-style entry */
int main(int argc, char **argv)
{
    char *progname = basename(argv[0]);

    /* Check if program name contains "unzip" or ends with decompress-related
     * names.
     */
    if (strstr(progname, "unzip") || strstr(progname, "decompress") ||
        !strcmp(progname, "munzip")) {
        return decompress(argc, argv);
    }

    /* Default to compression mode */
    return compress(argc, argv);
}
