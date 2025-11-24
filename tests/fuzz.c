/**
 * libFuzzer harness for LZ77 compressor and decompressor
 *
 * Test strategies:
 * 1. Direct decompression of fuzzed input (malformed input handling)
 * 2. Round-trip: compress fuzzed data, decompress, verify exact match
 * 3. Compressor boundary conditions (MIN_INPUT_SIZE, MAX_LEN, MAX_DISTANCE)
 * 4. Decompressor boundary conditions (truncated, corrupted, invalid refs)
 * 5. Overlapping copy stress (distance < length cases)
 * 6. Format structure tests (control bytes, extended lengths, distances)
 * 7. Random output buffer sizes (catch over-writes)
 * 8. Mutated compressed stream
 * 9. Truncated token edge cases (len=6 boundary, incomplete tokens)
 * 10. Output buffer overflow tests (decoded size > max_out)
 * 11. Distance edge handling (dist=0, dist=MAX_DISTANCE, dist>produced)
 * 12. Multi-chunk match tests (matches > MAX_LEN)
 * 13. API edge cases (length=0, max_out=0, negative, match-first)
 *
 * Run: ./fuzz -max_len=65536 -timeout=10 corpus/
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../lz77.h"

/* Maximum sizes for fuzzing */
#define MAX_IN_SIZE (256 * 1024)
#define MAX_OUT_SIZE (512 * 1024)
#define MAX_DECOMP_SIZE (4 * 1024 * 1024)

/* Static buffers to avoid malloc overhead */
static uint8_t g_compressed[MAX_OUT_SIZE];
static uint8_t g_decompressed[MAX_DECOMP_SIZE];
static uint8_t g_workmem[LZ77_WORKMEM_SIZE];
static uint8_t g_scratch[MAX_DECOMP_SIZE];

/* Simple PRNG for deterministic randomization based on input */
static uint32_t fuzz_rand(const uint8_t *data, size_t size, uint32_t seed)
{
    uint32_t h = seed;
    for (size_t i = 0; i < size && i < 64; i++)
        h = h * 31 + data[i];
    return h;
}

/* Test 1: Direct decompression of fuzzed input
 *
 * Note: lz77_decompress returns 0 on error, not negative.
 * We allow 0 here since fuzzed input may be invalid.
 */
static void fuzz_decompress(const uint8_t *data, size_t size)
{
    if (size == 0)
        return;

    int result =
        lz77_decompress(data, (int) size, g_decompressed, MAX_DECOMP_SIZE);

    /* Result must be in valid range (0 is valid for error/empty) */
    if (result < 0 || result > MAX_DECOMP_SIZE)
        __builtin_trap();
}

/* Test 2: Round-trip compression/decompression */
static void fuzz_roundtrip(const uint8_t *data, size_t size)
{
    if (size == 0 || size > MAX_IN_SIZE)
        return;

    int compressed_size =
        lz77_compress(data, (int) size, g_compressed, g_workmem);

    if (compressed_size <= 0)
        __builtin_trap();

    if (compressed_size > MAX_OUT_SIZE)
        __builtin_trap();

    int decompressed_size = lz77_decompress(g_compressed, compressed_size,
                                            g_decompressed, MAX_DECOMP_SIZE);

    if (decompressed_size != (int) size)
        __builtin_trap();

    if (memcmp(data, g_decompressed, size) != 0)
        __builtin_trap();
}

