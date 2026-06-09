/*
 * Componente: i2c_sensor
 * Driver modular de sensores I2C sobre el bus Qwiic (F-03).
 *
 * Este componente sustituye al backend I2C que antes vivia en co_sensor. Su
 * objetivo es soportar VARIOS sensores I2C compartiendo el mismo bus (p. ej.
 * un sensor de O2, uno de O3 y, en el futuro, un DFRobot MultiGasSensor) de
 * forma modular: un unico bus maestro se inicializa una sola vez y cada sensor
 * se implementa como un "driver" independiente registrado en el componente.
 *
 * Para anadir un sensor nuevo NO hace falta tocar la logica del bus: basta con
 * (1) escribir un driver en drivers/ que rellene un i2c_sensor_drv_t, (2) anadir
 * su opcion en Kconfig y (3) registrarlo en la tabla de drivers de i2c_sensor.c.
 * Ver drivers/multigas.c como plantilla y la seccion correspondiente del README.
 *
 * La aplicacion no depende del numero ni del tipo de sensores: lee todas las
 * medidas validas con i2c_sensor_read() y publica una por campo del payload JSON
 * (ver sensor_payload). La unidad de cada medida la define su propio driver.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cota superior de sensores que la aplicacion espera leer en un ciclo. Debe ser
 * >= al numero de drivers que puedan estar habilitados simultaneamente. */
#define I2C_SENSOR_MAX_READINGS  8

/**
 * @brief Una medida tomada de un sensor del bus.
 *
 * Los punteros 'key' y 'unit' apuntan a literales estaticos del driver, por lo
 * que son validos durante toda la vida del programa (no hay que copiarlos).
 */
typedef struct {
    const char *key;       /* clave del campo JSON (p. ej. "O2", "O3", "CO_LEVEL") */
    const char *unit;      /* unidad fisica de la medida (p. ej. "%vol", "ppb")    */
    float       value;     /* valor medido                                         */
    uint8_t     decimals;  /* cifras decimales recomendadas al serializar el valor */
} i2c_sensor_reading_t;

/**
 * @brief Inicializa el bus I2C compartido (si algun driver lo necesita) y todos
 *        los drivers de sensor habilitados en menuconfig.
 *
 * Es tolerante a fallos por sensor: si un sensor concreto no responde, se marca
 * inactivo pero el resto sigue operativo (filosofia modular). Devuelve error
 * solo si no queda ningun sensor utilizable.
 *
 * @return ESP_OK si al menos un sensor quedo activo;
 *         ESP_ERR_INVALID_STATE si no hay ningun driver habilitado;
 *         o el codigo de error de la capa I2C si falla la creacion del bus.
 */
esp_err_t i2c_sensor_init(void);

/**
 * @brief Lee todos los sensores activos en una sola pasada.
 *
 * Recorre los drivers activos y escribe una entrada por cada lectura correcta.
 * Una lectura fallida de un sensor no aborta la pasada: simplemente no aparece
 * en la salida.
 *
 * @param[out] out    Array destino de medidas.
 * @param      max    Capacidad de 'out' (se recomienda I2C_SENSOR_MAX_READINGS).
 * @param[out] n_out  Numero de medidas validas escritas en 'out'.
 * @return ESP_OK si se obtuvo al menos una medida valida;
 *         ESP_ERR_INVALID_ARG si out o n_out son NULL o max es 0;
 *         ESP_ERR_INVALID_STATE si no se ha inicializado el componente;
 *         ESP_FAIL si ningun sensor activo devolvio una medida valida.
 */
esp_err_t i2c_sensor_read(i2c_sensor_reading_t *out, size_t max, size_t *n_out);

/**
 * @brief Numero de drivers de sensor activos (inicializados con exito).
 */
size_t i2c_sensor_count(void);

/**
 * @brief Libera los recursos de todos los drivers y del bus I2C compartido.
 */
void i2c_sensor_deinit(void);

#ifdef __cplusplus
}
#endif
