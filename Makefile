# WaveForge - IEC 62586 waveform simulator
# SPDX-License-Identifier: MIT

CC       ?= cc
AR       ?= ar
CFLAGS   ?= -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Iinclude
CPPFLAGS ?=
LDFLAGS  ?=
LDLIBS   ?= -lm

PREFIX     ?= /usr/local
BINDIR     ?= $(PREFIX)/bin
LIBDIR     ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include

BUILD    := build
OBJDIR   := $(BUILD)/obj
LIB      := $(BUILD)/libwaveforge.a
WG       := $(BUILD)/wg
SELFTEST := $(BUILD)/selftest

SRCS := $(sort $(wildcard src/*.c))
OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all lib wg selftest test clean install

all: lib wg selftest

lib: $(LIB)

wg: $(WG)

selftest: $(SELFTEST)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

$(OBJDIR):
	mkdir -p $@

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(WG): apps/wg_main.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)

$(SELFTEST): tests/wg_selftest.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)

test: $(SELFTEST)
	./$(SELFTEST)

install: all
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(INCLUDEDIR)
	install -m 644 $(LIB) $(DESTDIR)$(LIBDIR)/
	install -m 644 include/*.h $(DESTDIR)$(INCLUDEDIR)/
	install -m 755 $(WG) $(DESTDIR)$(BINDIR)/wg

clean:
	$(RM) -r $(BUILD)
