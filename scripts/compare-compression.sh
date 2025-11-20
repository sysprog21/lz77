#!/bin/bash
# Compare compression ratios and binary sizes of different compression tools
# Tools: lz77 (this project), lz4, gzip, bzip2

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DATASET_DIR="${PROJECT_ROOT}/tests/dataset"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()
{
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_section()
{
    echo -e "\n${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}\n"
}

# Test files selection
TEST_FILES=(
    "canterbury/alice29.txt"
    "canterbury/asyoulik.txt"
    "canterbury/fields.c"
    "canterbury/kennedy.xls"
    "silesia/dickens"
    "silesia/mozilla"
    "silesia/mr"
    "silesia/xml"
    "enwik/enwik8.txt"
)

# Create temp directory
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

log_section "BINARY SIZE COMPARISON"

echo "Tool                Binary    Dynamic Libraries               Total (est.)"
echo "----                ------    -----------------               ------------"
printf "lz77 (mzip)         50K       libSystem only                 50K\n"
printf "lz4                 248K      libSystem only                 248K\n"
printf "gzip                168K      libbz2, liblzma, libz          ~512K\n"
printf "bzip2               165K      libbz2                         ~228K\n"

echo ""
echo "Library sizes (typical):"
echo "  libbz2:  ~63K  (bzip2 compression)"
echo "  liblzma: ~181K (xz/lzma compression)"
echo "  libz:    ~100K (zlib/deflate compression)"
echo ""
echo "Note: gzip binary includes support for multiple formats (gzip/bzip2/xz)"
echo "      via dynamic linking to system libraries."

log_section "COMPRESSION RATIO COMPARISON"

printf "%-30s %12s %12s %12s %12s %12s\n" "File" "Original" "lz77" "lz4" "gzip" "bzip2"
printf "%-30s %12s %12s %12s %12s %12s\n" "----" "--------" "----" "---" "----" "-----"

total_original=0
total_lz77=0
total_lz4=0
total_gzip=0
total_bzip2=0

for file in "${TEST_FILES[@]}"; do
    filepath="${DATASET_DIR}/${file}"

    if [ ! -f "$filepath" ]; then
        continue
    fi

    # Get original size
    original_size=$(stat -f%z "$filepath")
    total_original=$((total_original + original_size))

    # Compress with lz77
    "${PROJECT_ROOT}/tools/mzip" "$filepath" "${TEMP_DIR}/test.mz" 2> /dev/null
    lz77_size=$(stat -f%z "${TEMP_DIR}/test.mz")
    total_lz77=$((total_lz77 + lz77_size))
    lz77_ratio=$(echo "scale=2; $lz77_size * 100 / $original_size" | bc)
    rm -f "${TEMP_DIR}/test.mz"

    # Compress with lz4
    lz4 -q "$filepath" "${TEMP_DIR}/test.lz4" 2> /dev/null
    lz4_size=$(stat -f%z "${TEMP_DIR}/test.lz4")
    total_lz4=$((total_lz4 + lz4_size))
    lz4_ratio=$(echo "scale=2; $lz4_size * 100 / $original_size" | bc)
    rm -f "${TEMP_DIR}/test.lz4"

    # Compress with gzip
    gzip -c "$filepath" > "${TEMP_DIR}/test.gz" 2> /dev/null
    gzip_size=$(stat -f%z "${TEMP_DIR}/test.gz")
    total_gzip=$((total_gzip + gzip_size))
    gzip_ratio=$(echo "scale=2; $gzip_size * 100 / $original_size" | bc)
    rm -f "${TEMP_DIR}/test.gz"

    # Compress with bzip2
    bzip2 -c "$filepath" > "${TEMP_DIR}/test.bz2" 2> /dev/null
    bzip2_size=$(stat -f%z "${TEMP_DIR}/test.bz2")
    total_bzip2=$((total_bzip2 + bzip2_size))
    bzip2_ratio=$(echo "scale=2; $bzip2_size * 100 / $original_size" | bc)
    rm -f "${TEMP_DIR}/test.bz2"

    # Format sizes for display
    orig_fmt=$(numfmt --to=iec-i --suffix=B $original_size 2> /dev/null || echo "${original_size}B")
    lz77_fmt=$(numfmt --to=iec-i --suffix=B $lz77_size 2> /dev/null || echo "${lz77_size}B")
    lz4_fmt=$(numfmt --to=iec-i --suffix=B $lz4_size 2> /dev/null || echo "${lz4_size}B")
    gzip_fmt=$(numfmt --to=iec-i --suffix=B $gzip_size 2> /dev/null || echo "${gzip_size}B")
    bzip2_fmt=$(numfmt --to=iec-i --suffix=B $bzip2_size 2> /dev/null || echo "${bzip2_size}B")

    printf "%-30s %10s %9s(%2.0f%%) %9s(%2.0f%%) %9s(%2.0f%%) %9s(%2.0f%%)\n" \
        "$file" "$orig_fmt" \
        "$lz77_fmt" "$lz77_ratio" \
        "$lz4_fmt" "$lz4_ratio" \
        "$gzip_fmt" "$gzip_ratio" \
        "$bzip2_fmt" "$bzip2_ratio"
done

log_section "TOTAL STATISTICS"

total_lz77_ratio=$(echo "scale=2; $total_lz77 * 100 / $total_original" | bc)
total_lz4_ratio=$(echo "scale=2; $total_lz4 * 100 / $total_original" | bc)
total_gzip_ratio=$(echo "scale=2; $total_gzip * 100 / $total_original" | bc)
total_bzip2_ratio=$(echo "scale=2; $total_bzip2 * 100 / $total_original" | bc)

orig_total_fmt=$(numfmt --to=iec-i --suffix=B $total_original 2> /dev/null || echo "${total_original}B")
lz77_total_fmt=$(numfmt --to=iec-i --suffix=B $total_lz77 2> /dev/null || echo "${total_lz77}B")
lz4_total_fmt=$(numfmt --to=iec-i --suffix=B $total_lz4 2> /dev/null || echo "${total_lz4}B")
gzip_total_fmt=$(numfmt --to=iec-i --suffix=B $total_gzip 2> /dev/null || echo "${total_gzip}B")
bzip2_total_fmt=$(numfmt --to=iec-i --suffix=B $total_bzip2 2> /dev/null || echo "${total_bzip2}B")

printf "%-30s %10s %9s(%2.0f%%) %9s(%2.0f%%) %9s(%2.0f%%) %9s(%2.0f%%)\n" \
    "TOTAL" "$orig_total_fmt" \
    "$lz77_total_fmt" "$total_lz77_ratio" \
    "$lz4_total_fmt" "$total_lz4_ratio" \
    "$gzip_total_fmt" "$total_gzip_ratio" \
    "$bzip2_total_fmt" "$total_bzip2_ratio"

log_section "SUMMARY"

echo "Compression Ratio (lower is better):"
echo "  lz77 (this project): ${total_lz77_ratio}%"
echo "  lz4:                 ${total_lz4_ratio}%"
echo "  gzip:                ${total_gzip_ratio}%"
echo "  bzip2:               ${total_bzip2_ratio}%"
echo ""
echo "Total Size Including Dependencies (smaller is better):"
echo "  lz77 (mzip):  50K  (no compression library dependencies)"
echo "  lz4:          248K (no compression library dependencies)"
echo "  bzip2:        ~228K (165K binary + ~63K libbz2)"
echo "  gzip:         ~512K (168K binary + ~344K libraries)"
echo ""
echo "Key Insight: lz77 achieves smallest total footprint with zero"
echo "             compression library dependencies, making it ideal"
echo "             for embedded systems and minimal installations."
