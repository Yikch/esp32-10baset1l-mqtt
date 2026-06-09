/*
 * Driver: DFRobot Gravity I2C Ozone Sensor (SEN0321) - implementacion.
 *
 * Protocolo (DFRobot_OzoneSensor): en modo automatico se escribe el registro de
 * modo (0x03 <- 0x00) en la inicializacion y se leen 2 bytes del registro 0x09;
 * concentracion_ppb = (msb << 8) | lsb. Direccion I2C por defecto 0x73.
 *
 * La medida se entrega en PARTES POR MIL MILLONES (ppb), unidad nativa del
 * sensor (0-10000 ppb = 0-10 ppm). Migrado sin cambios funcionales desde el
 * antiguo backend I2C de co_sensor.
 */
#include "ozone_sen0321.h"

#include "esp_log.h"
#include "sdkconfig.h"

#define OZONE_REG_MODE        0x03   /* registro de modo de medida            */
#define OZONE_REG_AUTO_DATA   0x09   /* datos de concentracion (modo auto)    */
#define OZONE_MODE_AUTOMATIC  0x00   /* medida continua automatica            */
#define OZONE_PPB_MAX         10000  /* fondo de escala: 10 ppm = 10000 ppb   */
#define OZONE_TIMEOUT_MS      100    /* timeout por transaccion I2C           */

static const char *TAG = "ozone_sen0321";

static i2c_master_dev_handle_t s_dev;

static esp_err_t ozone_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CONFIG_I2C_SENSOR_OZONE_ADDR,
        .scl_speed_hz    = CONFIG_I2C_SENSOR_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus_add_device: %s", esp_err_to_name(err));
        return err;
    }
    err = i2c_master_probe(bus, CONFIG_I2C_SENSOR_OZONE_ADDR, OZONE_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SEN0321 no responde en 0x%02x: %s",
                 CONFIG_I2C_SENSOR_OZONE_ADDR, esp_err_to_name(err));
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return err;
    }

    /* Modo de medida automatica (lectura continua desde el registro 0x09). */
    uint8_t mode_cmd[2] = { OZONE_REG_MODE, OZONE_MODE_AUTOMATIC };
    err = i2c_master_transmit(s_dev, mode_cmd, sizeof(mode_cmd), OZONE_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configuracion de modo automatico fallida: %s",
                 esp_err_to_name(err));
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return err;
    }
    ESP_LOGI(TAG, "SEN0321 (O3) listo en 0x%02x", CONFIG_I2C_SENSOR_OZONE_ADDR);
    ESP_LOGW(TAG, "Sensor electroquimico: requiere precalentamiento para "
                  "lecturas estables");
    return ESP_OK;
}

static esp_err_t ozone_read(float *out)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Promedio de varias lecturas para atenuar el ruido (F-03). */
    int64_t acc = 0;
    for (int i = 0; i < CONFIG_I2C_SENSOR_OZONE_SAMPLES; i++) {
        uint8_t reg = OZONE_REG_AUTO_DATA;
        uint8_t buf[2] = { 0 };
        esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1,
                                                    buf, sizeof(buf),
                                                    OZONE_TIMEOUT_MS);
        if (err != ESP_OK) {
            return err;
        }
        acc += ((int)buf[0] << 8) | buf[1];
    }
    int ppb = (int)(acc / CONFIG_I2C_SENSOR_OZONE_SAMPLES);

    if (ppb < 0) {
        ppb = 0;
    } else if (ppb > OZONE_PPB_MAX) {
        ppb = OZONE_PPB_MAX;
    }
    *out = (float)ppb;
    return ESP_OK;
}

static void ozone_deinit(void)
{
    if (s_dev != NULL) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
}

const i2c_sensor_drv_t ozone_sen0321_driver = {
    .name      = "DFRobot SEN0321 (O3)",
    .key       = "O3",
    .unit      = "ppb",
    .decimals  = 0,
    .needs_bus = true,
    .init      = ozone_init,
    .read      = ozone_read,
    .deinit    = ozone_deinit,
};
