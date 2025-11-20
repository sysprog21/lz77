# lz77

A minimalist C implementation of the [Lempel-Ziv 77 algorithm](https://en.wikipedia.org/wiki/LZ77_and_LZ78) for lossless data compression.

## Features
- Single-header library - drop `lz77.h` into your project
- Zero hidden allocations - caller controls all memory
- Embedded-friendly - predictable 32KB memory requirement
- Pure LZ77 - no entropy coding, simple and fast
- Tiny footprint - 20K binary with no compression-specific library dependencies

## Quick Start

### Building

```bash
make                # Build tools and tests
make check          # Run all tests (downloads ~200MB datasets on first run)
make clean          # Clean build artifacts
```

### Command-Line Tools

```bash
tools/mzip input.txt output.mz      # Compress
tools/munzip output.mz              # Decompress to stdout
```

### Library Usage
```c
#include "lz77.h"

uint8_t input[1000] = { /* your data */ };
// Safe output buffer size: length + length/32 + 128
uint8_t compressed[1000 + 1000/32 + 128];
uint8_t workmem[LZ77_WORKMEM_SIZE];  // 32KB workspace

// Compress
int compressed_size = lz77_compress(input, 1000, compressed, workmem);

// Decompress (no workspace needed)
uint8_t output[1000];
lz77_decompress(compressed, compressed_size, output, 1000);
```

## API Reference

### Functions

```c
int lz77_compress(const void *in, int length, void *out, void *workmem);
```
Compresses data using LZ77 algorithm.
- `in`: Input data buffer
- `length`: Input data size in bytes
- `out`: Output buffer for compressed data (must be at least `length + length/32 + 128` bytes)
- `workmem`: 32KB workspace (use `LZ77_WORKMEM_SIZE`)
- Returns: Compressed size in bytes

Note: To avoid buffer overflow, ensure `out` buffer size ≥ `length + length/32 + 128` bytes.
In worst case (incompressible data), output may be slightly larger than input.

```c
int lz77_decompress(const void *in, int length, void *out, int max_out);
```
Decompresses LZ77-compressed data.
- `in`: Compressed data buffer
- `length`: Compressed data size in bytes
- `out`: Output buffer for decompressed data
- `max_out`: Maximum output buffer size
- Returns: Decompressed size in bytes, or 0 on error

### Memory Requirements

| Operation | Workspace | Notes |
|-----------|-----------|-------|
| Compression | 32KB | Caller provides via `workmem` parameter |
| Decompression | 0 bytes | Zero workspace required |

### Limitations

| Constraint | Value | Rationale |
|-----------|-------|-----------|
| Maximum input size | ~4 GiB | Hash table uses uint32_t for position storage |

**Note**: Files larger than 4 GiB may cause integer overflow in position tracking, leading to compression failure.
This constraint is inherent to the 32KB memory design (8192 × uint32_t hash table).
For practical embedded systems use cases, this limit is acceptable.

### Algorithm Parameters

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Sliding window | 8KB | Balances compression vs memory |
| Hash table | 8192 entries | Optimizes ratio/speed/memory |
| Min match length | 3 bytes | Balances ratio vs overhead |
| Max match length | 264 bytes | Efficient encoding limit |

## Performance

### Compression Results

Benchmark on standard test corpora (Canterbury, Silesia, Enwik8):

| File | Original | Compressed | Ratio |
|------|----------|------------|-------|
| alice29.txt | 149KB | 86KB | 58% |
| kennedy.xls | 1006KB | 406KB | 40% |
| dickens | 9.7MB | 5.9MB | 61% |
| xml | 5.1MB | 1.4MB | 26% |
| enwik8.txt | 95MB | 54MB | 56% |

### Comparison vs Other Compressors

| Tool | Ratio | Binary Size | Total Footprint |
|------|-------|-------------|-----------------|
| lz77 | 51% | 20K | 20K (no deps) |
| lz4 | 56% | 198K | 198K |
| gzip | 35% | 91K | 91K |
| bzip2 | 28% | 38K | 115K (+libbz2) |

Key advantage: 10x smaller footprint than lz4 with no compression-specific library dependencies.

See [BENCH.md](BENCH.md) for detailed analysis.

## Testing

```bash
make check          # Run all tests
```

Test suite includes:
- 12 API unit tests (edge cases, round-trip validation)
- 20 integration tests (benchmark corpus files)
- ~200MB test datasets auto-downloaded on first run

## Design Details

### Caller-Provided Workspace

The compression function requires a 32KB workspace buffer that the caller provides. This design, used by industry-standard libraries (zlib, LZ4, Zstd), offers:

Benefits:
- Explicit memory management - no hidden allocations
- Embedded-friendly - no heap required
- Zero allocation overhead per compression
- Predictable memory footprint (known at compile-time)
- Reusable workspace across multiple compressions

Usage Patterns:
```c
// Stack allocation (simple)
uint8_t workmem[LZ77_WORKMEM_SIZE];
lz77_compress(input, len, output, workmem);

// Heap allocation (reusable)
uint8_t *workmem = malloc(LZ77_WORKMEM_SIZE);
for (int i = 0; i < num_files; i++) {
    lz77_compress(files[i].data, files[i].len, output, workmem);
}
free(workmem);
```

### Optimizations

The implementation includes algorithmic optimizations for improved compression:
1. Hash function improvement - Stronger avalanche mixing reduces collisions
2. Lazy parsing - Checks next position for better matches before emitting
3. Dictionary backfill - Seeds hash table during long matches for future compression

These optimizations maintain design constraints: pure LZ77, zero decompression workspace, byte-aligned tokens, single-pass, <250 LOC.

### Test Datasets

Standard compression benchmark corpora used worldwide:

| Corpus | Size | Files | Description |
|--------|------|-------|-------------|
| Canterbury | ~3MB | 11 | Diverse file types |
| Silesia | ~103MB | 8 | Large real-world files |
| Enwik8 | ~95MB | 1 | Wikipedia XML dump |

Total: ~200MB downloaded automatically via `make dataset` or `make check`

Features: idempotent downloads, mirror fallback, MD5 verification, automatic cleanup

## License
`lz77` is available under a permissive MIT-style license.
Use of this source code is governed by a MIT license that can be found in the [LICENSE](LICENSE) file.