/* Test 3: Compressor boundary conditions */
static void fuzz_compress_boundaries(const uint8_t *data, size_t size)
{
    if (size < 2)
        return;

    uint8_t variant = data[0] % 10;
    const uint8_t *payload = data + 1;
    size_t payload_size = size - 1;

    switch (variant) {
    case 0:
        /* MIN_INPUT_SIZE boundary */
        if (payload_size >= MIN_INPUT_SIZE) {
            int r =
                lz77_compress(payload, MIN_INPUT_SIZE, g_compressed, g_workmem);
            if (r <= 0)
                __builtin_trap();
        }
        break;

    case 1:
        /* MIN_INPUT_SIZE - 1 (literal path) */
        if (payload_size >= MIN_INPUT_SIZE - 1) {
            int r = lz77_compress(payload, MIN_INPUT_SIZE - 1, g_compressed,
                                  g_workmem);
            if (r <= 0)
                __builtin_trap();
        }
        break;

    case 2:
        /* Highly repetitive (stress match encoding) */
        if (payload_size > 0) {
            size_t repeat_size = (payload_size < 2048) ? payload_size : 2048;
            memset(g_scratch, payload[0], repeat_size);
            int r = lz77_compress(g_scratch, (int) repeat_size, g_compressed,
                                  g_workmem);
            if (r <= 0)
                __builtin_trap();
            int d = lz77_decompress(g_compressed, r, g_decompressed,
                                    MAX_DECOMP_SIZE);
            if (d != (int) repeat_size)
                __builtin_trap();
        }
        break;

    case 3:
        /* MAX_DISTANCE boundary - matches at window edge */
        if (payload_size >= 32) {
            size_t test_size = (payload_size < MAX_DISTANCE + 512)
                                   ? payload_size
                                   : MAX_DISTANCE + 512;
            int r = lz77_compress(payload, (int) test_size, g_compressed,
                                  g_workmem);
            if (r <= 0)
                __builtin_trap();
            int d = lz77_decompress(g_compressed, r, g_decompressed,
                                    MAX_DECOMP_SIZE);
            if (d != (int) test_size ||
                memcmp(payload, g_decompressed, test_size) != 0)
                __builtin_trap();
        }
        break;

    case 4:
        /* MAX_LEN boundary - very long matches */
        if (payload_size >= 4) {
            size_t long_size = MAX_LEN * 4;
            for (size_t i = 0; i < long_size; i++)
                g_scratch[i] = payload[i % payload_size];
            int r = lz77_compress(g_scratch, (int) long_size, g_compressed,
                                  g_workmem);
            if (r <= 0)
                __builtin_trap();
            int d = lz77_decompress(g_compressed, r, g_decompressed,
                                    (int) long_size);
            if (d != (int) long_size ||
                memcmp(g_scratch, g_decompressed, long_size) != 0)
                __builtin_trap();
        }
        break;

    case 5:
        /* MAX_COPY boundary - literal runs */
        if (payload_size >= MAX_COPY + 8) {
            /* Create data that forces MAX_COPY literal chunks */
            for (size_t i = 0; i < MAX_COPY * 3; i++)
                g_scratch[i] = (uint8_t) (i * 17 + payload[i % payload_size]);
            int r =
                lz77_compress(g_scratch, MAX_COPY * 3, g_compressed, g_workmem);
            if (r <= 0)
                __builtin_trap();
        }
        break;

    case 6:
        /* Distance = 1 (run-length encoding pattern) */
        if (payload_size >= 1) {
            /* Pattern: "AAAAAA..." - distance 1 matches */
            size_t rle_size = 512;
            memset(g_scratch, payload[0], rle_size);
            int r = lz77_compress(g_scratch, (int) rle_size, g_compressed,
                                  g_workmem);
            if (r <= 0)
                __builtin_trap();
            int d = lz77_decompress(g_compressed, r, g_decompressed,
                                    MAX_DECOMP_SIZE);
            if (d != (int) rle_size)
                __builtin_trap();
            for (size_t i = 0; i < rle_size; i++) {
                if (g_decompressed[i] != payload[0])
                    __builtin_trap();
            }
        }
        break;

    case 7:
        /* Empty and minimal inputs */
        {
            int r = lz77_compress(payload, 0, g_compressed, g_workmem);
            if (r != 0)
                __builtin_trap();

            if (payload_size >= 1) {
                r = lz77_compress(payload, 1, g_compressed, g_workmem);
                if (r <= 0)
                    __builtin_trap();
            }
        }
        break;

    case 8:
        /* Hash collision stress - repeating 3-byte patterns with round-trip */
        if (payload_size >= 6) {
            for (size_t i = 0; i < 1024; i++)
                g_scratch[i] = payload[(i % 3) % payload_size];
            int r = lz77_compress(g_scratch, 1024, g_compressed, g_workmem);
            if (r <= 0)
                __builtin_trap();
            /* Add round-trip verification to catch mis-matches from stale hash
             */
            int d = lz77_decompress(g_compressed, r, g_decompressed,
                                    MAX_DECOMP_SIZE);
            if (d != 1024)
                __builtin_trap();
            if (memcmp(g_scratch, g_decompressed, 1024) != 0)
                __builtin_trap();
        }
        break;

    case 9:
        /* Alternating compressible/incompressible */
        if (payload_size >= 64) {
            for (size_t i = 0; i < 512; i++) {
                if ((i / 32) % 2 == 0)
                    g_scratch[i] = 'A'; /* Compressible */
                else
                    g_scratch[i] = payload[i % payload_size]; /* Random */
            }
            int r = lz77_compress(g_scratch, 512, g_compressed, g_workmem);
            if (r <= 0)
                __builtin_trap();
        }
        break;
    }
}

