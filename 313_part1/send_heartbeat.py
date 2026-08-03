import socket
import time

target_ip = "127.0.0.1"
target_port = 9999

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

try:
    while True:
        sock.sendto(b"ACK:OK", (target_ip, target_port))
        print("sent ACK:OK")
        time.sleep(0.1)  # 100ms, 10Hz
except KeyboardInterrupt:
    print("heartbeat stopped")
finally:
    sock.close()