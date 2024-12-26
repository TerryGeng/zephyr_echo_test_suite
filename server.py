import socket
import logging
import time
from collections import deque

port = 4242

recvd_size_queue = deque(maxlen=500)
timestamp_queue  = deque(maxlen=500)

ctr = 0


logging.basicConfig(level=logging.DEBUG,
                    format='%(asctime)s: '
                    '%(message)s')

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind(('', port))
    s.listen(1)

    logging.info("Listening at *:4242")

    s.settimeout(None)

    try:
        while True:
            ctr = 0
            recvd_size_queue.clear()
            timestamp_queue.clear()

            s.settimeout(None)
            conn, addr = s.accept()

            with conn:
                logging.info(f"Connected from {addr}")
                try:
                    while True:
                        conn.settimeout(10)
                        data = conn.recv(128)

                        if not data:
                            break

                        recvd_size_queue.append(len(data))
                        timestamp_queue.append(time.time())

                        ctr += 1

                        conn.sendall(data)

                        if ctr % 500 == 0:
                            speed = sum(recvd_size_queue) / (
                                    timestamp_queue[-1] - timestamp_queue[0])

                            logging.info(f"Data receiving rate: {speed/1000:.1f} kB/s")
                except TimeoutError:
                    conn.close()
                    logging.info("Timeout. Close connection.")
    finally:
        s.close()
