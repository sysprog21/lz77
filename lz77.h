#ifndef LZ77_H
#define LZ77_H

/**
 * @file lz77.h
 * @brief Fast LZ77 compression library with minimal dependencies
 *
 * Features:
 * - Zero dependencies beyond standard library
 * - Caller-provided buffers (no hidden allocations)
 * - Fast decompression suitable for embedded systems
 * - Predictable memory usage (32KB workspace)
 * - Cache-friendly hash table design
 *
 * Usage Example:
 * @code
 *   #include <stdlib.h>
 *   #include "lz77.h"
 *
 *   uint8_t input[1024] = "Data to compress...";
 *   uint8_t compressed[2048];
 *   uint8_t decompressed[1024];
 *   uint8_t workmem[LZ77_WORKMEM_SIZE];
 *
 *   int compressed_size = lz77_compress(input, 1024, compressed, workmem);
 *   int decompressed_size = lz77_decompress(compressed, compressed_size,
 *                                           decompressed, 1024);
 * @endcode
 */

#include <stdint.h>
#include <string.h>

/* Compression algorithm constants */
#define MAX_COPY 32       /* Maximum literal run length */
#define MAX_LEN 264       /* Maximum match length (256 + 8) */
#define MAX_DISTANCE 8192 /* Maximum backward reference distance */
#define MIN_MATCH_LEN 3   /* Minimum match length for compression */
#define MIN_INPUT_SIZE 13 /* Minimum input size for full compression */

/* Hash table configuration */
#define HASH_LOG 13               /* Log2 of hash table size */
#define HASH_SIZE (1 << HASH_LOG) /* Hash table size: 8192 entries */
#define HASH_MASK (HASH_SIZE - 1) /* Hash table index mask */

/* Buffer size estimation constants */
#define COMPRESS_OVERHEAD 128 /* Safety margin for worst-case compression */

/* Workspace requirements */
#define LZ77_WORKMEM_SIZE (HASH_SIZE * sizeof(uint32_t)) /* 32KB workspace */

#if defined(__clang__) || defined(__GNUC__)
#define LZ77_LIKELY(x) __builtin_expect(!!(x), 1)
#define LZ77_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LZ77_LIKELY(x) (x)
#define LZ77_UNLIKELY(x) (x)
#endif

/**
 * Hash function for dictionary lookup.
 * Maps 24-bit sequences to hash table indices (0-8191).
 * Uses integer multiplication and bit shifting for fast, uniform distribution.
 */
static inline uint32_t lz77_hash(uint32_t v)
{
    v ^= v >> 15;
    v *= 0x27d4eb2d; /* Multiplicative hash constant */
    return v >> (32 - HASH_LOG);
}

/**
 * Safe unaligned 32-bit read using memcpy.
 * Avoids undefined behavior on architectures requiring aligned access.
 */
