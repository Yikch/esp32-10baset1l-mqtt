/*
 * Firmware del nodo sensor de gas sobre Single Pair Ethernet (10BASE-T1L).
 *
 * Al arrancar se levanta la interfaz Ethernet SPE (MAC-PHY ADIN1110) por SPI con
 * IP estatica (componente eth_adin1110, F-01/F-02), y en paralelo arranca el
 * camino de datos: muestreo del sensor y cliente MQTT (que publica al obtener IP).
 *
 * Arquitectura del camino de datos: dos tareas FreeRTOS desacopladas por una
 * cola (F-08):
 *
 *   [sensor_sampling_task] --(sensor_msg_t)--> [cola] --> [mqtt_network_task]
 *      lee los sensores I2C                     buffer        publica por MQTT
 *      construye el JSON                         local         con QoS configurable
 *
 * El nodo lee uno o varios sensores I2C del bus Qwiic (componente i2c_sensor,
 * modular y multi-sensor) y publica una medida por campo del payload JSON.
 *
 * Incluye el mecanismo de presencia del nodo (LWT, ONLINE/OFFLINE, cierre
 * ordenado y keepalive; F-10..F-13, seccion 9). La salud del sensor se refleja
 * en el topic de estado: el ONLINE lleva un campo "sensor":"ok"|"fault", de modo
 * que un fallo de comunicacion con el sensor (I2C) NO tira el nodo: este sigue
 * accesible, reintenta la lectura y notifica el fallo por MQTT.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"

#include "mqtt_client.h"

#include "app_config.h"
#include "i2c_sensor.h"
#include "sensor_payload.h"

/* Interfaz Ethernet Single Pair (ADIN1110, 10BASE-T1L) - F-01/F-02. */
#include "eth_adin1110.h"

static const char *TAG = "spe_node";

/* Profundidad de la cola: numero maximo de muestras pendientes en buffer. */
#define SENSOR_QUEUE_DEPTH    64
/* Tramo de espera entre refrescos del watchdog, en ms. */
#define WDT_CHUNK_MS      500
/* Limite del outbox de esp-mqtt, en bytes (acota la cola en RAM, F-07/9.6). */
#define MQTT_OUTBOX_LIMIT 8192
/* Espera maxima del PUBACK del estado OFFLINE en el cierre ordenado (F-12). */
#define SHUTDOWN_ACK_TIMEOUT_MS  2000
/* Lecturas consecutivas fallidas antes de declarar el sensor en fallo. */
#define SENSOR_FAULT_THRESHOLD   3

/* Mensaje que circula por la cola entre la tarea de muestreo y la de red. */
typedef struct {
    char json[SENSOR_PAYLOAD_MAX_LEN];   /* payload listo para publicar    */
} sensor_msg_t;

static app_config_t             s_cfg;
static QueueHandle_t            s_queue;
static esp_mqtt_client_handle_t s_client;
static volatile bool            s_mqtt_connected;
static volatile bool            s_shutdown_requested;

/* Salud del sensor (campo "sensor" del estado ONLINE) y estado del driver.
 * s_sensor_ok lo escribe solo la tarea de muestreo; el resto solo lo lee. */
static volatile bool            s_sensor_ok;
static volatile bool            s_sensor_inited;

/* Payloads de estado OFFLINE precompilados en arranque (F-10/F-12). El LWT
 * exige que el buffer del mensaje siga siendo valido durante toda la vida del
 * cliente, por eso son estaticos. El ONLINE se construye al vuelo porque
 * incorpora la salud del sensor, que varia en ejecucion (F-11). */
static char s_status_offline_unexpected[SENSOR_PAYLOAD_STATUS_MAX_LEN];
static char s_status_offline_graceful[SENSOR_PAYLOAD_STATUS_MAX_LEN];

/* Sincronizacion para esperar el PUBACK de un publish concreto (cierre F-12). */
static EventGroupHandle_t s_mqtt_evt;
#define BIT_PUBACK   BIT0
static volatile int s_wait_msg_id = -1;

/* ---------------------------------------------------------------------- */
/* Utilidades                                                             */
/* ---------------------------------------------------------------------- */

/* Suscribe la tarea actual al Task Watchdog Timer (F-09). */
static void wdt_subscribe(void)
{
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Tarea no suscrita al watchdog: %s", esp_err_to_name(err));
    }
}

