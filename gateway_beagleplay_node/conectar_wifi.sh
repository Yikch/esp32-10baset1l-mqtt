#!/bin/bash

# --- CONFIGURACIÓN ---
INTERFACE="wlan0"
SSID="vivoX"
PASSWORD="yikang1234S"
# ---------------------

echo "Iniciando proceso de conexión en $INTERFACE..."

# 1. Escanear redes
wpa_cli -i $INTERFACE scan > /dev/null
sleep 2

# 2. Añadir nueva red y capturar el ID generado
NET_ID=$(wpa_cli -i $INTERFACE add_network | tail -n 1)

if [[ -z "$NET_ID" || "$NET_ID" == "Selected interface"* ]]; then
    echo "Error: No se pudo crear una nueva red en wpa_cli."
    exit 1
fi

echo "Configurando Red ID: $NET_ID"

# 3. Configurar SSID y Password
# Nota: Los valores deben ir entre comillas dobles dentro de comillas simples
wpa_cli -i $INTERFACE set_network $NET_ID ssid "\"$SSID\""
wpa_cli -i $INTERFACE set_network $NET_ID psk "\"$PASSWORD\""

# 4. Habilitar y seleccionar la red
wpa_cli -i $INTERFACE enable_network $NET_ID
wpa_cli -i $INTERFACE select_network $NET_ID

echo "Esperando asociación..."
sleep 5

# 5. Solicitar dirección IP (DHCP)
echo "Solicitando dirección IP..."
sudo dhclient $INTERFACE

echo "¡Proceso finalizado! Verificando conexión:"
ip addr show $INTERFACE | grep "inet "