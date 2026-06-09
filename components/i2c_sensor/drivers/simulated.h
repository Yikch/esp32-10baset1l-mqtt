/*
 * Driver: sensor simulado (sin hardware).
 *
 * Genera lecturas sinteticas (recorrido aleatorio acotado) para validar la
 * aplicacion sin sensores fisicos. No usa el bus I2C (needs_bus = false), por lo
 * que el componente funciona aunque no haya bus ni Qwiic conectado. Publica en el
 * campo "CO_LEVEL" para mantener la compatibilidad del build por defecto con el
 * contrato historico del gateway. Ver i2c_sensor_drv.h.
 */
#pragma once

#include "i2c_sensor_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const i2c_sensor_drv_t simulated_driver;

#ifdef __cplusplus
}
#endif
