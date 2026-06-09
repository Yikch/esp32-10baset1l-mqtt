# Driver del fabricante para el The Gravity: I2C Oxygen Sensor DFRobot (SEN0322)
Enlace: https://github.com/DFRobot/DFRobot_OxygenSensor/tree/master

The Gravity: I2C Oxygen Sensor is based on electrochemical principles and it can measure the ambient O2 concentration accurately and conveniently. Its effective range is 0~25%Vol. The sensor boasts high precision, high sensitivity, wide linear range, high anti-interference ability, high stability, and good repeatability. When equipped with an I2C interface, the sensor can read the ambient O2 concentration easily, meanwhile, it can also work with various MCU and sensors. This Arduino-compatible oxygen sensor can be widely applied to fields like industries, mines, warehouses, and other spaces where the air is not easy to circulate as well as to measure the oxygen concentration in the environment.

Product Link（https://www.dfrobot.com/product-2052.html）

````cpp
/*!
 * @file DFRobot_OxygenSensor.cpp
 * @brief Define the basic struct of DFRobot_OxygenSensor class, the implementation of basic method
 * @copyright	Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @license The MIT License (MIT)
 * @author ZhixinLiu(zhixin.liu@dfrobot.com)
 * @version V1.0
 * @date 2022-08-02
 * @url https://github.com/DFRobot/DFRobot_OxygenSensor
 */
#include "DFRobot_OxygenSensor.h"

DFRobot_OxygenSensor::DFRobot_OxygenSensor(TwoWire *pWire)
{
  this->_pWire = pWire;
}

DFRobot_OxygenSensor::~DFRobot_OxygenSensor()
{
  this->_pWire = NULL;
}

bool DFRobot_OxygenSensor::begin(uint8_t addr)
{
  this->_addr = addr;
  _pWire->begin();
  _pWire->beginTransmission(_addr);
  if(_pWire->endTransmission() == 0){
    uint8_t version = getVersion();
    if(version == 0xFF){
      this->_version = eOldVersion;
    }else if(version == 0x01){
      this->_version = eNewVersion;
    }
    return true;
  }
  return false;
}

void DFRobot_OxygenSensor::readFlash()
{
  uint8_t value[2] ={0};
  _pWire->beginTransmission(_addr);
  _pWire->write(GET_KEY_REGISTER);
  _pWire->endTransmission();
  delay(50);
  _pWire->requestFrom(_addr, (uint8_t)2);
  int i = 0;
  while (_pWire->available()){
    value[i++] = _pWire->read();
  }
  uint16_t temp = ((uint16_t)value[1] << 8) | value[0];
  if(temp == 0){
    this->_Key = 20.9 / 120.0;
  }else{
    this->_Key = (float)temp / 1000.0;
  }
}

void DFRobot_OxygenSensor::i2cWrite(uint8_t reg, uint8_t data)
{
  _pWire->beginTransmission(_addr);
  _pWire->write(reg);
  _pWire->write(data);
  _pWire->endTransmission();
}

void DFRobot_OxygenSensor::calibrate(float vol, float mv)
{
  uint16_t keyValue = vol * 10;
  uint8_t keytemp = 0;
  if(mv < 0.000001 && mv > (-0.000001) ) {
    keytemp = ((uint8_t)keyValue & 0xff);
    i2cWrite(USER_SET_REGISTER, keytemp);
  }else {
    keyValue = (vol / mv) * 1000;
    if(_version == eOldVersion){
      if(keyValue <= 255){
        keytemp = ((uint8_t)keyValue & 0xff);
      }else{
        keytemp = 255;
      }
      i2cWrite(AUTUAL_SET_REGISTER, keytemp);
    }else{
      _pWire->beginTransmission(_addr);
      _pWire->write(AUTUAL_SET_REGISTER_);
      _pWire->write(keyValue & 0xFF);
      _pWire->write((keyValue >> 8) & 0xFF);
      _pWire->endTransmission();
    }
  }
}

float DFRobot_OxygenSensor::getOxygenData(uint8_t collectNum)
{
  uint8_t rxbuf[10]={0}, k = 0;
  static uint8_t i = 0, j = 0;
  readFlash();
  if(collectNum > 0){
    for(j = collectNum - 1;  j > 0; j--) {  oxygenData[j] = oxygenData[j-1]; }
    _pWire->beginTransmission(_addr);
    _pWire->write(OXYGEN_DATA_REGISTER);
    _pWire->endTransmission();
    delay(100);
    _pWire->requestFrom(_addr, (uint8_t)3);
      while (_pWire->available()){
        rxbuf[k++] = _pWire->read();
      }
    oxygenData[0] = ((_Key) * (((float)rxbuf[0]) + ((float)rxbuf[1] / 10.0) + ((float)rxbuf[2] / 100.0)));
    if(i < collectNum) i++;
    return getAverageNum(oxygenData, i);
  }else {
    return -1.0;
  }
}

