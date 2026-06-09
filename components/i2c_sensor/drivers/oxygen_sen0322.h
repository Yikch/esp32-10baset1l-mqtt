/*
 * Driver: DFRobot Gravity I2C Oxygen Sensor (SEN0322).
 *
 * Sensor electroquimico de O2 (0-25 %vol) con interfaz I2C. Direccion 0x70-0x73
 * seleccionable por DIP. Ver i2c_sensor_drv.h para el contrato de driver.
 */
#pragma once

#include "i2c_sensor_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const i2c_sensor_drv_t oxygen_sen0322_driver;

#ifdef __cplusplus
}
#endif
