/*
 * Componente: co_sensor - implementacion.
 *
 * Driver para SENSORES DE GAS ANALOGICOS (ADC). Lee la salida de tension de un
 * sensor analogico (p. ej. DFRobot Gravity Gas Sensor v2) por la unidad ADC1 del
 * ESP32 y la convierte a ppm.
 *
 * NOTA: los sensores I2C se han movido al componente i2c_sensor (modular,
 * multi-sensor sobre el bus Qwiic). co_sensor se conserva exclusivamente como
 * driver de sensores ADC.
 *
 * *** PENDIENTE DE COMPROBACION Y PRUEBA ***
 * Sobre la placa SparkFun MicroMod ESP32 NO se van a probar sensores ADC por
 * limitaciones de hardware (el banco usa el sensor I2C por Qwiic). Por tanto:
 *   - La conversion tension->ppm es un modelo lineal PROVISIONAL sin calibrar.
 *   - Este componente no esta cableado en app_main; queda como driver autonomo
 *     para un futuro nodo con sensor analogico real.
 * Verificar el canal/atenuacion, calibrar con gas patron y validar en hardware
 * antes de darlo por bueno.
 */
#include "co_sensor.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "co_sensor";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static bool                      s_cali_ok;
static bool                      s_ready;

/* Satura un valor al rango de medida en ppm (F-03). */
static int clamp_ppm(int ppm)
{
    if (ppm < CO_SENSOR_PPM_MIN) {
        return CO_SENSOR_PPM_MIN;
    }
    if (ppm > CO_SENSOR_PPM_MAX) {
        return CO_SENSOR_PPM_MAX;
    }
    return ppm;
}

/*
 * Conversion tension -> ppm.
 *
 * TODO(calibracion): modelo lineal provisional. El sensor de CO real exige
 * calibracion con gas patron; ajustar CO_SENSOR_CLEAN_AIR_MV (offset de aire
 * limpio) y CO_SENSOR_MV_PER_PPM (sensibilidad) en menuconfig una vez medidos.
 */
static int voltage_to_ppm(int mv)
{
    int delta_mv = mv - CONFIG_CO_SENSOR_CLEAN_AIR_MV;
    if (delta_mv <= 0) {
        return CO_SENSOR_PPM_MIN;
    }
    /* sensibilidad en uV/ppm -> ppm = delta_mv * 1000 / (uV/ppm). */
    int ppm = (delta_mv * 1000) / CONFIG_CO_SENSOR_MV_PER_PPM;
    return clamp_ppm(ppm);
}

static esp_err_t cali_init(void)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = CONFIG_CO_SENSOR_ADC_CHANNEL,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    return adc_cali_create_scheme_curve_fitting(&cfg, &s_cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    return adc_cali_create_scheme_line_fitting(&cfg, &s_cali);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t co_sensor_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,            /* rango ~0-3.1 V */
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, CONFIG_CO_SENSOR_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return err;
    }

    s_cali_ok = (cali_init() == ESP_OK);
    if (!s_cali_ok) {
        ESP_LOGW(TAG, "Sin calibracion ADC; se usa el valor crudo como mV");
    }

    s_ready = true;
    ESP_LOGI(TAG, "Backend ADC listo (ADC1 canal %d, %d muestras/lectura)",
             CONFIG_CO_SENSOR_ADC_CHANNEL, CONFIG_CO_SENSOR_ADC_SAMPLES);
    ESP_LOGW(TAG, "co_sensor (ADC) PENDIENTE de calibracion y prueba en hardware");
    return ESP_OK;
}

esp_err_t co_sensor_read_ppm(int *ppm_out)
{
    if (ppm_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Promedio de varias conversiones para atenuar el ruido del sensor. */
    int64_t acc = 0;
    for (int i = 0; i < CONFIG_CO_SENSOR_ADC_SAMPLES; i++) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_adc, CONFIG_CO_SENSOR_ADC_CHANNEL, &raw);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "adc_oneshot_read: %s", esp_err_to_name(err));
            return err;
        }
        acc += raw;
    }
    int raw_avg = (int)(acc / CONFIG_CO_SENSOR_ADC_SAMPLES);

    int mv = raw_avg;
    if (s_cali_ok) {
        esp_err_t err = adc_cali_raw_to_voltage(s_cali, raw_avg, &mv);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "adc_cali_raw_to_voltage: %s", esp_err_to_name(err));
            return err;
        }
    }

    *ppm_out = voltage_to_ppm(mv);
    ESP_LOGD(TAG, "raw=%d mv=%d -> %d ppm", raw_avg, mv, *ppm_out);
    return ESP_OK;
}

void co_sensor_deinit(void)
{
    if (s_cali_ok) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(s_cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(s_cali);
#endif
        s_cali_ok = false;
    }
    if (s_adc != NULL) {
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
    }
    s_ready = false;
}
