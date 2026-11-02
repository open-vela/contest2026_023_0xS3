############################################################################
# Contest 2026 team 023 VelaOps device client - Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = velaops
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 32768
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_023_VELAOPS)

CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/include
CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/src
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/src
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/netutils/cjson/cJSON
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/mbedtls/include

MAINSRC = velaops_main.c
CSRCS = src/velaops_protocol.c
CSRCS += src/velaops_autoconfig.c
CSRCS += src/velaops_device_config.c
CSRCS += src/velaops_health.c
CSRCS += src/velaops_incident.c
CSRCS += src/velaops_memory_result.c
CSRCS += src/velaops_resource_result.c
CSRCS += src/velaops_local_diagnosis.c
CSRCS += src/velaops_resource_incident.c
CSRCS += src/velaops_agent_tools.c
CSRCS += src/velaops_agent_monitor.c
CSRCS += src/velaops_agent_display.c
CSRCS += src/velaops_approval.c
CSRCS += src/velaops_button_approval.c
CSRCS += src/velaops_guarded_repair.c
CSRCS += src/velaops_crypto_mbedtls.c
CSRCS += src/velaops_proxy_client.c
CSRCS += src/velaops_proxy_http_transport.c
CSRCS += src/velaops_dashboard.c
CSRCS += src/velaops_display.c
CSRCS += src/velaops_screen.c
CSRCS += src/velaops_serial_tunnel.c

include $(APPDIR)/Application.mk
