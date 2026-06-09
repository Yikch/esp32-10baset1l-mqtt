/*
 * Componente: app_config - implementacion.
 *
 * La configuracion se almacena en la namespace NVS "nodecfg". Cada clave que
 * no exista en NVS se rellena con el valor por defecto de Kconfig: un nodo
 * recien flasheado arranca con configuracion valida sin necesidad de NVS.
 */
#include "app_config.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "app_config";

/* Namespace NVS y claves. Las claves NVS estan limitadas a 15 caracteres. */
#define NVS_NS          "nodecfg"
#define KEY_IP          "ip"
#define KEY_NETMASK     "netmask"
#define KEY_GATEWAY     "gateway"
#define KEY_NODE_ID     "node_id"
#define KEY_BROKER      "broker_uri"
#define KEY_USERNAME    "username"
#define KEY_TOPIC       "topic"
#define KEY_STATUS_TOP  "status_top"     /* <= 15 caracteres (limite NVS) */
#define KEY_CMD_TOP     "cmd_top"
#define KEY_QOS         "qos"
#define KEY_KEEPALIVE   "keepalive"
#define KEY_PERIOD      "period_ms"

/* Lee una cadena de NVS; si no existe, copia el valor por defecto. */
static void load_str(nvs_handle_t h, bool have_nvs, const char *key,
                      char *out, size_t out_sz, const char *def)
{
    size_t len = out_sz;
    if (!have_nvs || nvs_get_str(h, key, out, &len) != ESP_OK) {
        strlcpy(out, def, out_sz);
    }
}

esp_err_t app_config_load(app_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(cfg, 0, sizeof(*cfg));

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    const bool have_nvs = (err == ESP_OK);
    if (!have_nvs) {
        ESP_LOGW(TAG, "NVS '%s' no disponible (%s); se usan valores de Kconfig",
                 NVS_NS, esp_err_to_name(err));
    }

    load_str(h, have_nvs, KEY_IP,      cfg->ip,      sizeof(cfg->ip),      CONFIG_NODE_STATIC_IP);
    load_str(h, have_nvs, KEY_NETMASK, cfg->netmask, sizeof(cfg->netmask), CONFIG_NODE_NETMASK);
    load_str(h, have_nvs, KEY_GATEWAY, cfg->gateway, sizeof(cfg->gateway), CONFIG_NODE_GATEWAY);
    load_str(h, have_nvs, KEY_NODE_ID,  cfg->node_id, sizeof(cfg->node_id), CONFIG_NODE_ID);
    load_str(h, have_nvs, KEY_BROKER,   cfg->broker_uri, sizeof(cfg->broker_uri), CONFIG_NODE_MQTT_BROKER_URI);
    load_str(h, have_nvs, KEY_USERNAME, cfg->username, sizeof(cfg->username), CONFIG_NODE_MQTT_USERNAME);
    load_str(h, have_nvs, KEY_TOPIC,      cfg->topic,        sizeof(cfg->topic),        CONFIG_NODE_MQTT_TOPIC);
    load_str(h, have_nvs, KEY_STATUS_TOP, cfg->status_topic, sizeof(cfg->status_topic), CONFIG_NODE_MQTT_STATUS_TOPIC);
    load_str(h, have_nvs, KEY_CMD_TOP,    cfg->cmd_topic,    sizeof(cfg->cmd_topic),    CONFIG_NODE_MQTT_CMD_TOPIC);

    uint8_t qos = (uint8_t)CONFIG_NODE_MQTT_QOS;
    if (have_nvs) {
        (void)nvs_get_u8(h, KEY_QOS, &qos);
    }
    cfg->qos = qos;

    uint16_t keepalive = (uint16_t)CONFIG_NODE_MQTT_KEEPALIVE_S;
    if (have_nvs) {
        (void)nvs_get_u16(h, KEY_KEEPALIVE, &keepalive);
    }
    cfg->keepalive_s = keepalive;

    uint32_t period = (uint32_t)CONFIG_NODE_SAMPLE_PERIOD_MS;
    if (have_nvs) {
        (void)nvs_get_u32(h, KEY_PERIOD, &period);
    }
    cfg->sample_period_ms = period;

    if (have_nvs) {
        nvs_close(h);
    }

    /* Saneado: corregir valores fuera de rango (F-06, F-13 y contrato MQTT). */
    if (cfg->qos > 2) {
        ESP_LOGW(TAG, "QoS %u fuera de rango; se ajusta a 1", cfg->qos);
        cfg->qos = 1;
    }
    if (cfg->keepalive_s < APP_CONFIG_KEEPALIVE_MIN_S) {
        cfg->keepalive_s = APP_CONFIG_KEEPALIVE_MIN_S;
    } else if (cfg->keepalive_s > APP_CONFIG_KEEPALIVE_MAX_S) {
        ESP_LOGW(TAG, "Keepalive %u s > %u s (F-13); se ajusta al maximo",
                 cfg->keepalive_s, APP_CONFIG_KEEPALIVE_MAX_S);
        cfg->keepalive_s = APP_CONFIG_KEEPALIVE_MAX_S;
    }
    if (cfg->sample_period_ms < APP_CONFIG_PERIOD_MIN_MS) {
        cfg->sample_period_ms = APP_CONFIG_PERIOD_MIN_MS;
    } else if (cfg->sample_period_ms > APP_CONFIG_PERIOD_MAX_MS) {
        cfg->sample_period_ms = APP_CONFIG_PERIOD_MAX_MS;
    }

    return ESP_OK;
}

esp_err_t app_config_save(const app_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo abrir NVS '%s': %s", NVS_NS, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(h, KEY_IP, cfg->ip);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_NETMASK, cfg->netmask);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_GATEWAY, cfg->gateway);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_NODE_ID, cfg->node_id);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_BROKER, cfg->broker_uri);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_USERNAME, cfg->username);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_TOPIC, cfg->topic);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_STATUS_TOP, cfg->status_topic);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_CMD_TOP, cfg->cmd_topic);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_QOS, cfg->qos);
    if (err == ESP_OK) err = nvs_set_u16(h, KEY_KEEPALIVE, cfg->keepalive_s);
    if (err == ESP_OK) err = nvs_set_u32(h, KEY_PERIOD, cfg->sample_period_ms);
    if (err == ESP_OK) err = nvs_commit(h);

    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error guardando configuracion: %s", esp_err_to_name(err));
    }
    return err;
}

void app_config_print(const app_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    ESP_LOGI(TAG, "Configuracion del nodo:");
    ESP_LOGI(TAG, "  Nodo   : id=%s", cfg->node_id);
    ESP_LOGI(TAG, "  Red    : IP=%s  mascara=%s  gateway=%s",
             cfg->ip, cfg->netmask, cfg->gateway);
    ESP_LOGI(TAG, "  MQTT   : broker=%s  QoS=%u  keepalive=%u s",
             cfg->broker_uri, cfg->qos, cfg->keepalive_s);
    ESP_LOGI(TAG, "  MQTT   : topic_datos=%s", cfg->topic);
    ESP_LOGI(TAG, "  MQTT   : topic_estado=%s  topic_cmd=%s",
             cfg->status_topic, cfg->cmd_topic);
    ESP_LOGI(TAG, "  MQTT   : usuario=%s",
             cfg->username[0] != '\0' ? cfg->username : "(sin autenticacion)");
    ESP_LOGI(TAG, "  Muestreo: periodo=%u ms", (unsigned)cfg->sample_period_ms);
}
