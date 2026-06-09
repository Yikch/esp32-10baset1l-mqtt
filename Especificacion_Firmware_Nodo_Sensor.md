# Especificación del firmware — Nodo sensor de gas sobre SPE

Documento autocontenido. Describe el firmware del nodo sensor sin requerir
acceso a la memoria del TFM.

---

## 1. Contexto

Firmware para un nodo sensor que mide concentración de monóxido de carbono (CO)
y la transmite por **Single Pair Ethernet (10BASE-T1L)** a un gateway remoto
mediante MQTT.

### Hardware

- **MCU:** ESP32-WROOM-32E (16 MB flash, 520 KB SRAM) — módulo SparkFun MicroMod ESP32.
- **Interfaz de red:** placa SparkFun MicroMod SPE con MAC-PHY **ADIN1110**
  (10BASE-T1L). Conexión ESP32 ↔ ADIN1110 por **SPI** + líneas INT y RESET.
- **Sensor de CO:** sensor **I2C** conectado por bus Qwiic.

### Red

- Enlace punto a punto 10BASE-T1L con un gateway.
- **IP estática** (sin DHCP).
- Broker MQTT en una IP fija, puerto 1883.

### Contrato de interfaz hacia el gateway (fijo, no modificable)

El sensor mantiene **dos topics** MQTT: uno para mediciones y otro para
estado (presencia/ausencia). El gateway utiliza el reloj propio como
autoridad temporal única, por lo que **el payload no incluye ningún
sello de tiempo**: la marca temporal la asigna InfluxDB en el momento
de la inserción.

**Topic de datos:**

| Elemento | Valor |
|----------|-------|
| Topic    | `v1/spe/sensor/data` |
| Payload  | JSON `{"CO_LEVEL":"<valor>"}` |
| QoS      | 1 |
| Retain   | No |

**Topic de estado (presencia del nodo):**

| Elemento | Valor |
|----------|-------|
| Topic    | `v1/spe/sensor/status` |
| Payload  | JSON `{"node":"<node_id>","status":"ONLINE"\|"OFFLINE","reason":"<reason>"}` |
| QoS      | 1 |
| Retain   | Sí |

Valores admitidos de `reason` en transición a `OFFLINE`:
`shutdown` (cierre ordenado) o `unexpected_disconnect` (caída detectada
por LWT). El mecanismo se detalla en la sección 9.

---

## 2. Requisitos funcionales

| ID | Requisito |
|----|-----------|
| F-01 | Inicializar el ADIN1110 por SPI: pines SPI/INT/RESET, reset, configuración MAC y negociación del enlace 10BASE-T1L. |
| F-02 | Registrar la interfaz en la pila TCP/IP y asignar IP estática (sin DHCP). |
| F-03 | Leer el sensor de CO por I2C con resolución ≥1 ppm en rango 0–2000 ppm. |
| F-04 | Serializar la lectura en JSON con el esquema exacto `{"CO_LEVEL":"<valor>"}`. |
| F-05 | Cliente MQTT: conectar al broker y publicar en el topic acordado con QoS 1. |
| F-06 | Periodo de muestreo configurable, ≤30 s (por defecto 2 s). |
| F-07 | Reconexión automática ante caída del enlace o del broker; encolado **en RAM** de los mensajes pendientes durante cortes transitorios (segundos). **No se persiste a flash**: los cortes prolongados se notifican mediante el mecanismo F-10, no se buferizan. |
| F-08 | Arquitectura de dos tareas FreeRTOS (muestreo I2C / red MQTT) desacopladas por una cola. |
| F-09 | Arranque automático tras reset y supervisión de tareas (watchdog). |
| F-10 | Registrar un **Last Will Testament (LWT)** en `v1/spe/sensor/status` con payload `{"node":"<id>","status":"OFFLINE","reason":"unexpected_disconnect"}`, QoS 1, retain=true, **antes** de la conexión MQTT. |
| F-11 | Publicar `{"node":"<id>","status":"ONLINE"}` en `v1/spe/sensor/status` con QoS 1 y retain=true **inmediatamente tras una conexión MQTT exitosa** (en el callback `MQTT_EVENT_CONNECTED`). |
| F-12 | Ante un cierre ordenado del nodo (reinicio programado, comando administrativo), publicar `{"node":"<id>","status":"OFFLINE","reason":"shutdown"}` (QoS 1, retain=true) y esperar confirmación antes de desconectarse, para distinguir cierres ordenados de caídas no controladas. |
| F-13 | Configurar `keepalive ≤ 20 s` en el cliente MQTT. El broker debe detectar la caída del sensor en menos de 30 s (≈ 1,5 × `keepalive`). |

