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
#   make menuconfig     the configuration menu (also: tiny_defconfig, tinyest_defconfig,
#                       full_defconfig, size-report; docs/config.md)
#   make clean

CC        ?= cc
PREFIX    ?= /usr/local
BUILDDIR  ?= build
REFSHELL  ?= bash

# The configuration (make menuconfig, docs/config.md): .config if there is one, else the
# defaults of config/Kconfig, which are the full shell. NOCONFIG=1 ignores .config: the
# sanitizer, fuzz and strict builds use it so they always test everything.
NOCONFIG  ?= 0
ifeq ($(NOCONFIG),0)
-include .config
HAVE_CFG  := $(wildcard .config)
endif
# $(call cfg,NAME,default): "y" if CONFIG_VASH_NAME is on (or, with no .config, if default is y)
cfg        = $(if $(HAVE_CFG),$(filter y,$(CONFIG_VASH_$(1))),$(2))
OPT       := $(if $(call cfg,OPT_SIZE,),-Os,-O2)

# Strict C99 + POSIX.1-2008 on purpose: it's what keeps this portable
# instead of accidentally glibc-only.
CFLAGS    ?= $(OPT)
LDLIBS    += -pthread
CFLAGS    += -pthread -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra \
             -DVAPORSHELL_POSIX -Iposix -I. -MMD -MP $(EXTRA_CFLAGS)

# Hardening, on by default (HARDEN=0 drops it; docs/security.md). Each flag is
# kept only if this compiler/linker accepts it, so the build stays portable.
HARDEN ?= $(if $(call cfg,HARDEN,y),1,0)
cc_ok = $(shell $(CC) $(1) -x c -c /dev/null -o /dev/null 2>/dev/null && echo $(1))
ld_ok = $(shell $(CC) $(1) -x c /dev/null -o /dev/null 2>/dev/null && echo $(1))
ifeq ($(HARDEN),1)
CFLAGS    += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 $(call cc_ok,-fstack-protector-strong) \
             $(call cc_ok,-fstack-clash-protection) $(call cc_ok,-fcf-protection) \
             $(call cc_ok,-fno-strict-overflow) $(call cc_ok,-fPIE)
LDFLAGS   += $(call ld_ok,-pie) $(call ld_ok,-Wl$(comma)-z$(comma)relro$(comma)-z$(comma)now) \
             $(call ld_ok,-Wl$(comma)-z$(comma)noexecstack)
endif
comma := ,

# The rest of the "Build" menu.
ifneq ($(call cfg,GC_SECTIONS,y),)
CFLAGS    += -ffunction-sections -fdata-sections
LDFLAGS   += $(call ld_ok,-Wl$(comma)--gc-sections) $(call ld_ok,-Wl$(comma)-dead_strip)
endif
ifneq ($(call cfg,LTO,),)
LTO       := $(or $(call cc_ok,-flto=auto),$(call cc_ok,-flto))
CFLAGS    += $(LTO)
LDFLAGS   += $(LTO) $(OPT)
endif
ifneq ($(call cfg,STRIP,),)
LDFLAGS   += $(call ld_ok,-s)
endif
ifneq ($(call cfg,DEBUG,),)
CFLAGS    += -g
endif

# The "Language" and "Interactive shell" menus: a switch that is off becomes -DVASH_NO_xxx
# (the headers then have inline stubs) and the module's source file is not compiled.
NO_SRCS    :=
ifneq ($(call cfg,MODES_POSIX_ONLY,),)
CFLAGS    += -DVASH_POSIX_ONLY
endif
ifeq ($(call cfg,LINE_EDITOR,y),)
CFLAGS    += -DVASH_NO_EDITOR
NO_SRCS   += ledit.c bind.c
endif
ifeq ($(call cfg,HISTORY,y),)
CFLAGS    += -DVASH_NO_HISTORY
NO_SRCS   += history.c
endif
ifeq ($(call cfg,COMPLETION,y),)
CFLAGS    += -DVASH_NO_COMPLETION
NO_SRCS   += complete.c compgen.c compfish.c
endif
ifeq ($(call cfg,COMPLETION_PROG,y),)
CFLAGS    += -DVASH_NO_PROG_COMPLETION
endif
ifeq ($(call cfg,COMPLETION_FISH,y),)
CFLAGS    += -DVASH_NO_FISH
NO_SRCS   += compfish.c
endif
ifeq ($(call cfg,HIGHLIGHT,y),)
CFLAGS    += -DVASH_NO_HIGHLIGHT
endif
ifeq ($(call cfg,AUTOSUGGEST,y),)
CFLAGS    += -DVASH_NO_AUTOSUGGEST
endif
ifeq ($(call cfg,TITLE,y),)
CFLAGS    += -DVASH_NO_TITLE
endif

