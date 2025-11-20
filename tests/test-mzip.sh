#!/bin/bash
# Comprehensive test suite for mzip/munzip compression tools
# Tests basic functionality and critical bug regression

# Don't use set -e - handle errors explicitly in each test

# Determine tool paths - use absolute paths
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -f "$SCRIPT_DIR/../tools/mzip" ]; then
    MZIP="$SCRIPT_DIR/../tools/mzip"
    MUNZIP="$SCRIPT_DIR/../tools/munzip"
elif [ -f "$SCRIPT_DIR/tools/mzip" ]; then
    MZIP="$SCRIPT_DIR/tools/mzip"
    MUNZIP="$SCRIPT_DIR/tools/munzip"
else
    echo "Error: Cannot find mzip/munzip tools"
    exit 1
fi

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PASS=0
FAIL=0
TOTAL=0

# Test directory
TESTDIR="/tmp/mzip-test-$$"
mkdir -p "$TESTDIR"
trap "rm -rf $TESTDIR" EXIT

echo "mzip/munzip Test Suite"
echo "======================"
echo ""

# Helper functions
pass()
{
    echo -e "${GREEN}[PASS]${NC} $1"
    ((PASS++))
    ((TOTAL++))
}

fail()
{
    echo -e "${RED}[FAIL]${NC} $1"
    ((FAIL++))
    ((TOTAL++))
}

# Test 1: Basic compression/decompression round-trip
test_basic_roundtrip()
{
    echo "Test: Basic compression and decompression"

    echo "Hello, mzip test!" > "$TESTDIR/input.txt"
    cp "$TESTDIR/input.txt" "$TESTDIR/input.txt.orig"

    if $MZIP "$TESTDIR/input.txt" "$TESTDIR/output.mz" > /dev/null 2>&1; then
        rm -f "$TESTDIR/input.txt"
        cd "$TESTDIR"
        if $MUNZIP output.mz > /dev/null 2>&1; then
            if diff -q input.txt input.txt.orig > /dev/null 2>&1; then
                pass "Basic round-trip successful"
            else
                fail "Data corruption in round-trip"
            fi
        else
            fail "Decompression failed"
        fi
        cd - > /dev/null
    else
        fail "Compression failed"
    fi
}

# Test 2: Deep path handling (basename extraction bug fix)
test_deep_path()
{
    echo "Test: Deep path basename extraction"

    mkdir -p "$TESTDIR/source/a/b/c/d/e/f"
    echo "deep path test" > "$TESTDIR/source/a/b/c/d/e/f/file.txt"

    if timeout 5 $MZIP "$TESTDIR/source/a/b/c/d/e/f/file.txt" "$TESTDIR/deep.mz" > /dev/null 2>&1; then
        # Extract in a clean directory to verify basename-only extraction
        mkdir -p "$TESTDIR/extract"
        cd "$TESTDIR/extract"
        if $MUNZIP ../deep.mz > /dev/null 2>&1; then
            # Should extract as just "file.txt", not create a/b/c/... directories
            if [ -f "file.txt" ] && [ ! -d "a" ]; then
                pass "Deep path handled correctly (basename only)"
            else
                fail "Deep path created directory structure"
            fi
        else
            fail "Deep path decompression failed"
        fi
        cd - > /dev/null
    else
        fail "Deep path compression timeout or failure"
    fi
}

# Test 3: Empty file handling
test_empty_file()
{
    echo "Test: Empty file compression/decompression"

    touch "$TESTDIR/empty.txt"

    if $MZIP "$TESTDIR/empty.txt" "$TESTDIR/empty.mz" > /dev/null 2>&1; then
        rm -f "$TESTDIR/empty.txt"
        cd "$TESTDIR"
        if $MUNZIP empty.mz > /dev/null 2>&1; then
            if [ -f "empty.txt" ] && [ ! -s "empty.txt" ]; then
                pass "Empty file handled correctly"
            else
                fail "Empty file incorrect"
            fi
        else
            fail "Empty file decompression failed"
        fi
        cd - > /dev/null
    else
        fail "Empty file compression failed"
    fi
}

# Test 4: Large file handling
test_large_file()
{
    echo "Test: Large file (>128KB, multiple chunks)"

    # Create 300KB file
    dd if=/dev/urandom of="$TESTDIR/large.bin" bs=1024 count=300 > /dev/null 2>&1
    cp "$TESTDIR/large.bin" "$TESTDIR/large.bin.orig"

    if $MZIP "$TESTDIR/large.bin" "$TESTDIR/large.mz" > /dev/null 2>&1; then
        rm -f "$TESTDIR/large.bin"
        cd "$TESTDIR"
        if $MUNZIP large.mz > /dev/null 2>&1; then
            if diff -q large.bin large.bin.orig > /dev/null 2>&1; then
                pass "Large file (multi-chunk) handled correctly"
            else
                fail "Large file data corruption"
            fi
        else
            fail "Large file decompression failed"
        fi
        cd - > /dev/null
    else
        fail "Large file compression failed"
    fi
}