/* Test 4: Decompressor boundary conditions */
static void fuzz_decompress_boundaries(const uint8_t *data, size_t size)
{
    if (size < 2)
        return;

    uint8_t variant = data[0] % 10;
    const uint8_t *payload = data + 1;
    size_t payload_size = size - 1;

    switch (variant) {
    case 0:
        /* Tiny output buffer - should return 0 or small positive */
        if (payload_size > 0) {
            int r =
                lz77_decompress(payload, (int) payload_size, g_decompressed, 1);
            /* r >= 0 is valid (0 = error/no output, 1 = single byte) */
            if (r < 0 || r > 1)
                __builtin_trap();
        }
        break;

    case 1:
        /* Zero output buffer */
        if (payload_size > 0) {
            int r =
                lz77_decompress(payload, (int) payload_size, g_decompressed, 0);
            if (r != 0)
                __builtin_trap();
        }
        break;

    case 2:
        /* Exact-fit output buffer */
        if (payload_size >= MIN_INPUT_SIZE && payload_size <= 1024) {
            int comp_size = lz77_compress(payload, (int) payload_size,
                                          g_compressed, g_workmem);
            if (comp_size > 0) {
                int r = lz77_decompress(g_compressed, comp_size, g_decompressed,
                                        (int) payload_size);
                if (r != (int) payload_size)
                    __builtin_trap();
            }
        }
        break;

    case 3:
        /* Truncated compressed data - must handle gracefully (return 0 on
         * error) */
        if (payload_size >= MIN_INPUT_SIZE) {
            int comp_size = lz77_compress(payload, (int) payload_size,
                                          g_compressed, g_workmem);
            if (comp_size > 2) {
                for (int trunc = 1; trunc < comp_size && trunc < 16; trunc++) {
                    int r = lz77_decompress(g_compressed, trunc, g_decompressed,
                                            MAX_DECOMP_SIZE);
                    /* 0 is valid error return, r > 0 may happen for partial
                     * decode */
                    if (r < 0 || r > MAX_DECOMP_SIZE)
                        __builtin_trap();
                }
            }
        }
        break;

    case 4:
        /* Corrupted control bytes - decompressor must not crash */
        if (payload_size >= MIN_INPUT_SIZE) {
            int comp_size = lz77_compress(payload, (int) payload_size,
                                          g_compressed, g_workmem);
            if (comp_size > 0) {
                /* Test corruption at multiple positions */
                for (int pos = 0; pos < comp_size && pos < 8; pos++) {
                    uint8_t orig = g_compressed[pos];
                    g_compressed[pos] ^= 0xff;
                    int r = lz77_decompress(g_compressed, comp_size,
                                            g_decompressed, MAX_DECOMP_SIZE);
                    g_compressed[pos] = orig;
                    /* 0 = error is valid, positive = partial decode is valid */
                    if (r < 0 || r > MAX_DECOMP_SIZE)
                        __builtin_trap();
                }
            }
        }
        break;

    case 5:
        /* Invalid backward reference (distance 0) - must return 0 */
        {
            uint8_t bad_data[] = {0x20, 0x00}; /* Match with distance 0 */
            int r = lz77_decompress(bad_data, sizeof(bad_data), g_decompressed,
                                    MAX_DECOMP_SIZE);
            /* Should return 0 (error) or small positive */
            if (r < 0 || r > MAX_DECOMP_SIZE)
                __builtin_trap();
        }
        break;

    case 6:
        /* Invalid backward reference (too large) - must return 0 */
        {
            uint8_t bad_data[] = {0xff, 0xff, 0xff};
            int r = lz77_decompress(bad_data, sizeof(bad_data), g_decompressed,
                                    MAX_DECOMP_SIZE);
            /* Should return 0 (error) or small positive */
            if (r < 0 || r > MAX_DECOMP_SIZE)
                __builtin_trap();
        }
        break;

    case 7:
        /* Extended length edge cases */
        {
            /* len=7 with various ext_len values */
            uint8_t ext_tests[][4] = {
                {0xe0, 0x00, 0x01, 0x00}, /* ext_len=0, dist=1 */
                {0xe0, 0xff, 0x01, 0x00}, /* ext_len=255, dist=1 */
                {0xe0, 0x00, 0xff, 0x00}, /* ext_len=0, dist=255 */
            };
            for (size_t i = 0; i < sizeof(ext_tests) / sizeof(ext_tests[0]);
                 i++) {
                int r = lz77_decompress(ext_tests[i], 4, g_decompressed,
                                        MAX_DECOMP_SIZE);
                /* 0 = error is valid */
                if (r < 0 || r > MAX_DECOMP_SIZE)
                    __builtin_trap();
            }
        }
        break;

    case 8:
        /* Trailing garbage after valid stream */
        if (payload_size >= MIN_INPUT_SIZE && payload_size <= 256) {
            int comp_size = lz77_compress(payload, (int) payload_size,
                                          g_compressed, g_workmem);
            if (comp_size > 0 && comp_size < MAX_OUT_SIZE - 64) {
                /* Append garbage */
                memset(g_compressed + comp_size, 0xff, 64);
                int r = lz77_decompress(g_compressed, comp_size + 64,
                                        g_decompressed, MAX_DECOMP_SIZE);
                /* Should handle gracefully, 0 = error is valid */
                if (r < 0 || r > MAX_DECOMP_SIZE)
                    __builtin_trap();
            }
        }
        break;

    case 9:
        /* Output buffer one byte too small - should return 0 */
        if (payload_size >= MIN_INPUT_SIZE && payload_size <= 512) {
            int comp_size = lz77_compress(payload, (int) payload_size,
                                          g_compressed, g_workmem);
            if (comp_size > 0) {
                int r = lz77_decompress(g_compressed, comp_size, g_decompressed,
                                        (int) payload_size - 1);
                /* Should fail gracefully (return 0), never negative */
                if (r < 0 || r > (int) payload_size - 1)
                    __builtin_trap();
            }
        }
        break;
    }
}

