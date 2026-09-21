# Standalone (non-NuttX) build: whenever NuttX's build system isn't
# driving (APPDIR unset), use posix.mk instead. The NuttX build below
# is unchanged.
ifndef APPDIR
include posix.mk
else

include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_VAPOROS_VAPORSHELL_PROGNAME)
PRIORITY  = $(CONFIG_VAPOROS_VAPORSHELL_PRIORITY)
STACKSIZE = $(CONFIG_VAPOROS_VAPORSHELL_STACKSIZE)
MODULE    = $(CONFIG_VAPOROS_VAPORSHELL)

MAINSRC = vaporshell_main.c
CSRCS = alias.c arena.c arith.c array.c assign.c builtins.c builtins_extra.c brace.c builtins_io.c declare.c special.c dirs.c dispatch.c exec.c expand.c glob.c hash.c help.c inproc.c shopt.c lexer.c mode.c parser.c
CSRCS += platform_nuttx.c redir.c test.c traps.c util.c vars.c wordscan.c

include $(APPDIR)/external/vapor-nostdinc.mk
include $(APPDIR)/Application.mk

endif