# Test 5: Filename with spaces
test_spaces_in_name()
{
    echo "Test: Filename with spaces"

    echo "spaces test" > "$TESTDIR/file with spaces.txt"

    if $MZIP "$TESTDIR/file with spaces.txt" "$TESTDIR/spaces.mz" > /dev/null 2>&1; then
        rm -f "$TESTDIR/file with spaces.txt"
        cd "$TESTDIR"
        if $MUNZIP spaces.mz > /dev/null 2>&1; then
            if [ -f "file with spaces.txt" ]; then
                pass "Spaces in filename handled correctly"
            else
                fail "Spaces in filename not preserved"
            fi
        else
            fail "Spaces filename decompression failed"
        fi
        cd - > /dev/null
    else
        fail "Spaces filename compression failed"
    fi
}

# Test 6: Magic number detection
test_magic_detection()
{
    echo "Test: Magic number detection (prevent double compression)"

    echo "test data" > "$TESTDIR/data.txt"
    $MZIP "$TESTDIR/data.txt" "$TESTDIR/archive.mz" > /dev/null 2>&1

    # Try to compress already compressed file
    if $MZIP "$TESTDIR/archive.mz" "$TESTDIR/archive2.mz" 2>&1 | grep -q "already a mzip archive"; then
        pass "Magic detection prevents double compression"
    else
        fail "Magic detection failed"
    fi
}

# Test 7: Non-mzip file rejection
test_non_mzip_rejection()
{
    echo "Test: Non-mzip file rejection"

    echo "This is not a mzip archive" > "$TESTDIR/fake.mz"

    cd "$TESTDIR"
    if $MUNZIP fake.mz 2>&1 | grep -q "not a mzip archive"; then
        pass "Non-mzip file correctly rejected"
    else
        fail "Non-mzip file not detected"
    fi
    cd - > /dev/null
}

# Test 8: File already exists protection
test_overwrite_protection()
{
    echo "Test: Overwrite protection"

    echo "original" > "$TESTDIR/test.txt"
    $MZIP "$TESTDIR/test.txt" "$TESTDIR/test.mz" > /dev/null 2>&1

    # Try to create archive with existing name
    if $MZIP "$TESTDIR/test.txt" "$TESTDIR/test.mz" 2>&1 | grep -q "already exists"; then
        pass "Overwrite protection works"
    else
        fail "Overwrite protection failed"
    fi
}

# Test 9: Binary data integrity
test_binary_data()
{
    echo "Test: Binary data integrity"

    # Create binary file with all byte values
    python3 -c "import sys; sys.stdout.buffer.write(bytes(range(256)) * 100)" > "$TESTDIR/binary.dat" 2> /dev/null \
        || perl -e 'print pack("C*", 0..255) x 100' > "$TESTDIR/binary.dat"
    cp "$TESTDIR/binary.dat" "$TESTDIR/binary.dat.orig"

    if $MZIP "$TESTDIR/binary.dat" "$TESTDIR/binary.mz" > /dev/null 2>&1; then
        rm -f "$TESTDIR/binary.dat"
        cd "$TESTDIR"
        if $MUNZIP binary.mz > /dev/null 2>&1; then
            if diff -q binary.dat binary.dat.orig > /dev/null 2>&1; then
                pass "Binary data integrity preserved"
            else
                fail "Binary data corruption"
            fi
        else
            fail "Binary data decompression failed"
        fi
        cd - > /dev/null
    else
        fail "Binary data compression failed"
    fi
}

# Test 10: Command-line argument handling
test_cmdline_args()
{
    echo "Test: Command-line argument parsing"

    # Test help flag
    if $MZIP --help > /dev/null 2>&1; then
        # Test unknown flag
        if $MZIP --unknown 2>&1 | grep -q "unknown option"; then
            pass "Command-line argument parsing works"
        else
            fail "Unknown option not detected"
        fi
    else
        fail "Help flag failed"
    fi
}

# Run all tests
test_basic_roundtrip
test_deep_path
test_empty_file
test_large_file
test_spaces_in_name
test_magic_detection
test_non_mzip_rejection
test_overwrite_protection
test_binary_data
test_cmdline_args

# Summary
echo ""
echo "======================"
echo "Results: $PASS passed, $FAIL failed (total: $TOTAL)"

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}All mzip/munzip tests PASSED${NC}"
    exit 0
else
    echo -e "${RED}Some tests FAILED${NC}"
    exit 1
fi