/* Test 5: Overlapping copy stress (distance < length) */
static void fuzz_overlapping_copy(const uint8_t *data, size_t size)
{
    if (size < 4)
        return;

    /* Create patterns that trigger overlapping copies */
    uint8_t pattern = data[0];
    size_t repeat = 128 + (data[1] % 384);

    /* Single byte repeated - creates distance=1 matches */
    memset(g_scratch, pattern, repeat);

    int comp_size =
        lz77_compress(g_scratch, (int) repeat, g_compressed, g_workmem);
    if (comp_size <= 0)
        __builtin_trap();

    int decomp_size = lz77_decompress(g_compressed, comp_size, g_decompressed,
                                      MAX_DECOMP_SIZE);
    if (decomp_size != (int) repeat)
        __builtin_trap();

    /* Verify all bytes match */
    for (size_t i = 0; i < repeat; i++) {
        if (g_decompressed[i] != pattern)
            __builtin_trap();
    }

    /* Two-byte pattern - creates distance=2 matches */
    if (size >= 4 && repeat >= 4) {
        for (size_t i = 0; i < repeat; i++)
            g_scratch[i] = (i & 1) ? data[2] : data[3];

        comp_size =
            lz77_compress(g_scratch, (int) repeat, g_compressed, g_workmem);
        if (comp_size <= 0)
            __builtin_trap();

        decomp_size = lz77_decompress(g_compressed, comp_size, g_decompressed,
                                      MAX_DECOMP_SIZE);
        if (decomp_size != (int) repeat)
            __builtin_trap();

        if (memcmp(g_scratch, g_decompressed, repeat) != 0)
            __builtin_trap();
    }
}

/* Test 6: Random output buffer sizes */
static void fuzz_random_buffer_size(const uint8_t *data, size_t size)
{
    if (size < 4 || size > 4096)
        return;

    /* Use input bytes to determine buffer size */
    uint32_t rand_val = fuzz_rand(data, size, 0x12345678);
    int out_size = 1 + (rand_val % (size * 4));
    if (out_size > MAX_DECOMP_SIZE)
        out_size = MAX_DECOMP_SIZE;

    /* Direct decompress with random buffer size */
    int r = lz77_decompress(data, (int) size, g_decompressed, out_size);
    if (r < 0 || r > out_size)
        __builtin_trap();

    /* Round-trip with constrained buffer */
    if (size >= MIN_INPUT_SIZE) {
        int comp_size =
            lz77_compress(data, (int) size, g_compressed, g_workmem);
        if (comp_size > 0) {
            /* Try with exact buffer */
            r = lz77_decompress(g_compressed, comp_size, g_decompressed,
                                (int) size);
            if (r != (int) size)
                __builtin_trap();

            /* Try with slightly larger buffer */
            r = lz77_decompress(g_compressed, comp_size, g_decompressed,
                                (int) size + 1);
            if (r != (int) size)
                __builtin_trap();
        }
    }
}

/* Test 7: Structure-aware format tests */
static void fuzz_format_structure(const uint8_t *data, size_t size)
{
    if (size < 3)
        return;

    /* Build semi-valid compressed streams based on fuzz input */
    uint8_t ctrl = data[0];
    size_t out_pos = 0;

    if (ctrl < 0x20) {
        /* Literal: length = ctrl + 1 */
        uint8_t lit_len = (ctrl & 0x1f) + 1;
        g_scratch[out_pos++] = ctrl;
        for (uint8_t i = 0; i < lit_len && out_pos < 256; i++)
            g_scratch[out_pos++] = (size > 1 + i) ? data[1 + i] : 0;
    } else {
        /* Match: try to build valid-ish match token */
        g_scratch[out_pos++] = ctrl;
        if ((ctrl >> 5) == 7 && size > 1) {
            /* Extended length */
            g_scratch[out_pos++] = data[1];
        }
        if (size > 2)
            g_scratch[out_pos++] = data[2]; /* dist_low */
    }

    if (out_pos > 0) {
        int r = lz77_decompress(g_scratch, (int) out_pos, g_decompressed,
                                MAX_DECOMP_SIZE);
        if (r < 0)
            __builtin_trap();
    }
}

/* Test 8: Mutated compressed stream */
static void fuzz_mutate_compressed(const uint8_t *data, size_t size)
{
    if (size < MIN_INPUT_SIZE || size > 2048)
        return;

    int comp_size = lz77_compress(data, (int) size, g_compressed, g_workmem);
    if (comp_size <= 0)
        return;

    /* Mutate random positions in compressed data */
    uint32_t mutations = 1 + (data[0] % 4);
    for (uint32_t m = 0; m < mutations && m < (uint32_t) comp_size; m++) {
        int pos = data[(m + 1) % size] % comp_size;
        uint8_t orig = g_compressed[pos];
        g_compressed[pos] ^= data[(m + 2) % size];

        int r = lz77_decompress(g_compressed, comp_size, g_decompressed,
                                MAX_DECOMP_SIZE);
        /* Must handle gracefully - 0 (error) or positive (partial) both valid
         */
        if (r < 0 || r > MAX_DECOMP_SIZE)
            __builtin_trap();

        g_compressed[pos] = orig;
    }
}

/* Test 9: Truncated token edge cases
 *
 * Targets specific early-return paths in lz77_decompress:
 * - len==6 && ip > ip_bound (extended length read)
 * - Incomplete match tokens
 * - Incomplete literal tokens
 */
