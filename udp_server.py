import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", 8888))

print("Listening on 127.0.0.1:8888...")

while True:
    data, addr = sock.recvfrom(1024)
    print(data.decode("utf-8", errors="replace"), addr)