static inline uint32_t lz77_read32(const void *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

/**
 * Calculate match length between reference and current position.
 * Compares bytes until mismatch or end of input is reached.
 *
 * Return Number of matching bytes (excluding initial MIN_MATCH_LEN)
 */
static uint32_t match_len(const uint8_t *ref_ptr,
                          const uint8_t *ip_ptr,
                          const uint8_t *ip_end)
{
    const uint8_t *const start = ref_ptr;
    while (LZ77_LIKELY(ip_ptr < ip_end) && *ref_ptr == *ip_ptr)
        ++ref_ptr, ++ip_ptr;
    return (uint32_t) (ref_ptr - start);
}

/**
 * Encode a match (backward reference) to output stream.
 * Format: [control byte][optional length][distance low][distance high]
 *
 * Handles long matches by splitting into multiple chunks if needed.
 */
static uint8_t *match(uint32_t len, uint32_t distance, uint8_t *op)
{
    --distance; /* Distance is stored as (actual_distance - 1) */

    /* Split very long matches into MAX_LEN chunks */
    for (; len > MAX_LEN - 2; len -= MAX_LEN - 2) {
        *op++ = (7 << 5) + (distance >> 8);
        *op++ = MAX_LEN - 2 - 7 - 2;
        *op++ = distance & 255;
    }

    /* Encode final match chunk */
    *op++ = ((len < 7) ? len : 7) << 5 | (distance >> 8);
    if (len >= 7)
        *op++ = len - 7; /* Extended length byte */
    *op++ = distance & 255;
    return op;
}

/**
 * Encode literal run to output stream.
 * Format: [length-1][literal bytes...]
 *
 * Long runs are split into MAX_COPY chunks (32 bytes each).
 */
static uint8_t *literals(uint32_t runs, const uint8_t *src, uint8_t *dest)
{
    /* Split long literal runs into MAX_COPY chunks */
    while (runs >= MAX_COPY) {
        *dest++ = MAX_COPY - 1;
        memcpy(dest, src, MAX_COPY);
        src += MAX_COPY, dest += MAX_COPY, runs -= MAX_COPY;
    }

    /* Encode remaining literals */
    if (runs) {
        *dest++ = runs - 1;
        memcpy(dest, src, runs);
        dest += runs;
    }
    return dest;
}

/**
 * Compresses a block of data using the LZ77 algorithm with lazy matching.
 *
 * This function implements LZ77 compression with the following optimizations:
 * - Hash-based dictionary lookup (8192-entry table)
 * - Lazy matching for improved compression ratios
 * - Cache-friendly memory access patterns
 *
 * @param in      Pointer to the input data buffer
 * @param length  Length of input data in bytes (can be 0)
 * @param out     Pointer to output buffer for compressed data
 *                (must be large enough: input_size + COMPRESS_OVERHEAD
 * recommended)
 * @param workmem Workspace buffer (must be at least LZ77_WORKMEM_SIZE bytes)
 *                This buffer is used for the hash table and can be reused
 *                between compression calls.
 *
 * @return Size of compressed data in bytes, or 0 if input length <= 0
 *
 * @note The output buffer should be at least as large as the input to handle
 *       worst-case scenarios where data expands rather than compresses.
 *
 * @note This function does not allocate any memory internally. All buffers
 *       must be provided by the caller.
 *
 * Usage Example:
 * @code
 *   uint8_t input[1024] = {...};
 *   uint8_t output[1024 + COMPRESS_OVERHEAD];
 *   uint8_t workspace[LZ77_WORKMEM_SIZE];
 *
 *   int compressed_size = lz77_compress(input, 1024, output, workspace);
 *   if (compressed_size > 0) {
 *       // compressed_size bytes in output buffer contain compressed data
 *   }
 * @endcode
 */
int lz77_compress(const void *in, int length, void *out, void *workmem)
{
    const uint8_t *ip = (const uint8_t *) in, *ip_start = ip;
    const uint8_t *in_end = ip + length;
    uint8_t *op = (uint8_t *) out;

    /* Handle small inputs that don't meet MIN_INPUT_SIZE */
    if (length <= 0)
        return 0;
    if (length < MIN_INPUT_SIZE)
        return literals((uint32_t) length, ip, op) - (uint8_t *) out;

    const uint8_t *ip_limit = ip + length - MIN_INPUT_SIZE;

    uint32_t *htab = (uint32_t *) workmem;
    uint32_t seq, hash;
    memset(htab, 0, LZ77_WORKMEM_SIZE);

    /* we start with literal copy */
    const uint8_t *anchor = ip;
    ip += 2;

    /* main loop */
    while (LZ77_LIKELY(ip < ip_limit)) {
        const uint8_t *ref;
        uint32_t distance, cmp;

        /* find potential match */
        do {
            seq = lz77_read32(ip) & 0xffffff;
            hash = lz77_hash(seq);
            ref = ip_start + htab[hash];
            distance = ip - ref;
            htab[hash] = ip - ip_start;
            cmp = (distance < MAX_DISTANCE) ? lz77_read32(ref) & 0xffffff
                                            : 0x1000000;

            if (LZ77_UNLIKELY(ip >= ip_limit))
                break;

            ++ip;
        } while (seq != cmp);

        if (LZ77_UNLIKELY(ip >= ip_limit))
            break;

        --ip;

        if (ip > anchor)
            op = literals(ip - anchor, anchor, op);

        uint32_t len =
            match_len(ref + MIN_MATCH_LEN, ip + MIN_MATCH_LEN, in_end) + 1;

        /* Two-step lazy matching: check positions ip+1 and ip+2 for better
         * matches.
         *
         * This explores one additional lookahead position. A match at ip+2 must
         * be better than the current best by at least the cost of emitting 2
         * literals plus encoding overhead.
         *
         * Cost model:
         * - Short match (len<7): 2 bytes encoding
         * - Long match (len>=7): 3 bytes encoding
         * - Literal overhead: 1 byte per 32 bytes (MAX_COPY)
         */

        /* Step 1: Check if ip+1 has better match (one-step lazy) */
        uint32_t lazy_step = 0; /* 0=use ip, 1=use ip+1, 2=use ip+2 */

        if (LZ77_LIKELY(ip + 1 < ip_limit)) {
            uint32_t seq_next = lz77_read32(ip + 1) & 0xffffff;
            uint32_t hash_next = lz77_hash(seq_next);
            const uint8_t *ref_next = ip_start + htab[hash_next];
            uint32_t distance_next = (ip + 1) - ref_next;

            if (distance_next < MAX_DISTANCE &&
                (lz77_read32(ref_next) & 0xffffff) == seq_next) {
                uint32_t len_next = match_len(ref_next + MIN_MATCH_LEN,
                                              ip + 1 + MIN_MATCH_LEN, in_end) +
                                    1;

                /* accept lazy if worth the extra literal cost */
                if (len_next > len + (len < 7 ? 1 : 0)) {
                    lazy_step = 1;
                    len = len_next;
                    distance = distance_next;
                    ref = ref_next;
                }
            }
        }

        /* Step 2: Check if ip+2 has even better match (two-step lazy) */
        if (LZ77_LIKELY(ip + 2 < ip_limit)) {
            uint32_t seq_next2 = lz77_read32(ip + 2) & 0xffffff;
            uint32_t hash_next2 = lz77_hash(seq_next2);
            const uint8_t *ref_next2 = ip_start + htab[hash_next2];
            uint32_t distance_next2 = (ip + 2) - ref_next2;

            if (distance_next2 < MAX_DISTANCE &&
                (lz77_read32(ref_next2) & 0xffffff) == seq_next2) {
                uint32_t len_next2 = match_len(ref_next2 + MIN_MATCH_LEN,
                                               ip + 2 + MIN_MATCH_LEN, in_end) +
                                     1;

                /* accept if better than best considering 2-literal cost */
                if (len_next2 > len + (len < 7 ? 1 : 0)) {
                    lazy_step = 2;
                    len = len_next2;
                    distance = distance_next2;
                    ref = ref_next2;
                }
            }
        }

        /* Emit literals and update position based on lazy decision */
        if (lazy_step > 0) {
            op = literals(lazy_step, ip, op);
            ip += lazy_step;
            anchor = ip;
        }

        op = match(len, distance, op);

        /* update the hash at match boundary */
        ip += len;
        if (LZ77_LIKELY(ip + 4 <= in_end)) {
            seq = lz77_read32(ip);
            hash = lz77_hash(seq & 0xffffff);
            htab[hash] = ip++ - ip_start;
            seq >>= 8;
            hash = lz77_hash(seq);
            htab[hash] = ip++ - ip_start;
        } else {
            /* Not enough space for hash updates, but still advance ip by 2 */
            if (ip < in_end)
                ip++;
            if (ip < in_end)
                ip++;
        }

        /* light backfill: seed dictionary for long matches */
        if (len > 12) {
            const uint8_t *p = ip - len + 5;
            if (p > ip_start && p + 3 < ip && p + 4 <= in_end) {
                uint32_t s = lz77_read32(p) & 0xffffff;
                uint32_t h = lz77_hash(s);
                htab[h] = p - ip_start;
            }
        }

        anchor = ip;
    }

    return literals((uint8_t *) in + length - anchor, anchor, op) -
           (uint8_t *) out;
}

/**
 * Decompresses a block of LZ77-compressed data.
 *
 * This function decompresses data that was compressed with lz77_compress().
 * Decompression is fast and does not require a workspace buffer.
 *
 * @param in      Pointer to compressed data buffer
 * @param length  Length of compressed data in bytes
 * @param out     Pointer to output buffer for decompressed data
 * @param max_out Maximum size of output buffer (prevents buffer overflow)
 *
 * @return Size of decompressed data in bytes, or 0 on error
 *
 * @note Returns 0 if:
 *       - Input length is <= 0
 *       - Output buffer is too small (max_out insufficient)
 *       - Compressed data is corrupted or invalid
 *       - Backward reference goes outside valid range
 *
 * @note This function does not allocate any memory internally.
 *
 * Usage Example:
 * @code
 *   uint8_t compressed[512] = {...};  // Previously compressed data
 *   uint8_t output[1024];  // Buffer large enough for decompressed data
 *
 *   int decompressed_size = lz77_decompress(compressed, 512, output, 1024);
 *   if (decompressed_size > 0) {
 *       // output buffer contains decompressed_size bytes of original data
 *   } else {
 *       // Decompression failed - corrupt data or insufficient buffer
 *   }
 * @endcode
 */
int lz77_decompress(const void *in, int length, void *out, int max_out)
{
    /* Validate input length before any pointer operations */
    if (length <= 0)
        return 0;

    const uint8_t *ip = (const uint8_t *) in, *ip_limit = ip + length;
    const uint8_t *ip_bound = (length >= 2) ? (ip_limit - 2) : ip;
    uint8_t *op = (uint8_t *) out, *op_limit = op + max_out;
    uint32_t ctrl = (*ip++) & 31;

    while (1) {
        if (ctrl >= 32) {
            uint32_t len = (ctrl >> 5) - 1, ofs = (ctrl & 31) << 8;
            const uint8_t *ref = op - ofs - 1;

            if (LZ77_UNLIKELY(len == 6 && ip > ip_bound))
                return 0;
            if (len == 6)
                len += *ip++;

            ref -= *ip++;
            len += 3;
            if (LZ77_UNLIKELY(op + len > op_limit ||
                              ref < (const uint8_t *) out))
                return 0;
            for (uint32_t remain = len, distance = op - ref; remain;) {
                uint32_t chunk = remain < distance ? remain : distance;
                memcpy(op, ref, chunk);
                op += chunk, ref += chunk, remain -= chunk;
            }
        } else {
            ctrl++;
            if (LZ77_UNLIKELY(op + ctrl > op_limit || ip + ctrl > ip_limit))
                return 0;
            memcpy(op, ip, ctrl);
            ip += ctrl, op += ctrl;
        }

        if (LZ77_UNLIKELY(ip > ip_bound))
            break;

        ctrl = *ip++;
    }

    return op - (uint8_t *) out;
}

#endif /* LZ77_H */
