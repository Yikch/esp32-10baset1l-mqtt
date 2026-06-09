/*
 * Componente: sensor_payload - implementacion.
 *
 * El payload de datos es un objeto JSON con un campo por medida:
 * {"k0":"v0","k1":"v1",...}. Se construye con snprintf incremental sobre el
 * buffer proporcionado: sin heap, sin dependencias externas y sin escapado (los
 * valores vienen ya formateados y se asume que no contienen comillas).
 */
#include "sensor_payload.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"

static const char *TAG = "sensor_payload";

esp_err_t sensor_payload_build_data(const sensor_field_t *fields, size_t n_fields,
                                    char *buf, size_t buf_len)
{
    if (fields == NULL || buf == NULL || buf_len == 0 || n_fields == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t off = 0;
    int n = snprintf(buf + off, buf_len - off, "{");
    if (n < 0 || (size_t)n >= buf_len - off) {
        goto too_small;
    }
    off += (size_t)n;

    for (size_t i = 0; i < n_fields; i++) {
        n = snprintf(buf + off, buf_len - off, "%s\"%s\":\"%s\"",
                     (i == 0) ? "" : ",", fields[i].key, fields[i].value);
        if (n < 0 || (size_t)n >= buf_len - off) {
            goto too_small;
        }
        off += (size_t)n;
    }

    n = snprintf(buf + off, buf_len - off, "}");
    if (n < 0 || (size_t)n >= buf_len - off) {
        goto too_small;
    }
    return ESP_OK;

too_small:
    ESP_LOGE(TAG, "Buffer insuficiente para el payload de datos (%u bytes)",
             (unsigned)buf_len);
    return ESP_ERR_INVALID_SIZE;
}

esp_err_t sensor_payload_build_status(const char *node_id, const char *status,
                                      const char *extra_key, const char *extra_val,
                                      char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0 || node_id == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool has_extra = (extra_key != NULL && extra_key[0] != '\0' &&
                      extra_val != NULL && extra_val[0] != '\0');

    int n;
    if (has_extra) {
        n = snprintf(buf, buf_len,
                     "{\"node\":\"%s\",\"status\":\"%s\",\"%s\":\"%s\"}",
                     node_id, status, extra_key, extra_val);
    } else {
        n = snprintf(buf, buf_len,
                     "{\"node\":\"%s\",\"status\":\"%s\"}", node_id, status);
    }
    if (n < 0 || (size_t)n >= buf_len) {
        ESP_LOGE(TAG, "Buffer insuficiente para el payload de estado (%u bytes)",
                 (unsigned)buf_len);
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}