float DFRobot_OxygenSensor::getAverageNum(float bArray[], uint8_t len)
{
  uint8_t i = 0;
  double bTemp = 0;
  for(i = 0; i < len; i++) {
    bTemp += bArray[i];
  }
  return bTemp / (float)len;
}

eProbeLife_t DFRobot_OxygenSensor::checkProbeLife(void)
{
  int8_t value = 0;
  if(_version == eOldVersion){
    return eVersionError;
  }
  _pWire->beginTransmission(_addr);
  _pWire->write(PROBE_LIFE_REGISTER);
  _pWire->endTransmission();
  _pWire->requestFrom(_addr, (uint8_t)1);
  if(_pWire->available()){
    value = _pWire->read();
  }
  return (eProbeLife_t)value;
}

uint8_t DFRobot_OxygenSensor::getVersion(void)
{
  uint8_t value = 0;
  _pWire->beginTransmission(_addr);
  _pWire->write(VERSION_REGISTER);
  _pWire->endTransmission();
  _pWire->requestFrom(_addr, (uint8_t)1);
  if(_pWire->available()){
    value = _pWire->read();
  }
  return value;
}

float DFRobot_OxygenSensor::getCurrentData(void)
{
  float crrData = 0.0;
  uint8_t rxbuf[10]={0}, k = 0;

  _pWire->beginTransmission(_addr);
  _pWire->write(OXYGEN_DATA_REGISTER);
  _pWire->endTransmission();
  delay(100);
  _pWire->requestFrom(_addr, (uint8_t)3);
  while (_pWire->available()){
    rxbuf[k++] = _pWire->read();
  }
  crrData = ((float)rxbuf[0]) + ((float)rxbuf[1] / 10.0) + ((float)rxbuf[2] / 100.0);
  delay(50);
  return crrData;
}
````

````h
/*!
 * @file DFRobot_OxygenSensor.h
 * @brief Define basic struct of DFRobot_OxygenSensor class
 * @details This is an electrochemical oxygen sensor, I2C address can be changed by a dip switch, and the oxygen concentration can be obtained through I2C.
 * @copyright	Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @license The MIT License (MIT)
 * @author ZhixinLiu(zhixin.liu@dfrobot.com)
 * @version V1.0
 * @date 2023-08-02
 * @url https://github.com/DFRobot/DFRobot_OxygenSensor
 */
#ifndef __DFRobot_OxygenSensor_H__
#define __DFRobot_OxygenSensor_H__

#include <Arduino.h>
#include <Wire.h>

#define ADDRESS_0   0x70
#define ADDRESS_1   0x71
#define ADDRESS_2   0x72
#define ADDRESS_3   0x73  ///< iic slave Address select
#define OCOUNT      100   ///< oxygen Count Value
#define OXYGEN_DATA_REGISTER 0x03   ///< register for oxygen data
#define USER_SET_REGISTER    0x08   ///< register for users to configure key value manually
#define AUTUAL_SET_REGISTER  0x09   ///< register that automatically configure key value
#define GET_KEY_REGISTER     0x0A   ///< register for obtaining key value

//      GET_KEY_REGISTER_L 0x0A
//      GET_KEY_REGISTER_H 0x0B
#define AUTUAL_SET_REGISTER_  0x0C ///< Register for automatic configuration of key values in v1.0.2
//      AUTUAL_SET_REGISTER_L = 0xC
//      AUTUAL_SET_REGISTER_H = 0xD
#define PROBE_LIFE_REGISTER  0x0E   ///< register for probe life status
#define VERSION_REGISTER 0x0F   ///< register for obtaining version information

/**
 * @enum eVersion_t
 * @brief Sensor version type
 */
typedef enum{
  eOldVersion = 0x00,
  eNewVersion = 0x01
}eVersion_t;

/**
 * @enum eProbeLife_t
 * @brief Probe lifespan status type
*/
typedef enum{
  eVersionError = -1,
  eProbenExhausted = 0,
  eProbenNormal = 1
}eProbeLife_t;

