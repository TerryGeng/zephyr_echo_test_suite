import socket
import logging
import time
from collections import deque

port = 4242

recvd_size_queue = deque(maxlen=500)
timestamp_queue  = deque(maxlen=500)

ctr = 0

peer_ip = '192.168.11.2'
peer_port = 4242

data = b"""Sockets are used nearly everywhere, but are one of the most severely misunderstood technologies around. This is a 10,000 foot overview of sockets. It's not really a tutorial - you'll still have work to do in getting things operational. It doesn't cover the fine points (and there are a lot of them), but I hope it will give you enough background to begin using them decently."""
b""" I'm only going to talk about INET (i.e. IPv4) sockets, but they account for at least 99% of the sockets in use. And I'll only talk about STREAM (i.e. TCP) sockets - unless you really know what you're doing (in which case this HOWTO isn't for you!), you'll get better behavior and performance from a STREAM socket than anything else. I will try to clear up the mystery of what a socket is, as well as some hints on how to work with blocking and non-blocking sockets. But I'll start by talking about blocking sockets. You'll need to know how they work before dealing with non-blocking sockets."""
b"""Part of the trouble with understanding these things is that "socket" can mean a number of subtly different things, depending on context. So first, let's make a distinction between a "client" socket - an endpoint of a conversation, and a "server" socket, which is more like a switchboard operator. The client application (your browser, for example) uses "client" sockets exclusively; the web server it's talking to uses both "server" sockets and "client" sockets.
"""


logging.basicConfig(level=logging.DEBUG,
                    format='%(asctime)s: '
                    '%(message)s')

logging.info(f"Connecting to {peer_ip}:{peer_port}")

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
    s.connect((peer_ip, peer_port))

    s.settimeout(5)
    s.setblocking(False)

    ctr = 0

    recvd_size_queue.clear()
    timestamp_queue.clear()

    datalag = 0

    try:
        while True:
            tot_sendsize = 0

            while tot_sendsize < len(data):
                try:
                    sendsize = s.send(data[tot_sendsize:])
                    tot_sendsize += sendsize
                    datalag += sendsize
                except BlockingIOError:
                    continue

                while True:
                    try:
                        recvbuf = s.recv(4096)

                        datalag -= len(recvbuf)

                        recvd_size_queue.append(len(recvbuf))
                        timestamp_queue.append(time.time())

                        ctr += 1

                        if ctr and ctr % 500 == 0:
                            speed = sum(recvd_size_queue) / (
                                    timestamp_queue[-1] - timestamp_queue[0])

                            logging.info(f"Data exchange rate: {speed/1000:.1f} kB/s")
                    except BlockingIOError:
                        if datalag > 2048*2:
                            continue
                        else:
                            break

    except TimeoutError:
        s.close()
        logging.info("Timeout. Close connection.")
    finally:
        s.close()
