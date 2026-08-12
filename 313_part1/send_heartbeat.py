import argparse
import socket
import time


def main() -> None:
    parser = argparse.ArgumentParser(description="Send heartbeat packets for the virtual arm.")
    parser.add_argument("--ip", default="127.0.0.1", help="Target IP address")
    parser.add_argument("--port", type=int, default=9999, help="Target UDP port")
    parser.add_argument("--interval-ms", type=int, default=100, help="Heartbeat interval in milliseconds")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.ip, args.port)

    print(f"Sending heartbeat to {args.ip}:{args.port} every {args.interval_ms} ms")
    try:
        while True:
            sock.sendto(b"ACK:OK", target)
            time.sleep(max(args.interval_ms, 1) / 1000.0)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()


if __name__ == "__main__":
    main()