static void fuzz_truncated_tokens(const uint8_t *data, size_t size)
{
    if (size < 1)
        return;

    /* Truncated extended length match: \xe0 alone (len=7, needs ext_len byte)
     */
    {
        uint8_t trunc1[] = {0xe0};
        int r = lz77_decompress(trunc1, sizeof(trunc1), g_decompressed,
                                MAX_DECOMP_SIZE);
        if (r < 0 || r > MAX_DECOMP_SIZE)
            __builtin_trap();
    }

    /* Truncated extended length match: \xe0\xff (len=7 + ext, needs dist byte)
     */
    {
        uint8_t trunc2[] = {0xe0, 0xff};
        int r = lz77_decompress(trunc2, sizeof(trunc2), g_decompressed,
                                MAX_DECOMP_SIZE);
        if (r < 0 || r > MAX_DECOMP_SIZE)
            __builtin_trap();
    }

    /* Truncated literal: \x1f needs 32 bytes but none provided */
    {
        uint8_t trunc3[] = {0x1f};
        int r = lz77_decompress(trunc3, sizeof(trunc3), g_decompressed,
                                MAX_DECOMP_SIZE);
        if (r < 0 || r > MAX_DECOMP_SIZE)
            __builtin_trap();
    }

    /* Truncated literal with partial payload */
    {
        uint8_t trunc4[] = {0x0f, 'A', 'B', 'C'}; /* Needs 16 bytes, has 3 */
        int r = lz77_decompress(trunc4, sizeof(trunc4), g_decompressed,
                                MAX_DECOMP_SIZE);
        if (r < 0 || r > MAX_DECOMP_SIZE)
            __builtin_trap();
    }

    /* Short match missing distance byte */
    {
        uint8_t trunc5[] = {0x40}; /* len=2 match, needs dist_low byte */
        int r = lz77_decompress(trunc5, sizeof(trunc5), g_decompressed,
                                MAX_DECOMP_SIZE);
        if (r < 0 || r > MAX_DECOMP_SIZE)
            __builtin_trap();
    }

    /* Fuzz-driven truncation tests */
    if (size >= 2) {
        uint8_t variant = data[0] % 8;
        size_t trunc_len = 1 + (data[1] % 3);

        uint8_t test_patterns[][4] = {
            {0xe0, 0x00, 0x01, 0x00}, /* Full extended match */
            {0x40, 0x01, 0x00, 0x00}, /* Short match with dist */
            {0x1f, 'A', 'B', 'C'},    /* Max literal length */
            {0xc0, 0x01, 0x00, 0x00}, /* Medium match */
            {0xff, 0xff, 0x1f, 0x00}, /* Max everything */
            {0x20, 0x01, 0x00, 0x00}, /* Minimal match */
            {0xe0, 0xff, 0xff, 0x1f}, /* Extended + max dist */
            {0x00, 'X', 0x00, 0x00},  /* Single literal */
        };

        if (variant < 8 && trunc_len < 4) {
            int r = lz77_decompress(test_patterns[variant], (int) trunc_len,
                                    g_decompressed, MAX_DECOMP_SIZE);
            if (r < 0 || r > MAX_DECOMP_SIZE)
                __builtin_trap();
        }
    }
}

/* Test 10: Output buffer overflow tests
 *
 * Tests scenarios where decoded output would exceed max_out.
 */
static void fuzz_output_overflow(const uint8_t *data, size_t size)
{
    if (size < 2)
        return;

    /* Create compressed data that decodes to more than max_out */
    uint8_t variant = data[0] % 5;

    switch (variant) {
    case 0:
        /* Literal that would overflow small buffer */
        {
            uint8_t lit_stream[] = {
                0x0f, 'A', 'B', 'C', 'D', 'E', 'F', 'G',
                'H',  'I', 'J', 'K', 'L', 'M', 'N', 'O',
            };
            int r =
                lz77_decompress(lit_stream, sizeof(lit_stream), g_decompressed,
                                8); /* Only 8 bytes output */
            /* Should return 0 (buffer too small) */
            if (r < 0 || r > 8)
                __builtin_trap();
        }
        break;

    case 1:
        /* Match that would overflow */
        {
            /* First emit some data, then match that overflows */
            uint8_t overflow_match[] = {
                0x03, 'A',  'B', 'C', 'D', /* 4 byte literal */
                0xe0, 0xf0, 0x01 /* long match (7+240+3=250 bytes) at dist 1 */
            };
            int r = lz77_decompress(overflow_match, sizeof(overflow_match),
                                    g_decompressed, 16); /* Way too small */
            if (r < 0 || r > 16)
                __builtin_trap();
        }
        break;

    case 2:
        /* Exact fit test - compress data, decompress with exact buffer */
        if (size >= MIN_INPUT_SIZE && size <= 256) {
            int comp_size =
                lz77_compress(data, (int) size, g_compressed, g_workmem);
            if (comp_size > 0) {
                /* Exact fit should work */
                int r = lz77_decompress(g_compressed, comp_size, g_decompressed,
                                        (int) size);
                if (r != (int) size)
                    __builtin_trap();

                /* One byte less should fail */
                r = lz77_decompress(g_compressed, comp_size, g_decompressed,
                                    (int) size - 1);
                if (r < 0 || r > (int) size - 1)
                    __builtin_trap();
            }
        }
        break;

    case 3:
        /* Multi-chunk match overflow */
        {
            /* Create RLE-style compressed data with huge decoded size */
            uint8_t rle_overflow[] = {
                0x00, 'A',        /* 1 byte literal */
                0xe0, 0xff, 0x01, /* Match: 7+255+3=265 at dist 1 */
                0xe0, 0xff, 0x01, /* Another huge match */
                0xe0, 0xff, 0x01  /* And another */
            };
            int r = lz77_decompress(rle_overflow, sizeof(rle_overflow),
                                    g_decompressed, 100); /* Much too small */
            if (r < 0 || r > 100)
                __builtin_trap();
        }
        break;

    case 4:
        /* Zero output buffer */
        {
            uint8_t any_data[] = {0x00, 'A'};
            int r =
                lz77_decompress(any_data, sizeof(any_data), g_decompressed, 0);
            if (r != 0)
                __builtin_trap();
        }
        break;
    }
}

