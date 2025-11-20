#!/bin/bash
# Download compression benchmark datasets for testing
# Datasets: Canterbury Corpus, Silesia Corpus, Enwik8

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DATASET_DIR="${PROJECT_ROOT}/tests/dataset"
TEMP_DIR="${DATASET_DIR}/.tmp"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info()
{
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn()
{
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error()
{
    echo -e "${RED}[ERROR]${NC} $1"
}

check_command()
{
    if ! command -v "$1" &> /dev/null; then
        log_error "Required command '$1' not found"
        exit 1
    fi
}

download_file()
{
    local url="$1"
    local output="$2"

    log_info "Downloading $(basename "$output")..."

    if command -v curl &> /dev/null; then
        curl -L -o "$output" "$url" || return 1
    elif command -v wget &> /dev/null; then
        wget -O "$output" "$url" || return 1
    else
        log_error "Neither curl nor wget found"
        return 1
    fi
}

verify_md5()
{
    local file="$1"
    local expected_md5="$2"

    if [ -z "$expected_md5" ]; then
        return 0
    fi

    local actual_md5
    if command -v md5sum &> /dev/null; then
        actual_md5=$(md5sum "$file" | cut -d' ' -f1)
    elif command -v md5 &> /dev/null; then
        actual_md5=$(md5 -q "$file")
    else
        log_warn "md5sum/md5 not found, skipping verification"
        return 0
    fi

    if [ "$actual_md5" != "$expected_md5" ]; then
        log_error "MD5 verification failed for $file"
        log_error "Expected: $expected_md5"
        log_error "Got:      $actual_md5"
        return 1
    fi

    log_info "MD5 verification passed"
    return 0
}

download_canterbury()
{
    local corpus_dir="${DATASET_DIR}/canterbury"

    if [ -d "$corpus_dir" ] && [ "$(ls -A "$corpus_dir" 2> /dev/null | wc -l)" -gt 0 ]; then
        log_info "Canterbury Corpus already exists, skipping download"
        return 0
    fi

    log_info "Downloading Canterbury Corpus..."
    mkdir -p "$corpus_dir"

    local archive="${TEMP_DIR}/cantrbry.tar.gz"
    local url="https://corpus.canterbury.ac.nz/resources/cantrbry.tar.gz"

    download_file "$url" "$archive" || {
        # Try mirror
        log_warn "Primary source failed, trying mirror..."
        url="https://www.data-compression.info/files/corpora/cantrbry.tar.gz"
        download_file "$url" "$archive" || return 1
    }

    log_info "Extracting Canterbury Corpus..."
    tar -xzf "$archive" -C "$corpus_dir"

    log_info "Canterbury Corpus downloaded successfully"
}

download_silesia()
{
    local corpus_dir="${DATASET_DIR}/silesia"

    if [ -d "$corpus_dir" ] && [ "$(ls -A "$corpus_dir" 2> /dev/null | wc -l)" -gt 0 ]; then
        log_info "Silesia Corpus already exists, skipping download"
        return 0
    fi

    log_info "Downloading Silesia Corpus..."
    mkdir -p "$corpus_dir"

    local archive="${TEMP_DIR}/silesia.zip"

    # Try multiple mirrors in order of reliability and speed
    local mirrors=(
        "http://wanos.co/assets/silesia.tar"                          # Fast mirror, tar format
        "http://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip"           # Original source
        "https://www.data-compression.info/files/corpora/silesia.zip" # Reliable mirror
        "https://mattmahoney.net/dc/silesia.zip"                      # Matt Mahoney mirror
    )

    local success=0
    for url in "${mirrors[@]}"; do
        log_info "Trying: $url"

        if [[ "$url" == *.tar ]]; then
            # Handle .tar format
            local tar_archive="${TEMP_DIR}/silesia.tar"
            if download_file "$url" "$tar_archive"; then
                log_info "Extracting Silesia Corpus (tar format)..."
                tar -xf "$tar_archive" -C "$corpus_dir" --strip-components=1 2> /dev/null || tar -xf "$tar_archive" -C "$corpus_dir"
                success=1
                break
            fi
        else
            # Handle .zip format
            if download_file "$url" "$archive"; then
                log_info "Extracting Silesia Corpus (zip format)..."
                unzip -q "$archive" -d "$corpus_dir"
                # Flatten nested directory if present
                if [ -d "$corpus_dir/silesia" ]; then
                    mv "$corpus_dir/silesia"/* "$corpus_dir/" && rmdir "$corpus_dir/silesia"
                fi
                success=1
                break
            fi
        fi

        log_warn "Mirror failed, trying next..."
    done

    if [ "$success" -eq 0 ]; then
        log_error "All mirrors failed for Silesia Corpus"
        return 1
    fi

    log_info "Silesia Corpus downloaded successfully"
}

download_enwik8()
{
    local corpus_dir="${DATASET_DIR}/enwik"

    if [ -f "$corpus_dir/enwik8.txt" ]; then
        log_info "Enwik8 already exists, skipping download"
        return 0
    fi

    log_info "Downloading Enwik8..."
    mkdir -p "$corpus_dir"

    local archive="${TEMP_DIR}/enwik8.zip"
    local url="http://mattmahoney.net/dc/enwik8.zip"
    local expected_md5="a1fa5ffddb56f4953e226637dabbb36a"

    download_file "$url" "$archive" || return 1

    log_info "Extracting Enwik8..."
    unzip -q "$archive" -d "$corpus_dir"

    # Rename to .txt extension
    if [ -f "$corpus_dir/enwik8" ]; then
        mv "$corpus_dir/enwik8" "$corpus_dir/enwik8.txt"
    fi

    # Verify MD5
    verify_md5 "$corpus_dir/enwik8.txt" "$expected_md5" || return 1

    log_info "Enwik8 downloaded successfully"
}

main()
{
    log_info "Starting dataset download..."
    log_info "Dataset directory: $DATASET_DIR"

    # Check required commands
    if ! command -v curl &> /dev/null && ! command -v wget &> /dev/null; then
        log_error "Neither curl nor wget found. Please install one of them."
        exit 1
    fi

    check_command tar
    check_command unzip

    # Create directories
    mkdir -p "$DATASET_DIR"
    mkdir -p "$TEMP_DIR"

    # Download datasets
    download_canterbury || log_error "Failed to download Canterbury Corpus"
    download_silesia || log_error "Failed to download Silesia Corpus"
    download_enwik8 || log_error "Failed to download Enwik8"

    # Cleanup
    if [ -d "$TEMP_DIR" ]; then
        log_info "Cleaning up temporary files..."
        rm -rf "$TEMP_DIR"
    fi

    log_info "Dataset download complete!"
    log_info "Dataset size: $(du -sh "$DATASET_DIR" | cut -f1)"
}

main "$@"
