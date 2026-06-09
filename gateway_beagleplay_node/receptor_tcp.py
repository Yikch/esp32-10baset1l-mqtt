import socket

TCP_IP = '0.0.0.0' 
TCP_PORT = 5000

def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((TCP_IP, TCP_PORT))
        s.listen()
        print(f"[*] Servidor TCP iniciado.")
        print(f"[*] Escuchando en el puerto {TCP_PORT}...")
        print("-" * 40)
        
        while True:
            conn, addr = s.accept() 
            print(f"\n[+] Nueva conexión establecida desde: {addr[0]}")
            
            with conn:
                while True:
                    data = conn.recv(1024)
                    
                    # Si 'data' está vacío, significa que la BeaglePlay se ha desconectado
                    if not data:
                        print(f"[-] Conexión cerrada por el cliente {addr[0]}")
                        break 
                    
                    # Decodificamos y mostramos el mensaje
                    mensaje = data.decode('utf-8').strip()
                    print(f"[{addr[0]}] Dato recibido -> {mensaje}")

if __name__ == '__main__':
    main()