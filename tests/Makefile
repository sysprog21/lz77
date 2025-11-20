include ../mk/common.mk

CFLAGS ?= -Wall -O2
CPPFLAGS ?=
LDFLAGS ?=
CFLAGS += -MMD -MP -I..
TARGETS := driver api
OBJS := driver.o api.o
DEPS := $(OBJS:.o=.d)

all: $(TARGETS)

driver: driver.o
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) $(LDFLAGS) -o $@ $<

api: api.o
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) $(LDFLAGS) -o $@ $<

driver.o: driver.c ../lz77.h
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

api.o: api.c ../lz77.h
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

check: api driver
	$(VECHO) "Running API tests...\n"
	$(Q)./api
	$(VECHO) "\n"
	$(VECHO) "Running driver tests...\n"
	$(Q)./driver
	$(VECHO) "\n"
	$(VECHO) "Running mzip/munzip tests...\n"
	$(Q)./test-mzip.sh
	$(VECHO) "\n"
	$(VECHO) "Running path sanitization tests...\n"
	$(Q)./test-path-sanitization.sh

clean :
	$(VECHO) "  CLEAN\ttests\n"
	$(Q)$(RM) $(TARGETS) $(OBJS) $(DEPS)

-include $(DEPS)
