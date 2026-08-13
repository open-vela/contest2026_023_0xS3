# VelaOps Sentinel smoke test
if(CONFIG_LVX_USE_DEMO_CONTEST2026_023_VELAOPS_SMOKE)
  nuttx_add_application(
    NAME velaops_smoke
    SRCS velaops_smoke_main.c
    STACKSIZE 4096)
endif()

if(CONFIG_LVX_USE_DEMO_CONTEST2026_023_VELAOPS)
  nuttx_add_application(
    NAME velaops
    SRCS velaops_main.c velaops_config.c velaops_vault.c velaops_ssh.c
         velaops_metrics.c velaops_dashboard.c velaops_storage.c velaops_wifi.c
         velaops_connpool.c velaops_server.c velaops_rules.c
         velaops_state.c velaops_input.c velaops_led.c
    STACKSIZE 12288)
endif()
