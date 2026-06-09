#!/bin/bash
# =============================================================================
# setup_iot_stack.sh
# Script de inicialización del entorno IoT - TFM BeaglePlay
# Autor: Generado automáticamente
# Descripción: Crea la estructura de carpetas, permisos, usuarios del sistema
#              y ficheros de configuración necesarios para levantar el stack
#              Docker (Mosquitto + InfluxDB + Node-RED) correctamente.
# Uso: sudo bash setup_iot_stack.sh
# =============================================================================

set -euo pipefail  # Abortar si cualquier comando falla

# ─────────────────────────────────────────────────────────────────────────────
# COLORES PARA OUTPUT
# ─────────────────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# ─────────────────────────────────────────────────────────────────────────────
# FUNCIONES DE LOG
# ─────────────────────────────────────────────────────────────────────────────
log_info()    { echo -e "${BLUE}[INFO]${NC}  $1"; }
log_ok()      { echo -e "${GREEN}[OK]${NC}    $1"; }
log_warn()    { echo -e "${YELLOW}[WARN]${NC}  $1"; }
log_error()   { echo -e "${RED}[ERROR]${NC} $1"; }
log_section() { echo -e "\n${BOLD}${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"; 
                echo -e "${BOLD}${CYAN}  $1${NC}";
                echo -e "${BOLD}${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"; }

# ─────────────────────────────────────────────────────────────────────────────
# COMPROBACIÓN DE ROOT
# ─────────────────────────────────────────────────────────────────────────────
if [[ "$EUID" -ne 0 ]]; then
    log_error "Este script debe ejecutarse como root: sudo bash $0"
    exit 1
fi

# ─────────────────────────────────────────────────────────────────────────────
# CONFIGURACIÓN — EDITA ESTOS VALORES ANTES DE EJECUTAR
# ─────────────────────────────────────────────────────────────────────────────

# Directorio base donde vive el docker-compose.yml
BASE_DIR="$(pwd)"

# UID/GID para Node-RED (imagen oficial usa UID 1000)
NODERED_UID=1000
NODERED_GID=1000

# UID/GID para InfluxDB (imagen oficial usa UID 1000)
INFLUXDB_UID=1000
INFLUXDB_GID=1000

# Usuario del sistema Linux que lanzará docker compose (no root)
# Cámbialo si tu usuario en la BeaglePlay es diferente
DEPLOY_USER="${SUDO_USER:-$(logname 2>/dev/null || echo 'debian')}"

# ─────────────────────────────────────────────────────────────────────────────
# BANNER
# ─────────────────────────────────────────────────────────────────────────────
echo -e "${BOLD}"
echo "  ╔══════════════════════════════════════════════════╗"
echo "  ║        IoT Stack Setup — TFM BeaglePlay          ║"
echo "  ║   Mosquitto · InfluxDB · Node-RED via Docker     ║"
echo "  ╚══════════════════════════════════════════════════╝"
echo -e "${NC}"
log_info "Directorio base: ${BASE_DIR}"
log_info "Usuario de despliegue: ${DEPLOY_USER}"
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# PASO 1: COMPROBACIÓN DE DEPENDENCIAS
# ─────────────────────────────────────────────────────────────────────────────
log_section "PASO 1 · Comprobando dependencias"

check_dependency() {
    if command -v "$1" &>/dev/null; then
        log_ok "$1 encontrado: $(command -v $1)"
    else
        log_error "$1 no está instalado. Instálalo antes de continuar."
        exit 1
    fi
}

check_dependency docker
check_dependency docker-compose || check_dependency "docker compose"

# Verificar que el docker daemon está activo
if ! docker info &>/dev/null; then
    log_error "El daemon de Docker no está corriendo. Ejecuta: sudo systemctl start docker"
    exit 1
fi
log_ok "Docker daemon activo"

# ─────────────────────────────────────────────────────────────────────────────
# PASO 2: USUARIOS DEL SISTEMA
# ─────────────────────────────────────────────────────────────────────────────
log_section "PASO 2 · Creando usuarios del sistema"

