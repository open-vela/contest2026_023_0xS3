/****************************************************************************
 * VelaOps 局域网 HTTP 传输适配主机测试。
 ****************************************************************************/

#include "velaops_proxy_http_transport.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define EXPECTED(value) do { if (!(value)) fail(#value, __LINE__); } while (0)

typedef struct
{
  int listener;
  const char *reply;
  bool request_valid;
  int reply_delay_ms;
} test_server_t;

typedef struct
{
  pthread_t target;
  int delay_ms;
} interrupter_t;

static void fail(const char *expression, int line)
{
  fprintf(stderr, "FAIL line %d: %s\n", line, expression);
  exit(EXIT_FAILURE);
}

static void sleep_milliseconds(int milliseconds)
{
  struct timespec duration;

  duration.tv_sec = milliseconds / 1000;
  duration.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
  nanosleep(&duration, NULL);
}

static void ignore_signal(int signal_number)
{
  (void)signal_number;
}

static void *interrupt_once(void *argument)
{
  interrupter_t *interrupter = argument;

  sleep_milliseconds(interrupter->delay_ms);
  pthread_kill(interrupter->target, SIGUSR1);
  return NULL;
}

static void *serve_once(void *argument)
{
  test_server_t *server = argument;
  char request[2048];
  size_t used = 0;
  int client = accept(server->listener, NULL, NULL);

  if (client < 0)
    {
      return NULL;
    }
  while (used + 1 < sizeof(request))
    {
      ssize_t received = recv(client, request + used,
                              sizeof(request) - used - 1, 0);
      if (received <= 0)
        {
          break;
        }
      used += (size_t)received;
      request[used] = '\0';
      if (strstr(request, "\r\n\r\n{}") != NULL)
        {
          break;
        }
    }
  server->request_valid =
      strstr(request, "POST /v1/auth/check HTTP/1.1\r\n") != NULL &&
      strstr(request, "Content-Type: application/json\r\n") != NULL &&
      strstr(request, "X-Test: signed\r\n") != NULL &&
      strstr(request, "Content-Length: 2\r\n") != NULL;
  sleep_milliseconds(server->reply_delay_ms);
  send(client, server->reply, strlen(server->reply), 0);
  close(client);
  return NULL;
}

static int start_server(test_server_t *server, pthread_t *thread, char port[6])
{
  struct sockaddr_in address;
  socklen_t address_len = sizeof(address);

  server->listener = socket(AF_INET, SOCK_STREAM, 0);
  EXPECTED(server->listener >= 0);
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  EXPECTED(bind(server->listener, (struct sockaddr *)&address,
                sizeof(address)) == 0);
  EXPECTED(getsockname(server->listener, (struct sockaddr *)&address,
                       &address_len) == 0);
  EXPECTED(listen(server->listener, 1) == 0);
  EXPECTED(snprintf(port, 6, "%u", (unsigned int)ntohs(address.sin_port)) > 0);
  return pthread_create(thread, NULL, serve_once, server);
}

static int request_once(const char *reply, bool *request_valid,
                        bool interrupt_receive)
{
  test_server_t server = {
    .reply = reply,
    .reply_delay_ms = interrupt_receive ? 120 : 0
  };
  pthread_t thread;
  pthread_t interrupter_thread;
  interrupter_t interrupter = {
    .target = pthread_self(),
    .delay_ms = 40
  };
  struct sigaction action;
  char port[6];
  velaops_proxy_http_context_t context;
  velaops_http_header_t headers[] = {
    {"Content-Type", "application/json"},
    {"X-Test", "signed"},
    {NULL, NULL}
  };
  char response[128];
  size_t response_len = 0;
  int status;

  EXPECTED(start_server(&server, &thread, port) == 0);
  if (interrupt_receive)
    {
      memset(&action, 0, sizeof(action));
      action.sa_handler = ignore_signal;
      sigemptyset(&action.sa_mask);
      EXPECTED(sigaction(SIGUSR1, &action, NULL) == 0);
      EXPECTED(pthread_create(&interrupter_thread, NULL, interrupt_once,
                              &interrupter) == 0);
    }
  context.host = "127.0.0.1";
  context.port = port;
  context.timeout_seconds = 2;
  status = velaops_proxy_http_transport(
      &context, "POST", "/v1/auth/check", headers,
      (const uint8_t *)"{}", 2, response, sizeof(response), &response_len);
  if (interrupt_receive)
    {
      pthread_join(interrupter_thread, NULL);
    }
  pthread_join(thread, NULL);
  close(server.listener);
  *request_valid = server.request_valid;
  if (status == 200)
    {
      EXPECTED(response_len == strlen("{\"ok\":true}"));
      EXPECTED(memcmp(response, "{\"ok\":true}", response_len) == 0);
    }
  return status;
}

static void test_round_trip(void)
{
  static const char reply[] =
      "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
      "Content-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}";
  bool request_valid = false;

  EXPECTED(request_once(reply, &request_valid, false) == 200);
  EXPECTED(request_valid);
}

static void test_retries_interrupted_receive(void)
{
  static const char reply[] =
      "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
      "Content-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}";
  bool request_valid = false;

  EXPECTED(request_once(reply, &request_valid, true) == 200);
  EXPECTED(request_valid);
}

static void test_rejects_truncated_response(void)
{
  static const char reply[] =
      "HTTP/1.1 200 OK\r\nContent-Length: 12\r\nConnection: close\r\n\r\n"
      "{\"ok\":true}";
  bool request_valid = false;

  EXPECTED(request_once(reply, &request_valid, false) < 0);
  EXPECTED(request_valid);
}

int main(void)
{
  test_round_trip();
  test_retries_interrupted_receive();
  test_rejects_truncated_response();
  puts("PASS: VelaOps LAN HTTP transport tests");
  return EXIT_SUCCESS;
}
