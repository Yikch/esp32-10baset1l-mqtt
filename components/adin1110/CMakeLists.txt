idf_component_register(
    SRCS "adin1110.c"
    INCLUDE_DIRS "include"
    REQUIRES no_os_port
)

# adin1110.c es codigo de terceros (Analog Devices, no-OS), integrado sin
# modificar para poder seguir el upstream.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
