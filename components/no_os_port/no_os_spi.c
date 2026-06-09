/*
 * Componente: no_os_port - SPI maestro.
 *
 * Implementa la API no_os_spi del driver ADIN1110 sobre el controlador
 * spi_master de ESP-IDF. El DMA es obligatorio: las tramas Ethernet del
 * ADIN1110 superan el limite de 64 bytes por transaccion sin DMA.
 */
#include "no_os_spi.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

#include "driver/spi_master.h"
#include "esp_err.h"

/* Tamano maximo de transferencia SPI. Debe cubrir la mayor transferencia de
 * ambos modos del driver ADIN1110: ADIN1110_BUFF_LEN (1530 B) en SPI generico
 * y OA_SPI_BUFF_LEN (1632 B) en modo OA TC6. 2048 da margen para los dos. */
#define NO_OS_SPI_MAX_TRANSFER_SZ   2048

struct no_os_spi_desc {
    spi_device_handle_t handle;
    int                 host_id;
    bool                bus_owned;   /* true si este descriptor inicializo el bus */
};

int32_t no_os_spi_init(struct no_os_spi_desc **desc,
                       const struct no_os_spi_init_param *param)
{
    if (!desc || !param) {
        return -EINVAL;
    }

    struct no_os_spi_desc *d = calloc(1, sizeof(*d));
    if (!d) {
        return -ENOMEM;
    }

    const spi_bus_config_t buscfg = {
        .mosi_io_num     = param->mosi_gpio,
        .miso_io_num     = param->miso_gpio,
        .sclk_io_num     = param->sclk_gpio,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = NO_OS_SPI_MAX_TRANSFER_SZ,
    };
    /* ESP_ERR_INVALID_STATE => el bus ya estaba inicializado por otro: se
     * tolera, pero entonces no es nuestro y no se debera liberar. */
    esp_err_t err = spi_bus_initialize(param->host_id, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        free(d);
        return -EIO;
    }
    d->bus_owned = (err == ESP_OK);

    const spi_device_interface_config_t devcfg = {
        .clock_speed_hz = param->max_speed_hz,
        .mode           = param->mode,
        .spics_io_num   = param->cs_gpio,
        .queue_size     = 3,
    };
    err = spi_bus_add_device(param->host_id, &devcfg, &d->handle);
    if (err != ESP_OK) {
        if (d->bus_owned) {
            spi_bus_free(param->host_id);
        }
        free(d);
        return -EIO;
    }

    d->host_id = param->host_id;
    *desc = d;
    return 0;
}

int32_t no_os_spi_remove(struct no_os_spi_desc *desc)
{
    if (!desc) {
        return -EINVAL;
    }
    spi_bus_remove_device(desc->handle);
    if (desc->bus_owned) {
        spi_bus_free(desc->host_id);
    }
    free(desc);
    return 0;
}

int32_t no_os_spi_transfer(struct no_os_spi_desc *desc,
                           struct no_os_spi_msg *msgs, uint32_t len)
{
    if (!desc || !msgs) {
        return -EINVAL;
    }

    for (uint32_t i = 0; i < len; i++) {
        if (msgs[i].bytes_number == 0) {
            continue;
        }
        /* El driver ADIN1110 reutiliza el mismo buffer DMA-capable como tx y
         * rx; ESP-IDF admite la transferencia full-duplex in situ. */
        spi_transaction_t t = {
            .length    = (size_t)msgs[i].bytes_number * 8,
            .tx_buffer = msgs[i].tx_buff,
            .rx_buffer = msgs[i].rx_buff,
        };
        if (spi_device_transmit(desc->handle, &t) != ESP_OK) {
            return -EIO;
        }
    }
    return 0;
}
