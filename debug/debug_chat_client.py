#!/usr/bin/env python3
"""Interactive console client that connects directly to the Retrace TCP server."""

import argparse
import getpass
import socket
import sys
import threading


def receive_lines(sock: socket.socket, stop: threading.Event) -> None:
    pending = b""
    try:
        while not stop.is_set():
            chunk = sock.recv(4096)
            if not chunk:
                print("\n[connection closed by server]")
                break
            pending += chunk
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                print("\n< " + line.rstrip(b"\r").decode("utf-8", errors="replace"))
    except OSError as exc:
        if not stop.is_set():
            print(f"\n[receive error: {exc}]")
    finally:
        stop.set()


def main() -> int:
    parser = argparse.ArgumentParser(description="Connect directly to a Retrace TCP server.")
    parser.add_argument("host", help="Jetson/server IP address or hostname")
    parser.add_argument("-p", "--port", type=int, default=5000, help="TCP port (default: 5000)")
    args = parser.parse_args()

    client_id = input("Client ID: ").strip()
    password = getpass.getpass("Password: ")
    if not client_id or any(ch in client_id for ch in "]: \r\n"):
        print("Client ID must be non-empty and cannot contain ], colon, or whitespace.", file=sys.stderr)
        return 2
    if not password or any(ch in password for ch in "]\r\n"):
        print("Password must be non-empty and cannot contain ] or a newline.", file=sys.stderr)
        return 2

    stop = threading.Event()
    try:
        with socket.create_connection((args.host, args.port), timeout=10) as sock:
            sock.settimeout(None)
            # The current server reads this legacy login token up to the closing bracket.
            sock.sendall(f"[{client_id}:{password}]".encode("utf-8"))
            receiver = threading.Thread(target=receive_lines, args=(sock, stop), daemon=True)
            receiver.start()
            print("Connected. Type a protocol command or text and press Enter; /quit exits.")
            print("Messages are sent directly to the server as [SERVER]<your input>.")
            while not stop.is_set():
                try:
                    message = input("> ")
                except (EOFError, KeyboardInterrupt):
                    print()
                    break
                if message.strip().lower() in {"/quit", "/exit"}:
                    break
                if not message:
                    continue
                if "\n" in message or "\r" in message:
                    print("Please enter one line at a time.")
                    continue
                sock.sendall(("[SERVER]" + message + "\n").encode("utf-8"))
    except (OSError, UnicodeError) as exc:
        print(f"Connection failed: {exc}", file=sys.stderr)
        return 1
    finally:
        stop.set()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