/* Espera 'total_ms' en tramos, refrescando el watchdog en cada uno, de modo
 * que un periodo de muestreo largo no dispare el TWDT (F-06/F-09). */
static void delay_with_wdt(uint32_t total_ms)
{
    uint32_t elapsed = 0;
    while (elapsed < total_ms) {
        uint32_t chunk = (total_ms - elapsed) < WDT_CHUNK_MS
                             ? (total_ms - elapsed) : WDT_CHUNK_MS;
        vTaskDelay(pdMS_TO_TICKS(chunk));
        esp_task_wdt_reset();
        elapsed += chunk;
    }
}

/* ---------------------------------------------------------------------- */
/* Cliente MQTT (F-05) + presencia del nodo (F-10..F-13)                  */
/* ---------------------------------------------------------------------- */

/* Publica el estado ONLINE retenido con la salud actual del sensor (F-11).
 * Se construye al vuelo porque el campo "sensor" cambia en ejecucion. */
static void publish_online_status(void)
{
    char buf[SENSOR_PAYLOAD_STATUS_MAX_LEN];
    if (sensor_payload_build_status(s_cfg.node_id, "ONLINE",
                                    "sensor", s_sensor_ok ? "ok" : "fault",
                                    buf, sizeof(buf)) == ESP_OK) {
        esp_mqtt_client_publish(s_client, s_cfg.status_topic,
                                buf, 0, /* qos */ 1, /* retain */ 1);
    }
}

/* Actualiza la salud del sensor y, si cambia, republica el estado (alarma por
 * MQTT mediante el campo "sensor" del topic de estado). */
static void set_sensor_health(bool ok)
{
    if (ok == s_sensor_ok) {
        return;
    }
    s_sensor_ok = ok;
    ESP_LOGW(TAG, "Salud del sensor: %s", ok ? "OK (recuperado)" : "FALLO (alarma)");
    if (s_mqtt_connected) {
        publish_online_status();
    }
    /* Si no hay conexion, el proximo MQTT_EVENT_CONNECTED publica el estado
     * con la salud vigente. */
}

/* Espera el PUBACK del publish con 'msg_id' hasta 'timeout_ms' (F-12). */
static bool wait_for_publish_ack(int msg_id, uint32_t timeout_ms)
{
    if (msg_id < 0) {
        return false;
    }
    s_wait_msg_id = msg_id;
    xEventGroupClearBits(s_mqtt_evt, BIT_PUBACK);
    EventBits_t bits = xEventGroupWaitBits(s_mqtt_evt, BIT_PUBACK,
                                           pdTRUE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    s_wait_msg_id = -1;
    return (bits & BIT_PUBACK) != 0;
}

/* Determina si el payload de un comando administrativo solicita reinicio. */
static bool cmd_requests_reboot(const char *data, int len)
{
    if (data == NULL || len <= 0) {
        return false;
    }
    /* Comparacion robusta sin asumir terminacion en '\0': se copia un tramo
     * acotado, se pasa a minusculas y se buscan "reboot" o "shutdown". */
    char buf[64];
    int n = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
    for (int i = 0; i < n; i++) {
        char c = data[i];
        buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
    }
    buf[n] = '\0';
    return (strstr(buf, "reboot") != NULL) ||
           (strstr(buf, "shutdown") != NULL);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        /* F-11: publicar ONLINE retenido (con la salud del sensor) ANTES de
         * habilitar la publicacion de datos, para que el dashboard refleje
         * siempre el ultimo estado. */
        publish_online_status();
        ESP_LOGI(TAG, "MQTT conectado; estado ONLINE publicado (sensor:%s)",
                 s_sensor_ok ? "ok" : "fault");
        /* Suscripcion al topic de comandos administrativos (F-12). */
        if (s_cfg.cmd_topic[0] != '\0') {
            esp_mqtt_client_subscribe(s_client, s_cfg.cmd_topic, 1);
        }
        s_mqtt_connected = true;
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_connected = false;
        ESP_LOGW(TAG, "MQTT desconectado; reconexion automatica en curso");
        break;

    case MQTT_EVENT_PUBLISHED:
        /* PUBACK de un QoS 1. Despierta a quien espere ese msg_id (F-12). */
        if (event->msg_id == s_wait_msg_id) {
            xEventGroupSetBits(s_mqtt_evt, BIT_PUBACK);
        }
        break;

    case MQTT_EVENT_DATA:
        /* Comando administrativo entrante (F-12). El cierre ordenado no se
         * ejecuta aqui (este callback corre en la tarea del cliente MQTT):
         * se senaliza y lo realiza mqtt_network_task. */
        if (cmd_requests_reboot(event->data, event->data_len)) {
            ESP_LOGW(TAG, "Comando de reinicio recibido; cierre ordenado");
            s_shutdown_requested = true;
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error (tipo 0x%x)", event->error_handle->error_type);
        break;

    default:
        break;
    }
}

/* Arranca el cliente esp-mqtt. La reconexion automatica ante caida del broker
 * esta activada por defecto en el componente (F-07). */
static void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_cfg.broker_uri,

        /* F-13: keepalive <= 20 s para deteccion de caida en < 30 s. */
        .session.keepalive  = s_cfg.keepalive_s,

        /* F-10: Last Will Testament registrado antes de conectar. */
        .session.last_will = {
            .topic   = s_cfg.status_topic,
            .msg     = s_status_offline_unexpected,
            .msg_len = (int)strlen(s_status_offline_unexpected),
            .qos     = 1,
            .retain  = 1,
        },

        /* Acota la cola de salida en RAM ante cortes prolongados (9.6). */
        .outbox.limit = MQTT_OUTBOX_LIMIT,
    };
    /* Autenticacion opcional: usuario MQTT (en ThingsBoard, el access token
     * del dispositivo). Si esta vacio, se conecta sin credenciales. */
    if (s_cfg.username[0] != '\0') {
        mqtt_cfg.credentials.username = s_cfg.username;
    }
    s_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
}

