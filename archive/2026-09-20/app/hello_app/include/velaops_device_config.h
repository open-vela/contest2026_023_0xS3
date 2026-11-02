/****************************************************************************
 * VelaOps 设备私密配置加载。
 ****************************************************************************/

#ifndef VELAOPS_DEVICE_CONFIG_H
#define VELAOPS_DEVICE_CONFIG_H

#include <stddef.h>

#define VELAOPS_CONFIG_FILE "/data/velaops/config.json"
#define VELAOPS_HOST_CAPACITY 254
#define VELAOPS_PORT_CAPACITY 6
#define VELAOPS_DEVICE_ID_CAPACITY 65
#define VELAOPS_SECRET_CAPACITY 129

typedef enum
{
  VELAOPS_CONFIG_OK = 0,
  VELAOPS_CONFIG_INVALID_ARGUMENT,
  VELAOPS_CONFIG_IO_ERROR,
  VELAOPS_CONFIG_TOO_LARGE,
  VELAOPS_CONFIG_INVALID_JSON,
  VELAOPS_CONFIG_INVALID_FIELD,
  VELAOPS_CONFIG_NO_MEMORY
} velaops_config_status_t;

typedef struct
{
  char host[VELAOPS_HOST_CAPACITY];
  char port[VELAOPS_PORT_CAPACITY];
  char device_id[VELAOPS_DEVICE_ID_CAPACITY];
  char secret[VELAOPS_SECRET_CAPACITY];
} velaops_device_config_t;

const char *velaops_config_status_name(velaops_config_status_t status);

velaops_config_status_t velaops_device_config_load(
    const char *config_path, velaops_device_config_t *config);

/* 显式清除内存中的密钥。 */
void velaops_device_config_clear(velaops_device_config_t *config);

#endif /* VELAOPS_DEVICE_CONFIG_H */
