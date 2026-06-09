# Cada driver se compila solo si su sensor esta habilitado en menuconfig: asi
# los .c de drivers no referencian simbolos CONFIG_..._ADDR inexistentes cuando
# el sensor esta desactivado. Coherente con las guardas #if de i2c_sensor.c.
set(srcs "i2c_sensor.c")

if(CONFIG_I2C_SENSOR_OXYGEN_ENABLE)
    list(APPEND srcs "drivers/oxygen_sen0322.c")
endif()
if(CONFIG_I2C_SENSOR_OZONE_ENABLE)
    list(APPEND srcs "drivers/ozone_sen0321.c")
endif()
if(CONFIG_I2C_SENSOR_MULTIGAS_ENABLE)
    list(APPEND srcs "drivers/multigas.c")
endif()
if(CONFIG_I2C_SENSOR_SIMULATED_ENABLE)
    list(APPEND srcs "drivers/simulated.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "drivers"
    PRIV_REQUIRES esp_driver_i2c esp_hw_support
)