# Función para crear un usuario de sistema si no existe
create_system_user() {
    local username="$1"
    local uid="$2"
    local gid="$3"
    local desc="$4"

    # Crear grupo si no existe con el GID indicado
    if ! getent group "$username" &>/dev/null; then
        groupadd --gid "$gid" "$username" 2>/dev/null || \
        groupadd "$username"  # sin GID fijo si ya está en uso
        log_ok "Grupo '${username}' creado"
    else
        log_warn "Grupo '${username}' ya existe — omitido"
    fi

    # Crear usuario si no existe
    if ! id "$username" &>/dev/null; then
        useradd \
            --uid "$uid" \
            --gid "$username" \
            --no-create-home \
            --shell /usr/sbin/nologin \
            --comment "$desc" \
            "$username" 2>/dev/null || \
        useradd \
            --gid "$username" \
            --no-create-home \
            --shell /usr/sbin/nologin \
            --comment "$desc" \
            "$username"
        log_ok "Usuario de sistema '${username}' creado (UID: $(id -u $username))"
    else
        log_warn "Usuario '${username}' ya existe (UID: $(id -u $username)) — omitido"
    fi
}

create_system_user "nodered"   "$NODERED_UID"   "$NODERED_GID"   "Node-RED service user"
create_system_user "influxdb"  "$INFLUXDB_UID"  "$INFLUXDB_GID"  "InfluxDB service user"

# Añadir el usuario de despliegue al grupo docker (para no necesitar sudo en docker)
if id "$DEPLOY_USER" &>/dev/null; then
    if ! groups "$DEPLOY_USER" | grep -q '\bdocker\b'; then
        usermod -aG docker "$DEPLOY_USER"
        log_ok "Usuario '${DEPLOY_USER}' añadido al grupo 'docker'"
        log_warn "Necesitarás cerrar sesión y volver a entrar para que el cambio surta efecto"
    else
        log_warn "Usuario '${DEPLOY_USER}' ya pertenece al grupo 'docker' — omitido"
    fi
else
    log_warn "Usuario de despliegue '${DEPLOY_USER}' no encontrado en el sistema"
fi

# ─────────────────────────────────────────────────────────────────────────────
# PASO 3: ESTRUCTURA DE DIRECTORIOS
# ─────────────────────────────────────────────────────────────────────────────
log_section "PASO 3 · Creando estructura de directorios"

# Lista de directorios a crear
DIRS=(
    "${BASE_DIR}/mosquitto/config"
    "${BASE_DIR}/mosquitto/data"
    "${BASE_DIR}/mosquitto/log"
    "${BASE_DIR}/influxdb_data"
    "${BASE_DIR}/node_red_data"
)

for dir in "${DIRS[@]}"; do
    if [[ -d "$dir" ]]; then
        log_warn "Ya existe: ${dir} — omitido"
    else
        mkdir -p "$dir"
        log_ok "Creado: ${dir}"
    fi
done

# ─────────────────────────────────────────────────────────────────────────────
# PASO 4: PERMISOS
# ─────────────────────────────────────────────────────────────────────────────
log_section "PASO 4 · Aplicando permisos"

# Node-RED: propietario = usuario nodered (UID 1000)
chown -R "$(id -u nodered):$(id -g nodered)" "${BASE_DIR}/node_red_data"
chmod -R 755 "${BASE_DIR}/node_red_data"
log_ok "node_red_data → propietario: nodered ($(id -u nodered):$(id -g nodered)), permisos: 755"

# InfluxDB: propietario = usuario influxdb (UID 1000)
chown -R "$(id -u influxdb):$(id -g influxdb)" "${BASE_DIR}/influxdb_data"
chmod -R 750 "${BASE_DIR}/influxdb_data"
log_ok "influxdb_data → propietario: influxdb ($(id -u influxdb):$(id -g influxdb)), permisos: 750"

# Mosquitto: el broker corre como UID 1883 dentro del contenedor oficial
# Pero las carpetas del host deben ser accesibles; usamos 1883 si existe o root+777
MOSQUITTO_CONTAINER_UID=1883
chown -R "${MOSQUITTO_CONTAINER_UID}:${MOSQUITTO_CONTAINER_UID}" \
    "${BASE_DIR}/mosquitto/data" \
    "${BASE_DIR}/mosquitto/log" 2>/dev/null || \
chown -R root:root \
    "${BASE_DIR}/mosquitto/data" \
    "${BASE_DIR}/mosquitto/log"

