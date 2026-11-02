/****************************************************************************
 * VelaOps Proxy API Client 实现。
 ****************************************************************************/

#include "velaops_proxy_client.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#define VELAOPS_SCHEMA_VERSION 1
#define VELAOPS_CONTENT_TYPE "application/json"

static bool velaops_copy_text(char *destination, size_t capacity,
                              const char *source)
{
  size_t length;

  if (destination == NULL || capacity == 0 || source == NULL)
    {
      return false;
    }
  length = strlen(source);
  if (length >= capacity)
    {
      return false;
    }
  memcpy(destination, source, length + 1);
  return true;
}

static bool velaops_copy_json(char *destination, size_t capacity,
                              const cJSON *item)
{
  char *encoded;
  bool copied;

  encoded = cJSON_PrintUnformatted(item);
  if (encoded == NULL)
    {
      return false;
    }
  copied = velaops_copy_text(destination, capacity, encoded);
  cJSON_free(encoded);
  return copied;
}

static bool velaops_parse_request_id(const cJSON *root,
                                     const char *expected_request_id,
                                     velaops_proxy_response_t *response)
{
  const cJSON *request_id = cJSON_GetObjectItemCaseSensitive(root,
                                                             "request_id");

  if (request_id == NULL)
    {
      return true;
    }
  if (!cJSON_IsString(request_id) || request_id->valuestring == NULL ||
      strlen(request_id->valuestring) != 32 ||
      strcmp(request_id->valuestring, expected_request_id) != 0)
    {
      return false;
    }
  response->has_request_id = true;
  return velaops_copy_text(response->request_id, sizeof(response->request_id),
                           request_id->valuestring);
}

static bool velaops_parse_success(const cJSON *root,
                                  velaops_proxy_response_t *response)
{
  const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");

  if (result == NULL)
    {
      return false;
    }
  response->ok = true;
  return velaops_copy_json(response->result_json,
                           sizeof(response->result_json), result);
}

static bool velaops_parse_error(const cJSON *root,
                                velaops_proxy_response_t *response)
{
  const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
  const cJSON *code;
  const cJSON *message;
  const cJSON *retryable;

  if (!cJSON_IsObject(error))
    {
      return false;
    }
  code = cJSON_GetObjectItemCaseSensitive(error, "code");
  message = cJSON_GetObjectItemCaseSensitive(error, "message");
  retryable = cJSON_GetObjectItemCaseSensitive(error, "retryable");
  if (!cJSON_IsString(code) || code->valuestring == NULL ||
      !cJSON_IsString(message) || message->valuestring == NULL ||
      !cJSON_IsBool(retryable))
    {
      return false;
    }

  response->ok = false;
  response->retryable = cJSON_IsTrue(retryable);
  return velaops_copy_text(response->error_code,
                           sizeof(response->error_code), code->valuestring) &&
         velaops_copy_text(response->error_message,
                           sizeof(response->error_message),
                           message->valuestring);
}

static velaops_proxy_client_status_t velaops_parse_response(
    const char *body, size_t body_len, int http_status,
    const char *expected_request_id, velaops_proxy_response_t *response)
{
  cJSON *root;
  const cJSON *schema_version;
  const cJSON *ok;
  bool valid;

  root = cJSON_ParseWithLength(body, body_len);
  if (!cJSON_IsObject(root))
    {
      cJSON_Delete(root);
      return VELAOPS_PROXY_CLIENT_INVALID_RESPONSE;
    }
  schema_version = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
  ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
  valid = cJSON_IsNumber(schema_version) &&
          schema_version->valuedouble == VELAOPS_SCHEMA_VERSION &&
          cJSON_IsBool(ok) &&
          velaops_parse_request_id(root, expected_request_id, response);

  if (valid && cJSON_IsTrue(ok))
    {
      valid = http_status >= 200 && http_status < 300 &&
              velaops_parse_success(root, response);
    }
  else if (valid)
    {
      valid = (http_status < 200 || http_status >= 300) &&
              velaops_parse_error(root, response);
    }
  cJSON_Delete(root);
  return valid ? VELAOPS_PROXY_CLIENT_OK :
                 VELAOPS_PROXY_CLIENT_INVALID_RESPONSE;
}

const char *velaops_proxy_client_status_name(
    velaops_proxy_client_status_t status)
{
  static const char *const names[] = {
    "ok",
    "invalid_argument",
    "protocol_error",
    "transport_error",
    "response_too_large",
    "invalid_response"
  };

  if ((unsigned int)status >= sizeof(names) / sizeof(names[0]))
    {
      return "unknown";
    }
  return names[status];
}

velaops_proxy_client_status_t velaops_proxy_client_post_json(
    const velaops_proxy_client_t *client,
    const char *target,
    const uint8_t *body, size_t body_len,
    const velaops_auth_metadata_t *metadata,
    velaops_proxy_response_t *response)
{
  char signature[VELAOPS_SIGNATURE_HEX_SIZE];
  char timestamp[24];
  char response_body[VELAOPS_PROXY_RESPONSE_CAPACITY];
  size_t response_len = 0;
  int http_status;
  velaops_protocol_status_t protocol_status;
  velaops_http_header_t headers[8];

  if (client == NULL || client->device_id == NULL || client->secret == NULL ||
      client->transport == NULL || target == NULL || metadata == NULL ||
      response == NULL || (body == NULL && body_len != 0) ||
      metadata->device_id == NULL ||
      strcmp(client->device_id, metadata->device_id) != 0)
    {
      return VELAOPS_PROXY_CLIENT_INVALID_ARGUMENT;
    }

  memset(response, 0, sizeof(*response));
  protocol_status = velaops_calculate_signature(
      client->secret, client->secret_len, "POST", target, body, body_len,
      metadata, signature);
  if (protocol_status != VELAOPS_PROTOCOL_OK)
    {
      return VELAOPS_PROXY_CLIENT_PROTOCOL_ERROR;
    }
  if (snprintf(timestamp, sizeof(timestamp), "%lld",
               (long long)metadata->timestamp) < 0)
    {
      return VELAOPS_PROXY_CLIENT_PROTOCOL_ERROR;
    }

  headers[0] = (velaops_http_header_t){"Content-Type", VELAOPS_CONTENT_TYPE};
  headers[1] = (velaops_http_header_t){VELAOPS_HEADER_VERSION,
                                      metadata->version};
  headers[2] = (velaops_http_header_t){VELAOPS_HEADER_DEVICE_ID,
                                      metadata->device_id};
  headers[3] = (velaops_http_header_t){VELAOPS_HEADER_REQUEST_ID,
                                      metadata->request_id};
  headers[4] = (velaops_http_header_t){VELAOPS_HEADER_TIMESTAMP, timestamp};
  headers[5] = (velaops_http_header_t){VELAOPS_HEADER_NONCE, metadata->nonce};
  headers[6] = (velaops_http_header_t){VELAOPS_HEADER_SIGNATURE, signature};
  headers[7] = (velaops_http_header_t){NULL, NULL};

  http_status = client->transport(
      client->transport_context, "POST", target, headers, body, body_len,
      response_body, sizeof(response_body), &response_len);
  if (http_status < 0)
    {
      return VELAOPS_PROXY_CLIENT_TRANSPORT_ERROR;
    }
  if (response_len >= sizeof(response_body))
    {
      return VELAOPS_PROXY_CLIENT_RESPONSE_TOO_LARGE;
    }
  response_body[response_len] = '\0';
  response->http_status = http_status;
  return velaops_parse_response(response_body, response_len, http_status,
                                metadata->request_id, response);
}
