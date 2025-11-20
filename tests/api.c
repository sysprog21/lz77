#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lz77.h"

#define TEST_PASSED "\033[32mPASS\033[0m"
#define TEST_FAILED "\033[31mFAIL\033[0m"

#define LZ77_TEST_CASE(name, fn) \
    static int fn(void);         \
    static struct test_case s_test_##name = {#name, fn};

#define ASSERT_SUCCESS(expr)                                           \
    do {                                                               \
        if ((expr) < 0) {                                              \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, \
                    __LINE__, #expr);                                  \
            return -1;                                                 \
        }                                                              \
    } while (0)

#define ASSERT_INT_EQUALS(expected, actual)                           \
    do {                                                              \
        if ((expected) != (actual)) {                                 \
            fprintf(stderr, "%s:%d: expected %d, got %d\n", __FILE__, \
                    __LINE__, (int) (expected), (int) (actual));      \
            return -1;                                                \
        }                                                             \
    } while (0)

#define ASSERT_TRUE(expr)                                              \
    do {                                                               \
        if (!(expr)) {                                                 \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, \
                    __LINE__, #expr);                                  \
            return -1;                                                 \
        }                                                              \
    } while (0)

#define ASSERT_BIN_ARRAYS_EQUALS(expected, expected_len, actual, actual_len) \
    do {                                                                     \
        if ((expected_len) != (actual_len) ||                                \
            memcmp((expected), (actual), (expected_len)) != 0) {             \
            fprintf(stderr, "%s:%d: binary arrays differ\n", __FILE__,       \
                    __LINE__);                                               \
            return -1;                                                       \
        }                                                                    \
    } while (0)

struct test_case {
    const char *name;
    int (*fn)(void);
};

static int tests_passed = 0;
static int tests_failed = 0;

/* Basic compression/decompression tests */
LZ77_TEST_CASE(compress_decompress_empty, test_compress_decompress_empty)
static int test_compress_decompress_empty(void)
{
    uint8_t input[] = "";
    uint8_t compressed[100];
    uint8_t decompressed[100];
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    int compressed_size = lz77_compress(input, 0, compressed, workmem);
    int decompressed_size =
        lz77_decompress(compressed, compressed_size, decompressed, 100);

    ASSERT_INT_EQUALS(0, decompressed_size);
    return 0;
}

LZ77_TEST_CASE(compress_decompress_single_char,
               test_compress_decompress_single_char)
static int test_compress_decompress_single_char(void)
{
    uint8_t input[] = "A";
    uint8_t compressed[100];
    uint8_t decompressed[100];
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    int compressed_size = lz77_compress(input, 1, compressed, workmem);
    int decompressed_size =
        lz77_decompress(compressed, compressed_size, decompressed, 100);

    ASSERT_INT_EQUALS(1, decompressed_size);
    ASSERT_INT_EQUALS('A', decompressed[0]);
    return 0;
}

LZ77_TEST_CASE(compress_decompress_repeated_chars,
               test_compress_decompress_repeated_chars)
static int test_compress_decompress_repeated_chars(void)
{
    uint8_t input[] = "AAAAAAAAAAAAAAAAAAAA";
    uint8_t compressed[100];
    uint8_t decompressed[100];
    int input_len = strlen((char *) input);
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    int compressed_size = lz77_compress(input, input_len, compressed, workmem);
    int decompressed_size =
        lz77_decompress(compressed, compressed_size, decompressed, 100);

    ASSERT_INT_EQUALS(input_len, decompressed_size);
    ASSERT_BIN_ARRAYS_EQUALS(input, input_len, decompressed, decompressed_size);
    return 0;
}

LZ77_TEST_CASE(compress_decompress_pattern, test_compress_decompress_pattern)
static int test_compress_decompress_pattern(void)
{
    uint8_t input[] = "ABCABCABCABCABC";
    uint8_t compressed[100];
    uint8_t decompressed[100];
    int input_len = strlen((char *) input);
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    int compressed_size = lz77_compress(input, input_len, compressed, workmem);
    int decompressed_size =
        lz77_decompress(compressed, compressed_size, decompressed, 100);

    ASSERT_INT_EQUALS(input_len, decompressed_size);
    ASSERT_BIN_ARRAYS_EQUALS(input, input_len, decompressed, decompressed_size);
    return 0;
}