chmod -R 755 "${BASE_DIR}/mosquitto/data" "${BASE_DIR}/mosquitto/log"
log_ok "mosquitto/data y mosquitto/log → permisos: 755"

# El config lo mantenemos como root (solo lectura para el contenedor)
chmod -R 644 "${BASE_DIR}/mosquitto/config" 2>/dev/null || true
log_ok "mosquitto/config → permisos: 644"

# ─────────────────────────────────────────────────────────────────────────────
# PASO 5: FICHERO mosquitto.conf
# ─────────────────────────────────────────────────────────────────────────────
log_section "PASO 5 · Generando mosquitto.conf"

MOSQUITTO_CONF="${BASE_DIR}/mosquitto/config/mosquitto.conf"

if [[ -f "$MOSQUITTO_CONF" ]]; then
    log_warn "mosquitto.conf ya existe — se hace backup y se sobreescribe"
    cp "$MOSQUITTO_CONF" "${MOSQUITTO_CONF}.bak.$(date +%Y%m%d_%H%M%S)"
fi

cat > "$MOSQUITTO_CONF" << 'EOF'
# ─────────────────────────────────────────────────────────────────
# mosquitto.conf — TFM BeaglePlay IoT Stack
# ─────────────────────────────────────────────────────────────────

# Puerto de escucha MQTT estándar
listener 1883

# Acceso anónimo permitido (red local de cavidad, sin exposición exterior)
# IMPORTANTE: cambiar a false y añadir credenciales si la red no es de confianza
allow_anonymous true

# ── Persistencia ──────────────────────────────────────────────────
# Guarda las suscripciones y mensajes pendientes en disco
# para que sobrevivan a un reinicio del broker
persistence true
persistence_location /mosquitto/data/

# ── Logging ───────────────────────────────────────────────────────
log_dest file /mosquitto/log/mosquitto.log
log_type error
log_type warning
log_type notice
log_type information

# Tamaño máximo del log antes de rotar (en bytes) — 10 MB
# Evita que el log llene la SD en una BeaglePlay
#log_facility 5

# ── Límites de conexión ────────────────────────────────────────────
# Máximo de clientes simultáneos (ajustar según nodos desplegados)
max_connections 50

# Tiempo máximo sin actividad antes de desconectar un cliente (segundos)
keepalive_interval 60
EOF

log_ok "mosquitto.conf generado en: ${MOSQUITTO_CONF}"

# ─────────────────────────────────────────────────────────────────────────────
# PASO 6: VERIFICACIÓN FINAL
# ─────────────────────────────────────────────────────────────────────────────
log_section "PASO 6 · Verificación final"

echo ""
log_info "Estructura de directorios creada:"
echo ""

# Mostrar árbol si está disponible, sino find
if command -v tree &>/dev/null; then
    tree -pug "${BASE_DIR}/mosquitto" "${BASE_DIR}/influxdb_data" "${BASE_DIR}/node_red_data" 2>/dev/null || \
    find "${BASE_DIR}" -maxdepth 3 \( -path "*/mosquitto/*" -o -name "influxdb_data" -o -name "node_red_data" \) -ls
else
    find "${BASE_DIR}/mosquitto" "${BASE_DIR}/influxdb_data" "${BASE_DIR}/node_red_data" \
        -maxdepth 2 -exec ls -lad {} \; 2>/dev/null
fi

echo ""
log_info "Usuarios del sistema creados:"
for u in nodered influxdb; do
    if id "$u" &>/dev/null; then
        echo "  → ${u}: UID=$(id -u $u), GID=$(id -g $u), Shell=$(getent passwd $u | cut -d: -f7)"
    fi
done

# ─────────────────────────────────────────────────────────────────────────────
# RESUMEN FINAL
# ─────────────────────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BOLD}${GREEN}  ✔  Setup completado correctamente${NC}"
echo -e "${BOLD}${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo -e "  Siguiente paso → levanta el stack con:"
echo -e "  ${CYAN}cd ${BASE_DIR} && docker compose up -d${NC}"
echo ""
echo -e "  Para ver los logs en tiempo real:"
echo -e "  ${CYAN}docker compose logs -f${NC}"
echo ""