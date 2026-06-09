/*
 * Componente: eth_adin1110 - implementacion.
 *
 * ADIN1110 (10BASE-T1L, SPI OPEN Alliance TC6) integrado como interfaz esp_netif
 * de clase Ethernet con IP estatica. Ver eth_adin1110.h.
 *
 * Modelo de concurrencia: un unico mutex (s_lock) serializa TODA llamada
 * adin1110_* (TX desde lwIP, RX y sondeo de enlace desde la tarea de servicio),
 * porque el driver no-OS no es reentrante (buffers de chunk OA compartidos).
 */
#include "eth_adin1110.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "sdkconfig.h"

#include "adin1110.h"

static const char *TAG = "eth_adin";

/* Trama Ethernet maxima sin FCS: cabecera (14) + datos (1500) = 1514. Coincide
 * con la capacidad del buffer de trama del driver OA TC6 (oa_tc6_frame_buffer,
 * CONFIG_OA_CHUNK_BUFFER_SIZE), de modo que ni la TX (write_fifo) ni la RX
 * (read_fifo) lo desbordan. La FCS la anade/quita el MAC del ADIN1110: no viaja
 * por SPI. */
#define ETH_FRAME_MAX_LEN   1514

/* Reintentos de transmision ante FIFO de TX llena (-EAGAIN). */
#define TX_EAGAIN_RETRIES   8

/* Maximo de tramas a drenar por pasada de la tarea de servicio. */
#define RX_DRAIN_BUDGET     16

/* ---------------------------------------------------------------------- */
/* Estado del componente (singleton)                                      */
/* ---------------------------------------------------------------------- */

typedef struct {
    esp_netif_driver_base_t base;   /* DEBE ser el primer campo (esp_netif). */
} adin_netif_driver_t;

static struct adin1110_desc *s_adin;
static SemaphoreHandle_t     s_lock;       /* serializa los accesos al driver  */
static SemaphoreHandle_t     s_int_sem;    /* despierta la tarea ante INT      */
static esp_netif_t          *s_netif;
static adin_netif_driver_t  *s_driver;
static volatile bool         s_link_up;
static uint8_t               s_mac[ADIN1110_ETH_ALEN];
static esp_netif_ip_info_t   s_ip_info;

#define LOCK()    xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK()  xSemaphoreGive(s_lock)

/* ---------------------------------------------------------------------- */
/* Bring-up del ADIN1110 (OPEN Alliance TC6)                              */
/* ---------------------------------------------------------------------- */

static esp_err_t adin_hw_init(void)
{
    struct adin1110_init_param ip = {
        .chip_type  = ADIN1110,
        .oa_tc6_spi = true,                 /* OPEN Alliance TC6 (default del chip) */
        .append_crc = false,                /* en OA la proteccion la lleva prote_spi */
        .comm_param = {
            .host_id      = CONFIG_ETH_ADIN1110_SPI_HOST,
            .sclk_gpio    = CONFIG_ETH_ADIN1110_SCLK_GPIO,
            .mosi_gpio    = CONFIG_ETH_ADIN1110_MOSI_GPIO,
            .miso_gpio    = CONFIG_ETH_ADIN1110_MISO_GPIO,
            .cs_gpio      = CONFIG_ETH_ADIN1110_CS_GPIO,
            .max_speed_hz = CONFIG_ETH_ADIN1110_SPI_HZ,
            .mode         = 0,
        },
        .reset_param = { .number = CONFIG_ETH_ADIN1110_RESET_GPIO },
        .int_param   = { .number = -1 },    /* el INT lo gestiona este componente */
    };
    memcpy(ip.mac_address, s_mac, sizeof(s_mac));

    int ret = adin1110_init(&s_adin, &ip);
    if (ret != 0) {
        s_adin = NULL;
        ESP_LOGE(TAG, "adin1110_init fallo (%d)", ret);
        return ESP_FAIL;
    }

    uint32_t phy_id = 0;
    ret = adin1110_reg_read(s_adin, ADIN1110_PHY_ID_REG, &phy_id);
    if (ret != 0 || phy_id != ADIN1110_PHY_ID) {
        ESP_LOGE(TAG, "PHY ID invalido: 0x%08X (ret=%d)", (unsigned)phy_id, ret);
        adin1110_remove(s_adin);
        s_adin = NULL;
        return ESP_FAIL;
    }

    /* Limpia los latches de estado (RESETC es write-1-to-clear). */
    adin1110_reg_write(s_adin, ADIN1110_STATUS0_REG, ADIN1110_CLEAR_STATUS0);

    /* Filtro de MAC propia + reenvio de broadcast al host (necesario para ARP). */
    adin1110_set_mac_addr(s_adin, s_mac);
    adin1110_broadcast_filter(s_adin, true);

    ESP_LOGI(TAG, "ADIN1110 OK: PHY ID 0x%08X (OA TC6)", (unsigned)phy_id);
    return ESP_OK;
}

