/****************************************************************************
 * 主机侧单测：TF 卡凭据文件解析。
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "velaops_autoconfig.h"

static int failures;

#define EXPECT(condition)                                          \
  do                                                               \
    {                                                              \
      if (!(condition))                                            \
        {                                                          \
          printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
          failures++;                                              \
        }                                                          \
    }                                                              \
  while (0)

static void test_full_credentials(void)
{
  velaops_credentials_t cred;
  const char *content =
      "# VelaOps credentials\n"
      "\n"
      "WIFI_SSID=TP-GM2.4\n"
      "WIFI_PASSWORD=secret pass 123\n"
      "MIMO_API_KEY=sk-test-key-42\n"
      "DEMO_HOST=192.168.31.139\n"
      "DEMO_PORT=28790\n";

  EXPECT(velaops_autoconfig_parse_credentials(content, &cred) == 0);
  EXPECT(strcmp(cred.wifi_ssid, "TP-GM2.4") == 0);
  EXPECT(strcmp(cred.wifi_password, "secret pass 123") == 0);
  EXPECT(cred.has_api_key == 1);
  EXPECT(strcmp(cred.api_key, "sk-test-key-42") == 0);
  EXPECT(cred.has_demo_host == 1);
  EXPECT(strcmp(cred.demo_host, "192.168.31.139") == 0);
  EXPECT(strcmp(cred.demo_port, "28790") == 0);
}

static void test_trim_and_crlf(void)
{
  velaops_credentials_t cred;
  const char *content =
      "  WIFI_SSID =  TP-GM2.4  \r\n"
      "WIFI_PASSWORD=p@ss\r\n"
      "UNKNOWN_KEY=ignored\n";

  EXPECT(velaops_autoconfig_parse_credentials(content, &cred) == 0);
  EXPECT(strcmp(cred.wifi_ssid, "TP-GM2.4") == 0);
  EXPECT(strcmp(cred.wifi_password, "p@ss") == 0);
  EXPECT(cred.has_api_key == 0);
}

static void test_missing_required(void)
{
  velaops_credentials_t cred;

  EXPECT(velaops_autoconfig_parse_credentials("WIFI_SSID=only\n", &cred)
         == -1);
  EXPECT(velaops_autoconfig_parse_credentials("", &cred) == -1);
  EXPECT(velaops_autoconfig_parse_credentials(NULL, &cred) == -1);
  EXPECT(velaops_autoconfig_parse_credentials("WIFI_SSID=a\n", NULL) == -1);
}

static void test_prefix_key_not_confused(void)
{
  velaops_credentials_t cred;
  const char *content =
      "WIFI_SSID_EXTRA=nope\n"
      "WIFI_SSID=real-ssid\n"
      "WIFI_PASSWORD=real-pass\n";

  EXPECT(velaops_autoconfig_parse_credentials(content, &cred) == 0);
  EXPECT(strcmp(cred.wifi_ssid, "real-ssid") == 0);
}

static void test_no_trailing_newline(void)
{
  velaops_credentials_t cred;
  const char *content = "WIFI_SSID=abc\nWIFI_PASSWORD=xyz";

  EXPECT(velaops_autoconfig_parse_credentials(content, &cred) == 0);
  EXPECT(strcmp(cred.wifi_password, "xyz") == 0);
}

int main(void)
{
  test_full_credentials();
  test_trim_and_crlf();
  test_missing_required();
  test_prefix_key_not_confused();
  test_no_trailing_newline();

  if (failures != 0)
    {
      printf("test_autoconfig: %d FAILURES\n", failures);
      return 1;
    }

  printf("test_autoconfig: all pass\n");
  return 0;
}
