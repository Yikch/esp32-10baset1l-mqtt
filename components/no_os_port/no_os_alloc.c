/*
 * Componente: no_os_port - reserva de memoria.
 *
 * Toda la memoria se reserva DMA-capable: el buffer interno del driver
 * ADIN1110 se reserva con estas funciones y se usa como buffer de
 * transferencia SPI con DMA.
 */
#include "no_os_alloc.h"

#include "esp_heap_caps.h"

#define NO_OS_ALLOC_CAPS   (MALLOC_CAP_DMA | MALLOC_CAP_8BIT)

void *no_os_malloc(size_t size)
{
    return heap_caps_malloc(size, NO_OS_ALLOC_CAPS);
}

void *no_os_calloc(size_t nmemb, size_t size)
{
    return heap_caps_calloc(nmemb, size, NO_OS_ALLOC_CAPS);
}

void *no_os_realloc(void *ptr, size_t size)
{
    return heap_caps_realloc(ptr, size, NO_OS_ALLOC_CAPS);
}

void no_os_free(void *ptr)
{
    heap_caps_free(ptr);
}
