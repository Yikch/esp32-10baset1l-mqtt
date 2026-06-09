/*
 * Driver: sensor simulado (sin hardware) - implementacion.
 *
 * Recorrido aleatorio acotado en torno a un valor inicial plausible de interior.
 * No toca el bus I2C; ignora el parametro 'bus' de init(). Util para validar el
 * camino de datos (tareas, cola, MQTT, presencia) sin sensores fisicos.
 */
#include "simulated.h"

#include "esp_random.h"
#include "esp_log.h"

#define SIM_MIN     0
#define SIM_MAX     2000
#define SIM_INIT    400    /* valor inicial plausible en interior (ppm) */

static const char *TAG = "sim_sensor";

static int  s_value = SIM_INIT;
static bool s_ready;

static int clamp(int v)
{
    if (v < SIM_MIN) {
        return SIM_MIN;
    }
    if (v > SIM_MAX) {
        return SIM_MAX;
    }
    return v;
}

static esp_err_t sim_init(i2c_master_bus_handle_t bus)
{
    (void)bus;   /* el sensor simulado no usa el bus I2C */
    s_value = SIM_INIT;
    s_ready = true;
    ESP_LOGI(TAG, "Sensor simulado listo (lecturas sinteticas)");
    return ESP_OK;
}

static esp_err_t sim_read(float *out)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Variacion en [-20, +20] respecto a la lectura anterior. */
    int delta = (int)(esp_random() % 41) - 20;
    s_value = clamp(s_value + delta);
    *out = (float)s_value;
    return ESP_OK;
}

static void sim_deinit(void)
{
    s_ready = false;
}

const i2c_sensor_drv_t simulated_driver = {
    .name      = "Simulado",
    .key       = "CO_LEVEL",
    .unit      = "ppm",
    .decimals  = 0,
    .needs_bus = false,
    .init      = sim_init,
    .read      = sim_read,
    .deinit    = sim_deinit,
};
