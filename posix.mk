# posix.mk -- standalone (non-NuttX) build of vaporshell for Linux,
# macOS and the BSDs. Pulled in by the Makefile only when APPDIR is
# unset, i.e. whenever NuttX's own build system isn't driving; the
# NuttX build never reads this file.
#
#   make                build ./build/vaporshell
#   make check          every differential suite, each vs its reference shell
#   make check-smoosh   run the smoosh POSIX corpus, report regressions
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
LDLIBS    += -pthread
CFLAGS    += -pthread -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra \
             -DVAPORSHELL_POSIX -Iposix -I. -MMD -MP $(EXTRA_CFLAGS)

# Every top-level .c is part of the NuttX build too (see the Makefile's
# MAINSRC/CSRCS); posix/ holds what the standalone build adds on top.
# platform_nuttx.c and dispatch.c (the tbx table) only exist for NuttX.
SRCS      := $(filter-out platform_nuttx.c dispatch.c,$(wildcard *.c)) \
             $(wildcard posix/*.c)
OBJS      := $(SRCS:%.c=$(BUILDDIR)/%.o)
BIN       := $(BUILDDIR)/vaporshell

.PHONY: all help check check-smoosh check-asan check-vaporos check-all build-vaporos coverage asan strict install uninstall clean
.DEFAULT_GOAL := all

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Everything below runs the suites in tests/ (see tests/run-suites.sh).
# ONLY=printf runs only the tests whose file name contains "printf".
check: $(BIN)
	sh tests/run-suites.sh $(BIN)

check-smoosh: $(BIN)
	sh tests/smoosh-check.sh $(BIN)

# vaporOS (NuttX): rebuild what changed and test in the simulator, or do the
# full vaporOS build with FULL=1 / NUTTX_ARGS=--full. See tests/nuttx-check.sh
# for VAPOROS_DIR, NUTTX_DIR and the other options.
NUTTX_ARGS ?=
check-vaporos:
	sh tests/nuttx-check.sh $(if $(FULL),--full) $(NUTTX_ARGS)

build-vaporos:
	sh tests/nuttx-check.sh --build-only $(if $(FULL),--full) $(NUTTX_ARGS)

# Everything, one report: Linux, Linux under ASan/UBSan, smoosh, and vaporOS.
check-all: $(BIN) asan
	sh tests/check-all.sh $(BIN) build-asan/vaporshell $(NUTTX_ARGS)

help:
	@echo "make -f posix.mk <target>"
	@echo
	@echo "  all              build build/vaporshell (default)"
	@echo "  check            all suites on the Linux build     (ONLY=name filters)"
	@echo "  check-asan       the same under ASan + UBSan"
	@echo "  check-smoosh     the smoosh POSIX corpus"
	@echo "  check-vaporos    rebuild for vaporOS/NuttX, test in the simulator"
	@echo "                   (FULL=1: full vaporOS build; NUTTX_ARGS=--jobs 4 ...)"
	@echo "  build-vaporos    only rebuild for vaporOS"
	@echo "  check-all        every check above, ONE report"
	@echo "  strict           build with fortify off and -Werror"
	@echo "  coverage         regenerate docs/*-coverage.md"
	@echo
	@echo "  reference shells: BASH_REF=/path/to/bash DASH_REF=/path/to/dash"

# Regenerates docs/bash-coverage.md and docs/posix-coverage.md from probes.
# BASH_REF=/path/to/bash and DASH_REF=/path/to/dash pick the reference shells.
coverage: $(BIN)
	python3 tests/coverage/gen-docs.py $(BIN) $(if $(BASH_REF),--bash $(BASH_REF)) $(if $(DASH_REF),--dash $(DASH_REF))

SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1

asan:
	$(MAKE) BUILDDIR=build-asan EXTRA_CFLAGS="$(SAN_FLAGS)" LDFLAGS="$(SAN_FLAGS)"

check-asan: asan
	VS_PLATFORM='Linux ASan' ASAN_OPTIONS=detect_leaks=0 sh tests/run-suites.sh build-asan/vaporshell

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
