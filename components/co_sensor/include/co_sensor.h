/*
 * Componente: co_sensor
 * Driver de SENSORES DE GAS ANALOGICOS (ADC) - F-03.
 *
 * Lee un sensor de gas analogico (p. ej. DFRobot Gravity Gas Sensor v2) por la
 * unidad ADC1 del ESP32 y devuelve la concentracion en ppm (0-2000).
 *
 * Los sensores I2C se han trasladado al componente i2c_sensor (modular,
 * multi-sensor sobre el bus Qwiic). co_sensor queda exclusivamente como driver
 * de sensores ADC.
 *
 * *** PENDIENTE DE COMPROBACION Y PRUEBA ***
 * No se probaran sensores ADC sobre la placa SparkFun MicroMod ESP32 por
 * limitaciones de hardware: el banco usa el sensor I2C por Qwiic. Este
 * componente se conserva como driver autonomo (no cableado en app_main) para un
 * futuro nodo con sensor analogico. La conversion tension->ppm es provisional y
 * requiere calibracion con gas patron antes de usarse en produccion.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Rango de medida en ppm (F-03). */
#define CO_SENSOR_PPM_MIN  0
#define CO_SENSOR_PPM_MAX  2000

/**
 * @brief Inicializa el sensor ADC (unidad ADC1 y calibracion si esta disponible).
 *
 * @return ESP_OK o codigo de error de la capa ADC.
 */
esp_err_t co_sensor_init(void);

/**
 * @brief Lee la concentracion de gas en ppm.
 *
 * Promedia varias conversiones para atenuar el ruido. El valor devuelto se
 * satura al rango [CO_SENSOR_PPM_MIN, CO_SENSOR_PPM_MAX].
 *
 * @param[out] ppm_out  Concentracion en ppm.
 * @return ESP_OK; ESP_ERR_INVALID_ARG si ppm_out es NULL;
 *         ESP_ERR_INVALID_STATE si no se ha inicializado.
 */
esp_err_t co_sensor_read_ppm(int *ppm_out);

/**
 * @brief Libera los recursos del sensor (unidad ADC, calibracion).
 */
void co_sensor_deinit(void);

#ifdef __cplusplus
}
#endif