/* Cierre ordenado (F-12): publica OFFLINE/shutdown retenido, espera el PUBACK
 * y detiene el cliente antes de reiniciar, para que el gateway distinga un
 * reinicio programado de una caida no controlada. */
static void mqtt_shutdown_graceful(void)
{
    int msg_id = esp_mqtt_client_publish(s_client, s_cfg.status_topic,
                                         s_status_offline_graceful, 0,
                                         /* qos */ 1, /* retain */ 1);
    if (wait_for_publish_ack(msg_id, SHUTDOWN_ACK_TIMEOUT_MS)) {
        ESP_LOGI(TAG, "Estado OFFLINE/shutdown confirmado por el broker");
    } else {
        ESP_LOGW(TAG, "Sin confirmacion del estado OFFLINE/shutdown (timeout)");
    }
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
}

/* ---------------------------------------------------------------------- */
/* Tarea de muestreo (F-03/F-04/F-06/F-08)                                */
/* ---------------------------------------------------------------------- */

/* Serializa las medidas validas de una pasada en el payload JSON de 'msg'.
 * Devuelve ESP_OK si el payload se construyo (con al menos un campo). */
static esp_err_t build_msg_from_readings(const i2c_sensor_reading_t *readings,
                                         size_t n, sensor_msg_t *msg)
{
    sensor_field_t fields[I2C_SENSOR_MAX_READINGS];
    for (size_t i = 0; i < n; i++) {
        fields[i].key = readings[i].key;
        snprintf(fields[i].value, sizeof(fields[i].value), "%.*f",
                 readings[i].decimals, readings[i].value);
    }
    return sensor_payload_build_data(fields, n, msg->json, sizeof(msg->json));
}

