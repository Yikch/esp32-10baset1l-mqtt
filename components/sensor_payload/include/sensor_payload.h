/*
 * Componente: sensor_payload
 * Constructor del payload JSON publicado por MQTT (F-04).
 *
 * El nodo puede llevar varios sensores I2C en el mismo bus (ver i2c_sensor), de
 * modo que el payload de datos es un objeto JSON con un campo por medida:
 *     {"O2":"20.9","O3":"5"}
 * Cada valor se serializa como cadena (no como numero), por compatibilidad con
 * el historico del gateway. Con un unico sensor simulado el payload se reduce al
 * contrato historico {"CO_LEVEL":"<valor>"}. El topic de datos no cambia.
 */
#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Longitud maxima del valor formateado de un campo (cadena). */
#define SENSOR_FIELD_VALUE_MAX  16

/* Tamano de buffer suficiente para el payload de datos con varios campos. */
#define SENSOR_PAYLOAD_MAX_LEN  256

/* Tamano de buffer suficiente para el payload de estado, contando un node_id
 * de hasta 63 caracteres y el reason mas largo ("unexpected_disconnect"). */
#define SENSOR_PAYLOAD_STATUS_MAX_LEN  160

/**
 * @brief Un campo del payload de datos: clave JSON y su valor ya formateado.
 *
 * El valor se almacena ya como cadena (formateado por el llamante con la
 * precision propia de cada sensor), de modo que sensor_payload no necesita
 * conocer unidades ni decimales.
 */
typedef struct {
    const char *key;                          /* clave JSON (p. ej. "O2")     */
    char        value[SENSOR_FIELD_VALUE_MAX];/* valor formateado como cadena */
} sensor_field_t;

/**
 * @brief Construye el payload JSON de datos a partir de una lista de campos.
 *
 * Genera {"k0":"v0","k1":"v1",...}. No usa asignacion dinamica: escribe
 * directamente en el buffer proporcionado. Los valores ya vienen formateados y
 * no se escapan (se asume que no contienen comillas).
 *
 * @param fields    Array de campos (clave + valor formateado).
 * @param n_fields  Numero de campos (>= 1).
 * @param[out] buf  Buffer destino (se recomienda SENSOR_PAYLOAD_MAX_LEN).
 * @param buf_len   Tamano del buffer.
 * @return ESP_OK; ESP_ERR_INVALID_ARG si fields/buf es NULL o n_fields es 0;
 *         ESP_ERR_INVALID_SIZE si el buffer es demasiado pequeno.
 */
esp_err_t sensor_payload_build_data(const sensor_field_t *fields, size_t n_fields,
                                    char *buf, size_t buf_len);

/**
 * @brief Construye el payload JSON de estado del nodo (F-10, F-11, F-12).
 *
 * Sin campo extra:  {"node":"<id>","status":"<status>"}
 * Con campo extra:  {"node":"<id>","status":"<status>","<key>":"<val>"}
 *
 * El campo extra opcional sirve para el motivo de OFFLINE
 * ("reason":"shutdown"/"unexpected_disconnect") o la salud del sensor en
 * ONLINE ("sensor":"ok"/"fault").
 *
 * Los valores proceden de la configuracion/estado del nodo (no de fuentes
 * externas), por lo que no se realiza escapado JSON: se asume que no contienen
 * comillas.
 *
 * @param node_id    Identificador del nodo.
 * @param status     "ONLINE" u "OFFLINE".
 * @param extra_key  Clave del campo extra (p. ej. "reason" o "sensor"); NULL o
 *                   "" para omitir el campo extra.
 * @param extra_val  Valor del campo extra; NULL o "" para omitir el campo.
 * @param[out] buf   Buffer destino (se recomienda SENSOR_PAYLOAD_STATUS_MAX_LEN).
 * @param buf_len    Tamano del buffer.
 * @return ESP_OK; ESP_ERR_INVALID_ARG si algun argumento obligatorio es NULL;
 *         ESP_ERR_INVALID_SIZE si el buffer es demasiado pequeno.
 */
esp_err_t sensor_payload_build_status(const char *node_id, const char *status,
                                      const char *extra_key, const char *extra_val,
                                      char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
