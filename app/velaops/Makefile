############################################################################
# apps/examples/velaops/Makefile
#
# SPDX-License-Identifier: Apache-2.0
#
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_EXAMPLES_VELAOPS_PROGNAME)
PRIORITY  = $(CONFIG_EXAMPLES_VELAOPS_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_VELAOPS_STACKSIZE)
MODULE    = $(CONFIG_EXAMPLES_VELAOPS)

CFLAGS += ${INCDIR_PREFIX}$(CURDIR)

MAINSRC = velaops_main.c
CSRCS   = velaops_ui.c velaops_ssh.c velaops_prov.c velaops_llm.c \
          velaops_conf.c velaops_health.c

include $(APPDIR)/Application.mk
