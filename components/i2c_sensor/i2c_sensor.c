/*
 * Componente: i2c_sensor - nucleo (bus compartido + registro de drivers).
 *
 * Responsabilidades:
 *   - Crear y destruir el bus I2C maestro compartido por todos los sensores.
 *   - Mantener la TABLA DE DRIVERS habilitados (s_drivers) y, a partir de ella,
 *     inicializar, leer y liberar cada sensor de forma uniforme.
 *
 * El nucleo no conoce ningun sensor concreto: trata cada driver a traves del
 * contrato i2c_sensor_drv_t. Anadir un sensor nuevo se reduce a registrar su
 * descriptor en s_drivers[] (ver la guia "Anadir un sensor I2C" del README).
 */
#include "i2c_sensor.h"
#include "i2c_sensor_drv.h"

#include "esp_log.h"
#include "sdkconfig.h"

/* ---- Tabla de drivers ------------------------------------------------------
 * Cada driver expone una instancia constante de i2c_sensor_drv_t (ver su .h).
 * Se incluyen y registran bajo su guarda de Kconfig, de modo que solo se
 * compilan los sensores habilitados en menuconfig. */

#if CONFIG_I2C_SENSOR_OXYGEN_ENABLE
#include "oxygen_sen0322.h"
#endif
#if CONFIG_I2C_SENSOR_OZONE_ENABLE
#include "ozone_sen0321.h"
#endif
#if CONFIG_I2C_SENSOR_MULTIGAS_ENABLE
#include "multigas.h"
#endif
#if CONFIG_I2C_SENSOR_SIMULATED_ENABLE
#include "simulated.h"
#endif

static const i2c_sensor_drv_t *const s_drivers[] = {
#if CONFIG_I2C_SENSOR_OXYGEN_ENABLE
    &oxygen_sen0322_driver,
#endif
#if CONFIG_I2C_SENSOR_OZONE_ENABLE
    &ozone_sen0321_driver,
#endif
#if CONFIG_I2C_SENSOR_MULTIGAS_ENABLE
    &multigas_driver,
#endif
#if CONFIG_I2C_SENSOR_SIMULATED_ENABLE
    &simulated_driver,
#endif
};

#define DRIVER_COUNT  (sizeof(s_drivers) / sizeof(s_drivers[0]))

static const char *TAG = "i2c_sensor";

static i2c_master_bus_handle_t s_bus;
/* +1 evita un array de tamano cero si no hay ningun driver habilitado (la
 * funcion init detecta y reporta esa misconfiguracion). */
static bool                    s_active[DRIVER_COUNT + 1];  /* driver inicializado OK */
static size_t                  s_active_count;
static bool                    s_inited;

/* Crea el bus I2C maestro compartido a partir de la configuracion de menuconfig.
 * Solo se invoca si algun driver habilitado lo necesita (needs_bus). */
static esp_err_t bus_create(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = CONFIG_I2C_SENSOR_PORT,
        .sda_io_num                   = CONFIG_I2C_SENSOR_SDA_GPIO,
        .scl_io_num                   = CONFIG_I2C_SENSOR_SCL_GPIO,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
        s_bus = NULL;
        return err;
    }
    ESP_LOGI(TAG, "Bus I2C compartido listo (puerto %d, SDA=%d SCL=%d, %d Hz)",
             CONFIG_I2C_SENSOR_PORT, CONFIG_I2C_SENSOR_SDA_GPIO,
             CONFIG_I2C_SENSOR_SCL_GPIO, CONFIG_I2C_SENSOR_FREQ_HZ);
    return ESP_OK;
}

esp_err_t i2c_sensor_init(void)
{
    if (DRIVER_COUNT == 0) {
        ESP_LOGE(TAG, "Ningun sensor I2C habilitado en menuconfig");
        return ESP_ERR_INVALID_STATE;
    }

    /* El bus solo se crea si algun driver habilitado lo usa, de modo que el
     * driver simulado funcione aunque no haya hardware ni bus configurado. */
    bool need_bus = false;
    for (size_t i = 0; i < DRIVER_COUNT; i++) {
        if (s_drivers[i]->needs_bus) {
            need_bus = true;
            break;
        }
    }
    if (need_bus && s_bus == NULL) {
        esp_err_t err = bus_create();
        if (err != ESP_OK) {
            return err;
        }
    }

    /* Inicializa cada driver. Un fallo individual no aborta el conjunto: el
     * sensor queda inactivo y el resto sigue operativo. */
    s_active_count = 0;
    for (size_t i = 0; i < DRIVER_COUNT; i++) {
        const i2c_sensor_drv_t *drv = s_drivers[i];
        i2c_master_bus_handle_t bus = drv->needs_bus ? s_bus : NULL;
        esp_err_t err = drv->init(bus);
        s_active[i] = (err == ESP_OK);
        if (s_active[i]) {
            s_active_count++;
            ESP_LOGI(TAG, "Sensor activo: %s (campo \"%s\", %s)",
                     drv->name, drv->key, drv->unit);
        } else {
            ESP_LOGW(TAG, "Sensor no disponible: %s (%s)",
                     drv->name, esp_err_to_name(err));
        }
    }

    s_inited = true;
    if (s_active_count == 0) {
        ESP_LOGE(TAG, "Ningun sensor I2C respondio");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "i2c_sensor listo: %u de %u sensores activos",
             (unsigned)s_active_count, (unsigned)DRIVER_COUNT);
    return ESP_OK;
}

esp_err_t i2c_sensor_read(i2c_sensor_reading_t *out, size_t max, size_t *n_out)
{
    if (out == NULL || n_out == NULL || max == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t n = 0;
    for (size_t i = 0; i < DRIVER_COUNT && n < max; i++) {
        if (!s_active[i]) {
            continue;
        }
        const i2c_sensor_drv_t *drv = s_drivers[i];
        float value = 0.0f;
        esp_err_t err = drv->read(&value);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Lectura fallida de %s: %s",
                     drv->name, esp_err_to_name(err));
            continue;
        }
        out[n].key      = drv->key;
        out[n].unit     = drv->unit;
        out[n].value    = value;
        out[n].decimals = drv->decimals;
        ESP_LOGD(TAG, "%s = %.*f %s", drv->key, drv->decimals, value, drv->unit);
        n++;
    }

    *n_out = n;
    return (n > 0) ? ESP_OK : ESP_FAIL;
}

size_t i2c_sensor_count(void)
{
    return s_active_count;
}

void i2c_sensor_deinit(void)
{
    for (size_t i = 0; i < DRIVER_COUNT; i++) {
        if (s_active[i]) {
            s_drivers[i]->deinit();
            s_active[i] = false;
        }
    }
    s_active_count = 0;
    if (s_bus != NULL) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    s_inited = false;
}