class DFRobot_OxygenSensor
{
public:
  DFRobot_OxygenSensor(TwoWire *pWire = &Wire);
  ~DFRobot_OxygenSensor();
  /**
   * @fn begin
   * @brief Initialize i2c
   * @param addr i2c device address
   * @n     Default to use i2c address of 0x70 without passing parameters
   * @return None
   */
  bool begin(uint8_t addr = ADDRESS_0);

  /**
   * @fn calibrate
   * @brief Calibrate oxygen sensor
   * @param vol oxygen concentration unit vol
   * @param mv calibrated voltage unit mv
   * @return None
   */
  void calibrate(float vol, float mv = 0);

  /**
   * @fn getOxygenData
   * @brief Get oxygen concentration
   * @param collectNum The number of data to be smoothed
   * @n     For example, upload 20 and take the average value of the 20 data, then return the concentration data
   * @return Oxygen concentration, unit
   */  
  float getOxygenData(uint8_t collectNum);
  
    /**
   * @fn checkProbeLife
   * @brief Get probe lifespan status
   * @return eProbeLife_t
   * @n     eProbenExhausted    The probe's lifespan has been exhausted.It is recommended to replace the sensor probe.
   * @n     eProbenNormal       The probe's lifespan is normal
   * @n     eVersionError       The sensor is not a new version, so it does not have the probe lifespan status register.
  */
  eProbeLife_t checkProbeLife(void);

  /**
   * getCurrentData
   * @brief Get current data
   * @return float current data
   */
  float getCurrentData(void);

private:
  void readFlash();
  void i2cWrite(uint8_t reg, uint8_t data);
  uint8_t  _addr;                               
  uint8_t _version;
  float _Key = 0.0;                          ///< oxygen key value
  float oxygenData[OCOUNT] = {0.00};
  float getAverageNum(float bArray[], uint8_t len);
  uint8_t getVersion(void);
  TwoWire *_pWire;
};
#endif
```

Ejemplo Arduino:

```ino
/*!
 * @file calibrateOxygenSensor.ino
 * @brief Calibrate oxygen sensor
 * @n step: we must first determine the iic device address, will dial the code switch A0, A1 (ADDRESS_0 for [0 0]), (ADDRESS_1 for [1 0]), (ADDRESS_2 for [0 1]), (ADDRESS_3 for [1 1]).
 * @n Then calibrate the oxygen sensor
 * @n note: It takes about 10 minutes to stablize oxygen concentration.
 * @n The experimental phenomenon is that a certain percentage of oxygen concentration is printed on the serial port.
 * @n Because the oxygen concentration in oxygen air is certain, the data will not be greater than 25% vol.
 * @copyright Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @license The MIT License (MIT)
 * @author ZhixinLiu(zhixin.liu@dfrobot.com)
 * @version V1.0
 * @date 2021-10-22
 * @url https://github.com/DFRobot/DFRobot_OxygenSensor
 */
#include "DFRobot_OxygenSensor.h"

/**
 * i2c slave Address, The default is ADDRESS_3.
 * ADDRESS_0   0x70  i2c device address.
 * ADDRESS_1   0x71
 * ADDRESS_2   0x72
 * ADDRESS_3   0x73
 */
#define Oxygen_IICAddress ADDRESS_3
#define OXYGEN_CONECTRATION 20.9  // The current concentration of oxygen in the air.
#define OXYGEN_MV           0     // The value marked on the sensor, Do not use must be assigned to 0.
DFRobot_OxygenSensor oxygen;

void setup(void) 
{
  Serial.begin(9600);
  while(!oxygen.begin(Oxygen_IICAddress)){
    Serial.println("I2c device number error !");
    delay(1000);
  }
  Serial.println("I2c connect success !");
  
  /**
   * Choose method 1 or method 2 to calibrate the oxygen sensor.
   * 1. Directly calibrate the oxygen sensor by adding two parameters to the sensor.
   * 2. Waiting for stable oxygen sensors for about 10 minutes, 
   *    OXYGEN_CONECTRATION is the current concentration of oxygen in the air (20.9%mol except in special cases),
   *    Not using the first calibration method, the OXYGEN MV must be 0.
   */
  oxygen.calibrate(OXYGEN_CONECTRATION, OXYGEN_MV);
}

void loop(void)
{
  Serial.println("The oxygen sensor was calibrated successfully.");
  delay(1000);
}
```