LZ77_TEST_CASE(compress_decompress_text, test_compress_decompress_text)
static int test_compress_decompress_text(void)
{
    uint8_t input[] =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog.";
    uint8_t compressed[200];
    uint8_t decompressed[200];
    int input_len = strlen((char *) input);
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    int compressed_size = lz77_compress(input, input_len, compressed, workmem);
    int decompressed_size =
        lz77_decompress(compressed, compressed_size, decompressed, 200);

    ASSERT_INT_EQUALS(input_len, decompressed_size);
    ASSERT_BIN_ARRAYS_EQUALS(input, input_len, decompressed, decompressed_size);
    return 0;
}

LZ77_TEST_CASE(compress_decompress_binary, test_compress_decompress_binary)
static int test_compress_decompress_binary(void)
{
    uint8_t input[256];
    for (int i = 0; i < 256; i++)
        input[i] = (uint8_t) i;
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    uint8_t compressed[512];
    uint8_t decompressed[512];

    int compressed_size = lz77_compress(input, 256, compressed, workmem);
    int decompressed_size =
        lz77_decompress(compressed, compressed_size, decompressed, 512);

    ASSERT_INT_EQUALS(256, decompressed_size);
    ASSERT_BIN_ARRAYS_EQUALS(input, 256, decompressed, decompressed_size);
    return 0;
}

LZ77_TEST_CASE(compression_ratio, test_compression_ratio)
static int test_compression_ratio(void)
{
    uint8_t input[1000];
    memset(input, 'A', 1000);
    uint8_t compressed[2000];
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    int compressed_size = lz77_compress(input, 1000, compressed, workmem);

    /* Highly compressible data should compress to < 50% */
    ASSERT_TRUE(compressed_size < 500);
    return 0;
}

LZ77_TEST_CASE(large_input, test_large_input)
static int test_large_input(void)
{
    const int size = 10000;
    uint8_t *input = malloc(size);
    uint8_t *compressed = malloc(size * 2);
    uint8_t *decompressed = malloc(size);
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    ASSERT_TRUE(input != NULL && compressed != NULL && decompressed != NULL);

    /* Create pattern with some repetition */
    for (int i = 0; i < size; i++)
        input[i] = (uint8_t) (i % 100);

    int compressed_size = lz77_compress(input, size, compressed, workmem);
    int decompressed_size =
        lz77_decompress(compressed, compressed_size, decompressed, size);

    ASSERT_INT_EQUALS(size, decompressed_size);
    ASSERT_BIN_ARRAYS_EQUALS(input, size, decompressed, decompressed_size);

    free(input);
    free(compressed);
    free(decompressed);
    return 0;
}

