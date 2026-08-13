/****************************************************************************
 * VelaOps Sentinel - 持久存储管理
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>

#include "velaops.h"

#define VELAOPS_DATA_DIR      "/tmp/velaops"
#define VELAOPS_VAULT_PATH    VELAOPS_DATA_DIR "/credentials.vlt"
#define VELAOPS_WIFI_PATH     VELAOPS_DATA_DIR "/wifi.conf"
#define VELAOPS_CONFIG_PATH   VELAOPS_DATA_DIR "/config.json"

/* 检查目录是否存在，不存在则创建 */
static int ensure_directory(const char *path)
{
  struct stat st;
  if (stat(path, &st) == 0)
    {
      return 0; /* 目录已存在 */
    }

  if (mkdir(path, 0700) < 0 && errno != EEXIST)
    {
      printf("Failed to create directory %s: %d\n", path, errno);
      return -1;
    }

  return 0;
}

/* 初始化持久存储 */
int velaops_storage_init(void)
{
  printf("Storage: Using %s\n", VELAOPS_DATA_DIR);

  /* 创建 VelaOps 数据目录 */
  if (ensure_directory(VELAOPS_DATA_DIR) < 0)
    {
      return -1;
    }

  printf("Storage: Initialized at %s\n", VELAOPS_DATA_DIR);
  return 0;
}

/* 检查文件是否存在 */
bool velaops_storage_file_exists(const char *path)
{
  struct stat st;
  return stat(path, &st) == 0;
}

/* 获取 Vault 文件路径 */
const char *velaops_storage_get_vault_path(void)
{
  return VELAOPS_VAULT_PATH;
}

/* 获取 Wi-Fi 配置路径 */
const char *velaops_storage_get_wifi_path(void)
{
  return VELAOPS_WIFI_PATH;
}

/* 获取配置文件路径 */
const char *velaops_storage_get_config_path(void)
{
  return VELAOPS_CONFIG_PATH;
}

/* 检查 Vault 文件是否存在 */
bool velaops_storage_vault_exists(void)
{
  return velaops_storage_file_exists(VELAOPS_VAULT_PATH);
}

/* 保存 Wi-Fi 配置 */
int velaops_storage_save_wifi_config(const char *ssid, const char *password)
{
  int fd;
  char buffer[256];
  int len;

  fd = open(VELAOPS_WIFI_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0)
    {
      printf("Storage: Failed to open wifi config: %d\n", errno);
      return -1;
    }

  len = snprintf(buffer, sizeof(buffer), "%s\n%s\n", ssid, password);
  if (write(fd, buffer, len) != len)
    {
      printf("Storage: Failed to write wifi config: %d\n", errno);
      close(fd);
      return -1;
    }

  close(fd);
  printf("Storage: Wi-Fi config saved\n");
  return 0;
}

/* 读取 Wi-Fi 配置 */
int velaops_storage_load_wifi_config(char *ssid, unsigned int ssid_size,
                                     char *password, unsigned int password_size)
{
  int fd;
  char buffer[256];
  int n;
  char *line1;
  char *line2;

  fd = open(VELAOPS_WIFI_PATH, O_RDONLY);
  if (fd < 0)
    {
      return -1; /* 文件不存在 */
    }

  n = read(fd, buffer, sizeof(buffer) - 1);
  close(fd);

  if (n <= 0)
    {
      return -1;
    }

  buffer[n] = '\0';

  /* 解析第一行：SSID */
  line1 = strtok(buffer, "\n");
  if (line1 == NULL)
    {
      return -1;
    }

  /* 解析第二行：密码 */
  line2 = strtok(NULL, "\n");
  if (line2 == NULL)
    {
      return -1;
    }

  strncpy(ssid, line1, ssid_size - 1);
  ssid[ssid_size - 1] = '\0';
  strncpy(password, line2, password_size - 1);
  password[password_size - 1] = '\0';

  printf("Storage: Wi-Fi config loaded\n");
  return 0;
}
