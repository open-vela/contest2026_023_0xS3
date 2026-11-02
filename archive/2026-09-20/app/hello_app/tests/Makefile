CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
CPPFLAGS += -I../include -I../src
CPPFLAGS += -I../../../../packages/ai_agent/include
CPPFLAGS += -I../../../../packages/ai_agent/src
LDLIBS += -lcrypto

PROTOCOL_SOURCES = ../src/velaops_protocol.c ../src/velaops_crypto_openssl.c
CJSON_SOURCES = ../../../../apps/netutils/cjson/cJSON/cJSON.c
TARGETS = test_protocol test_proxy_client test_tls_transport test_http_transport \
	test_device_config \
	test_incident test_health test_memory_result test_resource_result \
	test_dashboard test_agent_tools test_agent_monitor test_approval \
	test_guarded_repair test_local_diagnosis \
	test_resource_incident test_autoconfig

.PHONY: all check clean

all: $(TARGETS)

test_protocol: test_protocol.c $(PROTOCOL_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

test_proxy_client: test_proxy_client.c ../src/velaops_proxy_client.c \
		$(PROTOCOL_SOURCES) $(CJSON_SOURCES)
	$(CC) $(CPPFLAGS) -I../../../../apps/netutils/cjson/cJSON \
		$(CFLAGS) $^ $(LDLIBS) -o $@

test_tls_transport: test_tls_transport.c fake_vela_tls.c \
		../src/velaops_proxy_tls_transport.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

test_http_transport: test_http_transport.c \
		../src/velaops_proxy_http_transport.c
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) $^ -pthread -o $@

test_device_config: test_device_config.c ../src/velaops_device_config.c \
		$(CJSON_SOURCES)
	$(CC) $(CPPFLAGS) -I../../../../apps/netutils/cjson/cJSON \
		$(CFLAGS) $^ -o $@

test_incident: test_incident.c ../src/velaops_incident.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

test_health: test_health.c ../src/velaops_health.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -lm -o $@

test_memory_result: test_memory_result.c ../src/velaops_memory_result.c \
		$(CJSON_SOURCES)
	$(CC) $(CPPFLAGS) -I../../../../apps/netutils/cjson/cJSON \
		$(CFLAGS) $^ -lm -o $@

test_resource_result: test_resource_result.c ../src/velaops_resource_result.c \
		$(CJSON_SOURCES)
	$(CC) $(CPPFLAGS) -I../../../../apps/netutils/cjson/cJSON \
		$(CFLAGS) $^ -lm -o $@

test_dashboard: test_dashboard.c ../src/velaops_dashboard.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

test_agent_tools: test_agent_tools.c ../src/velaops_agent_tools.c \
		$(CJSON_SOURCES)
	$(CC) $(CPPFLAGS) -I../../../../apps/netutils/cjson/cJSON \
		-DOK=0 -DERROR=-1 $(CFLAGS) $^ -o $@

test_agent_monitor: test_agent_monitor.c ../src/velaops_agent_monitor.c \
		../src/velaops_resource_incident.c ../src/velaops_incident.c \
		../src/velaops_local_diagnosis.c ../src/velaops_resource_result.c \
		$(CJSON_SOURCES)
	$(CC) $(CPPFLAGS) -I../../../../apps/netutils/cjson/cJSON \
		-D_POSIX_C_SOURCE=200809L -DOK=0 -DERROR=-1 $(CFLAGS) $^ -pthread -lm -o $@

test_approval: test_approval.c ../src/velaops_approval.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

test_guarded_repair: test_guarded_repair.c \
		../src/velaops_guarded_repair.c ../src/velaops_resource_result.c \
		$(CJSON_SOURCES)
	$(CC) $(CPPFLAGS) -I../../../../apps/netutils/cjson/cJSON \
		$(CFLAGS) $^ -lm -o $@

test_local_diagnosis: test_local_diagnosis.c \
		../src/velaops_local_diagnosis.c ../src/velaops_resource_result.c \
		$(CJSON_SOURCES)
	$(CC) $(CPPFLAGS) -I../../../../apps/netutils/cjson/cJSON \
		$(CFLAGS) $^ -lm -o $@

test_resource_incident: test_resource_incident.c \
		../src/velaops_resource_incident.c ../src/velaops_incident.c \
		../src/velaops_local_diagnosis.c ../src/velaops_resource_result.c \
		$(CJSON_SOURCES)
	$(CC) $(CPPFLAGS) -I../../../../apps/netutils/cjson/cJSON \
		$(CFLAGS) $^ -lm -o $@

test_autoconfig: test_autoconfig.c ../src/velaops_autoconfig.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

check: $(TARGETS)
	./test_protocol
	./test_proxy_client
	./test_tls_transport
	./test_http_transport
	./test_device_config
	./test_incident
	./test_health
	./test_memory_result
	./test_resource_result
	./test_dashboard
	./test_agent_tools
	./test_agent_monitor
	./test_approval
	./test_guarded_repair
	./test_local_diagnosis
	./test_resource_incident
	./test_autoconfig
	python3 ./test_skill.py

clean:
	$(RM) $(TARGETS)
