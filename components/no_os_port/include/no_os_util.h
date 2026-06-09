/*
 * Componente: no_os_port - capa de utilidades de la abstraccion no-OS.
 *
 * Macros y funciones de manipulacion de bits y de acceso a memoria no alineada
 * que el driver ADIN1110 espera de la capa no-OS de Analog Devices. Cabecera
 * pura (solo C estandar), sin dependencias de ESP-IDF.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/* Mascara de un bit y mascara de un rango de bits [h:l] (palabra de 32 bits). */
#define NO_OS_BIT(x)            (1UL << (x))
#define NO_OS_GENMASK(h, l)     (((~0UL) << (l)) & (~0UL >> (31U - (h))))

/* Coloca/extrae un campo definido por una mascara contigua. */
#define no_os_field_prep(mask, val) \
    (((uint32_t)(val) << __builtin_ctz(mask)) & (mask))
#define no_os_field_get(mask, reg) \
    (((uint32_t)(reg) & (mask)) >> __builtin_ctz(mask))

/* Redondea 'val' al alza al multiplo de 'alignment' (potencia de 2). */
#define no_os_align(val, alignment) \
    (((val) + ((alignment) - 1)) & ~((uint32_t)(alignment) - 1))

/* Minimo, maximo y division entera con redondeo al alza. */
#define no_os_min(x, y)             (((x) < (y)) ? (x) : (y))
#define no_os_max(x, y)             (((x) > (y)) ? (x) : (y))
#define NO_OS_DIV_ROUND_UP(x, y)    (((x) + (y) - 1) / (y))

/* --- Acceso a memoria no alineada, orden big-endian --- */

static inline void no_os_put_unaligned_be16(uint16_t val, uint8_t *buf)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val);
}

static inline void no_os_put_unaligned_be32(uint32_t val, uint8_t *buf)
{
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >> 8);
    buf[3] = (uint8_t)(val);
}

static inline uint16_t no_os_get_unaligned_be16(const uint8_t *buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

static inline uint32_t no_os_get_unaligned_be32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}
