/*
 * Componente: no_os_port - API de reserva de memoria de la abstraccion no-OS.
 *
 * IMPORTANTE: toda la memoria se reserva DMA-capable. El driver ADIN1110 usa
 * el buffer reservado aqui como buffer de transferencia SPI con DMA.
 */
#pragma once

#include <stddef.h>

void *no_os_malloc(size_t size);
void *no_os_calloc(size_t nmemb, size_t size);
void *no_os_realloc(void *ptr, size_t size);
void  no_os_free(void *ptr);
