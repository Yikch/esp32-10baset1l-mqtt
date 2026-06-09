/*
 * Componente: app_config
 * Capa de configuracion del nodo sensor SPE.
 *
 * Los parametros (IP estatica, broker MQTT, topic, periodo de muestreo) no se
 * incrustan en codigo: se definen por defecto en Kconfig (menuconfig) y pueden
 * sobrescribirse en tiempo de ejecucion desde NVS, clave a clave.
 * Cubre el requisito no funcional 3 y los requisitos F-02, F-05 y F-06.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CONFIG_IP_MAX    16   /* "255.255.255.255" + '\0' */
#define APP_CONFIG_STR_MAX   64
#define APP_CONFIG_URI_MAX   128

/* Limites del periodo de muestreo (F-06: configurable, <=30 s). */
#define APP_CONFIG_PERIOD_MIN_MS  100U
#define APP_CONFIG_PERIOD_MAX_MS  30000U

/* Limites del keepalive MQTT (F-13: el broker debe detectar la caida en menos
 * de 30 s ~= 1,5 x keepalive, por lo que el keepalive debe ser <= 20 s). */
#define APP_CONFIG_KEEPALIVE_MIN_S  5U
#define APP_CONFIG_KEEPALIVE_MAX_S  20U

/**
 * @brief Configuracion completa del nodo sensor.
 */
typedef struct {
    /* Interfaz de red - IP estatica, sin DHCP (F-02). */
    char ip[APP_CONFIG_IP_MAX];
    char netmask[APP_CONFIG_IP_MAX];
    char gateway[APP_CONFIG_IP_MAX];

    /* Identidad del nodo. Se incrusta en los payloads de estado (F-10..F-12). */
    char     node_id[APP_CONFIG_STR_MAX];

    /* Cliente MQTT (F-05). Contrato de interfaz hacia el gateway. */
    char     broker_uri[APP_CONFIG_URI_MAX];
    char     username[APP_CONFIG_STR_MAX];  /* usuario MQTT; vacio = sin auth.
                                               En ThingsBoard: access token. */
    char     topic[APP_CONFIG_STR_MAX];         /* topic de datos (F-04/F-05) */
    char     status_topic[APP_CONFIG_STR_MAX];  /* topic de estado/LWT (F-10..F-12) */
    char     cmd_topic[APP_CONFIG_STR_MAX];     /* topic de comandos admin (F-12) */
    uint8_t  qos;
    uint16_t keepalive_s;                       /* keepalive MQTT en s (F-13) */

    /* Muestreo del sensor (F-06). */
    uint32_t sample_period_ms;
} app_config_t;

/**
 * @brief Carga la configuracion del nodo.
 *
 * Lee la namespace NVS del nodo. Cada parametro ausente en NVS toma su valor
 * por defecto de Kconfig, de modo que un dispositivo recien flasheado arranca
 * con una configuracion valida. Los valores fuera de rango se corrigen.
 *
 * @param[out] cfg  Estructura destino.
 * @return ESP_OK o ESP_ERR_INVALID_ARG.
 */
esp_err_t app_config_load(app_config_t *cfg);

/**
 * @brief Guarda la configuracion en NVS para sobrescribir los valores de Kconfig.
 *
 * @param cfg  Configuracion a persistir.
 * @return ESP_OK o codigo de error de NVS.
 */
esp_err_t app_config_save(const app_config_t *cfg);

/**
 * @brief Vuelca la configuracion actual por el log (nivel INFO).
 */
void app_config_print(const app_config_t *cfg);

#ifdef __cplusplus
}
#endif
