/*
 * Componente: no_os_port - CRC8.
 *
 * Generacion de tabla y calculo de CRC8 (MSB primero), equivalentes a los de
 * no-OS / kernel Linux. C puro, sin dependencias de plataforma.
 */
#include "no_os_crc8.h"

void no_os_crc8_populate_msb(uint8_t *table, uint8_t polynomial)
{
    const uint8_t msbit = 0x80;
    uint8_t t = msbit;

    table[0] = 0;
    for (int i = 1; i < NO_OS_CRC8_TABLE_SIZE; i *= 2) {
        t = (uint8_t)((t << 1) ^ ((t & msbit) ? polynomial : 0));
        for (int j = 0; j < i; j++) {
            table[i + j] = table[j] ^ t;
        }
    }
}

uint8_t no_os_crc8(const uint8_t *table, const uint8_t *pdata,
                   size_t nbytes, uint8_t crc)
{
    while (nbytes-- > 0) {
        crc = table[(crc ^ *pdata++) & 0xff];
    }
    return crc;
}