/* ---------------------------------------------------------------------- */
/* Transmision (camino lwIP -> ADIN1110)                                  */
/* ---------------------------------------------------------------------- */

/* Llamada por esp_netif/lwIP con una trama de capa 2 contigua (sin FCS, que lo
 * anade el MAC del ADIN1110). Marshalling a adin1110_eth_buff: los 14 bytes de
 * cabecera (dest+src+ethertype, contiguos en la struct) y el payload aparte. */
static esp_err_t adin_transmit(void *h, void *buffer, size_t len)
{
    (void)h;
    if (len < ADIN1110_ETH_HDR_LEN || len > ETH_FRAME_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    struct adin1110_eth_buff eb = {0};
    memcpy(eb.mac_dest, buffer, ADIN1110_ETH_HDR_LEN);   /* 14 B contiguos */
    eb.payload = (uint8_t *)buffer + ADIN1110_ETH_HDR_LEN;
    eb.len     = len;

    LOCK();
    int ret = adin1110_write_fifo(s_adin, 0, &eb);
    UNLOCK();

    /* FIFO de TX llena: reintentos breves liberando el mutex entre intentos. */
    for (int i = 0; ret == -EAGAIN && i < TX_EAGAIN_RETRIES; i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
        LOCK();
        ret = adin1110_write_fifo(s_adin, 0, &eb);
        UNLOCK();
    }

    if (ret != 0) {
        ESP_LOGW(TAG, "TX fallida (%d), %u bytes", ret, (unsigned)len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ---------------------------------------------------------------------- */
/* Recepcion y supervision (tarea de servicio)                            */
/* ---------------------------------------------------------------------- */

/* Libera el buffer de una trama de RX cedida a la pila. esp_netif invoca esta
 * funcion (con el puntero pasado como 4.o argumento de esp_netif_receive)
 * cuando lwIP termina con la trama. Es OBLIGATORIA en el modelo zero-copy de
 * esp_netif: si se deja en NULL, esp_pbuf_free desreferencia un puntero de
 * funcion NULL y provoca un panic (InstrFetchProhibited) al recibir la primera
 * trama. */
static void adin_free_rx(void *h, void *buffer)
{
    (void)h;
    free(buffer);
}

/* Drena una trama de la FIFO de RX y la entrega a la pila. Devuelve true si
 * entrego una trama, false si no habia mas (o error).
 *
 * Modelo zero-copy: esp_netif NO copia la trama; referencia el buffer y lo
 * libera despues via adin_free_rx. Por eso cada trama usa un buffer propio del
 * heap cuya propiedad se cede a la pila. Un buffer estatico reutilizado se
 * corromperia, porque esp_netif_receive encola el pbuf en el tcpip_thread y
 * regresa antes de que lwIP lo procese. */
static bool adin_rx_one(void)
{
    uint8_t *buf = malloc(ETH_FRAME_MAX_LEN);
    if (buf == NULL) {
        return false;
    }

    struct adin1110_eth_buff eb = {0};
    eb.payload = &buf[ADIN1110_ETH_HDR_LEN];   /* el payload se escribe aqui */

    LOCK();
    int ret = adin1110_read_fifo(s_adin, 0, &eb);
    UNLOCK();
    if (ret != 0) {
        free(buf);
        return false;   /* sin tramas pendientes */
    }

    /* NOTA: si eb.len incluyera el FCS (4 B), habria que restarlo aqui; a
     * validar contra el gateway. */
    if (eb.len < ADIN1110_ETH_HDR_LEN || eb.len > ETH_FRAME_MAX_LEN) {
        free(buf);
        return false;
    }

    /* Reensambla la trama contigua: cabecera de 14 B al frente del buffer. */
    memcpy(buf, eb.mac_dest, ADIN1110_ETH_HDR_LEN);

    /* Cede la propiedad del buffer a la pila (se libera en adin_free_rx). Si
     * esp_netif no lo acepta (interfaz caida / sin memoria) no toma posesion y
     * se libera aqui. */
    if (esp_netif_receive(s_netif, buf, eb.len, buf) != ESP_OK) {
        free(buf);
    }
    return true;
}

/* Sondea el enlace y refleja el cambio en el esp_netif (up/down). Al subir el
 * enlace con IP estatica, esp_netif emite IP_EVENT_ETH_GOT_IP -> arranca MQTT. */
static void adin_update_link(void)
{
    uint32_t state = 0;
    LOCK();
    int ret = adin1110_link_state(s_adin, &state);
    UNLOCK();
    if (ret != 0) {
        return;
    }

    bool up = (state != 0);
    if (up == s_link_up) {
        return;
    }
    s_link_up = up;
    ESP_LOGI(TAG, "Enlace 10BASE-T1L %s", up ? "ACTIVO (UP)" : "CAIDO (DOWN)");
    if (up) {
        esp_netif_action_connected(s_netif, NULL, 0, NULL);
    } else {
        esp_netif_action_disconnected(s_netif, NULL, 0, NULL);
    }
}

static void adin_service_task(void *arg)
{
    (void)arg;
    if (esp_task_wdt_add(NULL) != ESP_OK) {
        ESP_LOGW(TAG, "Tarea de servicio no suscrita al watchdog");
    }

    while (true) {
        /* Espera la interrupcion del ADIN1110; el timeout fija la cadencia de
         * sondeo del enlace y el drenaje de respaldo si no hay INT. */
        xSemaphoreTake(s_int_sem, pdMS_TO_TICKS(CONFIG_ETH_ADIN1110_POLL_MS));
        esp_task_wdt_reset();

        /* Drena la FIFO de RX (acotado para no monopolizar la CPU). */
        for (int i = 0; i < RX_DRAIN_BUDGET && adin_rx_one(); i++) {
        }

        adin_update_link();
    }
}

static void IRAM_ATTR adin_int_isr(void *arg)
{
    (void)arg;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_int_sem, &hp);
    if (hp) {
        portYIELD_FROM_ISR();
    }
}

/* ---------------------------------------------------------------------- */
/* Glue esp_netif                                                         */
/* ---------------------------------------------------------------------- */

static esp_err_t adin_post_attach(esp_netif_t *netif, void *args)
{
    adin_netif_driver_t *drv = args;
    drv->base.netif = netif;

    const esp_netif_driver_ifconfig_t ifcfg = {
        .handle               = drv,
        .transmit             = adin_transmit,
        .driver_free_rx_buffer = adin_free_rx,   /* libera el buffer cedido a lwIP */
    };
    return esp_netif_set_driver_config(netif, &ifcfg);
}

/* ---------------------------------------------------------------------- */
/* API publica                                                            */
/* ---------------------------------------------------------------------- */

esp_err_t eth_adin1110_start(const esp_netif_ip_info_t *ip_info,
                             const uint8_t mac_addr[6])
{
    if (s_lock != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ip_info == NULL || mac_addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(s_mac, mac_addr, sizeof(s_mac));
    s_ip_info = *ip_info;

    s_lock    = xSemaphoreCreateMutex();
    s_int_sem = xSemaphoreCreateBinary();
    if (s_lock == NULL || s_int_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Bring-up SPE (ADIN1110, OA TC6): SCLK=%d MOSI=%d MISO=%d CS=%d "
                  "INT=%d @ %d Hz",
             CONFIG_ETH_ADIN1110_SCLK_GPIO, CONFIG_ETH_ADIN1110_MOSI_GPIO,
             CONFIG_ETH_ADIN1110_MISO_GPIO, CONFIG_ETH_ADIN1110_CS_GPIO,
             CONFIG_ETH_ADIN1110_INT_GPIO, CONFIG_ETH_ADIN1110_SPI_HZ);

    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 3 && err != ESP_OK; i++) {
        err = adin_hw_init();
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar el ADIN1110");
        return err;
    }

    /* esp_netif de clase Ethernet (para que emita IP_EVENT_ETH_GOT_IP). */
    esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t cfg = {
        .base   = &base_cfg,
        .driver = NULL,                          /* se fija en post_attach     */
        .stack  = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };
    s_netif = esp_netif_new(&cfg);
    if (s_netif == NULL) {
        return ESP_FAIL;
    }
    esp_netif_set_mac(s_netif, s_mac);

    s_driver = calloc(1, sizeof(*s_driver));
    if (s_driver == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_driver->base.post_attach = adin_post_attach;
    ESP_ERROR_CHECK(esp_netif_attach(s_netif, s_driver));

    /* IP estatica (F-02): sin DHCP. */
    esp_netif_dhcpc_stop(s_netif);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_netif, &s_ip_info));

    /* Linea de interrupcion del ADIN1110 (recepcion dirigida por evento). */
    if (CONFIG_ETH_ADIN1110_INT_GPIO >= 0) {
        const gpio_config_t io = {
            .pin_bit_mask = 1ULL << CONFIG_ETH_ADIN1110_INT_GPIO,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .intr_type    = GPIO_INTR_NEGEDGE,   /* INT activo a nivel bajo */
        };
        ESP_ERROR_CHECK(gpio_config(&io));
        esp_err_t isr_err = gpio_install_isr_service(0);
        if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
            ESP_ERROR_CHECK(isr_err);
        }
        ESP_ERROR_CHECK(gpio_isr_handler_add(CONFIG_ETH_ADIN1110_INT_GPIO,
                                             adin_int_isr, NULL));
    }

    /* Interfaz administrativamente arriba; la conexion (link) la marca la tarea
     * de servicio cuando el enlace 10BASE-T1L sube. */
    esp_netif_action_start(s_netif, NULL, 0, NULL);

    BaseType_t ok = xTaskCreate(adin_service_task, "eth_adin", 4096, NULL, 6, NULL);
    if (ok != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Interfaz ADIN1110 lista (IP estatica " IPSTR ")",
             IP2STR(&s_ip_info.ip));
    return ESP_OK;
}

bool eth_adin1110_link_up(void)
{
    return s_link_up;
}
