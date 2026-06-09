/*
 * Componente: no_os_port - API de retardos de la abstraccion no-OS.
 */
#pragma once

#include <stdint.h>

/* Retardo en milisegundos (cede la CPU mediante vTaskDelay). */
void no_os_mdelay(uint32_t msecs);

/* Retardo en microsegundos (espera activa). */
void no_os_udelay(uint32_t usecs);
