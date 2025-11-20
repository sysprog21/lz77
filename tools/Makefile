include ../mk/common.mk

CFLAGS ?= -Wall -O2
CPPFLAGS ?=
LDFLAGS ?=
CFLAGS += -MMD -MP -I..
TARGETS := mzip munzip
OBJS := mzip.o
DEPS := $(OBJS:.o=.d)

all: $(TARGETS)

mzip: mzip.o
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) $(LDFLAGS) -o $@ $<

munzip: mzip
	$(VECHO) "  LN\t$@\n"
	$(Q)ln -sf mzip munzip

mzip.o: mzip.c ../lz77.h
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean :
	$(VECHO) "  CLEAN\ttools\n"
	$(Q)$(RM) $(TARGETS) $(OBJS) $(DEPS)

-include $(DEPS)
