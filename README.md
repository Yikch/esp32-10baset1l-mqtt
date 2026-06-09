| Target soportado | ESP32 |
| ---------------- | ----- |

# Firmware del nodo sensor de gas — Single Pair Ethernet / MQTT

Firmware ESP-IDF para un nodo sensor que mide concentración de gas y la publica
por **MQTT** hacia un gateway remoto. El enlace es **Single Pair Ethernet
(10BASE-T1L)** mediante el MAC-PHY **ADIN1110**; la red es **exclusivamente por
SPE** (sin Wi-Fi).

El proyecto parte del ejemplo `protocols/mqtt` de ESP-IDF y se ha reestructurado
en componentes. La especificación funcional completa está en
[Especificacion_Firmware_Nodo_Sensor.md](Especificacion_Firmware_Nodo_Sensor.md)
(requisitos `F-01`…`F-13`).

> **Sensores del banco de pruebas.** El nodo lee sus sensores por el bus **I2C /
> Qwiic** de la placa Main, a través del componente **`i2c_sensor`** (modular,
> varios sensores en el mismo bus). El sensor I2C físicamente disponible es el
> **DFRobot Gravity I2C Ozone Sensor (SEN0321, O₃)**; el componente incluye además
> el driver del **DFRobot Gravity I2C Oxygen Sensor (SEN0322, O₂)** y una
> plantilla para un **DFRobot MultiGasSensor**. Sin hardware conectado, arranca
> por defecto un **sensor simulado**. Ver [Sensores](#sensores-soportados).

---

> **Estado actual.** Al arrancar se inicializa el ADIN1110 por SPI y se verifica
> la comunicación leyendo el **PHY ID** (`0x0283BC91`); el chip se integra como
> interfaz `esp_netif` de clase Ethernet con **IP estática** (componente
> `eth_adin1110`, F-01/F-02). **En paralelo** corre el camino de datos (sensores
> + MQTT). Un fallo de comunicación con un sensor I2C **no tira el nodo**: se
> reintenta y se notifica en el campo `"sensor"` del topic de estado. Queda
> pendiente la **subida efectiva del enlace 10BASE-T1L** contra el gateway (que
> dispara la obtención de IP y, con ella, el arranque de MQTT).

## Arquitectura

### Capas software

```
+-----------------------------------------------+
|  app_main : bring-up SPE + camino de datos     |  main/
+------------------------+----------------------+
|  eth_adin1110           |  app_config          |  components/  (codigo propio)
|  (esp_netif Ethernet +  |  i2c_sensor          |
|   IP estatica + monitor)|  co_sensor (ADC)     |
|                         |  sensor_payload      |
+------------------------+----------------------+
|  esp_netif + lwIP + esp-mqtt   (red / MQTT)    |  ESP-IDF
+- - - - - - - - - - - - - - - - - - - - - - - - +
|  adin1110  (driver MAC-PHY 10BASE-T1L)         |  components/  (Analog Devices)
+-----------------------------------------------+
|  no_os_port  (shim no-OS -> ESP-IDF) + oa_tc6  |  components/
+-----------------------------------------------+
```

### Interfaz de red SPE (componente `eth_adin1110`)

`eth_adin1110_start()` levanta el ADIN1110 y lo expone como un `esp_netif` de
clase Ethernet con IP estática (**F-01/F-02**). Sustituye al antiguo componente
de *bring-up* `spe_link` (que solo verificaba el PHY ID) y además conecta el chip
a la pila de red y transmite/recibe tramas:

1. Inicializa el ADIN1110 (SPI + reset) y **verifica el PHY ID** (`0x0283BC91`):
   confirma que la comunicación SPI y el reset funcionan. Protocolo **OPEN
   Alliance TC6** (impuesto por los *straps* de la placa, ver §8 de la spec).
2. Registra la interfaz en `esp_netif` con **IP estática** y crea una **tarea de
   servicio** que: atiende la **interrupción del pin INT** para drenar las tramas
   recibidas (`read_fifo`), transmite las salientes (`write_fifo`, con reintento
   ante FIFO llena) y **sondea el estado del enlace** 10BASE-T1L.
3. Cuando el enlace sube y la interfaz obtiene IP, `esp_netif` emite
   `IP_EVENT_ETH_GOT_IP`, que dispara el arranque del cliente MQTT.

Toda llamada al driver no-OS se serializa con un **mutex** (no es reentrante,
§8.4) y la tarea de servicio se suscribe al **Task Watchdog Timer** (**F-09**).
La línea clave del log de arranque es:

```
eth_adin: ADIN1110 OK: PHY ID 0x0283BC91 (OA TC6)
```

seguida de transiciones `Enlace 10BASE-T1L ACTIVO (UP)` / `CAIDO (DOWN)`.

### Flujo de datos en ejecución

```
[sensor_sampling_task] --(sensor_msg_t)--> [cola FreeRTOS] --> [mqtt_network_task]
   lee los sensores I2C                     buffer local          publica por MQTT
   construye el JSON                        (64 muestras)         con QoS configurable
```

Dos tareas FreeRTOS desacopladas por una cola (requisito **F-08**):

- **`sensor_sampling_task`** — lee **todos los sensores I2C activos** cada
  `periodo` ms (**F-03**, **F-06**) mediante `i2c_sensor_read()`, serializa las
  medidas en un JSON con un campo por sensor (**F-04**) y lo encola.
- **`mqtt_network_task`** — extrae muestras de la cola y las publica en el
  topic configurado con el QoS configurado (**F-05**), y ejecuta el cierre
  ordenado cuando se solicita por comando (**F-12**).

La cola actúa además como **buffer local** mientras no hay conexión (**F-07**):
si se llena, la tarea de muestreo descarta la muestra más antigua para
conservar la información más reciente. Ambas tareas se supervisan con el
**Task Watchdog Timer** (**F-09**).

### Presencia del nodo (LWT) — F-10…F-13

El nodo mantiene un segundo topic, **`v1/spe/sensor/status`** (retain=true), con
su estado de presencia:

- **LWT** registrado antes de conectar: si el nodo cae sin `DISCONNECT`, el
  broker publica `{"node":"<id>","status":"OFFLINE","reason":"unexpected_disconnect"}`
  (**F-10**).
- Al conectar se publica `{"node":"<id>","status":"ONLINE","sensor":"ok"}`
  retenido, antes de empezar a publicar datos (**F-11**).
- **Salud del sensor:** si los sensores no comunican (fallo de init o de lectura
  por I2C), el nodo **no se reinicia**; reintenta y republica el estado con
  `"sensor":"fault"` (y `"sensor":"ok"` al recuperarse). Así el gateway distingue
  "nodo caído" (LWT/keepalive) de "nodo vivo con sensor averiado".
- Un comando con `reboot`/`shutdown` en `v1/spe/sensor/cmd` dispara un cierre
  ordenado: publica `{"node":"<id>","status":"OFFLINE","reason":"shutdown"}`,
  espera el PUBACK y reinicia (**F-12**).
- `keepalive ≤ 20 s` para que el broker detecte la caída en < 30 s (**F-13**).

---

## Estructura de componentes

| Componente | Ruta | Responsabilidad |
|------------|------|-----------------|
| `main` | [main/](main/) | `app_main`: arranque, tareas, cola, cliente MQTT |
| `app_config` | [components/app_config/](components/app_config/) | Configuración del nodo (Kconfig + override por NVS) |
| `i2c_sensor` | [components/i2c_sensor/](components/i2c_sensor/) | Driver **modular** de sensores I2C sobre el bus Qwiic (varios sensores en el mismo bus) |
| `co_sensor` | [components/co_sensor/](components/co_sensor/) | Driver de sensores de gas **ADC** (analógicos). *Pendiente de prueba en hardware*; no cableado en `app_main` |
| `sensor_payload` | [components/sensor_payload/](components/sensor_payload/) | Constructor del payload JSON (datos multi-campo + estado) |
| `eth_adin1110` | [components/eth_adin1110/](components/eth_adin1110/) | Interfaz Ethernet SPE (ADIN1110): `esp_netif` + IP estática, TX/RX de tramas, monitor de enlace (mutex/WDT) |
| `adin1110` | [components/adin1110/](components/adin1110/) | Driver MAC-PHY ADIN1110 (Analog Devices, no-OS) |
| `no_os_port` | [components/no_os_port/](components/no_os_port/) | Shim de la capa no-OS sobre ESP-IDF + driver OA TC6 |

> El proyecto usa `MINIMAL_BUILD ON`: solo se compilan `main` y sus
> dependencias. `main` requiere `i2c_sensor`, `sensor_payload`, `app_config` y
> `eth_adin1110`; este último requiere `adin1110` → `no_os_port`, de modo que
> toda la pila del driver se compila. `co_sensor` (ADC) **no** es dependencia de
> `main`, por lo que con `MINIMAL_BUILD` no se compila salvo que se cablee.

---

## Componente `i2c_sensor` (modular)

`i2c_sensor` soporta **varios sensores I2C en el mismo bus** (Qwiic). Separa dos
responsabilidades:

- **Núcleo** ([i2c_sensor.c](components/i2c_sensor/i2c_sensor.c)): crea **un único
  bus maestro** compartido y mantiene una **tabla de drivers** habilitados. No
  conoce ningún sensor concreto: inicializa, lee y libera cada uno a través del
  contrato `i2c_sensor_drv_t`.
- **Drivers** ([components/i2c_sensor/drivers/](components/i2c_sensor/drivers/)):
  cada sensor es un fichero independiente que rellena un `i2c_sensor_drv_t`
  (`name`, `key` JSON, `unit`, `decimals`, `init/read/deinit`).

La aplicación no depende del número ni del tipo de sensores: `sensor_sampling_task`
llama a `i2c_sensor_read()`, que devuelve todas las medidas válidas, y publica
una por campo del payload. Un sensor que falla se marca inactivo sin afectar al
resto (tolerancia a fallos por sensor).

### Drivers incluidos

| Driver | Sensor | Campo JSON | Unidad | Por defecto |
|--------|--------|-----------|--------|-------------|
| `simulated` | — (sin hardware) | `CO_LEVEL` | ppm | **habilitado** |
| `oxygen_sen0322` | DFRobot Gravity I2C Oxygen (SEN0322) | `O2` | %vol | deshabilitado |
| `ozone_sen0321` | DFRobot Gravity I2C Ozone (SEN0321) | `O3` | ppb | deshabilitado |
| `multigas` | DFRobot MultiGasSensor (**plantilla**) | `GAS` | ppm | deshabilitado |

> El driver **simulado** no usa el bus I2C (`needs_bus = false`), por lo que el
> firmware arranca y publica aunque no haya hardware. Usa la clave `CO_LEVEL`
> para que el *build* por defecto mantenga el contrato histórico del gateway. En
> el banco con sensores reales, deshabilitarlo y habilitar los sensores físicos.
> El driver **multigas** es un *scaffold* (protocolo sin portar): si se habilita,
> queda inactivo hasta completar `drivers/multigas.c`.

### Añadir un sensor I2C nuevo (3 pasos)

1. **Driver:** crear `drivers/<sensor>.c` (+ `.h`) que rellene un
   `i2c_sensor_drv_t` con sus metadatos e `init/read/deinit` sobre el `bus` que
   recibe `init()`. Usar [drivers/multigas.c](components/i2c_sensor/drivers/multigas.c)
   como plantilla.
2. **Kconfig:** añadir su opción `I2C_SENSOR_<X>_ENABLE` (y dirección, muestras…)
   en [components/i2c_sensor/Kconfig](components/i2c_sensor/Kconfig).
3. **Registro:** añadir el `#include` y la entrada en la tabla `s_drivers[]` de
   [i2c_sensor.c](components/i2c_sensor/i2c_sensor.c), bajo su guarda
   `#if CONFIG_I2C_SENSOR_<X>_ENABLE`. El [CMakeLists.txt](components/i2c_sensor/CMakeLists.txt)
   compila cada driver solo si su sensor está habilitado.

No hay que tocar la lógica del bus ni la de la aplicación.

---

## Sensores soportados

A lo largo del TFM se han utilizado varios sensores de gas. Esta tabla los
resume y dónde encajan en el firmware:

| Sensor | Interfaz | Nodo / componente | Magnitud | Estado |
|--------|----------|-------------------|----------|--------|
| **Mikroe CO Click** | Analógica → ADC (IIO) | BeaglePlay (iteraciones 0–3) | CO (cruda ADC) | Usado como productor temporal |
| **DFRobot Gravity Gas Sensor v2** (sonda CO) | Analógica → ADC | ESP32 / `co_sensor` | CO (ppm) | **Pendiente** (no se prueba en ESP32) |
| **DFRobot Gravity I2C Ozone (SEN0321)** | I2C / Qwiic | ESP32 / `i2c_sensor` (`ozone_sen0321`) | O₃ (ppb) | Operativo en el banco |
| **DFRobot Gravity I2C Oxygen (SEN0322)** | I2C / Qwiic | ESP32 / `i2c_sensor` (`oxygen_sen0322`) | O₂ (%vol) | Implementado |
| **DFRobot MultiGasSensor** | I2C / Qwiic | ESP32 / `i2c_sensor` (`multigas`) | Configurable | Plantilla (sin portar) |

- **Mikroe CO Click (BeaglePlay).** Sensor analógico usado con el **BeaglePlay**
  como nodo productor en las iteraciones 0–3 del TFM. Se lee por el subsistema
  *Industrial I/O* de Linux (`/sys/bus/iio/.../in_voltage0_raw`) y se publica con
  el script `prueba_CO_click.py` (ver apéndice de la memoria). No corre sobre
  ESP32.
- **DFRobot Gravity Gas Sensor v2 (ADC).** Sensor analógico (sonda de CO) leído
  por la unidad **ADC1** del ESP32 en el componente `co_sensor`. La conversión
  tensión→ppm es **lineal provisional** y requiere calibración con gas patrón.
  **No se prueba en la placa ESP32** por limitaciones de hardware: el componente
  queda marcado como *pendiente* y no se cablea en `app_main`.
- **DFRobot Gravity I2C Ozone (SEN0321).** Sensor electroquímico de **ozono**
  (0–10 ppm; resolución 0,01 ppm según datasheet) por **I2C/Qwiic**. Es el sensor
  I2C disponible en el banco. La lectura se entrega en **ppb** en el campo `O3`.
- **DFRobot Gravity I2C Oxygen (SEN0322).** Sensor electroquímico de **oxígeno**
  (0–25 %vol) por **I2C/Qwiic**, descrito en
  [components/i2c_sensor/cpp_driver.md](components/i2c_sensor/cpp_driver.md)
  (driver de referencia del fabricante). Portado a ESP-IDF en
  `oxygen_sen0322`; publica en %vol en el campo `O2`.
- **DFRobot MultiGasSensor.** Familia multigas I2C. Incluida como **plantilla**
  (`multigas`) para demostrar la modularidad: añadir un sensor nuevo al bus
  compartido sin tocar el núcleo. Completar el protocolo antes de usar.

> **Sensores electroquímicos:** SEN0321/SEN0322 requieren **precalentamiento**
> (minutos) para lecturas estables.

### Detalle técnico de los sensores I2C (datasheets)

Resumen de los datasheets del proyecto
([SEN0322](SEN0322_oxygen-sensor_datasheet_1.0.pdf),
[SEN0321](SEN0321_iic-ozone-sensor_datasheet_v1.pdf)). En ambos casos, el módulo
DFRobot integra el **elemento sensor electroquímico de Winsen** más la
electrónica de acondicionamiento y la interfaz digital.

#### DFRobot SEN0322 — Oxígeno (O₂)

- **Tecnología:** célula electroquímica **amperométrica** (elemento Winsen
  **ME2-O2-Ф20**). El O₂ se **oxida en el electrodo de trabajo** de la celda y la
  **corriente** generada es **proporcional a la concentración** (ley de Faraday):
  la concentración se obtiene midiendo esa corriente.
- **Qué se lee:** la celda entrega una **corriente muy baja** (sensibilidad
  0,08–0,25 mA en aire); el módulo DFRobot la amplifica, digitaliza y expone por
  **I2C**. El firmware lee 3 bytes (entero, décimas, centésimas) y los multiplica
  por un factor de calibración (`key`) que el módulo guarda en su flash → **O₂ en
  %vol** (ver [cpp_driver.md](components/i2c_sensor/cpp_driver.md)). Es un sensor
  **calibrable** (función `calibrate` del fabricante).
- **Características clave:** rango **0–25 %vol** (máx. 30 %vol); **T90 ≤ 15 s**;
  repetibilidad < 2 % del valor; deriva de cero ≤ 1 %vol (−20…40 °C); estabilidad
  < 2 %/mes; presión atmosférica ±10 %; vida útil **~2 años**; almacenamiento
  −20…50 °C, 0–99 %RH (sin condensación). Salida **lineal** con la concentración.
- **Precauciones:** no retirar la membrana impermeable/transpirable; no encapsular
  en resina ni dejar en ambiente sin O₂; evitar disolventes orgánicos y gases
  corrosivos; no soldar/forzar los pines.

#### DFRobot SEN0321 — Ozono (O₃)

- **Tecnología:** módulo electroquímico de O₃ (elemento Winsen **ZE27-O3**) con
  **sensor de temperatura integrado** para **compensación térmica** y salida
  digital ya **linealizada**; buena selectividad y antiinterferencias.
- **Interfaz:** el módulo Winsen es nativo **UART** (3 V TTL, 9600 8N1; por
  defecto auto-subida a 1 Hz, o modo pregunta/respuesta; tramas de 9 bytes con
  *checksum*). La placa **DFRobot Gravity SEN0321 expone I2C**, que es la que usa
  el firmware: escribe el registro de modo (`0x03 ← 0x00`, automático) y lee 2
  bytes del registro `0x09`.
- **Qué se lee:** concentración de O₃ en **ppb** = `MSB·256 + LSB` (campo `O3`).
  `ppm = ppb / 1000`. El bus transporta ppb enteros (LSB = 1 ppb), aunque la
  **resolución del sensor es 0,01 ppm** (datasheet).
- **Características clave:** rango **0–10 ppm**; gas interferente: alcohol y
  similares; **respuesta ≤ 90 s**, recuperación ≤ 90 s; **precalentamiento ≤ 3
  min** (primer encendido: 24–48 h para estabilizar del todo); alimentación
  3,7–5,5 V (**sin protección de polaridad inversa**); Tª trabajo −20…50 °C,
  humedad 15–90 %RH; vida útil **~2 años** (en aire).
- **Precauciones:** electroquímico (mismas que el O₂): membrana intacta, sin
  resinas/disolventes/gases corrosivos, sin golpes ni vibración excesiva.

---

## Configuración (`idf.py menuconfig`)

| Menú | Parámetros |
|------|-----------|
| **Enlace SPE - ADIN1110 (10BASE-T1L)** | Host SPI, GPIO SCLK/MOSI/MISO/CS, frecuencia SPI, GPIO RESET/INT, periodo de sondeo del enlace |
| **Nodo sensor SPE - Configuración** | IP/máscara/gateway, `node_id`, broker MQTT, usuario, topic de datos/estado/comandos, QoS, keepalive, periodo de muestreo |
| **Nodo sensor SPE - Sensores I2C** | Bus compartido (puerto, GPIO SDA/SCL Qwiic, frecuencia) y, por sensor, habilitación + dirección I2C + muestras (O2, O3, MultiGas, simulado) |
| **Nodo sensor SPE - Sensor de gas ADC (co_sensor, pendiente)** | Canal ADC1, muestras y calibración del sensor analógico (no se prueba en ESP32) |

> No hay menú de Wi-Fi: la red es **exclusivamente por SPE** (ADIN1110). El
> helper `example_connect` y las dependencias `protocol_examples_common` /
> `esp_wifi` se han retirado.

> **Sensores en el mismo bus (direcciones DIP).** Si se habilitan varios
> sensores I2C, sus direcciones deben ser **distintas**. El DIP A1/A0 (2 bits)
> selecciona `0x70`–`0x73`. **Ojo a la polaridad:** en el **SEN0321 (O3)** las
> líneas están en pull-up (interruptor **OFF = bit 1**), por lo que con **ambos
> en OFF** responde en `0x73` (verificado). Verifica la dirección real de cada
> módulo (escaneo I2C / `i2c_master_probe`) y haz que el valor de `..._ADDR`
> coincida. Defaults del firmware: O2=`0x70`, O3=`0x73`.

Los parámetros se definen por defecto en Kconfig y pueden sobrescribirse en
NVS clave a clave (namespace `nodecfg`); no van incrustados en código
(requisito no funcional §3).

### Conexión a ThingsBoard

| Campo | Valor |
|-------|-------|
| URI del broker | `mqtt://eu.thingsboard.cloud:1883` |
| Usuario MQTT | *access token* del dispositivo |
| Topic | `v1/devices/me/telemetry` |

> **Red SOLO por SPE (sin Wi-Fi).** El cliente MQTT se arranca cuando la interfaz
> del ADIN1110 obtiene IP (`IP_EVENT_ETH_GOT_IP`). Hasta que el enlace SPE
> esté operativo, el nodo muestrea localmente y **no publica**.

---

## Contrato de payload (MQTT)

El topic de datos (`v1/spe/sensor/data`) transporta un objeto JSON con **un campo
por sensor activo**, con la clave nativa de cada uno:

```json
{"O2":"20.9","O3":"5"}
```

Con un único sensor simulado, el payload se reduce al contrato histórico
`{"CO_LEVEL":"<valor>"}`. Los valores se serializan como cadena. **Los topics no
cambian**; el procesamiento del gateway (Node-RED → InfluxDB) debe adaptarse a
los campos que publique el nodo ESP32.

> Los **cambios concretos a aplicar en el gateway** (funciones `function 1` /
> `function 2` de Node-RED genéricas y esquema InfluxDB) están documentados en
> [`Arquitectura_Estado_y_Alarmas.md`](../../Arquitectura_Estado_y_Alarmas.md)
> §2.1.1, junto con el campo de salud `sensor` del estado ONLINE (§2.2).

---

## Compilación y flasheo

```
idf.py set-target esp32
idf.py menuconfig
idf.py build flash monitor
```

El camino de datos añade al log las líneas `Muestra encolada:` (con el JSON) y
`Publicado en ...`; si los sensores no responden verás `Lectura de sensores
fallida` y `Salud del sensor: FALLO (alarma)` (sin reinicio del nodo). Si en el
arranque del enlace aparece `PHY ID invalido` en lugar de `ADIN1110 OK`, revisa
el cableado SPI, la alimentación y el pin de RESET.

---

## Puesta en marcha del hardware (bring-up): problemas y soluciones

Problemas reales encontrados al llevar el firmware a la placa (SparkFun MicroMod
ESP32 + Main Board Single + Function Board ADIN1110) y cómo se resolvieron.
Útiles para reproducir el montaje.

### 1. No se podía flashear/arrancar con la Function Board ADIN1110 conectada

**Síntoma:** con la placa SPE conectada, `esptool` fallaba con `Failed to
communicate with the flash chip` / `A fatal error occurred: Packet content
transfer stopped`. Sin la placa, flasheaba bien.

**Causa:** la Function Board ADIN1110 lleva la señal **PWR_EN** (pin **71** del
conector MicroMod, con un **pull-up de 10 kΩ a 3.3 V**) y ese pin va al
**GPIO12 (MTDI)** del ESP32. GPIO12 es un *strapping pin*: su nivel en el reset
fija la tensión del regulador interno de la flash (`VDD_SDIO`): bajo → 3.3 V,
**alto → 1.8 V**. El pull-up forzaba GPIO12 alto → la flash (de 3.3 V) quedaba a
1.8 V → ilegible → fallo al flashear **y al arrancar**. (Lo confirma el
esquemático `SparkFun_MicroMod_Function_ADIN1110.pdf`, sección *Power Enable*.)

**Solución (definitiva, sin tocar hardware):** quemar el eFuse que fija
`VDD_SDIO` a 3.3 V para que el ESP32 **ignore GPIO12**. En una terminal con el
entorno IDF cargado (en VS Code: *ESP-IDF: Open ESP-IDF Terminal*):

```
espefuse.py -p COM5 summary                 # inspeccionar (no quema nada)
espefuse.py -p COM5 set_flash_voltage 3.3V  # pide teclear BURN; IRREVERSIBLE
espefuse.py -p COM5 summary                 # verificar (XPD_SDIO_* forzados)
```

Si `espefuse.py` no está en el PATH, invocar el módulo con el Python del IDF:
`python -m espefuse -p COM5 set_flash_voltage 3.3V`.

Es **irreversible** (eFuse OTP) pero seguro aquí: la flash es Winbond de 3.3 V,
así que 3.3 V es el valor correcto. Tras el quemado se flashea y arranca con la
Function Board conectada. Hay que **repetirlo por cada módulo ESP32**.
Alternativas no usadas: aislar el pin 71 del M.2 con cinta Kapton (reversible,
pero delicado por los pines de potencia 70/72/73 contiguos) o quitar el pull-up.

### 2. Crash al inicializar el ADIN1110 con reset por software

**Síntoma:** `Guru Meditation Error: StoreProhibited` en
`adin1110_standard_spi_reg_write` → `no_os_put_unaligned_be16`.

**Causa:** bug del driver *upstream* de Analog Devices: con `reset_gpio` ausente
(`CONFIG_SPE_ADIN1110_RESET_GPIO = -1`, reset por software), `adin1110_init`
llama a `adin1110_sw_reset()` —que hace un `reg_write` sobre `desc->data`—
**antes** de reservar `desc->data` (queda NULL → escritura a NULL).

**Solución:** parche local en [adin1110.c](components/adin1110/adin1110.c)
(marcado con `[Parche local ...]`) que adelanta la reserva de `desc->data` antes
del software reset. Con esto, RESET=-1 (reset por software) funciona. Cuando se
confirme el pin de RESET hardware real en el conector M.2, configurarlo en
`menuconfig` es preferible al reset por software.

### 3. Tamaño de flash y consejos de flasheo

- **Flash de 16 MB:** el módulo monta una **W25Q128 (16 MB)**; el `sdkconfig`
  venía con 2 MB. Corregido a `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` (fijado también
  en [sdkconfig.defaults](sdkconfig.defaults) para que persista).
- **Flasheo robusto:** ante cortes de transferencia, conviene bajar el *baud
  rate* de flasheo a 115200 (`idf.flashBaudRate` en [.vscode/settings.json](.vscode/settings.json)),
  usar cable de datos y puerto USB directo. *(En este proyecto el corte real era
  el strapping de GPIO12 del punto 1, no el baud.)*

---

## Estado del driver ADIN1110

El componente [`eth_adin1110`](components/eth_adin1110/) hace el *bring-up* del
ADIN1110 al arrancar y lo integra como interfaz `esp_netif` con IP estática,
supervisando el enlace con reinicialización ante fallos (mutex + watchdog).
**Comunicación SPI verificada** leyendo el PHY ID:

```
eth_adin: ADIN1110 OK: PHY ID 0x0283BC91 (OA TC6)
```

**Protocolo SPI: OPEN Alliance TC6 (no genérico).** La placa SparkFun arranca con
los straps `SPI_CFG` a nivel bajo, que según la **Tabla 10 de la datasheet**
seleccionan **OPEN Alliance con protección (CRC)** por defecto; por eso el modo
genérico fallaba (`-22`/`-116`). Configuración:
- `oa_tc6_spi = true` en [eth_adin1110](components/eth_adin1110/eth_adin1110.c).
- Parche local en `adin1110.c`: `oa_param.prote_spi = true` (el upstream lo dejaba
  sin inicializar). En OA la protección la lleva `prote_spi`.

**Pines SPI** (bus VSPI del conector M.2, verificados contra el esquemático):
SCK=GPIO18, COPI=GPIO23, CIPO=GPIO19, **CS=GPIO5** (`SPI_CS`). **RESET** = `-1` →
reset por software. **INT** (en `F0/INT` = GPIO12) dispara la recepción de tramas.

**Otros straps por defecto** (Tabla 10): *prefer slave* (el gateway debe ser
**master** para que suba el enlace 10BASE-T1L) y PHY en *software power-down* tras
reset (el driver lo saca en `setup_phy`).

**Robustez (§8.4), parches locales en `adin1110.c`:**
- *Timeouts* en los bucles MDIO (`adin1110_mdio_wait_done`): un PHY que no responde
  falla con `-ETIMEDOUT` en vez de colgar el SPI y disparar el watchdog.
- Reserva de `desc->data` antes del *software reset* (evita `StoreProhibited` con
  `RESET=-1`).

- **Pendiente:** subida efectiva del enlace 10BASE-T1L contra el gateway
  (negociación master/slave del PHY) y obtención de IP por la interfaz, que
  dispara MQTT. La validación extremo a extremo con sensor real se hará con los
  dos nodos definitivos montados.

---

## Cosas a tener en cuenta

- **Sensores por el bus I2C/Qwiic (componente `i2c_sensor`).** En el SparkFun
  MicroMod ESP32 el bus Qwiic es **SDA=GPIO21 / SCL=GPIO22** (por defecto,
  configurables). Varios sensores comparten el bus; sus direcciones DIP deben ser
  distintas. Por defecto arranca el sensor **simulado** (sin hardware); habilitar
  el sensor físico (O2/O3) en `menuconfig`.
- **`co_sensor` (ADC) pendiente.** El backend analógico vive ahora en su propio
  componente `co_sensor` y **no se prueba en ESP32** (limitación de hardware);
  usa **ADC1** (la ADC2 entra en conflicto con la radio Wi-Fi, ya retirada). Su
  conversión tensión→ppm es **lineal provisional** (`TODO`) y requiere
  calibración con gas patrón. No está cableado en `app_main`.
- **MQTT en texto plano.** Se usa `mqtt://` (puerto 1883), sin TLS: el *access
  token* viaja sin cifrar. Aceptable para pruebas de laboratorio. Migrar a
  `mqtts://` requiere reactivar el *certificate bundle* (ver
  [sdkconfig.defaults](sdkconfig.defaults)).
- **Payload multi-campo.** El topic de datos lleva un campo por sensor
  (`{"O2":"...","O3":"..."}`); con el simulado se reduce a `{"CO_LEVEL":"..."}`.
  Los valores van como cadena. El gateway debe adaptarse a los campos publicados.
- **IP estática por SPE.** Los campos IP/máscara/gateway de `app_config` se
  aplican en `eth_adin1110` (sin DHCP). El cliente MQTT arranca al obtener IP.
- **Código de terceros casi sin modificar.** `adin1110.c` y `oa_tc6.c` (Analog
  Devices) se integran para poder seguir el *upstream*; sus advertencias se
  toleran con `-Wno-error` y la adaptación vive en `no_os_port`. Lleva **un único
  parche local documentado** (`[Parche local ...]` en `adin1110_init`); ver el
  problema 2 de *Puesta en marcha del hardware*.
- **Buffer local acotado.** La cola guarda hasta 64 muestras (~128 s a 2 s de
  periodo). Superado ese margen se descartan las muestras más antiguas; no hay
  persistencia en flash.
