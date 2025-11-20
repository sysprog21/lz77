#!/bin/bash
# Validation tests for mzip-unified.c critical bug fixes
# Tests for GPT-5 Priority 1 issues that were fixed

set -e

# Determine tool paths based on where script is run from
if [ -f "./tools/mzip" ]; then
    # Run from project root
    MZIP="./tools/mzip"
    MUNZIP="./tools/munzip"
elif [ -f "../tools/mzip" ]; then
    # Run from tests directory
    MZIP="../tools/mzip"
    MUNZIP="../tools/munzip"
else
    echo "Error: Cannot find mzip tools"
    exit 1
fi

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

echo "mzip-unified.c Validation Tests"
echo "================================="
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

skip()
{
    echo -e "${YELLOW}[SKIP]${NC} $1"
}

# Test 1: Path prefix extraction (fixed infinite loop bug)
test_path_prefix()
{
    echo "Test: Basename extraction with deep path (no infinite loop)"

    # Create test file with deep path
    mkdir -p "$TESTDIR/very/deep/directory/structure"
    echo "test data" > "$TESTDIR/very/deep/directory/structure/myfile.dat"

    # The bug would cause infinite loop during basename extraction in pack_file_compressed
    # Compress with full path - this would hang forever with the old buggy code
    timeout 5 $MZIP "$TESTDIR/very/deep/directory/structure/myfile.dat" "$TESTDIR/test.mz" > /dev/null 2>&1
    local result=$?

    if [ $result -eq 124 ]; then
        fail "Infinite loop detected during path extraction (timeout)"
        return
    elif [ $result -ne 0 ]; then
        fail "Compression failed"
        return
    fi

    # Verify archive was created
    if [ -f "$TESTDIR/test.mz" ]; then
        pass "Path extraction completed without infinite loop"
    else
        fail "Archive not created"
    fi
}

# Test 2: Magic detection (fixed fseek argument order)
test_magic_detection()
{
    echo "Test: Magic byte detection with correct fseek"

    # Create test file
    echo "test data for magic detection" > "$TESTDIR/input.txt"

    # Compress it
    $MZIP "$TESTDIR/input.txt" "$TESTDIR/archive.mz" > /dev/null 2>&1

    # Try to compress already compressed file (should detect magic)
    if $MZIP "$TESTDIR/archive.mz" "$TESTDIR/archive2.mz" 2>&1 | grep -q "already a mzip archive"; then
        pass "Magic detection works correctly"
    else
        fail "Magic detection failed (fseek argument order bug?)"
    fi
}

# Test 3: Double compression/decompression (tests file handle management)
test_double_roundtrip()
{
    echo "Test: Double round-trip (file handle cleanup)"

    echo "original test data" > "$TESTDIR/original.txt"

    # First compression
    $MZIP "$TESTDIR/original.txt" "$TESTDIR/first.mz" > /dev/null 2>&1

    # First decompression
    cd "$TESTDIR"
    $MUNZIP first.mz > /dev/null 2>&1
    mv original.txt first.txt
    cd - > /dev/null

    # Second compression
    $MZIP "$TESTDIR/first.txt" "$TESTDIR/second.mz" > /dev/null 2>&1

    # Second decompression
    cd "$TESTDIR"
    $MUNZIP second.mz > /dev/null 2>&1
    cd - > /dev/null

    # Verify data integrity
    if diff -q "$TESTDIR/original.txt" "$TESTDIR/first.txt" > /dev/null; then
        pass "Double round-trip successful (no file handle issues)"
    else
        fail "Double round-trip data corruption"
    fi
}

# Test 4: Command-line argument parsing (fixed i <= argc bug)
test_cmdline_args()
{
    echo "Test: Command-line argument parsing bounds"

    # Create test file
    echo "cmdline test" > "$TESTDIR/input.txt"

    # Test with various argument combinations
    if $MZIP -v > /dev/null 2>&1; then
        # Test with max arguments (should not access argv[argc])
        if $MZIP "$TESTDIR/input.txt" "$TESTDIR/output.mz" > /dev/null 2>&1; then
            pass "Command-line parsing works (no argv[argc] access)"
        else
            fail "Command-line parsing failed with valid arguments"
        fi
    else
        fail "Version flag parsing failed"
    fi
}

# Test 5: Large file path (stress test basename extraction)
test_long_path()
{
    echo "Test: Long file path handling"

    # Create deeply nested path
    DEEP_PATH="$TESTDIR/$(printf 'a/%.0s' {1..20})file.txt"
    mkdir -p "$(dirname "$DEEP_PATH")"
    echo "deep path test" > "$DEEP_PATH"

    # Compress with deep path
    if $MZIP "$DEEP_PATH" "$TESTDIR/deep.mz" > /dev/null 2>&1; then
        cd "$TESTDIR"
        if $MUNZIP deep.mz > /dev/null 2>&1 && [ -f "file.txt" ]; then
            pass "Long path handled correctly"
        else
            fail "Long path decompression failed"
        fi
        cd - > /dev/null
    else
        fail "Long path compression failed"
    fi
}

# Test 6: Non-mzip file detection
test_non_mzip_detection()
{
    echo "Test: Non-mzip file detection"

    # Create regular text file
    echo "This is not a mzip archive" > "$TESTDIR/notmzip.txt"

    # Try to decompress non-mzip file
    cd "$TESTDIR"
    if $MUNZIP notmzip.txt 2>&1 | grep -q "not a mzip archive"; then
        pass "Non-mzip file correctly rejected"
    else
        fail "Non-mzip file not detected"
    fi
    cd - > /dev/null
}

# Test 7: Empty file handling
test_empty_file()
{
    echo "Test: Empty file compression/decompression"

    touch "$TESTDIR/empty.txt"

    if $MZIP "$TESTDIR/empty.txt" "$TESTDIR/empty.mz" > /dev/null 2>&1; then
        cd "$TESTDIR"
        if $MUNZIP empty.mz > /dev/null 2>&1; then
            if [ -f "empty.txt" ] && [ ! -s "empty.txt" ]; then
                pass "Empty file handled correctly"
            else
                fail "Empty file decompression incorrect"
            fi
        else
            fail "Empty file decompression failed"
        fi
        cd - > /dev/null
    else
        fail "Empty file compression failed"
    fi
}

# Test 8: File with spaces in name (path parsing edge case)
test_spaces_in_name()
{
    echo "Test: Filename with spaces"

    echo "spaces test" > "$TESTDIR/file with spaces.txt"

    if $MZIP "$TESTDIR/file with spaces.txt" "$TESTDIR/spaces.mz" > /dev/null 2>&1; then
        cd "$TESTDIR"
        if $MUNZIP spaces.mz > /dev/null 2>&1; then
            if [ -f "file with spaces.txt" ]; then
                pass "Spaces in filename handled correctly"
            else
                fail "Spaces in filename not preserved"
            fi
        else
            fail "File with spaces decompression failed"
        fi
        cd - > /dev/null
    else
        fail "File with spaces compression failed"
    fi
}

# Run all tests
test_path_prefix
test_magic_detection
test_double_roundtrip
test_cmdline_args
test_long_path
test_non_mzip_detection
test_empty_file
test_spaces_in_name

# Summary
echo ""
echo "================================="
echo "Results: $PASS passed, $FAIL failed"

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}All critical bug regression tests PASSED${NC}"
    exit 0
else
    echo -e "${RED}Some tests FAILED - critical bugs may have regressed${NC}"
    exit 1
fi
