import socket
import logging
import time
from collections import deque

class EchoClientState:
    def __init__(self):
        self.recvd_size_queue = deque(maxlen=500)
        self.timestamp_queue  = deque(maxlen=500)
        self.list_report_time = time.time()

port = 4242

recvd_size_queue = deque(maxlen=500)
timestamp_queue  = deque(maxlen=500)

clients = {}

logging.basicConfig(level=logging.DEBUG,
                    format='%(asctime)s: '
                    '%(message)s')

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
    s.bind(('', port))

    logging.info("Listening at *:4242")

    s.settimeout(None)

    ctr = 0
    recvd_size_queue.clear()
    timestamp_queue.clear()

    try:
        while True:
            data, client_address = s.recvfrom(2048)

            if not data:
                break

            if client_address not in clients:
                logging.info(f"Client {client_address} incoming")
                clients[client_address] = EchoClientState()
                clients[client_address].last_report_time = time.time()

            client = clients[client_address]

            if client.timestamp_queue and time.time() - client.timestamp_queue[-1] > 6:
                client.recvd_size_queue.clear()
                client.timestamp_queue.clear()

            client.recvd_size_queue.append(len(data))
            client.timestamp_queue.append(time.time())

            s.sendto(data, client_address)

            if time.time() - clients[client_address].last_report_time > 5:
                clients[client_address].last_report_time = time.time()
                speed = sum(client.recvd_size_queue) / (
                        client.timestamp_queue[-1] - client.timestamp_queue[0])

                logging.info(f"Client {client_address} data exchange rate: {speed/1000:.1f} kB/s")
    finally:
        s.close()
