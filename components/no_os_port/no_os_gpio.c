/*
 * Componente: no_os_port - GPIO.
 *
 * Implementa la API no_os_gpio del driver ADIN1110 sobre driver/gpio de
 * ESP-IDF. El driver solo usa la linea de RESET (salida).
 */
#include "no_os_gpio.h"

#include <errno.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_err.h"

struct no_os_gpio_desc {
    int number;
};

int32_t no_os_gpio_get_optional(struct no_os_gpio_desc **desc,
                                const struct no_os_gpio_init_param *param)
{
    if (!desc) {
        return -EINVAL;
    }
    /* Pin opcional: ausente o numero negativo -> descriptor NULL, sin error. */
    if (!param || param->number < 0) {
        *desc = NULL;
        return 0;
    }

    struct no_os_gpio_desc *d = calloc(1, sizeof(*d));
    if (!d) {
        return -ENOMEM;
    }
    d->number = param->number;
    *desc = d;
    return 0;
}

int32_t no_os_gpio_direction_output(struct no_os_gpio_desc *desc, uint8_t value)
{
    if (!desc) {
        return -EINVAL;
    }
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << desc->number,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io) != ESP_OK) {
        return -EIO;
    }
    return no_os_gpio_set_value(desc, value);
}

int32_t no_os_gpio_set_value(struct no_os_gpio_desc *desc, uint8_t value)
{
    if (!desc) {
        return -EINVAL;
    }
    return gpio_set_level(desc->number, value) == ESP_OK ? 0 : -EIO;
}

int32_t no_os_gpio_remove(struct no_os_gpio_desc *desc)
{
    if (desc) {
        free(desc);
    }
    return 0;
}