/* Test 11: Distance edge handling
 *
 * Tests distance values at boundaries:
 * - dist = 0 (invalid, should return 0)
 * - dist = produced output size (edge case)
 * - dist > MAX_DISTANCE (should return 0)
 * - dist > produced (invalid backreference)
 */
static void fuzz_distance_edges(const uint8_t *data, size_t size)
{
    if (size < 1)
        return;

    /* Distance = 0 (invalid) */
    {
        uint8_t dist_zero[] = {
            0x03, 'A',  'B', 'C', 'D', /* 4 bytes output */
            0x20, 0x00,                /* Match len=1+3=4, dist=0+1=1... wait */
        };
        /* Actually format: ctrl=(len<<5)|dist_high, then dist_low */
        /* dist = (ctrl & 0x1f) << 8 | dist_low, then ref -= dist_low */
        /* To get distance 0: need final ref = op which means all zeros */
        uint8_t bad_dist[] = {0x20,
                              0x00}; /* Try to make dist=1 at empty output */
        int r = lz77_decompress(bad_dist, sizeof(bad_dist), g_decompressed,
                                MAX_DECOMP_SIZE);
        /* Should fail gracefully */
        if (r < 0 || r > MAX_DECOMP_SIZE)
            __builtin_trap();
    }

    /* Distance = MAX_DISTANCE (8191 = 0x1fff) */
    {
        /* Need output first, then reference at max distance */
        /* dist = (ctrl & 0x1f) << 8 | dist_low = 0x1f << 8 | 0xff = 0x1fff =
         * 8191 */
        uint8_t max_dist[] = {
            0x1f, 'A',  'A', 'A', 'A', 'A', 'A', 'A', 'A', /* 8 bytes */
            'A',  'A',  'A', 'A', 'A', 'A', 'A', 'A',      /* 16 bytes */
            'A',  'A',  'A', 'A', 'A', 'A', 'A', 'A',      /* 24 bytes */
            'A',  'A',  'A', 'A', 'A', 'A', 'A', 'A',      /* 32 bytes total */
            0x3f, 0xff,
            /* Match with dist_high=0x1f, dist_low=0xff → dist=8192 */
        };
        int r = lz77_decompress(max_dist, sizeof(max_dist), g_decompressed,
                                MAX_DECOMP_SIZE);
        /* With only 32 bytes produced, dist=8192 is invalid → should return 0
         */
        if (r < 0 || r > MAX_DECOMP_SIZE)
            __builtin_trap();
    }

    /* Distance > produced output (backreference before start) */
    {
        uint8_t over_dist[] = {
            0x00,
            'A', /* 1 byte output */
            0x40,
            0x10, /* Match at dist=(0<<8|0x10)+1=17, but only 1 byte exists */
        };
        int r = lz77_decompress(over_dist, sizeof(over_dist), g_decompressed,
                                MAX_DECOMP_SIZE);
        if (r < 0 || r > MAX_DECOMP_SIZE)
            __builtin_trap();
    }

    /* Distance exactly = produced (reference to start) */
    if (size >= 8) {
        /* Build valid data first, then match referencing exact start */
        memset(g_scratch, 'A', 256);
        int comp_size = lz77_compress(g_scratch, 256, g_compressed, g_workmem);
        if (comp_size > 0) {
            int r = lz77_decompress(g_compressed, comp_size, g_decompressed,
                                    MAX_DECOMP_SIZE);
            if (r != 256)
                __builtin_trap();
        }
    }

    /* Fuzz-driven distance testing */
    if (size >= 4) {
        uint8_t dist_high = data[0] & 0x1f;
        uint8_t dist_low = data[1];
        uint8_t len_field = (data[2] % 7) + 1;
        uint8_t ctrl = (len_field << 5) | dist_high;

        uint8_t test_stream[8];
        size_t pos = 0;

        /* Add some valid output first */
        test_stream[pos++] = 0x01; /* 2 byte literal */
        test_stream[pos++] = data[3];
        test_stream[pos++] = data[3];

        /* Add match with fuzzed distance */
        test_stream[pos++] = ctrl;
        if (len_field == 7 && size > 4)
            test_stream[pos++] = data[4] % 32; /* ext_len */
        test_stream[pos++] = dist_low;

        int r = lz77_decompress(test_stream, (int) pos, g_decompressed,
                                MAX_DECOMP_SIZE);
        if (r < 0 || r > MAX_DECOMP_SIZE)
            __builtin_trap();
    }
}

