/*
 * Componente: no_os_port - API SPI de la abstraccion no-OS.
 *
 * Subconjunto de la API no_os_spi que utiliza el driver ADIN1110. La
 * implementacion (no_os_spi.c) se apoya en el controlador spi_master de
 * ESP-IDF. El parametro de inicializacion se ha adaptado para transportar la
 * configuracion SPI propia de ESP-IDF.
 */
#pragma once

#include <stdint.h>

/* Segmento de una transferencia SPI full-duplex. */
struct no_os_spi_msg {
    uint8_t *tx_buff;        /* datos a transmitir                         */
    uint8_t *rx_buff;        /* buffer de recepcion (puede ser == tx_buff) */
    uint32_t bytes_number;   /* longitud en bytes                          */
    uint8_t  cs_change;      /* gestionado automaticamente por ESP-IDF     */
};

/* Parametro de inicializacion del bus/dispositivo SPI (especifico de ESP-IDF). */
struct no_os_spi_init_param {
    int      host_id;        /* spi_host_device_t (p.ej. SPI2_HOST)        */
    int      sclk_gpio;
    int      mosi_gpio;
    int      miso_gpio;
    int      cs_gpio;
    uint32_t max_speed_hz;   /* frecuencia de reloj SPI                    */
    uint8_t  mode;           /* modo SPI 0..3 (ADIN1110: modo 0)           */
};

/* Descriptor opaco; su definicion vive en no_os_spi.c. */
struct no_os_spi_desc;

int32_t no_os_spi_init(struct no_os_spi_desc **desc,
                       const struct no_os_spi_init_param *param);
int32_t no_os_spi_remove(struct no_os_spi_desc *desc);
int32_t no_os_spi_transfer(struct no_os_spi_desc *desc,
                           struct no_os_spi_msg *msgs, uint32_t len);
