include mk/common.mk

.PHONY: all tools tests clean check dataset distclean

all: tools tests

# Include fuzz.mk after 'all' to keep 'all' as the default target
include mk/fuzz.mk

tools:
	$(VECHO) "Building tools...\n"
	$(Q)$(MAKE) -C tools

tests:
	$(VECHO) "Building tests...\n"
	$(Q)$(MAKE) -C tests

dataset:
	$(VECHO) "Ensuring test datasets are available...\n"
	$(Q)./scripts/download-dataset.sh

check: tools tests dataset
	$(Q)$(MAKE) -C tests check

clean:
	$(Q)$(MAKE) -C tools clean
	$(Q)$(MAKE) -C tests clean

distclean: clean
	$(VECHO) "Removing downloaded datasets...\n"
	$(Q)rm -rf tests/dataset
