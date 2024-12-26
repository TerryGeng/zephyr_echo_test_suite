import select
import socket
import logging
import time
from collections import deque

port = 4242

class EchoClientState:
    def __init__(self):
        self.recvd_size_queue = deque(maxlen=500)
        self.timestamp_queue  = deque(maxlen=500)
        self.list_report_time = time.time()


logging.basicConfig(level=logging.DEBUG,
                    format='%(asctime)s: '
                    '%(message)s')


sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

sock.bind(('', port))
sock.listen(1)

logging.info("Listening at *:4242")

sock.settimeout(None)

clients = {}

read_list = [sock]

try:
    while True:
        readable, writable, errored = select.select(read_list, [], [])

        to_remove = []

        for s in readable:
            if s is sock:
                conn, addr = sock.accept()
                conn.settimeout(10)
                read_list.append(conn)
                clients[conn.fileno()] = EchoClientState()
                clients[conn.fileno()].last_report_time = time.time()
                logging.info(f"Socket {conn.fileno()}: connection from {addr}")
            else:
                try:
                    data = s.recv(1024)

                    if not data:
                        break

                    clients[s.fileno()].recvd_size_queue.append(len(data))
                    clients[s.fileno()].timestamp_queue.append(time.time())

                    s.sendall(data)
                except Exception as e:
                    logging.error(f"Socket {s.fileno()}: closed due to exception")
                    s.close()
                    read_list.remove(s)
                    to_remove.append(s)
                    logging.exception(e)
                    continue


                if time.time() - clients[s.fileno()].last_report_time > 5:
                    clients[s.fileno()].last_report_time = time.time()
                    speed = sum(clients[s.fileno()].recvd_size_queue) / (
                            clients[s.fileno()].timestamp_queue[-1] - clients[s.fileno()].timestamp_queue[0])

                    logging.info(f"Socket {s.fileno()}: data exchange rate: {speed/1000:.1f} kB/s")

        for s, client in clients.items():
            if client and client.timestamp_queue and (time.time() - client.timestamp_queue[-1] > 10):
                logging.info(f"Socket {s.fileno()}: timeout")
                s.close()
                read_list.remove(s)
                to_remove.append(s)

        for s in to_remove:
            del clients[s]

finally:
    [ s.close() for s in read_list ]