---

## 3. Requisitos no funcionales

- Framework **ESP-IDF v5.x**; pila TCP/IP **lwIP**.
- Estabilidad en operación continua, sin fugas de memoria.
- Parámetros (IP, broker, topic, periodo) configurables vía `menuconfig`/NVS,
  no incrustados en código.
- El firmware no debe asumir condiciones ambientales ideales
  (entorno objetivo: −20/+70 °C, 95 % HR).

---

## 4. Proyecto de ejemplo de Espressif como base

No existe un ejemplo único que combine SPE + MQTT. Se recomienda partir de
**dos** ejemplos y fusionarlos:

1. **Base principal — `examples/protocols/mqtt/tcp`**
   Aporta el cliente esp-mqtt ya integrado y el patrón de arranque de red.
   Es el esqueleto de la aplicación.

2. **Referencia de red — `examples/ethernet/basic`**
   Muestra cómo se instancia un MAC Ethernet **sobre SPI**
   (W5500/DM9051/KSZ8851), se asocia a un PHY, se registra en `esp_eth` y se
   conecta a lwIP vía `esp_netif`. Es la plantilla a imitar para encajar el
   ADIN1110.

   Útil además: **`examples/ethernet/iperf`** para validar el enlace una vez
   levantado.

**Estrategia:** tomar la estructura de `mqtt/tcp`, sustituir su conexión Wi-Fi
por una interfaz Ethernet construida según el patrón de `ethernet/basic`, pero
usando un driver propio para el ADIN1110.

---

## 5. Componentes del ESP-IDF Component Registry

La mayor parte de lo necesario **ya viene incluido en ESP-IDF** y no hay que
añadirlo: `esp-mqtt`, `cJSON`, `esp_eth`, driver I2C, NVS, FreeRTOS, lwIP.

Del registro (`idf.py add-dependency`) solo se necesitaría:

- **Driver del sensor de CO** — *solo si* existe un componente para el modelo
  I2C concreto utilizado (buscar en `components.espressif.com` por el nombre
  del chip). Si no existe → se crea manualmente (ver sección 6).
- *(Opcional)* `espressif/iperf-cmd` — utilidad de consola para medir el caudal
  del enlace durante las pruebas.

> **Nota:** a día de hoy **no hay driver oficial de Espressif** para el ADIN1110
> ni está en `esp_eth`. Conviene buscar `adin1110` en el registro por si hay un
> componente comunitario, pero se debe planificar asumiendo que no lo hay.

---

## 6. Componentes a crear manualmente

1. **Driver ADIN1110 / 10BASE-T1L MAC-PHY** — el grueso del trabajo.
   El ADIN1110 habla el protocolo **OPEN Alliance TC6** (SPI MAC-PHY). Hay que
   implementar el *framing* TC6 sobre el SPI maestro de ESP-IDF y exponerlo
   como un **driver `esp_eth` personalizado** (MAC + PHY) para que se integre
   con `esp_netif`/lwIP igual que cualquier otra interfaz Ethernet.
   Referencias para portarlo: el driver C de código abierto de Analog Devices
   y la librería *Single Pair Ethernet* de SparkFun (es Arduino, pero sirve
   de guía).

2. **Driver del sensor de CO** — si no hay componente en el registro para el
   sensor elegido, escribir el driver I2C (lectura, escalado a ppm).

3. **Componentes de aplicación** (código propio del proyecto):
   - Constructor del payload JSON.
   - Tarea de muestreo y tarea de red + cola de comunicación entre ambas.
   - Capa de configuración (Kconfig + NVS) y supervisión de tareas.

> **Camino crítico:** el principal riesgo de planificación es el punto 1, el
> driver del ADIN1110. Antes de fijar plazos, conviene una prueba de concepto
> que solo levante el enlace y haga `ping`.

---

## 7. Cuestiones abiertas / riesgos a confirmar

