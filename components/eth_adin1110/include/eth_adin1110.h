/*
 * Componente: eth_adin1110 - interfaz Ethernet 10BASE-T1L (SPE).
 *
 * Levanta el ADIN1110 (SPI, OPEN Alliance TC6) y lo expone como un esp_netif de
 * clase Ethernet con IP estatica (F-01/F-02). La recepcion de tramas y la
 * supervision del enlace corren en una tarea de servicio propia; un unico mutex
 * serializa todo acceso al driver no-OS (no reentrante).
 *
 * Sustituye al componente de bring-up `spe_link`: aqui ademas se conecta a la
 * pila de red y se transmiten/reciben tramas.
 */
#ifndef ETH_ADIN1110_H
#define ETH_ADIN1110_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arranca el ADIN1110 y crea la interfaz de red con IP estatica.
 *
 * Debe llamarse despues de esp_netif_init() y esp_event_loop_create_default().
 * No es fatal si el enlace 10BASE-T1L aun no esta activo: la interfaz se marca
 * "conectada" (y emite IP_EVENT_ETH_GOT_IP) cuando el enlace sube.
 *
 * @param ip_info  IP/mascara/gateway estaticos de la interfaz.
 * @param mac_addr MAC de 6 bytes del nodo.
 * @return ESP_OK si el bring-up SPI y la creacion de la interfaz tuvieron exito.
 */
esp_err_t eth_adin1110_start(const esp_netif_ip_info_t *ip_info,
                             const uint8_t mac_addr[6]);

/** @brief Estado actual del enlace 10BASE-T1L (true = activo). */
bool eth_adin1110_link_up(void);

#ifdef __cplusplus
}
#endif

#endif /* ETH_ADIN1110_H */
