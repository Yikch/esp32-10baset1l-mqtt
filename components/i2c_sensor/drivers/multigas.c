/*
 * Driver (PLANTILLA): DFRobot Gravity Multi-Gas Sensor - implementacion scaffold.
 *
 * Este fichero es una PLANTILLA, no un driver funcional. Sirve de ejemplo
 * concreto de los 3 pasos para anadir un sensor al bus I2C compartido:
 *
 *   1. Implementar este driver (init/read/deinit) sobre el bus que recibe init().
 *   2. Declarar sus opciones en components/i2c_sensor/Kconfig (direccion, gas...).
 *   3. Registrar 'multigas_driver' en la tabla s_drivers[] de i2c_sensor.c bajo
 *      su guarda CONFIG_I2C_SENSOR_MULTIGAS_ENABLE (ya hecho).
 *
 * Para completarlo, portar el protocolo del driver oficial de DFRobot
 * (DFRobot_MultiGasSensor, https://github.com/DFRobot/DFRobot_MultiGasSensor):
 * el sensor usa tramas de comando de 9 bytes con checksum sobre I2C para
 * seleccionar el modo de adquisicion y leer la concentracion del gas. Mientras
 * el protocolo no este portado, read() devuelve ESP_ERR_NOT_SUPPORTED, de modo
 * que i2c_sensor lo marca inactivo sin afectar al resto de sensores del bus.
 *
 * Esta plantilla esta DESHABILITADA por defecto (Kconfig). Habilitarla sin
 * completar el protocolo dejara el sensor permanentemente inactivo (esperado).
 */
#include "multigas.h"

#include "esp_log.h"
#include "sdkconfig.h"

#define MULTIGAS_TIMEOUT_MS  100   /* timeout por transaccion I2C */

static const char *TAG = "multigas";

static i2c_master_dev_handle_t s_dev;

static esp_err_t multigas_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CONFIG_I2C_SENSOR_MULTIGAS_ADDR,
        .scl_speed_hz    = CONFIG_I2C_SENSOR_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus_add_device: %s", esp_err_to_name(err));
        return err;
    }
    err = i2c_master_probe(bus, CONFIG_I2C_SENSOR_MULTIGAS_ADDR, MULTIGAS_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MultiGasSensor no responde en 0x%02x: %s",
                 CONFIG_I2C_SENSOR_MULTIGAS_ADDR, esp_err_to_name(err));
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return err;
    }

    /* TODO(multigas): seleccionar el modo de adquisicion (PASSIVITY/INITIATIVE)
     * y, si procede, activar la compensacion por temperatura, segun el driver
     * oficial de DFRobot. */
    ESP_LOGW(TAG, "Driver MultiGas es una PLANTILLA: protocolo sin portar");
    return ESP_OK;
}

static esp_err_t multigas_read(float *out)
{
    (void)out;
    /* TODO(multigas): construir la trama de comando de lectura de concentracion
     * (con checksum), enviarla, leer la respuesta y decodificar el valor del gas
     * y su factor decimal segun DFRobot_MultiGasSensor. */
    return ESP_ERR_NOT_SUPPORTED;
}

static void multigas_deinit(void)
{
    if (s_dev != NULL) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
}

const i2c_sensor_drv_t multigas_driver = {
    .name      = "DFRobot MultiGasSensor (plantilla)",
    .key       = "GAS",     /* TODO: ajustar a la especie configurada (CO, H2S...) */
    .unit      = "ppm",
    .decimals  = 1,
    .needs_bus = true,
    .init      = multigas_init,
    .read      = multigas_read,
    .deinit    = multigas_deinit,
};