1. **Modelo concreto del sensor de CO I2C** — fijar el sensor Qwiic definitivo
   y comprobar su rango/resolución reales frente a F-03.
2. **Driver ADIN1110 sobre ESP-IDF** — la integración del driver de Analog
   Devices con el SPI maestro de ESP-IDF y lwIP es el principal esfuerzo
   técnico y un riesgo de planificación.
3. **Esquema eléctrico** — documentar los pines reales SPI/INT/RESET del
   conector M.2 y la alimentación.

---

## 8. Integración del driver ADIN1110 (no-OS de Analog Devices)

Esta sección documenta el plan de integración del driver del MAC-PHY ADIN1110
a partir del código fuente *no-OS* de Analog Devices (`adin1110.c` /
`adin1110.h`), atendiendo a los requisitos F-01, F-02, F-07 y F-09 y a los
requisitos no funcionales de estabilidad y entorno no ideal.

### 8.1. Análisis del driver no-OS

El driver de Analog Devices es un driver de **registro y FIFO**: expone
funciones crudas (`adin1110_init`, `reg_read/write`, `write_fifo`, `read_fifo`,
`mdio_*`, `link_state`, `set_mac_addr`…). **No** es un driver `esp_eth` ni se
integra con ninguna pila TCP/IP; esa capa la debe aportar quien lo usa.

**Dos protocolos SPI en el mismo driver** (campo `oa_tc6_spi` del descriptor):

- **SPI genérico** (`oa_tc6_spi = false`): implementado íntegramente en
  `adin1110.c`.
- **OPEN Alliance TC6** (`oa_tc6_spi = true`): delega en `oa_tc6.c`, fichero
  independiente que no forma parte del driver base.

El ADIN1110 soporta ambos protocolos. **La placa SparkFun Function ADIN1110 arranca
con los straps `SPI_CFG` a nivel bajo, que según la Tabla 10 de la datasheet
seleccionan OPEN Alliance TC6 con protección (CRC) por defecto.** Por ello el nodo
adopta el **protocolo OPEN Alliance TC6** (`oa_tc6_spi = true`, `prote_spi = true`),
delegando en `oa_tc6.c`. Usar el modo genérico exigiría modificar los straps por
hardware (pull-ups de 4,7 kΩ en `SPI_CFG`), por lo que se descarta. Verificado:
lectura correcta del PHY ID `0x0283BC91`.

**Dependencias de la capa de abstracción no-OS** (a portar a ESP-IDF):

| Header no-OS | Función | Port |
|--------------|---------|------|
| `no_os_spi.h`   | SPI maestro (`spi_init/transfer/remove`) | Sobre `spi_master` de ESP-IDF |
| `no_os_gpio.h`  | GPIO (pin RESET) | Sobre `driver/gpio` |
| `no_os_delay.h` | Retardos | `vTaskDelay` |
| `no_os_alloc.h` | Reserva de memoria | Heap **DMA-capable** |
| `no_os_crc8.h`, `no_os_util.h` | CRC8 y macros de bits | Copiar (C puro) |
| `oa_tc6.h`      | Tipos TC6 | *Stub* (el modo genérico no lo usa) |

**Limitaciones del driver relevantes para la integración:**

- **No gestiona el pin INT**: `adin1110_init` solo inicializa el GPIO de RESET.
  La gestión de interrupciones es responsabilidad de la capa superior.
- **No es reentrante**: el buffer `desc->data` se comparte entre operaciones de
  registro y de FIFO. Transmisión y recepción concurrentes lo corrompen.
- **Bucles MDIO sin *timeout***: un bus SPI bloqueado deriva en bucle infinito.
- `write_fifo` devuelve `-EAGAIN` si la FIFO de transmisión está llena.
- `read_fifo` lee un único frame por llamada.

### 8.2. Arquitectura de integración

Se mantiene `adin1110.c` **sin modificar** (para poder seguir el *upstream* de
Analog Devices) y toda la adaptación se concentra en dos componentes nuevos:

