# Fuzzing rules for LZ77
# Requires: LLVM clang with libFuzzer support (not Apple clang)
#
# Usage: make fuzz  (builds and runs fuzzer with progress bar, Ctrl+C to stop)
#
# Linux: sudo apt-get install clang
# macOS: brew install llvm (see tests/FUZZING.md for details)

FUZZ_DIR = tests
FUZZ_TARGET = $(FUZZ_DIR)/fuzz
FUZZ_CORPUS = $(FUZZ_DIR)/corpus
FUZZ_TIME = 300

# Stricter sanitizer options
export ASAN_OPTIONS=detect_container_overflow=1:strict_string_checks=1:abort_on_error=1:detect_stack_use_after_return=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

.PHONY: fuzz fuzz-verbose

fuzz: tools
	@# Detect LLVM clang (Apple clang lacks libFuzzer)
	@LLVM_CC=""; \
	for cc in /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang clang; do \
		if [ -x "$$cc" ] 2>/dev/null || command -v "$$cc" >/dev/null 2>&1; then \
			ver=$$($$cc --version 2>/dev/null | head -1); \
			case "$$ver" in \
				*Apple*) continue ;; \
				*) LLVM_CC="$$cc"; break ;; \
			esac; \
		fi; \
	done; \
	if [ -z "$$LLVM_CC" ]; then \
		printf "$(RED)Error: LLVM clang not found$(NC)\n"; \
		echo ""; \
		echo "libFuzzer requires LLVM clang (Apple clang lacks libFuzzer)."; \
		echo ""; \
		echo "Linux:"; \
		echo "  sudo apt-get install clang"; \
		echo ""; \
		echo "macOS (may have ABI issues with recent LLVM):"; \
		echo "  brew install llvm"; \
		echo "  export PATH=\"/opt/homebrew/opt/llvm/bin:\$$PATH\""; \
		echo ""; \
		echo "For reliable fuzzing, use Linux or CI."; \
		exit 1; \
	fi; \
	mkdir -p $(FUZZ_CORPUS); \
	if [ -f tools/mzip ]; then \
		rm -f $(FUZZ_CORPUS)/seed*.mz; \
		echo "test" | tools/mzip /dev/stdin $(FUZZ_CORPUS)/seed1.mz 2>/dev/null || true; \
		echo "AAAAAAAAAA" | tools/mzip /dev/stdin $(FUZZ_CORPUS)/seed2.mz 2>/dev/null || true; \
		echo "ABCABCABCABC" | tools/mzip /dev/stdin $(FUZZ_CORPUS)/seed3.mz 2>/dev/null || true; \
		head -c 256 /dev/zero | tools/mzip /dev/stdin $(FUZZ_CORPUS)/seed4.mz 2>/dev/null || true; \
		head -c 1024 /dev/urandom 2>/dev/null | tools/mzip /dev/stdin $(FUZZ_CORPUS)/seed5.mz 2>/dev/null || true; \
	fi; \
	printf '\x00' > $(FUZZ_CORPUS)/empty.bin; \
	printf '\x01A' > $(FUZZ_CORPUS)/literal.bin; \
	printf '\x00A' > $(FUZZ_CORPUS)/literal1.bin; \
	printf '\x03ABCD' > $(FUZZ_CORPUS)/literal4.bin; \
	printf '\x1fAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA' > $(FUZZ_CORPUS)/literal32.bin; \
	printf '\xe0' > $(FUZZ_CORPUS)/trunc_ext.bin; \
	printf '\xe0\xff' > $(FUZZ_CORPUS)/trunc_ext2.bin; \
	printf '\x20\x00' > $(FUZZ_CORPUS)/dist_zero.bin; \
	printf '\x40\x01' > $(FUZZ_CORPUS)/match_d1.bin; \
	printf '\xff\xff\xff' > $(FUZZ_CORPUS)/invalid.bin; \
	printf '\x00A\x20\x01' > $(FUZZ_CORPUS)/lit_match.bin; \
	printf '\xc0\x01' > $(FUZZ_CORPUS)/len6_d1.bin; \
	printf '\xc0\xff' > $(FUZZ_CORPUS)/len6_d255.bin; \
	printf '\x40\x01' > $(FUZZ_CORPUS)/match_first.bin; \
	printf '\xe0\x00\x01' > $(FUZZ_CORPUS)/ext_match.bin; \
	printf '\x1fAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\x3f\xff' > $(FUZZ_CORPUS)/max_dist.bin; \
	printf '\x00A\x40\x01' > $(FUZZ_CORPUS)/lazy_trigger.bin; \
	printf '\x20\x01\x20\x01\x20\x01' > $(FUZZ_CORPUS)/rle_chain.bin; \
	printf '\xe0\xfa\x01' > $(FUZZ_CORPUS)/near_maxlen.bin; \
	LLVM_DIR=$$(dirname $$(dirname $$LLVM_CC)); \
	if [ -d "$$LLVM_DIR/lib/c++" ]; then \
		LIBCXX_FLAGS="-stdlib=libc++ -L$$LLVM_DIR/lib/c++ -Wl,-rpath,$$LLVM_DIR/lib/c++"; \
	else \
		LIBCXX_FLAGS=""; \
	fi; \
	$$LLVM_CC -g -O1 -I. -fsanitize=fuzzer,address,undefined \
		$$LIBCXX_FLAGS $(FUZZ_DIR)/fuzz.c -o $(FUZZ_TARGET) 2>/dev/null && \
	scripts/fuzz-progress.sh ./$(FUZZ_TARGET) $(FUZZ_TIME) \
		-max_len=65536 -timeout=10 \
		-max_total_time=$(FUZZ_TIME) \
		-use_value_profile=1 \
		-print_final_stats=1 \
		-dict=$(FUZZ_DIR)/fuzz.dict \
		$(FUZZ_CORPUS)/

# Verbose mode (original libFuzzer output)
fuzz-verbose: tools
	@LLVM_CC=""; \
	for cc in /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang clang; do \
		if [ -x "$$cc" ] 2>/dev/null || command -v "$$cc" >/dev/null 2>&1; then \
			ver=$$($$cc --version 2>/dev/null | head -1); \
			case "$$ver" in \
				*Apple*) continue ;; \
				*) LLVM_CC="$$cc"; break ;; \
			esac; \
		fi; \
	done; \
	if [ -z "$$LLVM_CC" ]; then \
		printf "$(RED)Error: LLVM clang not found$(NC)\n"; \
		exit 1; \
	fi; \
	echo "Using: $$LLVM_CC"; \
	mkdir -p $(FUZZ_CORPUS); \
	LLVM_DIR=$$(dirname $$(dirname $$LLVM_CC)); \
	if [ -d "$$LLVM_DIR/lib/c++" ]; then \
		LIBCXX_FLAGS="-stdlib=libc++ -L$$LLVM_DIR/lib/c++ -Wl,-rpath,$$LLVM_DIR/lib/c++"; \
	else \
		LIBCXX_FLAGS=""; \
	fi; \
	$$LLVM_CC -g -O1 -I. -fsanitize=fuzzer,address,undefined \
		$$LIBCXX_FLAGS $(FUZZ_DIR)/fuzz.c -o $(FUZZ_TARGET) && \
	echo "Running fuzzer (verbose mode)..." && \
	./$(FUZZ_TARGET) -max_len=65536 -timeout=10 \
		-max_total_time=$(FUZZ_TIME) \
		-use_value_profile=1 \
		-print_final_stats=1 \
		-dict=$(FUZZ_DIR)/fuzz.dict \
		$(FUZZ_CORPUS)/
