#!/usr/bin/env python3
#
#   Test server for EspcpTests tcp.idle_receive_survives_poll_timeout.
#
#   Accepts connections and keeps them open for as long as the client likes, however long they are idle
#   (the bench web server closes an idle connection after 60 s, which is the poll timeout).  When a
#   request arrives it answers with a short HTTP response and closes.  Run it on a machine on the
#   bench network and set IdleServer in MeadowApp.cs to that machine's address:
#
#       python3 tests/managed/EspcpTests/idle_server.py [port]
#
import socket
import sys
import threading

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8081
RESPONSE = b"HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 3\r\n\r\nok\n"


def serve(connection):
    try:
        connection.recv(4096)
        connection.sendall(RESPONSE)
    except OSError:
        pass
    finally:
        connection.close()


listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
listener.bind(("", PORT))
listener.listen(16)
print(f"idle server listening on port {PORT}", flush=True)
while True:
    client, _ = listener.accept()
    threading.Thread(target=serve, args=(client,), daemon=True).start()
