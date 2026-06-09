/*
 * Driver: DFRobot Gravity I2C Ozone Sensor (SEN0321).
 *
 * Sensor electroquimico de O3 (0-10 ppm, resolucion 0,01 ppm segun datasheet;
 * el bus transporta ppb enteros) con interfaz I2C.
 * Direccion 0x70-0x73 por DIP A1/A0 (2 bits, lineas en pull-up: OFF = 1; con
 * ambos interruptores en OFF responde en 0x73, valor por defecto). La medida se
 * entrega en ppb.
 * Migrado del antiguo backend I2C de co_sensor. Ver i2c_sensor_drv.h.
 */
#pragma once

#include "i2c_sensor_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const i2c_sensor_drv_t ozone_sen0321_driver;

#ifdef __cplusplus
}
#endif
