# posix.mk -- standalone (non-NuttX) build of vaporshell for Linux,
# macOS and the BSDs. Pulled in by the Makefile only when APPDIR is
# unset, i.e. whenever NuttX's own build system isn't driving; the
# NuttX build never reads this file.
#
#   make                build ./build/vaporshell
#   make check          run tests/own against bash, compare stdout+status
#   make asan           build ./build-asan/vaporshell (ASan + UBSan)
#   make check-asan     same tests, against the sanitizer build
#   make strict         warnings-as-errors with distro fortify off (see below)
#   make install        PREFIX=/usr/local (DESTDIR honored)
#   make clean

CC        ?= cc
PREFIX    ?= /usr/local
BUILDDIR  ?= build
REFSHELL  ?= bash

# Strict C99 + POSIX.1-2008 on purpose: it's what keeps this portable
# instead of accidentally glibc-only.
CFLAGS    ?= -O2
CFLAGS    += -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra \
             -DVAPORSHELL_POSIX -Iposix -I. -MMD -MP $(EXTRA_CFLAGS)

# Every top-level .c is part of the NuttX build too (see the Makefile's
# MAINSRC/CSRCS); posix/ holds what the standalone build adds on top.
SRCS      := $(wildcard *.c) $(wildcard posix/*.c)
OBJS      := $(SRCS:%.c=$(BUILDDIR)/%.o)
BIN       := $(BUILDDIR)/vaporshell

.PHONY: all check asan check-asan strict install uninstall clean
.DEFAULT_GOAL := all

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

check: $(BIN)
	sh tests/posix-check.sh $(BIN) $(REFSHELL)

SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1

asan:
	$(MAKE) BUILDDIR=build-asan EXTRA_CFLAGS="$(SAN_FLAGS)" LDFLAGS="$(SAN_FLAGS)"

check-asan: asan
	ASAN_OPTIONS=detect_leaks=0 sh tests/posix-check.sh build-asan/vaporshell $(REFSHELL)

# Ubuntu-style toolchains enable _FORTIFY_SOURCE by default, and its
# glibc wrappers quietly declare functions (realpath, ...) that strict
# -D_POSIX_C_SOURCE would otherwise hide -- so a plain build there can
# pass while Fedora fails. This is the build that sees what Fedora sees.
strict:
	$(MAKE) BUILDDIR=build-strict EXTRA_CFLAGS="-U_FORTIFY_SOURCE -Werror"

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/vaporshell

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/vaporshell

clean:
	rm -rf build build-asan build-strict

-include $(OBJS:.o=.d)