/* Test 12: Multi-chunk match tests
 *
 * Tests matches longer than MAX_LEN (264) that require multiple chunks.
 */
static void fuzz_multi_chunk_match(const uint8_t *data, size_t size)
{
    if (size < 6)
        return;

    /* Create highly repetitive data that produces multi-chunk matches */
    size_t pattern_len = 1 + (data[0] % 4); /* 1-4 byte pattern */
    size_t total_len =
        MAX_LEN * 3 + (data[1] % 256); /* Multiple MAX_LEN chunks */

    if (total_len > MAX_DECOMP_SIZE)
        total_len = MAX_DECOMP_SIZE;

    /* Ensure we have enough data for the pattern */
    if (pattern_len + 2 > size)
        pattern_len = size - 2;
    if (pattern_len == 0)
        pattern_len = 1;

    /* Fill scratch with repeating pattern */
    for (size_t i = 0; i < total_len; i++) {
        g_scratch[i] = data[(i % pattern_len) + 2];
    }

    int comp_size =
        lz77_compress(g_scratch, (int) total_len, g_compressed, g_workmem);
    if (comp_size <= 0)
        __builtin_trap();

    int decomp_size = lz77_decompress(g_compressed, comp_size, g_decompressed,
                                      MAX_DECOMP_SIZE);
    if (decomp_size != (int) total_len)
        __builtin_trap();

    if (memcmp(g_scratch, g_decompressed, total_len) != 0)
        __builtin_trap();

    /* Test with constrained output buffer */
    int half_len = (int) total_len / 2;
    int r = lz77_decompress(g_compressed, comp_size, g_decompressed, half_len);
    /* Should fail (return 0) because buffer too small */
    if (r < 0 || r > half_len)
        __builtin_trap();

    /* Mutate multi-chunk stream and verify graceful handling */
    if (comp_size > 4) {
        for (int i = 0; i < 4 && i < comp_size; i++) {
            uint8_t orig = g_compressed[i];
            g_compressed[i] = data[i % size];
            r = lz77_decompress(g_compressed, comp_size, g_decompressed,
                                MAX_DECOMP_SIZE);
            if (r < 0 || r > MAX_DECOMP_SIZE)
                __builtin_trap();
            g_compressed[i] = orig;
        }
    }
}

/* Test 13: API edge cases and blind spots
 *
 * Covers:
 * - length=0, max_out=0 API contract
 * - Match-as-first-token (must return 0 or misinterpret as literal)
 * - Overlapping copies for distances 3-8
 * - MAX_DISTANCE boundary (8192 vs 8191)
 */