# Every top-level .c is part of the NuttX build too (see the Makefile's
# MAINSRC/CSRCS); posix/ holds what the standalone build adds on top.
# platform_nuttx.c and dispatch.c (the tbx table) only exist for NuttX.
SRCS      := $(filter-out platform_nuttx.c dispatch.c $(NO_SRCS),$(wildcard *.c)) \
             $(wildcard posix/*.c)
OBJS      := $(SRCS:%.c=$(BUILDDIR)/%.o)
BIN       := $(BUILDDIR)/vaporshell

.PHONY: menuconfig guiconfig defconfig full_defconfig tiny_defconfig tinyest_defconfig olddefconfig savedefconfig size-report distclean fuzz all help small bench check check-pty check-smoosh check-asan check-vaporos check-all build-vaporos coverage asan strict install uninstall clean
.DEFAULT_GOAL := all

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILDDIR)/%.o: %.c $(HAVE_CFG)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Everything below runs the suites in tests/ (see tests/run-suites.sh).
# ONLY=printf runs only the tests whose file name contains "printf".
check: $(BIN)
	sh tests/run-suites.sh $(BIN)

# Tab latency: make -f posix.mk bench [LINE='ls /usr/bi']
BENCH_OBJS := $(filter-out $(BUILDDIR)/vaporshell_main.o,$(OBJS))
$(BUILDDIR)/complete_bench: tests/bench/complete_bench.c $(BENCH_OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(BENCH_OBJS) $(LDLIBS)

# The size the device build has (vaporOS compiles for size): build-small/vaporshell.
small:
	$(MAKE) -f posix.mk NOCONFIG=1 BUILDDIR=build-small EXTRA_CFLAGS=-Os

bench: $(BUILDDIR)/complete_bench
	@$(BUILDDIR)/complete_bench "$(or $(LINE),g)"

check-pty: $(BIN)
	python3 -I tests/pty/run.py $(BIN) $(ONLY)

check-corpus: $(BIN)
	tests/corpus/run.sh $(BIN) $(ONLY)

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
	@echo "  small            build-small/vaporshell, built with -Os like the device build"
	@echo "  check-pty        the line editor, driven through a pseudo-terminal"
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
	$(MAKE) NOCONFIG=1 BUILDDIR=build-asan EXTRA_CFLAGS="$(SAN_FLAGS)" LDFLAGS="$(SAN_FLAGS)"

# Fuzzing (needs clang): make fuzz builds build-fuzz/fuzz, then
#   build-fuzz/fuzz fuzz/corpus -max_total_time=60     (see docs/security.md)
FUZZ_FLAGS := -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -g -O1
FUZZ_OBJS  := $(filter-out build-fuzz/vaporshell_main.o,$(OBJS:$(BUILDDIR)/%=build-fuzz/%))

fuzz:
	$(MAKE) -f posix.mk NOCONFIG=1 CC=clang BUILDDIR=build-fuzz EXTRA_CFLAGS="$(FUZZ_FLAGS)" $(FUZZ_OBJS)
	clang $(FUZZ_FLAGS) -fsanitize=fuzzer -pthread -std=c99 -D_POSIX_C_SOURCE=200809L -DVAPORSHELL_POSIX -Iposix -I. \
	  -o build-fuzz/fuzz fuzz/fuzz.c $(FUZZ_OBJS) -pthread

check-asan: asan
	VASH_NOCONFIG=1 VASH_PLATFORM='Linux ASan' ASAN_OPTIONS=detect_leaks=0 sh tests/run-suites.sh build-asan/vaporshell

# Ubuntu-style toolchains enable _FORTIFY_SOURCE by default, and its
# glibc wrappers quietly declare functions (realpath, ...) that strict
# -D_POSIX_C_SOURCE would otherwise hide -- so a plain build there can
# pass while Fedora fails. This is the build that sees what Fedora sees.
strict:
	$(MAKE) NOCONFIG=1 BUILDDIR=build-strict EXTRA_CFLAGS="-U_FORTIFY_SOURCE -Werror"

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/vaporshell

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/vaporshell

clean:
	rm -rf build build-asan build-strict build-fuzz build-small

distclean: clean
	rm -f .config .config.old defconfig

# ---- Configuration (docs/config.md) ----------------------------------------------------
# Kconfiglib is vendored in tools/kconfig: nothing to install but python3.

KCONF := python3 tools/kconfig

menuconfig:
	$(KCONF)/menuconfig.py config/Kconfig

guiconfig:
	$(KCONF)/guiconfig.py config/Kconfig

defconfig: full_defconfig

full_defconfig tiny_defconfig tinyest_defconfig:
	$(KCONF)/defconfig.py --kconfig config/Kconfig config/$@

olddefconfig:
	$(KCONF)/olddefconfig.py config/Kconfig

# The smallest defconfig that reproduces .config, written to ./defconfig
savedefconfig:
	$(KCONF)/savedefconfig.py --kconfig config/Kconfig --out defconfig

# What this configuration costs, object by object (meaningless per object with LTO).
size-report: $(BIN)
	@echo "configuration:"; if [ -f .config ]; then grep -E '^CONFIG_' .config | sed 's/^/  /'; else echo "  none (.config absent: the defaults, i.e. full)"; fi
	@echo; echo "bytes (text+data) per object, largest first:"
	@size $(OBJS) | awk 'NR>1 {printf "%8d  %s\n", $$1+$$2, $$6}' | sort -rn | head -25
	@echo; size $(BIN); ls -l $(BIN) | awk '{print $$5 " bytes on disk"}'

-include $(OBJS:.o=.d)
