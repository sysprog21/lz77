#!/bin/sh
# Fuzzer wrapper with progress bar display
# Parses libFuzzer output and shows a clean progress bar
#
# Usage: ./scripts/fuzz-progress.sh <fuzzer_binary> <max_time_seconds> [fuzzer_args...]

set -e

FUZZER="$1"
MAX_TIME="$2"
shift 2

if [ -z "$FUZZER" ] || [ -z "$MAX_TIME" ]; then
    echo "Usage: $0 <fuzzer_binary> <max_time_seconds> [fuzzer_args...]"
    exit 1
fi

# Progress bar configuration
BAR_WIDTH=30
FILLED_CHAR="█"
EMPTY_CHAR="░"

# ANSI colors
GREEN="\033[32m"
YELLOW="\033[33m"
CYAN="\033[36m"
RESET="\033[0m"
BOLD="\033[1m"
CLEAR_LINE="\033[2K"

# State tracking
start_time=$(date +%s)

draw_progress_bar() {
    elapsed=$1
    cov=$2
    corpus=$3
    exec_s=$4

    # Calculate percentage
    if [ "$MAX_TIME" -gt 0 ]; then
        pct=$((elapsed * 100 / MAX_TIME))
        [ "$pct" -gt 100 ] && pct=100
    else
        pct=0
    fi

    # Calculate filled/empty portions
    filled=$((pct * BAR_WIDTH / 100))
    empty=$((BAR_WIDTH - filled))

    # Build progress bar
    bar=""
    i=0
    while [ $i -lt $filled ]; do
        bar="${bar}${FILLED_CHAR}"
        i=$((i + 1))
    done
    i=0
    while [ $i -lt $empty ]; do
        bar="${bar}${EMPTY_CHAR}"
        i=$((i + 1))
    done

    # Format time remaining
    remain=$((MAX_TIME - elapsed))
    if [ "$remain" -lt 0 ]; then
        remain=0
    fi
    remain_mins=$((remain / 60))
    remain_secs=$((remain % 60))

    # Format exec/s with K suffix for large numbers
    if [ "$exec_s" -ge 1000 ]; then
        exec_k=$((exec_s / 1000))
        exec_str="${exec_k}K"
    else
        exec_str="$exec_s"
    fi

    # Print progress line (overwrite previous)
    # Format: [████████░░░░░░░░░░░░░░░░░░░░░░] 50% 2:30 left | 318 edges | 969 inputs | 2.8K/s
    printf "\r${CLEAR_LINE}"
    printf "${CYAN}[${bar}]${RESET} "
    printf "${BOLD}%d%%${RESET} " "$pct"
    printf "${GREEN}%d:%02d${RESET} left " "$remain_mins" "$remain_secs"
    printf "| ${YELLOW}%d${RESET} edges " "$cov"
    printf "| %d inputs " "$corpus"
    printf "| %s/s" "$exec_str"
}

# Print header
total_mins=$((MAX_TIME / 60))
echo ""
printf "${BOLD}LZ77 Fuzzer${RESET} (%d min)\n" "$total_mins"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Run fuzzer and parse output
"$FUZZER" "$@" 2>&1 | while IFS= read -r line; do
    # Parse libFuzzer status lines
    # Format: #12345  NEW    cov: 246 ft: 2800 corp: 350/40Kb exec/s: 5000
    case "$line" in
        *"cov:"*"ft:"*"corp:"*"exec/s:"*)
            # Extract values using shell parameter expansion
            # Use LC_ALL=C to handle binary data in libFuzzer output
            cov=$(echo "$line" | LC_ALL=C sed -n 's/.*cov: *\([0-9]*\).*/\1/p')
            corpus=$(echo "$line" | LC_ALL=C sed -n 's/.*corp: *\([0-9]*\).*/\1/p')
            exec_s=$(echo "$line" | LC_ALL=C sed -n 's/.*exec\/s: *\([0-9]*\).*/\1/p')

            # Calculate elapsed time
            now=$(date +%s)
            elapsed=$((now - start_time))

            # Update progress bar
            if [ -n "$cov" ]; then
                draw_progress_bar "$elapsed" "$cov" "$corpus" "$exec_s"
            fi
            ;;
        *"BINGO"*|*"ERROR"*|*"CRASH"*|*"TIMEOUT"*)
            # Show important messages
            printf "\n${BOLD}>>> %s${RESET}\n" "$line"
            ;;
        "Done "*)
            # Final summary line - print newline before it
            printf "\n%s\n" "$line"
            ;;
        "stat::"*)
            # Stats lines - print without extra newlines
            echo "$line"
            ;;
    esac
done

# Final summary
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
printf "${GREEN}Fuzzing complete${RESET}\n"
