import socket
import time
import struct

# Configuración
UDP_IP = "0.0.0.0" # Escucha en todas las interfaces
UDP_PORT = 5005

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
sock.settimeout(2.0) # Si pasan 2 segundos sin datos, consideramos la prueba terminada

print(f"[*] Servidor benchmarking escuchando en puerto {UDP_PORT}...")
print("[*] Esperando ráfaga de datos SPE...")

expected_packets = 0
received_packets = 0
total_bytes = 0
start_time = 0
end_time = 0

while True:
    try:
        data, addr = sock.recvfrom(2048)
        
        if start_time == 0:
            start_time = time.time()
            print(f"[+] Prueba iniciada desde {addr[0]}")

        # Comprobar si es el paquete final
        if data == b"END":
            end_time = time.time()
            break

        # Leer el número de secuencia (los primeros 4 bytes del paquete)
        seq_num = struct.unpack('!I', data[:4])[0]
        
        # El número máximo de secuencia nos dice cuántos paquetes se enviaron en total
        expected_packets = max(expected_packets, seq_num)
        received_packets += 1
        total_bytes += len(data)
        end_time = time.time() # Actualizar tiempo por si se pierde el paquete "END"

    except socket.timeout:
        if start_time != 0:
            print("\n[-] Timeout alcanzado (fin de transmisión o se perdió el paquete final).")
            break

# --- CÁLCULO DE MÉTRICAS ---
duration = end_time - start_time

if duration > 0 and expected_packets > 0:
    megabits = (total_bytes * 8) / (1024 * 1024)
    throughput = megabits / duration
    lost_packets = expected_packets - received_packets
    loss_percent = (lost_packets / expected_packets) * 100

    print("\n" + "="*45)
    print("📊 RESULTADOS DEL BENCHMARK SPE")
    print("="*45)
    print(f"⏱️  Tiempo de prueba:    {duration:.2f} segundos")
    print(f"📦 Paquetes recibidos:  {received_packets} de {expected_packets}")
    print(f"❌ Paquetes perdidos:   {lost_packets} ({loss_percent:.2f}%)")
    print(f"💾 Datos transferidos:  {total_bytes / (1024*1024):.2f} MB")
    print(f"🚀 Rendimiento (Speed): {throughput:.2f} Mbps")
    print("="*45)
    
    print("\n📌 CONCLUSIÓN AUTOMÁTICA:")
    if loss_percent > 5.0:
        print("⚠️ El enlace SPE presenta ALTA PÉRDIDA de paquetes. Revisa la calidad del cable, los conectores o posibles interferencias electromagnéticas.")
    elif throughput < 4.0:
        print("⚠️ La pérdida de paquetes es buena, pero la VELOCIDAD ES BAJA para el estándar 10BASE-T1L. Puede haber un cuello de botella en la CPU o buffers de red.")
    else:
        print("✅ EXCELENTE RENDIMIENTO. El enlace SPE opera cerca de su capacidad teórica (10 Mbps) con alta fiabilidad en la transmisión física.")
else:
    print("No se recibieron datos suficientes para el cálculo.")