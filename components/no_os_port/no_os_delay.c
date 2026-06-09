/*
 * Componente: no_os_port - retardos.
 */
#include "no_os_delay.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

void no_os_mdelay(uint32_t msecs)
{
    if (msecs == 0) {
        return;
    }
    TickType_t ticks = pdMS_TO_TICKS(msecs);
    if (ticks == 0) {
        ticks = 1;   /* garantiza un retardo no nulo */
    }
    vTaskDelay(ticks);
}

void no_os_udelay(uint32_t usecs)
{
    esp_rom_delay_us(usecs);
}
