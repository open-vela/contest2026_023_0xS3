# Contest 2026 team 023 VelaOps device client
if(CONFIG_LVX_USE_DEMO_CONTEST2026_023_VELAOPS)
  set(VELAOPS_SRCS
    velaops_main.c
    src/velaops_device_config.c
    src/velaops_health.c
    src/velaops_incident.c
    src/velaops_memory_result.c
    src/velaops_resource_result.c
    src/velaops_local_diagnosis.c
    src/velaops_resource_incident.c
    src/velaops_agent_tools.c
    src/velaops_agent_monitor.c
    src/velaops_agent_display.c
    src/velaops_approval.c
    src/velaops_button_approval.c
    src/velaops_guarded_repair.c
    src/velaops_protocol.c
    src/velaops_crypto_mbedtls.c
    src/velaops_proxy_client.c
    src/velaops_proxy_http_transport.c
    src/velaops_dashboard.c
    src/velaops_display.c
    src/velaops_serial_tunnel.c)
  set(VELAOPS_INCDIRS
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${NUTTX_APPS_DIR}/packages/ai_agent/include
    ${NUTTX_APPS_DIR}/packages/ai_agent/src
    ${NUTTX_APPS_DIR}/netutils/cjson/cJSON
    ${NUTTX_APPS_DIR}/crypto/mbedtls/mbedtls/include)

  nuttx_add_application(
    NAME velaops
    SRCS ${VELAOPS_SRCS}
    INCLUDE_DIRECTORIES ${VELAOPS_INCDIRS}
    STACKSIZE 32768)
endif()
