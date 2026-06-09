/*
 * Driver (PLANTILLA): DFRobot Gravity Multi-Gas Sensor (DFRobot_MultiGasSensor).
 *
 * Scaffold para demostrar como se anade un sensor nuevo al bus I2C compartido
 * sin tocar el nucleo del componente. La logica de protocolo esta marcada como
 * TODO: usar esta plantilla como punto de partida al integrar el sensor real.
 * Ver i2c_sensor_drv.h y la guia "Anadir un sensor I2C" del README.
 */
#pragma once

#include "i2c_sensor_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const i2c_sensor_drv_t multigas_driver;

#ifdef __cplusplus
}
#endif