```
+-----------------------------------------------+
|  Aplicacion (tareas muestreo/red, MQTT)        |
+-----------------------------------------------+
|  esp_netif + lwIP   (IP estatica, F-02)        |
+-----------------------------------------------+
|  Glue esp_eth: MAC + PHY personalizados        |  componente esp_eth_adin1110
+-----------------------------------------------+
|  adin1110.c  (driver no-OS, sin modificar)     |  componente adin1110
+-----------------------------------------------+
|  Shim de plataforma no-OS -> ESP-IDF           |  componente no_os_port
|  (SPI maestro, GPIO, delay, alloc DMA, crc8)   |
+-----------------------------------------------+
```

### 8.3. Pasos de integración

Ordenados por riesgo; las fases 0–2 constituyen la prueba de concepto
recomendada en la sección 6.

**Fase 0 — Hardware**
1. Fijar y documentar los pines del conector M.2: `SCLK/MOSI/MISO/CS`, `INT`,
   `RESET` y alimentación (resuelve el riesgo 7.3).

**Fase 1 — Shim de plataforma no-OS** (componente `no_os_port`)
2. Implementar `no_os_spi_*` sobre el `spi_master` de ESP-IDF, con DMA.
3. Implementar `no_os_gpio_*` sobre `driver/gpio`.
4. Implementar `no_os_delay` y `no_os_alloc` (memoria DMA-capable); copiar
   `no_os_crc8` y `no_os_util`; crear el *stub* de `oa_tc6.h`.

**Fase 2 — Prueba de concepto de *bring-up*** (sin pila de red)
5. Invocar `adin1110_init` y verificar el **PHY ID** (`0x0283BC91`): confirma
   que el SPI y el reset funcionan.
6. Sondear `adin1110_link_state` hasta enlace 10BASE-T1L activo contra el
   gateway; ajustar registros PHY (clause 45) si procede (maestro/esclavo,
   nivel de transmisión 1,0 / 2,4 V).
7. Prueba de datos cruda con `write_fifo`/`read_fifo` (p. ej. una trama ARP o
   de eco) antes de introducir lwIP.

**Fase 3 — Glue `esp_eth`** (componente `esp_eth_adin1110`)
8. Implementar la interfaz `esp_eth_mac_t`: `transmit` sobre `write_fifo` (con
   reintento ante `-EAGAIN`), `init/start/stop`, `read/write_phy_reg` sobre
   `adin1110_mdio_*`, `set_addr`.
9. Crear la **tarea de recepción**: ISR en el pin `INT` -> semáforo -> la tarea
   lee `STATUS1` y drena `read_fifo` mientras `RX_RDY` esté activo, entregando
   las tramas a la pila de red.
10. Implementar un `esp_eth_phy_t` mínimo: `get_link` sobre
    `adin1110_link_state`.

**Fase 4 — Red (F-01 / F-02)**
11. `esp_eth_driver_install` + `esp_netif` Ethernet + `esp_eth_new_netif_glue`.
12. Configurar **IP estática** con `esp_netif_set_ip_info` y
    `esp_netif_dhcpc_stop`, tomando los parámetros del componente `app_config`.
13. Sustituir `example_connect()` por el arranque de esta interfaz; retirar
    `protocol_examples_common` y `esp_wifi` de la compilación.

**Fase 5 — Cierre**
14. Validar el enlace con `iperf` y el flujo MQTT extremo a extremo.

### 8.4. Consideraciones de rendimiento y resiliencia

**Rendimiento**

- **SPI con DMA obligatorio**: las tramas alcanzan 1530 bytes y `read_fifo`
  realiza una única transferencia en ráfaga; sin DMA, ESP-IDF limita cada
  transacción a 64 bytes. El buffer `desc->data` debe reservarse en memoria
  DMA-capable.
- Reloj SPI conservador al inicio (~10–15 MHz) y aumento posterior validando
  con CRC e `iperf`.
- Recepción dirigida por interrupción (ISR), no por sondeo, para minimizar
  latencia y uso de CPU.

**Resiliencia**

- **Exclusión mutua** (mutex) alrededor de toda llamada `adin1110_*`: el driver
  no es reentrante y `desc->data` es un buffer compartido.
- **CRC por transacción SPI** (`append_crc = true`): el entorno objetivo
  (−20/+70 °C, 95 % HR) exige detectar corrupción del bus; el camino de lectura
  ya verifica el CRC.
- **Timeouts en los bucles MDIO** de `adin1110.c` (única modificación
  recomendada del driver) para evitar bloqueos; como salvaguarda última actúa
  el watchdog de tareas (F-09).
