/*
 * Componente: i2c_sensor - interfaz interna de driver.
 *
 * Contrato que debe cumplir cada sensor del bus. Cada driver vive en su propio
 * fichero drivers/<sensor>.c, expone una unica instancia constante de
 * i2c_sensor_drv_t y se registra en la tabla de i2c_sensor.c bajo su guarda de
 * Kconfig. El nucleo del componente (bus + bucle de lectura) no conoce los
 * detalles de ningun sensor concreto: solo invoca estos punteros de funcion.
 *
 * Esta cabecera es de uso interno del componente (drivers + nucleo); la
 * aplicacion usa unicamente i2c_sensor.h.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Descriptor de un driver de sensor I2C.
 *
 * Los campos de metadatos (name, key, unit, decimals) son estaticos y describen
 * la medida que produce el sensor. Los punteros de funcion implementan su ciclo
 * de vida sobre el bus compartido.
 */
typedef struct i2c_sensor_drv {
    const char *name;       /* nombre legible para los logs (modelo del sensor)   */
    const char *key;        /* clave del campo JSON publicado (p. ej. "O2")       */
    const char *unit;       /* unidad fisica de la medida (p. ej. "%vol")         */
    uint8_t     decimals;   /* cifras decimales recomendadas para el valor        */
    bool        needs_bus;  /* true si el sensor usa el bus I2C compartido;
                               false para drivers sin hardware (p. ej. simulado)  */

    /**
     * @brief Inicializa el sensor: anade su dispositivo al bus y lo configura.
     * @param bus Bus maestro compartido (NULL si needs_bus == false).
     * @return ESP_OK si el sensor queda listo para leer.
     */
    esp_err_t (*init)(i2c_master_bus_handle_t bus);

    /**
     * @brief Toma una medida del sensor.
     * @param[out] out Valor medido, en las unidades indicadas por 'unit'.
     * @return ESP_OK si la lectura es valida.
     */
    esp_err_t (*read)(float *out);

    /**
     * @brief Libera los recursos del sensor (quita su dispositivo del bus).
     */
    void (*deinit)(void);
} i2c_sensor_drv_t;

#ifdef __cplusplus
}
#endif
