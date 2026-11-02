/****************************************************************************
 * VelaOps 设备私密配置加载实现。
 ****************************************************************************/

#include "velaops_device_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define VELAOPS_CONFIG_MAX_SIZE 2048
#define VELAOPS_CONFIG_SCHEMA_VERSION 1
#define VELAOPS_MIN_SECRET_LENGTH 32

static void velaops_secure_clear(void *memory, size_t length)
{
  volatile unsigned char *cursor = memory;

  while (length-- > 0)
    {
      *cursor++ = 0;
    }
}

static velaops_config_status_t velaops_read_file(
    const char *path, size_t maximum, char **content, size_t *content_len)
{
  FILE *file;
  long size;
  char *buffer;
  size_t read_size;

  file = fopen(path, "rb");
  if (file == NULL)
    {
      return VELAOPS_CONFIG_IO_ERROR;
    }
  if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0)
    {
      fclose(file);
      return VELAOPS_CONFIG_IO_ERROR;
    }
  if ((unsigned long)size > maximum)
    {
      fclose(file);
      return VELAOPS_CONFIG_TOO_LARGE;
    }
  if (fseek(file, 0, SEEK_SET) != 0)
    {
      fclose(file);
      return VELAOPS_CONFIG_IO_ERROR;
    }

  buffer = malloc((size_t)size + 1);
  if (buffer == NULL)
    {
      fclose(file);
      return VELAOPS_CONFIG_NO_MEMORY;
    }
  read_size = fread(buffer, 1, (size_t)size, file);
  fclose(file);
  if (read_size != (size_t)size)
    {
      velaops_secure_clear(buffer, (size_t)size + 1);
      free(buffer);
      return VELAOPS_CONFIG_IO_ERROR;
    }
  buffer[read_size] = '\0';
  *content = buffer;
  *content_len = read_size;
  return VELAOPS_CONFIG_OK;
}

static bool velaops_copy_string_field(const cJSON *root, const char *name,
                                      char *destination, size_t capacity)
{
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
  size_t length;

  if (!cJSON_IsString(item) || item->valuestring == NULL)
    {
      return false;
    }
  length = strlen(item->valuestring);
  if (length == 0 || length >= capacity)
    {
      return false;
    }
  memcpy(destination, item->valuestring, length + 1);
  return true;
}

static bool velaops_valid_host(const char *host)
{
  const unsigned char *cursor = (const unsigned char *)host;

  while (*cursor != '\0')
    {
      if (!( (*cursor >= 'a' && *cursor <= 'z') ||
             (*cursor >= 'A' && *cursor <= 'Z') ||
             (*cursor >= '0' && *cursor <= '9') ||
             *cursor == '.' || *cursor == '-'))
        {
          return false;
        }
      cursor++;
    }
  return cursor != (const unsigned char *)host;
}

static bool velaops_valid_port(const char *port)
{
  const char *cursor = port;
  unsigned long value = 0;

  if (*cursor == '\0')
    {
      return false;
    }
  while (*cursor != '\0')
    {
      if (*cursor < '0' || *cursor > '9')
        {
          return false;
        }
      value = value * 10 + (unsigned long)(*cursor - '0');
      cursor++;
    }
  return value > 0 && value <= 65535;
}

static bool velaops_valid_device_id(const char *device_id)
{
  const unsigned char *cursor = (const unsigned char *)device_id;

  if (!( (*cursor >= 'a' && *cursor <= 'z') ||
         (*cursor >= 'A' && *cursor <= 'Z') ||
         (*cursor >= '0' && *cursor <= '9')))
    {
      return false;
    }
  cursor++;
  while (*cursor != '\0')
    {
      if (!( (*cursor >= 'a' && *cursor <= 'z') ||
             (*cursor >= 'A' && *cursor <= 'Z') ||
             (*cursor >= '0' && *cursor <= '9') ||
             *cursor == '.' || *cursor == '_' || *cursor == '-'))
        {
          return false;
        }
      cursor++;
    }
  return true;
}

static bool velaops_has_exact_fields(const cJSON *root)
{
  const char *const names[] = {
    "schema_version", "host", "port", "device_id", "secret"
  };
  bool seen[sizeof(names) / sizeof(names[0])] = {false};
  const cJSON *item;
  size_t count = 0;
  size_t index;

  cJSON_ArrayForEach(item, root)
    {
      bool known = false;
      for (index = 0; index < sizeof(names) / sizeof(names[0]); index++)
        {
          if (item->string != NULL && strcmp(item->string, names[index]) == 0)
            {
              if (seen[index])
                {
                  return false;
                }
              seen[index] = true;
              known = true;
              break;
            }
        }
      if (!known)
        {
          return false;
        }
      count++;
    }
  return count == sizeof(names) / sizeof(names[0]);
}

const char *velaops_config_status_name(velaops_config_status_t status)
{
  static const char *const names[] = {
    "ok", "invalid_argument", "io_error", "too_large", "invalid_json",
    "invalid_field", "no_memory"
  };

  if ((unsigned int)status >= sizeof(names) / sizeof(names[0]))
    {
      return "unknown";
    }
  return names[status];
}

velaops_config_status_t velaops_device_config_load(
    const char *config_path, velaops_device_config_t *config)
{
  char *json_text = NULL;
  size_t json_len = 0;
  cJSON *root = NULL;
  const cJSON *schema_version;
  cJSON *secret_item;
  velaops_config_status_t status;

  if (config_path == NULL || config == NULL)
    {
      return VELAOPS_CONFIG_INVALID_ARGUMENT;
    }
  memset(config, 0, sizeof(*config));

  status = velaops_read_file(config_path, VELAOPS_CONFIG_MAX_SIZE,
                             &json_text, &json_len);
  if (status != VELAOPS_CONFIG_OK)
    {
      return status;
    }
  root = cJSON_ParseWithLength(json_text, json_len);
  if (!cJSON_IsObject(root))
    {
      status = VELAOPS_CONFIG_INVALID_JSON;
      goto cleanup;
    }
  schema_version = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
  if (!velaops_has_exact_fields(root) || !cJSON_IsNumber(schema_version) ||
      schema_version->valuedouble != VELAOPS_CONFIG_SCHEMA_VERSION ||
      !velaops_copy_string_field(root, "host", config->host,
                                 sizeof(config->host)) ||
      !velaops_copy_string_field(root, "port", config->port,
                                 sizeof(config->port)) ||
      !velaops_copy_string_field(root, "device_id", config->device_id,
                                 sizeof(config->device_id)) ||
      !velaops_copy_string_field(root, "secret", config->secret,
                                 sizeof(config->secret)) ||
      !velaops_valid_host(config->host) || !velaops_valid_port(config->port) ||
      !velaops_valid_device_id(config->device_id) ||
      strlen(config->secret) < VELAOPS_MIN_SECRET_LENGTH)
    {
      status = VELAOPS_CONFIG_INVALID_FIELD;
      goto cleanup;
    }

cleanup:
  secret_item = cJSON_GetObjectItemCaseSensitive(root, "secret");
  if (cJSON_IsString(secret_item) && secret_item->valuestring != NULL)
    {
      velaops_secure_clear(secret_item->valuestring,
                           strlen(secret_item->valuestring));
    }
  cJSON_Delete(root);
  velaops_secure_clear(json_text, json_len);
  free(json_text);
  if (status != VELAOPS_CONFIG_OK)
    {
      velaops_device_config_clear(config);
    }
  return status;
}

void velaops_device_config_clear(velaops_device_config_t *config)
{
  if (config != NULL)
    {
      velaops_secure_clear(config, sizeof(*config));
    }
}
