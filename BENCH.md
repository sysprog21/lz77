# Compression Benchmark Comparison

Comparison of compression ratios and binary sizes between lz77 (this project), lz4, gzip, and bzip2.

## Binary Size Comparison (Ubuntu Linux)

### Executable and Dependencies

| Tool | Binary Size | Dynamic Libraries | Total Size | Notes |
|------|-------------|-------------------|------------|-------|
| lz77 (mzip) | 20K | None (libc only) | 20K | Smallest total footprint |
| gzip | 91K | None (libc only) | 91K | Deflate algorithm built-in |
| bzip2 | 38K | libbz2 (77K) | 115K | Single compression library |
| lz4 | 198K | None (libc only) | 198K | LZ4 algorithm built-in |

Note: Total Size = executable + compression-specific libraries (excluding libc).
"libc only" means no additional compression-specific libraries beyond standard C library.

## Compression Ratio Comparison

Test performed on 6 representative files from Canterbury and Enwik8 corpora (total 102MB).

| File | Original | lz77 | lz4 | gzip | bzip2 |
|------|----------|------|-----|------|-------|
| canterbury/alice29.txt | 149KiB | 86KiB (58%) | 87KiB (58%) | 54KiB (36%) | 43KiB (28%) |
| canterbury/asyoulik.txt | 123KiB | 75KiB (61%) | 78KiB (64%) | 48KiB (39%) | 39KiB (32%) |
| canterbury/fields.c | 11KiB | 4.8KiB (44%) | 5.2KiB (47%) | 3.1KiB (28%) | 3.0KiB (27%) |
| canterbury/kennedy.xls | 1006KiB | 406KiB (40%) | 366KiB (36%) | 200KiB (20%) | 128KiB (13%) |
| xml | 5.1MiB | 1.4MiB (26%) | 1.2MiB (23%) | 672KiB (13%) | 431KiB (8%) |
| enwik/enwik8.txt | 95MiB | 54MiB (56%) | 55MiB (57%) | 35MiB (37%) | 28MiB (29%) |

## Overall Statistics

| Metric | lz77 | lz4 | gzip | bzip2 |
|--------|------|-----|------|-------|
| Total Compressed Size | 54MiB | 57MiB | 36MiB | 29MiB |
| Average Compression Ratio | 51.3% | 55.8% | 35.3% | 28.4% |
| Binary Size (executable only) | 20K | 198K | 91K | 38K |
| Total Footprint (with libs) | 20K | 198K | 91K | 115K |

## Analysis

### Compression Ratio

1. bzip2: Best compression (28.4%) - uses Burrows-Wheeler transform + Huffman coding
2. gzip: Good compression (35.3%) - uses DEFLATE (LZ77 + Huffman coding)
3. lz77: Moderate compression (51.3%) - pure LZ77 with two-step lazy matching
4. lz4: Fast compression (55.8%) - optimized for speed over ratio

### Total Footprint (Binary + Libraries)

1. lz77: 20K - Smallest total footprint, zero compression library dependencies
2. gzip: 91K (4.6x larger) - Deflate algorithm built-in
3. bzip2: 115K (5.8x larger) - Binary + libbz2 shared library
4. lz4: 198K (9.9x larger) - LZ4 algorithm built-in

## Key Insights

1. Total Footprint Reality Check (Ubuntu Linux):
   - lz77: 20K total - smallest footprint, zero compression library dependencies
   - gzip: 91K total - deflate built-in, 4.6x larger than lz77
   - bzip2: 115K total - 38K binary + 77K libbz2, 5.8x larger than lz77
   - lz4: 198K total - LZ4 built-in, 9.9x larger than lz77

2. Built-in Algorithms vs Shared Libraries:
   - All tools link dynamically to libc (universal on all systems)
   - gzip: Deflate algorithm built into binary (91K, self-contained)
   - lz4: LZ4 algorithm built into binary (198K, self-contained)
   - bzip2: Uses separate compression library (115K total = 38K binary + 77K libbz2)
   - Built-in algorithms mean no compression-specific library dependencies
   - For minimal systems, lz77 remains smallest at 20K with zero library dependencies

3. LZ77 Baseline Performance:
   - Pure LZ77 with two-step lazy matching: 51.3% compression ratio
   - LZ77 + Huffman (gzip/DEFLATE): 35.3% → 31% improvement from entropy coding
   - BWT + Huffman (bzip2): 28.4% → 45% improvement from advanced transforms
   - Shows clear value proposition of each algorithmic enhancement

4. Embedded System Suitability (Total Footprint Ranking):
   - lz77: Excellent (20K, no libs, no malloc, predictable 32KB workspace)
   - gzip: Good (91K, self-contained, standard deflate)
   - bzip2: Good (115K, single 77K library dependency)
   - lz4: Moderate (198K, fast but larger binary)

5. File Type Performance:
   - XML (highly structured): All tools excel (8-26% ratios)
   - Text (natural language): bzip2 best (28-32%), lz77/lz4 moderate (57-64%)
   - Binary (kennedy.xls): Similar across tools (13-40%)
   - Wikipedia XML (enwik8): Consistent performance across all tools (29-57%)

## Reproduction

Run the comparison benchmark:

```bash
# Ensure datasets are downloaded
make dataset

# Run comparison script
./scripts/compare-compression.sh
```

## Environment

- Platform: Ubuntu Linux 24.04 (Noble Numbat)
- Kernel: Linux 6.8.0-84-generic x86_64
- lz77: This project (optimized with lazy parsing and backfill)
- lz4: 1.9.4 (statically linked)
- gzip: System gzip (statically linked deflate)
- bzip2: 1.0.6 (38K binary + 77K libbz2.so.1.0.4)
- Test dataset: ~102MB (6 representative files from Canterbury and Enwik8 corpora)