- **Detección de fallo y reinicialización**: vigilar `STATUS0.RESETC` (reset
  espurio) y `STATUS1.SPI_ERR`; ante fallo, reinicializar el driver. Encaja con
  la reconexión automática de MQTT (F-07).
- **Supervisión del enlace** reflejada en `esp_netif` (up/down); al caer el
  enlace, la cola local de la aplicación retiene las muestras pendientes
  (F-07).
- La tarea de recepción se suscribe al *Task Watchdog Timer* y lo refresca, en
  línea con la supervisión de tareas (F-09).

---

## 9. Mecanismo de detección de caída del nodo (LWT)

Esta sección desarrolla los requisitos F-10 a F-13 y describe la
arquitectura de detección de presencia del sensor.

### 9.1. Justificación

El sistema adopta una política deliberada de **"fail loud"**: en vez de
buferizar mediciones en disco durante cortes prolongados (lo que requeriría
sellos de tiempo en origen, sincronización NTP y reconstrucción posterior
de la serie), el sensor renuncia a preservar el histórico durante cortes
largos a cambio de **notificar la indisponibilidad de inmediato**. El
gateway permanece como única autoridad temporal del sistema, lo que
elimina toda la complejidad asociada a la sincronización de relojes en
un nodo sin acceso a Internet.

El mecanismo se apoya en una funcionalidad nativa de MQTT —el *Last
Will Testament*— que el broker activa automáticamente cuando un cliente
se desconecta sin enviar un paquete `DISCONNECT` ordenado.

### 9.2. Estados y transiciones

```
              connect() OK
   (sin estado) ────────────────────► ONLINE
                                       │
                                       │ keepalive expirado
                                       │ (caida no controlada)
                                       ▼
                                     OFFLINE
                                  reason: unexpected_disconnect
                                       │
                                       │ DISCONNECT ordenado
                                       │ antes de reset/apagado
                                       ▼
                                     OFFLINE
                                  reason: shutdown
```

El estado se mantiene retenido (`retain=true`) en el topic
`v1/spe/sensor/status`, de modo que cualquier suscriptor que se conecte
con posterioridad recibe el último estado conocido de forma inmediata.

### 9.3. Configuración del cliente esp-mqtt (ESP-IDF)

El LWT se configura en la estructura `esp_mqtt_client_config_t` antes
de invocar `esp_mqtt_client_start`. Los campos relevantes son
`session.last_will.*` y `session.keepalive`.

```c
#include "mqtt_client.h"

#define NODE_ID            "esp32_01"
#define MQTT_BROKER_URI    "mqtt://192.168.10.2:1883"
#define MQTT_TOPIC_DATA    "v1/spe/sensor/data"
#define MQTT_TOPIC_STATUS  "v1/spe/sensor/status"

/* Payloads precompilados de estado. */
static const char STATUS_ONLINE[] =
    "{\"node\":\"" NODE_ID "\",\"status\":\"ONLINE\"}";

static const char STATUS_OFFLINE_GRACEFUL[] =
    "{\"node\":\"" NODE_ID "\",\"status\":\"OFFLINE\","
    "\"reason\":\"shutdown\"}";

static const char STATUS_OFFLINE_UNEXPECTED[] =
    "{\"node\":\"" NODE_ID "\",\"status\":\"OFFLINE\","
    "\"reason\":\"unexpected_disconnect\"}";

void mqtt_app_start(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,

        /* F-13: deteccion de caida en menos de 30 s. */
        .session.keepalive  = 20,

        /* F-10: Last Will Testament. */
        .session.last_will = {
            .topic   = MQTT_TOPIC_STATUS,
            .msg     = STATUS_OFFLINE_UNEXPECTED,
            .msg_len = sizeof(STATUS_OFFLINE_UNEXPECTED) - 1,
            .qos     = 1,
            .retain  = 1,
        },
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}
```

### 9.4. Publicación de ONLINE tras conexión exitosa

En el manejador de eventos del cliente, al recibir
`MQTT_EVENT_CONNECTED` se publica el estado `ONLINE` retenido. Esto
satisface F-11 y debe ejecutarse **antes** de iniciar la publicación
periódica de mediciones, para que el dashboard refleje siempre el
último estado conocido del nodo.

