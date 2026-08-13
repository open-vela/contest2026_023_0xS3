/****************************************************************************
 * VelaOps Sentinel - 连接池管理
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "velaops.h"

/* 初始化连接池 */
int velaops_connpool_init(struct velaops_connection_pool *pool)
{
  if (pool == NULL)
    {
      return -1;
    }

  memset(pool, 0, sizeof(*pool));
  pool->max_connections = VELAOPS_MAX_SERVERS;
  pool->count = 0;

  printf("Connection pool: Initialized (max %d connections)\n",
         pool->max_connections);
  return 0;
}

/* 从连接池获取连接 */
struct velaops_ssh *velaops_connpool_get(struct velaops_connection_pool *pool,
                                         const struct velaops_profile *profile)
{
  int i;
  struct velaops_ssh *client = NULL;
  char fingerprint[VELAOPS_FINGERPRINT_MAX];

  if (pool == NULL || profile == NULL)
    {
      return NULL;
    }

  /* 查找现有连接 */
  for (i = 0; i < pool->count; i++)
    {
      if (pool->connections[i] != NULL &&
          velaops_ssh_is_connected(pool->connections[i]))
        {
          printf("Connection pool: Reusing existing connection\n");
          return pool->connections[i];
        }
    }

  /* 创建新连接 */
  if (pool->count >= pool->max_connections)
    {
      printf("Connection pool: Maximum connections reached\n");
      return NULL;
    }

  printf("Connection pool: Creating new connection\n");
  client = velaops_ssh_connect(profile, fingerprint, sizeof(fingerprint));
  if (client != NULL)
    {
      pool->connections[pool->count] = client;
      pool->count++;
      printf("Connection pool: Connection added (%d/%d)\n",
             pool->count, pool->max_connections);
    }

  return client;
}

/* 释放连接回连接池 */
void velaops_connpool_release(struct velaops_connection_pool *pool,
                              struct velaops_ssh *client)
{
  int i;

  if (pool == NULL || client == NULL)
    {
      return;
    }

  /* 查找并移除连接 */
  for (i = 0; i < pool->count; i++)
    {
      if (pool->connections[i] == client)
        {
          /* 关闭连接 */
          velaops_ssh_close(client);
          pool->connections[i] = NULL;

          /* 移动剩余连接 */
          for (; i < pool->count - 1; i++)
            {
              pool->connections[i] = pool->connections[i + 1];
            }
          pool->count--;

          printf("Connection pool: Connection released (%d/%d)\n",
                 pool->count, pool->max_connections);
          return;
        }
    }
}

/* 清理连接池 */
void velaops_connpool_cleanup(struct velaops_connection_pool *pool)
{
  int i;

  if (pool == NULL)
    {
      return;
    }

  printf("Connection pool: Cleaning up %d connections\n", pool->count);

  for (i = 0; i < pool->count; i++)
    {
      if (pool->connections[i] != NULL)
        {
          velaops_ssh_close(pool->connections[i]);
          pool->connections[i] = NULL;
        }
    }

  pool->count = 0;
}
