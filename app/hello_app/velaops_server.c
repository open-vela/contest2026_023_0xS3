/****************************************************************************
 * VelaOps Sentinel - 多服务器配置管理
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "velaops.h"

#define SERVER_CONFIG_PATH "/tmp/velaops/servers.conf"

/* 加载服务器列表 */
int velaops_server_list_load(struct velaops_server_config *servers,
                             int max_servers)
{
  int fd;
  int count = 0;
  struct velaops_server_config temp;

  if (servers == NULL || max_servers <= 0)
    {
      return -1;
    }

  fd = open(SERVER_CONFIG_PATH, O_RDONLY);
  if (fd < 0)
    {
      printf("Server: No saved server configuration found\n");
      return 0;
    }

  /* 读取服务器配置 */
  while (count < max_servers &&
         read(fd, &temp, sizeof(temp)) == sizeof(temp))
    {
      servers[count] = temp;
      count++;
    }

  close(fd);
  printf("Server: Loaded %d server configurations\n", count);
  return count;
}

/* 保存服务器列表 */
int velaops_server_list_save(const struct velaops_server_config *servers,
                             int count)
{
  int fd;
  int i;
  int written = 0;

  if (servers == NULL || count <= 0)
    {
      return -1;
    }

  fd = open(SERVER_CONFIG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0)
    {
      printf("Server: Failed to open config file for writing\n");
      return -1;
    }

  /* 保存服务器配置 */
  for (i = 0; i < count; i++)
    {
      if (write(fd, &servers[i], sizeof(servers[i])) == sizeof(servers[i]))
        {
          written++;
        }
    }

  close(fd);
  printf("Server: Saved %d server configurations\n", written);
  return written;
}

/* 添加服务器 */
int velaops_server_add(struct velaops_server_config *servers, int *count,
                       const struct velaops_server_config *new_server)
{
  if (servers == NULL || count == NULL || new_server == NULL)
    {
      return -1;
    }

  if (*count >= VELAOPS_MAX_SERVERS)
    {
      printf("Server: Maximum servers reached (%d)\n", VELAOPS_MAX_SERVERS);
      return -1;
    }

  /* 检查是否已存在同名服务器 */
  int i;
  for (i = 0; i < *count; i++)
    {
      if (strcmp(servers[i].name, new_server->name) == 0)
        {
          printf("Server: Server '%s' already exists\n", new_server->name);
          return -1;
        }
    }

  /* 添加新服务器 */
  servers[*count] = *new_server;
  servers[*count].last_used = time(NULL);
  (*count)++;

  printf("Server: Added server '%s' (%d/%d)\n",
         new_server->name, *count, VELAOPS_MAX_SERVERS);

  /* 保存配置 */
  velaops_server_list_save(servers, *count);
  return 0;
}

/* 删除服务器 */
int velaops_server_remove(struct velaops_server_config *servers, int *count,
                          int index)
{
  int i;

  if (servers == NULL || count == NULL || index < 0 || index >= *count)
    {
      return -1;
    }

  printf("Server: Removing server '%s'\n", servers[index].name);

  /* 移动剩余服务器 */
  for (i = index; i < *count - 1; i++)
    {
      servers[i] = servers[i + 1];
    }
  (*count)--;

  /* 保存配置 */
  velaops_server_list_save(servers, *count);
  return 0;
}

/* 查找服务器 */
int velaops_server_find(const struct velaops_server_config *servers, int count,
                        const char *name)
{
  int i;

  if (servers == NULL || name == NULL)
    {
      return -1;
    }

  for (i = 0; i < count; i++)
    {
      if (strcmp(servers[i].name, name) == 0)
        {
          return i;
        }
    }

  return -1;
}

/* 更新服务器最后使用时间 */
void velaops_server_update_last_used(struct velaops_server_config *server)
{
  if (server != NULL)
    {
      server->last_used = time(NULL);
    }
}
