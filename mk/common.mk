# Common Makefile utilities

# Detect operating system for printf compatibility
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    PRINTF = printf
else
    PRINTF = env printf
endif

# Control build verbosity
# Usage: make V=1 or make VERBOSE=1 for verbose output
ifeq ("$(origin V)", "command line")
    VERBOSE = $(V)
endif

ifndef VERBOSE
    VERBOSE = 0
endif

ifeq ($(VERBOSE),1)
    Q :=
    VECHO = @true
else
    Q := @
    VECHO = @$(PRINTF)
endif

# Color output for test results and messages
GREEN := \033[32m
YELLOW := \033[33m
RED := \033[31m
NC := \033[0m

# Helper functions for colored output
notice = $(PRINTF) "$(GREEN)$(strip $1)$(NC)\n"
warn = $(PRINTF) "$(YELLOW)$(strip $1)$(NC)\n"
error = $(PRINTF) "$(RED)$(strip $1)$(NC)\n"