static void sensor_sampling_task(void *arg)
{
    wdt_subscribe();
    ESP_LOGI(TAG, "Tarea de muestreo iniciada (periodo %u ms)",
             (unsigned)s_cfg.sample_period_ms);

    int fail_count = 0;
    sensor_msg_t msg;
    while (true) {
        /* (Re)inicializacion de los sensores si no estan listos: cubre un fallo
         * de init en arranque y la recuperacion tras un fallo persistente. */
        if (!s_sensor_inited) {
            if (i2c_sensor_init() == ESP_OK) {
                s_sensor_inited = true;
                fail_count = 0;
                ESP_LOGI(TAG, "Sensores inicializados (%u activos)",
                         (unsigned)i2c_sensor_count());
                set_sensor_health(true);
            } else {
                set_sensor_health(false);
                delay_with_wdt(s_cfg.sample_period_ms);
                continue;
            }
        }

        i2c_sensor_reading_t readings[I2C_SENSOR_MAX_READINGS];
        size_t n = 0;
        esp_err_t err = i2c_sensor_read(readings, I2C_SENSOR_MAX_READINGS, &n);

        if (err != ESP_OK || n == 0) {
            ESP_LOGE(TAG, "Lectura de sensores fallida: %s", esp_err_to_name(err));
            if (++fail_count >= SENSOR_FAULT_THRESHOLD) {
                set_sensor_health(false);
                /* Recuperacion: reinicializa el bus por si quedo bloqueado;
                 * el siguiente ciclo intentara i2c_sensor_init de nuevo. */
                i2c_sensor_deinit();
                s_sensor_inited = false;
            }
        } else if (build_msg_from_readings(readings, n, &msg) != ESP_OK) {
            ESP_LOGE(TAG, "Construccion del payload fallida (%u campos)",
                     (unsigned)n);
        } else {
            fail_count = 0;
            set_sensor_health(true);

            /* La cola es el buffer local (F-07): si esta llena, se descarta la
             * muestra mas antigua para conservar la informacion mas reciente. */
            if (xQueueSend(s_queue, &msg, 0) == errQUEUE_FULL) {
                sensor_msg_t discarded;
                (void)xQueueReceive(s_queue, &discarded, 0);
                (void)xQueueSend(s_queue, &msg, 0);
                ESP_LOGW(TAG, "Cola llena: descartada la muestra mas antigua");
            }
            ESP_LOGI(TAG, "Muestra encolada: %s", msg.json);
        }

        delay_with_wdt(s_cfg.sample_period_ms);
    }
}

/* ---------------------------------------------------------------------- */
/* Tarea de red (F-05/F-07/F-08/F-12)                                     */
/* ---------------------------------------------------------------------- */

