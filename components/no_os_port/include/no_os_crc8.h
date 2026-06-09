/*
 * Componente: no_os_port - API CRC8 de la abstraccion no-OS.
 *
 * El driver ADIN1110 usa CRC8 (polinomio 0x07, MSB primero) para proteger la
 * integridad de las transacciones SPI. Algoritmo basado en tabla, equivalente
 * al de no-OS / kernel Linux.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#define NO_OS_CRC8_TABLE_SIZE   256

/* Declara una tabla CRC8 con ambito de fichero. */
#define NO_OS_DECLARE_CRC8_TABLE(_table) \
    static uint8_t _table[NO_OS_CRC8_TABLE_SIZE]

/* Genera la tabla de busqueda para el polinomio dado (MSB primero). */
void no_os_crc8_populate_msb(uint8_t *table, uint8_t polynomial);

/* Calcula el CRC8 de 'nbytes' a partir del valor inicial 'crc'. */
uint8_t no_os_crc8(const uint8_t *table, const uint8_t *pdata,
                   size_t nbytes, uint8_t crc);