/* Chunked compression tests - inspired by aws-c-compression partial I/O */
LZ77_TEST_CASE(compress_chunked, test_compress_chunked)
static int test_compress_chunked(void)
{
    /* Test that compression works correctly when limited by output buffer */
    const uint8_t input[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789";
    const int input_len = strlen((const char *) input);
    uint8_t compressed_full[256];
    uint8_t compressed_chunked[256];
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    /* Get full compression result for comparison */
    int full_size = lz77_compress(input, input_len, compressed_full, workmem);
    ASSERT_TRUE(full_size > 0);

    /* Note: Current lz77 implementation doesn't support incremental output */
    /* This test verifies consistency when compressing the same data */
    int chunked_size =
        lz77_compress(input, input_len, compressed_chunked, workmem);
    ASSERT_INT_EQUALS(full_size, chunked_size);
    ASSERT_BIN_ARRAYS_EQUALS(compressed_full, full_size, compressed_chunked,
                             chunked_size);

    return 0;
}

LZ77_TEST_CASE(decompress_output_validation, test_decompress_output_validation)
static int test_decompress_output_validation(void)
{
    /* Test decompression with various output buffer sizes */
    const uint8_t input[] = "AAABBBCCCDDD";
    const int input_len = strlen((const char *) input);
    uint8_t compressed[256];
    uint8_t decompressed[256];
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    int compressed_size = lz77_compress(input, input_len, compressed, workmem);
    ASSERT_TRUE(compressed_size > 0);

    /* Decompress with exact buffer size */
    int result =
        lz77_decompress(compressed, compressed_size, decompressed, input_len);
    ASSERT_INT_EQUALS(input_len, result);
    ASSERT_BIN_ARRAYS_EQUALS(input, input_len, decompressed, result);

    /* Decompress with larger buffer */
    result = lz77_decompress(compressed, compressed_size, decompressed,
                             input_len * 2);
    ASSERT_INT_EQUALS(input_len, result);

    /* Decompress with insufficient buffer should fail */
    result = lz77_decompress(compressed, compressed_size, decompressed, 5);
    ASSERT_INT_EQUALS(0, result); /* Should return 0 on failure */

    return 0;
}

LZ77_TEST_CASE(transitive_all_chars, test_transitive_all_chars)
static int test_transitive_all_chars(void)
{
    /* Test encode -> decode produces original (transitive property) */
    uint8_t input[256];
    for (int i = 0; i < 256; i++)
        input[i] = (uint8_t) i;
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    uint8_t compressed[1024];
    uint8_t decompressed[256];

    int compressed_size = lz77_compress(input, 256, compressed, workmem);
    ASSERT_TRUE(compressed_size > 0);

    int decompressed_size =
        lz77_decompress(compressed, compressed_size, decompressed, 256);
    ASSERT_INT_EQUALS(256, decompressed_size);
    ASSERT_BIN_ARRAYS_EQUALS(input, 256, decompressed, decompressed_size);

    return 0;
}

LZ77_TEST_CASE(transitive_repeated_pattern, test_transitive_repeated_pattern)
static int test_transitive_repeated_pattern(void)
{
    /* Test transitive property with highly compressible data */
    const char *pattern = "The quick brown fox ";
    const int pattern_len = strlen(pattern);
    const int repetitions = 50;
    const int input_len = pattern_len * repetitions;
    uint8_t workmem[LZ77_WORKMEM_SIZE];

    uint8_t *input = malloc(input_len);
    uint8_t *compressed = malloc(input_len * 2);
    uint8_t *decompressed = malloc(input_len);

    ASSERT_TRUE(input != NULL && compressed != NULL && decompressed != NULL);

    /* Build repeated pattern */
    for (int i = 0; i < repetitions; i++)
        memcpy(input + i * pattern_len, pattern, pattern_len);

    int compressed_size = lz77_compress(input, input_len, compressed, workmem);
    ASSERT_TRUE(compressed_size > 0);
    ASSERT_TRUE(compressed_size < input_len); /* Should compress well */

    int decompressed_size =
        lz77_decompress(compressed, compressed_size, decompressed, input_len);
    ASSERT_INT_EQUALS(input_len, decompressed_size);
    ASSERT_BIN_ARRAYS_EQUALS(input, input_len, decompressed, decompressed_size);

    free(input);
    free(compressed);
    free(decompressed);
    return 0;
}

/* Test registration table */
static struct test_case *s_tests[] = {
    &s_test_compress_decompress_empty,
    &s_test_compress_decompress_single_char,
    &s_test_compress_decompress_repeated_chars,
    &s_test_compress_decompress_pattern,
    &s_test_compress_decompress_text,
    &s_test_compress_decompress_binary,
    &s_test_compression_ratio,
    &s_test_large_input,
    &s_test_compress_chunked,
    &s_test_decompress_output_validation,
    &s_test_transitive_all_chars,
    &s_test_transitive_repeated_pattern,
};

static const size_t s_num_tests = sizeof(s_tests) / sizeof(s_tests[0]);

static void run_test(struct test_case *test)
{
    int result = test->fn();
    if (result == 0) {
        printf("%s %s\n", TEST_PASSED, test->name);
        tests_passed++;
    } else {
        printf("%s %s\n", TEST_FAILED, test->name);
        tests_failed++;
    }
}

int main(void)
{
    printf("Running LZ77 API Tests\n");
    printf("======================\n");

    for (size_t i = 0; i < s_num_tests; i++)
        run_test(s_tests[i]);

    printf("========================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