static void fuzz_api_edge_cases(const uint8_t *data, size_t size)
{
    if (size < 1)
        return;

    uint8_t variant = data[0] % 10;

    switch (variant) {
    case 0:
        /* Test lz77_decompress with length=0 - must return 0 */
        {
            int r =
                lz77_decompress(g_scratch, 0, g_decompressed, MAX_DECOMP_SIZE);
            if (r != 0)
                __builtin_trap();
        }
        break;

    case 1:
        /* Test lz77_compress with length=0 - must return 0 */
        {
            int r = lz77_compress(g_scratch, 0, g_compressed, g_workmem);
            if (r != 0)
                __builtin_trap();
        }
        break;

    case 2:
        /* Match-as-first-token - first byte >= 0x20 is misinterpreted as
         * literal */
        {
            /* The first control byte is masked with & 31, so match tokens at
             * start are interpreted as literals. This is expected behavior, not
             * an error. */
            uint8_t match_first[] = {0x40,
                                     0x01}; /* Would be match if not first */
            int r = lz77_decompress(match_first, sizeof(match_first),
                                    g_decompressed, MAX_DECOMP_SIZE);
            /* Should NOT crash - either return 0 (error) or interpret as
             * literal */
            if (r < 0 || r > MAX_DECOMP_SIZE)
                __builtin_trap();
        }
        break;

    case 3:
        /* Overlapping copy distance=3 */
        if (size >= 3) {
            size_t repeat = 256;
            for (size_t i = 0; i < repeat; i++)
                g_scratch[i] = data[(i % 3) % size];
            int comp_size =
                lz77_compress(g_scratch, (int) repeat, g_compressed, g_workmem);
            if (comp_size <= 0)
                __builtin_trap();
            int decomp_size = lz77_decompress(g_compressed, comp_size,
                                              g_decompressed, MAX_DECOMP_SIZE);
            if (decomp_size != (int) repeat)
                __builtin_trap();
            if (memcmp(g_scratch, g_decompressed, repeat) != 0)
                __builtin_trap();
        }
        break;

    case 4:
        /* Overlapping copy distance=4 */
        if (size >= 4) {
            size_t repeat = 256;
            for (size_t i = 0; i < repeat; i++)
                g_scratch[i] = data[(i % 4) % size];
            int comp_size =
                lz77_compress(g_scratch, (int) repeat, g_compressed, g_workmem);
            if (comp_size <= 0)
                __builtin_trap();
            int decomp_size = lz77_decompress(g_compressed, comp_size,
                                              g_decompressed, MAX_DECOMP_SIZE);
            if (decomp_size != (int) repeat)
                __builtin_trap();
            if (memcmp(g_scratch, g_decompressed, repeat) != 0)
                __builtin_trap();
        }
        break;

    case 5:
        /* Overlapping copy distances 5-8 */
        if (size >= 8) {
            for (int dist = 5; dist <= 8; dist++) {
                size_t repeat = 256;
                for (size_t i = 0; i < repeat; i++)
                    g_scratch[i] = data[(i % dist) % size];
                int comp_size = lz77_compress(g_scratch, (int) repeat,
                                              g_compressed, g_workmem);
                if (comp_size <= 0)
                    __builtin_trap();
                int decomp_size = lz77_decompress(
                    g_compressed, comp_size, g_decompressed, MAX_DECOMP_SIZE);
                if (decomp_size != (int) repeat)
                    __builtin_trap();
                if (memcmp(g_scratch, g_decompressed, repeat) != 0)
                    __builtin_trap();
            }
        }
        break;

    case 6:
        /* MAX_DISTANCE boundary - create data to trigger distance 8191/8192 */
        {
            /* Compressor rejects distance >= MAX_DISTANCE (8192), so distance
             * 8192 should degrade to literals. Test that distance 8191 works.
             */
            size_t test_size = MAX_DISTANCE + 256;
            /* Create pattern: first 8 bytes, then garbage, then same 8 bytes */
            memset(g_scratch, 'X', test_size);
            for (int i = 0; i < 8; i++) {
                g_scratch[i] = 'A' + i;
                g_scratch[MAX_DISTANCE - 1 + i] = 'A' + i; /* distance = 8191 */
            }
            int comp_size = lz77_compress(g_scratch, (int) test_size,
                                          g_compressed, g_workmem);
            if (comp_size <= 0)
                __builtin_trap();
            int decomp_size = lz77_decompress(g_compressed, comp_size,
                                              g_decompressed, MAX_DECOMP_SIZE);
            if (decomp_size != (int) test_size)
                __builtin_trap();
            if (memcmp(g_scratch, g_decompressed, test_size) != 0)
                __builtin_trap();
        }
        break;

    case 7:
        /* Literal + match sequence with max distance token */
        {
            /* Build: 32-byte literal + match at max valid distance (8191) */
            /* This requires 32 bytes produced first, then match referencing far
             * back */
            uint8_t max_dist_seq[64];
            size_t pos = 0;
            /* Literal: 32 bytes */
            max_dist_seq[pos++] = 0x1f; /* 32 byte literal */
            for (int i = 0; i < 32; i++)
                max_dist_seq[pos++] = 'A';
            /* Match: dist=32 (within range), len=4 */
            max_dist_seq[pos++] = 0x40; /* len=2+3=5, dist_high=0 */
            max_dist_seq[pos++] = 0x1f; /* dist_low=31, total dist=32 */

            int r = lz77_decompress(max_dist_seq, (int) pos, g_decompressed,
                                    MAX_DECOMP_SIZE);
            if (r < 0 || r > MAX_DECOMP_SIZE)
                __builtin_trap();
        }
        break;

    case 8:
        /* Test negative/invalid length scenarios */
        {
            /* The API uses 'int' for length, negative values should return 0 */
            int r =
                lz77_decompress(g_scratch, -1, g_decompressed, MAX_DECOMP_SIZE);
            if (r != 0)
                __builtin_trap();
            r = lz77_compress(g_scratch, -1, g_compressed, g_workmem);
            if (r != 0)
                __builtin_trap();
        }
        break;

    case 9:
        /* Test negative max_out */
        {
            uint8_t simple[] = {0x00, 'A'};
            int r = lz77_decompress(simple, sizeof(simple), g_decompressed, -1);
            /* Negative max_out should be treated as 0 or cause safe error */
            if (r < 0)
                __builtin_trap();
        }
        break;
    }
}

/* libFuzzer entry point */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0)
        return 0;

    /* Run all test strategies */
    fuzz_decompress(data, size);
    fuzz_roundtrip(data, size);
    fuzz_compress_boundaries(data, size);
    fuzz_decompress_boundaries(data, size);
    fuzz_overlapping_copy(data, size);
    fuzz_random_buffer_size(data, size);
    fuzz_format_structure(data, size);
    fuzz_mutate_compressed(data, size);
    fuzz_truncated_tokens(data, size);
    fuzz_output_overflow(data, size);
    fuzz_distance_edges(data, size);
    fuzz_multi_chunk_match(data, size);
    fuzz_api_edge_cases(data, size);

    return 0;
}
