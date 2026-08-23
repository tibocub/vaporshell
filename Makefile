include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_VAPOROS_VAPORSHELL_PROGNAME)
PRIORITY  = $(CONFIG_VAPOROS_VAPORSHELL_PRIORITY)
STACKSIZE = $(CONFIG_VAPOROS_VAPORSHELL_STACKSIZE)
MODULE    = $(CONFIG_VAPOROS_VAPORSHELL)

MAINSRC = vaporshell_main.c
CSRCS = tokenize.c dispatch.c help.c script.c builtins.c exec.c expand.c line.c subst.c pipeline.c control.c

include $(APPDIR)/external/vapor-nostdinc.mk
include $(APPDIR)/Application.mk
