############################################################################
# Contest 2026 team 0xS3 hello app - Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = velaops_smoke
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 4096
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_023_VELAOPS_SMOKE)

MAINSRC = velaops_smoke_main.c

ifeq ($(CONFIG_LVX_USE_DEMO_CONTEST2026_023_VELAOPS),y)
PROGNAME += velaops
PRIORITY += SCHED_PRIORITY_DEFAULT
STACKSIZE += 24576
MAINSRC += velaops_main.c

# Use libssh for SSH (original implementation)
CSRCS += velaops_config.c velaops_vault.c velaops_ssh.c velaops_metrics.c \
	 velaops_dashboard.c velaops_storage.c velaops_wifi.c \
	 velaops_connpool.c velaops_server.c velaops_rules.c \
	 velaops_state.c velaops_input.c velaops_led.c
endif

include $(APPDIR)/Application.mk