static void mqtt_network_task(void *arg)
{
    wdt_subscribe();
    ESP_LOGI(TAG, "Tarea de red iniciada");

    sensor_msg_t msg;
    while (true) {
        esp_task_wdt_reset();

        /* Cierre ordenado solicitado por comando (F-12). Se ejecuta aqui, no
         * en el callback de MQTT, para no detener el cliente desde su propia
         * tarea de eventos. */
        if (s_shutdown_requested && s_mqtt_connected) {
            mqtt_shutdown_graceful();
            ESP_LOGW(TAG, "Reiniciando tras cierre ordenado");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }

        /* Sin conexion no se toca la cola: las muestras se acumulan en ella
         * como buffer local (F-07) y la tarea de muestreo gestiona el
         * desbordamiento. Asi, ademas, una sola tarea desencola en cada
         * estado, evitando carreras sobre el frente de la cola. */
        if (!s_mqtt_connected) {
            delay_with_wdt(WDT_CHUNK_MS);
            continue;
        }

        /* Se retira la muestra mas antigua tomando posesion de ella: la tarea
         * de muestreo ya no puede afectarla mientras se publica. El timeout de
         * 1 s permite refrescar el watchdog aunque no haya datos. */
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }

        int msg_id = esp_mqtt_client_publish(s_client, s_cfg.topic,
                                             msg.json, 0, s_cfg.qos, 0);
        if (msg_id >= 0) {
            ESP_LOGI(TAG, "Publicado en %s: %s", s_cfg.topic, msg.json);
        } else {
            /* Fallo de publicacion: se devuelve la muestra al frente de la
             * cola para conservar el orden y se reintentara. */
            if (xQueueSendToFront(s_queue, &msg, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Cola llena: muestra descartada");
            } else {
                ESP_LOGW(TAG, "Publicacion fallida; muestra reencolada");
            }
            delay_with_wdt(WDT_CHUNK_MS);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Arranque (F-09)                                                        */
/* ---------------------------------------------------------------------- */

/* Arranca el cliente MQTT y su tarea de red cuando una interfaz de red obtiene
 * IP. Con el diseno SOLO-SPE, esa interfaz sera la del ADIN1110 (esp_eth +
 * esp_netif, Fase 4). Hasta que exista y obtenga IP este evento no se dispara
 * y MQTT permanece detenido: sin red no hay transporte hacia el broker y no se
 * usa Wi-Fi. El guardado (s_client != NULL) evita un doble arranque si llegan
 * varios eventos de IP. */
static void on_network_got_ip(void *arg, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    (void)arg; (void)base; (void)event_id; (void)event_data;
    if (s_client != NULL) {
        return;
    }
    ESP_LOGI(TAG, "Interfaz de red con IP: arrancando MQTT");
    mqtt_start();
    xTaskCreate(mqtt_network_task, "mqtt_net", 4096, NULL, 5, NULL);
}

/* Construye los payloads de estado OFFLINE a partir del node_id configurado.
 * El ONLINE se construye al vuelo (incorpora la salud del sensor). */
static void build_status_payloads(void)
{
    ESP_ERROR_CHECK(sensor_payload_build_status(
        s_cfg.node_id, "OFFLINE", "reason", "unexpected_disconnect",
        s_status_offline_unexpected, sizeof(s_status_offline_unexpected)));
    ESP_ERROR_CHECK(sensor_payload_build_status(
        s_cfg.node_id, "OFFLINE", "reason", "shutdown",
        s_status_offline_graceful, sizeof(s_status_offline_graceful)));
}

/* Pone en marcha el camino de datos completo (sensor, cola, MQTT y tareas). */
static void data_path_start(void)
{
    /* Payloads de estado y sincronizacion del cierre ordenado (F-10..F-12). */
    build_status_payloads();
    s_mqtt_evt = xEventGroupCreate();
    if (s_mqtt_evt == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el grupo de eventos MQTT");
        abort();
    }

    /* Sensores I2C (F-03). Un fallo de inicializacion NO es fatal: el nodo
     * arranca igualmente, queda accesible por MQTT y la tarea de muestreo
     * reintenta; el fallo se notifica en el campo "sensor" del estado. */
    esp_err_t serr = i2c_sensor_init();
    s_sensor_inited = (serr == ESP_OK);
    s_sensor_ok     = (serr == ESP_OK);
    if (serr != ESP_OK) {
        ESP_LOGE(TAG, "Sensores no inicializados: %s; el nodo arranca igualmente "
                      "y reintentara en la tarea de muestreo", esp_err_to_name(serr));
    }

    /* Cola de comunicacion entre tareas (F-08). */
    s_queue = xQueueCreate(SENSOR_QUEUE_DEPTH, sizeof(sensor_msg_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "No se pudo crear la cola");
        abort();
    }

    /* El muestreo del sensor corre SIEMPRE y usa la cola como buffer local
     * (F-07), con independencia de que haya red. */
    xTaskCreate(sensor_sampling_task, "sensor_sampling", 4096, NULL, 5, NULL);

    /* Red SOLO por SPE (F-02): el cliente MQTT y su tarea de red se arrancan
     * cuando la interfaz del ADIN1110 obtenga IP (IP_EVENT_ETH_GOT_IP). No se usa
     * Wi-Fi. Mientras el enlace SPE no este operativo, el nodo muestrea
     * localmente y NO publica. */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               on_network_got_ip, NULL));
    ESP_LOGI(TAG, "MQTT se arrancara cuando la interfaz SPE obtenga IP; "
                  "muestreo local activo");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Nodo sensor SPE - arranque");

    /* NVS: necesario para la pila de red y para la configuracion del nodo. */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Configuracion del nodo desde Kconfig/NVS (NFR 3). */
    ESP_ERROR_CHECK(app_config_load(&s_cfg));
    app_config_print(&s_cfg);

    /* Camino de datos: sensor + tareas + registro del disparador MQTT
     * (IP_EVENT_ETH_GOT_IP). Se monta ANTES de levantar la interfaz de red para
     * no perder el evento de IP si el enlace sube de inmediato. */
    data_path_start();

    /* Interfaz SPE/ADIN1110 con IP estatica (F-01/F-02). Al subir el enlace
     * 10BASE-T1L emite IP_EVENT_ETH_GOT_IP -> arranca MQTT. Un fallo de bring-up
     * NO tumba el nodo (F-09): sigue con el muestreo local. */
    esp_netif_ip_info_t ip_info = {0};
    esp_netif_str_to_ip4(s_cfg.ip,      &ip_info.ip);
    esp_netif_str_to_ip4(s_cfg.netmask, &ip_info.netmask);
    esp_netif_str_to_ip4(s_cfg.gateway, &ip_info.gw);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_ETH);

    esp_err_t eth_err = eth_adin1110_start(&ip_info, mac);
    if (eth_err != ESP_OK) {
        ESP_LOGE(TAG, "Interfaz SPE no disponible (%s); el nodo sigue con "
                      "muestreo local", esp_err_to_name(eth_err));
    }
}
