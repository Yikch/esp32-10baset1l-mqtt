/*
 * Componente: no_os_port - API GPIO de la abstraccion no-OS.
 *
 * Subconjunto de la API no_os_gpio que utiliza el driver ADIN1110 (solo la
 * linea de RESET). Implementado sobre driver/gpio de ESP-IDF.
 */
#pragma once

#include <stdint.h>

#define NO_OS_GPIO_LOW   0
#define NO_OS_GPIO_HIGH  1

/* Parametro de inicializacion de un GPIO. */
struct no_os_gpio_init_param {
    int number;              /* numero de GPIO; negativo -> pin no presente */
};

/* Descriptor opaco; su definicion vive en no_os_gpio.c. */
struct no_os_gpio_desc;

/*
 * Obtiene un GPIO opcional: si 'param' es NULL o el numero es negativo,
 * devuelve *desc = NULL y exito (el pin simplemente no existe).
 */
int32_t no_os_gpio_get_optional(struct no_os_gpio_desc **desc,
                                const struct no_os_gpio_init_param *param);
int32_t no_os_gpio_direction_output(struct no_os_gpio_desc *desc, uint8_t value);
int32_t no_os_gpio_set_value(struct no_os_gpio_desc *desc, uint8_t value);
int32_t no_os_gpio_remove(struct no_os_gpio_desc *desc);
