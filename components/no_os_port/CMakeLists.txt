idf_component_register(
    SRCS "no_os_spi.c"
         "no_os_gpio.c"
         "no_os_delay.c"
         "no_os_alloc.c"
         "no_os_crc8.c"
         "oa_tc6.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio
)

# oa_tc6.c es codigo de terceros (Analog Devices, no-OS), integrado sin
# modificar. Se toleran sus advertencias solo en ese fichero; el resto del
# shim (no_os_*.c) mantiene los avisos como errores.
set_source_files_properties(oa_tc6.c PROPERTIES COMPILE_OPTIONS "-Wno-error")