```c
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_client_handle_t client = handler_args;

    switch ((esp_mqtt_event_id_t) event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT conectado");
        /* F-11: publicacion retenida de estado ONLINE. */
        esp_mqtt_client_publish(client, MQTT_TOPIC_STATUS,
                                STATUS_ONLINE,
                                sizeof(STATUS_ONLINE) - 1,
                                /* qos */ 1, /* retain */ 1);
        /* Aqui se libera el semaforo que permite arrancar la
           tarea de muestreo y publicacion de datos. */
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT desconectado; esp-mqtt reintentara solo");
        break;

    /* ... resto de eventos ... */
    default:
        break;
    }
}
```

### 9.5. Cierre ordenado (F-12)

Cuando el firmware solicita un reinicio voluntario (por ejemplo, tras
una actualización OTA o un comando administrativo recibido por MQTT),
debe publicar el estado `OFFLINE/shutdown` retenido y esperar a la
confirmación del broker **antes** de invocar `esp_restart()` o cerrar
el cliente. Esta distinción permite al gateway diferenciar una
recarga programada de una caída real.

```c
void mqtt_shutdown_graceful(esp_mqtt_client_handle_t client)
{
    int msg_id = esp_mqtt_client_publish(
        client, MQTT_TOPIC_STATUS,
        STATUS_OFFLINE_GRACEFUL,
        sizeof(STATUS_OFFLINE_GRACEFUL) - 1,
        /* qos */ 1, /* retain */ 1);

    /* Esperar PUBACK antes de desconectar. Sin esto, el DISCONNECT
       puede precipitarse y el OFFLINE no llega nunca al broker. */
    wait_for_publish_ack(msg_id, /* timeout_ms */ 2000);

    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
}
```

El helper `wait_for_publish_ack` puede implementarse con un grupo de
eventos FreeRTOS armado en el callback `MQTT_EVENT_PUBLISHED`.

### 9.6. Política de buferizado durante cortes (F-07 matizado)

esp-mqtt mantiene de forma nativa una cola **en RAM** de mensajes QoS
1 pendientes durante una desconexión. Esta cola:

- **Cubre cortes transitorios** (segundos a pocos minutos) sin
  necesidad de código adicional. Al restablecerse la conexión, los
  mensajes encolados se reenvían en orden.
- **No persiste a flash**: un `esp_restart()`, un reinicio por
  *brown-out* o una pérdida de alimentación borran la cola.
- **Está acotada**: se recomienda fijar `out_buffer_size` y vigilar
  el evento `MQTT_EVENT_PUBLISHED` para evitar el crecimiento
  ilimitado de la cola en cortes de horas.

El sistema **no implementa persistencia en NVS** del *outbox* MQTT.
Esta decisión es coherente con la política de "fail loud" descrita en
9.1: en lugar de reconstruir un histórico con sellos de tiempo
artificiales tras una desconexión prolongada, el gateway registra una
laguna explícita en la serie temporal y conserva, en el measurement
`system_status`, los eventos de transición ONLINE/OFFLINE producidos
por este mecanismo.

### 9.7. Validación

Las pruebas de aceptación de este mecanismo se enumeran a continuación
y deben ejecutarse contra el banco de pruebas con doble BeaglePlay
descrito en la memoria del TFM (capítulo de experimentación):

1. **Detección por LWT.** Desconectar físicamente el cable SPE durante
   30 s con el sensor publicando. El broker debe publicar el LWT en
   `v1/spe/sensor/status` antes de los 30 s (`1,5 × keepalive`).
2. **Recuperación.** Al restablecer el cable, el sensor se reconecta y
   publica `ONLINE` retenido en menos de 5 s.
3. **Cierre ordenado.** Reiniciar el firmware mediante un comando
   administrativo. El último mensaje en `v1/spe/sensor/status` antes
   del reinicio debe ser `OFFLINE/shutdown`, no `unexpected_disconnect`.
4. **Continuidad de la serie.** Tras un corte de 30 s, la serie
   temporal `calidad_aire` de InfluxDB debe presentar una **laguna
   explícita** de aproximadamente 15 muestras (a 2 s de periodo) y
   ningún dato reconstruido con timestamp falso.
