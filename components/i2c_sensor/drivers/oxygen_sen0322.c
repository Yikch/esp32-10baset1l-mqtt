/*
 * Driver: DFRobot Gravity I2C Oxygen Sensor (SEN0322) - implementacion.
 *
 * Portado del driver oficial Arduino de DFRobot (DFRobot_OxygenSensor) al API
 * i2c_master de ESP-IDF. Referencia: https://github.com/DFRobot/DFRobot_OxygenSensor
 *
 * Protocolo de lectura (igual que getOxygenData del driver Arduino):
 *   1. Leer la "key" de calibracion de la flash del sensor (registro 0x0A):
 *      escribir el registro, esperar y leer 2 bytes. key = temp/1000 (o el valor
 *      por defecto 20.9/120 si la flash esta vacia).
 *   2. Leer la concentracion (registro 0x03): escribir el registro, esperar y
 *      leer 3 bytes. O2[%vol] = key * (b0 + b1/10 + b2/100).
 * Cada transaccion escribe el registro con STOP y luego lee (sin start repetido),
 * con la espera intermedia que exige el sensor.
 */
#include "oxygen_sen0322.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sdkconfig.h"

#define OXY_REG_DATA        0x03   /* registro de datos de oxigeno          */
#define OXY_REG_GET_KEY     0x0A   /* registro de la key de calibracion     */
#define OXY_DATA_DELAY_MS   100    /* espera tras pedir datos (driver DFRobot) */
#define OXY_KEY_DELAY_MS    50     /* espera tras pedir la key                 */
#define OXY_TIMEOUT_MS      100    /* timeout por transaccion I2C            */
#define OXY_DEFAULT_KEY     (20.9f / 120.0f)  /* key si la flash esta vacia  */

static const char *TAG = "oxy_sen0322";

static i2c_master_dev_handle_t s_dev;

/* Escribe un registro (con STOP), espera y lee 'len' bytes. Reproduce la
 * secuencia write/endTransmission/delay/requestFrom del driver Arduino. */
static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len, uint32_t delay_ms)
{
    esp_err_t err = i2c_master_transmit(s_dev, &reg, 1, OXY_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    return i2c_master_receive(s_dev, buf, len, OXY_TIMEOUT_MS);
}

/* Lee la key de calibracion de la flash del sensor. */
static float read_key(void)
{
    uint8_t v[2] = { 0 };
    if (reg_read(OXY_REG_GET_KEY, v, sizeof(v), OXY_KEY_DELAY_MS) != ESP_OK) {
        return OXY_DEFAULT_KEY;
    }
    uint16_t temp = ((uint16_t)v[1] << 8) | v[0];
    return (temp == 0) ? OXY_DEFAULT_KEY : (float)temp / 1000.0f;
}

static esp_err_t oxygen_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CONFIG_I2C_SENSOR_OXYGEN_ADDR,
        .scl_speed_hz    = CONFIG_I2C_SENSOR_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus_add_device: %s", esp_err_to_name(err));
        return err;
    }
    err = i2c_master_probe(bus, CONFIG_I2C_SENSOR_OXYGEN_ADDR, OXY_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SEN0322 no responde en 0x%02x: %s",
                 CONFIG_I2C_SENSOR_OXYGEN_ADDR, esp_err_to_name(err));
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return err;
    }
    ESP_LOGI(TAG, "SEN0322 (O2) listo en 0x%02x", CONFIG_I2C_SENSOR_OXYGEN_ADDR);
    ESP_LOGW(TAG, "Sensor electroquimico: requiere precalentamiento (~min) para "
                  "lecturas estables");
    return ESP_OK;
}

static esp_err_t oxygen_read(float *out)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    float key = read_key();

    /* Promedio de varias lecturas para atenuar el ruido (F-03). */
    float acc = 0.0f;
    int got = 0;
    for (int i = 0; i < CONFIG_I2C_SENSOR_OXYGEN_SAMPLES; i++) {
        uint8_t b[3] = { 0 };
        if (reg_read(OXY_REG_DATA, b, sizeof(b), OXY_DATA_DELAY_MS) != ESP_OK) {
            continue;
        }
        acc += key * ((float)b[0] + (float)b[1] / 10.0f + (float)b[2] / 100.0f);
        got++;
    }
    if (got == 0) {
        return ESP_FAIL;
    }
    *out = acc / (float)got;
    return ESP_OK;
}

static void oxygen_deinit(void)
{
    if (s_dev != NULL) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
}

const i2c_sensor_drv_t oxygen_sen0322_driver = {
    .name      = "DFRobot SEN0322 (O2)",
    .key       = "O2",
    .unit      = "%vol",
    .decimals  = 1,
    .needs_bus = true,
    .init      = oxygen_init,
    .read      = oxygen_read,
    .deinit    = oxygen_deinit,
};
